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

---

## 1. 지금까지 뭘 만들었나

게임 본체는 **리슨서버** 방식이지만, 채팅·계정·로비(방 목록)는 **별도 프로세스(데디케이트)** 로 분리했다.
호스트가 게임을 나가도 이 서버는 살아있으므로 대화가 끊기지 않고, 계정도 유지되고, 방 목록도 관리된다.

**중요**: 로비 서버는 방 "주소록" 역할만 한다. 실제 게임 트래픽(이동/전투/GAS)은 이 서버를
전혀 거치지 않고 참가자가 호스트의 리슨서버에 직접 붙는다. 그래서 방이 몇 개 열려도
서버 부하가 거의 늘지 않는다.

| 단계 | 내용 | 상태 |
|---|---|---|
| 1 | 프로토콜 정의 (`ChatProtocol.h`) | 완료 (v6) |
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
| 8 | 리슨서버 → 채팅서버 신원 미러링 | **미착수** |
| 9 | 귓속말 | **미착수** |

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
│   │   └─ Rooms.h/.cpp         방 레지스트리 (메모리만, 휘발성)
│   └─ TestClient/
│       └─ TestClient.cpp       검증용 콘솔 클라이언트 (계정/로비 명령 포함)
└─ TeamProject_MOU/             언리얼 프로젝트
    └─ Source/TeamProject_MOU/
        ├─ TeamProject_MOU.Build.cs
        ├─ Public/Chat/         채팅+계정+로비 헤더
        └─ Private/Chat/        채팅+계정+로비 구현
```

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

엔진과 무관한 순수 C++ 콘솔 프로그램이다. **CMake 로만 빌드한다** (SQLite 앰알가메이션이
C 파일이라 `project()` 에 `C` 언어가 켜져 있어야 한다).

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
Server.exe 9000
```

DB 경로를 지정하고 싶으면 두 번째 인자로: `Server.exe 9000 chat_log.db`
(계정과 채팅 로그가 같은 파일에 테이블만 나눠서 들어간다.)

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
| 에디터 출력 로그 | 필터 `LogMOUChat` 로 같은 내용 |

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
      ├─ UChatSubsystem 이 리슨서버 넷드라이버가 뜬 것을 감지
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
> 이제 방장 쪽 `UChatSubsystem` 이 리슨서버가 실제로 뜬 것을 확인한 뒤에 신호를 보낸다.
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
(`UChatSubsystem::PollListenServer`). 콘솔에는 감지할 리슨서버가 없으므로 손으로 친다.

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
UnrealEditor-Cmd.exe "<경로>\TeamProject_MOU.uproject" -game -nullrhi -unattended -nosplash -nosound -ExecCmds="MOU.Chat.Connect 127.0.0.1 9000,MOU.Chat.Register id123 pw123456 닉네임,MOU.Chat.Login id123 pw123456 0,MOU.Exec.Delayed 4 MOU.Room.List" -LogCmds="LogMOUChat Verbose" -log -abslog="<경로>\ue_test.log"
```

로그에서 `LogMOUChat` 을 필터링해 확인한다. 한계: `-nullrhi` 라 **화면에 어떻게 보이는지는 확인 못 한다.**
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
  │ FChatClientRunnable   (워커 스레드)              │  소켓/바이트만 다룸
  │        ↕ TQueue (SPSC)                          │  UObject 접근 절대 금지
  │ FSocketLobbyBackend   (게임 스레드)              │  패킷 조립 ─┐
  │        │  implements ILobbyBackend              │            ├ 교체 지점
  │        │  (FEOSLobbyBackend 로 갈아끼울 수 있다) │            ─┘
  │ UChatSubsystem        (게임 스레드)              │  상태 보관 / 델리게이트
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

### 핵심 원칙 4가지

**1. 채팅·계정·로비는 UE 리플리케이션을 쓰지 않는다.**
Server RPC / Multicast 를 타지 않고 `Server.exe` 로 가는 별도 TCP 소켓으로 간다.
그래서 호스트가 게임을 나가도 채팅이 유지되고, 로비 서버 하나로 모든 방을 관리할 수 있다.

**2. 워커 스레드에서 UObject 를 절대 건드리지 않는다.**
언리얼의 UObject 는 스레드 세이프하지 않다. 수신 데이터는 순수 데이터 구조체로 바꿔
`TQueue` 에 넣기만 하고, 게임 스레드의 `UChatSubsystem::Tick` 이 꺼내서 그때 델리게이트를 쏜다.
이 규칙을 어기면 재현이 어려운 랜덤 크래시가 난다.

**3. 신원은 서버가 확정한다.**
`ChatBroadcast` 의 `SenderUserId`/`SenderName`, 방의 호스트 IP 모두 항상 서버가
세션 정보(TCP 연결 자체 또는 계정 DB)로 채운다. 클라이언트가 보낸 값을 그대로 옮기지 않는다.
이게 없으면 사망자 채널도, 방 호스트 주소 위조 방지도 성립하지 않는다.

**4. 로비 서버는 주소록일 뿐, 게임 트래픽은 지나가지 않는다.**
방 생성/조회/참여는 메타데이터 교환이다. 참여가 승인되면 클라이언트는 호스트에게
**직접** `ClientTravel` 한다. 그래서 로비 서버 부하는 방 개수와 무관하게 낮다.
대신 이 구조는 **같은 네트워크(NAT 없음)에서만** 동작한다 — 자세한 내용은 11절 참고.

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
| **NAT 통과** | **안 됨 (포트포워딩 필요)** | **됨 (P2P 릴레이)** |
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
| **안 바뀜** | `UChatSubsystem` 의 블루프린트 API, UMG 위젯 5종, 게임 로직 전부 |
| **바뀜** | `FEOSLobbyBackend` 의 내용, 그리고 그것을 고르는 설정값 한 줄 |

현실적인 최종 구성은 **"EOS = 계정·세션, 자체 서버 = 채팅·게임 데이터"** 다.
EOS Connect 의 `ProductUserId` 를 `accounts` 테이블의 외부 키로 저장하면 둘이 이어진다.
붙이는 순서는 `Chat/EOSLobbyBackend.h` 주석에 단계별로 적어뒀다.

### 스레드 경계 상세

```
     게임 스레드                            워커 스레드
   ┌─────────────────┐                 ┌──────────────────────────┐
   │ UChatSubsystem  │                 │  FChatClientRunnable     │
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
| `TestClient/TestClient.cpp` | 검증용 콘솔 클라이언트. 계정/로비 명령 포함 |

