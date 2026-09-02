# MOU 코드 리팩토링 기록

작성: 2026-08-26
대상: 오늘 작업한 **서버/NAT** + 이전에 작업한 **보이스·무전기** 시스템

이 문서는 "무엇을 발견했고, 왜 그게 문제이고, 어떻게 바꿨는가" 를 남긴다.
`SERVER_INTEGRATION.md` 가 **시스템이 어떻게 동작하는가**를 다룬다면,
이 문서는 **왜 지금 모양이 되었는가**를 다룬다.

> 항목 번호는 고정이다. 나중에 항목이 추가돼도 기존 번호는 바꾸지 않는다.
> 코드 주석에서 `CodeRefactoring.md 의 R-2` 처럼 참조하기 때문이다.

---

## 목차

- [A. 적용 완료](#a-적용-완료)
  - [P-1. UPnP 탐색 9초 → 3초](#p-1-upnp-탐색-9초--3초)
  - [P-2. 참여자 레벨 이동 — 직렬을 병렬로](#p-2-참여자-레벨-이동--직렬을-병렬로)
  - [R-1. 콘솔 명령을 별도 파일로 분리](#r-1-콘솔-명령을-별도-파일로-분리)
  - [R-3. 방 호스트 주소 치환](#r-3-방-호스트-주소-치환)
  - [B-1. 빌드 스크립트가 조용히 실패하던 문제](#b-1-빌드-스크립트가-조용히-실패하던-문제)
  - [B-2. .gitignore — 산출물은 막고 스크립트는 지킨다](#b-2-gitignore--산출물은-막고-스크립트는-지킨다)
- [B. 발견했으나 아직 안 고친 것](#b-발견했으나-아직-안-고친-것)
  - [R-2. ServerSubsystem 도 같은 비대화](#r-2-serversubsystem-도-같은-비대화)
  - [R-4. 두 상태 위젯의 골격 중복](#r-4-두-상태-위젯의-골격-중복)
  - [R-5. VOICE_INTEGRATION.md 7-4절이 폐기된 설계를 담고 있다](#r-5-voice_integrationmd-7-4절이-폐기된-설계를-담고-있다)
  - [R-6. 프레이밍 코드 이중화](#r-6-프레이밍-코드-이중화)
- [C. 리팩토링 원칙](#c-리팩토링-원칙)

---

# A. 적용 완료

## P-1. UPnP 탐색 9초 → 3초

**증상.** PIE 에서 방 만들기 창을 열면 "공유기에 포트를 여는 중입니다..." 가
한참 떠 있었다. 방 제목을 다 입력하고 버튼을 눌러도 그 대기가 안 끝나 있으면
전송이 보류됐다 (`RoomCreateWidgetBase` 의 `bCreateWaitingForNat`).

**원인.** SSDP 탐색이 검색 대상 3개를 **순차로** 돌면서 각각 전체 타임아웃을
기다리고 있었다.

```cpp
// 예전 (Private/Server/Net/NatPortMapping.cpp)
for (const TCHAR* SearchTarget : SearchTargets)   // IGD:1, IGD:2, rootdevice
{
    Guard.Socket->SendTo(...);                     // 하나 쏘고
    const double Deadline = ... + TimeoutSeconds;  // 3초 꼬박 기다리고
    while (...) { ... }                            // 그다음 것을 쏜다
}
```

`DiscoverGateway(TimeoutSeconds = 3.0f)` 이므로 UPnP 를 끈 공유기에서는
**3 × 3초 = 9초**를 전부 쓴 뒤에야 `NoGatewayFound` 가 났다. 우리 테스트
공유기가 정확히 그 경우였다.

**왜 순차일 이유가 없었나.** SSDP 응답은 우리가 만든 **소켓 하나**로
비동기로 돌아온다. 요청 A 에 대한 응답이 오기 전에 요청 B 를 보내도
아무 문제가 없고, 규격도 이를 막지 않는다. 순차 구조는 "요청-응답을
한 쌍으로 처리한다" 는 습관이 만든 것이지 프로토콜의 요구가 아니었다.

**바꾼 것.** 세 요청을 **먼저 전부 쏘고**, 그다음 한 번만 기다린다.

```cpp
// 지금
for (const TCHAR* SearchTarget : SearchTargets)
{
    Guard.Socket->SendTo(...);        // 세 개를 연달아 쏘기만 한다
}

const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
while (FPlatformTime::Seconds() < Deadline)
{
    // 어느 요청에 대한 응답이든 먼저 온 것을 쓴다
}
```

`MX` 도 2 → 1 로 줄였다. "이 초 안에 아무 때나 답하라" 는 값인데, 어차피
우리는 첫 응답이 오는 즉시 반환하므로 크게 잡을 이유가 없다.

**효과.**

| | 예전 | 지금 |
|---|---|---|
| UPnP 없는 공유기 (최악) | 9초 | 3초 |
| UPnP 되는 공유기 | 첫 응답까지 (~100ms) | 동일 |
| 응답 폴링 간격 | 200ms | 100ms |

**같이 고친 곳.** `MOU_Server/Server/NatPortMapping.cpp` 에 **같은 로직이
복제되어 있어** 함께 고쳤다. 두 벌인 것은 의도된 것이지만
(→ [R-6](#r-6-프레이밍-코드-이중화)), 그래서 **한쪽만 고치면 반쪽만
빨라진다.** 서버 쪽은 이 9초가 `accept` 루프 진입을 그대로 지연시켰다.

**수정 파일**

- `TeamProject_MOU/Source/TeamProject_MOU/Private/Server/Net/NatPortMapping.cpp`
- `MOU_Server/Server/NatPortMapping.cpp`

**영향 검사.** `SendSsdpMSearch` 는 `DiscoverGateway` 에서만 불린다.
반환값 계약(`Success` / `NoGatewayFound` / `NetworkError`)은 그대로라
호출부는 손대지 않았다. 워커 스레드(`FNatMappingRunnable`) 구조도 무관하다.

---

## P-2. 참여자 레벨 이동 — 직렬을 병렬로

**증상.** 방장이 게임을 시작하면 참여자는 "방장이 서버를 여는 중입니다..."
를 한참 보다가, 그게 끝나면 이번엔 자기 맵 로딩을 처음부터 기다렸다.

**원인.** 두 박자 구조(`LobbyWidgetBase.h` 상단 설명) 자체는 옳다.
문제는 두 로딩이 **겹치지 않는다**는 것이었다.

```
[예전]
방장    ──── 맵 로드 ────▶ 리슨서버 뜸
                             │
참여자  ─── 대기(놀고 있음) ──┴──▶ 맵 로드 ────▶ 입장
        └────── 참여자가 실제로 기다리는 시간 = 두 로딩의 합 ──────┘
```

참여자는 1박자 내내 **아무 일도 하지 않고** 기다리기만 했다.

**바꾼 것.** 참여자가 1박자를 받는 순간 목적지 맵을 **비동기로 미리 올린다.**

```
[지금]
방장    ──── 맵 로드 ────▶ 리슨서버 뜸
참여자  ─── 맵 미리 로드 ──▶(완료)  └──▶ 입장 (즉시)
```

**왜 참여자가 맵 이름을 알 수 있나.** `HostMapName` 은 `ULobbyWidgetBase` 의
`UPROPERTY(EditAnywhere)` 라 WBP 에 박힌다. 방장과 참여자가 같은 위젯
블루프린트를 쓰므로 **같은 값을 이미 갖고 있다.** 그래서 프로토콜에 맵
이름을 추가하지 않고도 된다.

> 방마다 맵을 다르게 고르게 되면 이 전제가 깨진다. 그때는 `RoomInfo` 에
> 맵 이름을 실어야 하고, 이 최적화도 그 값을 써야 한다.

**★ 소유권이 이 최적화의 성패를 가른다.** 처음에는 로비 위젯이 패키지 참조를
들고 있게 만들었는데, 그러면 **효과가 없다.** `ClientTravel` 이 시작되면
월드가 헐리면서 위젯이 먼저 파괴되고, 참조가 끊긴 패키지를 GC 가 도로
가져가기 때문이다. 미리 올린 것이 정확히 필요한 순간에 사라진다.

그래서 소유권을 `UServerSubsystem` 으로 옮겼다. `UGameInstanceSubsystem`
이라 **레벨 이동을 그대로 넘어간다.** 위젯은 호출만 한다.

```cpp
// ULobbyWidgetBase::BeginPreloadHostMap
if (UServerSubsystem* Server = GetServerSubsystem())
{
    Server->BeginPreloadMap(HostMapName);   // 로딩과 참조 보관은 저쪽이 한다
}
```

`AddToRoot()` 를 쓰지 않은 이유는, 해제를 한 번이라도 빠뜨리면 맵이 통째로
메모리에 영원히 남기 때문이다. `UPROPERTY` 참조는 소유자가 사라지면 같이
풀린다.

**실패해도 안전하다.** 짧은 이름(`L_Game`)은 `FPackageName::SearchForPackageOnDisk`
로 경로를 찾고, 못 찾으면 그냥 포기한다. 로딩이 실패하거나 늦어도 여행은
그대로 진행된다 — 이 기능은 "빨라지면 좋고 아니면 말고" 다. 그래서 실패를
오류로 취급하지 않고 `Verbose` 로만 남긴다.

**끄는 법.** `bPreloadMapWhileWaiting = false` (WBP 또는 C++ 기본값).

**수정 파일**

- `Public/Server/Lobby/LobbyWidgetBase.h` — `bPreloadMapWhileWaiting`, `BeginPreloadHostMap()`
- `Private/Server/Lobby/LobbyWidgetBase.cpp` — `HandleGameStarted` 에서 참여자만 호출
- `Public/Server/ServerSubsystem.h` — `BeginPreloadMap()`, `ReleasePreloadedMap()`, `PreloadedMapPackage`
- `Private/Server/ServerSubsystem.cpp` — 구현 + `Deinitialize` 에서 해제

**영향 검사.**

- `HandleGameStarted` 는 방장에게도 불린다 → `if (!bIsHost)` 로 참여자만 태웠다.
  방장은 어차피 곧바로 `OpenLevel` 하므로 미리 올릴 이유가 없다.
- `TravelAsClient` / `MakeTravelURL` 은 건드리지 않았다. 여행 경로는 그대로다.
- `UServerSubsystem::Deinitialize` 에 해제를 넣어 참조가 남지 않게 했다.
- 새 `UPROPERTY(TObjectPtr<UPackage>)` 가 늘었으므로 UHT 재생성이 필요하다
  (빌드로 확인 완료).

---

## R-1. 콘솔 명령을 별도 파일로 분리

**발견.** `Private/Voice/VoiceSubsystem.cpp` 가 **2105 줄**이었는데,
그중 **1012 줄(48%)** 이 콘솔 명령 20 개였다.

파일의 절반이 "이 시스템이 무엇을 하는가" 가 아니라 "그것을 어떻게 손으로
찔러보는가" 였다. 캡처·코덱·라우팅을 고치러 열었다가 검증 도구 사이를
스크롤하며 헤매게 된다.

**왜 분리가 안전했나.** 이 명령들은 전부 `FindVoiceSubsystem()` 으로
서브시스템을 찾아 **public 함수만** 부른다. private 멤버를 하나도 건드리지
않는다. 그래서 friend 선언도, 헤더 변경도, 접근자 추가도 없이 **파일만
옮기면** 끝났다.

> 이 성질은 앞으로도 지켜야 하는 규칙이다. 콘솔 명령이 private 를
> 필요로 한다면 그건 "명령이 지저분해서" 가 아니라 **그 기능이 아직
> API 로 정리되지 않았다는 신호**다. 그때는 명령 쪽을 늘리지 말고
> 서브시스템에 공개 함수를 먼저 만든다.

**결과.**

| 파일 | 예전 | 지금 |
|---|---|---|
| `VoiceSubsystem.cpp` | 2105줄 | **1093줄** |
| `VoiceConsoleCommands.cpp` | — | 1061줄 (신규) |

**수정 파일**

- `Private/Voice/VoiceConsoleCommands.cpp` (신규)
- `Private/Voice/VoiceSubsystem.cpp` (해당 블록 제거)

**영향 검사.**

- 헤더 변경 없음 → 다른 어떤 파일도 다시 볼 필요가 없다.
- `FAutoConsoleCommand` 는 **정적 객체 생성자에서 등록**되므로, 어느
  번역 단위에 있든 모듈이 로드되면 등록된다. 명령 이름과 동작은 그대로다.
- 익명 네임스페이스의 `FindVoiceSubsystem` 도 같이 옮겨서 이름 충돌이 없다.
- 옮긴 블록이 쓰던 타입을 전부 조사해 include 를 새로 구성했다
  (AI 지각, `VoiceModule`, 타이머 등).

---

## R-3. 방 호스트 주소 치환

이 항목의 증상과 진단 절차는 `SERVER_INTEGRATION.md` 13절
("참가자가 ... 무한 로딩") 에 있다. 여기서는 **설계 판단**만 남긴다.

**문제의 본질.** 방의 호스트 주소를 `accept()` 의 피어 주소로 정하는 것은
"클라이언트가 신고한 주소를 믿지 않는다" 는 **옳은 원칙**이었다. 신고를
믿으면 남의 주소를 적어 엉뚱한 곳으로 접속을 몰아줄 수 있다.

문제는 그 원칙이 **호스트와 서버가 같은 NAT 안에 있는 경우**를 다루지
못한다는 것이었다. 그때 피어 주소는 사설 IP 이고, 헤어핀까지 타면
게이트웨이 IP 가 찍힌다. 외부 참가자는 그 주소로 갈 수 없다.

**택하지 않은 안.** "클라이언트가 자기 공인 IP 를 신고하게 한다" —
가장 쉽지만 원칙을 그대로 버리는 것이다. 채택하지 않았다.

**택한 안.** 서버가 **자기가 아는 두 값 중에서만** 고르게 한다.

1. `accept()` 의 피어 주소 — 공인이면 그대로 쓴다
2. 서버 자신의 공인 IP — 피어가 사설이면 이걸 쓴다

클라이언트가 보내온 값은 여전히 쓰지 않으므로 원칙이 유지된다.

```cpp
std::string ResolveHostAddress(const std::string& PeerAddress)
{
    if (GPublicIp.empty() || !IsPrivateAddress(PeerAddress))
    {
        return PeerAddress;
    }
    return GPublicIp;
}
```

서버 자신의 공인 IP 는 `--public-ip` 로 주거나 `--upnp` 성공 시 자동으로
얻는다. 사설 주소를 `--public-ip` 로 주면 상황만 나빠지므로 **시작을
거부한다** — 조용히 무시하면 원인을 찾을 수 없기 때문이다.

**남은 한계.** 방장과 참여자가 **둘 다** 서버와 같은 공유기 안이면
참여자도 헤어핀을 타야 한다. 공유기가 지원하지 않으면 그 조합만 실패한다.
제대로 풀려면 STUN/ICE 처럼 후보 주소를 여러 개 주고 되는 것을 고르게
해야 하는데, 그건 `SERVER_INTEGRATION.md` 14-5절의 릴레이 전환과 같이 갈
이야기다.

**수정 파일** — `MOU_Server/Server/Server.cpp`

---

## B-1. 빌드 스크립트가 조용히 실패하던 문제

**증상.** NAT 코드를 넣고 커밋했는데 서버가 `[NAT]` 로그를 하나도 안 찍었다.
원인을 찾는 데 하루가 걸렸다.

**원인 두 겹.**

1. `build_server.bat` 이 VS 경로를 `2022\Community` 로 **하드코딩**하고 있었다.
   VS 2026(`18\Community`)만 깔린 PC 에서는 `cl` 을 못 찾고 **조용히** 실패했다.
2. 그래서 NAT 이전에 빌드된 `Server.exe` 가 그대로 남았다. 그런데 옛 `main()`
   의 인자 검사는 `argc < 2 || argc > 3` 이라 `Server.exe 9000 --upnp` 가
   **에러 없이 통과한다.** 대신 `--upnp` 를 **DB 경로로 해석**해서 `--upnp`
   라는 이름의 빈 SQLite 파일을 만든다. 계정이 전부 사라진 것처럼도 보인다.

**진짜 문제는 "조용히" 였다.** 빌드가 실패했으면 실패했다고 말해야 하고,
바이너리가 낡았으면 낡았다고 말해야 한다. 둘 다 침묵했다.

**바꾼 것.**

- `build_server.bat` — VS 설치 경로를 후보 목록에서 자동 탐색.
  못 찾으면 `[ERROR]` 를 찍고 `exit /b 1`. 컴파일 실패도 마찬가지.
- `run_server.bat` (신규) — 켜기 전에 세 가지를 먼저 확인한다.
  - `Server.exe` 가 NAT 기능이 있는 빌드인지 (`--upnp` 문자열 유무로 판정)
  - LAN IP / 게이트웨이 / 공인 IP
  - `DefaultGame.ini` 의 `ServerHost` 가 실제 공인 IP 와 일치하는지

**★ 배치 파일은 ASCII 만 쓴다.** `cmd` 는 `.bat` 을 **콘솔 코드페이지**로
파싱하는데 이 값이 PC 마다 다르다(949 / 65001). UTF-8 로 저장한 한글
주석이 949 콘솔에서는 명령어로 오해돼서 **스크립트 자체가 깨진다**
(`'d' is not recognized...`). 실제로 재현했다. 그래서 한글 설명은
이 문서와 `SERVER_INTEGRATION.md` 에 두고, 배치 파일은 ASCII 로 고정했다.

---

## B-2. .gitignore — 산출물은 막고 스크립트는 지킨다

**증상.** pull 받을 때마다 `Server.exe` 가 없어서 매번 누군가에게
"빌드해달라" 고 부탁해야 했다.

**진단.** `*.exe` 를 무시하는 것 **자체는 옳다.** 산출물을 커밋하면 툴체인이
다른 사람끼리 충돌만 난다. 진짜 문제는 **직접 빌드할 수단이 저장소에
없었다**는 것이었다.

지금은 `build_server.bat` / `run_server.bat` 이 그 수단이다. 확인해보니
둘 다 이미 정상 추적 중이었지만, 나중에 누가 "빌드 산출물 정리" 한다고
폭넓은 패턴을 넣었을 때 조용히 사라질 수 있다. 그래서 **못을 박았다.**

```gitignore
# 산출물은 막고
Server_Build/*.exe
Server_Build/*.obj
Server_Build/*.db

# 만드는 수단은 반드시 지킨다
!*.bat
!build_server.bat
!run_server.bat
```

**원칙: 만들어지는 것은 무시하고, 만드는 것은 반드시 커밋한다.**

**수정 파일** — `MOU_Server/.gitignore`

---

# B. 발견했으나 아직 안 고친 것

여기 있는 것들은 **지금 동작에 문제가 없다.** 고치면 나아지지만, 지금
고치면 검증 부담이 이득보다 커서 미뤘다. 각 항목에 "언제 하면 좋은가" 를
같이 적어둔다.

## R-2. ServerSubsystem 도 같은 비대화

`Private/Server/ServerSubsystem.cpp` 에 콘솔 명령이 **16개** 있다.
[R-1](#r-1-콘솔-명령을-별도-파일로-분리) 과 같은 문제다.

**왜 지금 안 했나.** 오늘 이 파일을 `BeginPreloadMap` 으로 이미 건드렸다.
성능 수정과 파일 분리를 한 커밋에 섞으면, 나중에 문제가 생겼을 때 어느
쪽 때문인지 가릴 수 없다.

**할 때.** P-2 가 실제 플레이로 검증된 뒤. 방법은 R-1 과 동일하다 —
먼저 명령들이 public API 만 쓰는지 확인하고, 그렇다면
`Private/Server/ServerConsoleCommands.cpp` 로 옮기면 된다.

## R-4. 두 상태 위젯의 골격 중복

`URadioStatusWidget`(501줄) 과 `UVoiceStatusWidget`(336줄) 이 **같은 골격**을
각자 갖고 있다.

| 공통 요소 | 하는 일 |
|---|---|
| `RefreshInterval` / `TimeSinceLastRefresh` | 매 프레임 문자열을 만들지 않으려는 폴링 주기 조절 |
| `NativeTick` 에서 주기 갱신 | 위와 세트 |
| `BuildDefaultLayout()` | WBP 없이 C++ 로 캔버스 + 텍스트 조립 |
| 레거시 `InputComponent` 에 키 직접 바인딩 | `NativeConstruct` 에서 걸고 `NativeDestruct` 에서 뗀다 |
| `RootCanvas` + 화면 모서리 배치 | 우상단(마이크) / 우하단(무전기) |

**제안.** `UVoiceHudWidgetBase`(가칭)를 만들어 위 다섯 가지를 올린다.
파생 클래스는 `RefreshStatusText()` 와 키 목록만 채운다.

**왜 지금 안 했나.** 이 중복은 **동작하는 코드**이고, 두 위젯 다
`VOICE_INTEGRATION.md` 의 검증 절차에 쓰이는 도구다. 공통 부모를 만들면
두 위젯을 다시 전부 손으로 검증해야 하는데, 지금은 서버 쪽 검증이 먼저다.

**할 때.** 세 번째 상태 위젯이 필요해지는 순간. 두 번은 참을 수 있지만
세 번째부터는 반드시 부모를 만들어야 한다.

## R-5. VOICE_INTEGRATION.md 7-4절이 폐기된 설계를 담고 있다

`Public/Voice/Radio.h` 상단에 이렇게 적혀 있다:

> ※ 문서의 7-4절은 위 폐기 결정 이전에 쓰인 것이라 아직 옛 설계를 담고 있다.

폐기된 설계는 **"바닥에 떨어진 무전기가 계속 울려 NPC 를 유인한다"** 이고,
확정된 규칙은 **"드롭하면 전원이 자동으로 꺼진다"** 이다.

**왜 위험한가.** 코드는 맞고 문서는 틀린 상태다. 이 시스템을 처음 보는
사람은 문서를 먼저 읽는다. 그리고 "떨어진 무전기가 안 울리는데?" 를
**버그로 신고**하게 된다. 헤더 주석에 경고가 있지만, 헤더까지 읽는
사람은 이미 코드를 보고 있는 사람이다.

**할 때.** 다음에 `VOICE_INTEGRATION.md` 를 열 때 반드시. 코드 수정이
필요 없는, 순수하게 문서만 고치면 되는 항목이다.

## R-6. 프레이밍 코드 이중화

`Shared/Framing.cpp`(서버) 와 `Private/Server/Net/ChatFraming.cpp`(언리얼) 이
같은 길이 프리픽스 프레이밍을 각자 구현하고 있다. NAT 도 마찬가지로
두 벌이다(`MOU_Server/Server/NatPortMapping.cpp` /
`Private/Server/Net/NatPortMapping.cpp`).

**이건 의도된 것이다.** `SERVER_INTEGRATION.md` 6절에 이유가 있다 —
서버 쪽은 언리얼 엔진이 없어 `FSocket` 을 쓸 수 없고, 언리얼 쪽은 winsock 을
직접 include 하면 엔진 매크로와 충돌한다.

**그래도 기록해두는 이유.** 오늘 [P-1](#p-1-upnp-탐색-9초--3초) 에서 정확히
이 이중화 때문에 **한쪽만 고칠 뻔했다.** 이중화 자체는 유지하더라도,
"한쪽을 고치면 반드시 다른 쪽도 본다" 는 것이 규칙으로 남아야 한다.

**제안.** 두 파일 상단 주석에 서로를 가리키는 링크를 넣는다(NAT 쪽에는
오늘 넣었다). 프로토콜 로직이 더 늘어나면, 엔진에 의존하지 않는 순수
로직만 헤더 온리로 뽑아 양쪽이 include 하는 방법을 검토한다.

---

# C. 리팩토링 원칙

오늘 작업에서 반복해서 적용한 판단 기준이다.

**1. 조용한 실패를 없애는 것이 최우선이다.**
가장 비싼 버그는 틀린 동작이 아니라 **아무 말도 하지 않는 동작**이었다.
빌드 실패([B-1](#b-1-빌드-스크립트가-조용히-실패하던-문제)), 옛 바이너리,
포트포워딩 비활성 — 전부 화면상으로는 정상으로 보였다.
그래서 `run_server.bat` 은 켜기 전에 상태를 먼저 찍고,
`--public-ip` 에 사설 주소가 오면 시작을 거부한다.

**2. 성능은 대기의 순서를 바꿔서 얻는다.**
P-1 과 P-2 둘 다 코드를 빠르게 만든 것이 아니다. **기다리는 순서**를
바꿨을 뿐이다. 셋을 차례로 기다리던 것을 한 번에 기다리게 했고,
앞뒤로 붙어 있던 두 로딩을 겹치게 했다.

**3. 소유권이 최적화의 성패를 가른다.**
P-2 는 코드를 다 짜고도 **참조를 누가 들고 있느냐** 때문에 무효가 될 뻔했다.
"이 객체가 언제 죽는가" 를 먼저 확인하지 않으면 최적화는 조용히 사라진다.

**4. 원칙을 버리지 말고 원칙 안에서 값을 늘린다.**
R-3 에서 "클라이언트를 믿지 않는다" 는 원칙은 그대로 두고, 서버가 아는
값의 **가짓수**를 늘려서 풀었다.

**5. 한 커밋에 두 종류의 변경을 섞지 않는다.**
R-2 를 오늘 하지 않은 이유다. 성능 수정과 파일 이동이 같은 커밋에 있으면
문제가 생겼을 때 원인을 가릴 수 없다.

**6. 파일을 고치면 그 파일이 영향을 주는 곳까지 본다.**
각 항목의 "영향 검사" 절이 그 기록이다. P-1 에서는 복제된 서버 쪽 파일을,
P-2 에서는 `HandleGameStarted` 가 방장에게도 불린다는 사실을 그렇게 찾았다.

---

## 검증 기록

| 항목 | 검증 방법 | 결과 |
|---|---|---|
| P-1 (언리얼) | `TeamProject_MOUEditor` 빌드 | 통과 |
| P-1 (서버) | `build_server.bat` | 통과 |
| P-2 | `TeamProject_MOUEditor` 빌드 (UHT 재생성 포함) | 통과 |
| R-1 | `TeamProject_MOUEditor` 빌드 | 통과 |
| R-3 | `TestClient` 로 방 생성 → 서버 로그에서 주소 치환 확인 | 통과 |
| B-1 | `build_server.bat` 재실행 | 통과 |
| B-2 | `git check-ignore` 로 bat/exe/obj/db 판정 확인 | 통과 |

> P-2 는 **빌드까지만** 검증했다. 실제 체감 개선은 큰 맵으로 방장/참여자
> 플레이를 해봐야 확인된다. `LogMOUServer` 에 `맵 미리 올리기 완료` 가
> 찍히고 그 뒤 `ClientTravel` 이 즉시 끝나는지 보면 된다.
