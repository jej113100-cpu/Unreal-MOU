# MOU 서버 통합 문서

담당: 서버/클라이언트 파트
최종 갱신: 2026-08-21

이 문서 하나만 보면 **새 PC 에서 처음부터 셋업하고, 빌드하고, 검증하고, 이어서 개발**할 수 있다.
채팅뿐 아니라 계정(로그인)·로비(방 목록)까지 이 문서 하나로 다룬다.
(예전 이름은 `CHAT_INTEGRATION.md` 였는데, 채팅 말고도 다루는 범위가 넓어져서 이름을 바꿨다.)

> **"왜 이 구조인가"가 궁금하면 [14절](#14-설계-근거--외부-피드백에-대한-답변)부터 읽으면 된다.**
> 별도 프로세스를 둔 이유, EOS·Steam 대신 직접 구현한 이유, NAT 한계, 앞으로의 전환 계획을 정리해뒀다.

---

## 목차

1. [지금까지 뭘 만들었나](#1-지금까지-뭘-만들었나)
2. [새 PC 셋업 (처음 한 번)](#2-새-pc-셋업-처음-한-번)
3. [빌드하기](#3-빌드하기)
4. [동작 확인](#4-동작-확인)
5. [구조](#5-구조)
6. [파일별 역할](#6-파일별-역할)
7. [API 레퍼런스](#7-api-레퍼런스)
8. [프로토콜 레퍼런스](#8-프로토콜-레퍼런스)
9. [팀 공용 파일 변경 내역](#9-팀-공용-파일-변경-내역)
10. [이 시스템의 코딩 규칙](#10-이-시스템의-코딩-규칙)
11. [알려진 한계](#11-알려진-한계)
12. [다음 단계](#12-다음-단계)
13. [문제 해결](#13-문제-해결)
14. [설계 근거 — 외부 피드백에 대한 답변](#14-설계-근거--외부-피드백에-대한-답변)
15. [회고 — UPnP 와 서버 주소](#15-회고--upnp-와-서버-주소-2026-08-25--08-27)

---

## 1. 지금까지 뭘 만들었나

게임 본체는 **리슨서버** 방식이지만, 채팅·계정·로비(방 목록)는 **별도 프로세스(데디케이트)** 로 분리했다.
호스트가 게임을 나가도 이 서버는 살아있으므로 대화가 끊기지 않고, 계정도 유지되고, 방 목록도 관리된다.

**중요**: 로비 서버는 방 "주소록" 역할만 한다. 실제 게임 트래픽(이동/전투/GAS)은 이 서버를
전혀 거치지 않고 참가자가 호스트의 리슨서버에 직접 붙는다. 그래서 방이 몇 개 열려도
서버 부하가 거의 늘지 않는다.

| 단계 | 내용 | 상태 |
|---|---|---|
| 1 | 프로토콜 정의 (`ChatProtocol.h`) | 완료 (**v7**, 2026-08-24) |
| 2 | 세션 구조체 (`Session.h/.cpp`) | 완료 |
| 3 | 길이 프리픽스 프레이밍 (`Framing.h/.cpp`) | 완료 (split/merge/bad 3종 통과) |
| 4 | 로그인 — 계정(아이디/비밀번호) 인증 | **완료** (PBKDF2 해시, UserId 영속) |
| 5 | 채널 라우팅 (All / Team / Dead) | 완료 |
| 6 | SQLite 채팅 로그 적재 | **완료** (비동기 큐, 히스토리 조회는 미착수) |
| 7 | 언리얼 클라이언트 (소켓/스레드/서브시스템) | 완료 |
| 7-UI | 언리얼 채팅 UI 위젯 | 완료 (WBP 불필요) |
| 7-계정 | 로그인 / 계정 생성 UI 위젯 | 완료 (WBP 불필요) |
| 로비 | 방 생성 / 목록 / 참여 (서버 + 클라이언트 API) | **완료** |
| 로비-UI | 메인메뉴 / 방 생성창 / 방 목록·참여창 | **완료** (WBP 불필요) |
| 대기실 | 방 멤버 추적 / 준비완료 / 게임시작 / 방장 이탈 처리 | **완료** (v5) |
| 로비-여행 | 방장 `OpenLevel(listen)` / 참여자 `ClientTravel` | 참여자 쪽 **완료** (v6, 호스트 준비 신호). 방장 쪽은 맵 이름 미정 |
| 로비-백엔드 | `ILobbyBackend` 로 계정/세션 탐색 분리 (EOS 전환 대비) | **완료** (v6). EOS 구현체는 뼈대 |
| 로비-보안 | `PreLogin` 에서 방 비밀번호 재검사 | **미착수** ← 진짜 관문 |
| NAT | UPnP 자동 포트포워딩 (로그인 서버 9000 + 방장 리슨서버 7777) | **완료** (2026-08-25). CGNAT 은 여전히 안 됨 — 14-5절 |
| 8 | 리슨서버 → 채팅서버 신원 미러링 | **폐기** (2026-08-24) — 아래 참고 |
| 9 | 귓속말 | 형태가 바뀌어 완료 — **v7 친구/메신저**로 흡수됨 |
| 친구 | 친구 신청/수락/삭제, 접속 상태(Presence) | **완료** (v7). `Friends.cpp`, `TestClient` `/add /accept /decline /unfriend` |
| 메신저 | 1:1 DM 송수신·오프라인 보관·기록 조회·읽음 처리 | **완료** (v7). `DirectMessages.cpp` |
| 메신저-UI | 친구 목록 패널 + 대화창 (언리얼) | **완료**. `UFriendListWidgetBase`, `UMessengerWidgetBase` (WBP 불필요) |
| 인게임 채팅 | 죽은사람끼리만 쓰는 채팅 (리슨서버 RPC, 서버 미경유) | **설계만 완료, 미착수**. `Server/Chat/InGame/` 에 화면 코드(구 `ChatWidgetBase`) 보관 중 |

> **8단계(리슨서버 → 채팅서버 신원 미러링)가 왜 폐기됐나**: 대기실 상태 미러링과
> 혼동하기 쉬운데 별개다. 이건 "누가 죽었는가"를 채팅 서버에 알려 `Dead` 채널을
> 쓰려던 계획이었다. 리슨서버가 외부 서버에 인증 연결을 하나 더 유지해야 하고,
> 그 연결이 끊기면 게임 내 채팅이 죽는 새 실패 모드가 생겨서 포기했다. 대신
> 죽은사람 채팅은 **외부 서버를 아예 안 타고** 리슨서버 RPC 로만 처리하기로
> 방향을 바꿨다 — `EChatChannel::Dead` 옵코드는 프로토콜에 번호만 남기고 더
> 안 쓴다. 자세한 논거는 `CHAT_DESIGN.md` 3절.

> 단계 번호는 원래 `MOU_Server/README.md` 의 로드맵을 따르되, 계정·로비처럼
> 로드맵에 없던 작업은 별도 이름을 붙였다.

### 오늘(2026-08-10) 처리한 문제 3가지

6단계 착수 전에 코드 리뷰에서 나온 구멍 세 개를 먼저 막았다.

1. **재접속 시 미전송 패킷 유실.** 끊기기 직전 큐에 남아있던 패킷이 재접속 후 `LoginReq` 보다
   먼저 나가 서버가 조용히 버리던 문제. 재접속 성공 직후 `OutboundPackets` 를 비우도록 수정.
2. **프로토콜 버전 핸드셰이크 부재.** 서버와 클라이언트 버전이 달라도 조용히 재접속 루프만
   반복하던 문제. `LoginReqBody::Version` / `LoginAckBody::Result,ServerVersion` 추가 (v1→v2).
3. **PIE 다중 창에서 채팅 UI 가 첫 창에만 뜸.** 전역 위젯 포인터를
   `TMap<TWeakObjectPtr<UWorld>, ...>` 로 바꿔 창마다 독립적으로 뜨도록 수정.

---

## 2. 새 PC 셋업 (처음 한 번)

### 2-1. 필요한 것

| 항목 | 버전 | 비고 |
|---|---|---|
| Unreal Engine | **5.8** | `.uproject` 의 `EngineAssociation` 이 5.8 |
| Visual Studio | 2022 또는 2026 | **둘 다 설치돼 있으면 아래 2-3 필수** |
| VS 워크로드 | C++ 게임 개발 / C++ 데스크톱 개발 | MSVC 툴셋 + Windows SDK |
| Git | 아무거나 | |

### 2-2. 저장소 받기

```bash
git clone <저장소 URL> Unreal-MOU
```

> **`TeamProject_MOU` 폴더만 따로 복사하면 빌드가 깨진다.**
> `Build.cs` 가 저장소 루트의 `MOU_Server/Shared` 를 참조하기 때문이다. 반드시 전체를 받을 것.

받은 뒤 저장소 구조:

```
Unreal-MOU/
├─ SERVER_INTEGRATION.md        이 문서
├─ MOU_Server/                  채팅/계정/로비 서버 (엔진 없는 순수 C++)
│   ├─ CMakeLists.txt
│   ├─ README.md                서버 자체 설계 문서
│   ├─ Shared/                  서버·클라이언트 공용
│   │   ├─ ChatProtocol.h       ★ 언리얼도 이 파일을 그대로 include 한다
│   │   ├─ Net.h                소켓 API 래퍼 (언리얼에서 include 금지)
│   │   └─ Framing.h/.cpp       프레이밍 (언리얼에서 include 금지)
│   ├─ ThirdParty/sqlite/       SQLite 3.47.1 앰알가메이션 (외부 코드, 수정 금지)
│   ├─ Server/
│   │   ├─ Server.cpp           accept 루프 / 패킷 핸들러 / 채널 라우팅
│   │   ├─ Session.h/.cpp       ClientSession + SessionManager
│   │   ├─ Accounts.h/.cpp      계정 DB (아이디/비밀번호/닉네임). 동기 커밋
│   │   ├─ Crypto.h/.cpp        SHA-256 / PBKDF2 / HMAC 자체 구현 (외부 의존성 없음)
│   │   ├─ ChatLog.h/.cpp       채팅 로그 DB. 비동기 큐 + 라이터 스레드
│   │   ├─ Rooms.h/.cpp         방 레지스트리 (메모리만, 휘발성)
│   │   ├─ Friends.h/.cpp       친구 관계 DB. Accounts 와 같은 동기 커밋 (v7)
│   │   └─ DirectMessages.h/.cpp 1:1 메시지 DB. 동기 커밋, 오프라인 보관 (v7)
│   └─ TestClient/
│       └─ TestClient.cpp       검증용 콘솔 클라이언트 (계정/로비 명령 포함)
└─ TeamProject_MOU/             언리얼 프로젝트
    └─ Source/TeamProject_MOU/
        ├─ TeamProject_MOU.Build.cs
        ├─ Public/Server/       외부 서버와 말하는 것 전부 (헤더)
        │    ├─ Net/              프레이밍 · 소켓 워커
        │    ├─ Lobby/            로그인 · 방 · 대기실
        │    ├─ Chat/             로비 채팅(메신저) · 인게임 채팅
        │    └─ Social/           친구 · 접속 상태
        └─ Private/Server/      같은 구조의 구현
```

> **`Chat/` 이 아니라 `Server/` 인 이유** (2026-08-24 개편): 이 폴더에는 처음부터
> 채팅뿐 아니라 로그인·방·대기실이 같이 들어 있었다. 공통점은 "채팅" 이 아니라
> **"`Server.exe` 와 TCP 로 말하는 코드"** 다. 이름을 내용에 맞추고, 그 아래를
> 기능별로 나눴다. 자세한 것은 `Public/Server/Chat/CHAT_DESIGN.md`.

### 2-3. Visual Studio 버전 고정 ★ 중요

VS 2022 와 2026 이 **둘 다 설치돼 있으면**, UBT 가 자동으로 최신(2026)용 프로젝트 파일을 만든다.
그 상태에서 VS 2022 로 열면 솔루션 탐색기에 이렇게 뜬다:

> **TeamProject_MOU(호환되지 않음)** — 이 프로젝트는 현재 버전의 Visual Studio와 호환되지 않습니다.

VS 2022 는 2026 의 툴셋(`v145`, `ToolsVersion 18.0`)을 모르기 때문이다.

**해결**: `%APPDATA%\Unreal Engine\UnrealBuildTool\BuildConfiguration.xml` 을 아래처럼 만든다.
이 파일은 **사용자별 설정**이라 git 에 들어가지 않는다. PC 마다 각자 해야 한다.

```xml
<?xml version="1.0" encoding="utf-8" ?>
<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">
  <ProjectFileGenerator>
    <Format>VisualStudio2022</Format>
  </ProjectFileGenerator>
  <BuildConfiguration>
    <bAllowUBAExecutor>false</bAllowUBAExecutor>
  </BuildConfiguration>
</Configuration>
```

- `Format` — 쓰는 VS 에 맞춰 `VisualStudio2022` 또는 `VisualStudio2026`.
  이걸 넣어야 우클릭 → *Generate Visual Studio project files* 를 눌러도 계속 그 버전으로 나온다.
- `bAllowUBAExecutor` — 빌드 가속기(UBA) 끄기. **13번 문제 해결** 참고. 메모리가 넉넉하면 빼도 된다.

### 2-4. 프로젝트 파일 생성

`TeamProject_MOU.uproject` 우클릭 → **Generate Visual Studio project files**

또는 커맨드라인 (엔진 경로는 각자 다름):

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="C:\...\Unreal-MOU\TeamProject_MOU\TeamProject_MOU.uproject" -game -rocket -progress
```

> 새 `.cpp`/`.h` 를 추가하거나 `git pull` 로 받은 뒤에는 **반드시 다시 실행**해야 VS 에 파일이 보인다.

---

## 3. 빌드하기

### 3-1. 서버 (`Server.exe`)

엔진과 무관한 순수 C++ 콘솔 프로그램이다.

**가장 간단한 방법** — VS 설치 경로를 알아서 찾아 `Server_Build\Server.exe` 를 만든다:

```bash
MOU_Server\build_server.bat
```

> 이 스크립트는 예전에 VS 경로를 `2022\Community` 로 하드코딩하고 있었다. VS 2026 만
> 깔린 PC 에서는 `cl` 을 못 찾고 **조용히** 실패해서, 옛 `Server.exe` 가 그대로 남아
> "NAT 코드를 넣었는데 왜 안 되지" 를 하루 헤매게 만들었다. 지금은 설치 경로를 후보
> 목록에서 찾고, 빌드가 실패하면 `[ERROR]` 를 찍고 `exit /b 1` 로 멈춘다.

**CMake 로 빌드해도 된다** (SQLite 앰알가메이션이 C 파일이라 `project()` 에 `C` 언어가
켜져 있어야 한다). `TestClient.exe` 까지 필요하면 이쪽을 쓴다.

```bash
cmake -S MOU_Server -B MOU_Server/out/build/x64-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build MOU_Server/out/build/x64-Debug
```

Visual Studio 개발자 명령 프롬프트(`vcvars64.bat` 실행 후) 안에서 돌려야
MSVC 툴체인을 찾는다. VS 에 번들된 CMake/Ninja 를 쓰면 별도 설치가 필요 없다:

```
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe
```

빌드 결과물 두 개가 나온다: `Server.exe`, `TestClient.exe`.

### 3-2. 언리얼 프로젝트

**VS 에서**: 구성을 `Development Editor` / `Win64` 로 두고 빌드 (Ctrl+Shift+B).
시작 프로젝트가 `UE5`(엔진)로 잡혀 있으면 `TeamProject_MOU` 로 바꾼다.

**커맨드라인에서**:

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" TeamProject_MOUEditor Win64 Development -Project="C:\...\Unreal-MOU\TeamProject_MOU\TeamProject_MOU.uproject" -WaitMutex
```

`Variant_Platforming does not exist` 류의 경고가 잔뜩 뜨는데, 이건 원래 템플릿에 있던
`Build.cs` 의 남은 경로라서 **무시해도 된다.**

---

## 4. 동작 확인

### 4-1. 기본 흐름 (로그인 → 채팅)

**1) 서버를 먼저 켠다** (별도 콘솔 창)

```bash
MOU_Server\run_server.bat
```

이걸 쓰는 것을 권한다. 그냥 `Server.exe 9000` 으로도 뜨지만, `run_server.bat` 은 켜기 전에
**남이 못 붙는 흔한 원인 세 가지**를 먼저 확인해준다:

- `Server.exe` 가 NAT 기능이 있는 최신 빌드인가 (옛 빌드면 멈추고 알려준다)
- 이 PC 의 LAN IP / 게이트웨이 / **공인 IP**
- `DefaultGame.ini` 의 `ServerHost` 가 실제 공인 IP 와 **일치하는가** (다르면 `[WARN]`)

직접 띄우려면:

```bash
Server_Build\Server.exe 9000 Server_Build\chat_log.db
```

(계정과 채팅 로그가 같은 파일에 테이블만 나눠서 들어간다.)

**다른 네트워크(각자 집)에서 접속시키려면** 두 가지가 다 되어 있어야 한다 —
**서버 쪽 공유기의 포트포워딩**과, **클라이언트가 공인 IP 를 보게 하는 것**이다.
UPnP 는 앞의 하나만 자동화해줄 뿐, 뒤의 하나는 대신 해주지 않는다.

- 공유기가 UPnP 를 지원하면 `--upnp` 로 자동화된다: `run_server.bat --upnp` (2026-08-25).
  시작 시 콘솔에 `[NAT] 외부에서는 <공인IP>:<포트> 로 접속하면 된다` 가 찍힌다.
- `[NAT] 포트를 열지 못했다: ...` 가 뜨면 공유기 관리 페이지에서 **수동 포트포워딩**을 넣는다:
  `외부 TCP 9000 -> 서버 PC 의 LAN IP:9000`
- 어느 쪽이든 `Config/DefaultGame.ini` 의 `ServerHost` 를 **공인 IP** 로 맞춰 커밋해야
  팀원 클라이언트가 그리로 붙는다.

실패해도(UPnP 미지원/CGNAT) 서버는 그대로 뜬다 — 같은 네트워크 접속만 가능한 상태로 남을 뿐이다.
같은 공유기 안에서만 테스트할 거면 `--upnp` 도 포트포워딩도 필요 없다.
안 될 때의 판별 순서는 [13절](#다른-네트워크의-팀원이-로그인을-못-한다) 에 정리해뒀다.

**2) 에디터에서 PIE 실행 → `` ` `` 키로 콘솔을 열고, 로그인 UI 를 띄운다**

```bash
MOU.Chat.ShowLogin 127.0.0.1 9000
```

화면 중앙에 로그인 창이 뜬다. 아이디/비밀번호를 입력하고 **계정 만들기** → **로그인**.
(콘솔 명령으로 직접 하려면 `MOU.Chat.Connect` → `MOU.Chat.Register` → `MOU.Chat.Login` 순서.)

**3) 로그인에 성공하면 로그인 창이 사라지고 채팅창이 자동으로 뜬다.**

**4) 확인 지점**

| 어디 | 나와야 하는 것 |
|---|---|
| 서버 콘솔 | `[가입] player1 (닉네임 홍길동) -> UserId=1` |
| 서버 콘솔 | `[로그인] 홍길동 -> UserId=1, Team=0` |
| 채팅창 | `%s(으)로 접속했습니다.` |
| 에디터 출력 로그 | 필터 `LogMOUServer` 로 같은 내용 |

**서버가 꺼져 있어도 게임은 정상 진행된다.** 3초 간격으로 자동 재접속하고,
재접속에 성공하면 **자동으로 다시 로그인**한다 (계정 인증 실패로 거부된 경우는 재시도하지 않는다).

**같은 계정으로 다시 로그인하면 UserId 가 그대로 유지된다.** (예전에는 접속마다 새 번호를
받았는데, 계정 시스템 도입으로 `UserId = accounts.id` 가 되어 재접속·서버 재시작에도 유지된다.)

### 4-2. 로비(방) 흐름 — UI

**로그인에 성공하면 로비 메인메뉴가 자동으로 뜬다.**
(`ULoginWidgetBase::bShowLobbyWidgetOnSuccess` 가 기본 켜짐. 콘솔로 직접 띄우려면 `MOU.Lobby.Show`)

**메인메뉴**

| 버튼 | 하는 일 |
|---|---|
| **방 만들기** | 방 생성창을 연다. 제목(필수) + 비번 숫자 4자리(선택, 비우면 공개방) |
| **참여하기** | 방 목록창을 연다. 3초마다 자동 새로고침, 비번 방은 4자리를 물어본 뒤 참여 |
| **게임 종료** | `QuitGame` |

로그인 전에는 앞의 두 버튼이 잠긴다 (눌러봐야 서버가 `NotAuthed` 로 거부한다).

**방을 만들거나 참여하면 같은 화면이 대기실로 바뀐다.** 버튼 세 개의 라벨만 달라진다.

| 버튼 자리 | 참여자 | 방장 |
|---|---|---|
| 1번 | **준비하기** / 준비 해제 | **게임 시작** (전원 준비 시에만 켜짐) |
| 2번 | 커스터마이징 *(미구현)* | 커스터마이징 *(미구현)* |
| 3번 | **나가기** | **나가기** |

- 대기실에는 **명단이 보인다.** `★ 방장` / `● 준비완료` / `○ 대기중`
- 준비 상태는 **서버가 진실을 갖는다.** 버튼을 눌러도 화면을 먼저 바꾸지 않고,
  서버가 갱신된 명단을 돌려주면 그때 라벨이 바뀐다. 두 클라이언트가 다른 그림을 볼 일이 없다.
- **나가기는 게임 종료가 아니다.** 방에서 빠져나와 메인메뉴로 돌아간다.

**방장이 나가면 방이 사라진다 — 이양하지 않는다.**
호스트가 곧 리슨서버라서, 호스트 프로세스가 죽으면 게임 세션 자체가 없어진다.
남은 사람을 새 호스트로 세우려면 리슨서버를 다시 열고 전원이 새 주소로 재접속해야
하는데 UE 가 이를 기본 지원하지 않는다. 그래서 서버가 남은 멤버에게 `RoomClosed` 를
보내고, 그들은 "방장이 나가서 방이 사라졌습니다" 안내와 함께 메인메뉴로 돌아간다.
**정상 종료든 랜선이 뽑혔든 같은 경로로 처리된다.**

**여행(리슨서버 접속)은 게임 시작 때만, 그리고 두 박자로 나뉘어 일어난다. (v6)**

```
방장이 "게임 시작"
      │
      ├─▶ RoomStart ─────▶ 방장   : OpenLevel(맵, "listen") 시작
      │              └───▶ 참여자 : "방장이 서버를 여는 중입니다..." (아직 떠나지 않는다)
      │
      │   (방장의 맵 로딩… 몇 초가 걸릴 수도, 금방 끝날 수도 있다)
      │
      ├─ UServerSubsystem 이 리슨서버 넷드라이버가 뜬 것을 감지
      ├─▶ RoomHostReadyReq ─▶ 서버
      └─▶ RoomHostReady ────▶ 참여자 : 지금 ClientTravel
```

| 프로퍼티 | 기본값 | 설명 |
|---|---|---|
| `HostMapName` | (비어있음) | 채우면 방장이 `OpenLevel(맵, "listen?RoomPassword=…")` 로 리슨서버를 연다 |
| `bAutoTravelOnGameStart` | **true** | 참여자가 호스트 준비 신호를 받으면 `ClientTravel(호스트주소?RoomPassword=…)` |

`HostMapName` 만 기본값이 비어 있다. 맵 이름은 게임 쪽 사정이고 틀리면 검은 화면이 되기 때문이다.
맵이 정해지면 채우거나 `OnGameStarted` 훅에서 직접 처리하면 된다.

> **v5 의 `GuestTravelDelay`(고정 3초)는 사라졌다.**
> 그 3초에는 근거가 없었다 — 방장이 큰 맵을 저사양 PC 에서 열면 모자라서 참여자가
> 아직 없는 서버에 붙으려다 튕겼고, 반대로 금방 열려도 3초를 그냥 버렸다.
> 이제 방장 쪽 `UServerSubsystem` 이 리슨서버가 실제로 뜬 것을 확인한 뒤에 신호를 보낸다.
> 상한은 `HostReadyTimeoutSeconds`(기본 60초)이고, 넘기면 신호를 보내지 않는다 —
> 열리지도 않은 주소로 참여자를 보내는 것보다 대기실에 남겨두는 편이 낫다.

### 4-2-1. 로비 흐름 — 콘솔 (UI 없이)

```bash
MOU.Lobby.Show                      # 로비 띄우기 / MOU.Lobby.Hide 로 제거
MOU.Room.Host 우리팀 방 1234       # 방 만들기 (비번 1234, 리슨서버 포트 기본 7777)
MOU.Room.List                       # 대기 중인 방 목록
MOU.Room.Join 1 1234                # 방번호 1에 비번 1234로 참여
MOU.Room.Ready 1                    # 준비완료 (0 이면 해제). 참여자만 의미 있음
MOU.Room.Start                      # 게임 시작. 방장만, 전원 준비 완료일 때만
MOU.Room.Leave                      # 방에서 나가기. 방장이 나가면 방이 사라진다
```

방 참여에 성공하면 로그에 호스트 주소가 찍힌다 — 이 주소로 `ClientTravel` 하면 된다.

**두 창(또는 두 프로세스)으로 대기실 전체를 검증한 결과** (2026-08-11, 헤드리스 2클라이언트):

```
[방 생성] #1 "테스트방" 방장=호스트(1) 주소=127.0.0.1:7777
[방 참여] #1 <- 게스트(2), 주소 127.0.0.1:7777 전달
[준비]   #1 게스트(2) -> 준비완료
[게임 시작] #1 방장=호스트, 호스트 127.0.0.1:7777, 인원 2명
[방 삭제] #1 방장 호스트(1) 가 나갔다. 남은 1명에게 통보. 남은 방 0개
```

게스트 쪽 로그에서 `대기실 #1 명단 2명 (전원준비 X)` → `(전원준비 O)` →
`게임 시작 (나는 참여자)` → `방 #1 이(가) 닫혔다. 방장이 나갔다.` 까지 확인했다.

### 4-3. 다중 클라이언트 테스트

PIE 플레이어 수를 2~3 으로 올린다. 창마다 GameInstance 가 따로 생기므로
채팅/로비 연결도 창마다 독립적으로 만들어진다. 각 창에서 다른 계정으로 로그인하면 된다.

`TestClient.exe` 를 섞어도 된다 (같은 프로세스가 계정·로비 명령도 전부 지원):

```bash
TestClient.exe 127.0.0.1 9000 아이디 비밀번호 --register 닉네임
```

접속 후 대화형 모드에서 `/host`, `/rooms`, `/join`, `/close` 로 로비를 검증할 수 있다.
`MOU.Chat.ShowLogin` 도 참고하면 실제 게임 클라이언트가 어떻게 동작하는지 알 수 있다.

**호스트 준비 신호(v6)를 콘솔로 검증하기** — 창 두 개로 한다.

| 순서 | 방장 창 | 참여자 창 | 기대 결과 |
|---|---|---|---|
| 1 | `/host 테스트방` | | 방 생성 |
| 2 | | `/rooms` → `/join 1` | 참여 성공, 호스트 주소 수신 |
| 3 | | `/ready` | 방장 쪽 명단에 준비완료 |
| 4 | `/go` | | 양쪽에 `[게임 시작]`. **참여자는 "호스트가 서버를 여는 중" 까지만** |
| 5 | `/hostready` | | 참여자에게만 `[호스트 준비 완료] → 지금 접속하면 된다` |

4번과 5번 사이에 참여자 화면이 멈춰 있는 것이 정상이다. 그 구간이 실제 게임에서는
방장의 맵 로딩 시간이고, 언리얼 클라이언트는 그 끝을 감지해 5번을 자동으로 보낸다
(`UServerSubsystem::PollListenServer`). 콘솔에는 감지할 리슨서버가 없으므로 손으로 친다.

순서를 뒤집어(`/go` 없이 `/hostready`) 서버 콘솔에 `사유 11`(NotStarted)이 찍히는지도
확인해두면 좋다. 아직 열리지도 않은 게임으로 사람을 보내지 않는다는 뜻이다.

### 4-4. 채팅창 조작

| 조작 | 동작 |
|---|---|
| `Enter` | 입력창 열기 / 전송 |
| `Esc` | 입력 취소 |
| `/a`, `/all` | 전체 채널로 전환 |
| `/t`, `/team` | 팀 채널로 전환 |
| `/d`, `/dead` | 사망 채널로 전환 |
| `/t 안녕` | 팀 채널로 바꾸고 바로 전송 |

### 4-5. 콘솔 명령 목록

**계정/연결**

| 명령 | 설명 |
|---|---|
| `MOU.Chat.Connect [호스트] [포트]` | 접속 (기본 `127.0.0.1 9000`) |
| `MOU.Chat.Register <아이디> <비밀번호> [닉네임]` | 계정 생성 |
| `MOU.Chat.Login <아이디> <비밀번호> [팀ID]` | 로그인 |
| `MOU.Chat.ShowLogin [호스트] [포트]` | 로그인 UI 위젯을 띄운다 |
| `MOU.Chat.ShowUI` / `MOU.Chat.HideUI` | 채팅창 표시 / 제거 |
| `MOU.Chat.ToggleInput` | 입력창 열기·닫기 (Enter 가 안 먹을 때) |
| `MOU.Chat.Say <채널> <메시지>` | UI 없이 전송. 채널 `0`=전체 `1`=팀 `2`=사망 |
| `MOU.Chat.Dead <0\|1>` | 생사 상태 변경 (테스트 전용) |
| `MOU.Chat.Disconnect` | 연결 종료 |

**로비**

| 명령 | 설명 |
|---|---|
| `MOU.Room.Host <방제목> [비번4자리] [포트]` | 방 만들기 (포트 기본 7777) |
| `MOU.Room.List` | 대기 중인 방 목록 요청 |
| `MOU.Room.Join <방번호> [비번4자리]` | 방 참여 |
| `MOU.Room.Leave` | 지금 있는 방에서 나간다. **방장이 나가면 방이 사라진다** |
| `MOU.Room.Ready [0\|1]` | 준비 상태를 바꾼다 (생략하면 1). 참여자만 의미가 있다 |
| `MOU.Room.Start` | 게임 시작. 방장만, 전원 준비 완료일 때만 |
| `MOU.Lobby.Show` | 로비 위젯을 띄운다 (PIE 창마다 따로 뜬다) |
| `MOU.Lobby.Hide` | 로비를 화면에서 제거한다 |

**테스트 보조**

| 명령 | 설명 |
|---|---|
| `MOU.Exec.Delayed <초> <명령...>` | N초 뒤에 콘솔 명령을 실행한다. 헤드리스(`-ExecCmds`) 검증에서 "로그인 완료 후 방 목록 요청" 같은 순서를 만들 때 쓴다 |

### 4-6. 서버 프레이밍 회귀 테스트

TCP 프레이밍이 깨지지 않았는지 확인하는 3종 세트. 서버 코드를 건드렸으면 반드시 돌린다.

```bash
TestClient.exe 127.0.0.1 9000 splitter pw123456 --register 스플리터 split
TestClient.exe 127.0.0.1 9000 merger   pw123456 --register 머저    merge
TestClient.exe 127.0.0.1 9000 attacker pw123456 --register 공격자  bad
```

| 테스트 | 보내는 것 | 통과 조건 |
|---|---|---|
| `split` | 패킷 1개를 1바이트씩 나눠 전송 | 서버에 메시지가 **정확히 1번** |
| `merge` | 패킷 3개를 1번의 send 로 | 서버에 메시지가 **3줄** |
| `bad` | `BodySize=999999` 위조 헤더 | `[차단]` 후 **해당 연결만** 끊김. 서버는 생존 |

### 4-7. 헤드리스 자동 검증 (선택)

에디터 GUI 없이 CI 처럼 돌리는 방법. 실제로 이 방식으로 검증했다.

```bash
UnrealEditor-Cmd.exe "<경로>\TeamProject_MOU.uproject" -game -nullrhi -unattended -nosplash -nosound -ExecCmds="MOU.Chat.Connect 127.0.0.1 9000,MOU.Chat.Register id123 pw123456 닉네임,MOU.Chat.Login id123 pw123456 0,MOU.Exec.Delayed 4 MOU.Room.List" -LogCmds="LogMOUServer Verbose" -log -abslog="<경로>\ue_test.log"
```

로그에서 `LogMOUServer` 을 필터링해 확인한다. 한계: `-nullrhi` 라 **화면에 어떻게 보이는지는 확인 못 한다.**
레이아웃과 색상은 에디터에서 눈으로 봐야 한다.

또 `-ExecCmds` 는 모든 명령을 **한 프레임에** 실행하므로, 서버 왕복(로그인 등)이 끝나야
의미가 있는 명령은 `MOU.Exec.Delayed` 로 지연시켜야 한다.

---

## 5. 구조

```
   [Server.exe :9000]       ← 엔진 없는 별도 프로세스. 게임과 무관하게 상시 가동
     │  accounts.db / chat_log.db (SQLite)   ← 계정은 동기 커밋, 채팅 로그는 비동기
     │  방 목록 (메모리만, 휘발성)
            ▲
            │ TCP (게임 리플리케이션과 완전히 별개)
            ▼
  ┌────────────────────────────────────────────────┐
  │ FServerClientRunnable   (워커 스레드)              │  소켓/바이트만 다룸
  │        ↕ TQueue (SPSC)                          │  UObject 접근 절대 금지
  │ FSocketLobbyBackend   (게임 스레드)              │  패킷 조립 ─┐
  │        │  implements ILobbyBackend              │            ├ 교체 지점
  │        │  (FEOSLobbyBackend 로 갈아끼울 수 있다) │            ─┘
  │ UServerSubsystem        (게임 스레드)              │  상태 보관 / 델리게이트
  │        ↓ OnChatMessageReceived 등                │
  │ UChatWidgetBase / ULoginWidgetBase   (UMG)      │  로그·입력창 / 로그인·가입
  │ ULobbyWidgetBase                     (UMG)      │  메인메뉴 + 대기실
  │   ├ URoomCreateWidgetBase                       │  방 만들기
  │   └ URoomListWidgetBase                         │  방 목록 + 참여
  └────────────────────────────────────────────────┘

  [리슨서버(게임)] ── UE 기본 리플리케이션 ── [게임 클라이언트]
     이동/전투/GAS. 채팅·계정·로비는 여기를 타지 않는다.
     방을 통해서만 서로의 IP:Port 를 알게 되고, 그 뒤로는 직접 붙는다.
```

### NAT 자동 포트포워딩 (2026-08-25 추가)

**뚫어야 하는 관문이 둘이라, 각각 다른 프로세스가 자기 것을 스스로 연다.**

```
[친구의 클라이언트]
    │
    ├─① 로그인/채팅/방목록 ──▶ Server.exe:9000 (TCP)
    │     Server.exe 자신이 시작 시 --upnp 로 공유기에 9000 을 열어달라고 요청한다
    │     (MOU_Server/Server/NatPortMapping.cpp)
    │
    └─② 방 참여 후 ──────────▶ 방장의 리슨서버:7777 (UDP)
          방장이 "방 만들기" 창을 여는 순간 그 방장의 PC 가 공유기에 7777 을 요청한다
          (TeamProject_MOU/.../Server/Net/NatPortMapping.cpp, 게임 클라이언트 쪽)
```

**포트를 여는 코드는 반드시 그 포트를 실제로 리스닝하는 프로세스 쪽에 있어야 한다.** 그래서
겉보기엔 같은 기능인데 구현이 두 벌이다 — `Server.exe` 는 자기 자신의 9000 을, 방장의 언리얼
클라이언트는 자기 자신의(정확히는 리슨서버가 될) 7777 을 각자 연다. 언리얼 클라이언트가 9000 을
대신 열어주는 식으로 합치면, `Server.exe` 를 다른 PC(예: 클라우드 VM)로 옮기는 순간 엉뚱한
기기의 포트를 여는 코드가 된다.

프로토콜(SSDP 탐색 + SOAP `AddPortMapping`/`DeletePortMapping`)은 같지만 구현도 두 벌이다 —
언리얼 쪽은 `FSocket`(엔진 소켓 API), `Server.exe` 쪽은 raw winsock 을 쓴다. `Framing.cpp` /
`ChatFraming.cpp` 가 두 벌인 것과 같은 이유(6절)다.

| | 여는 주체 | 포트 | 프로토콜 | 시점 |
|---|---|---|---|---|
| 로그인 서버 | `Server.exe` 자신 | 9000 | TCP | 서버 시작 시 (`--upnp` 플래그를 줬을 때만) |
| 방장의 리슨서버 | 방장의 언리얼 클라이언트 | 7777(기본) | UDP | 방 생성창이 열리는 순간(백그라운드) |

**실패해도 서버/방 생성은 그대로 진행된다.** UPnP 미지원 공유기거나 CGNAT 이면 같은 네트워크에서만
접속 가능한 예전 상태로 조용히 되돌아간다 — 오류가 아니라 "이 경로로는 못 간다" 는 뜻이다.
CGNAT 은 이 방식으로 근본적으로 못 푼다 (14-5절).

### 핵심 원칙 4가지

**1. 채팅·계정·로비는 UE 리플리케이션을 쓰지 않는다.**
Server RPC / Multicast 를 타지 않고 `Server.exe` 로 가는 별도 TCP 소켓으로 간다.
그래서 호스트가 게임을 나가도 채팅이 유지되고, 로비 서버 하나로 모든 방을 관리할 수 있다.

**2. 워커 스레드에서 UObject 를 절대 건드리지 않는다.**
언리얼의 UObject 는 스레드 세이프하지 않다. 수신 데이터는 순수 데이터 구조체로 바꿔
`TQueue` 에 넣기만 하고, 게임 스레드의 `UServerSubsystem::Tick` 이 꺼내서 그때 델리게이트를 쏜다.
이 규칙을 어기면 재현이 어려운 랜덤 크래시가 난다.

**3. 신원은 서버가 확정한다.**
`ChatBroadcast` 의 `SenderUserId`/`SenderName`, 방의 호스트 IP 모두 항상 서버가
세션 정보(TCP 연결 자체 또는 계정 DB)로 채운다. 클라이언트가 보낸 값을 그대로 옮기지 않는다.
이게 없으면 사망자 채널도, 방 호스트 주소 위조 방지도 성립하지 않는다.

**4. 로비 서버는 주소록일 뿐, 게임 트래픽은 지나가지 않는다.**
방 생성/조회/참여는 메타데이터 교환이다. 참여가 승인되면 클라이언트는 호스트에게
**직접** `ClientTravel` 한다. 그래서 로비 서버 부하는 방 개수와 무관하게 낮다.
대신 이 구조는 원래 **같은 네트워크(NAT 없음)에서만** 동작했다. **UPnP 자동 포트포워딩(2026-08-25)**
으로 흔한 가정용 공유기 환경에서는 자동으로 뚫리지만, 통신사 NAT(CGNAT) 안에서는 여전히 안 된다 —
자세한 내용은 11절과 14-5절 참고.

### 백엔드 교체 (v6에서 추가)

**계정 인증과 세션 탐색은 `ILobbyBackend` 뒤에 숨어 있다.** 설정값 하나로 갈아끼운다.

```
Project Settings → Game → MOU Server → Lobby Backend
  자체 서버 (TCP)   FSocketLobbyBackend   ← 현재 기본값
  EOS               FEOSLobbyBackend      ← 뼈대만 있음
```

| | 자체 서버 | EOS |
|---|---|---|
| 계정 | `accounts` 테이블 (SQLite) | EOS Connect (`ProductUserId`) |
| 방 목록 | `Rooms.cpp` (메모리) | EOS Session / Lobby |
| **NAT 통과** | **UPnP 로 자동화됨 (2026-08-25). CGNAT 은 여전히 안 됨** | **됨 (P2P 릴레이, CGNAT 포함)** |
| 사망자 채널 | 됨 | **안 됨** — 게임 상태를 아는 쪽만 판정할 수 있다 |
| 채팅 로그 영속화 | 됨 | **안 됨** |
| 준비물 | 없음 | Epic Dev Portal 등록, 플러그인 3종, SDK |

**왜 지금 자체 서버인가.** 외부 SDK·Epic 계정·인터넷 없이 바로 돌아가고,
사망자 채널과 채팅 로그처럼 **EOS 로 옮길 수 없는 기능**이 이미 여기에 있다.
채팅 때문에 어차피 상시 프로세스가 하나 필요했고, 방 목록과 계정을 거기에 얹은 것이다 —
로비 전용 서버를 새로 띄운 게 아니라 프로세스를 하나로 합친 쪽이다.

**왜 언젠가 EOS 인가.** 자체 서버가 못 푸는 문제는 방 목록이 아니라 **NAT** 다.
참가자는 호스트의 공인 IP:7777 로 직접 붙는데, 호스트가 포트포워딩을 하지 않으면
공유기가 그 접속을 막는다. 각자 집에서 붙는 시연을 하려면 릴레이가 필요하다.

**교체할 때 바뀌는 것과 안 바뀌는 것.**

| | |
|---|---|
| **안 바뀜** | `UServerSubsystem` 의 블루프린트 API, UMG 위젯 5종, 게임 로직 전부 |
| **바뀜** | `FEOSLobbyBackend` 의 내용, 그리고 그것을 고르는 설정값 한 줄 |

현실적인 최종 구성은 **"EOS = 계정·세션, 자체 서버 = 채팅·게임 데이터"** 다.
EOS Connect 의 `ProductUserId` 를 `accounts` 테이블의 외부 키로 저장하면 둘이 이어진다.
붙이는 순서는 `Server/Lobby/EOSLobbyBackend.h` 주석에 단계별로 적어뒀다.

### 스레드 경계 상세

```
     게임 스레드                            워커 스레드
   ┌─────────────────┐                 ┌──────────────────────────┐
   │ UServerSubsystem  │                 │  FServerClientRunnable     │
   │                 │                 │                          │
   │ SendChat()      │──OutboundQ────▶ │  PumpSend()  → Socket    │
   │ Login()         │                 │                          │
   │ CreateRoom() 등  │                 │                          │
   │                 │                 │                          │
   │ Tick()          │◀──InboundMsgQ── │  PumpRecv()  ← Socket    │
   │  → 델리게이트    │◀──InboundEvtQ── │  (파싱 후 구조체로)        │
   └─────────────────┘                 └──────────────────────────┘
```

큐는 전부 `EQueueMode::Spsc`(단일 생산자·단일 소비자)다. 각 큐를 반대편에서 쓰면 안 된다.

**서버 쪽도 스레드 경계가 있다.** 클라이언트마다 스레드 하나(`std::thread(...).detach()`)가
붙는데, 계정 DB(`Accounts`)는 그 스레드에서 **동기**로 바로 커밋하고, 채팅 로그(`ChatLog`)는
**MPSC 큐 + 전용 라이터 스레드 1개**로 비동기 처리한다. 왜 다른지는 11절 참고.

---

## 6. 파일별 역할

### 서버 (`MOU_Server/`)

| 파일 | 역할 |
|---|---|
| `Shared/ChatProtocol.h` | **패킷 정의. 언리얼과 공유하는 유일한 파일** |
| `Shared/Net.h` | 플랫폼별 소켓 API 래퍼. **언리얼에서 include 금지** |
| `Shared/Framing.h/.cpp` | 길이 프리픽스 프레이밍. **언리얼에서 include 금지** |
| `ThirdParty/sqlite/` | SQLite 3.47.1 앰알가메이션. 언리얼 5.8 동봉본 그대로 복사, 수정 없음 |
| `Server/Session.h/.cpp` | `ClientSession` + `SessionManager`. 세션의 `PeerAddress` 를 방 생성 시 호스트 주소로 쓴다 |
| `Server/Server.cpp` | accept 루프, 패킷 핸들러, `RouteChat()` 채널 라우팅, 로비 핸들러 |
| `Server/Accounts.h/.cpp` | 계정 DB. `login_id`/`pw_salt`/`pw_hash`/`nickname`. **동기 커밋** — 가입/로그인은 지연을 감수하고 무손실을 택한다 |
| `Server/Crypto.h/.cpp` | SHA-256 / HMAC-SHA256 / PBKDF2 자체 구현. 외부 라이브러리(OpenSSL 등) 없이 비밀번호 해시만 목적 |
| `Server/ChatLog.h/.cpp` | 채팅 로그 DB. MPSC 큐 + 라이터 스레드 1개. **비동기** — 채팅 지연보다 유실 허용을 택한다 |
| `Server/Rooms.h/.cpp` | 방 레지스트리. **메모리만** (SQLite 미사용, 휘발성) |
| `Server/Friends.h/.cpp` | 친구 관계 DB (v7). `Accounts` 와 같은 이유로 **동기 커밋** — 수락 여부는 유실되면 안 된다. `low_id`/`high_id` 정규화를 이 파일에서만 한다 |
| `Server/DirectMessages.h/.cpp` | 1:1 메시지 DB (v7). **동기 커밋** + 커서 페이징. 저장이 항상 전송보다 먼저다 |
| `Server/NatPortMapping.h/.cpp` | (2026-08-25) `Server.exe` 자신의 9000 포트를 UPnP 로 여는 클라이언트. `--upnp` 플래그로만 켜진다. raw winsock 사용 — 언리얼 쪽 동명 파일과 다른 구현(위 "NAT 자동 포트포워딩" 참고) |
| `TestClient/TestClient.cpp` | 검증용 콘솔 클라이언트. 계정/로비 명령 포함 |

### 언리얼 (`TeamProject_MOU/Source/TeamProject_MOU/`)

| 파일 | 역할 |
|---|---|
| `Public/Server/Chat/ChatTypes.h` | BP 노출 타입: `FChatMessage`, `FChatLoginResult`, `EChatChannelBP`, `EChatLoginResultBP`, `EChatConnectionState`, `LogMOUServer` |
| `Public/Server/Lobby/LobbyTypes.h` | BP 노출 로비 타입: `FMOURoomInfo`, `FMOURoomJoinResult`, `EMOURoomResultBP`, `EMOURoomStateBP`, `EMOULobbyBackendType` |
| `Public/Server/Lobby/LobbyBackend.h`<br>`Private/Server/Lobby/LobbyBackend.cpp` | **`ILobbyBackend` 인터페이스 + 팩토리.** 계정/세션 탐색의 교체 지점. `FServerClientEvent`(백엔드 → 게임 스레드 사건)도 여기 있다 |
| `Public/Server/Lobby/SocketLobbyBackend.h`<br>`Private/Server/Lobby/SocketLobbyBackend.cpp` | 자체 서버 백엔드. **패킷 조립은 여기서만 한다.** 워커 스레드 수명도 여기가 소유 |
| `Public/Server/Lobby/EOSLobbyBackend.h`<br>`Private/Server/Lobby/EOSLobbyBackend.cpp` | EOS 백엔드 **뼈대.** 각 함수가 어떤 EOS API 로 바뀌는지, 붙이는 순서가 주석에 있다 |
| `Public/Server/ServerSettings.h`<br>`Private/Server/ServerSettings.cpp` | `UDeveloperSettings`. 서버 주소 / 백엔드 종류 / 호스트 준비 대기 상한. 우선순위: 실행 인자 > 개인 ini > 팀 공유 ini |
| `Public/Server/Net/ChatFraming.h`<br>`Private/Server/Net/ChatFraming.cpp` | 프레이밍의 `TArray` 버전 + UTF-8 변환. 서버 `Framing.cpp` 와 로직 동일. BP enum ↔ 서버 enum `static_assert` 전부 여기 모여있다 |
| `Public/Server/Net/ServerClientRunnable.h`<br>`Private/Server/Net/ServerClientRunnable.cpp` | `FRunnable` 워커. 접속·재접속·송수신·패킷 파싱. **소유자는 `FSocketLobbyBackend`** |
| `Public/Server/Net/NatPortMapping.h`<br>`Private/Server/Net/NatPortMapping.cpp` | (2026-08-25) UPnP 프로토콜(SSDP/SOAP). 순수 C++, `FSocket` 만 씀. `Server.exe` 쪽 동명 파일과 여는 포트가 다르다(위 "NAT 자동 포트포워딩" 참고) |
| `Public/Server/Net/NatMappingRunnable.h`<br>`Private/Server/Net/NatMappingRunnable.cpp` | (2026-08-25) `FRunnable` 워커. `FNatPortMapping` 을 블로킹으로 돌리고, **매핑에 성공한 뒤에도 종료될 때까지 살아있다가 나가면서 지운다** — 스레드 수명이 곧 매핑 수명 |
| `Public/Server/Net/NatPortMappingSubsystem.h`<br>`Private/Server/Net/NatPortMappingSubsystem.cpp` | (2026-08-25) `UGameInstanceSubsystem`. 게임 스레드 진입점. `ILobbyBackend` 뒤에 안 숨긴 이유: 대화 상대가 `Server.exe` 가 아니라 공유기라 EOS 로 갈아끼워도 그대로 남는 코드다 |
| `Public/Server/ServerSubsystem.h`<br>`Private/Server/ServerSubsystem.cpp` | `UGameInstanceSubsystem`. **진입점. UI/게임플레이는 이것만 쓴다.** 백엔드를 고르고, 방장의 리슨서버가 뜨는지 감시한다. 친구/메신저 델리게이트도 여기서 나간다(v7). 패킷은 조립하지 않는다 |
| `Public/Server/Lobby/LoginWidgetBase.h`<br>`Private/Server/Lobby/LoginWidgetBase.cpp` | `UUserWidget`. 아이디/비밀번호 로그인 + 계정 생성. 성공 시 로비 자동 생성. **(구) 전체채팅 자동 생성은 v7 부터 기본 꺼짐** — `bShowChatWidgetOnSuccess=false` |
| `Public/Server/Lobby/LobbyWidgetBase.h`<br>`Private/Server/Lobby/LobbyWidgetBase.cpp` | `UUserWidget`. 메인메뉴 + 대기실. 같은 버튼 3개가 상태에 따라 라벨/동작을 바꾼다 |
| `Public/Server/Lobby/RoomCreateWidgetBase.h`<br>`Private/Server/Lobby/RoomCreateWidgetBase.cpp` | `UUserWidget`. 제목 + 비번 4자리로 방 생성. 리슨서버는 열지 않는다 |
| `Public/Server/Lobby/RoomListWidgetBase.h`<br>`Private/Server/Lobby/RoomListWidgetBase.cpp` | `UUserWidget`. 방 목록 표시 + 참여. 줄 하나는 `URoomListEntryWidget`(같은 헤더) |
| `Public/Server/Social/FriendTypes.h` | BP 노출 타입 (v7): `FMOUFriend`, `FMOUDirectMessage`, `EMOUFriendStateBP`, `EMOUPresenceBP`, `EMOUFriendResultBP` |
| `Public/Server/Social/FriendListWidgetBase.h`<br>`Private/Server/Social/FriendListWidgetBase.cpp` | `UFriendEntryWidget` + `UFriendListWidgetBase` (v7). 친구 목록 패널. 폭 고정(`PanelWidth`), 상태 문구 자동 소멸(`StatusClearSeconds`) |
| `Public/Server/Chat/MessengerWidgetBase.h`<br>`Private/Server/Chat/MessengerWidgetBase.cpp` | `UDmWindowWidget` + `UMessengerWidgetBase` (v7). 대화창 여러 개 + 친구 목록 통합 패널 |
| `Public/Server/Chat/InGame/ChatWidgetBase.h`<br>`Private/Server/Chat/InGame/ChatWidgetBase.cpp` | (구) 전체채팅 위젯. **로비에서는 더 이상 안 뜬다.** 화면 코드(로그 스크롤/입력창/토글키)를 인게임 죽은사람 채팅에 재활용하려고 `Chat/InGame/` 으로 옮겨 보관 중(2026-08-24) |

### 왜 프레이밍을 두 번 구현했나

`Framing.h` 를 언리얼에서 그대로 include 하면 같은 폴더의 `Net.h` 가 딸려 들어오고,
`Net.h` 는 `winsock2.h` / `windows.h` 를 포함해서 언리얼 매크로(`TEXT`, `GetObject` 등)와 충돌한다.
그래서 **"선언은 공유(`ChatProtocol.h`), 구현은 각자"** 로 나눴다.

> **서버 `Framing.cpp` 를 고치면 언리얼 `ChatFraming.cpp` 도 같이 고쳐야 한다.**

### 왜 비밀번호 해시를 직접 구현했나 (`Crypto.h/.cpp`)

OpenSSL 을 붙이면 팀원 전원이 빌드 환경을 맞춰야 한다. 윈도우 CNG(`bcrypt.h`)를 쓰면
서버가 윈도우에 묶인다. 필요한 게 SHA-256 계열 하나뿐이라 표준 C++ 로 직접 넣었다.
PBKDF2 로 10만 회 반복해서 GPU 무차별 대입을 늦추고, 비교는 상수 시간 비교로 타이밍 공격을 막는다.

### 왜 계정과 채팅 로그의 저장 정책이 다른가

| | 계정 (`Accounts`) | 채팅 로그 (`ChatLog`) |
|---|---|---|
| 처리 | 동기 (호출 스레드에서 즉시 커밋) | 비동기 (큐 → 전용 라이터 스레드) |
| `synchronous` | `FULL` | `NORMAL` |
| 실패 시 영향 | "가입했는데 계정이 없다" — **있을 수 없음** | 최근 몇 줄 유실 — **감수 가능** |

계정은 정합성이 최우선이라 지연을 감수하고, 채팅 로그는 지연 없는 것이 최우선이라
약간의 유실 위험을 감수한다. 커넥션도 분리했다 — `ChatLog` 의 커넥션은 라이터 스레드 전용이라
남이 끼면 그 전제가 깨진다.

### 왜 `UGameInstanceSubsystem` 인가

- 레벨을 이동해도(리슨서버 트래블 포함) 파괴되지 않아 연결이 유지된다.
  `PlayerController` 에 붙이면 트래블마다 재접속해야 한다.
- PIE 에서 클라이언트 창을 N개 띄우면 GameInstance 도 N개 생긴다.
  즉 연결도 자동으로 N개가 되어, 별도 작업 없이 다중 클라이언트 테스트가 된다.
- `GameMode` 에 붙이면 서버에만 존재해서 클라이언트가 못 쓴다.

---

## 7. API 레퍼런스

### `UServerSubsystem` — 진입점

블루프린트에서는 `Get Chat Subsystem` 노드로 얻는다.

**연결/계정**

| 함수 | 설명 |
|---|---|
| `ConnectToChatServer(Host, Port)` | 접속 시작. 즉시 반환, 실제 접속은 워커 스레드. 실패해도 자동 재시도 |
| `Login(LoginId, Password, TeamId)` | 로그인. **접속 전에 불러도 된다** — 보관했다가 연결되는 순간 자동 전송. 계정 인증 실패(없는 아이디/틀린 비번 등)면 재시도하지 않는다 |
| `RegisterAccount(LoginId, Password, Nickname)` | 계정 생성. 성공해도 자동 로그인은 안 되므로 이어서 `Login` 을 불러야 한다 |
| `ValidateCredentials(LoginId, Password, OutReason)` *(static)* | 서버 전송 전 길이 규칙 검사. UI 가 즉시 안내할 때 사용 |
| `GetLoginResultText(Result)` *(static)* | 실패 사유를 사용자 문구로 변환 |
| `SendChat(Channel, Text)` | 채팅 전송. 로그인 전이면 막고 경고 로그를 남긴다 |
| `SetDeadForTest(bDead)` | 생사 상태 변경. **테스트 전용, 8단계에서 제거 예정** |
| `Disconnect()` | 연결 종료 + 워커 스레드 정리 |
| `GetConnectionState()` | `EChatConnectionState` |
| `GetLoginResult()` | 서버가 확정한 내 신원 (`FChatLoginResult`, `UserId`=계정 번호) |

**로비**

| 함수 | 설명 |
|---|---|
| `CreateRoom(Title, RoomPassword, HostPort=7777)` | 방 생성. 로그인 후에만 가능. `RoomPassword` 를 비우면 공개방 |
| `RequestRoomList()` | 대기 중인 방 목록 요청. 결과는 `OnRoomListReceived` |
| `JoinRoom(RoomId, RoomPassword)` | 방에 입장. 성공하면 **대기실 멤버가 된다** (`OnRoomJoinCompleted` → `OnRoomMembersChanged`) |
| `LeaveRoom()` | 지금 있는 방에서 나간다. **방장이 나가면 방이 사라진다** |
| `SetReady(bReady)` | 준비 상태 변경. 응답은 없고 갱신된 명단이 `OnRoomMembersChanged` 로 온다 |
| `StartGame()` | 게임 시작. 방장만, 전원 준비 완료일 때만. 성공하면 멤버 전원에게 `OnRoomGameStarted`, 이어서 방장의 리슨서버가 뜨면 참여자에게 `OnRoomHostReady` |
| `UpdateRoomState(RoomId, CurrentPlayers, bInGame)` | 방 진행상태 통지. **v5 부터 인원수는 서버가 직접 세므로 무시된다** — 새로 쓸 일은 거의 없다 |
| `IsValidRoomPassword(Pw)` *(static)* | 숫자 4자리 규칙 검사 |
| `GetRoomResultText(Result)` *(static)* | 방 관련 실패 사유를 사용자 문구로 변환 |
| `GetMyRoomId()` | 내가 **방장인** 방 번호. 0 이면 방장이 아님 (참여자일 수는 있다) |
| `GetCurrentRoomId()` | 지금 **들어가 있는** 방 번호. 방장이든 참여자든. 0 이면 어느 방에도 없음 |
| `IsRoomHost()` | 내가 지금 방의 방장인지 |
| `GetRoomMembers()` | 마지막으로 받은 대기실 명단 (`TArray<FMOURoomMember>`) |
| `AreAllMembersReady()` | 참여자 전원이 준비했는지. **서버가 판정해 내려준 값** |
| `IsSelfReady()` | 내 준비 상태 |
| `IsWaitingForListenServer()` | **방장 전용.** 지금 "내 리슨서버가 열리기" 를 기다리는 중인가. UI 에 "서버 여는 중..." 을 띄울 때 |
| `GetBackendName()` | 지금 쓰고 있는 백엔드 이름 ("자체 서버(TCP)" / "EOS"). 디버그 표시용 |

> `GetMyRoomId()` 와 `GetCurrentRoomId()` 를 나눠 둔 이유: 참여자는 방에 있어도 방장이 아니다.
> 하나로 합치면 "방장인가" 와 "방에 있는가" 를 구분할 수 없어서 대기실 UI 가 갈라지지 않는다.

**델리게이트**

| 델리게이트 | 시그니처 |
|---|---|
| `OnChatMessageReceived` | `(const FChatMessage& Message)` |
| `OnChatStateChanged` | `(EChatConnectionState NewState, const FString& Detail)` |
| `OnChatLoginCompleted` | `(const FChatLoginResult& Result)` |
| `OnChatRegisterCompleted` | `(bool bSuccess, EChatLoginResultBP Result)` |
| `OnRoomCreated` | `(bool bSuccess, int32 RoomId, EMOURoomResultBP Result)` |
| `OnRoomListReceived` | `(const TArray<FMOURoomInfo>& Rooms)` |
| `OnRoomJoinCompleted` | `(const FMOURoomJoinResult& Result)` |
| `OnRoomMembersChanged` | `(int32 RoomId, const TArray<FMOURoomMember>& Members, bool bAllReady)` |
| `OnRoomClosed` | `(int32 RoomId, EMOURoomCloseReasonBP Reason)` |
| `OnRoomGameStarted` | `(const FMOURoomJoinResult& Host, bool bIsHost)` — **떠날 때가 아니다.** 방장은 리슨서버를 열고, 참여자는 기다린다 |
| `OnRoomHostReady` | `(const FMOURoomJoinResult& Host)` — **참여자는 여기서 떠난다.** 방장에게는 오지 않는다 |

**`Connected` 와 `LoggedIn` 은 다르다.**
`Connected` = TCP 는 붙었지만 아직 `UserId` 가 없다. 이 상태로 채팅/로비 요청을 보내면 서버가 거부한다.
`LoggedIn` = `LoginAck` 를 받아 신원이 확정됐다. 이때부터 채팅·방 생성·참여 가능.

### `UChatWidgetBase` — 채팅 UI

| 함수 | 설명 |
|---|---|
| `OpenChatInput()` / `CloseChatInput()` / `ToggleChatInput()` | 입력창 제어 |
| `SetActiveChannel(Channel)` / `CycleChannel()` | 채널 전환 |
| `AddSystemLine(Text)` | 클라이언트 로컬 안내 문구 추가 (서버로 안 감) |
| `GetActiveChannel()` / `IsChatInputOpen()` | 조회 |

| 프로퍼티 | 기본값 | 설명 |
|---|---|---|
| `MaxLogLines` | 200 | 넘으면 오래된 줄부터 제거 |
| `bBindToggleKeyToOwningPlayer` | true | PlayerController 의 InputComponent 에 토글 키 직접 바인딩 |
| `ChatToggleKey` | Enter | 위 옵션이 켜져 있을 때 쓸 키 |
| `bManageMouseCursor` | true | 입력창 열고 닫을 때 마우스 커서를 이 위젯이 제어할지 |

### `ULoginWidgetBase` — 로그인 / 계정 생성 UI

| 함수 | 설명 |
|---|---|
| `TryLogin()` / `TryRegister()` | 입력창 값으로 시도. 형식이 안 맞으면 서버에 보내지 않고 즉시 안내 |
| `SetMessage(Text, bIsError)` | 안내 문구 표시 |
| `OnLoginSucceeded(Result)` *(BlueprintImplementableEvent)* | 로그인 성공 후 블루프린트가 이어갈 훅 |

| 프로퍼티 | 기본값 | 설명 |
|---|---|---|
| `ServerHost` / `ServerPort` | `127.0.0.1` / `9000` | |
| `bRemoveOnSuccess` | true | 로그인 성공 시 자기 자신을 뷰포트에서 제거 |
| `bShowChatWidgetOnSuccess` | true | 성공 시 채팅 위젯 자동 생성 |
| `ChatWidgetClass` | (비어있으면 `UChatWidgetBase`) | 디자이너가 만든 WBP 를 대신 띄우고 싶을 때 |
| `bShowLobbyWidgetOnSuccess` | true | 성공 시 로비 메인메뉴 자동 생성 |
| `LobbyWidgetClass` | (비어있으면 `ULobbyWidgetBase`) | 디자이너가 만든 WBP_Lobby 를 대신 띄우고 싶을 때 |

비밀번호 입력칸은 항상 `IsPassword` 로 가려지고, 성공/실패 이후 입력값을 지운다.
**로그(UE_LOG)에는 절대 비밀번호를 남기지 않는다** — 이 규칙은 서버·클라이언트 전 구간 동일.

채팅 위젯을 먼저 띄우고 로비를 나중에 띄운다. 나중에 붙은 쪽이 위에 오므로 로비 버튼이 채팅창에 가리지 않는다.

### `ULobbyWidgetBase` — 로비 메인메뉴 + 대기실

**화면 하나가 상태에 따라 세 얼굴을 갖는다.** 버튼 세 개의 라벨과 동작만 바뀐다.

| 상태 | 1번 (`PrimaryButton`) | 2번 (`SecondaryButton`) | 3번 (`TertiaryButton`) |
|---|---|---|---|
| 메인메뉴 | 방 만들기 | 참여하기 | 게임 종료 |
| 대기실(참여자) | 준비하기 / 준비 해제 | 커스터마이징 | 나가기 |
| 대기실(방장) | 게임 시작 * | 커스터마이징 | 나가기 |

\* 참여자가 전원 준비해야 켜진다. 판정은 서버가 한다(`RoomMemberList.bAllReady`).

버튼을 상태마다 새로 만들지 않는 이유: 위젯이 하나면 WBP 배치가 한 번으로 끝나고,
"지금 무엇을 할 수 있는가" 가 늘 같은 자리에 있어 눈이 헤매지 않는다.
그래서 바인딩 이름도 `HostButton` 이 아니라 `PrimaryButton` 이다 — 대기실에서
"준비하기" 를 담당할 때 `HostButton` 은 거짓말이 된다.

**화면을 바꾸는 곳은 `RefreshUI()` 하나뿐이다.** 여기저기서 `SetText` 를 부르면
"준비하기" 라고 적힌 버튼이 방을 만드는 식의 어긋남이 반드시 생긴다.

| 함수 | 설명 |
|---|---|
| `GetUIState()` | `EMOULobbyUIState` (`MainMenu` / `WaitingRoom`) |
| `OpenRoomCreate()` / `OpenRoomList()` | 자식 창을 연다. 메인메뉴에서만 동작 |
| `ToggleReady()` | 내 준비 상태를 뒤집는다. 참여자만 |
| `RequestStartGame()` | 게임 시작. 방장만, 전원 준비 완료일 때만 |
| `LeaveRoom()` | 대기실에서 나가 메인메뉴로. 방장이 나가면 방이 사라진다 |
| `OpenCustomize()` | 커스터마이징. **아직 미구현** — 훅만 부르고 안내 문구를 띄운다 |
| `QuitGame()` | 게임 종료. 메인메뉴에서만 |
| `GetMyRoomPassword()` | 내 방의 비밀번호. **임시 보관소** — `PreLogin` 을 붙일 때 GameMode 로 옮길 것 |
| `OnEnteredWaitingRoom(RoomId, bIsHost)` *(BP 이벤트)* | 대기실에 들어갔다 |
| `OnLeftWaitingRoom(bRoomClosed)` *(BP 이벤트)* | 메인메뉴로 돌아왔다. `bRoomClosed` 면 방장이 나가서 쫓겨난 것 |
| `OnGameStarted(Host, bIsHost, RoomPassword)` *(BP 이벤트)* | **여기서 여행한다** |
| `OnCustomizeRequested()` *(BP 이벤트)* | 커스터마이징 화면이 생기면 여기서 띄운다 |

| 프로퍼티 | 기본값 | 설명 |
|---|---|---|
| `RoomCreateWidgetClass` / `RoomListWidgetClass` | (비어있음) | 디자이너 WBP 로 갈아끼울 때 |
| `HostPort` | 7777 | 방 생성창에 그대로 넘어간다. **게임의 리슨서버 포트와 같아야 한다** |
| `HostMapName` | (비어있음) | 채우면 **게임 시작 시** 방장이 `OpenLevel(맵, "listen")` |
| `bAutoTravelOnGameStart` | **true** | 켜면 **호스트 준비 신호를 받은 뒤** 참여자가 호스트로 `ClientTravel` |
| `bHideWhileChildOpen` | true | 자식 창이 열려 있는 동안 이 화면을 접는다 |
| `bManageMouseCursor` | true | 로비가 커서/입력 모드를 관리한다. **자식 창은 자동으로 false 가 된다** |

블루프린트 훅도 여행 시점에 맞춰 둘로 나뉜다.

| 훅 | 언제 | 누구에게 |
|---|---|---|
| `OnGameStarted(Host, bIsHost, RoomPassword)` | 게임이 시작됐다 | 방 전원 |
| `OnHostReady(Host, RoomPassword)` | 방장의 리슨서버가 열렸다 | **참여자만** |

> **여행 시점이 v5 에서 한 번, v6 에서 한 번 바뀌었다.**
> v4 까지는 방을 만들거나 참여하는 즉시 떠났다. v5 부터 **게임 시작 때만** 떠나고
> 그 사이가 대기실이 됐다. v6 부터는 참여자가 떠나는 시점이 게임 시작이 아니라
> **방장의 리슨서버가 실제로 열린 시점**이다.

### `URoomCreateWidgetBase` — 방 생성창

| 함수 | 설명 |
|---|---|
| `TryCreateRoom()` | 입력값 검사 후 `CreateRoom()` 호출 |
| `CancelCreate()` | 닫는다 |
| `OnRoomCreateSucceeded(RoomId, RoomPassword)` *(BlueprintImplementableEvent)* | 성공 훅 |

보내기 전에 클라이언트가 먼저 거르는 것: 로그인 여부, 이미 방장인지, 빈 제목,
제목 UTF-8 바이트 상한(48), **비번을 적었다면 반드시 숫자 4자리**.
(비었으면 공개방. 형식이 틀린 값을 조용히 무시하면 "1234 를 넣었는데 공개방이더라" 가 된다.)

### `URoomListWidgetBase` — 방 목록 / 참여창

| 함수 | 설명 |
|---|---|
| `RefreshRoomList()` | 목록 재요청 |
| `BeginJoin(RoomId)` | 참여 시작. **비번 방이면 바로 보내지 않고 입력창을 먼저 연다** |
| `ConfirmJoinWithPassword()` / `CancelPasswordPrompt()` | 비번 입력 확정 / 취소 |
| `CloseList()` | 닫는다 |
| `OnRoomJoinApproved(Result, RoomPassword)` *(BlueprintImplementableEvent)* | 승인 훅 |

| 프로퍼티 | 기본값 | 설명 |
|---|---|---|
| `EntryWidgetClass` | (비어있으면 `URoomListEntryWidget`) | 목록 한 줄의 위젯 |
| `AutoRefreshInterval` | 3초 | 0 이하면 새로고침 버튼으로만. **방은 호스트가 끊기면 조용히 사라지므로 주기 갱신이 필요하다** |
| `bRemoveOnSuccess` | true | 참여 승인 시 스스로 닫는다 |

동작상 알아둘 것:

- 목록을 다시 받을 때 줄을 **통째로 다시 만든다.** 방 개수 상한이 20 이라 부담이 없고,
  재사용 로직을 두면 "사라진 방의 버튼이 남아있는" 버그가 생긴다.
- 비번 입력 중에 새로고침이 돌아 **그 방이 사라졌으면 입력창을 닫고 알려준다.**
- 참여 실패가 `WrongPassword` 일 때만 입력창을 열어둔다. 정원 초과·없는 방이면
  비번을 고쳐봐야 소용없으므로 목록으로 돌려보낸다.

### 게임에 정식으로 붙이기

콘솔 명령은 검증용이다. 실제 흐름은 타이틀 화면 등에서:

```cpp
ULoginWidgetBase* LoginWidget = CreateWidget<ULoginWidgetBase>(PC, ULoginWidgetBase::StaticClass());
LoginWidget->ServerHost = TEXT("127.0.0.1");
LoginWidget->ServerPort = 9000;
LoginWidget->AddToViewport();
```

로그인 성공 이후 흐름(채팅 위젯, 로비 메뉴 등)은 `OnLoginSucceeded` 또는
`OnChatLoginCompleted` 델리게이트에 이어 붙이면 된다.

### UI 디자이너가 꾸미고 싶다면

두 위젯 모두 **WBP 없이 C++ 로 레이아웃을 조립**한다. 디자이너가 꾸미려면 해당 클래스를
부모로 하는 WBP 를 만들고 아래 **이름 그대로** 위젯을 배치하면 C++ 이 자동으로 집어서 쓴다.

**`WBP_Chat`** (부모: `UChatWidgetBase`)

| 위젯 이름 | 타입 | 필수 |
|---|---|---|
| `ChatLogBox` | Vertical Box | **필수** |
| `ChatInputBox` | Editable Text Box | **필수** |
| `ChatScrollBox` | Scroll Box | 권장 (`ChatLogBox` 를 감싸야 함) |
| `StatusText` / `ChannelText` / `ChatRootBorder` | Text Block / Text Block / Border | 선택 |

**`WBP_Login`** (부모: `ULoginWidgetBase`)

| 위젯 이름 | 타입 | 필수 |
|---|---|---|
| `LoginIdBox` / `PasswordBox` | Editable Text Box | **필수** |
| `LoginButton` / `RegisterButton` | Button | **필수** |
| `NicknameBox` | Editable Text Box | 선택 (없으면 아이디를 닉네임으로 사용) |
| `MessageText` / `TitleText` | Text Block | 선택 |

**`WBP_Lobby`** (부모: `ULobbyWidgetBase`)

| 위젯 이름 | 타입 | 필수 |
|---|---|---|
| `PrimaryButton` / `SecondaryButton` / `TertiaryButton` | Button | **필수** |
| `PrimaryButtonLabel` / `SecondaryButtonLabel` / `TertiaryButtonLabel` | Text Block (각 버튼 안에) | **필수** (없으면 라벨이 상태에 따라 안 바뀐다) |
| `MemberListBox` | Vertical Box | 권장 (대기실 명단이 여기 쌓인다) |
| `StatusText` / `MessageText` / `TitleText` | Text Block | 선택 |

**`WBP_RoomCreate`** (부모: `URoomCreateWidgetBase`)

| 위젯 이름 | 타입 | 필수 |
|---|---|---|
| `RoomTitleBox` | Editable Text Box | **필수** |
| `CreateButton` / `CancelButton` | Button | **필수** |
| `RoomPasswordBox` | Editable Text Box | 권장 (없으면 공개방만 만들 수 있다) |
| `MessageText` / `TitleText` | Text Block | 선택 |

**`WBP_RoomList`** (부모: `URoomListWidgetBase`)

| 위젯 이름 | 타입 | 필수 |
|---|---|---|
| `RoomListBox` | Vertical Box | **필수** (여기에 줄이 쌓인다) |
| `RoomListScrollBox` | Scroll Box | 권장 (`RoomListBox` 를 감싸야 함) |
| `PasswordPromptPanel` | 아무 Panel | 권장 (비번 방 참여에 필요) |
| `JoinPasswordBox` | Editable Text Box | 권장 (`PasswordPromptPanel` 안에) |
| `JoinConfirmButton` / `JoinCancelButton` | Button | 권장 |
| `RefreshButton` / `CloseButton` / `StatusText` / `TitleText` | Button / Text Block | 선택 |

**`WBP_RoomListEntry`** (부모: `URoomListEntryWidget`, `EntryWidgetClass` 에 지정)

| 위젯 이름 | 타입 | 필수 |
|---|---|---|
| `EntryJoinButton` | Button | **필수** (또는 BP 에서 `Request Join` 호출) |
| `EntryTitleText` / `EntryHostText` / `EntryPlayersText` / `EntryLockText` | Text Block | 선택 |

WBP 를 만들면 C++ 기본 레이아웃은 자동으로 사용되지 않는다
(`WidgetTree->RootWidget` 이 비어있을 때만 조립하기 때문).

---

## 8. 프로토콜 레퍼런스

원본: `MOU_Server/Shared/ChatProtocol.h`. 이 헤더만 언리얼과 공유한다. 현재 **v6**.

### 버전 이력

| 버전 | 변경 내용 |
|---|---|
| 1 | 최초 |
| 2 | `LoginReqBody` 에 `Version`, `LoginAckBody` 에 `Result`/`ServerVersion` 추가 (버전 핸드셰이크) |
| 3 | 계정 시스템. `LoginReqBody` 가 이름 대신 아이디/비밀번호를 보낸다. `RegisterReq/Ack` 추가. `UserId` 가 접속 일련번호에서 계정 고유번호로 바뀜 |
| 4 | 로비(방 목록). `RoomCreateReq/Ack`, `RoomListReq/Ack`, `RoomJoinReq/Ack`, `RoomLeaveReq`, `RoomStateUpdate` 추가 |
| 5 | 대기실. **서버가 방 멤버를 추적한다.** `RoomMemberList`, `RoomReadyReq`, `RoomClosed`, `RoomStartReq/RoomStart` 추가. `RoomJoin` 이 "주소 조회" 에서 "실제 입장" 으로, `RoomLeaveReq` 가 방장 전용에서 전원용으로 바뀜. 인원수는 호스트 신고값이 아니라 서버가 센 값 |
| 6 | 호스트 준비 신호. `RoomHostReadyReq/RoomHostReady` 추가. **`RoomStart` 의 의미가 "떠나라" 에서 "시작됐다" 로 좁아졌다** — 참여자가 실제로 떠나는 시점은 `RoomHostReady` 가 정한다. `ERoomResult::NotStarted` 추가 |

### 패킷 헤더 (6바이트 고정, `#pragma pack(1)`)

```
┌──────────────────┬──────────┬─────────────────────┐
│ BodySize uint32  │ Opcode   │ Body (BodySize 바이트) │
│                  │ uint16   │                     │
└──────────────────┴──────────┴─────────────────────┘
```

TCP 는 바이트 스트림이라 `send()` 한 번이 `recv()` 한 번으로 오지 않는다.
`BodySize` 를 보고 직접 경계를 잘라야 한다. 이게 프레이밍의 전부다.

### 오피코드

| 값 | 이름 | 방향 | 상태 |
|---|---|---|---|
| 1 | `LoginReq` | C → S | 사용 중 (v3부터 아이디+비밀번호) |
| 2 | `LoginAck` | S → C | 사용 중 |
| 3 | `ChatSend` | C → S | 사용 중 |
| 4 | `ChatBroadcast` | S → C | 사용 중 |
| 5 | `HistoryReq` | C → S | **예약됨, 미구현** (6단계 히스토리 조회) |
| 6 | `HistoryAck` | S → C | **예약됨, 미구현** |
| 7 | `WhisperSend` | C → S | 9단계 예정 |
| 8 | `SetDead` | C → S | 임시. 8단계에서 리슨서버 전용으로 |
| 9 | `Heartbeat` | C → S | 서버가 받고 무시만 함 |
| 10 | `RegisterReq` | C → S | 사용 중 (계정 생성) |
| 11 | `RegisterAck` | S → C | 사용 중 |
| 12 | `RoomCreateReq` | C → S | 사용 중 |
| 13 | `RoomCreateAck` | S → C | 사용 중 |
| 14 | `RoomListReq` | C → S | 사용 중 |
| 15 | `RoomListAck` | S → C | 사용 중 |
| 16 | `RoomJoinReq` | C → S | 사용 중 |
| 17 | `RoomJoinAck` | S → C | 사용 중 |
| 18 | `RoomLeaveReq` | C → S | 사용 중 (누구나. **호스트가 보내면 방 삭제**) |
| 19 | `RoomStateUpdate` | C → S | 사용 중 (방장만 유효. v5 부터 인원수 필드는 무시) |
| 20 | `RoomMemberList` | S → C | 사용 중 (멤버/준비상태가 바뀔 때마다 방 전원에게) |
| 21 | `RoomReadyReq` | C → S | 사용 중 |
| 22 | `RoomClosed` | S → C | 사용 중 (방장이 나갔을 때 남은 멤버에게) |
| 23 | `RoomStartReq` | C → S | 사용 중 (방장만) |
| 24 | `RoomStart` | S → C | 사용 중 (방 전원에게. **"떠나라" 가 아니라 "시작됐다" 다**) |
| 25 | `RoomHostReadyReq` | C → S | 사용 중 (방장만. 리슨서버가 실제로 열렸다) |
| 26 | `RoomHostReady` | S → C | 사용 중 (참여자에게. **여기서 떠난다**) |

### 로그인 / 가입 실패 사유 (`ELoginResult`)

| 값 | 이름 | 의미 |
|---|---|---|
| 0 | `Success` | |
| 1 | `VersionMismatch` | 재시도해도 계속 실패한다. 서버·클라 재빌드 필요 |
| 2 | `InvalidRequest` | 바디 크기가 맞지 않음 |
| 3 | `AccountNotFound` | 없는 아이디 |
| 4 | `WrongPassword` | 비밀번호 불일치 |
| 5 | `DuplicateId` | 가입 시 이미 있는 아이디 |
| 6 | `InvalidFormat` | 아이디/비번 길이 규칙 위반 |
| 7 | `ServerError` | DB 오류 등. 클라이언트 잘못 아님 |

### 방 요청 결과 (`ERoomResult`)

| 값 | 이름 | 의미 |
|---|---|---|
| 0 | `Success` | |
| 1 | `NotAuthed` | 로그인하지 않음 |
| 2 | `NotFound` | 없는 방 (이미 닫혔을 수 있음) |
| 3 | `WrongPassword` | |
| 4 | `Full` | 정원 초과 |
| 5 | `AlreadyStarted` | 이미 게임이 시작된 방 |
| 6 | `AlreadyHosting` | 한 사람이 방을 두 개 가질 수 없음 |
| 7 | `InvalidRequest` | |

### 상한값

| 상수 | 값 | 의미 |
|---|---|---|
| `kMaxBodySize` | 4096 | 넘으면 서버가 **연결을 끊는다** |
| `kMaxNameLen` | 32 | 닉네임 고정 배열 크기 (널 종료 포함) |
| `kMaxTextLen` | 512 | 메시지 본문 **UTF-8 바이트** 수. 한글 약 170자 |
| `kMaxLoginIdLen` | 24 | 아이디 (널 종료 포함) |
| `kMaxPasswordLen` | 64 | 비밀번호 (널 종료 포함) |
| `kMinLoginIdLen` / `kMinPasswordLen` | 3 / 6 | 하한 |
| `kMaxRoomTitleLen` | 48 | 방 제목 |
| `kRoomPasswordLen` | 4 | 방 비밀번호. **고정 4바이트, 널 종료 없음** (숫자만) |
| `kMaxPlayersInRoom` | 4 | 1~4인 게임 |
| `kMaxRoomsInList` | 20 | 한 번에 내려주는 방 개수 상한 |

> `TextLen` 은 글자 수가 아니라 **바이트 수**다.
> 상한을 넘겨 보내면 서버가 `Malformed` 로 판단해 연결을 끊으므로,
> 클라이언트가 `EncodeUtf8Clamped()` 로 **문자 경계에 맞춰** 미리 자른다.

### 채널

| 값 | 이름 | 수신자 |
|---|---|---|
| 0 | `All` | 접속자 전원 |
| 1 | `Team` | 나와 `TeamId` 가 같은 사람 |
| 2 | `Dead` | 죽은 사람끼리. 산 사람이 보내면 서버가 **무응답으로 폐기** |
| 3 | `Whisper` | 9단계 예정. 지금 보내면 아무도 못 받음 |
| 4 | `System` | 서버만 생성. 클라이언트가 보내면 무시 |

채널 번호는 `ChatProtocol.h` 의 `MOU::EChatChannel` 과 언리얼의 `EChatChannelBP` 가
반드시 일치해야 한다. `ChatFraming.h` 의 `static_assert` 가 컴파일 타임에 검사한다
(로그인 결과·방 결과·방 상태 enum 도 전부 같은 방식으로 검사한다).

### 방 목록 응답 형태 (`RoomListAckBody` + `RoomInfo[]`)

고정 헤더(`Count`) 뒤에 `RoomInfo` 가 `Count` 개 이어붙는 가변 길이 패킷이다
(`ChatBroadcast` 의 "고정 헤더 + 가변 본문" 과 같은 방식). **`RoomInfo` 에는 호스트 주소가 없다** —
목록만 보고 비밀번호 방에 바로 접속하는 것을 막기 위해 의도적으로 뺐다. 주소는
`RoomJoinReq` 로 비밀번호를 통과해야만 `RoomJoinAckBody::HostAddress` 로 내려온다.

---

## 9. 팀 공용 파일 변경 내역

> **다른 파트 작업에 영향이 갈 수 있는 파일은 아래 두 개다.**

### `TeamProject_MOU/Source/TeamProject_MOU/TeamProject_MOU.Build.cs` (수정)

| 변경 | 내용 | 다른 파트에 미치는 영향 |
|---|---|---|
| `using System.IO;` 추가 | `Path` 클래스 사용 | 없음 |
| `"Sockets"`, `"Networking"` 추가 | 엔진 소켓 API | 없음 |
| `"SlateCore"` 추가 | UMG 위젯의 `ETextCommit` 리플렉션 링크에 필요 | 없음. 원래 주석으로 추천돼 있던 모듈 |
| `PublicIncludePaths` 에 `MOU_Server/Shared` 추가 | 서버와 프로토콜 헤더 공유 | **저장소 전체를 받아야 함** |

### `MOU_Server/CMakeLists.txt` (수정)

| 변경 | 내용 |
|---|---|
| `project(MOU_Server C CXX)` | SQLite 앰알가메이션이 C 파일이라 C 언어를 켰다 (이전엔 `CXX` 만) |
| `sqlite3` 정적 라이브러리 타깃 추가 | `ThirdParty/sqlite/sqlite3.c` 를 빌드해서 `Server` 에 링크 |
| `Server` 소스에 `Accounts.cpp`/`Crypto.cpp`/`ChatLog.cpp`/`Rooms.cpp` 추가 | |
| `Server` 소스에 `NatPortMapping.cpp` 추가 (2026-08-25) | UPnP 로 9000 포트를 여는 코드. `--upnp` 로만 켜지므로 링크는 항상 되지만 기본 동작은 그대로다 |

**서버 실행 인자가 바뀌었다.** `Server.exe <port>` → `Server.exe <port> [db경로] [--upnp]`
(뒤 두 인자는 전부 선택이라 기존 실행 스크립트는 그대로 동작한다. `--upnp` 는 위치에 상관없이
아무 데나 넣어도 인식된다).

### 바뀌지 않은 것 (확인용)

- `TeamProject_MOU.uproject` — 플러그인 추가 없음. `Sockets`/`Networking`/`SlateCore` 는 엔진 기본 모듈
- ~~`Config/*.ini` — 건드리지 않았다~~ → **`DefaultEngine.ini` 에 `[CoreRedirects]` 한 줄이 추가됐다 (2026-08-25).**
  `UChatSubsystem` → `UServerSubsystem` 이름 변경 때문이다. 저장소의 `.uasset` 중 이 클래스를
  참조하는 것은 없었지만, 팀원 PC 에 아직 커밋되지 않은 블루프린트가 있을 수 있어 안전망으로 넣었다.
  전원이 한 번씩 열어 재저장한 뒤에는 지워도 된다
- 기존 게임플레이 코드(`Base/`, `TeamProject_MOUCharacter` 등) — 전혀 건드리지 않았다
- 콘텐츠(`.uasset`) — 추가/수정 **0개**. 에셋 충돌 걱정 없음

### 추가된 파일 (전부 신규, 충돌 없음)

```
서버:
  MOU_Server/Server/Accounts.h  Accounts.cpp  Crypto.h  Crypto.cpp
  MOU_Server/Server/ChatLog.h   ChatLog.cpp
  MOU_Server/Server/Rooms.h     Rooms.cpp
  MOU_Server/ThirdParty/sqlite/ sqlite3.h  sqlite3.c  README.md

언리얼 (Public/ 과 Private/ 이 같은 구조. .h 는 Public, .cpp 는 Private):
  Source/TeamProject_MOU/Public/Server/        ServerSubsystem.h  ServerSettings.h
  Source/TeamProject_MOU/Public/Server/Net/    ChatFraming.h  ServerClientRunnable.h
  Source/TeamProject_MOU/Public/Server/Lobby/  LobbyBackend.h  SocketLobbyBackend.h
                                               EOSLobbyBackend.h  LobbyTypes.h
                                               LoginWidgetBase.h  LobbyWidgetBase.h
                                               RoomCreateWidgetBase.h  RoomListWidgetBase.h
  Source/TeamProject_MOU/Public/Server/Chat/   ChatTypes.h  ChatWidgetBase.h
  Source/TeamProject_MOU/Public/Server/Social/ (친구 시스템 — 구현 예정)

NAT 포트포워딩 (2026-08-25 추가):
  Source/TeamProject_MOU/Public/Server/Net/    NatPortMapping.h          UPnP 프로토콜 (순수 C++)
                                               NatMappingRunnable.h      워커 스레드
                                               NatPortMappingSubsystem.h 게임 스레드 진입점
```

### 이름 변경 (2026-08-25) — `Chat` → `Server`

`Server.exe` 는 채팅뿐 아니라 로그인·방·친구·메신저를 전부 다루는데, 클라이언트 쪽
이름에 `Chat` 이 남아 있어 새로 보는 사람이 "채팅 전용" 으로 오해했다.
폴더를 `Chat/` 에서 `Server/` 로 옮긴 2026-08-24 개편의 연장이다.

| 이전 | 이후 |
|---|---|
| `ChatClientRunnable.h/.cpp` | `ServerClientRunnable.h/.cpp` |
| `FChatClientRunnable` | `FServerClientRunnable` |
| `ChatSubsystem.h/.cpp` | `ServerSubsystem.h/.cpp` |
| `UChatSubsystem` | `UServerSubsystem` |
| `FChatClientEvent` / `EChatClientEventType` | `FServerClientEvent` / `EServerClientEventType` |
| `LogMOUChat` | `LogMOUServer` |

**바꾸지 않은 것** — 이것들은 진짜로 채팅 고유라서 그대로 둔다:
`FChatMessage`, `EChatChannel`/`EChatChannelBP`, `ChatTypes.h`, `ChatFraming.h/.cpp`,
`ChatWidgetBase`, 서버의 `ChatLog`/`ChatProtocol.h`, 그리고 **콘솔 명령 `MOU.Chat.*`**
(문서와 팀원 습관에 이미 박혀 있어 지금 바꾸면 얻는 것보다 잃는 것이 크다).

> `LogMOUServer` 선언은 여전히 `Server/Chat/ChatTypes.h` 에 있다. 옮기면 이 카테고리를
> 쓰는 파일 전부의 include 를 갈아야 해서 미뤘다. 나중에 정리할 후보.

> 로비 UI 3종을 추가할 때 **팀 공용 파일은 하나도 건드리지 않았다.** `Build.cs` 도 그대로다
> (필요한 모듈 `UMG`/`Slate`/`SlateCore` 가 이미 들어 있다). `.uasset` 추가도 0개다.
> 기존 파일 중 바뀐 것은 `LoginWidgetBase.h/.cpp` 하나뿐이고,
> 로그인 성공 시 로비를 띄우는 옵션(`bShowLobbyWidgetOnSuccess`)이 늘어난 것이 전부다.

### 커밋하면 안 되는 것 / `.gitignore` 로 옮긴 것

- `TeamProject_MOU/Automation_TeamProject_MOU.slnx`, `TeamProject_MOU.slnx` — UBT 가 자동 생성하고
  엔진 절대 경로가 PC 마다 달라 매번 diff 가 난다. 처리 방향은 보류 중 (요청 시에만 다시 논의).
- `chat_log.db` / `chat_log.db-wal` / `chat_log.db-shm` — 서버 실행 중 생기는 런타임 산출물.
  루트 `.gitignore` 에 추가해뒀다.
- `%APPDATA%\...\BuildConfiguration.xml` — 사용자별 설정. 애초에 저장소 밖이다.

---

## 10. 이 시스템의 코딩 규칙

### 파일 인코딩 — **UTF-8 (BOM 포함)**

`Server/` 폴더의 `.h`/`.cpp` 는 UTF-8 로 저장한다.
주석에 한글이 있어서, BOM 이 없으면 MSVC 가 CP949 로 오해해 깨질 수 있다.

> **★ 실제로는 BOM 이 섞여 있다** (2026-08-24 확인). `Lobby/` 의 9개 파일
> (`LobbyBackend`, `SocketLobbyBackend`, `EOSLobbyBackend`, `LobbyTypes`,
> `LoginWidgetBase` 의 `.h`/`.cpp`)은 **BOM 없이** 저장돼 있고 나머지는 있다.
> 이 상태로 지금까지 빌드가 통과해 왔으므로 BOM 이 절대 조건은 아니다
> (`Build.cs` 에 `/utf-8` 지정은 없다 — 엔진 기본 동작으로 보이지만 확인하지
> 않았다). **새 파일은 BOM 을 넣어 통일한다** — 이미 없는 파일을 굳이 바꾸지는
> 않는다(전부 재저장하면 diff 만 커지고 얻는 것이 없다).

**편집기에서 저장할 때 인코딩을 바꾸지 말 것.** VS 는 기존 인코딩을 유지하니 보통 문제없다.

> 기존 팀원 파일(`Base/` 등)은 CP949 로 저장돼 있다. 파일마다 인코딩이 달라도
> BOM 유무로 구분되므로 컴파일에는 문제가 없다.

### 주석 규칙

이 파트를 담당하지 않은 팀원이 리뷰할 수 있도록, **모든 파일 상단에**:

- 이 파일이 시스템 어디에 위치하는지 (다이어그램 포함)
- 어디와 정보를 주고받는지 (대응하는 서버 코드 파일·함수 이름)
- 수정 시 같이 고쳐야 하는 파일

그리고 함수 단위로 **"왜 이렇게 했는지"** 를 남긴다. 무엇을 하는지는 코드가 말해준다.

### 스레드 규칙

| 하는 곳 | 해도 되는 것 |
|---|---|
| `FServerClientRunnable` (워커) | 소켓, 바이트 배열, 순수 구조체, `TQueue` 넣기 |
| `FServerClientRunnable` (워커) | ❌ UObject, UMG, 델리게이트 브로드캐스트, `UE_LOG` 외 엔진 API |
| `UServerSubsystem::Tick` (게임) | 큐에서 꺼내기, 델리게이트 브로드캐스트 |
| `UChatWidgetBase`/`ULoginWidgetBase` (게임) | 위젯 조작 |
| 서버 `ChatLog` 라이터 스레드 | `sqlite3*` 를 여기서만 건드린다. 다른 스레드가 같은 커넥션을 만지면 전제가 깨진다 |
| 서버 `Accounts` | 여러 클라이언트 스레드가 동시에 부를 수 있어 자체 뮤텍스로 직렬화한다 |

### 종료 순서 (지키지 않으면 크래시)

**클라이언트** — 워커 스레드 정리는 **반드시** 이 순서다 (`UServerSubsystem::ShutdownClient`):

1. `ChatClient->Stop()` — 종료 요청 플래그
2. `ChatThread->Kill(/*bShouldWait=*/true)` — **`true` 필수.** 워커가 `Run()` 을 빠져나올 때까지 대기
3. `delete ChatClient` — 스레드가 완전히 끝난 뒤에

`bShouldWait` 를 `false` 로 두면 **PIE 를 껐다 켤 때 에디터가 통째로 죽는다.**

**서버** — Ctrl+C(SIGINT/SIGTERM) 핸들러가 `ChatLog::Stop()` → `Accounts::Stop()` 순으로
정리한 뒤 `_Exit(0)` 한다. `ChatLog::Stop()` 이 큐에 남은 채팅 로그를 전부 커밋할 때까지 기다리므로,
**정상 종료(Ctrl+C)라면 로그 유실이 없다.**

### 비밀번호 취급 규칙

- 서버: 절대 평문 저장 금지. 반드시 `Crypto::Pbkdf2HmacSha256` + 랜덤 솔트를 거친다.
- 클라이언트: 로그인/가입 성공·실패 관계없이 **UE_LOG 에 비밀번호 문자열을 남기지 않는다.**
  `PendingPassword` 는 `Disconnect()` 시 비운다.
- UI: `EditableTextBox::SetIsPassword(true)` 로 항상 가린다.

---

## 11. 알려진 한계

### 계정 / 인증

- **비밀번호가 평문으로 전송된다 (TLS 없음).** 저장은 PBKDF2 해시라 안전하지만,
  네트워크 구간은 암호화가 없다. 같은 네트워크에 있는 사람이 패킷을 뜨면 비밀번호가 그대로 보인다.
  **팀원들에게 실제로 쓰는 비밀번호를 넣지 말라고 공지할 것.** 프로토콜 헤더와 로그인 UI 헤더에
  경고 주석을 남겨뒀다. 제대로 하려면 TLS 를 씌워야 하는데 이 프로젝트 범위를 넘는다.
- ~~프로토콜 버전 검사 없음~~ → **해결됨 (v2).**
  `LoginReqBody::Version` 을 서버가 검사하고, 다르면 `LoginAck` 에
  `ELoginResult::VersionMismatch` 와 서버 버전을 담아 거부한다.
  **`ChatProtocol.h` 를 고쳤으면 `kProtocolVersion` 을 올리고 서버와 언리얼을 같이 다시 빌드할 것.**
- ~~재로그인하면 UserId 가 바뀜~~ → **해결됨 (v3, 계정 시스템).**
  `UserId = accounts.id` 라 같은 계정이면 재접속·서버 재시작에도 동일한 값이 나온다.
- **인증이 없는 것은 아니지만 가볍다.** PBKDF2 10만 회는 실무 표준(Argon2id/bcrypt)보다
  GPU 내성이 약하다. 이 프로젝트 규모에서는 충분하다고 판단했다.

### 로비 / 방

- **방 비밀번호(숫자 4자리)는 암호학적 의미가 없다.** 1만 가지뿐이라 "아는 사람만 들어오게"
  하는 정도의 장치다. 무작정 시도를 막으려면 호스트 쪽에 시도 횟수 제한을 두는 게 좋다.
- **로비 서버의 비밀번호 검사만으로는 못 막는다.** 목록을 거치지 않고 호스트 IP 로 직접
  `ClientTravel` 하면 로비 서버를 건너뛸 수 있다. **호스트의 `AGameModeBase::PreLogin` 에서
  URL 옵션(`RoomPassword`)을 다시 검사해야 실제로 잠긴다.** 이 부분은 아직 미구현 — 12절 참고.
- ~~NAT 를 해결해주지 않는다~~ → **UPnP 자동 포트포워딩으로 흔한 경우는 해결됨 (2026-08-25).**
  방장이 "방 만들기" 창을 여는 순간 백그라운드로 공유기에 리슨서버 포트를 열어달라고 요청하고,
  성공하면 그 외부 포트를 방 정보에 신고한다(`URoomCreateWidgetBase`,
  `TeamProject_MOU/.../Net/NatPortMapping*`). 참가자가 몰라도 되고 별도 조작도 없다.
  **여전히 안 되는 경우**: 공유기가 UPnP 를 지원하지 않거나(설정에서 꺼둔 경우 포함),
  방장이 **통신사 NAT(CGNAT)** 안에 있으면 실패한다 — 이때는 예전처럼 같은 네트워크에서만
  접속되고, 근본적으로는 Steam/EOS 같은 릴레이 서비스가 필요하다(14-5절). 실패해도 방은
  정상적으로 만들어진다.
- **방은 SQLite 에 저장하지 않는다 (의도적).** 서버가 재시작되면 호스트들의 리슨서버도
  이미 죽어 있으므로, 방을 복원해봤자 "들어갈 수 없는 방"만 보여주게 된다. 메모리에만 두고
  호스트 접속이 끊기면(정상 종료든 강제 종료든) 세션 정리 시점에 자동으로 지운다.
- **한 사람당 방 하나.** 이미 방이 있거나 남의 방에 들어가 있으면 `AlreadyHosting` 으로 거부된다.
  UI 에서는 방에 들어가는 순간 화면이 대기실로 바뀌어 애초에 두 번 만들 수 없다.
- **방장이 나가면 방이 사라진다. 호스트 이양은 하지 않는다.** 호스트가 곧 리슨서버라
  이양하려면 리슨서버 재개설 + 전원 재접속 + 상태 복원이 필요한데 UE 가 기본 지원하지 않는다.
  남은 멤버는 `RoomClosed` 를 받고 메인메뉴로 돌아간다.
- **방장 쪽 여행은 아직 맵 이름을 기다린다.** `ULobbyWidgetBase::HostMapName` 이 비어 있으면
  `OpenLevel` 을 부르지 않는다. 맵 이름은 게임 쪽 사정이고 틀리면 검은 화면이 되기 때문이다.
  참여자 쪽(`bAutoTravelOnGameStart`)은 v6 부터 기본으로 켜져 있다.
- ~~게임 시작 시 참여자가 호스트보다 먼저 붙을 수 있다~~ → **해결됨 (v6).**
  방장 쪽 `UServerSubsystem` 이 리슨서버 넷드라이버가 뜬 것을 확인하고 `RoomHostReadyReq` 를
  보내면, 서버가 참여자에게 `RoomHostReady` 를 넘긴다. 참여자는 그때 떠난다.
  v5 의 `GuestTravelDelay`(고정 3초)는 사라졌다.
  **남은 한계:** 호스트가 끝내 리슨서버를 열지 못하면 참여자는 대기실에 남는다
  (`HostReadyTimeoutSeconds`, 기본 60초). 방장이 방을 나가면 `RoomClosed` 로 정리된다.
- **방을 나갔다 다른 방에 들어갈 때 순서가 꼬일 수 있다.** 이전 방의 명단이 늦게 도착하는
  경우가 있어 클라이언트가 `RoomId` 를 확인하고 버린다. 서버는 다른 방에 있는 사람의
  `Join` 을 `InvalidRequest` 로 거부한다 — 조용히 옮겨주면 원래 방에 알릴 기회를 놓친다.

### 채팅 로그 DB

- **비정상 종료 시 큐에 남은 줄이 유실된다.** 정상 종료(Ctrl+C)라면 무손실이지만,
  작업 관리자 강제 종료나 정전이면 아직 커밋 안 된 만큼 사라진다. 채팅 로그라 감수했다.
- **큐 상한 1만 줄.** 넘치면 버리고 `GetDroppedCount()` 로 집계한다.
  종료 시 "유실 N줄" 로 찍히므로, 0 이 아니면 디스크가 못 따라간다는 뜻이다.
- **아직 읽는 기능이 없다.** 쓰기 전용이다. 접속 시 최근 대화 N줄을 내려주는 기능(`HistoryReq`/`Ack`,
  오피코드는 이미 예약돼 있다)은 별도 작업이다.

### 언리얼 클라이언트 (일반)

- **엔디안 변환 없음.** 서버·클라이언트 모두 x86 리틀엔디안 전제.
- 전송 지연이 최대 50ms 붙는다 (`FServerClientRunnable::WaitMilliseconds`).
  워커가 `Wait()` 에서 깨어나야 큐를 비우기 때문. 채팅에는 무해하다.
- **콘솔 명령이 Shipping 빌드에도 남는다.** `MOU.Chat.Dead` 같은 치트성 명령에
  `ECVF_Cheat` 가 없다. `MOU.Chat.ShowLogin` 등 일부는 `#if !UE_BUILD_SHIPPING` 으로 뺐지만
  전부는 아니다. 8단계에서 정리할 예정.

> **팀 ID / 생사 신원 위조 문제는 여기서 뺐다.** `Server.exe` 의 `ChatSend`
> `Team`/`Dead` 채널·`SetDeadForTest()` 는 로비 메신저(v7, `Friends`/`DirectMessages`)와
> 무관하고, `Dead` 채널 자체가 리슨서버 RPC 로 이관되면서 `Server.exe` 를 안 타게 됐다
> (1절 8단계 폐기 참고). 지금은 **인게임 채팅 구현의 몫**이다 —
> `CHAT_DESIGN.md` 11-2절 · 11-4절 참고.

### 서버 (`MOU_Server/README.md` 에서 그대로)

- **`send()` 를 세션 락 안에서 호출한다.** 느린 클라이언트가 있으면 락을 오래 잡아 전체 채팅이 지연된다.
  인원이 늘면 세션별 송신 큐 + 전용 송신 스레드로 바꿔야 한다.
- **스레드를 detach 한다.** 서버 종료 시 클라이언트 스레드를 정리하지 않는다(소켓은 닫힘).
- 접속자 수 상한은 없다 (고정 배열이 아님).
- **UPnP 매핑은 영구(Lease=0)로 잡는다.** Lease 를 지원하지 않는 공유기가 흔해서 그렇게
  택했다(`NatPortMapping.cpp` 주석 참고). 정상 종료(Ctrl+C)면 `Nat::Stop()` 이 지우지만,
  **작업 관리자로 강제 종료하거나 정전이 나면 공유기에 매핑이 그대로 남는다.** 채팅 로그
  유실과 같은 종류의 트레이드오프이지만, 저건 로그 몇 줄이고 이건 방화벽 구멍이라 무게가
  다르다 — 재부팅 후 안 쓰는 매핑이 남아있는지 가끔 공유기 관리 페이지를 확인할 것.
- **9000 포트의 UPnP 매핑은 `--upnp` 를 켰을 때만 시도된다.** 기본값은 꺼짐이라 이 옵션을
  안 주면 예전과 동일하게 같은 네트워크에서만 접속된다 — 팀원들이 각자 로컬로 테스트할 때
  불필요한 포트가 열리지 않게 하려는 것이다.

---

## 12. 다음 단계

우선순위 순으로 정리했다.

### 로비 마무리 (권장 1순위 — 서버/클라이언트 API 와 UI 는 완료)

1. ~~**UI 위젯 3종**~~ → **완료.** 메인메뉴(`ULobbyWidgetBase`), 방 생성창(`URoomCreateWidgetBase`),
   방 목록창(`URoomListWidgetBase`). WBP 없이 동작하고, WBP 로 갈아끼울 수도 있다. [7절](#7-api-레퍼런스) 참고
2. **여행 연결** — 참여자 쪽은 끝났고, **방장 쪽은 맵 이름만 채우면 된다.**
   `ULobbyWidgetBase::HostMapName` 에 맵 이름을 넣으면 게임 시작 시
   `OpenLevel(맵, "listen")` 이 돌고, 리슨서버가 뜨는 즉시 참여자에게 출발 신호가 나간다.
   결정해야 할 것: **게임 맵으로 바로 갈 것인가, 인게임 로딩 맵을 따로 둘 것인가.**
   ~~같이 고칠 것: 참여자가 호스트보다 먼저 붙는 경쟁 상태~~ → **v6 에서 해결됨.**
3. **`AGameModeBase::PreLogin` 에서 `RoomPassword` URL 옵션 재검사** ← **진짜 관문. 다음 할 일.**
   로비 서버 검사는 UX 용이고, 이게 없으면 목록을 안 거치고 IP 직접 접속으로 뚫린다.
   지금 방 비밀번호는 `ULobbyWidgetBase::GetMyRoomPassword()` 에 임시로만 들고 있다 —
   여행 URL 에 실려 가므로(`OpenLevel(맵, "listen?RoomPassword=…")`) 새 레벨의 GameMode 가
   `InitGame` 에서 그 옵션을 읽어 보관하고, `PreLogin` 에서 참여자의 옵션과 비교하면 된다
4. ~~**대기실 준비/시작 버튼**~~ → **완료.** 다만 `APlayerState::bIsReady` 복제가 아니라
   **채팅 서버가 방 멤버와 준비 상태를 들고 있다.** 그래야 맵 없이도 대기실이 돌아가고,
   방장 이탈을 남은 사람에게 즉시 알릴 수 있다
5. ~~인원 변동마다 `UpdateRoomState()` 호출~~ → **불필요해졌다.** 서버가 멤버를 직접 세므로
   방 목록의 인원수가 항상 정확하다. `UpdateRoomState()` 는 진행 상태 통지용으로만 남아 있고
   인원수 필드는 무시된다
6. **게임 시작 후 인게임 연동**: 팀 배정과 생사 여부를 리슨서버가 채팅 서버에 미러링해야
   한다(8단계). 지금은 클라이언트가 `SetDeadForTest` 로 직접 알려서 위조가 가능하다

### 8단계 — 리슨서버 → 채팅서버 신원 미러링

팀 ID 와 생사 여부의 **권위자는 게임(리슨서버)** 인데 채팅 서버는 아직 그걸 모른다.
방 참여로 `TeamId` 를 서버가 정하게 되면 이 문제가 상당 부분 자연스럽게 줄어들 수도 있다
(참여 시점에 서버가 팀을 배정) — 로비 설계와 같이 다시 볼 것.

방향(기존 계획 유지):
1. 리슨서버(호스트)가 채팅 서버에 **관리 연결**을 하나 따로 연다
2. 그 연결에서만 `SetDead` / 팀 배정을 받는다. 일반 클라이언트가 보내면 거부
3. 로그인도 티켓 방식 검토 (계정 시스템이 생겼으니 우선순위는 낮아졌다 — 재논의 필요)

### 6단계 나머지 — 히스토리 조회

오피코드 5/6(`HistoryReq`/`HistoryAck`)은 이미 예약돼 있다. 로그인 직후 최근 N줄을
요청해 채팅창을 채우는 기능. **채널 권한 필터를 히스토리에도 똑같이 적용해야 한다** —
안 하면 지난 팀 대화가 남에게 새어나간다.

### 9단계 — 귓속말

서버 `RouteChat()` 의 `case EChatChannel::Whisper` 에 수신자 1명 찾는 로직만 추가하면 된다.
클라이언트는 `ChatSendBody::TargetUserId` 를 채운다 (이미 필드가 있다).
UI 쪽에 `/w <이름> <메시지>` 파싱을 추가한다 (`UChatWidgetBase::SubmitInput`).
저장까지 하면(메신저처럼 나중에 다시 보기) 히스토리 작업과 자연스럽게 묶인다.

### 그 외 개선 후보

- 접속 정보를 `DefaultGame.ini` 로 빼기 (`Host`/`Port` 하드코딩 제거)
- 서버: 세션별 송신 큐로 락 경합 제거
- 방 참여 시도 횟수 제한 (비밀번호 무작위 대입 방지)
- `.slnx` 파일들 `.gitignore` 처리 여부 결정 (보류 중)

---

## 13. 문제 해결

실제로 겪은 것들이다.

### 어느 공유기에 무엇을 열어야 하는가 (가장 자주 틀리는 곳)

한 번에 정리한다. **포워딩이 필요한 공유기는 두 곳이고, 서로 아무 상관이 없다.**

```
      참여자                          로그인 서버 PC              방장 PC
        │                                   │                       │
        │ ① 로그인·방목록  TCP 9000          │                       │
        ├──────────────────────────────────▶│                       │
        │      서버 공유기가 9000 을 넘겨줌                           │
        │                                                           │
        │ ② 게임 트래픽  UDP 7777                                    │
        ├──────────────────────────────────────────────────────────▶│
                     ★ 서버를 거치지 않는다. 방장에게 직접 간다
```

| 무엇 | 어느 공유기 | 규칙 |
|---|---|---|
| ① 로그인·방목록 | **로그인 서버 PC** 쪽 | 외부 TCP 9000 → 서버 PC LAN IP:9000 |
| ② 게임 접속 | **방장 PC** 쪽 | 외부 UDP 7777 → 방장 PC LAN IP:7777 |

**★ 여기서 하루를 썼다.** 서버 쪽 공유기에 9000 과 7777 을 둘 다 넣어두고
"포워딩은 다 했다" 고 생각했다. 그런데 방장이 **다른 네트워크**에 있으면
참여자의 게임 트래픽은 서버 공유기를 **아예 지나가지 않는다.** 방장 쪽
공유기로 곧장 가고, 거기 7777 이 없으면 그대로 버려진다.

증상은 이렇게 나타난다 — 참여자 화면에 방장의 **공인 IP** 가 정확히 찍히고
(`호스트 218.153.186.240:7777 로 이동합니다`), 그 상태로 MainLobby 에 남는다.
주소가 맞게 찍힌다는 것은 **로그인 서버 경로는 전부 정상**이라는 뜻이다.
막힌 곳은 ② 뿐이다.

#### 방장이 될 사람이 해야 하는 것

방장을 맡을 PC 가 있는 네트워크의 공유기에:

```
외부 UDP 7777  ->  그 PC 의 LAN IP : 7777
```

방장을 돌아가면서 맡을 거면 **그 사람들 전부** 각자 넣어야 한다.
서버 PC 는 이미 넣어뒀으니 서버 PC 가 방장일 때는 추가 작업이 없다.

#### 게임이 직접 알려준다 (2026-08-28)

리슨서버가 뜨는 순간 방장 로그에 값까지 채워서 찍힌다. 공유기 화면에
그대로 옮겨 적으면 된다.

```
[방장] 리슨서버가 192.168.0.32:7777 에서 듣고 있다.
[방장] 다른 네트워크의 참여자가 들어오려면 **이 PC 의 공유기**에 다음이 있어야 한다:
[방장]     외부 UDP 7777  ->  192.168.0.32 : 7777
[방장] 로그인 서버 쪽 공유기에 넣은 포워딩은 이 경로와 무관하다 — ...
```

#### UPnP 는 쓰지 않는다 (2026-08-28)

`DefaultGame.ini` 의 `bUseUpnpPortMapping=False` 다. 수동 포워딩으로
운영하기로 했으므로 시도조차 하지 않는다 — 시도하면 방 만들기 창이 열릴
때마다 SSDP 탐색으로 최대 3초를 버리고, "공유기에 포트를 여는 중" 이 떴다
실패해서 정상인 상황도 잘못된 것처럼 보인다.

지금 공유기가 UPnP 를 지원하는지만 확인하고 싶으면 설정과 무관하게:

```
MOU.Nat.Open 7777
```

이번 실행만 켜려면 실행 인자로 `-MOUUseUpnp=1`.

### 참가자가 "호스트 192.168.x.x:7777 로 이동합니다" 에서 무한 로딩

2026-08-26. 로그인·방 생성·참여·준비까지 다 되는데 **게임 시작**에서만 멈춘다.
참가자 화면에 뜬 주소가 **사설 IP**(게이트웨이 IP 인 경우가 많다)면 이것이다.

#### 왜 사설 주소가 전달됐나

방의 호스트 주소는 클라이언트가 신고하는 값이 아니라 서버가 `accept()` 에서 읽은
`Session::PeerAddress` 다. 클라이언트가 "내 IP 는 여기다" 라고 보내오는 값을 믿으면
남의 주소를 적어 엉뚱한 곳으로 접속을 몰아줄 수 있어서, **일부러** 이렇게 만들었다
(`ChatProtocol.h` 의 `RoomCreateReqBody` 주석).

이 설계는 호스트가 서버와 **다른** 공유기 뒤에 있을 때는 정확하다 — 그때 피어 주소가
곧 호스트의 공인 IP 다. 문제는 호스트가 서버와 **같은** 공유기 안에 있을 때다:

- 호스트가 LAN IP 로 서버에 붙으면 → 피어 주소가 `192.168.x.x`
- 호스트가 공인 IP 로 붙어 헤어핀을 타면 → 공유기가 출발지를 자기 주소로 바꿔서
  **게이트웨이 IP**(예: `192.168.35.1`) 가 찍힌다

어느 쪽이든 외부 참가자는 그 주소로 갈 수 없다. 그런데 로그인·방 목록은 전부
서버(9000)를 거치므로 **거기까지는 멀쩡히 동작한다.** 그래서 "다 되는데 시작만 안 되는"
모습이 된다.

#### 해결 — `--public-ip`

서버에 자기 공인 IP 를 알려주면, 피어 주소가 사설일 때 그 값으로 바꿔 기록한다.
클라이언트가 보내온 값은 여전히 쓰지 않는다 — 서버가 아는 두 값 중에서만 고른다.

```bash
Server.exe 9000 chat_log.db --public-ip 211.244.139.254
```

`run_server.bat` 은 공인 IP 를 자동으로 조회해서 이 인자를 붙여준다. 그냥 쓰면 된다.
`--upnp` 가 성공하면 거기서 알아낸 외부 IP 를 자동으로 쓰므로 인자를 생략해도 된다.

사설 주소를 `--public-ip` 로 주면 상황만 나빠지므로 서버가 시작을 거부한다.

켤 때 이 줄이 찍히면 적용된 것이다:

```
[방 주소] 방장이 이 서버와 같은 네트워크에 있으면 호스트 주소를 211.244.139.254 로 기록한다.
```

방을 만들 때는 이렇게 찍힌다:

```
[방 주소] 호스트가 서버와 같은 네트워크에 있다(127.0.0.1). 외부 참가자를 위해 211.244.139.254 로 바꿔 기록한다.
[방 생성] #1 "TestRoom" 방장=... 주소=211.244.139.254:7777
```

#### 주소만 고쳐서는 안 된다 — 7777 도 열어야 한다

주소가 공인 IP 로 바뀌어도, 그 주소의 **7777/UDP 가 방장 PC 로 포워딩**되어 있지 않으면
참가자는 여전히 못 붙는다. 공유기가 UPnP 를 지원하면 언리얼 쪽
`NatPortMappingSubsystem` 이 자동으로 열지만, UPnP 가 꺼져 있으면 수동으로 넣어야 한다:

```
외부 UDP 7777  ->  방장 PC 의 LAN IP : 7777
```

즉 서버 PC 가 방장도 겸하는 구성이라면 그 공유기에 **두 줄**이 필요하다:
`TCP 9000`(로그인/로비) 과 `UDP 7777`(게임).

#### 한계

호스트와 참가자가 **둘 다** 서버와 같은 공유기 안에 있으면, 참가자도 공인 IP 로 나갔다
돌아오는 헤어핀 접속을 하게 된다. 공유기가 헤어핀을 지원하지 않으면 그 조합만 실패한다.
전원이 같은 LAN 안에서 테스트할 거라면 `--public-ip` 를 빼고 켜는 편이 낫다.
(제대로 풀려면 STUN/ICE 처럼 후보 주소를 여러 개 주고 되는 것을 고르게 해야 하는데,
그건 14-5 절의 릴레이 전환과 같이 갈 이야기다.)

### 다른 네트워크의 팀원이 로그인을 못 한다

2026-08-26 에 실제로 겪은 것이다. 원인이 **세 겹**이라 하나씩 벗겨야 한다.
`run_server.bat` 이 이 세 가지를 켜기 전에 먼저 확인해준다.

**1) `Server.exe` 가 NAT 이전 빌드다 — `[NAT]` 로그가 아예 안 뜬다**

`--upnp` 를 줬는데 콘솔에 `[NAT]` 로 시작하는 줄이 **하나도** 없으면 이것이다.
새 코드는 성공하든 실패하든 무조건 `[NAT]` 를 출력하므로, 아무것도 없다는 건
그 코드가 바이너리에 없다는 뜻이다.

옛 `main()` 은 인자 검사가 `argc < 2 || argc > 3` 이라서 `Server.exe 9000 --upnp` 가
**에러 없이 통과한다.** 대신 `--upnp` 를 **DB 경로로 해석**해서 `--upnp` 라는 이름의
빈 SQLite 파일을 만든다. 그래서 계정이 전부 사라진 것처럼도 보인다.

> `build_server.bat` 이 VS 경로를 `2022\Community` 로 하드코딩하고 있어서, VS 2026 만
> 깔린 PC 에서는 `cl` 을 못 찾고 **조용히** 빌드가 실패해 옛 exe 가 그대로 남았다.
> 지금은 설치 경로를 후보 목록에서 자동으로 찾고, 실패하면 `[ERROR]` 를 찍고 멈춘다.

**2) 클라이언트가 아직 사설 IP 를 보고 있다**

`DefaultGame.ini` 의 `ServerHost` 가 `192.168.x.x` 면 **그 공유기 안에서만** 통한다.
UPnP 는 클라이언트가 어디로 붙을지를 바꿔주지 않는다 — 공유기에 구멍만 뚫을 뿐,
그 구멍의 **바깥쪽 주소(공인 IP)** 는 클라이언트가 직접 알고 찍어야 한다.

**3) 공유기에 포트가 안 열려 있다**

UPnP 가 `NoGatewayFound`(SSDP 무응답) 면 공유기가 UPnP 를 끄고 있거나 지원하지 않는다.
이때는 공유기 관리 페이지에서 **수동 포트포워딩**을 넣는다:

```
외부 TCP 9000  ->  서버 PC 의 LAN IP : 9000
```

> **★ "포트포워드 활성" 체크박스를 반드시 확인할 것.** 2026-08-26 에 이것 때문에
> 두 번 막혔다. 목록에 항목이 멀쩡히 들어가 있어도 이 체크가 꺼져 있으면 규칙이
> **통째로 무시된다.** 게다가 화면상으로는 목록에 줄이 보이니 다 된 것처럼 보인다.
>
> 순서도 중요하다: 항목을 넣고 `추가` → 목록에 줄이 생긴 것 확인 → `포트포워드 활성`
> 체크 → **`저장 후 적용`**. `추가`만 누르고 `저장 후 적용` 을 안 누르면 적용되지 않는다.
>
> 증상은 "밖에서 ping 은 되는데(공유기가 응답) TCP 만 안 붙는다" 로 나타난다.

`CarrierGradeNat` 이면 수동 포워딩으로도 안 된다 — 회선 자체가 공인 IP 를 안 준다.
14-5 절대로 릴레이(EOS/Steam P2P)가 필요하다.

#### CGNAT 인지 판별하기

```bash
tracert -d -h 5 8.8.8.8
```

홉 1 은 공유기다. **홉 2 가 공인 IP** 면(우리 공인 IP 와 같은 대역이면 더 확실하다)
단일 NAT 이므로 포트포워딩으로 해결된다. 홉 2 가 `100.64.x.x` 나 `10.x.x.x` 같은
사설/CGNAT 대역이면 이중 NAT 이라 포트포워딩이 무의미하다.

#### 확인 순서

```bash
MOU_Server\run_server.bat
```

`LAN IP` / `Gateway` / `Public IP` 와, `DefaultGame.ini` 의 `ServerHost` 일치 여부가
먼저 찍힌다. `[WARN]` 이 뜨면 공인 IP 가 바뀐 것이니 ini 를 고쳐 커밋한다.

서버를 켠 뒤 세 주소로 각각 붙여보면 어디까지 되는지 바로 갈린다:

```bash
powershell -Command "Test-NetConnection 127.0.0.1 -Port 9000; Test-NetConnection <LAN IP> -Port 9000; Test-NetConnection <공인 IP> -Port 9000"
```

- 127.0.0.1 실패 → 서버가 안 떠 있다
- LAN 실패 → 방화벽. `Server.exe` 인바운드 허용을 확인한다
- 공인 IP 만 실패 → **포트포워딩이 없다.** 팀원도 못 붙는다

#### 서버를 켠 PC 본인이 못 붙는 경우

공유기가 NAT 헤어핀(자기 공인 IP 로 나갔다 자기한테 돌아오는 접속)을 지원하지 않으면
서버 PC 에서만 접속이 실패한다. 공유 설정을 고치지 말고 그 PC 에서만:

```
MOU.Chat.SetServer 127.0.0.1 9000
```

`Saved/Config/` 에만 저장돼 git 에 안 올라간다. 되돌리려면 인자 없이 `MOU.Chat.SetServer`.

#### 왜 배치 파일에 한글이 없나

`cmd` 는 `.bat` 을 **콘솔 코드페이지**로 파싱한다. 이 값이 PC 마다 다르다(949 / 65001).
UTF-8 로 저장한 한글 주석이 949 콘솔에서는 명령어로 오해돼서 **스크립트 자체가 깨진다**
(`'d' is not recognized...`). 그래서 `build_server.bat` / `run_server.bat` 은 ASCII 만 쓰고,
설명은 이 문서에 둔다.

### "이 프로젝트는 현재 버전의 Visual Studio와 호환되지 않습니다"

**원인**: 프로젝트 파일이 다른 VS 버전용으로 생성됐다.
VS 2022 와 2026 이 둘 다 설치돼 있으면 UBT 가 자동으로 최신(2026)을 고른다.

**확인**: `Intermediate/ProjectFiles/TeamProject_MOU.vcxproj` 첫 줄의 `ToolsVersion`
(2022 = `17.0`, 2026 = `18.0`) 과 `UECommon.props` 의 `PlatformToolset` (`v143` / `v145`).

**해결**: [2-3 절](#2-3-visual-studio-버전-고정--중요) 의 `BuildConfiguration.xml` 설정 후 프로젝트 파일 재생성.
**폴더를 지울 필요는 전혀 없다.** `Binaries`/`Intermediate`/`DerivedDataCache`/`Saved` 삭제는
이 문제와 무관하고 전체 리빌드·셰이더 재컴파일만 유발한다.

### 빌드 중 `VirtualAlloc failed commit ... The paging file is too small`

**원인**: 빌드 가속기(UBA)가 기본 40GB 를 예약하는데 시스템 커밋 한계에 걸렸다.
VS 가 UE5 솔루션(59개 프로젝트) 을 열어둔 채 IntelliSense 를 돌리면 특히 잘 터진다.

**해결 (택 1)**:
1. `BuildConfiguration.xml` 에 `<bAllowUBAExecutor>false</bAllowUBAExecutor>` — **가장 간단.**
2. 페이지 파일 늘리기 (시스템 속성 → 고급 → 성능 → 고급 → 가상 메모리)
3. `<UnrealBuildAccelerator><StoreCapacityGb>12</StoreCapacityGb></UnrealBuildAccelerator>`

### `LNK2019: Z_Construct_UEnum_SlateCore_ETextCommit`

`Build.cs` 의 `PublicDependencyModuleNames` 에 `"SlateCore"` 가 빠졌다. 이미 추가돼 있다.

### `ChatProtocol.h` 를 찾을 수 없음

`TeamProject_MOU` 폴더만 복사했을 때 발생한다. 저장소 전체를 받아야 한다.

### CMake 설정(configure) 단계에서 `sqlite3.c` 관련 오류 / C 컴파일러를 못 찾음

`project()` 선언이 `CXX` 만 있고 `C` 가 빠졌을 때 난다. 이미 `project(MOU_Server C CXX)` 로
고쳐져 있다. 혹시 캐시가 꼬였으면 `out/build/x64-Debug` 를 지우고 다시 configure.

### `Server.exe` 를 실행했는데 `LINK : fatal error LNK1168` 로 재빌드가 안 됨

이전에 띄운 `Server.exe` 가 아직 실행 중이라 파일이 잠긴 것이다.
작업 관리자에서 종료하거나 `Get-Process Server | Stop-Process -Force` 후 다시 빌드.

### 프로토콜 버전 불일치인데 클라이언트 버전이 이상한 숫자로 찍힘

```
[거부] 프로토콜 버전 불일치. 클라이언트=28520, 서버=5
```

28520 은 `0x6F68` = `'h','o'` — **아이디 첫 두 글자**다. 즉 클라이언트가 `Version` 필드가
없던 시절(v1)의 `LoginReqBody` 를 보내고 있다는 뜻이다. 원인은 십중팔구
**빌드 구성이 달라서 낡은 DLL 이 로드된 것**이다.

`Binaries/Win64/` 를 보면 구성마다 DLL 이 따로 있다:

| 파일 | 구성 |
|---|---|
| `UnrealEditor-TeamProject_MOU.dll` | Development Editor |
| `UnrealEditor-TeamProject_MOU-Win64-DebugGame.dll` | DebugGame Editor |

`UnrealEditor.exe` / `UnrealEditor-Cmd.exe` 를 플래그 없이 실행하면 **Development** 를 쓴다.
DebugGame 만 빌드해두고 헤드리스 검증을 돌리면 몇 달 전 DLL 로 붙는다.
**두 구성을 같이 빌드하거나, 팀에서 쓰는 구성 하나로 통일할 것.**

### VS 에 새 파일이 안 보임

프로젝트 파일 재생성 후 VS 를 **완전히 껐다 켠다.** 열어둔 채 재생성하면 옛 정보를 들고 있다.

### 한글 주석이 깨져 보임 / `C2001` 같은 컴파일 에러

파일 인코딩이 UTF-8 BOM 에서 다른 것으로 바뀌었다. [10절](#10-이-시스템의-코딩-규칙) 참고.

### PIE 를 껐다 켜면 에디터가 크래시

워커 스레드 정리 순서 문제다. `ChatThread->Kill(true)` 의 인자가 `true` 인지 확인할 것.

### PIE 다중 창에서 채팅 UI 가 첫 번째 창에만 뜸

**해결됨.** `ChatWidgetBase.cpp` 가 전역 위젯 포인터 대신 `TMap<TWeakObjectPtr<UWorld>, ...>` 로
창마다 별도 위젯을 추적한다. 옛 버전 코드가 남아있다면 병합이 필요하다.

### 로그인했는데 UserId 가 매번 바뀜

**해결됨 (v3).** `UserId` 가 이제 `accounts.id` 다. 여전히 바뀐다면 계정을 매번 새로 만들고
있는 것은 아닌지(`--register` 를 재로그인 때도 계속 주고 있는지) 확인할 것.

### 채팅을 보냈는데 아무 일도 안 일어남

체크리스트:
1. `Server.exe` 가 켜져 있나
2. 상태가 `LoggedIn` 인가 (`Connected` 만으로는 서버가 버린다)
3. 사망 채널로 보내고 있는데 살아있는 상태 아닌가 (서버가 **무응답으로 폐기**한다)
4. 메시지가 512바이트를 넘지 않나
5. 에디터 출력 로그에서 `LogMOUServer` 필터로 경고 확인

### 방 목록에 방이 안 보임

체크리스트:
1. 방을 만든 쪽의 TCP 연결이 아직 살아있나 (연결이 끊기면 서버가 방을 자동 삭제한다)
2. 정원이 꽉 찼거나(`CurrentPlayers >= MaxPlayers`) 이미 `InGame` 상태인 방은 목록에서 빠진다
   (`Rooms::ListWaiting` 이 필터링한다) — `MOU.Room.Leave` 후 다시 만들어서 확인
3. 로그인 상태인가 — 로그인 전에는 서버가 빈 목록만 돌려준다

---

## 14. 설계 근거 — 외부 피드백에 대한 답변

> 이 절은 코드 사용법이 아니라 **왜 이 구조를 골랐는가**를 다룬다.
> "별도 백엔드 구축을 지양하라 / 상용 OSS(EOS·Steam)를 쓰라"는 피드백을 받았고,
> 그에 대한 우리 파트의 답변과 앞으로의 계획을 정리한 것이다.

### 14-1. 요약 — 한 문단

우리가 띄우는 `Server.exe`는 **데디케이트 게임 서버가 아니다.** 이동·전투·GAS를 처리하지
않고, 애초에 처리할 수 없다. 이 프로세스가 하는 일은 두 가지뿐이다 — **"누가 방을 열었는지
알려주는 주소록"** 과 **"리슨서버가 죽어도 살아남는 계정 저장소"**. 게임 트래픽은
참가자가 호스트의 리슨서버에 직접 붙어서 주고받고, 이 프로세스를 **한 바이트도 지나가지
않는다.** 즉 우리가 만든 것은 EOS Session / Steam Lobby가 하는 일을 직접 구현한 것이지,
피드백이 지양하라고 한 그 백엔드가 아니다.

### 14-2. 왜 리슨서버만으로는 안 되는가 — 두 가지 구멍

리슨서버(호스트가 곧 서버)로 게임 로직을 처리하는 것은 확정 사항이고 바꾸지 않는다.
다만 리슨서버에는 **게임 로직과 무관한** 구멍이 두 개 있다.

**1. 리슨서버는 자기 존재를 알릴 방법이 없다.**

```
방이 열리기 전  →  연결된 세션이 없다  →  RPC를 주고받을 상대가 없다
                                          ↑
                        "누가 방을 열었는가"를 알려면
                        리슨서버 바깥 어딘가에 그 정보가 있어야 한다
```

RPC는 **이미 연결된** 클라이언트–서버 사이에서만 동작한다. 그런데 "누구에게 연결할지
정하는 단계"는 그보다 앞이다. 이 순서 문제는 리슨서버 안에서는 풀 수 없다.

**2. 리슨서버는 계정을 영속시킬 수 없다.**

리슨서버는 호스트가 나가면 프로세스째 사라지는 휘발성이다. 같은 계정으로 다시 들어왔을 때
`UserId`가 유지되고, 나중에 붙일 커스터마이징 데이터가 남아 있으려면 **리슨서버가 죽어도
살아있는 저장소**가 필요하다. 계정을 리슨서버에 두면 방이 닫히는 순간 전부 날아간다.

> 상용 게임들도 같은 구조다. 게임 트래픽은 리슨서버(P2P)로 돌리고, 세션 탐색과 계정은
> Steam Lobby / EOS Session 같은 **별도 서비스**에 맡긴다. 우리가 한 것도 같은 분업이고,
> 그 별도 서비스를 남의 것 대신 직접 구현했을 뿐이다.

### 14-3. "데디케이트 서버가 아니다"의 객관적 근거

말이 아니라 코드로 확인할 수 있는 근거만 적는다.

| 근거 | 확인 방법 |
|---|---|
| **언리얼 엔진 코드가 0줄이다** | `MOU_Server/CMakeLists.txt` — CMake + Winsock + SQLite로만 빌드된다. 데디케이트 서버라면 있어야 할 `*.Target.cs`의 Server 타깃이 없다 |
| **프로토콜에 게임플레이 패킷이 없다** | `Shared/ChatProtocol.h`의 `EOpcode` 전체 — 로그인/계정생성/방 CRUD/채팅뿐이다. 좌표·입력·어빌리티 패킷이 **물리적으로 존재하지 않는다** |
| **주소를 넘겨주고 손을 뗀다** | `FMOURoomJoinResult::MakeTravelURL()`이 `HostAddress:HostPort`를 만들어주면 클라이언트는 호스트로 `ClientTravel` 한다. 그 뒤 트래픽은 이 프로세스를 타지 않는다 |
| **부하가 방 개수와 무관하다** | `Rooms.cpp`는 방 메타데이터만 `std::map`에 들고 있다. 방이 100개여도 늘어나는 것은 구조체 100개뿐이다 |

**즉 인프라 비용을 늘린 게 아니라 줄인 설계다.** 방마다 서버 인스턴스를 띄우는 구조가
아니라, 프로세스 하나가 모든 방의 주소록 역할만 한다.

### 14-4. 왜 EOS·Steam이 아니라 직접 구현했는가

**핵심: 채팅 때문에 어차피 상시 프로세스가 하나 필요했고, 방 목록과 계정을 거기에 얹은 것이다.**
로비 전용 서버를 새로 띄운 게 아니라 **필요한 프로세스를 하나로 합친 쪽**이다.

EOS로 옮길 수 없는 기능이 이미 이 프로세스 안에 있다.

| 기능 | EOS로 대체 | 대체할 수 없는 이유 |
|---|---|---|
| 방 목록 / 세션 탐색 | ✅ 가능 | EOS Session |
| 계정 인증 | ✅ 가능 | EOS Connect |
| **사망자 채널** | ❌ **불가** | "죽은 사람에게만 보이는 채팅"은 **게임 상태를 아는 쪽**만 판정할 수 있다. EOS는 누가 죽었는지 모른다 (`ClientSession::bDead`, `Server.cpp`의 `RouteChat`) |
| **채팅 로그 영속화** | ❌ **불가** | 호스트가 나가도 남아야 한다. SQLite `chat_log` 테이블 |
| **커스터마이징 데이터** | ❌ **불가** | 계정에 묶인 게임 데이터. EOS Player Data Storage는 용도가 다르고 별도 비용·제한이 붙는다 |

그 밖의 현실적인 이유:

- **팀 전원이 즉시 돌릴 수 있다.** 외부 SDK도, Epic/Steam 계정도, 인터넷 연결도 필요 없다.
  EOS를 붙이는 순간 팀원 전원이 Dev Portal 설정을 공유해야 하고, Steam은 AppID와
  Steam 클라이언트 실행이 전제가 된다. 개발·수업 시연 단계에서 이건 순수한 마찰이다.
- **학습 목적에 부합한다.** 프레이밍(길이 프리픽스), 스레드 경계, 세션 관리, PBKDF2 해싱을
  직접 구현한 경험은 SDK를 붙이는 것으로는 얻을 수 없다.
- **디버깅이 투명하다.** 서버 콘솔에 모든 판정 사유가 찍힌다. SDK 내부에서 실패하면
  로그가 우리 손을 떠난다.

**동시에, 피드백을 거부하는 것이 아니다.** OSS 전환 경로를 이미 코드에 만들어뒀다 (14-6절).

### 14-5. 인정하는 한계 — NAT (2026-08-25 갱신: 일부 해결됨)

**자체 구현이 풀지 못했던 문제는 방 목록이 아니라 NAT였다. 흔한 경우는 이제 자동으로 풀린다.**

```
이전 구조:
  참가자 ──────────────────────▶ 호스트 공인IP:7777
            공유기가 이 접속을 막는다 (포트포워딩이 없으면)

지금 구조 (2026-08-25):
  방장이 "방 만들기" 를 여는 순간
      └─▶ 방장의 PC 가 UPnP 로 공유기에 "7777 열어줘" 요청 (백그라운드)
             └─▶ 성공하면 그 외부 포트로 방을 등록 → 참가자는 그냥 접속하면 된다
             └─▶ 실패하면(미지원/CGNAT) 예전과 같은 LAN 전용 상태로 조용히 되돌아간다

  같은 방식으로 Server.exe 자신의 9000 포트도 `--upnp` 플래그로 열 수 있다
  (로그인 서버가 로컬 PC 에서 돌 때만 필요 — 원래부터 공인 IP 를 가진 곳에서
  돌린다면 이 문제 자체가 없다)
```

`ClientSession::PeerAddress`는 서버가 `accept()` 시점에 읽은 상대 IP다. 클라이언트가
신고한 값이 아니라 위조는 안 된다. **UPnP 매핑이 성공하면 그 IP의 (외부) 포트가 자동으로
열리므로** 더는 호스트가 수동으로 포트포워딩을 할 필요가 없다.

| 시연 환경 | 동작 여부 |
|---|---|
| 같은 공유기 / 교내망 (LAN) | ✅ 동작 (원래부터) |
| 각자 집에서 인터넷으로, 공유기가 UPnP 지원 | ✅ **자동 동작 (2026-08-25 추가)** — 수동 포트포워딩 불필요 |
| 각자 집에서 인터넷으로, UPnP 미지원/꺼짐 | ⚠️ 실패 시 LAN 전용으로 되돌아감. 수동 포트포워딩으로 우회 가능 |
| 방장이 통신사 NAT(CGNAT) 안 | ❌ **여전히 안 됨.** UPnP 로 근본적으로 못 푸는 문제 |

**CGNAT 은 자체 구현으로 못 고친다.** 공유기의 "외부" IP 자체가 사설 대역이라, 그 위에
통신사 장비가 한 겹 더 있다는 뜻이다. UPnP 는 내 공유기에만 말을 걸 수 있지 그 위의
통신사 장비에는 요청을 보낼 방법이 없다. 이건 홀펀칭이나 릴레이 서버가 필요하고,
그게 정확히 EOS P2P가 제공하는 가치다. **그래서 최종 단계에서 EOS를 붙일 계획이고,
그때 교체되는 것은 (UPnP 로 이미 자동화한 부분을 포함한) 세션 탐색 계층 전체다.**

> UPnP 구현 위치와 왜 두 프로세스(`Server.exe`, 언리얼 클라이언트)에 각각 있는지는
> 5절 "NAT 자동 포트포워딩" 참고.

### 14-6. 앞으로의 개발 계획

**전환 비용을 미리 확정해뒀다.** `ILobbyBackend` 인터페이스(v6)를 도입해서, EOS로 가는 것이
프로젝트 전체를 뒤집는 일이 아니라 **파일 하나를 채우는 일**이 되도록 만들었다.

```
UServerSubsystem            BP API / 상태 / 델리게이트   ← 백엔드를 모른다
  └─ ILobbyBackend                                    ← 교체 지점
       ├─ FSocketLobbyBackend   자체 서버 (현재 기본값)
       └─ FEOSLobbyBackend      EOS      (뼈대 + 전환 주석)
```

| | |
|---|---|
| **안 바뀜** | `UServerSubsystem`의 블루프린트 API, UMG 위젯 5종, 게임 로직 전부 |
| **바뀜** | `FEOSLobbyBackend`의 내용, 그리고 그것을 고르는 설정값 한 줄 |

전환은 `Project Settings → Game → MOU Server → Lobby Backend`에서 고르거나
실행 인자 `-MOULobbyBackend=EOS`로 한다. 자세한 내용은 [5절 백엔드 교체](#5-구조) 참고.

**단계별 계획**

| 단계 | 내용 | 상태 |
|---|---|---|
| 1 | 자체 서버로 로비·계정·대기실 완성 | ✅ 완료 |
| 2 | `ILobbyBackend`로 교체 지점 분리 | ✅ 완료 (v6) |
| 3 | 호스트 준비 신호로 여행 경쟁 상태 제거 | ✅ 완료 (v6) |
| 3.5 | UPnP 자동 포트포워딩 — NAT 부분 해결 (CGNAT 제외) | ✅ **완료 (2026-08-25)** |
| 4 | `PreLogin`에서 방 비밀번호 재검사 (보안 관문) | ⬜ 다음 작업 |
| 5 | LAN 환경 3~4인 실기기 시연 | ⬜ |
| 6 | `FEOSLobbyBackend` 구현 — **CGNAT** 해결 (UPnP 로 안 되는 나머지) | ⬜ 여력이 되면 |

**EOS를 붙일 때의 최종 구성**은 전면 교체가 아니라 **하이브리드**다.

```
EOS Connect  ──▶ ProductUserId ──▶ accounts 테이블의 외부 키로 저장
EOS Session  ──▶ 방 목록 / NAT 통과
자체 서버    ──▶ 채팅 (사망자 채널) / 채팅 로그 / 커스터마이징 데이터
```

상용 게임들이 실제로 쓰는 구조다. 붙이는 순서(Dev Portal 등록 → 플러그인 3종 →
`DefaultEngine.ini` → `Build.cs` → 각 함수 구현)는 `Server/Lobby/EOSLobbyBackend.h` 주석에
단계별로 적어뒀다.

### 14-7. 예상 질문

**Q. 그래도 서버를 하나 띄우는 건 맞지 않나?**
맞다. 다만 그 서버는 게임 트래픽을 처리하지 않으므로 **사양과 비용이 게임 서버와 다르다.**
방 100개가 열려도 메모리에 구조체 100개가 늘 뿐이라, 무료 티어 VM 한 대로 충분하다.
EOS를 쓰더라도 채팅·로그·커스터마이징 때문에 이 프로세스는 어차피 남는다.

**Q. 그럼 EOS를 지금 붙이면 되지 않나?**
UPnP 자동 포트포워딩(2026-08-25)으로 "각자 집에서 붙는" 시연의 상당수는 이미 된다 —
가정용 공유기 대부분이 UPnP 를 지원한다. **남은 것은 CGNAT 뿐이고**, 그건 팀원이나
플레이어가 어떤 통신사·회선을 쓰는지에 달려 있어 우리가 통제할 수 없는 변수다.
EOS를 지금 붙이면 팀 전원이 Dev Portal 설정을 공유해야 해서 개발 속도만 떨어지는데,
그 대가로 얻는 게 "CGNAT 인 사람만 추가로 접속 가능"이라 지금 단계에서는 맞지 않는
교환이라고 본다. 전환 경로는 이미 코드에 있으므로 CGNAT 이 실제로 발목을 잡을 때 붙인다.

**Q. 왜 데디케이트 서버가 필요하다고 보고하지 않았나?**
필요하지 않기 때문이다. 우리 구조에서 게임 시뮬레이션은 전부 호스트의 리슨서버가 한다.
데디케이트 서버를 띄우면 인프라 비용만 늘고 얻는 것이 없다.

---

## 15. 회고 — UPnP 와 서버 주소 (2026-08-25 ~ 08-27)

이 절은 다른 절과 목적이 다르다. [13절](#13-문제-해결)이 **"지금 안 될 때 무엇을 확인하는가"**
라면, 여기는 **"왜 이런 문제가 생겼고, 무엇을 배웠고, 다음에 어떻게 만들 것인가"** 를 남긴다.
나중에 이 작업을 설명해야 할 때(포트폴리오·발표·인수인계) 쓸 재료다.

### 15-1. 한 줄 요약

> 팀원 화면에는 **"친구가 못 들어온다"** 하나로 보였던 증상이, 실제로는 **성격이 다른
> 다섯 개의 문제**였다. 사흘 동안 하나씩 벗겨내면서, 각 문제를 **다시 겪지 않도록
> 스크립트와 로그에 못을 박는 것**까지를 한 세트로 처리했다.

### 15-2. 문제 지도 — "도달성"은 하나가 아니라 세 개의 독립된 축이다

이 작업에서 가장 값이 컸던 깨달음이다. 처음에는 전부 "NAT 문제" 한 덩어리로 봤는데,
실제로는 **서로 독립적으로 실패할 수 있는 세 축**이었다.

| # | 축 | 실패하면 나타나는 증상 | 필요한 것 |
|---|---|---|---|
| 1 | **TCP 9000** 도달성 (로그인·로비·채팅) | 로그인 자체가 안 됨 | 서버 PC 공유기의 포트포워딩 |
| 2 | **UDP 7777** 도달성 (게임 리슨서버) | 로그인·방 참여는 되는데 **게임 시작만** 무한 로딩 | 방장 PC 공유기의 포트포워딩 |
| 3 | **주소 인지** (클라가 어느 IP 를 보는가) | 위 둘이 다 열려 있어도 못 붙음 | `DefaultGame.ini` 의 `ServerHost` + 방 호스트 주소 |

**이 작업의 핵심 통찰:**

```text
UPnP 는 1번과 2번만 푼다. 3번은 풀어주지 않는다.
공유기에 "구멍을 뚫는 것" 과, 그 구멍의 "바깥쪽 주소를 알려주는 것" 은 다른 일이다.
```

이 둘을 구분하지 못해서 *"UPnP 를 붙였는데 왜 안 되지"* 를 반복했다. `--upnp` 가 성공해
`[NAT] 포트를 열었다` 가 찍혀도, 클라이언트가 `192.168.x.x` 를 보고 있으면 아무 일도
일어나지 않는다. UPnP 는 클라이언트 쪽을 건드리지 않기 때문이다.

또 1번과 2번이 **다른 PC 의 다른 프로토콜**이라는 것도 늦게 알았다. 서버 PC 가 방장을
겸하는 구성에서는 같은 공유기에 **두 줄**이 필요하다 — `TCP 9000` 과 `UDP 7777`.
1번만 열어두면 "로그인·방 목록·참여·준비까지 전부 되는데 시작만 안 되는" 모습이 되는데,
이게 가장 오진하기 쉬운 상태였다. **되는 게 너무 많아서 네트워크를 의심하지 않게 된다.**

### 15-3. 시행착오 기록

각 항목은 `증상 → 왜 오진했나 → 진짜 원인 → 개선 → 일반화` 순이다.
*"왜 오진했나"* 를 같이 적는 이유는, 다음에 비슷한 상황에서 **같은 방식으로 속지 않기**
위해서다. 원인만 적어두면 그 지식은 그 버그에서만 쓰인다.

---

#### T-1. 빌드가 조용히 실패해서, 옛 바이너리로 하루를 태웠다

| | |
|---|---|
| **증상** | NAT 코드를 넣고 커밋했는데 서버 콘솔에 `[NAT]` 로그가 **하나도** 안 찍힘 |
| **왜 오진했나** | 코드는 분명히 들어가 있으니 *"UPnP 가 안 되는 공유기인가 보다"* 로 넘어갔다. 빌드가 됐다는 것을 **의심 목록에 넣지 않았다** |
| **진짜 원인** | `build_server.bat` 이 VS 경로를 `2022\Community` 로 하드코딩. VS 2026(`18\Community`)만 깔린 PC 에서 `cl` 을 못 찾고 **에러 없이** 종료 → 옛 `Server.exe` 가 그대로 남음 |
| **개선** | VS 설치 경로를 후보 목록에서 자동 탐색. 못 찾거나 컴파일이 실패하면 `[ERROR]` 를 찍고 `exit /b 1`. `run_server.bat` 은 켜기 전에 **exe 안에 `--upnp` 문자열이 있는지** 로 NAT 빌드 여부를 먼저 판정한다 |

> **일반화.** 실패는 **소리를 내야 한다.** 조용한 실패는 버그보다 비싸다 —
> 버그는 증상이 원인 근처에 있지만, 조용한 실패는 증상이 **엉뚱한 곳** 에 나타난다.
> 여기서는 "빌드 스크립트 문제" 가 "UPnP 문제" 로 위장했다.

---

#### T-2. 잘못된 인자가 에러 없이 통과했다

| | |
|---|---|
| **증상** | `Server.exe 9000 --upnp` 를 줬는데 UPnP 가 안 돌고, 게다가 **계정이 전부 사라진 것처럼** 보임 |
| **왜 오진했나** | 인자를 틀리게 줬으면 프로그램이 알려줄 거라고 가정했다 |
| **진짜 원인** | 옛 `main()` 의 인자 검사가 개수 검사(`argc < 2 \|\| argc > 3`)뿐이었다. `--upnp` 가 **개수만 통과** 해서 **DB 경로로 해석** 됐다 → `--upnp` 라는 이름의 빈 SQLite 파일이 생기고 계정 테이블이 빈 상태로 열림 |
| **개선** | 인자 파서를 개수 검사에서 **의미 검사** 로 교체. `--` 로 시작하는 토큰은 절대 위치 인자로 먹지 않는다 |

> **일반화.** 위치 인자와 플래그를 개수로만 검사하면 **잘못된 입력이 유효한 입력으로
> 둔갑한다.** 특히 그 위치가 "파일 경로" 면 프로그램이 시키는 대로 파일을 만들어버려서
> 데이터가 사라진 것처럼 보인다.

---

#### T-3. UPnP 탐색이 9초 동안 서버를 붙잡고 있었다

| | |
|---|---|
| **증상** | PIE 에서 방 만들기 창을 열면 *"공유기에 포트를 여는 중입니다..."* 가 한참. 서버 쪽은 그동안 `accept` 루프에도 못 들어감 |
| **왜 오진했나** | "원래 네트워크 탐색은 느린 것" 이라고 넘겼다. 코드에 3초 타임아웃이라고 적혀 있어서 3초로 믿었다 |
| **진짜 원인** | SSDP 검색 대상 3개(`IGD:1`, `IGD:2`, `rootdevice`)를 **순차로** 돌면서 각각 전체 타임아웃을 소진 → **3 × 3초 = 9초** |
| **개선** | 세 요청을 **먼저 전부 쏘고 한 번만 기다린다.** SSDP 응답은 소켓 하나로 비동기로 돌아오므로 요청을 직렬화할 이유가 없었다. `MX` 도 2 → 1 |

| | 예전 | 지금 |
|---|---|---|
| UPnP 없는 공유기 (최악) | 9초 | **3초** |
| UPnP 되는 공유기 | ~100ms | 동일 |

> **일반화.** *"요청 하나에 응답 하나를 기다린다"* 는 습관이 만든 직렬화였지, 프로토콜의
> 요구가 아니었다. **프로토콜이 강제하는 순서와 코드가 만든 순서를 구분** 해야 한다.
>
> 그리고 이 코드는 **서버(`MOU_Server/`)와 언리얼(`Private/Server/Net/`)에 두 벌** 있다.
> 한쪽만 고치면 반쪽만 빨라진다 — 의도적 이중화에는 **"같이 고쳐야 한다"는 표시** 가
> 양쪽 주석에 있어야 한다.

---

#### T-4. 공유기 목록에는 규칙이 있는데 적용이 안 되어 있었다

| | |
|---|---|
| **증상** | 포트포워딩을 넣었는데도 밖에서 TCP 만 안 붙음. **ping 은 됨** (공유기가 응답) |
| **왜 오진했나** | 관리 페이지 목록에 규칙이 **줄로 보였다.** 화면상 다 된 것처럼 보이니 공유기를 용의선상에서 뺐다 |
| **진짜 원인** | `포트포워드 활성` 체크박스가 꺼져 있으면 목록에 항목이 있어도 **통째로 무시된다.** 게다가 `추가` 만 누르고 `저장 후 적용` 을 안 누르면 반영되지 않는다 |
| **개선** | 13절에 **순서까지** 명시: 항목 추가 → 목록 확인 → `포트포워드 활성` 체크 → `저장 후 적용` |

> **일반화.** *"ping 은 되는데 특정 포트만 안 된다"* 는 **방화벽/포워딩 규칙** 을 가리키는
> 신호다. 회선이나 주소 문제였다면 ping 도 안 됐을 것이다. **어디까지 되는지가 원인을
> 좁혀준다.**

---

#### T-5. 클라이언트가 사설 IP 를 보고 있었다 — UPnP 가 안 풀어주는 부분

| | |
|---|---|
| **증상** | UPnP 성공 로그가 찍히고 포트도 열렸는데 다른 집 팀원은 로그인 실패 |
| **왜 오진했나** | *"포트를 열었으니 이제 붙을 수 있다"* 고 생각했다. **뚫는 쪽만 보고 찾아오는 쪽을 안 봤다** |
| **진짜 원인** | `DefaultGame.ini` 의 `ServerHost` 가 여전히 `192.168.0.32`. 사설 IP 는 **그 공유기 안에서만** 통한다 |
| **개선** | `ServerHost` 를 공인 IP 로 변경(2026-08-26). `run_server.bat` 이 켤 때마다 **실제 공인 IP 와 ini 값을 대조** 해 다르면 `[WARN]` |

> **일반화.** 이것이 15-2 의 3번 축이다. **NAT 통과는 "구멍 뚫기"와 "주소 알리기"의 두
> 반쪽으로 이루어져 있고, UPnP 는 앞의 반쪽만 담당한다.** 이 경계를 문서에 명시적으로
> 못 박지 않으면 계속 헷갈린다.

---

#### T-6. 방 호스트 주소에 게이트웨이 IP 가 찍혔다

| | |
|---|---|
| **증상** | 로그인·방 생성·참여·준비까지 전부 되는데 **게임 시작에서만** 무한 로딩. 참가자 화면의 주소가 `192.168.35.1` |
| **왜 오진했나** | **되는 게 너무 많아서** 네트워크를 의심하지 않았다. 로비 로직이나 여행 타이밍 버그로 봤다 |
| **진짜 원인** | 방의 호스트 주소는 서버가 `accept()` 에서 읽은 `Session::PeerAddress`. 호스트가 **서버와 같은 공유기 안** 에 있으면 이 값이 사설 IP 이고, 헤어핀을 타면 **게이트웨이 IP** 가 찍힌다 |
| **개선** | `--public-ip` 신설. 피어 주소가 사설이면 서버 자신의 공인 IP 로 치환. `run_server.bat` 이 자동으로 붙여준다 |

**여기서의 설계 판단이 이 작업에서 가장 신경 쓴 부분이다.**

피어 주소를 쓰던 원래 규칙은 *"클라이언트가 신고한 주소를 믿지 않는다"* 는 **옳은
원칙** 이었다. 신고를 믿으면 남의 주소를 적어 접속을 엉뚱한 곳으로 몰아줄 수 있다.
문제는 그 원칙이 **호스트와 서버가 같은 NAT 안에 있는 경우** 를 다루지 못한 것뿐이었다.

- **택하지 않은 안** — "클라이언트가 자기 공인 IP 를 신고하게 한다".
  가장 쉽지만 **원칙 자체를 버리는 것** 이라 채택하지 않았다.
- **택한 안** — 서버가 **자기가 아는 두 값 중에서만** 고르게 한다.
  ① `accept()` 의 피어 주소(공인이면 그대로) ② 서버 자신의 공인 IP(피어가 사설일 때).
  클라이언트가 보낸 값은 여전히 쓰지 않으므로 **신뢰 경계가 유지된다.**

사설 주소를 `--public-ip` 로 주면 상황만 나빠지므로 **서버가 시작을 거부한다.**
조용히 무시하면 T-1 과 같은 함정이 다시 생기기 때문이다.

> **일반화.** 보안 원칙이 특정 상황을 못 다룰 때, **원칙을 버리는 것과 예외를 메우는 것은
> 다르다.** 예외는 **신뢰 경계 안쪽의 값으로만** 메울 수 있으면 원칙이 유지된다.

---

#### T-7. 서버를 켠 PC 본인만 접속에 실패했다

| | |
|---|---|
| **증상** | 팀원은 붙는데 **서버 PC 에서만** 로그인 실패 |
| **왜 오진했나** | 자기 PC 가 자기 서버에 못 붙는 상황을 상상하지 못했다. 방화벽부터 뒤졌다 |
| **진짜 원인** | **NAT 헤어핀** — 자기 공인 IP 로 나갔다가 자기한테 되돌아오는 접속. 지원하지 않는 공유기가 흔하다 |
| **개선** | 공유 설정(`DefaultGame.ini`)을 고치지 말고 **그 PC 에서만** `MOU.Chat.SetServer 127.0.0.1 9000`. `Saved/Config/` 에만 저장돼 git 에 안 올라간다 |

> **일반화.** 팀 공유 설정과 개인 환경 예외는 **저장 위치가 분리되어 있어야 한다.**
> 분리되어 있지 않으면 "내 PC 에서 되게 하려고 고친 것" 이 커밋되어 **팀 전원을 막는다.**

---

#### T-8. CGNAT — 포트포워딩으로 풀 수 없는 경우

| | |
|---|---|
| **증상** | 포트포워딩을 제대로 넣었는데도 밖에서 못 붙음 |
| **진짜 원인** | 회선이 공인 IP 를 주지 않는 구성(CGNAT). 공유기 위에 통신사 NAT 이 한 겹 더 있다 |
| **판별** | `tracert -d -h 5 8.8.8.8` → 홉 1 은 공유기. **홉 2 가 공인 IP 면** 단일 NAT(포트포워딩으로 해결), `100.64.x.x` / `10.x.x.x` 면 이중 NAT |
| **개선** | UPnP 실패 사유를 `NoGatewayFound` / `CarrierGradeNat` 로 **구분해서** 보고. 전자는 수동 포워딩으로 풀리고 후자는 안 풀린다 — **대응이 다르므로 구분이 필요하다** |

> **일반화.** 같은 "실패" 라도 **해결 가능한 실패와 불가능한 실패는 반드시 구분해서
> 보고** 해야 한다. 뭉뚱그리면 사용자가 해결 불가능한 것에 시간을 쓴다.

---

### 15-4. 정리 — 이 작업에서 얻은 원칙 4가지

이 네 가지가 위 여덟 개 시행착오를 관통한다.

1. **실패는 소리를 내야 한다.**
   조용한 실패(T-1, T-2)는 증상이 원인에서 멀리 떨어진 곳에 나타나서 디버깅 비용이
   폭증한다. 빌드 스크립트·인자 파서·NAT 결과 전부에 명시적 실패 경로를 넣었다.

2. **자동화의 경계를 문서에 못 박는다.**
   UPnP 는 *"NAT 을 해결한다"* 가 아니라 *"세 축 중 1·2번만 자동화한다"* 이다(T-5).
   경계를 적어두지 않으면 팀 전체가 같은 오해를 반복한다.

3. **원칙을 버리지 말고 예외를 신뢰 경계 안에서 메운다.** (T-6)

4. **네트워크 문제는 레이어별 이분 탐색으로 자른다.**
   `127.0.0.1` → LAN IP → 공인 IP 순으로 붙여보면 **어디서 끊기는지가 곧 원인** 이다.
   이 절차를 13절에 `Test-NetConnection` 한 줄로 박아뒀다.

```bash
powershell -Command "Test-NetConnection 127.0.0.1 -Port 9000"
```

| 어디서 끊기나 | 원인 |
|---|---|
| 127.0.0.1 실패 | 서버가 안 떠 있다 |
| LAN 실패 | 방화벽 (`Server.exe` 인바운드) |
| 공인 IP 만 실패 | **포트포워딩 없음** — 팀원도 못 붙는다 |

### 15-5. 지금 상태 — 수동 포트포워딩에 의존하고 있다

| 축 | 현재 방식 | 자동화 여부 |
|---|---|---|
| TCP 9000 (로그인·로비) | `--upnp` 시도 → 실패 시 **공유기에 수동 입력** | 부분 |
| UDP 7777 (게임) | `NatPortMappingSubsystem` 시도 → 실패 시 **수동** | 부분 |
| 서버 주소 인지 | `DefaultGame.ini` 에 공인 IP **하드코딩 후 커밋** | 수동 |
| 방 호스트 주소 | `--public-ip` (`run_server.bat` 이 자동 조회) | 자동 |

**왜 아직 수동인가.** 테스트 공유기가 UPnP 를 꺼둔 모델이라 결국 관리 페이지에서 직접
넣었다. 그래서 **지금 실제로 동작하는 구성은 "수동 포워딩 + 공인 IP 하드코딩"** 이다.

이 방식의 실제 비용은 다음과 같다 — 전부 실제로 겪은 것들이다.

- 공인 IP 가 바뀌면(공유기 재부팅, ISP 재할당) **ini 를 고쳐 다시 커밋** 해야 하고,
  그전까지 **팀 전원이 접속 불가** 다.
- 서버를 켜는 사람이 바뀌면 그 사람 공유기에 포워딩을 새로 넣어야 한다.
- 방장이 서버 PC 가 아니면 **방장 공유기에도** `UDP 7777` 을 넣어야 한다.
  방장이 바뀔 때마다 반복된다.
- 어느 하나가 빠져도 증상이 전부 *"안 들어와져요"* 라서 원인 분리에 시간이 든다.

### 15-6. 목표 — 수동 포트포워딩이 필요 없는 서버 프레임워크

**목표를 한 문장으로:**

> `run_server.bat` 하나만 실행하면, **공유기 관리 페이지를 열지 않고도** 다른 네트워크의
> 팀원이 접속할 수 있어야 한다. 안 되는 경우에는 **왜 안 되는지와 무엇을 해야 하는지** 를
> 사람이 읽을 수 있는 말로 알려준다.

15-2 의 세 축을 전부 자동화하되, **불가능한 경우(CGNAT)를 자동으로 판별해 다른 경로로
넘기는 것** 까지가 범위다. "모든 환경에서 무조건 된다" 는 목표가 아니다 — 그건 릴레이
없이는 불가능하고, **불가능한 것을 목표로 잡으면 실패 처리를 대충 하게 된다.**

#### 단계별 계획

| 단계 | 내용 | 푸는 축 | 상태 |
|---|---|---|---|
| **F-1** | 포트 매핑 코드를 **공용 모듈 한 벌** 로 통합 | 1·2 | 계획 |
| **F-2** | UPnP 실패 시 **NAT-PMP / PCP 폴백** | 1·2 | 계획 |
| **F-3** | 매핑 **리스 갱신** (자동 만료 대응) | 1·2 | 계획 |
| **F-4** | 서버가 **자기 공인 주소를 스스로 확인** → ini 하드코딩 제거 | 3 | 계획 |
| **F-5** | **도달성 자가진단** — 실제로 밖에서 붙어보고 결과를 알림 | 1·2·3 | 계획 |
| **F-6** | CGNAT **자동 판별 → 릴레이(EOS) 전환** | 전부 | 계획 (14-6절) |

---

**F-1. 포트 매핑 코드를 공용 모듈 한 벌로 통합**

지금 `NatPortMapping.cpp` 가 **서버(순수 C++)와 언리얼(FSocket) 두 벌** 로 존재한다.
T-3 에서 확인했듯 **한쪽만 고치면 반쪽만 고쳐진다.** 프레임워크로 만들려면 이 중복을
먼저 없애야 한다 — 아래 F-2~F-5 를 두 번씩 구현할 수는 없다.

> 이중화 자체는 의도된 것이었다(서버는 엔진에 의존하지 않는다는 원칙). 그래서 통합
> 방향은 "언리얼 쪽에 맞춘다" 가 아니라 **소켓 계층만 얇게 분리한 순수 C++ 코어 + 각
> 환경의 어댑터** 다. 프레이밍 코드 이중화(`CodeRefactoring.md` R-6)와 같은 구조를 쓴다.

**F-2. UPnP 실패 시 NAT-PMP / PCP 폴백**

UPnP 미지원 공유기가 실제로 우리 테스트 환경이었다(T-3, T-4). 그런데 UPnP 를 끄고도
**NAT-PMP(Apple 계열) 나 PCP(RFC 6887)** 는 켜져 있는 모델이 있다. 셋을 순서대로
시도하면 자동화 성공률이 올라간다.

셋 다 실패한 뒤에야 "수동으로 넣어달라" 고 안내한다 — **지금은 UPnP 하나만 실패해도
곧바로 수동으로 떨어진다.**

**F-3. 매핑 리스 갱신**

UPnP 매핑에는 **만료 시간(lease)** 이 있다. 지금은 시작할 때 한 번 열고 끝이라,
장시간 세션에서 매핑이 조용히 사라지면 **T-1 과 같은 종류의 조용한 실패** 가 된다 —
잘 되던 서버에 갑자기 아무도 못 들어오는데 로그에는 아무것도 안 남는다.

주기적으로 갱신하고, 갱신 실패를 **로그와 UI 양쪽에** 남긴다.

**F-4. 서버가 자기 공인 주소를 스스로 확인 (3번 축의 핵심)**

**`DefaultGame.ini` 에 공인 IP 를 하드코딩하고 커밋하는 지금 방식이 이 구조의 가장 약한
고리다.** IP 가 바뀌는 순간 팀 전원이 막히고, 그걸 알아채는 방법이 "안 된다는 연락" 뿐이다.

바꿀 방향은 **주소를 고정값이 아니라 조회 대상으로 만드는 것** 이다.

- 서버는 시작 시 자기 공인 주소를 확인한다 — UPnP 의 `GetExternalIPAddress`, 없으면
  STUN(RFC 5389). `run_server.bat` 이 지금 하는 외부 조회를 **서버 본체로 옮긴다.**
- 클라이언트는 ini 의 고정 IP 대신 **가벼운 디스커버리 지점** (고정 주소를 가진 아주 작은
  엔드포인트)에서 현재 서버 주소를 받아온다.
- ini 값은 **폴백으로만** 남긴다. 디스커버리가 죽어도 지금 방식으로 되돌아간다.

> 여기까지 오면 `--public-ip`(T-6)도 인자가 아니라 **자동으로 채워지는 값** 이 된다.
> 지금 `run_server.bat` 이 대신 해주고 있는 일을 서버가 직접 하는 것이다.

**F-5. 도달성 자가진단 — "열렸다" 가 아니라 "실제로 붙는다" 를 확인**

T-4 가 정확히 이 문제였다. 공유기는 규칙을 **받아들였다고 말했지만** 실제로는 적용되지
않았다. **매핑 API 의 성공 응답은 도달 가능성을 보장하지 않는다.**

그래서 서버가 켜진 뒤 **자기 공인 주소로 실제 접속을 한 번 시도** 하고, 그 결과를 15-4 의
이분 탐색 표 형태로 콘솔에 찍는다.

```text
[진단] 127.0.0.1:9000      OK
[진단] 192.168.0.32:9000   OK
[진단] 211.244.x.x:9000    실패
       -> 포트포워딩이 적용되지 않았다.
          공유기에서 "포트포워드 활성" 체크와 "저장 후 적용" 을 확인할 것.
```

헤어핀 미지원(T-7) 때문에 서버 PC 에서의 자가 테스트만으로는 단정할 수 없다.
그래서 **외부 에코 지점** 을 하나 두고 그쪽에서 역방향으로 두드리게 하는 것이 정확하다 —
F-4 의 디스커버리 지점이 이 역할을 겸할 수 있다.

**F-6. CGNAT 자동 판별 → 릴레이 전환**

F-2~F-5 를 다 해도 **CGNAT 회선은 원리적으로 안 된다** (T-8). 여기서 할 일은 "되게 만드는
것" 이 아니라 **빨리 정확하게 포기하고 다른 경로를 제시하는 것** 이다.

- UPnP 외부 IP 와 STUN 이 보고한 IP 가 **다르면** 이중 NAT 이다 — 자동 판별 가능
- 그 경우 `FEOSLobbyBackend`(14-6절)로 전환을 권고한다

교체 지점은 이미 `ILobbyBackend` 로 분리되어 있어 **게임 로직은 손대지 않는다.**

#### 이 프레임워크가 갖춰야 할 성질

위 시행착오에서 그대로 도출된 것들이다. 기능보다 이쪽이 중요하다.

| 성질 | 근거 |
|---|---|
| **모든 실패가 사유와 함께 로그에 남는다** | T-1, T-2, F-3 — 조용한 실패가 가장 비싸다 |
| **실패를 "해결 가능/불가능" 으로 구분해 보고한다** | T-8 — 대응이 다르므로 |
| **자동화가 안 될 때 사람이 할 일을 정확히 알려준다** | T-4 — "포트포워딩 하세요" 가 아니라 "활성 체크와 저장 후 적용을 확인하세요" |
| **성공 응답을 믿지 않고 실제 도달성을 확인한다** | T-4, F-5 |
| **팀 공유 설정과 개인 예외의 저장 위치가 분리된다** | T-7 |
| **코드가 한 벌이다** | T-3 — 두 벌이면 반쪽만 고쳐진다 |