### 언리얼 (`TeamProject_MOU/Source/TeamProject_MOU/`)

| 파일 | 역할 |
|---|---|
| `Public/Chat/ChatTypes.h` | BP 노출 타입: `FChatMessage`, `FChatLoginResult`, `EChatChannelBP`, `EChatLoginResultBP`, `EChatConnectionState`, `LogMOUChat` |
| `Public/Chat/LobbyTypes.h` | BP 노출 로비 타입: `FMOURoomInfo`, `FMOURoomJoinResult`, `EMOURoomResultBP`, `EMOURoomStateBP`, `EMOULobbyBackendType` |
| `Public/Chat/LobbyBackend.h`<br>`Private/Chat/LobbyBackend.cpp` | **`ILobbyBackend` 인터페이스 + 팩토리.** 계정/세션 탐색의 교체 지점. `FChatClientEvent`(백엔드 → 게임 스레드 사건)도 여기 있다 |
| `Public/Chat/SocketLobbyBackend.h`<br>`Private/Chat/SocketLobbyBackend.cpp` | 자체 서버 백엔드. **패킷 조립은 여기서만 한다.** 워커 스레드 수명도 여기가 소유 |
| `Public/Chat/EOSLobbyBackend.h`<br>`Private/Chat/EOSLobbyBackend.cpp` | EOS 백엔드 **뼈대.** 각 함수가 어떤 EOS API 로 바뀌는지, 붙이는 순서가 주석에 있다 |
| `Public/Chat/ServerSettings.h`<br>`Private/Chat/ServerSettings.cpp` | `UDeveloperSettings`. 서버 주소 / 백엔드 종류 / 호스트 준비 대기 상한. 우선순위: 실행 인자 > 개인 ini > 팀 공유 ini |
| `Public/Chat/ChatFraming.h`<br>`Private/Chat/ChatFraming.cpp` | 프레이밍의 `TArray` 버전 + UTF-8 변환. 서버 `Framing.cpp` 와 로직 동일. BP enum ↔ 서버 enum `static_assert` 전부 여기 모여있다 |
| `Public/Chat/ChatClientRunnable.h`<br>`Private/Chat/ChatClientRunnable.cpp` | `FRunnable` 워커. 접속·재접속·송수신·패킷 파싱. **소유자는 `FSocketLobbyBackend`** |
| `Public/Chat/ChatSubsystem.h`<br>`Private/Chat/ChatSubsystem.cpp` | `UGameInstanceSubsystem`. **진입점. UI/게임플레이는 이것만 쓴다.** 백엔드를 고르고, 방장의 리슨서버가 뜨는지 감시한다. 패킷은 조립하지 않는다 |
| `Public/Chat/ChatWidgetBase.h`<br>`Private/Chat/ChatWidgetBase.cpp` | `UUserWidget`. 채팅 로그 + 입력창. PIE 다중 창 지원(`TMap<World, Widget>`) |
| `Public/Chat/LoginWidgetBase.h`<br>`Private/Chat/LoginWidgetBase.cpp` | `UUserWidget`. 아이디/비밀번호 로그인 + 계정 생성. 성공 시 채팅 위젯 + 로비 자동 생성 |
| `Public/Chat/LobbyWidgetBase.h`<br>`Private/Chat/LobbyWidgetBase.cpp` | `UUserWidget`. 메인메뉴 + 대기실. 같은 버튼 3개가 상태에 따라 라벨/동작을 바꾼다 |
| `Public/Chat/RoomCreateWidgetBase.h`<br>`Private/Chat/RoomCreateWidgetBase.cpp` | `UUserWidget`. 제목 + 비번 4자리로 방 생성. 리슨서버는 열지 않는다 |
| `Public/Chat/RoomListWidgetBase.h`<br>`Private/Chat/RoomListWidgetBase.cpp` | `UUserWidget`. 방 목록 표시 + 참여. 줄 하나는 `URoomListEntryWidget`(같은 헤더) |

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

