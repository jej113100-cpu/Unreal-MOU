# 채팅/로그인 서버 접속 설정

> 대상: 서버를 켜지 않고 PIE 1인 실행만으로 동작을 확인하려는 팀원 전원
> 관련 코드: `Chat/ServerSettings.h`, `Config/DefaultGame.ini`

## 0. 이름 규칙

- **"ChatServer" 합성어는 쓰지 않는다.** 이 설정은 채팅 전용이 아니라 팀이 접속할
  서버 자체를 가리키므로 클래스/파일 이름은 `UMOUServerSettings`, `ServerSettings.h/.cpp`
  다. (`UMOUChatServerSettings`, `ChatServerSettings.h` 아님 — 예전 이름이니 코드에서
  보이면 리베이스가 덜 된 것이다.)
- **채팅 기능을 가리킬 때는 "Chat" 대신 "Chatting" 을 쓴다.** (신규 코드부터 적용 —
  기존 `ChatSubsystem`, `LogMOUChat`, `MOU.Chat.*` 콘솔 명령처럼 이미 자리 잡은 이름은
  이번 변경 범위 밖이라 그대로 둔다.)
- 실행 인자도 같은 규칙을 따른다: `-MOUServer=` (예전 `-MOUChatServer=` 아님).

## 1. 무엇이 문제였나

`MainLobby` 를 실행하면 로그인 화면이 뜨고, 그 화면이 채팅 서버에 접속을 시도한다.
예전에는 그 주소가 코드 여러 곳에 `127.0.0.1:9000` 으로 박혀 있었다.

`127.0.0.1` 은 특정 컴퓨터를 가리키는 주소가 **아니다.**
"이 프로그램이 지금 돌고 있는 바로 그 컴퓨터" 라는 뜻이다. 그래서 같은 코드가

- 서버를 켠 PC 에서는 → 그 PC 의 서버로 붙어서 **성공**
- 서버를 켜지 않은 PC 에서는 → **자기 자신**에게 붙으려다 **항상 실패**

로 갈렸다. 코드는 하나인데 사람마다 다른 곳을 가리켰던 것이라, 로그만 봐서는
"서버가 꺼져 있다" 는 것 말고는 원인이 드러나지 않았다.

## 2. 어떻게 고쳤나

접속 주소를 아는 곳을 **설정 파일 한 군데**로 모았다.

- 값을 정하는 곳: `TeamProject_MOU/Config/DefaultGame.ini`
  ```ini
  [/Script/TeamProject_MOU.MOUServerSettings]
  ServerHost=192.168.0.32
  ServerPort=9000
  ```
- 값을 읽는 곳: `UMOUServerSettings::ResolveEndpoint()` — 코드에서 주소가 필요한
  모든 지점(로그인 위젯, 플레이어 컨트롤러, 콘솔 명령)이 이 함수 하나만 부른다.

`DefaultGame.ini` 는 git 으로 공유되므로, **pull 만 받으면 팀원 전원이 같은 서버를
바라본다.** 각자 설정할 것이 없다.

`127.0.0.1` 대신 서버 PC 의 **LAN IP** 를 쓴다는 점이 핵심이다.
자기 LAN IP 로 자기 자신에게 접속하는 것도 정상 동작하므로, 서버를 켠 사람과
켜지 않은 사람이 **같은 설정을 그대로** 쓸 수 있다. 분기가 필요 없다.

## 3. 서버를 켜는 PC 가 할 일

1. 서버 실행 (포트는 ini 의 `ServerPort` 와 같아야 한다)
   ```bash
   MOU_Server/out/build/.../Server.exe 9000
   ```
   서버는 `INADDR_ANY` 로 bind 하므로 외부 접속을 이미 받고 있다. 서버 코드는 고칠 것이 없다.

2. **Windows 방화벽에 인바운드 규칙을 추가한다.** 이걸 빼먹으면 팀원 쪽에서
   "연결 시간 초과" 만 계속 뜬다. 관리자 PowerShell 에서 한 번만:
   ```bash
   netsh advfirewall firewall add rule name="MOU Server 9000" dir=in action=allow protocol=TCP localport=9000
   ```

3. IP 가 바뀌면 `DefaultGame.ini` 를 고쳐서 커밋한다.
   현재 IP 확인:
   ```bash
   ipconfig
   ```
   공유기가 DHCP 로 주소를 주기 때문에 재부팅 후 바뀔 수 있다. 자주 바뀐다면
   공유기 설정에서 이 PC 에 **고정 IP(DHCP 예약)** 를 걸어두는 편이 낫다.

## 4. 팀원이 할 일

`git pull` 후 에디터에서 PIE 실행. 그것뿐이다.

접속이 안 되면 PIE 콘솔(`~` 키)에서 먼저 이것부터 확인한다:

```bash
MOU.Chat.Server
```

출력 예: `채팅 서버: 192.168.0.32:9000 (설정 파일)`

- 주소가 `127.0.0.1` 로 나오면 → ini 가 반영되지 않은 것이다 (아래 5번 확인)
- 주소는 맞는데 접속이 안 되면 → 서버 PC 의 방화벽/서버 실행 여부 문제다

## 5. 주소를 바꾸는 3가지 방법

우선순위는 위가 이긴다.

| 방법 | 어떻게 | 영향 범위 |
|---|---|---|
| 실행 인자 | `-MOUServer=192.168.0.99:9000` | 이번 실행만 |
| 개인 설정 | 콘솔에서 `MOU.Chat.SetServer 192.168.0.99 9000` | 이 PC 만 (`Saved/` 는 gitignore) |
| 팀 공유 설정 | `Config/DefaultGame.ini` 수정 후 커밋<br>또는 Project Settings → Game → **MOU Server** | 팀 전원 |

개인 설정을 지우고 팀 공유 값으로 되돌리려면 인자 없이:
```bash
MOU.Chat.SetServer
```

## 6. 주의

- 코드에 주소를 다시 적지 말 것. `ULoginWidgetBase::ServerHost`,
  `ATeamProject_MOUPlayerController::ServerHostOverride` 는 **비워두는 것이 정상**이며,
  비어 있을 때만 위 설정이 적용된다. 여기에 `127.0.0.1` 을 넣으면 문제가 그대로 재발한다.
- WBP/BP 에서 `ServerHost` 를 눈으로 확인할 때 비어 있다고 놀라지 말 것. 정상이다.
- 게임 방(리슨서버) 접속 주소는 이 설정과 무관하다. 서버가 각 클라이언트의 실제 IP 를
  보고 `HostAddress` 로 내려주므로 이미 올바르게 동작한다.
- 다른 네트워크(집 ↔ 학교)에서 붙어야 한다면 사설 IP 로는 안 된다. 서버 PC 의
  공인 IP + 공유기 포트포워딩이 필요하고, 그때도 고칠 곳은 `DefaultGame.ini` 한 줄뿐이다.
