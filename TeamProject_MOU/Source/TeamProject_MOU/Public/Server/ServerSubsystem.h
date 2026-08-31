// MOU 채팅 - 채팅 시스템의 진입점.
//
// [팀원이 알아야 할 것 - 요약]
//   채팅을 쓰려면 이 서브시스템만 알면 된다. 소켓이나 스레드는 볼 필요 없다.
//   블루프린트에서:  Get Chat Subsystem -> Connect To Chat Server -> Login -> Send Chat
//   메시지 수신은 OnChatMessageReceived 델리게이트에 바인딩한다.
//
// [왜 GameInstanceSubsystem 인가]
//   - 레벨을 이동해도(리슨서버 트래블 포함) 파괴되지 않아 연결이 유지된다.
//     PlayerController 에 붙이면 트래블 때마다 재접속해야 한다.
//   - PIE 에서 클라이언트 창을 N개 띄우면 GameInstance 도 N개 생긴다.
//     즉 이 서브시스템도 N개가 되고, 채팅 연결도 자동으로 N개가 된다.
//     별도 작업 없이 다중 클라이언트 채팅 테스트가 된다.
//   - GameMode 에 붙이면 서버에만 존재해서 클라이언트가 못 쓴다.
//
// [게임 로직과의 관계]
//   이 시스템은 게임의 리슨서버 리플리케이션과 완전히 분리되어 있다.
//   Server RPC / Multicast 를 쓰지 않고, Server.exe 로 가는 별도 TCP 소켓을 쓴다.
//   따라서 호스트가 게임을 나가도 채팅은 끊기지 않는다.
//
//   현재 미해결(8단계 예정): TeamId 와 생사 여부의 권위자는 게임(리슨서버)인데
//   서버는 그 정보를 모른다. 지금은 클라이언트가 Login/SetDeadForTest 로
//   직접 알려주고 있어서 위조가 가능하다. 리슨서버가 서버에 미러링하도록 바꿔야 한다.

#pragma once

#include "CoreMinimal.h"
#include "Server/Chat/ChatTypes.h"
// 전방 선언으로 끝낼 수 없다. TUniquePtr<ILobbyBackend> 의 소멸자가
// (UHT 가 만드는 생성자에서) 완전한 타입을 요구하기 때문이다.
// LobbyBackend.h 는 UObject 나 엔진 헤더를 끌고 오지 않아 부담이 없다.
#include "Server/Lobby/LobbyBackend.h"
#include "Server/Lobby/LobbyTypes.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ServerSubsystem.generated.h"

/** 채팅 한 줄을 받았을 때. UI 는 여기 바인딩해서 로그를 채운다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChatMessageReceived, const FChatMessage&, Message);

/** 연결 상태가 바뀌었을 때. Detail 에는 실패 사유 같은 부가 설명이 들어온다(없을 수도 있다). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChatStateChanged, EChatConnectionState, NewState, const FString&, Detail);

/** 로그인이 끝나 내 신원(UserId/이름/팀)이 확정됐을 때. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChatLoginCompleted, const FChatLoginResult&, Result);

/** 계정 생성 시도가 끝났을 때. bSuccess 가 false 면 Result 에 사유가 들어있다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChatRegisterCompleted, bool, bSuccess, EChatLoginResultBP, Result);

/** 방 생성 결과. 성공하면 RoomId 가 내 방 번호다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnRoomCreated, bool, bSuccess, int32, RoomId, EMOURoomResultBP, Result);

/** 방 목록이 도착했을 때. 비어 있을 수도 있다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomListReceived, const TArray<FMOURoomInfo>&, Rooms);

/** 방 참여 시도 결과. 성공하면 대기실로 들어간 것이다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomJoinCompleted, const FMOURoomJoinResult&, Result);

/**
 * 대기실 명단이 갱신됐다. 들어오고/나가고/준비를 누를 때마다 온다.
 * bAllReady 는 서버가 판정한 값이다 — 방장의 "게임 시작" 버튼을 켤지 말지가 이것이다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnRoomMembersChanged, int32, RoomId, const TArray<FMOURoomMember>&, Members, bool, bAllReady);

/** 방이 사라졌다. 대기실을 닫고 메인메뉴로 돌아가야 한다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRoomClosed, int32, RoomId, EMOURoomCloseReasonBP, Reason);

/**
 * 게임이 시작됐다. **아직 떠날 때가 아니다.**
 *
 * 방장은 여기서 리슨서버를 열고(OpenLevel + listen), 참여자는 호스트가 다 열었다는
 * 신호(OnRoomHostReady)를 기다린다. 참여자가 이 시점에 떠나면 아직 열리지 않은
 * 주소에 붙으려다 튕긴다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRoomGameStarted, const FMOURoomJoinResult&, Host, bool, bIsHost);

/**
 * 방장의 리슨서버가 실제로 열렸다. **참여자는 여기서 떠난다.**
 *
 * Host.MakeTravelURL() 로 ClientTravel 하면 된다.
 * 방장에게는 오지 않는다 — 이 신호를 만든 것이 방장 자신이기 때문이다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomHostReady, const FMOURoomJoinResult&, Host);

// --- 친구 / 메신저 (v7) ---

/** 친구 목록 전체가 도착했다. 로그인 직후 한 번. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFriendListReceived, const TArray<FMOUFriend>&, Friends);

/** 친구 하나가 바뀌었다. bRemoved 면 목록에서 지운다. 목록 전체를 다시 받지 않는다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFriendUpdated, const FMOUFriend&, Friend, bool, bRemoved);

/** 친구의 접속 상태만 바뀌었다. 닉네임은 오지 않는다 — UserId 로 찾을 것. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFriendPresenceChanged, int64, UserId, EMOUPresenceBP, Presence);

/** 누가 나에게 친구 신청을 했다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFriendRequestReceived, int64, FromUserId, const FString&, FromNickname);

/** 친구 신청 결과. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFriendAddCompleted, bool, bSuccess, EMOUFriendResultBP, Result);

/** DM 한 통 도착. 실시간이든 로그인 시 밀린 것이든 같다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDirectMessageReceived, const FMOUDirectMessage&, Message);

/** 대화 기록 도착. 오래된 것 -> 최신 순. bHasMore 면 위로 더 있다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDmHistoryReceived, int64, PeerUserId, const TArray<FMOUDirectMessage>&, Messages, bool, bHasMore);

/** 지금 진행 중인 ClientTravel 이 직접 후보인지 relay 폴백인지 구분한다. */
enum class EMOUTravelTransport : uint8
{
	None,
	Direct,
	Relay,
};