### `UChatSubsystem` — 진입점

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

**서버 실행 인자가 바뀌었다.** `Server.exe <port>` → `Server.exe <port> [db경로]`
(두 번째 인자는 선택이라 기존 실행 스크립트는 그대로 동작한다).

### 바뀌지 않은 것 (확인용)

- `TeamProject_MOU.uproject` — 플러그인 추가 없음. `Sockets`/`Networking`/`SlateCore` 는 엔진 기본 모듈
- `Config/*.ini` — 건드리지 않았다
- 기존 게임플레이 코드(`Base/`, `TeamProject_MOUCharacter` 등) — 전혀 건드리지 않았다
- 콘텐츠(`.uasset`) — 추가/수정 **0개**. 에셋 충돌 걱정 없음

### 추가된 파일 (전부 신규, 충돌 없음)

```
서버:
  MOU_Server/Server/Accounts.h  Accounts.cpp  Crypto.h  Crypto.cpp
  MOU_Server/Server/ChatLog.h   ChatLog.cpp
  MOU_Server/Server/Rooms.h     Rooms.cpp
  MOU_Server/ThirdParty/sqlite/ sqlite3.h  sqlite3.c  README.md

언리얼:
  Source/TeamProject_MOU/Public/Chat/   ChatTypes.h  LobbyTypes.h  ChatFraming.h
                                         ChatClientRunnable.h  ChatSubsystem.h
                                         ChatWidgetBase.h  LoginWidgetBase.h
                                         LobbyWidgetBase.h  RoomCreateWidgetBase.h
                                         RoomListWidgetBase.h
  Source/TeamProject_MOU/Private/Chat/  ChatFraming.cpp  ChatClientRunnable.cpp
                                         ChatSubsystem.cpp  ChatWidgetBase.cpp
                                         LoginWidgetBase.cpp  LobbyWidgetBase.cpp
                                         RoomCreateWidgetBase.cpp  RoomListWidgetBase.cpp
```

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

