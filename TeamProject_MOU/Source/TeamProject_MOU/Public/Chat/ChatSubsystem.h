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
//   채팅 서버는 그 정보를 모른다. 지금은 클라이언트가 Login/SetDeadForTest 로
//   직접 알려주고 있어서 위조가 가능하다. 리슨서버가 채팅 서버에 미러링하도록 바꿔야 한다.

#pragma once

#include "CoreMinimal.h"
#include "Chat/ChatTypes.h"
#include "Chat/LobbyTypes.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChatSubsystem.generated.h"

class FChatClientRunnable;
class FRunnableThread;

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

/** 게임이 시작됐다. Result.MakeTravelURL() 로 호스트에게 붙으면 된다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRoomGameStarted, const FMOURoomJoinResult&, Host, bool, bIsHost);

/**
 * 채팅 서버 연결의 소유자.
 *
 * 역할:
 *   1. 워커 스레드(FChatClientRunnable)의 생성과 파괴
 *   2. 게임 스레드 Tick 에서 워커의 큐를 비우고 델리게이트로 전파
 *   3. 블루프린트/UMG 가 쓸 API 제공 (패킷 조립은 여기서 한다)
 */
UCLASS()
class TEAMPROJECT_MOU_API UChatSubsystem : public UGameInstanceSubsystem
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
	static UChatSubsystem* Get(const UObject* WorldContextObject);

	/**
	 * 채팅 서버에 접속을 시작한다. 즉시 반환하고 실제 접속은 워커 스레드에서 진행된다.
	 * 접속 결과는 OnChatStateChanged 로 알려준다.
	 *
	 * 실패해도 자동으로 재시도하므로, 서버를 나중에 켜도 알아서 붙는다.
	 * 이미 연결 중이면 아무 것도 하지 않는다.
	 *
	 * [주소를 인자로 넘기지 않는 것이 기본이다]
	 *   InHost 를 비워두거나 InPort 를 0 으로 두면 UMOUServerSettings::ResolveEndpoint
	 *   가 대신 정한다 — 즉 Config/DefaultGame.ini 의 팀 공유 주소를 쓴다.
	 *   여기에 127.0.0.1 을 직접 넘기면 사람마다 "자기 PC" 를 가리키게 되어, 서버를 켜지
	 *   않은 팀원은 무조건 접속에 실패한다. (Chat/ServerSettings.h 의 주석 참고)
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
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void StartGame();

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

private:
	/** 게임 스레드 틱. 워커 큐를 비우고 델리게이트를 브로드캐스트한다. */
	bool Tick(float DeltaTime);

	/** 워커 스레드를 Stop -> 종료 대기 -> 파괴 순서로 정리한다. */
	void ShutdownClient();

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
	 * 소켓 워커. 게임 스레드에서 생성/파괴하고, 그 사이에는 워커 스레드가 이 객체를 쓴다.
	 *
	 * 스마트 포인터를 쓰지 않고 원시 포인터로 두는 이유:
	 * 러너블의 수명은 "스레드가 끝났는가" 에만 달려 있고, 그 판단은 ShutdownClient 하나에서만
	 * 내린다. 참조 카운트로 관리하면 오히려 스레드가 살아있는데 객체가 먼저 사라질 여지가 생긴다.
	 * 반드시 ShutdownClient 를 거쳐서만 해제할 것. delete 를 직접 부르지 말 것.
	 */
	FChatClientRunnable* ChatClient = nullptr;
	FRunnableThread*     ChatThread = nullptr;

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