/**
 * 서버 연결의 소유자.
 *
 * 역할:
 *   1. 백엔드(ILobbyBackend)의 생성과 파괴
 *   2. 게임 스레드 Tick 에서 백엔드의 큐를 비우고 델리게이트로 전파
 *   3. 블루프린트/UMG 가 쓸 API 제공
 *   4. 백엔드마다 달라지면 안 되는 정책 보관 (재접속 시 자동 재로그인, 내 방 번호 등)
 *
 * [패킷은 여기 없다]
 *   MOU::LoginReqBody 같은 프로토콜 구조체는 FSocketLobbyBackend 안에만 있다.
 *   이 클래스는 "무엇을 하고 싶은지" 만 말하고 어떻게 나가는지는 모른다.
 *   그래서 백엔드를 EOS 로 바꿔도 이 파일은 한 줄도 바뀌지 않는다.
 */
UCLASS()
class TEAMPROJECT_MOU_API UServerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// --- UGameInstanceSubsystem ------------------------------------------
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- 블루프린트 API ---------------------------------------------------

	/**
	 * 아무 데서나 서브시스템을 얻는 헬퍼. 블루프린트에서 노드 하나로 쓸 수 있다.
	 * WorldContextObject 는 위젯이나 액터의 self 를 넣으면 된다.
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Chat", meta = (WorldContext = "WorldContextObject"))
	static UServerSubsystem* Get(const UObject* WorldContextObject);

	/**
	 * 서버에 접속을 시작한다. 즉시 반환하고 실제 접속은 워커 스레드에서 진행된다.
	 * 접속 결과는 OnChatStateChanged 로 알려준다.
	 *
	 * 실패해도 자동으로 재시도하므로, 서버를 나중에 켜도 알아서 붙는다.
	 * 이미 연결 중이면 아무 것도 하지 않는다.
	 *
	 * [주소를 인자로 넘기지 않는 것이 기본이다]
	 *   InHost 를 비워두거나 InPort 를 0 으로 두면 UMOUServerSettings::ResolveEndpoint
	 *   가 대신 정한다 — 즉 Config/DefaultGame.ini 의 팀 공유 주소를 쓴다.
	 *   여기에 127.0.0.1 을 직접 넘기면 사람마다 "자기 PC" 를 가리키게 되어, 서버를 켜지
	 *   않은 팀원은 무조건 접속에 실패한다. (Server/ServerSettings.h 의 주석 참고)
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat")
	void ConnectToChatServer(const FString& InHost = TEXT(""), int32 InPort = 0);

	/**
	 * 계정으로 로그인한다. 성공하면 서버가 accounts.id 를 UserId 로 돌려준다.
	 *
	 * 아직 연결 전이라면 요청을 보관했다가 연결되는 순간 자동으로 보낸다.
	 * 그래서 ConnectToChatServer 직후에 바로 불러도 된다.
	 *
	 * 화면에 표시할 이름은 여기서 정하지 않는다. 계정에 저장된 닉네임을
	 * 서버가 LoginAck 로 내려주므로, 그 값(GetLoginResult().Name)을 써야 한다.
	 *
	 * [경고] Password 는 평문으로 전송된다(TLS 없음).
	 *        실제로 쓰는 비밀번호를 넣지 말 것.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat")
	void Login(const FString& LoginId, const FString& Password, int32 TeamId);

	/**
	 * 계정을 만든다. 성공해도 자동 로그인은 되지 않으므로 이어서 Login 을 불러야 한다.
	 * 결과는 OnChatRegisterCompleted 로 온다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat")
	void RegisterAccount(const FString& LoginId, const FString& Password, const FString& Nickname);

	/**
	 * 아이디/비밀번호가 서버 규칙을 만족하는지 미리 검사한다.
	 *
	 * 서버도 똑같이 검사하지만, UI 가 먼저 걸러주면 왕복 없이 즉시 알려줄 수 있다.
	 * @param OutReason 실패 시 사용자에게 보여줄 안내 문구
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Chat")
	static bool ValidateCredentials(const FString& LoginId, const FString& Password, FString& OutReason);

	/**
	 * 실패 사유를 사용자에게 보여줄 문구로 바꾼다.
	 * 로그와 UI 가 같은 문구를 쓰도록 한 곳에 모아둔다.
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Chat")
	static FString GetLoginResultText(EChatLoginResultBP Result);

	/**
	 * 채팅을 보낸다. 로그인 전에 부르면 서버가 무시하므로 여기서 미리 막는다.
	 *
	 * 텍스트가 프로토콜 상한(UTF-8 기준 512바이트, 한글 약 170자)을 넘으면
	 * 문자 경계에 맞춰 자른다. 자르지 않고 보내면 서버가 연결을 끊어버린다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat")
	void SendChat(EChatChannelBP Channel, const FString& Text);

	/**
	 * 내 생사 상태를 서버에 알린다. 사망 채널 발화 자격 판정에 쓰인다.
	 *
	 * [임시] 이름 그대로 테스트용이다. 지금은 클라이언트가 자기 상태를 마음대로
	 * 바꿀 수 있어서 살아있는 사람이 사망 채널을 엿볼 수 있다.
	 * 8단계에서 리슨서버만 보낼 수 있도록 잠글 것이므로, 게임 로직에서 이 함수를
	 * 호출하는 코드를 늘리지 말 것.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat", meta = (DevelopmentOnly))
	void SetDeadForTest(bool bDead);

	// --- 로비 -------------------------------------------------------------

	/**
	 * 방을 만든다. 내가 방장이 되고, 이어서 리슨서버를 열어야 한다.
	 *
	 * 서버는 내 IP 를 TCP 연결에서 직접 읽으므로 여기서 보내지 않는다.
	 * HostPort 는 리슨서버가 실제로 열 포트다(기본 7777).
	 *
	 * @param RoomPassword 숫자 4자리. 비우면 공개방이 된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void CreateRoom(const FString& Title, const FString& RoomPassword, int32 HostPort = 7777);

	/** 대기 중인 방 목록을 요청한다. 결과는 OnRoomListReceived 로 온다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void RequestRoomList();

	/**
	 * 방에 들어간다. 성공하면 대기실 멤버가 되고 호스트 주소를 받는다.
	 * 결과는 OnRoomJoinCompleted 로 오고, 이어서 OnRoomMembersChanged 가 온다.
	 *
	 * 여기서 바로 여행하지 않는다. 게임이 시작될 때(OnRoomGameStarted) 떠난다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void JoinRoom(int32 RoomId, const FString& RoomPassword);

	/**
	 * 지금 있는 방에서 나간다. 방장이 나가면 방이 사라지고
	 * 남은 사람들에게 OnRoomClosed 가 간다(호스트 이양은 하지 않는다).
	 *
	 * 접속을 끊어도 서버가 같은 처리를 하지만, 명시적으로 나갈 때 쓴다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void LeaveRoom();

	/**
	 * 준비 상태를 바꾼다. 참여자만 의미가 있다(방장은 늘 준비된 것으로 본다).
	 * 응답은 따로 없고 갱신된 명단이 OnRoomMembersChanged 로 온다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void SetReady(bool bReady);

	/**
	 * 게임을 시작한다. 방장만, 전원이 준비했을 때만 성공한다.
	 * 성공하면 방 멤버 전원에게 OnRoomGameStarted 가 간다.
	 *
	 * 그 다음은 두 갈래다:
	 *   방장  : 리슨서버를 연다. 다 열리면 이 서브시스템이 자동으로 감지해
	 *           로비 서버에 "준비됐다" 를 보낸다 (아래 IsWaitingForListenServer 참고).
	 *   참여자: OnRoomHostReady 가 올 때까지 기다렸다가 떠난다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void StartGame();

	/**
	 * 지금 "내 리슨서버가 열리기" 를 기다리는 중인가. 방장에게만 true 가 된다.
	 *
	 * 감시는 RoomStart 를 받는 순간 자동으로 켜지고, 리슨서버가 뜨면 꺼진다.
	 * UI 가 "서버 여는 중..." 을 띄우고 싶을 때 쓴다.
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	bool IsWaitingForListenServer() const { return bWaitingForListenServer; }

	/**
	 * 참여자가 방장을 기다리는 동안 목적지 맵을 미리 메모리에 올린다. (2026-08-26)
	 *
	 * [왜 위젯이 아니라 여기 있나]
	 *   미리 올린 패키지를 **누가 붙잡고 있느냐**가 이 최적화의 전부다.
	 *   로비 위젯이 들고 있으면, 정작 ClientTravel 이 시작될 때 월드가 헐리면서
	 *   위젯이 먼저 파괴되고 참조가 끊긴다. 그러면 GC 가 패키지를 도로 가져가
	 *   미리 올린 의미가 사라진다.
	 *
	 *   UServerSubsystem 은 GameInstance 수명이라 레벨 이동을 그대로 넘어간다.
	 *   그래서 여기가 유일하게 맞는 자리다.
	 *
	 * 같은 맵을 두 번 요청하면 두 번째는 아무 일도 하지 않는다.
	 * 실패는 오류가 아니다 — 여행은 그대로 되고 다만 느릴 뿐이다.
	 */
	void BeginPreloadMap(const FString& MapName);

	/**
	 * 미리 올려둔 패키지를 놓아준다.
	 *
	 * 여행이 끝나면 그 맵은 이제 현재 월드가 소유하므로 우리가 더 붙잡고 있을
	 * 이유가 없다. 안 놓아주면 다음 판에서 다른 맵을 골랐을 때 옛 맵이 메모리에
	 * 그대로 남는다.
	 */
	void ReleasePreloadedMap();

	/**
	 * 참여자가 이 주소로 떠난다고 알려둔다. 접속에 실패하면 이 값을 넣어
	 * "어디에 못 붙었는지" 를 그대로 말해줄 수 있다.
	 */
	void NotifyTravelingTo(const FString& HostAddress, int32 HostPort);

	/** 접속 실패 사유를 사람이 읽을 문장으로. UI 가 그대로 띄워도 되게 만든다. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMOUTravelFailed, const FString& /*Reason*/);
	FOnMOUTravelFailed OnTravelFailed;

	// ─────────────────────────────────────────────────────────────────
	// 여행 (2026-08-29: ULobbyWidgetBase 에서 여기로 옮겼다)
	//
	// [왜 옮겼나 — 위젯이 여행을 맡으면 안 되는 이유]
	//   출발 신호(RoomHostReady)를 받는 주체가 UMG 위젯이었다. 구독이
	//   NativeConstruct 에서 걸리고 NativeDestruct 에서 풀리므로, 게임이 시작될 때
	//   BP 가 로비 위젯을 닫으면 **신호를 받을 사람이 사라진다.** 그러면 참여자는
	//   영영 떠나지 않고 "이동합니다..." 상태로 굳는다.
	//
	//   게다가 신호는 캐시되지 않아서, 위젯을 다시 만들어도 복구되지 않았다.
	//
	//   방장 쪽 감시(PollListenServer)는 이미 이 서브시스템에 있다. 같은 이유였다 —
	//   "방장은 곧 OpenLevel 로 맵을 갈아타므로 위젯이 감시를 맡으면 감시자가
	//   사라진다"(아래 RoomStart 처리 주석). 참여자 쪽만 그 원칙이 안 지켜져 있었다.
	//
	// 위젯은 이제 **설정을 넘겨주고 화면을 그리는 일**만 한다.
	// ─────────────────────────────────────────────────────────────────

	/**
	 * 여행에 필요한 값을 서브시스템에 등록한다. 로비 위젯이 자기 설정을 넘긴다.
	 *
	 * 값의 주인은 여전히 WBP 다(디자이너가 거기서 고친다). 다만 **행동**은 여기서
	 * 하므로, 위젯이 죽어도 이 값들은 남아 있어야 한다.
	 *
	 * @param InHostMapName  방장이 리슨서버로 열 맵. 비면 여행하지 않는다(BP 가 맡는다)
	 * @param bInAutoTravel  참여자가 출발 신호를 받으면 자동으로 ClientTravel 할지
	 * @param bInPreloadMap  참여자가 기다리는 동안 맵을 미리 올릴지
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void ConfigureTravel(const FString& InHostMapName, bool bInAutoTravel = true, bool bInPreloadMap = false);

	/**
	 * 방 비밀번호를 보관한다. 방장이면 URL 에 실어 리슨서버를 열고,
	 * 참여자면 ClientTravel URL 에 실어 보낸다.
	 *
	 * 위젯이 들고 있으면 위젯과 함께 사라진다 — 그러면 여행 URL 에서 비밀번호가
	 * 빠지고, 호스트의 PreLogin 이 검사하는 순간 거부된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void SetRoomPassword(const FString& InRoomPassword);

	/**
	 * 참여자가 방장에게 붙는다. 보통은 출발 신호를 받을 때 자동으로 불린다.
	 *
	 * bAutoTravelOnGameStart 를 꺼두고 BP 가 연출을 넣은 뒤 직접 부를 수도 있다.
	 * 아직 출발 신호를 못 받았으면 아무 일도 하지 않고 false 를 돌려준다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	bool TravelToHost();

	/**
	 * 출발 신호를 이미 받아 두었는가. 위젯이 새로 만들어졌을 때 상태를 복원하는 데 쓴다.
	 *
	 * ★ 이것이 캐시의 존재 이유다. 예전에는 신호가 브로드캐스트되고 버려져서,
	 *   그 순간 듣고 있던 위젯이 없으면 정보가 통째로 사라졌다.
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	bool HasPendingHostReady() const { return PendingHostReady.bSuccess; }

	/**
	 * 이 PC 의 LAN IPv4. 방을 만들 때 사설 후보로 신고한다. (v8)
	 * 못 찾으면 빈 문자열 — 그러면 공인 후보만 남고 v7 과 같이 동작한다.
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	static FString GetLocalLanAddress();

	/**
	 * 후보들 중 **내 네트워크에 맞는 것**을 고른다. (v8)
	 *
	 * [규칙]
	 *   1) Lan 후보가 있고 그 주소가 내 어댑터 중 하나와 같은 /24 안이면 그것
	 *   2) 아니면 Public 후보
	 *   3) 둘 다 없으면 남은 아무 것
	 *
	 * [왜 /24 인가]
	 *   넷마스크를 정확히 알아내는 경로가 플랫폼마다 다르고, 틀렸을 때의 손해가
	 *   이득보다 크다. /24 는 가정용·사무실 공유기의 절대다수를 덮고,
	 *   빗나가도 접속 실패 시 다음 후보로 넘어가므로 최악이 "예전과 같음" 이다.
	 *
	 * @param OutIndex 고른 후보의 인덱스. 실패 시 INDEX_NONE
	 * @return 고른 후보. 유효하지 않으면 후보가 하나도 쓸만하지 않다는 뜻이다
	 */
	static FMOUHostCandidate ChooseHostCandidate(const TArray<FMOUHostCandidate>& Candidates, bool bHostLanOnly, int32& OutIndex);

	/**
	 * 마지막에 쓴 후보가 실패했을 때 다음 후보로 다시 붙어본다. (v8)
	 *
	 * ★ 이것이 후보 선택을 안전하게 만든다.
	 *   /24 비교는 넷마스크를 모르고 하는 추측이라 빗나갈 수 있다. 폴백이 있으면
	 *   빗나갔을 때 최악이 "예전과 같음(공인으로 시도)" 이고, 없으면 "예전보다 나쁨" 이다.
	 *
	 * @return 다시 시도했으면 true. 남은 후보가 없으면 false — 그때가 진짜 실패다
	 */
	bool TryNextHostCandidate();

	// ─────────────────────────────────────────────────────────────────
	// 도달성 프로브 (v9)
	//
	// [왜 필요한가 — 실측으로 확인한 것]
	//   UPnP 가 "매핑 성공" 이라고 답해도 실제로 패킷이 들어온다는 보장이 없다.
	//   한 공유기에서 매핑 Enabled=1, 리슨서버 바인드 정상, 방화벽 Allow 였는데도
	//   외부에서 보낸 패킷이 **하나도** 도착하지 않았다. 규칙을 기록만 하고
	//   NAT 테이블에 반영하지 않는 펌웨어이거나 ISP 가 인바운드 UDP 를 거른 것이다.
	//
	//   그 상태로 게임을 시작하면 참여자 전원이 죽은 주소로 달려가 1분 가까이
	//   화면이 멈춘 뒤에야 실패한다. "열렸다" 를 믿지 말고 한 발 받아봐야 한다.
	// ─────────────────────────────────────────────────────────────────

	/**
	 * 외부에서 이 PC 의 게임 포트로 들어올 수 있는지 실제로 확인한다.
	 *
	 * 서버에게 "내 공인주소:이 포트로 UDP 한 발 쏴달라" 고 청하고, 그 패킷이
	 * 도착하는지 본다. 결과는 OnReachabilityChecked 로 오고 서버에도 신고된다.
	 *
	 * ★ 리슨서버가 그 포트를 잡기 **전에** 불러야 한다. 방을 만드는 시점이
	 *   적기다 — 그때는 로비 맵이라 넷드라이버가 없어서 포트가 비어 있다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void BeginReachabilityProbe(int32 Port = 7777);

	/** 프로브 결과. bReachable 이 거짓이면 이 방은 같은 LAN 전용이다. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMOUReachabilityChecked, bool, bReachable, const FString&, Detail);
	UPROPERTY(BlueprintAssignable, Category = "MOU|Lobby")
	FOnMOUReachabilityChecked OnReachabilityChecked;

	/** 프로브가 진행 중인가. 방 만들기 버튼이 이 값을 보고 기다린다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	bool IsProbingReachability() const { return bProbing; }

	// ─────────────────────────────────────────────────────────────────
	// UDP 홀펀칭 (v10)
	//
	// [왜 필요한가 — 실측으로 갈라낸 것]
	//   참여자 쪽 공유기는 **port-restricted cone** 이다:
	//     · 정적 포워딩(UPnP)으로 들어오는 미요청 인바운드  -> 차단
	//     · 자기가 먼저 쏜 상대의 **같은 포트**에서 오는 것  -> 통과
	//     · 같은 상대라도 **다른 포트**에서 오면            -> 차단 (10발 중 0발)
	//
	//   그래서 방장이 참여자의 정확한 공인 IP:포트로 미리 한 발 쏘면 그 구멍으로
	//   접속이 들어온다. 릴레이가 필요 없다.
	//
	// [세 조각]
	//   ① 게임 포트 확보    참여자도 예측 가능한 포트를 쓴다 (UMOUIpNetDriver)
	//   ② 엔드포인트 등록   그 포트에서 서버로 한 발 -> 서버가 출발지를 관측
	//   ③ 방장의 punch      RoomStart 로 받은 대상에게 OpenLevel 직전에 쏜다
	// ─────────────────────────────────────────────────────────────────

	/**
	 * 게임 포트를 확보하고 서버에 등록한다. 대기실에 들어갈 때 부른다.
	 *
	 * 확보한 포트는 UMOUIpNetDriver 에 넘겨져 ClientTravel 때 그대로 쓰인다.
	 * 그래야 서버가 관측한 엔드포인트와 실제 접속 출발지가 같아진다.
	 *
	 * ★ BasePort 가 사용 중이면 다음 번호로 올라간다.
	 *   한 PC 에서 인스턴스를 둘 띄우는 테스트가 이 폴백 없이는 깨진다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void RegisterGameEndpoint(int32 BasePort = 7777);

	/** 서버가 관측해 알려준 내 공인 엔드포인트. 등록 전이면 비어 있다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	FString GetObservedEndpoint() const { return ObservedEndpoint; }

	/**
	 * 지금 쓰고 있는 백엔드 이름. "자체 서버(TCP)" / "EOS".
	 * 디버그 화면에 띄워두면 "어디에 붙어 있는지" 를 묻지 않아도 된다.
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	FString GetBackendName() const;

	/**
	 * 방 진행 상태를 서버에 알린다. 방장만 의미가 있다.
	 *
	 * [v5] 인원수는 서버가 직접 세므로 CurrentPlayers 는 무시된다.
	 *      게임 시작도 StartGame() 이 대신하므로, 이 함수를 새로 쓸 일은 거의 없다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void UpdateRoomState(int32 RoomId, int32 CurrentPlayers, bool bInGame);

	/** 방 비밀번호가 숫자 4자리 규칙에 맞는지. UI 가 미리 걸러줄 때 쓴다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	static bool IsValidRoomPassword(const FString& RoomPassword);

	/** 방 관련 실패 사유를 사용자에게 보여줄 문구로 바꾼다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	static FString GetRoomResultText(EMOURoomResultBP Result);

	/** 내가 방장인 방 번호. 0 이면 방장이 아니다 (참여자로 들어가 있을 수는 있다). */
	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	int32 GetMyRoomId() const { return MyRoomId; }

	/** 지금 들어가 있는 방 번호. 방장이든 참여자든 상관없다. 0 이면 어느 방에도 없다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	int32 GetCurrentRoomId() const { return CurrentRoomId; }

	/** 내가 지금 방의 방장인지. */
	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	bool IsRoomHost() const { return CurrentRoomId != 0 && CurrentRoomId == MyRoomId; }

	/** 마지막으로 받은 대기실 명단. */
	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	TArray<FMOURoomMember> GetRoomMembers() const { return RoomMembers; }

	/** 참여자 전원이 준비했는지. 서버가 판정해 내려준 값이다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	bool AreAllMembersReady() const { return bAllMembersReady; }

	/** 내 준비 상태. 명단에서 나를 찾아 읽는다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	bool IsSelfReady() const;

	/** 연결을 끊고 워커 스레드를 정리한다. 재접속하려면 ConnectToChatServer 를 다시 부른다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat")
	void Disconnect();

	UFUNCTION(BlueprintPure, Category = "MOU|Chat")
	EChatConnectionState GetConnectionState() const { return ConnectionState; }

	/** 서버가 확정한 내 신원. 로그인 전에는 bSuccess 가 false 다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Chat")
	FChatLoginResult GetLoginResult() const { return LoginResult; }

	// --- 델리게이트 -------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "MOU|Chat")
	FOnChatMessageReceived OnChatMessageReceived;

	UPROPERTY(BlueprintAssignable, Category = "MOU|Chat")
	FOnChatStateChanged OnChatStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "MOU|Chat")
	FOnChatLoginCompleted OnChatLoginCompleted;

	UPROPERTY(BlueprintAssignable, Category = "MOU|Chat")
	FOnChatRegisterCompleted OnChatRegisterCompleted;

	UPROPERTY(BlueprintAssignable, Category = "MOU|Lobby")
	FOnRoomCreated OnRoomCreated;

	UPROPERTY(BlueprintAssignable, Category = "MOU|Lobby")
	FOnRoomListReceived OnRoomListReceived;

	UPROPERTY(BlueprintAssignable, Category = "MOU|Lobby")
	FOnRoomJoinCompleted OnRoomJoinCompleted;

	UPROPERTY(BlueprintAssignable, Category = "MOU|Lobby")
	FOnRoomMembersChanged OnRoomMembersChanged;

	UPROPERTY(BlueprintAssignable, Category = "MOU|Lobby")
	FOnRoomClosed OnRoomClosed;

	UPROPERTY(BlueprintAssignable, Category = "MOU|Lobby")
	FOnRoomGameStarted OnRoomGameStarted;

	UPROPERTY(BlueprintAssignable, Category = "MOU|Lobby")
	FOnRoomHostReady OnRoomHostReady;

	// --- 친구 / 메신저 (v7) ----------------------------------------------

	/** 친구 목록 전체가 도착했다. 로그인 직후 한 번. 패널을 통째로 다시 그린다. */
	UPROPERTY(BlueprintAssignable, Category = "MOU|Friend")
	FOnFriendListReceived OnFriendListReceived;

	/**
	 * 친구 하나가 바뀌었다(추가/수락/삭제). **목록을 다시 요청하지 않는다.**
	 * bRemoved 면 그 줄을 지우고, 아니면 있으면 갱신 없으면 추가한다.
	 */
	UPROPERTY(BlueprintAssignable, Category = "MOU|Friend")
	FOnFriendUpdated OnFriendUpdated;

	/**
	 * 친구의 접속 상태만 바뀌었다. 가장 자주 온다.
	 * ★ 닉네임이 오지 않으므로 UserId 로 찾아 상태만 갈아끼워야 한다.
	 */
	UPROPERTY(BlueprintAssignable, Category = "MOU|Friend")
	FOnFriendPresenceChanged OnFriendPresenceChanged;

	/** 누가 나에게 신청했다. 알림 배지를 띄운다. */
	UPROPERTY(BlueprintAssignable, Category = "MOU|Friend")
	FOnFriendRequestReceived OnFriendRequestReceived;

	/** 친구 신청 결과. 실패하면 Result 에 사유가 들어있다. */
	UPROPERTY(BlueprintAssignable, Category = "MOU|Friend")
	FOnFriendAddCompleted OnFriendAddCompleted;

	/** DM 이 도착했다. 실시간이든 로그인 시 밀린 것이든 같은 경로다. */
	UPROPERTY(BlueprintAssignable, Category = "MOU|Messenger")
	FOnDirectMessageReceived OnDirectMessageReceived;

	/** 대화 기록이 도착했다. bHasMore 면 위로 더 받을 것이 있다. */
	UPROPERTY(BlueprintAssignable, Category = "MOU|Messenger")
	FOnDmHistoryReceived OnDmHistoryReceived;

	// --- 친구 / 메신저 API ------------------------------------------------

	/**
	 * 친구 목록을 요청한다.
	 *
	 * ★ 보통은 직접 부를 일이 없다 — 로그인이 끝나면 자동으로 한 번 요청한다.
	 *   그 뒤로는 델타(OnFriendUpdated / OnFriendPresenceChanged)만 온다.
	 *   목록이 어긋났다고 의심될 때의 복구 수단으로 열어둔다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Friend")
	void RequestFriendList();

	/**
	 * 닉네임으로 친구를 신청한다.
	 *
	 * 결과는 OnFriendAddCompleted 로 온다. 동명이인이면 AmbiguousName 이 오는데,
	 * 지금은 그 사람을 특정할 방법이 없다(#태그가 들어오면 해결된다).
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Friend")
	void AddFriend(const FString& Query);

	/** 받은 신청을 수락한다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Friend")
	void AcceptFriendRequest(int64 FromUserId);

	/** 받은 신청을 거절한다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Friend")
	void DeclineFriendRequest(int64 FromUserId);

	/** 친구를 끊거나, 내가 보낸 신청을 취소한다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Friend")
	void RemoveFriend(int64 TargetUserId);

	/** 1:1 메시지를 보낸다. 친구가 아니면 서버가 버린다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Messenger")
	void SendDirectMessage(int64 TargetUserId, const FString& Text);

	/**
	 * 대화창을 연다 — 최근 기록을 받고 **안 읽음이 사라진다.**
	 *
	 * 읽음 처리가 여기 묶여 있는 이유: 창을 여는 것이 곧 읽는 것이다.
	 * 별도 함수로 두면 창은 열었는데 읽음 신호를 놓쳐 배지가 안 사라진다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Messenger")
	void OpenConversation(int64 PeerUserId);

	/**
	 * 위로 스크롤해서 이전 기록을 더 받는다. **읽음 처리를 하지 않는다.**
	 *
	 * @param OldestMessageId  지금 화면에 있는 가장 오래된 메시지의 MessageId.
	 *   0 을 넣으면 OpenConversation 과 같아져 읽음 처리까지 일어나므로 주의.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Messenger")
	void LoadOlderMessages(int64 PeerUserId, int64 OldestMessageId);

	/**
	 * 마지막으로 받은 친구 목록의 사본.
	 *
	 * 위젯이 늦게 만들어져 OnFriendListReceived 를 놓쳤을 때 여기서 채운다 —
	 * 이게 없으면 로비를 다시 열 때마다 목록이 빈 채로 시작한다.
	 */
	//
	// ★★ 두 개로 나눈 이유: **UHT 는 UFUNCTION 의 참조 반환을 거부한다.**
	//   const TArray<T>& 를 BlueprintPure 로 노출하면 헤더 파싱 단계에서 막힌다.
	//   그렇다고 값 반환만 두면 C++ 쪽이 매번 93개를 복사하게 되므로,
	//   블루프린트용(값)과 C++ 용(참조)을 따로 둔다.
	UFUNCTION(BlueprintPure, Category = "MOU|Friend")
	TArray<FMOUFriend> GetFriends() const { return CachedFriends; }

	/** C++ 전용. 복사 없이 읽는다. 위젯은 이쪽을 쓴다. */
	const TArray<FMOUFriend>& GetFriendsRef() const { return CachedFriends; }

	/** 안 읽은 DM 총 개수. 메신저 버튼 위 배지에 쓴다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Messenger")
	int32 GetTotalUnreadCount() const;

private:
	/** 게임 스레드 틱. 백엔드 큐를 비우고 델리게이트를 브로드캐스트한다. */
	bool Tick(float DeltaTime);

	/** 백엔드를 정리하고 버린다. 워커 스레드가 있으면 끝날 때까지 기다린다. */
	void ShutdownClient();

	/**
	 * 방장이 연 리슨서버가 실제로 떴는지 매 틱 확인하고, 뜨는 순간 백엔드에 알린다.
	 *
	 * [왜 위젯이 아니라 여기인가]
	 *   방장은 OpenLevel 로 맵을 갈아탄다. 그 순간 로비 위젯은 파괴되므로
	 *   위젯에 타이머를 걸어두면 신호를 보낼 주체가 사라진다.
	 *   이 서브시스템은 GameInstance 소속이라 레벨 이동을 넘어 살아남는다.
	 *
	 * [왜 "다 열렸다" 를 폴링으로 아는가]
	 *   OpenLevel 은 완료 콜백이 없고, 리슨서버가 접속을 받기 시작하는 정확한 시점은
	 *   넷드라이버가 생겼는지로만 알 수 있다. 매 틱 포인터 한 번 확인하는 비용이라
	 *   폴링이라고 부르기도 민망한 수준이다.
	 */
	void PollListenServer(float DeltaTime);

	/** 지금 이 프로세스가 리슨서버로 돌고 있는가. */
	bool IsListenServerUp() const;

	/**
	 * 참여자가 출발 신호를 못 받은 채 너무 오래 기다리지 않는지 본다. (2026-08-29)
	 *
	 * 방장 쪽 PollListenServer 와 짝이다. 그쪽은 "내 서버가 떴는가" 를 보고,
	 * 이쪽은 "방장이 끝내 못 열었는가" 를 본다. 둘 다 매 틱 돈다.
	 */
	void PollGuestHostReadyTimeout(float DeltaTime);

	/** 직접/relay 접속이 엔진의 긴 타임아웃에 갇히지 않도록 별도 시간 제한을 적용한다. */
	void PollTravelConnection(float DeltaTime);

	/** PendingNetGame 또는 현재 World의 서버 연결이 실제로 열린 상태인가. */
	bool IsTravelConnectionOpen() const;

	/** 방장이 리슨서버를 연다. HostMapName 이 비어 있으면 아무것도 하지 않는다. */
	void TravelAsHost();

	/**
	 * 리슨서버가 뜬 직후, 외부 참여자가 들어오려면 무엇이 더 필요한지 로그에 남긴다.
	 * 값(이 PC 의 LAN IP, 실제 리슨 포트)까지 채워서 그대로 공유기에 옮겨 적을 수 있게 한다.
	 */
	void LogListenServerReachability() const;

	/**
	 * 참여자가 방장에게 붙다가 실패했을 때 **왜** 실패했는지 알린다. (2026-08-28)
	 *
	 * [왜 필요한가 — 침묵이 가장 비쌌다]
	 *   여행이 실패해도 화면에는 "호스트 ...:7777 로 이동합니다..." 만 그대로
	 *   남아 있었다. 접속을 시도 중인지, 이미 실패했는지, 주소가 틀린 건지
	 *   포트가 안 열린 건지 구분할 방법이 전혀 없었다.
	 *   실제로 이 침묵 때문에 며칠을 서버 코드에서 원인을 찾았는데,
	 *   서버는 처음부터 정상이었고 막힌 곳은 방장 쪽 공유기였다.
	 *
	 *   엔진은 이미 실패를 알고 있다(ENetworkFailure). 그걸 받아 사람이 읽을
	 *   수 있는 문장으로 바꿔주기만 하면 된다.
	 */
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver,
	                          ENetworkFailure::Type FailureType, const FString& ErrorString);

	/** 레벨 이동 자체가 실패한 경우(맵을 못 찾는 등). 위와 같은 이유로 필요하다. */
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	/** 참여자가 방금 어디로 떠났는지. 실패 메시지에 주소를 같이 적으려고 들고 있다. */
	FString PendingTravelAddress;

	/** 구독 해제용. 안 떼면 서브시스템이 죽은 뒤에도 엔진이 호출한다. */
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;

	void SetConnectionState(EChatConnectionState NewState, const FString& Detail = FString());

	/** 보관해둔 로그인 요청을 실제로 전송한다. */
	void SendPendingLogin();

	/**
	 * 보관해둔 계정 생성 요청을 전송한다.
	 *
	 * 로그인과 마찬가지로 보관이 필요한 이유:
	 * 워커는 연결이 성사되는 순간 송신 큐를 통째로 비운다(끊기기 전에 쌓인
	 * 낡은 패킷이 LoginReq 보다 먼저 나가는 것을 막기 위해서다).
	 * 그래서 연결 전에 EnqueuePacket 한 RegisterReq 는 그대로 버려진다.
	 * 서브시스템이 들고 있다가 Connected 시점에 다시 보내야 한다.
	 */
	void SendPendingRegister();

	UPROPERTY()
	EChatConnectionState ConnectionState = EChatConnectionState::Disconnected;

	UPROPERTY()
	FChatLoginResult LoginResult;

	/**
	 * 마지막으로 받은 친구 목록 (v7).
	 *
	 * ★ 왜 캐시하는가: 위젯은 로비를 열고 닫을 때마다 새로 만들어지는데,
	 *   OnFriendListReceived 는 로그인 직후 한 번만 온다. 캐시가 없으면
	 *   **로비를 다시 열 때마다 친구 목록이 빈 채로 시작한다.**
	 *
	 * 델타(FriendUpdate / FriendPresence)가 올 때 여기도 같이 갱신하므로
	 * 항상 화면과 같은 내용이다.
	 */
	UPROPERTY()
	TArray<FMOUFriend> CachedFriends;

	/** 캐시에서 UserId 로 찾는다. 없으면 INDEX_NONE. */
	int32 FindCachedFriendIndex(int64 UserId) const;

	/**
	 * 계정/세션 탐색 백엔드. 종류는 설정이 정한다(UMOUServerSettings::LobbyBackend).
	 *
	 * [이 포인터가 이 클래스에서 유일한 "바깥으로 나가는 문" 이다]
	 *   위젯도 게임 로직도 백엔드를 직접 만지지 않는다. 그래서 EOS 로 갈아끼울 때
	 *   바뀌는 것은 여기에 무엇이 담기느냐 하나뿐이다.
	 *
	 * ConnectToChatServer 에서 만들고 ShutdownClient 에서 버린다.
	 * TUniquePtr 이므로 소멸자에서 자동으로 정리되지만, 워커 스레드를 안전하게
	 * 세우는 것은 백엔드의 Shutdown() 이 책임진다.
	 */
	TUniquePtr<ILobbyBackend> Backend;

	/**
	 * 방장이 리슨서버를 다 열기를 기다리는 중인가. RoomStart 를 받을 때 켜진다.
	 *
	 * v5 까지는 이 감시가 없었고, 참여자가 고정 3초를 센 뒤 떠났다.
	 * 그 3초는 근거 없는 값이라 느린 PC 에서는 모자라고 빠른 PC 에서는 낭비였다.
	 */
	bool  bWaitingForListenServer = false;

	/** 기다린 시간. UMOUServerSettings::HostReadyTimeoutSeconds 를 넘으면 포기한다. */
	float ListenServerWaitSeconds = 0.f;

	// ─── 여행 상태 (2026-08-29) ──────────────────────────────────────
	// 전부 위젯이 아니라 여기 있다. 위젯은 레벨과 함께 죽는다.

	/** 로비 위젯이 ConfigureTravel 로 넘겨준 값. */
	FString HostMapName;
	bool    bAutoTravelOnGameStart = true;
	bool    bPreloadMapWhileWaiting = false;

	/**
	 * 방장이면 내가 정한 값, 참여자면 입장할 때 쓴 값. 여행 URL 에 실린다.
	 *
	 * 이름에 Travel 을 붙인 이유: CreateRoom / JoinRoom 의 매개변수 이름이
	 * RoomPassword 라, 그냥 RoomPassword 로 두면 그 안에서 멤버를 가린다(C4458).
	 * 쓰임도 다르다 — 저쪽은 로비 서버에 보내는 값이고 이것은 여행 URL 에 싣는 값이다.
	 */
	FString TravelRoomPassword;

	/**
	 * 받아둔 출발 신호. bSuccess 가 참이면 "지금 떠나도 된다" 는 뜻이다.
	 *
	 * 브로드캐스트하고 버리지 않고 여기 남긴다 — 그래야 그 순간 듣고 있던 위젯이
	 * 없었어도, 나중에 만들어진 위젯이 HasPendingHostReady 로 상태를 복원할 수 있다.
	 */
	FMOURoomJoinResult PendingHostReady;

	/** PendingHostReady.Candidates 중 마지막으로 시도한 인덱스. 폴백 탐색 시작점이다. */
	int32 TriedCandidateIndex = INDEX_NONE;

	/** 이미 시도한 후보. 전부 실패한 뒤 첫 후보로 영원히 순환하는 것을 막는다. */
	TSet<int32> TriedCandidateIndices;

	/**
	 * 참여자가 RoomStart 를 받고 나서 흐른 시간. 출발 신호를 기다리는 중에만 센다.
	 *
	 * [왜 필요한가]
	 *   방장의 리슨서버가 안 열리면 서버는 출발 신호를 **보내지 않는다**(죽은 주소로
	 *   보내지 않으려고 일부러 그런다). 그런데 그러면 참여자에게는 아무것도 안 오고,
	 *   화면은 "방장이 서버를 여는 중입니다..." 로 영원히 굳는다.
	 *   기다림에는 끝이 있어야 한다.
	 */
	bool  bGuestWaitingForHostReady = false;
	float GuestWaitSeconds = 0.f;

	// ─── 도달성 프로브 상태 (v9) ─────────────────────────────────────
	bool   bProbing = false;
	uint32 ProbeNonce = 0;
	int32  ProbePort = 0;
	float  ProbeWaitSeconds = 0.f;
	/** 서버가 "쐈다" 고 답했는가. 그때부터 시간을 센다. */
	bool   bProbeDispatched = false;
	/** HostProbeSent 를 기다리는 시간까지 포함한 전체 상한. */
	float  ProbeTotalSeconds = 0.f;
	/** UPnP 규칙 반영 지연과 UDP 유실에 대비한 재요청 간격/횟수. */
	float  ProbeRetrySeconds = 0.f;
	int32  ProbeRequestAttempts = 0;
	/**
	 * 게임 포트에 bind 된 UDP 소켓. 셋이 같이 쓴다. (v10 에서 프로브 전용 -> 공용)
	 *
	 *   · 도달성 프로브 : 서버가 쏜 확인 패킷을 받는다
	 *   · 엔드포인트 등록: 여기서 서버로 쏴야 서버가 **이 포트**를 관측한다
	 *   · 홀펀칭       : 여기서 쏴야 NAT 구멍이 **이 포트**에 뚫린다
	 *
	 * ★ 셋 다 같은 소켓이어야 하는 이유가 같다. 다른 소켓을 쓰면 검증·등록·개방이
	 *   전부 엉뚱한 포트에 일어나고, 정작 게임이 쓸 포트는 아무것도 안 된 채 남는다.
	 *
	 * ★ 여행 직전에 반드시 닫는다. 열어둔 채 OpenLevel/ClientTravel 하면
	 *   넷드라이버가 같은 포트를 잡지 못한다.
	 */
	class FSocket* GameSocket = nullptr;

	/** 게임 포트를 확보한다. 이미 있으면 아무 일도 하지 않는다. 실패하면 false. */
	bool EnsureGameSocket(int32 BasePort);

	/** 게임 소켓을 닫는다. 여행 직전과 종료 시에 부른다. */
	void CloseGameSocket();

	/**
	 * 마지막 프로브 결과. 방이 생긴 뒤 다시 신고하는 데 쓴다. (2026-08-29)
	 *
	 * [왜 보관해야 하는가 — 실제로 난 버그]
	 *   프로브는 **방 만들기 창이 열릴 때** 시작한다(그때 게임 포트가 비어 있어서다).
	 *   그런데 결과가 나오는 시점은 사용자가 제목을 다 치고 "만들기" 를 누르기
	 *   전일 수도, 후일 수도 있다. 앞이면 서버에 방이 아직 없어서 신고가
	 *   NotInRoom 으로 거부되고, 그 방은 도달성 표시 없이 남는다.
	 *
	 *   즉 **입력 속도에 따라 갈리는 레이스**였다. 빨리 치면 되고 천천히 치면 안 됐다.
	 *   결과를 들고 있다가 RoomCreateAck 에서 한 번 더 보내면 순서와 무관해진다.
	 */
	bool bHasReachabilityResult = false;
	bool bLastReachable = false;

	// ─── 홀펀칭 상태 (v10) ──────────────────────────────────────────
	/**
	 * 확보한 게임 포트. 0 이면 확보하지 못했다(= 홀펀칭 없이 예전처럼 동작).
	 *
	 * ★ 프로브 소켓과 **같은 포트**여야 한다. 다른 포트로 확인하거나 punch 하면
	 *   정작 리슨서버/클라이언트가 쓸 포트는 검증도 개방도 안 된 채 남는다.
	 */
	int32 ReservedGamePort = 0;

	/** 서버가 관측해 알려준 내 공인 엔드포인트. 진단용 표시에 쓴다. */
	FString ObservedEndpoint;

	/** 방장이 punch 할 대상. RoomStart 로 받는다. */
	TArray<FMOUHostCandidate> PunchTargets;

	/**
	 * 대상들에게 더미 UDP 를 몇 발 쏜다. OpenLevel 직전에 부른다.
	 *
	 * ★ 프로브 소켓(= 게임 포트에 bind 된 소켓)으로 쏴야 한다.
	 *   다른 소켓으로 쏘면 NAT 에 뚫리는 구멍이 그 소켓의 포트에 생기고,
	 *   정작 리슨서버가 쓸 포트는 그대로 막혀 있다.
	 */
	void PunchTowardPeers();

	/** 기존 GameSocket으로 relay capability를 미리 등록해 NAT 매핑을 예열한다. */
	void RegisterRelayRouteFromGameSocket(const FMOUGameRelayRoute& Route, bool bHost);

	/** 실제 UE client socket 등록을 예약하고 guest-facing relay 포트로 떠난다. */
	bool TryRelayFallback();

	/** 프로브를 끝내고 결과를 알린다. 소켓을 닫는 유일한 경로다. */
	void FinishReachabilityProbe(bool bReachable, const FString& Detail);

	/** 매 틱 프로브 소켓을 들여다본다. 논블로킹이라 비용이 사실상 없다. */
	void PollReachabilityProbe(float DeltaTime);

	/**
	 * 미리 올린 맵 패키지. UPROPERTY 참조가 GC 를 막는다.
	 * AddToRoot 를 쓰지 않는 이유는 해제를 빠뜨렸을 때 맵이 영원히 남기 때문이다.
	 */
	UPROPERTY()
	TObjectPtr<UPackage> PreloadedMapPackage;

	/** 지금 미리 올리는 중이거나 이미 올려둔 맵. 중복 요청을 막는다. */
	FString PreloadedMapName;

	FTSTicker::FDelegateHandle TickHandle;

	// 연결 전에 Login() 이 호출된 경우 여기 보관했다가 Connected 시점에 보낸다.
	//
	// [주의] PendingPassword 는 재접속 시 다시 로그인하려고 메모리에 남겨둔다.
	//        Disconnect() 하면 지운다. 디스크나 로그에는 절대 쓰지 않는다.
	bool    bHasPendingLogin = false;
	FString PendingLoginId;
	FString PendingPassword;
	int32   PendingTeamId = -1;

	// 계정 생성 요청. 로그인과 달리 한 번만 보낸다(RegisterAck 를 받으면 지운다).
	// 재접속할 때마다 가입을 다시 시도하면 "이미 있는 아이디" 오류가 반복된다.
	bool    bHasPendingRegister = false;
	FString PendingRegisterId;
	FString PendingRegisterPassword;
	FString PendingRegisterNickname;

	/** 방을 떠났을 때 대기실 관련 상태를 한 번에 비운다. */
	void ClearRoomState();

	/** RoomStart 에서 받은 방장 전용 host-facing relay 경로들. */
	TArray<FMOUGameRelayRoute> PendingHostRelayRoutes;

	/** RoomHostReady 에서 받은 이 참여자 전용 guest-facing relay 경로. */
	FMOUGameRelayRoute PendingGuestRelayRoute;

	EMOUTravelTransport ActiveTravelTransport = EMOUTravelTransport::None;
	bool bRelayFallbackTried = false;
	float TravelAttemptSeconds = 0.f;

	/** 내가 방장인 방 번호. RoomCreateAck 로 확정되고, 방을 닫거나 끊기면 0 이 된다. */
	int32 MyRoomId = 0;

	/**
	 * 지금 들어가 있는 방. 방을 만들거나(RoomCreateAck) 참여하면(RoomJoinAck) 채워지고,
	 * 나가거나 방이 닫히거나 연결이 끊기면 0 이 된다.
	 *
	 * MyRoomId 와 나누어 두는 이유: 참여자는 방에 있어도 방장이 아니다.
	 * 하나로 합치면 "방장인가" 와 "방에 있는가" 를 구분할 수 없다.
	 */
	int32 CurrentRoomId = 0;

	UPROPERTY()
	TArray<FMOURoomMember> RoomMembers;

	bool bAllMembersReady = false;
};