`Chat/` 폴더의 모든 `.h`/`.cpp` 는 UTF-8 **BOM 포함**으로 저장돼 있다.
주석에 한글이 있어서 BOM 이 없으면 MSVC 가 CP949 로 오해해 깨진다.

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
| `FChatClientRunnable` (워커) | 소켓, 바이트 배열, 순수 구조체, `TQueue` 넣기 |
| `FChatClientRunnable` (워커) | ❌ UObject, UMG, 델리게이트 브로드캐스트, `UE_LOG` 외 엔진 API |
| `UChatSubsystem::Tick` (게임) | 큐에서 꺼내기, 델리게이트 브로드캐스트 |
| `UChatWidgetBase`/`ULoginWidgetBase` (게임) | 위젯 조작 |
| 서버 `ChatLog` 라이터 스레드 | `sqlite3*` 를 여기서만 건드린다. 다른 스레드가 같은 커넥션을 만지면 전제가 깨진다 |
| 서버 `Accounts` | 여러 클라이언트 스레드가 동시에 부를 수 있어 자체 뮤텍스로 직렬화한다 |

### 종료 순서 (지키지 않으면 크래시)

**클라이언트** — 워커 스레드 정리는 **반드시** 이 순서다 (`UChatSubsystem::ShutdownClient`):

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
- **NAT 를 해결해주지 않는다.** 이 구조는 로비(목록) 문제만 풀지 **접속(라우팅)** 문제는 못 푼다.
  참가자가 호스트의 `IP:Port` 에 직접 닿아야 하므로, **같은 네트워크(같은 공유기/교내망)에서만
  동작이 보장된다.** 각자 집에서 인터넷으로 붙는 시연이라면 포트포워딩이 필요하거나
  Steam/EOS 같은 릴레이 서비스로 가야 한다. (이번 프로젝트는 같은 네트워크 시연으로 확정했다.)
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
  방장 쪽 `UChatSubsystem` 이 리슨서버 넷드라이버가 뜬 것을 확인하고 `RoomHostReadyReq` 를
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
- 전송 지연이 최대 50ms 붙는다 (`FChatClientRunnable::WaitMilliseconds`).
  워커가 `Wait()` 에서 깨어나야 큐를 비우기 때문. 채팅에는 무해하다.
- **콘솔 명령이 Shipping 빌드에도 남는다.** `MOU.Chat.Dead` 같은 치트성 명령에
  `ECVF_Cheat` 가 없다. `MOU.Chat.ShowLogin` 등 일부는 `#if !UE_BUILD_SHIPPING` 으로 뺐지만
  전부는 아니다. 8단계에서 정리할 예정.
- **신원 위조 가능 (팀 ID / 생사).** 로그인 자체는 계정으로 막혔지만, `TeamId` 는 클라이언트가
  자유 입력이고 `SetDeadForTest()` 도 검증이 없다. **산 사람이 사망 채널을 엿볼 수 있다.**
  8단계에서 리슨서버가 신원을 미러링하도록 바꿀 것이므로, **게임플레이 코드에서
  `SetDeadForTest` 호출을 늘리지 말 것.**

### 서버 (`MOU_Server/README.md` 에서 그대로)

- **`send()` 를 세션 락 안에서 호출한다.** 느린 클라이언트가 있으면 락을 오래 잡아 전체 채팅이 지연된다.
  인원이 늘면 세션별 송신 큐 + 전용 송신 스레드로 바꿔야 한다.
- **스레드를 detach 한다.** 서버 종료 시 클라이언트 스레드를 정리하지 않는다(소켓은 닫힘).
- 접속자 수 상한은 없다 (고정 배열이 아님).

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
5. 에디터 출력 로그에서 `LogMOUChat` 필터로 경고 확인

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

### 14-5. 인정하는 한계 — NAT

**자체 구현이 풀지 못하는 문제는 방 목록이 아니라 NAT다. 이건 명확히 인정한다.**

```
현재 구조:
  참가자 ──────────────────────▶ 호스트 공인IP:7777
            공유기가 이 접속을 막는다 (포트포워딩이 없으면)
```

`ClientSession::PeerAddress`는 서버가 `accept()` 시점에 읽은 상대 IP다. 클라이언트가
신고한 값이 아니라 위조는 안 되지만, **그 IP의 7777 포트가 외부에 열려 있어야** 참가자가
붙을 수 있다. 호스트가 포트포워딩을 하지 않으면 실패한다.

| 시연 환경 | 동작 여부 |
|---|---|
| 같은 공유기 / 교내망 (LAN) | ✅ 동작 — **이번 프로젝트는 이 환경으로 확정** |
| 각자 집에서 인터넷으로 | ❌ 호스트의 포트포워딩 필요 |

