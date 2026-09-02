# MOU 채팅 설계 (메신저 / 친구 / 인게임 채팅)

작성: 2026-08-24
최종 갱신: 2026-08-25

상태: **M0~M11 구현 완료. 서버·클라 양쪽 빌드 통과.**
- 서버(M1~M6): 실서버 + `TestClient` 2개로 **실행 검증됨**
- 클라(M7~M9): `TeamProject_MOUEditor Win64 Development` **빌드 통과, 경고 0**
- M10: (구) 전체채팅을 지우지 않고 `Chat/InGame/` 으로 이관, 로비 자동생성은 끔
- M11: 실사용 중 발견된 UI 버그 2건(패널 폭 흔들림, 상태 문구 안 지워짐) 수정
- ⚠️ **아직 안 한 것**: 로비 화면에 `UMessengerWidgetBase` 를 실제로 붙이는 작업.
  위젯은 있지만 아무도 `CreateWidget` 하지 않는다 — 12-1절.

이 문서는 `SERVER_INTEGRATION.md` 와 같은 규칙을 따른다 — 무엇을 하는지보다
**왜 그렇게 했는지**를 남긴다.

---

## 목차

1. [무엇을 만드는가](#1-무엇을-만드는가)
2. [왜 폴더를 Server/ 로 바꿨는가](#2-왜-폴더를-server-로-바꿨는가)
3. [두 채팅은 완전히 다른 시스템이다](#3-두-채팅은-완전히-다른-시스템이다)
4. [친구 시스템](#4-친구-시스템)
5. [접속 상태(Presence)](#5-접속-상태presence)
6. [메신저 (1:1 DM)](#6-메신저-11-dm)
7. [프로토콜 v7](#7-프로토콜-v7)
8. [DB 스키마](#8-db-스키마)
9. [클래스 설계](#9-클래스-설계)
10. [UI 설계](#10-ui-설계)
11. [인게임 채팅 (추후 구현)](#11-인게임-채팅-추후-구현)
12. [구현 순서](#12-구현-순서)
13. [알려진 함정](#13-알려진-함정)
14. [정하지 않은 것](#14-정하지-않은-것)

---

## 1. 무엇을 만드는가

지금 로비에는 로그인과 방 만들기밖에 없다. 여기에 **롤 클라이언트식 친구 목록 +
메신저**를 붙인다.

| # | 기능 | 한 줄 정의 |
|---|---|---|
| A | **친구** | 닉네임으로 찾아 신청하고, 상대가 수락하면 서로 친구가 된다 |
| B | **접속 상태** | 친구가 지금 오프라인인지, 온라인인지, 게임 중인지 보인다 |
| C | **메신저** | 친구와 1:1 대화. **내용이 서버에 기록되어 다시 열면 남아 있다** |
| D | **인게임 채팅** | (추후) 죽은 사람끼리만 쓰는 채팅. **이 시스템이 아니다** — 11절 |

### 이 설계가 지키는 것

> **A·B·C 는 전부 `Server.exe`(외부 TCP 서버)가 진실을 갖는다.**
> 게임의 리슨서버는 이 셋에 대해 아무것도 모른다. 그래서 호스트가 게임을 나가도,
> 게임을 안 하고 로비에만 있어도 친구·메신저는 그대로 돌아간다.
>
> **D 는 정반대다.** 리슨서버가 진실을 갖고 `Server.exe` 는 관여하지 않는다.
> 이 둘을 한 시스템으로 묶으려던 흔적이 지금 코드에 남아 있는데(`EChatChannel::Dead`,
> `SetDead` 옵코드), **그것이 이번 재편으로 폐기되는 부분이다** — 3절.

---

## 2. 왜 폴더를 `Server/` 로 바꿨는가

기존 `Chat/` 폴더에는 처음부터 채팅이 아닌 것이 더 많았다.

```
Chat/  (구)
  ServerSubsystem  ServerClientRunnable  ChatFraming  ChatTypes  ChatWidgetBase   ← 채팅
  LoginWidgetBase                                                             ← 계정
  LobbyBackend  SocketLobbyBackend  EOSLobbyBackend  LobbyTypes               ← 로비
  LobbyWidgetBase  RoomCreateWidgetBase  RoomListWidgetBase                   ← 방/대기실
  ServerSettings                                                              ← 설정
```

**공통점은 "채팅" 이 아니라 "`Server.exe` 와 TCP 로 말하는 코드" 다.** 이름이
내용을 속이고 있었고, 여기에 친구·메신저까지 들어오면 더 나빠진다.

### 새 구조

```
Public/Server/                    (Private/ 도 같은 구조)
  ServerSettings.h                서버 주소 / 백엔드 종류        [공용]
  ServerSubsystem.h                 진입점. UI 는 이것만 안다      [공용]
  Net/                            ★ 바이트를 다루는 층
    ChatFraming.h                   길이 프레이밍 + UTF-8 변환
    ServerClientRunnable.h            FRunnable 워커. 소켓 송수신
  Lobby/                          ★ 로그인 · 방 · 대기실
    LobbyBackend.h                  ILobbyBackend 인터페이스 + 팩토리
    SocketLobbyBackend.h            자체 서버 구현 (패킷 조립은 여기서만)
    EOSLobbyBackend.h               EOS 뼈대
    LobbyTypes.h                    BP 노출 로비 타입
    LoginWidgetBase.h               로그인 / 가입 창
    LobbyWidgetBase.h               메인메뉴 + 대기실
    RoomCreateWidgetBase.h          방 만들기
    RoomListWidgetBase.h            방 목록
  Chat/                           ★ 채팅
    ChatTypes.h                     BP 노출 채팅 타입
    MessengerWidgetBase.h           UDmWindowWidget + UMessengerWidgetBase
    CHAT_DESIGN.md                  이 문서
    InGame/                         ★ 죽은사람 채팅 (추후 — 11절)
      ChatWidgetBase.h                (구) 전체채팅. 화면 코드를 재활용하려고 보관
  Social/                         ★ 친구
    FriendTypes.h                   FMOUFriend, FMOUDirectMessage, enum 3종
    FriendListWidgetBase.h          UFriendEntryWidget + UFriendListWidgetBase
```

> **설계 초안과 달라진 것 두 가지** (구현하면서 정리됨):
>   · `DirectMessageTypes.h` 를 따로 두지 않았다 — `FMOUDirectMessage` 가
>     구조체 하나뿐이라 `FriendTypes.h` 에 같이 뒀다. 파일을 나누면 include 만 늘어난다.
>   · `FriendAddWidgetBase.h` 를 따로 두지 않았다 — 친구 추가는 입력창 + 버튼
>     하나라 별도 창이 필요 없고, `UFriendListWidgetBase` 상단에 붙였다.
>     롤도 같은 자리에 있다.

**`ServerSubsystem` 과 `ServerSettings` 만 `Server/` 바로 아래 둔 이유**: 이 둘은
어느 하위 폴더에도 속하지 않는다. `ServerSubsystem` 은 Net·Lobby·Chat·Social 을
전부 다루는 진입점이고, `ServerSettings` 는 넷이 공통으로 읽는다. 하위로 내리면
"왜 저기 있지" 를 매번 되짚어야 한다.

> **`ServerSubsystem` 이라는 이름은 그대로 둔다.** 내용상 `ServerSubsystem` 이
> 맞지만, 지금 이름을 바꾸면 블루프린트에서 이 노드를 참조하는 자산이 전부
> 끊어진다(BP 는 클래스 이름으로 참조한다). **재편의 목적은 정리이지 사고가
> 아니다.** 이름 변경은 BP 참조를 한 번에 훑을 수 있을 때 따로 한다.

### 이번에 실제로 한 일

- `git mv` 로 27개 파일 이동 (**히스토리 보존됨** — `git log --follow` 가 그대로 동작)
- `#include "Chat/..."` → `#include "Server/..."` 전부 갱신 (25개 파일)
- `Build.cs` 주석, `SERVER_INTEGRATION.md` 경로표 갱신
- 외부 참조는 `TeamProject_MOUPlayerController.cpp` 2줄뿐이었다

---

## 3. 두 채팅은 완전히 다른 시스템이다

**이 절이 이 문서에서 가장 중요하다.** 이름이 둘 다 "채팅" 이라 같은 것으로 보이지만,
**경로도 권위자도 수명도 전부 다르다.**

| | **로비 채팅 (메신저)** | **인게임 채팅 (추후)** |
|---|---|---|
| 경로 | `Server.exe` 로 가는 별도 TCP | 게임 리슨서버의 UE 리플리케이션 |
| 권위자 | `Server.exe` | 방장의 리슨서버 |
| 언제 쓰나 | 로비에 있을 때 (게임 전/후) | 게임 중, **죽었을 때만** |
| 대상 | 친구 1:1 | 같은 방의 죽은 사람 전원 |
| 기록 | **남는다** (SQLite) | **안 남는다** (게임 끝나면 사라짐) |
| 호스트가 나가면 | 영향 없음 | 같이 사라짐 (세션 자체가 없어짐) |
| 구현 위치 | `Server/Chat/` + `MOU_Server/` | `Server/Chat/InGame/` (예정) |

### ★ 폐기되는 것 — `Dead` 채널과 `SetDead` 옵코드

지금 프로토콜에는 이런 것들이 있다.

```cpp
enum class EChatChannel : uint8_t { All, Team, Dead, Whisper, System };
SetDead = 8,   // 지금은 테스트 클라가 보내지만, 나중에는 리슨서버만 보낸다
```

`ServerSubsystem.h` 주석에도 남아 있다:

> *"현재 미해결(8단계 예정): TeamId 와 생사 여부의 권위자는 게임(리슨서버)인데
> 채팅 서버는 그 정보를 모른다. (…) 리슨서버가 채팅 서버에 미러링하도록 바꿔야 한다."*

**이 계획을 폐기한다.** 이유:

1. **미러링이 공짜가 아니다.** 리슨서버가 "누가 죽었다" 를 `Server.exe` 에
   계속 알려야 하는데, 그러려면 리슨서버가 외부 서버에 또 하나의 인증된 연결을
   유지해야 한다. 지금 그 연결이 없고, 만들면 "호스트가 채팅 서버와 끊겼을 때
   게임 내 채팅이 죽는" 새 실패 모드가 생긴다.
2. **얻는 것이 없다.** 죽은 사람 채팅은 **그 게임 안에서만** 의미가 있고
   기록도 필요 없다. 외부 서버를 태울 이유가 전혀 없다.
3. **왕복이 길어진다.** 리슨서버 → `Server.exe` → 다시 각 클라. 같은 방
   사람끼리 하는 말이 서버를 두 번 건너간다.

**그래서 인게임 채팅은 리슨서버 안에서 RPC 로만 처리한다**(11절).
`EChatChannel::Dead` 와 `SetDead` 는 **프로토콜에서 지우지는 않되 더 이상 쓰지
않는다** — 지우면 옵코드 번호가 밀려 v7 호환이 깨지고, 남겨두면 `TestClient` 로
과거 동작을 재현할 수 있다.

> **`ChatWidgetBase` 는 어떻게 됐나 (2026-08-24 처리 완료).**
>
> **지우지 않고 `Server/Chat/InGame/` 으로 옮겼다.** 로비에서는 더 이상 뜨지
> 않는다(`ULoginWidgetBase::bShowChatWidgetOnSuccess` 기본값을 false 로 내렸다).
>
> 지우지 않은 이유: 그 안에 있는 것이 **죽은사람 채팅에 거의 그대로 필요하다** —
> 채팅 로그 스크롤 · 최대 줄 수 관리 · 입력창 엔터 전송 · 토글 키 바인딩 ·
> PIE 다중 창 대응(`TMap<World, Widget>`). 화면은 같고 "누가 받는가" 만 다르다.
> 지웠다가 다시 만드는 것보다 남겨두는 편이 싸다.
>
> **★ 인게임용으로 바꿀 때 아래쪽 절반은 버려야 한다.** 지금은
> `UServerSubsystem` 을 구독해 TCP 로 보내는데, 죽은사람 채팅은 리슨서버 RPC 다.
> 자세한 것은 그 헤더 상단 주석과 11절.

---

## 4. 친구 시스템

### 4-1. 상태 전이

```
   (남)
     │  FriendAddReq(닉네임)
     ▼
  Pending ─────────────────────────┐
     │  상대가 Accept               │  상대가 Decline
     │                             │  또는 내가 Cancel
     ▼                             ▼
  Friend ──── 누구든 Remove ────> (남)
```

**대칭이 아니다.** `Pending` 은 방향이 있다(내가 보냄 / 내가 받음). 화면도 다르다 —
보낸 쪽은 "대기 중" 으로 회색, 받은 쪽은 **수락/거절 버튼**이 뜬다.

### 4-2. 친구를 어떻게 찾는가 — ★ 지금은 닉네임, 나중에 #태그

**결정 (2026-08-24)**: 지금은 **닉네임 정확 일치**로 찾는다. `#태그` 는 나중에 붙인다.

여기에 문제가 하나 있다. **`accounts.nickname` 은 UNIQUE 가 아니다.**

```sql
-- Accounts.cpp 73행
CREATE TABLE IF NOT EXISTS accounts(
  login_id  TEXT NOT NULL UNIQUE COLLATE NOCASE,   -- 유일
  nickname  TEXT NOT NULL,                          -- ★ 유일하지 않다
  ...
```

즉 **같은 닉네임이 여럿 있을 수 있고, 지금도 그렇다.** 대응은 셋 중 하나인데:

| 안 | 내용 | 판정 |
|---|---|---|
| a | `nickname` 에 UNIQUE 를 건다 | ✕ **`#태그` 계획과 정면 충돌**. 태그를 붙이는 이유가 닉 중복 허용인데, 지금 UNIQUE 를 걸면 나중에 풀어야 한다 |
| b | 중복이면 `AmbiguousName` 오류 | ✅ **채택** |
| c | 여러 명을 목록으로 돌려주고 고르게 한다 | ✕ 지금 필요 없는 UI 를 만든다. 태그가 들어오면 그 UI 가 다시 사라진다 |

**b 를 택한 이유**: 중복 닉네임에서 나는 "여러 명입니다" 오류가 **`#태그` 를 붙일
동기 그 자체**다. 지금 억지로 막아두면 태그가 왜 필요한지 알 수 없게 되고,
나중에 되돌리는 작업이 생긴다. 마이그레이션도 필요 없다.

### ★ 태그를 나중에 붙여도 프로토콜을 안 바꾸는 법

**검색어를 파싱하는 쪽을 서버에 둔다.** 클라는 사용자가 친 문자열을 그대로 보낸다.

```cpp
// 지금
constexpr uint32_t kMaxFriendQueryLen = kMaxNameLen + 8;  // 닉(32) + '#' + 태그 + 여유
struct FriendAddReqBody { char Query[kMaxFriendQueryLen]; };
```

서버의 처리:

```
Query 에 '#' 이 없다  →  nickname 완전 일치로 조회
                          결과 0개  → NotFound
                          결과 1개  → 그 사람에게 신청
                          결과 2개+ → AmbiguousName   ← 지금 여기서 태그가 필요해진다

Query 에 '#' 이 있다  →  (지금) InvalidFormat
                          (나중) nickname + tag 로 조회
```

**이렇게 하면 태그 도입 시 바뀌는 곳이 서버 함수 하나뿐이다.** 프로토콜 버전도,
클라 코드도, UI 도 그대로다. 필드 폭을 처음부터 태그까지 담을 만큼 잡아두는 것이
전부다 — 40바이트 아끼자고 나중에 v8 을 찍는 것이 훨씬 비싸다.

### 4-3. 규칙

- **자기 자신에게 신청 불가** (서버가 막는다)
- **이미 친구인데 또 신청 불가** → `AlreadyFriend`
- **양쪽이 서로에게 신청하면 즉시 친구가 된다** — 수락을 기다리게 하면
  "둘 다 신청했는데 왜 친구가 아니지" 가 된다
- **친구 수 상한 `kMaxFriends = 100`** — `FriendListAck` 가 `kMaxBodySize`(4096)를
  넘지 않게 하기 위한 값이다. 8절 계산 참고
- **차단은 만들지 않는다** (14절)

---

## 5. 접속 상태(Presence)

### 5-1. 3단계

**결정 (2026-08-24)**: `오프라인` / `온라인` / `게임중` 3단계.

| 상태 | 서버가 판정하는 근거 | 화면 |
|---|---|---|
| `Offline` | 그 계정의 인증된 세션이 없다 | ○ 회색 |
| `Online` | 세션이 있다. 방에 없거나, 방이 `Waiting` | ● 초록 |
| `InGame` | 방에 있고 그 방이 `ERoomState::InGame` | ● 노랑 |

**추가 비용이 거의 없다.** 서버는 이미 `SessionManager` 로 접속을 알고,
`Rooms` 로 방 상태를 안다. 새로 추적할 것이 없고 두 값을 합치기만 하면 된다.

> **`대기중`(방에 들어가 앉아 있음)을 따로 두지 않은 이유**: 상태가 4개가 되면
> 색·아이콘을 4종 준비해야 하는데, 친구 입장에서 `온라인` 과 `대기중` 의 차이가
> 행동을 바꾸지 않는다. 둘 다 "지금 말 걸어도 된다" 다. **행동을 바꾸는 경계는
> 게임 중인가 아닌가 하나뿐**이라 거기서만 나눈다.

### 5-2. 어떻게 전파하는가 — ★ 폴링하지 않는다

```
누군가의 상태가 바뀌는 순간은 넷뿐이다:
  1. 로그인 성공            → Online
  2. 연결 끊김              → Offline
  3. 방이 InGame 이 됨      → 그 방 멤버 전원 InGame
  4. 방이 사라지거나 나감    → Online

그 순간에 **그 사람의 친구 중 접속해 있는 사람에게만** FriendPresence 를 밀어준다.
```

**폴링(클라가 주기적으로 물어보기)을 하지 않는 이유**: 친구 100명 × 접속자 N명이
몇 초마다 전체 목록을 요청하면, 아무 일도 안 일어나는 동안에도 트래픽이 계속 흐른다.
상태 변화는 드문 사건이라 **바뀔 때만 밀어주는 쪽이 압도적으로 싸다.**

**로그인 직후에는 한 번 전체를 받는다**(`FriendListAck`). 그 뒤로는 델타만 온다.

> **★ 알림을 보낼 대상을 찾는 비용.** "내 친구 중 접속자" 를 구하려면 매번
> `friends` 테이블을 조회해야 한다. 로그인 시 그 사람의 친구 ID 목록을 세션에
> 캐시해 두고(`ClientSession::FriendIds`), 친구가 추가/삭제될 때만 갱신한다.
> 상태 변화마다 DB 를 때리면 접속자가 늘수록 로그인·로그아웃이 느려진다.

---

## 6. 메신저 (1:1 DM)

### 6-1. 오프라인 메시지

**결정 (2026-08-24)**: **저장 후 접속 시 전달.**

```
보낸다 → 서버가 dm_messages 에 INSERT (항상)
           │
           ├─ 상대가 접속 중  → 즉시 DirectMessage 로 밀어준다
           └─ 상대가 오프라인 → 아무것도 안 한다. DB 에만 남는다
                                 상대가 로그인하면 안 읽은 것을 내려준다
```

**핵심: 저장이 먼저다.** 전송 성공 여부와 무관하게 INSERT 한다. 순서를 바꾸면
"보내는 데는 성공했는데 기록이 없는" 메시지가 생긴다.

### 6-2. 읽음 처리

`read_at` 은 **대화창을 실제로 연 시점**에 찍는다. 친구 목록에 안 읽은 개수 배지를
띄우려면 이 값이 필요하다.

```
MessengerOpenReq(상대ID)  →  서버: 최근 N개 내려주고, 그 사람과의 미읽음을 read 처리
```

> **왜 클라가 "읽었다" 를 따로 보내지 않는가**: 창을 여는 것이 곧 읽는 것이다.
> 별도 옵코드를 두면 창은 열었는데 읽음 신호를 놓치는 경우가 생기고, 그러면
> 배지가 영원히 안 사라진다. **한 왕복에 묶는 편이 상태가 어긋날 여지가 없다.**

### 6-3. 기록 조회

- 대화창을 열면 **최근 50개**를 받는다 (`kDmPageSize = 50`)
- 위로 스크롤하면 그 이전 것을 더 받는다 (`BeforeMessageId` 기준 커서)
- **오프셋(`LIMIT n OFFSET m`)이 아니라 커서를 쓰는 이유**: 스크롤하는 도중에
  새 메시지가 오면 오프셋이 밀려서 같은 줄을 두 번 받거나 한 줄을 건너뛴다

### 6-4. 이 기록은 `chat_log` 와 다른 테이블이다

기존 `chat_log` 는 **비동기 큐**로 쓴다(`ChatLog.h`) — 몇 줄 유실돼도 괜찮다는
전제였다. **DM 은 그 전제가 안 맞는다.** 친구에게 보낸 메시지가 서버 재시작으로
사라지면 그건 버그다. 그래서:

| | `chat_log` (기존) | `dm_messages` (신규) |
|---|---|---|
| 쓰기 | 비동기 큐 → 라이터 스레드 | **동기** (`Accounts` 방식) |
| 유실 허용 | 허용 (큐에 남은 것) | **불가** |
| 조회 | 안 함 (기록용) | **자주** — 대화창을 열 때마다 |

동기 쓰기의 지연이 문제되면 그때 그룹 커밋을 넣는다. **DM 은 초당 수천 건이
날아오는 트래픽이 아니다** — 사람이 타이핑하는 속도가 상한이다.

---

## 7. 프로토콜 v7

`Shared/ChatProtocol.h` 에 추가한다. **기존 옵코드 번호는 건드리지 않는다.**

```cpp
//   6 -> 7 : 친구 + 메신저 추가. 접속 상태(Presence) 전파.
//            EChatChannel::Dead 와 SetDead 는 더 이상 쓰지 않는다(인게임 채팅이
//            리슨서버로 옮겨감). 번호는 호환을 위해 남겨둔다.
constexpr uint16_t kProtocolVersion = 7;
```

### 7-1. 새 옵코드

| 번호 | 이름 | 방향 | 뜻 |
|---|---|---|---|
| 27 | `FriendListReq` | C→S | 내 친구 + 대기 중인 신청 전부 |
| 28 | `FriendListAck` | S→C | 위의 응답 (뒤에 `FriendEntry` N개) |
| 29 | `FriendAddReq` | C→S | 닉네임(또는 닉#태그)으로 신청 |
| 30 | `FriendAddAck` | S→C | 신청 결과 |
| 31 | `FriendRequestIncoming` | S→C | **누가 나에게 신청했다** (실시간) |
| 32 | `FriendRespondReq` | C→S | 수락 / 거절 |
| 33 | `FriendRemoveReq` | C→S | 친구 삭제 (또는 보낸 신청 취소) |
| 34 | `FriendUpdate` | S→C | 친구 하나의 상태 변화 (추가/삭제/수락됨) |
| 35 | `FriendPresence` | S→C | 친구 하나의 접속 상태 변화 |
| 36 | `DirectMessageSend` | C→S | DM 보내기 |
| 37 | `DirectMessage` | S→C | DM 도착 (실시간 또는 로그인 시 밀린 것) |
| 38 | `DmHistoryReq` | C→S | 대화 기록 조회 (창 열기 / 위로 스크롤) |
| 39 | `DmHistoryAck` | S→C | 기록 응답 (뒤에 `DmEntry` N개) |

### 7-2. 주요 구조체

```cpp
// --- 친구 (v7) ---
// ★ 100 이 아니라 93 이다. 이유는 7-3절 계산 참고 — 100 이면 패킷 상한을 넘는다.
constexpr uint32_t kMaxFriends        = 93;
constexpr uint32_t kMaxFriendQueryLen = kMaxNameLen + 8;   // 닉 + '#' + 태그 + 여유
constexpr uint32_t kDmPageSize        = 50;

/** 친구 관계의 상태. */
enum class EFriendState : uint8_t
{
    Friend          = 0,   // 서로 수락함
    PendingOutgoing = 1,   // 내가 보냈고 상대가 아직 안 봄
    PendingIncoming = 2,   // 상대가 보냈고 내가 수락/거절해야 함
};

/** 접속 상태. 서버가 판정한다 — 클라가 주장하지 않는다. */
enum class EPresence : uint8_t
{
    Offline = 0,
    Online  = 1,
    InGame  = 2,
};

enum class EFriendResult : uint8_t
{
    Success        = 0,
    NotAuthed      = 1,
    NotFound       = 2,   // 그런 닉네임이 없다
    AmbiguousName  = 3,   // ★ 같은 닉네임이 여럿. 4-2절 — 태그가 필요해지는 지점
    AlreadyFriend  = 4,
    AlreadyPending = 5,
    SelfRequest    = 6,   // 자기 자신에게 신청
    LimitReached   = 7,   // kMaxFriends 초과
    InvalidFormat  = 8,   // 지금은 '#' 이 들어오면 여기
    DbError        = 9,
};

#pragma pack(push, 1)

/** FriendListAckBody 뒤에 Count 개가 이어붙는다. */
struct FriendEntry
{
    uint64_t UserId;
    char     Nickname[kMaxNameLen];
    uint8_t  State;         // EFriendState
    uint8_t  Presence;      // EPresence. State != Friend 면 항상 Offline 로 채운다
    uint16_t UnreadCount;   // 안 읽은 DM 개수
};

struct FriendListAckBody { uint16_t Count; };

struct FriendAddReqBody
{
    // ★ 닉네임만이 아니라 **사용자가 친 문자열 그대로**다.
    //   파싱은 서버가 한다. 그래야 나중에 '#태그' 를 붙일 때
    //   프로토콜을 안 바꾼다 (4-2절).
    char Query[kMaxFriendQueryLen];
};

struct FriendAddAckBody
{
    uint64_t TargetUserId;   // 성공했을 때만. 실패면 0
    uint8_t  bSuccess;
    uint8_t  Result;         // EFriendResult
};

/** 누가 나에게 신청했다. 받는 즉시 목록에 PendingIncoming 으로 추가된다. */
struct FriendRequestIncomingBody
{
    uint64_t FromUserId;
    char     FromNickname[kMaxNameLen];
};

struct FriendRespondReqBody
{
    uint64_t FromUserId;
    uint8_t  bAccept;        // 0 = 거절
};

struct FriendRemoveReqBody { uint64_t TargetUserId; };

/** 친구 하나가 바뀌었다. 목록 전체를 다시 받지 않게 하는 델타다. */
struct FriendUpdateBody
{
    uint64_t UserId;
    char     Nickname[kMaxNameLen];
    uint8_t  State;          // EFriendState
    uint8_t  Presence;       // EPresence
    uint8_t  bRemoved;       // 1 이면 목록에서 지운다 (State 는 무시)
};

/** 접속 상태만 바뀌었다. FriendUpdate 보다 훨씬 자주 오므로 작게 유지한다. */
struct FriendPresenceBody
{
    uint64_t UserId;
    uint8_t  Presence;       // EPresence
};

// --- 메신저 (v7) ---
// 뒤에 TextLen 바이트의 UTF-8 본문이 이어진다.
struct DirectMessageSendBody
{
    uint64_t TargetUserId;
    uint16_t TextLen;
};

// 뒤에 TextLen 바이트의 UTF-8 본문이 이어진다.
// 보낸 사람도 자기 메시지를 이 형태로 되받는다 — 그래야 여러 기기에서
// 같은 대화가 보이고, 서버가 매긴 MessageId/Timestamp 를 클라가 알 수 있다.
struct DirectMessageBody
{
    uint64_t MessageId;      // 서버가 매긴다. 커서 페이징의 기준
    uint64_t FromUserId;
    uint64_t ToUserId;
    int64_t  Timestamp;      // Unix epoch (초)
    uint16_t TextLen;
};

struct DmHistoryReqBody
{
    uint64_t PeerUserId;
    // 0 이면 가장 최근부터. 그 외에는 "이 번호보다 오래된 것" 을 달라는 뜻이다.
    // ★ OFFSET 이 아니라 커서인 이유는 6-3절.
    uint64_t BeforeMessageId;
};

// 뒤에 Count 개의 DmEntry 가 이어붙는다. 오래된 것 → 최신 순.
struct DmHistoryAckBody
{
    uint64_t PeerUserId;
    uint16_t Count;
    uint8_t  bHasMore;       // 더 위로 스크롤할 것이 남았는가
};

/** 기록 한 줄. 뒤에 TextLen 바이트가 이어진다 (가변이라 순회로 읽는다). */
struct DmEntry
{
    uint64_t MessageId;
    uint64_t FromUserId;
    int64_t  Timestamp;
    uint16_t TextLen;
};

#pragma pack(pop)
```

### 7-3. `static_assert` 를 반드시 같이 넣는다

기존 파일의 규칙이다. **패딩이 끼면 서버와 클라의 해석이 조용히 어긋난다.**

**실제 크기 — MSVC 로 컴파일해 확인했다** (2026-08-24, `cl 14.50`, `#pragma pack(1)` 적용):

| 구조체 | 크기 | | 구조체 | 크기 |
|---|---|---|---|---|
| `FriendEntry` | 44 | | `DirectMessageSendBody` | 10 |
| `FriendListAckBody` | 2 | | `DirectMessageBody` | 34 |
| `FriendAddReqBody` | 40 | | `DmHistoryReqBody` | 16 |
| `FriendAddAckBody` | 10 | | `DmHistoryAckBody` | 11 |
| `FriendRequestIncomingBody` | 40 | | `DmEntry` | 26 |
| `FriendRespondReqBody` | 9 | | | |
| `FriendRemoveReqBody` | 8 | | 친구 목록 | **4094** / 4096 |
| `FriendUpdateBody` | 43 | | DM 한 페이지 | 1311 / 4096 |
| `FriendPresenceBody` | 9 | | | |

> **★ `kMaxFriends` 가 100 이 아니라 93 인 이유.** `FriendEntry` 는 44바이트
> (8+32+1+1+2)다. `2 + 44 × 100 = 4402` → **`kMaxBodySize`(4096)를 넘는다.**
> 증상은 "친구가 많은 계정만 목록이 안 옴" 이라 찾기 고약하다.
>
> `2 + 44 × 93 = 4094` 로 딱 맞춘다. 대안이던 "`kMaxBodySize` 를 8192 로 올린다"
> 는 **모든 패킷의 상한이 같이 올라가 악성 패킷 방어가 약해지고**, "목록을 여러
> 패킷으로 쪼갠다" 는 지금 필요 없는 복잡도다. 93 이든 100 이든 실사용 차이가 없다.
>
> **`FriendEntry` 에 필드를 추가하면 `kMaxFriends` 를 같이 줄여야 한다.** 아래
> `static_assert` 가 컴파일 타임에 잡아준다 — 그게 이 줄을 넣는 이유다.

---

## 8. DB 스키마

`Server/Friends.cpp` (신규) 가 소유한다. `Accounts` 와 같은 **동기** 방식이다.

```sql
-- 친구 관계. 한 쌍당 한 줄만 둔다.
--
-- ★ (a, b) 와 (b, a) 를 둘 다 넣지 않는 이유:
--   두 줄이 되면 수락/삭제 때 둘을 같이 고쳐야 하고, 하나만 고쳐진 순간
--   "나는 친구인데 상대는 아닌" 상태가 만들어진다. 한 줄이면 그럴 수가 없다.
--   대신 조회할 때 양쪽을 봐야 한다(아래 인덱스 2개).
CREATE TABLE IF NOT EXISTS friends(
  low_id     INTEGER NOT NULL,   -- 항상 작은 쪽 id
  high_id    INTEGER NOT NULL,   -- 항상 큰 쪽 id
  -- 0 = 수락됨(친구), 그 외에는 "신청을 보낸 사람의 id"
  -- ★ 방향을 따로 컬럼에 두지 않고 여기 담는다. 수락되면 0 으로 덮으면 끝이라
  --   "대기 중인데 방향이 없는" 모순 상태가 생길 수 없다.
  requested_by INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  PRIMARY KEY(low_id, high_id)
);
CREATE INDEX IF NOT EXISTS idx_friends_low  ON friends(low_id);
CREATE INDEX IF NOT EXISTS idx_friends_high ON friends(high_id);

-- 1:1 메시지.
CREATE TABLE IF NOT EXISTS dm_messages(
  id        INTEGER PRIMARY KEY AUTOINCREMENT,   -- 커서 페이징의 기준
  from_id   INTEGER NOT NULL,
  to_id     INTEGER NOT NULL,
  text      TEXT    NOT NULL,
  sent_at   INTEGER NOT NULL,
  read_at   INTEGER                              -- NULL = 안 읽음
);

-- ★ 두 사람 사이의 대화를 최신순으로 뽑는 것이 가장 잦은 질의다.
--   (from,to) 와 (to,from) 을 둘 다 봐야 하므로 인덱스가 2개 필요하다.
CREATE INDEX IF NOT EXISTS idx_dm_pair ON dm_messages(from_id, to_id, id DESC);
CREATE INDEX IF NOT EXISTS idx_dm_rpair ON dm_messages(to_id, from_id, id DESC);

-- 로그인 직후 "안 읽은 것 전부" 를 뽑는 질의 전용.
CREATE INDEX IF NOT EXISTS idx_dm_unread ON dm_messages(to_id) WHERE read_at IS NULL;
```

> **`low_id`/`high_id` 정규화**: 관계를 넣거나 찾기 전에 항상
> `low = min(a,b), high = max(a,b)` 로 맞춘다. 이 규칙을 한 군데(`Friends.cpp` 의
> 헬퍼)에서만 적용하고, 나머지 코드가 직접 SQL 을 짜지 않도록 한다. 규칙이
> 여러 곳에 흩어지면 한 곳에서 빠뜨렸을 때 **관계가 중복 저장되고 조용히
> 어긋난다.**

### 태그를 나중에 붙일 때

```sql
ALTER TABLE accounts ADD COLUMN tag TEXT;                      -- 기존 행은 NULL
CREATE UNIQUE INDEX idx_accounts_nick_tag ON accounts(nickname, tag);
```

기존 계정에는 그때 일괄로 랜덤 태그를 배정한다. **`friends`/`dm_messages` 는
`accounts.id` 로만 참조하므로 손댈 필요가 없다** — 닉네임을 저장하지 않은 것이
여기서 값을 한다.

---

## 9. 클래스 설계

### 9-1. 서버 (`MOU_Server/Server/`)

| 파일 | 역할 | 동기/비동기 |
|---|---|---|
| `Friends.h/.cpp` (신규) | 친구 관계 CRUD, 닉네임 조회 | **동기** (`Accounts` 와 같은 이유) |
| `DirectMessages.h/.cpp` (신규) | DM 저장/조회/읽음 처리 | **동기** (6-4절) |
| `Presence.h/.cpp` (신규) | 상태 판정 + 친구에게 전파 | 메모리만 |
| `Session.h` (수정) | `ClientSession` 에 `FriendIds` 캐시 추가 | |
| `Server.cpp` (수정) | 새 옵코드 13개 라우팅 | |

**`Presence` 가 DB 를 안 갖는 이유**: 접속 상태는 **서버가 살아있는 동안만** 의미가
있다. 서버가 재시작하면 전원 오프라인이 맞다. 저장하면 오히려 "서버는 껐는데
DB 에는 온라인으로 남은" 유령이 생긴다.

### 9-2. 클라이언트

```
UServerSubsystem  (기존, Server/)
   │  친구/DM API 를 여기에 추가한다. UI 는 여전히 이것만 안다.
   │
   ├─ ILobbyBackend (기존, Server/Lobby/)
   │     └─ FSocketLobbyBackend — ★ 새 패킷 조립도 전부 여기서만
   │
   ├─ Server/Social/FriendTypes.h    (신규) FMOUFriend, EMOUPresenceBP, EMOUFriendStateBP
   └─ Server/Chat/DirectMessageTypes.h (신규) FMOUDirectMessage
```

**`ILobbyBackend` 에 함수를 추가하는 것이지 새 백엔드를 만들지 않는다.** 친구도
"서버에 물어보는 일" 이라 기존 추상화가 그대로 맞는다. `EOSLobbyBackend` 에는
빈 구현 + `TODO` 주석을 넣어 컴파일만 되게 둔다(기존 방식 그대로).

### 9-3. 새 델리게이트

```cpp
/** 친구 목록 전체가 도착했다. 로그인 직후 한 번. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFriendListReceived, const TArray<FMOUFriend>&, Friends);

/** 친구 하나가 바뀌었다(추가/삭제/수락). 목록을 다시 받지 않는다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFriendUpdated, const FMOUFriend&, Friend, bool, bRemoved);

/** 친구의 접속 상태만 바뀌었다. 가장 자주 온다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFriendPresenceChanged, int64, UserId, EMOUPresenceBP, Presence);

/** 누가 나에게 친구 신청을 했다. 알림 배지를 띄운다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFriendRequestReceived, int64, FromUserId, const FString&, FromNickname);

/** DM 이 도착했다. 실시간이든, 로그인 시 밀린 것이든 같은 경로다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDirectMessageReceived, const FMOUDirectMessage&, Message);

/** 대화 기록이 도착했다. bPrepend 면 위로 스크롤한 결과라 앞에 붙인다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDmHistoryReceived, int64, PeerUserId, const TArray<FMOUDirectMessage>&, Messages, bool, bHasMore);
```

---

## 10. UI 설계

### 10-1. 붙는 위치

```
ULoginWidgetBase (로그인)
  └─ ULobbyWidgetBase (메인메뉴 + 대기실)      ← 기존
       ├─ URoomCreateWidgetBase                 ← 기존
       ├─ URoomListWidgetBase                   ← 기존
       └─ ★ UMessengerWidgetBase                ← 신규. 화면 우측에 상주
             ├─ UFriendListWidgetBase             친구 목록 패널
             │    └─ UFriendEntryWidget            줄 하나 (같은 헤더)
             ├─ UFriendAddWidgetBase               친구 추가 창
             └─ UDmWindowWidget                    대화창 (여러 개 열릴 수 있음)
```

**메신저는 로비 위젯의 자식이지만 상태를 공유하지 않는다.** 방에 들어가든 말든
친구 목록은 그대로 떠 있어야 한다 — 롤에서 대기열을 잡아도 친구창이 살아있는 것과
같다. 그래서 `ULobbyWidgetBase` 의 상태 전환(메인메뉴 ↔ 대기실)이 메신저를
건드리지 않게 한다.

### 10-2. 화면 (레퍼런스: 롤 클라이언트)

```
┌────────────────────────────────┬──────────────────┐
│                                │ 커뮤니티  [+][🔍]│  ← 친구 추가 / 검색
│                                ├──────────────────┤
│                                │ ▾ 신청 (1)       │  ← 받은 신청이 있을 때만
│      기존 로비 화면             │  ● kimta   [✓][✕]│
│      (방 만들기 / 참여)         │ ▾ 일반 (2/57)    │
│                                │  ● kimta         │  온라인
│                                │  ● 칼바람식k     │  게임중
│                                │  ○ averty        │  오프라인
│                                │  ○ hazzys    [2] │  안읽음 배지
│                                ├──────────────────┤
│  ┌──────────────┬────────────┐ │                  │
│  │ 대화 목록     │ 융성쟁     │ │                  │  ← 대화창
│  │ ● 융성쟁      │ ...메시지  │ │                  │
│  └──────────────┴────────────┘ │                  │
└────────────────────────────────┴──────────────────┘
```

**WBP 없이도 동작해야 한다.** 기존 위젯들과 같은 규칙 — `WidgetTree->RootWidget` 이
비어 있으면 C++ 이 기본 레이아웃을 조립하고, WBP 를 부모로 지정하면
`BindWidgetOptional` 이름만 맞추면 디자인이 바뀐다.

### 10-3. 정렬

친구 목록은 **접속 상태 → 닉네임** 순으로 정렬한다.

```
받은 신청  (있으면 항상 맨 위 — 행동이 필요한 것이 먼저다)
온라인
게임중
보낸 신청  (회색, "대기 중")
오프라인
```

> **`온라인` 이 `게임중` 보다 위인 이유**: 지금 말을 걸 수 있는 사람이 먼저 보여야
> 한다. 게임 중인 친구는 답이 늦다.

---

## 11. 인게임 채팅 (추후 구현)

**★ 아직 구현하지 않는다. 이 절은 나중에 만들 사람을 위한 계약이다.**

### 11-1. 무엇인가

게임 중 **죽은 사람끼리만** 쓰는 채팅이다. 산 사람에게는 보이지 않는다.

### 11-2. ★ 외부 서버를 쓰지 않는다

3절에서 정한 대로다. **방장의 리슨서버 안에서 UE 리플리케이션으로만 처리한다.**

```
[죽은 클라]
  입력창 → ServerSendDeadChat()   ← Server RPC (Reliable)
     ▼
╔══════ 리슨서버 (권위) ══════════════════════╗
║  · 보낸 사람이 진짜 죽었는지 서버가 확인    ║
║    (클라 주장 안 믿음 — 음성 시스템과 동일) ║
║  · 수신자 = 이 세션의 죽은 플레이어 전원    ║
╚═════════════════════════════════════════════╝
     ▼ ClientReceiveDeadChat()    ← Client RPC. 죽은 사람에게만
[죽은 클라들]
```

- **`Reliable` 이다.** 음성(`Unreliable`)과 반대다 — 채팅은 한 줄 유실되면
  대화가 안 되고, 초당 몇 줄뿐이라 대역폭 부담이 없다.
- **기록하지 않는다.** 게임이 끝나면 사라진다.
- **생사 판정은 리슨서버가 한다.** 이미 음성 시스템(`VoiceRouter`)이 같은 판정을
  하고 있으므로, **그 코드를 재사용한다** — 두 시스템이 서로 다른 근거로 생사를
  판정하면 "채팅은 되는데 음성은 안 되는" 상태가 나온다.

### 11-3. 구현 위치

```
Public/Server/Chat/InGame/
  ChatWidgetBase.h                   ★ 이미 있다 — 구 전체채팅을 옮겨둔 것
                                       화면 코드(로그/입력창/토글키)를 여기서 가져다 쓴다
  DeadChatComponent.h                (신규 예정) APlayerController 에 붙는다
  DeadChatWidgetBase.h               (신규 예정) 죽었을 때만 뜨는 입력창
```

> **★ 맨땅에서 시작하지 말 것.** `ChatWidgetBase` 가 이미 이 폴더에 있고,
> 채팅 로그 스크롤 · 최대 줄 수 · 입력창 엔터 전송 · 토글 키 바인딩 ·
> PIE 다중 창 대응이 전부 들어 있다. **화면은 그대로 쓰고 전송 경로만 갈면 된다.**
>
> 갈아야 하는 부분은 명확하다:
>   · `UServerSubsystem::OnChatMessageReceived` 구독  →  `UDeadChatComponent` 의 델리게이트
>   · `UServerSubsystem::SendChat()` 호출            →  `ServerSendDeadChat()` RPC

> **`Server/` 아래인데 `Server.exe` 를 안 쓴다 — 헷갈리지 않게 주의.**
> `Chat/` 안에 두는 것은 **"채팅" 이라는 기능으로 묶기 위해서**다. 나중에 채팅
> 관련 코드를 찾는 사람이 두 군데를 뒤지지 않게 하려는 것이고, 경로가 곧 전송
> 수단을 뜻하지는 않는다. 이 주석을 해당 헤더 상단에도 반드시 남길 것.

### 11-4. `UVoiceRouter` 와의 관계

음성 시스템은 이미 사망자 차단을 3중으로 하고 있다(`VOICE_INTEGRATION.md` 8절).
**죽은 사람 채팅은 그 반대 조건이다** — 같은 판정을 쓰되 결과를 뒤집는다.

```cpp
// 음성:  UVoiceComponent::IsPlayerVoiceDead(PC) == true  → 차단
// 채팅:  UVoiceComponent::IsPlayerVoiceDead(PC) == true  → 허용
```

**이 함수를 그대로 쓴다.** 새로 만들면 두 판정이 갈라질 수 있다.

### 11-5. ★ 신원 위조 방지는 이 구현의 책임이다

`SERVER_INTEGRATION.md` 에 있던 "팀 ID / 생사 위조 가능" 한계를 여기로 옮겼다.
`Server.exe` 쪽 `SetDeadForTest()`/`TeamId` 자유 입력은 로비 메신저와는 무관한
문제였고(메신저는 `Friends`/`DirectMessages` 로 `bDead`/`TeamId` 를 아예 안 쓴다),
`Dead` 채널이 리슨서버 RPC 로 이관되면서 그 위조 경로 자체가 없어진다 — **단,
그건 이 구현이 11-2절대로 서버 RPC 에서 생사를 직접 판정할 때만 유효하다.**

구현할 때 지킬 것:

- `ServerSendDeadChat()` 은 **클라이언트가 보낸 "나 죽었음" 주장을 절대 믿지 않는다.**
  11-4절의 `UVoiceComponent::IsPlayerVoiceDead(PC)` 로 서버가 직접 재확인한다.
- 수신자 필터링(죽은 사람 전원)도 **서버가** 계산한다. 클라이언트가 채널을 골라
  보내는 방식이 아니다.
- 이 규칙이 깨지면 `Server.exe` 시절과 같은 문제(산 사람이 사망 채팅을 엿보거나
  위조)가 그대로 재현된다 — 서버만 바뀌었을 뿐 검증 없는 자기 신고는 여전히 취약하다.

---

## 12. 구현 순서

각 단계는 다음 단계 없이도 눈으로 확인 가능해야 한다.

| 단계 | 내용 | 검증 방법 | 상태 |
|---|---|---|---|
| **M0** | **폴더 재편** (`Chat/` → `Server/`) | 빌드 통과 | **파일 이동 완료** (2026-08-24). **★ 빌드 미검증** — 아래 참고 |
| M1 | 프로토콜 v7 정의 + `static_assert` | 서버·클라 양쪽 컴파일 통과 | **✅ 완료** (2026-08-24). MSVC 로 크기 검증됨 |
| M2 | `friends` / `dm_messages` 테이블 + `Friends.cpp` + `DirectMessages.cpp` | `FriendsTest` 38개 검사 전부 통과 | **✅ 완료** (2026-08-24) |
| M3 | 친구 옵코드 라우팅 (27~34) + 세션 캐시 | `TestClient` 2개로 신청·수락이 오간다 | **✅ 완료** (2026-08-24) |
| M4 | `Presence` (35) | 한쪽을 끄면 다른 쪽 목록이 오프라인으로 바뀐다 | **✅ 완료** (2026-08-24). 4단계 전이 검증 |
| M5 | DM 송수신 (36~37) + 오프라인 보관 | 껐다 켠 쪽이 밀린 메시지를 받는다 | **✅ 완료** (2026-08-24) |
| M6 | DM 기록 조회 (38~39) + 읽음 처리 | 커서로 이전 페이지를 이어 받는다 | **✅ 완료** (2026-08-24) |
| M7 | `UServerSubsystem` API + 델리게이트 | BP 에서 노드가 보이고 로그가 찍힌다 | **✅ 완료** (2026-08-24). 빌드 통과 |
| M8 | `UFriendListWidgetBase` + 친구 추가 창 | PIE 2창으로 신청→수락이 화면에서 된다 | **✅ 완료** (2026-08-24). 빌드 통과 |
| M9 | `UDmWindowWidget` + 메신저 통합 | 대화가 오가고 다시 열면 남아 있다 | **✅ 완료** (2026-08-24). 빌드 통과 |
| M10 | `ChatWidgetBase`(구 전체채팅) 제거 판단 | 메신저가 대체했는지 확인 후 | **✅ 완료** (2026-08-24). **지우지 않고 인게임용으로 이관** |
| M11 | 실사용 피드백 반영: 패널 폭 고정, 상태 문구 자동 소멸 | PIE 에서 친구 접속해도 로비 UI 가 안 밀림 | **✅ 완료** (2026-08-25) — 아래 참고 |

### M11 상세 — 실사용 중 발견된 UI 문제 2건 (2026-08-25)

1. **커뮤니티 패널이 글자 수에 따라 커졌다 작아졌다 했다.** 친구 패널을
   `ESlateSizeRule::Automatic` 으로 붙였더니 패널 폭이 내용물(제목 "커뮤니티
   (0/0)" → "(12/93)")을 따라갔다. **`USizeBox` 로 루트를 감싸 폭을 못 박았다**
   (`UFriendListWidgetBase::PanelWidth`, 기본 280, `EditAnywhere`). 닉네임/제목은
   `ClipToBounds` 로 잘라내고 오류 문구는 `AutoWrapText` 로 줄바꿈시켜, 고정폭을
   뚫고 나가는 다른 경로도 같이 막았다.
2. **"친구 신청을 보냈습니다." 가 화면에 계속 남아 있었다.** `SetStatus` 가
   텍스트만 세팅하고 지우는 코드가 없었다. `StatusClearSeconds`(기본 3초) 뒤에
   `ClearStatus()` 를 부르는 타이머를 달았다. **새 문구가 올 때마다 이전 타이머를
   먼저 끈다** — 안 그러면 연달아 신청했을 때 먼저 건 타이머가 두 번째 문구를
   조기에 지워버린다. `NativeDestruct` 에서도 해제한다(델리게이트와 같은 이유).
| — | **인게임 채팅** | 11절. **범위 밖** | |

**M2 와 M5 가 위험 구간이다.** M2 는 `low/high` 정규화를 한 군데로 모으는 것이
지켜지는지, M5 는 "저장 먼저, 전송 나중" 순서가 지켜지는지가 걸려 있다.

> **M2 의 두 위험은 `Tests/FriendsTest.cpp` 가 지킨다.** 스키마나 SQL 을 건드릴
> 때 같이 돌릴 것 — 둘 다 **깨져도 컴파일은 통과하고 증상만 이상해지는** 종류다.
>
> ```
> cmake --build . --target FriendsTest && ./FriendsTest
> ```
>
> 구현하면서 실제로 잡힌 것 하나: `Respond()` 에 방향 검사가 없으면 **자기가
> 보낸 신청을 스스로 수락해서** 상대 동의 없이 친구가 된다. 클라가 `FromUserId`
> 를 마음대로 채워 보낼 수 있으므로 서버가 반드시 막아야 한다(테스트 4번 항목).

### ★ M0 에서 아직 확인하지 않은 것

파일 이동과 경로 갱신은 끝났지만 **빌드를 돌려보지 않았다**(작업 환경에 엔진이
없었다). 다음 사람이 가장 먼저 할 일:

1. **`.uproject` 우클릭 → Generate Visual Studio project files.**
   폴더가 통째로 바뀌었으므로 이걸 안 하면 VS 가 옛 경로를 들고 있다.
2. `TeamProject_MOUEditor Win64 Development` 빌드.
3. 깨진다면 거의 확실히 **`#include` 경로 누락**이다. 이동 시 갱신한 규칙:

   | 옛 경로 | 새 경로 |
   |---|---|
   | `Chat/ChatFraming.h` · `Chat/ServerClientRunnable.h` | `Server/Net/...` |
   | `Chat/Lobby*.h` · `Chat/*LobbyBackend.h` · `Chat/Login*.h` · `Chat/Room*.h` | `Server/Lobby/...` |
   | `Chat/ChatTypes.h` · `Chat/ChatWidgetBase.h` | `Server/Chat/...` |
   | `Chat/ServerSubsystem.h` · `Chat/ServerSettings.h` | `Server/...` |

   저장소 전체에 `"Chat/` 문자열이 남아 있지 않은 것은 확인했다.
4. **`.generated.h` 는 손대지 않았다.** UHT 가 클래스 이름으로 찾으므로 폴더가
   바뀌어도 영향이 없다. 그래도 안 되면 `Intermediate/` 를 지우고 다시 생성한다.

---

## 12-1. ★ 다음 사람이 할 일 — 로비에 메신저 붙이기

**M0~M9 는 끝났고 양쪽 다 빌드가 통과한다.** 남은 것은 위젯을 화면에 올리는 것뿐이다.

### 지금 상태

| | 상태 |
|---|---|
| 서버 (M1~M6) | ✅ 실서버 + TestClient 2개로 실행 검증 |
| 클라 코드 (M7~M9) | ✅ 빌드 통과, 경고 0, UHT 리플렉션 통과 |
| **화면에 붙이기** | ❌ **아무도 `UMessengerWidgetBase` 를 만들지 않는다** |

즉 **지금 게임을 켜도 친구창이 안 보인다.** 위젯 클래스는 있지만 생성하는 코드가 없다.

### 붙이는 방법 (둘 중 하나)

**A. 블루프린트에서** — 팀 공용 파일을 안 건드리는 쪽이라 이걸 권한다.

로그인 성공 후 로비를 띄우는 BP 에서:

```
Create Widget (Class = MessengerWidgetBase)  ->  Add to Viewport
```

**B. C++ 에서** — `ULobbyWidgetBase::NativeConstruct` 에 추가.

```cpp
// ★ 로비 상태(메인메뉴 <-> 대기실)와 무관하게 계속 떠 있어야 한다.
//   방에 들어가도 친구창은 살아있는 것이 롤과 같은 동작이다.
if (MessengerWidget == nullptr)
{
    MessengerWidget = CreateWidget<UMessengerWidgetBase>(GetOwningPlayer(), MessengerClass);
    if (MessengerWidget) { MessengerWidget->AddToViewport(); }
}
```

### 붙인 뒤 동작 확인

서버를 띄우고 PIE 2창으로:

```bash
Server.exe 7777 chat_log.db
```

1. 양쪽 로그인 → 친구 목록이 자동으로 온다(로그인 시 서브시스템이 알아서 요청)
2. A 에서 B 닉네임으로 신청 → B 화면에 **[수락][거절]** 이 뜬다
3. B 수락 → 양쪽이 "친구 / 온라인" 으로 바뀐다
4. "메시지" → 대화창, 주고받기, 창 닫고 다시 열면 **기록이 남아 있다**
5. A 종료 → B 화면의 A 가 **오프라인**으로 바뀐다

**서버 콘솔에 `[친구]` `[DM]` `[상태]` 로그가 찍히므로**, 화면이 안 바뀌면 서버까지
갔는지 그것으로 먼저 가른다. 서버 로그가 있는데 화면이 그대로면 클라 델리게이트
문제이고, 로그가 없으면 전송 문제다.

### 빌드하면서 실제로 걸렸던 것 (같은 실수 반복 방지)

| 증상 | 원인 | 대처 |
|---|---|---|
| `C4458: 'Slot' 선언이 클래스 멤버를 숨김` | `UWidget` 에 이미 `Slot` 멤버가 있는데 지역변수 이름을 `Slot` 으로 씀. **UE 는 경고를 에러로 취급한다** | 지역변수를 `BoxSlot` 등으로 |
| `C2445: 조건식의 결과 형식이 모호` | `TSubclassOf<T>` 와 `UClass*` 를 삼항 연산자로 섞음(서로 변환 가능해서 공용 타입이 안 정해진다) | `if` 로 풀어 쓰기 |
| UHT 가 헤더에서 막힘 | `UFUNCTION` 이 **참조를 반환**(`const TArray<T>&`) | BP 용(값)과 C++ 용(참조)을 따로 |

> **`DECLARE_DELEGATE_*` 는 항상 UCLASS 바깥, 파일 스코프에 둔다.** 클래스 본문
> 안에 쓰면 UHT 가 중첩 클래스 정의로 만나 파싱이 흔들린다.

## 13. 알려진 함정

| 함정 | 증상 | 대응 |
|---|---|---|
| **`FriendEntry` × 100 이 `kMaxBodySize` 초과** | 친구가 많은 계정만 목록이 안 옴 | `static_assert` 로 컴파일 타임에 잡는다. 7-3절 |
| **`low/high` 정규화 누락** | 관계가 두 줄로 저장돼 "친구인데 목록에 없음" | 헬퍼 하나만 쓰게 강제. 8절 |
| **DM 을 전송 먼저 하고 저장 나중** | 서버가 죽으면 "보냈는데 기록 없음" | 항상 INSERT 먼저. 6-1절 |
| **`read_at` 을 별도 옵코드로** | 배지가 안 사라짐 | 창 열기와 한 왕복에 묶는다. 6-2절 |
| **Presence 를 폴링** | 접속자 늘수록 트래픽 급증 | 변화 시에만 push. 5-2절 |
| **상태 변화마다 friends 테이블 조회** | 로그인/로그아웃이 느려짐 | 세션에 `FriendIds` 캐시. 5-2절 |
| **`OFFSET` 페이징** | 스크롤 중 새 메시지 오면 줄이 겹치거나 빠짐 | 커서(`BeforeMessageId`). 6-3절 |
| **`nickname` 이 UNIQUE 라고 가정** | 동명이인에게 엉뚱하게 신청됨 | `AmbiguousName` 반환. 4-2절 |
| **★ 오프라인 상대의 닉네임을 세션에서만 찾음** | 친구 목록에 **이름 없는 줄**이 생겨 재로그인까지 남는다 | `ResolveNickname()` — 세션에 없으면 DB. M3 통합 테스트에서 실제로 잡힘 |
| **인게임 채팅을 `Server.exe` 로** | 3절에서 폐기한 설계로 되돌아감 | 11절을 먼저 읽을 것 |
| **새 옵코드를 중간 번호에 삽입** | 기존 클라와 조용히 어긋남 | 항상 끝에 추가. 기존 번호 불변 |

---

## 14. 정하지 않은 것

나중에 필요해지면 그때 정한다. **지금 만들지 않는다.**

| 항목 | 왜 미루는가 |
|---|---|
| **차단(Block)** | 친구도 아직 없는데 차단부터 만들 이유가 없다. 팀 내부용이라 급하지 않다 |
| **`#태그`** | 4-2절. 닉 중복이 실제로 불편해질 때 붙인다. 그때 프로토콜은 안 바뀐다 |
| **그룹 대화** | 1:1 이 먼저다. 그룹은 테이블 구조가 달라져(`conversations`) 별도 설계가 필요하다 |
| **친구 초대로 방 참여** | 방 시스템과 엮이는 지점이 많다. 메신저가 돌아간 뒤에 본다 |
| **읽음 표시(상대가 읽었는지)** | `read_at` 은 이미 있다. 상대에게 알리는 옵코드만 추가하면 되므로 언제든 붙일 수 있다 |
| **메시지 삭제 / 신고** | 외부 서비스가 아니라 필요 없다 |
| **`ServerSubsystem` → `ServerSubsystem` 개명** | 2절. BP 참조가 끊긴다. 한 번에 훑을 수 있을 때 |
| **알림음 / 토스트** | 배지부터 되고 나서 |

---

## 부록: 지금 상태 한 장 요약

```
✅ M0  폴더 재편 (Chat/ → Server/{Net,Lobby,Chat})  27개 파일, git rename 유지
       include 25개 파일 + Build.cs + SERVER_INTEGRATION.md 갱신. 언리얼 빌드 통과 확인됨

✅ M1  프로토콜 v7 — 옵코드 13개, enum 3개, 구조체 14개, static_assert 17개
       MSVC 로 크기 실측 검증. 친구목록 4094/4096 바이트로 딱 맞음
       EChatChannel::Dead / SetDead 폐기 명시 (번호는 호환 위해 존치)

✅ M2  friends / dm_messages 테이블 + Friends.cpp + DirectMessages.cpp
       Tests/FriendsTest.cpp 38개 검사 전부 통과 (/W4 무경고)
       Server.cpp 기동/종료에 연결. 실패해도 서버는 계속 뜬다(친구만 죽음)

✅ M3  친구 옵코드 라우팅 (27~34) + ClientSession::FriendIds 캐시(뮤텍스 보호)
       Rooms::GetRoomStateOf / ComputePresence / ResolveNickname 추가
       TestClient 에 /friends /add /accept /decline /unfriend
       실서버 2클라 통합 검증: 오프라인 신청→로그인 후 수락→양쪽 목록 일치,
       접속 중 실시간 알림, 온라인 표시, 삭제 전파 전부 확인

✅ M4  Presence push (35) — BroadcastPresence / BroadcastPresenceFor
       훅 4곳: 로그인 / 접속종료 / 게임시작(RoomStartReq) / 방나감·방삭제
       검증: 온라인 → 게임중 → 온라인 → 오프라인 4단계 전이 전부 확인

✅ M5  DM 송수신 (36~37) — 저장 먼저, 전송 나중. 친구 아니면 서버가 거부
       로그인 시 밀린 것 전달(읽음 처리는 안 함 - M6 의 대화창 열기가 찍는다)
       검증: 오프라인 보관 / 재접속 수신 / 실시간 전달 / 비친구 거부 / 보낸이 에코

✅ M6  DM 기록 조회 (38~39) + 읽음 처리
       ★ 최신 페이지(BeforeMessageId==0) 요청일 때만 읽음 처리 — 위로 스크롤에서도
         찍으면 그사이 도착한 메시지까지 읽음이 되어 배지만 사라진다
       친구가 아니면 기록도 못 본다(보내기만 막으면 옛 대화를 계속 들여다볼 수 있다)
       검증: 5통 조회 / 읽음 후 배지 소멸 / 재로그인 시 재전달 없음 / 커서 페이징

✅ M7  클라이언트 API — 빌드 통과 확인됨 (2026-08-24, UE 5.8 실제 컴파일)
       Social/FriendTypes.h (FMOUFriend, FMOUDirectMessage, enum 3종)
       EServerClientEventType +7, FServerClientEvent 필드 확장
       ILobbyBackend +6 메서드 / Socket 구현 / EOS 스텁
       ServerClientRunnable 파싱 7종 (가변 길이 DmEntry 순회 포함)
       UServerSubsystem 델리게이트 7개 + API 8개 + CachedFriends
       ★ 컴파일 중 실제로 걸린 것: UFUNCTION 참조 반환 금지 → GetFriends(값)/
         GetFriendsRef(참조) 로 분리. UHT 리플렉션(enum 3·struct 2) 전부 통과 확인

✅ M8  친구 목록 위젯 — 빌드 통과, 실사용 버그 2건 수정 (2026-08-25)
       Social/FriendListWidgetBase.h/.cpp (UFriendEntryWidget + UFriendListWidgetBase)
       WBP 없이 C++ 조립 / 상태별 버튼(수락·거절·취소·삭제·메시지)
       정렬: 받은신청 > 온라인 > 게임중 > 보낸신청 > 오프라인, 안에서 닉네임순
       NativeConstruct 에서 캐시 선반영(로비 재진입 시 빈 목록 방지)
       M11: 패널 폭 SizeBox 고정 + 상태 문구 타이머 자동 소멸 (아래 M11 상세)

✅ M9  대화창 + 메신저 통합 — 빌드 통과 (2026-08-24)
       Chat/MessengerWidgetBase.h/.cpp (UDmWindowWidget + UMessengerWidgetBase)
       대화창 여러 개 (PeerUserId 로 구분, 상한 넘으면 LRU 로 닫음)
       ★ 창이 닫혀 있으면 자동으로 열지 않는다 - 배지로만 알린다
       ★ "앞에 붙이기 vs 갈아끼우기" 는 창이 bAwaitingOlder 로 스스로 안다
         (서버 응답에 그 구분이 없다 - 요청한 쪽만 아는 정보)
       친구 끊기면 대화창 자동 닫힘 (서버가 전송을 거부하므로 창만 남으면 혼란)
       ★ 컴파일 중 실제로 걸린 것: UWidget::Slot 이름 가림(C4458),
         TSubclassOf<T> 삼항연산자 모호성(C2445) → 지역변수명/if 문으로 수정

✅ M10 (구) 전체채팅 처리 — 지우지 않고 Chat/InGame/ 으로 이관 (2026-08-24)
       로비에서 자동 생성 끔(bShowChatWidgetOnSuccess 기본 false)
       화면 코드(로그 스크롤/입력창/토글키)는 죽은사람 채팅에 재활용 예정

✅ M11 실사용 피드백 UI 수정 2건 (2026-08-25) — 상세는 위 M11 절

═══ 남은 일 ═══
⬜ 로비에 UMessengerWidgetBase 붙이기 (ULobbyWidgetBase 또는 BP 에서 CreateWidget)
   — 아직 아무도 CreateWidget 하지 않아 화면에 안 보인다. 12-1절 참고
⬜ 인게임 죽은사람 채팅 실제 구현 (11절 설계만 완료)
⬜ PreLogin 방 비밀번호 재검사 (진짜 보안 관문, SERVER_INTEGRATION.md 로비-보안 항목)

📌 결정 필요 없음 — 14절 항목은 전부 나중 일
```