**자체 로비 서버로는 이걸 고칠 수 없다.** NAT 홀펀칭이나 릴레이 서버가 필요하고,
그게 정확히 EOS P2P가 제공하는 가치다. **그래서 최종 단계에서 EOS를 붙일 계획이고,
그때 교체되는 것은 세션 탐색 계층뿐이다.**

### 14-6. 앞으로의 개발 계획

**전환 비용을 미리 확정해뒀다.** `ILobbyBackend` 인터페이스(v6)를 도입해서, EOS로 가는 것이
프로젝트 전체를 뒤집는 일이 아니라 **파일 하나를 채우는 일**이 되도록 만들었다.

```
UChatSubsystem            BP API / 상태 / 델리게이트   ← 백엔드를 모른다
  └─ ILobbyBackend                                    ← 교체 지점
       ├─ FSocketLobbyBackend   자체 서버 (현재 기본값)
       └─ FEOSLobbyBackend      EOS      (뼈대 + 전환 주석)
```

| | |
|---|---|
| **안 바뀜** | `UChatSubsystem`의 블루프린트 API, UMG 위젯 5종, 게임 로직 전부 |
| **바뀜** | `FEOSLobbyBackend`의 내용, 그리고 그것을 고르는 설정값 한 줄 |

전환은 `Project Settings → Game → MOU Server → Lobby Backend`에서 고르거나
실행 인자 `-MOULobbyBackend=EOS`로 한다. 자세한 내용은 [5절 백엔드 교체](#5-구조) 참고.

**단계별 계획**

| 단계 | 내용 | 상태 |
|---|---|---|
| 1 | 자체 서버로 로비·계정·대기실 완성 | ✅ 완료 |
| 2 | `ILobbyBackend`로 교체 지점 분리 | ✅ 완료 (v6) |
| 3 | 호스트 준비 신호로 여행 경쟁 상태 제거 | ✅ 완료 (v6) |
| 4 | `PreLogin`에서 방 비밀번호 재검사 (보안 관문) | ⬜ 다음 작업 |
| 5 | LAN 환경 3~4인 실기기 시연 | ⬜ |
| 6 | `FEOSLobbyBackend` 구현 — NAT 해결 | ⬜ 여력이 되면 |

**EOS를 붙일 때의 최종 구성**은 전면 교체가 아니라 **하이브리드**다.

```
EOS Connect  ──▶ ProductUserId ──▶ accounts 테이블의 외부 키로 저장
EOS Session  ──▶ 방 목록 / NAT 통과
자체 서버    ──▶ 채팅 (사망자 채널) / 채팅 로그 / 커스터마이징 데이터
```

상용 게임들이 실제로 쓰는 구조다. 붙이는 순서(Dev Portal 등록 → 플러그인 3종 →
`DefaultEngine.ini` → `Build.cs` → 각 함수 구현)는 `Chat/EOSLobbyBackend.h` 주석에
단계별로 적어뒀다.

### 14-7. 예상 질문

**Q. 그래도 서버를 하나 띄우는 건 맞지 않나?**
맞다. 다만 그 서버는 게임 트래픽을 처리하지 않으므로 **사양과 비용이 게임 서버와 다르다.**
방 100개가 열려도 메모리에 구조체 100개가 늘 뿐이라, 무료 티어 VM 한 대로 충분하다.
EOS를 쓰더라도 채팅·로그·커스터마이징 때문에 이 프로세스는 어차피 남는다.

**Q. 그럼 EOS를 지금 붙이면 되지 않나?**
NAT가 문제가 되는 시점, 즉 **각자 집에서 붙는 시연을 할 때** 붙이는 것이 맞다고 본다.
이번 프로젝트는 LAN 시연으로 확정됐고, EOS를 지금 붙이면 팀 전원이 Dev Portal 설정을
공유해야 해서 개발 속도만 떨어진다. 전환 경로는 이미 코드에 있으므로 필요해질 때 붙인다.

**Q. 왜 데디케이트 서버가 필요하다고 보고하지 않았나?**
필요하지 않기 때문이다. 우리 구조에서 게임 시뮬레이션은 전부 호스트의 리슨서버가 한다.
데디케이트 서버를 띄우면 인프라 비용만 늘고 얻는 것이 없다.
