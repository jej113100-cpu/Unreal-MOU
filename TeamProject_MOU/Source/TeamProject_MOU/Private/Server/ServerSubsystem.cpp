// MOU 채팅 - 서브시스템 구현.
//
// 이 파일이 하는 일은 결국 4가지다.
//   1. 백엔드(ILobbyBackend) 수명 관리 (생성 / 안전한 파괴)
//   2. 블루프린트가 부른 함수를 백엔드 호출로 넘기기 (+ 부를 자격이 있는지 검사)
//   3. 백엔드가 큐에 넣어둔 결과를 게임 스레드에서 꺼내 델리게이트로 뿌리기
//   4. 백엔드가 바뀌어도 같아야 하는 정책 보관 (재접속 자동 로그인, 방 번호, 호스트 준비 감시)
//
// [패킷을 조립하지 않는 것이 의도다]
//   MOU::LoginReqBody 같은 프로토콜 구조체를 다루는 곳은 SocketLobbyBackend.cpp 와
//   ServerClientRunnable.cpp 뿐이다. EOS 로 갈아끼우면 그 두 파일 대신 EOSLobbyBackend.cpp
//   가 쓰이고, 이 파일과 그 위(위젯/게임 로직)는 손대지 않는다.
//
//   예외가 하나 있다: ValidateCredentials / IsValidRoomPassword 는 프로토콜 헤더의
//   길이 상수를 읽는다. 그 숫자들은 전송 방식이 아니라 **서버와 맞춰야 하는 규칙**이고,
//   여기서 다시 적으면 서버가 상한을 바꿨을 때 조용히 어긋난다.

#include "Server/ServerSubsystem.h"

#include "Server/Net/ChatFraming.h"   // 계정/방 비밀번호 길이 규칙 상수 (패킷 조립에는 쓰지 않는다)
#include "Server/Lobby/LobbyBackend.h"
#include "Server/ServerSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/PendingNetGame.h"
#include "SocketSubsystem.h"
#include "Sockets.h"                          // FSocket (프로브·등록·홀펀칭이 직접 쓴다)
#include "Server/Net/MOUIpNetDriver.h"        // 클라이언트 바인드 포트 고정 (v10)
#include "IPAddress.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"   // ClientTravel (참여자 여행)
#include "Kismet/GameplayStatics.h"           // OpenLevel (방장 여행)
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY(LogMOUServer);

namespace
{
	// 에디터/비최적화 빌드는 UE의 InitialConnectTimeout 배율이 매우 커질 수 있다.
	// MOU의 직접 우선 -> relay 정책은 짧고 명확한 자체 제한으로 전환한다.
	constexpr float kDirectAttemptTimeoutSeconds = 8.f;
	constexpr float kRelayAttemptTimeoutSeconds  = 15.f;

	/**
	 * 로그 표시용 채널 이름.
	 * 서버 Server.cpp 의 ChannelName() 과 같은 문자열을 쓴다.
	 * 서버 콘솔 로그와 에디터 로그를 나란히 놓고 대조할 때 편하다.
	 */
	const TCHAR* ToChannelName(EChatChannelBP Channel)
	{
		switch (Channel)
		{
		case EChatChannelBP::All:     return TEXT("전체");
		case EChatChannelBP::Team:    return TEXT("팀");
		case EChatChannelBP::Dead:    return TEXT("사망");
		case EChatChannelBP::Whisper: return TEXT("귓속말");
		case EChatChannelBP::System:  return TEXT("시스템");
		default:                      return TEXT("알수없음");
		}
	}
}

// ---------------------------------------------------------------------------
// 수명 관리
// ---------------------------------------------------------------------------

void UServerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 여기서 자동 접속하지 않는다.
	// 접속 시점(타이틀 화면인지, 인게임 진입 후인지)은 게임 흐름에 따라 달라야 하고,
	// PIE 로 잠깐 레벨만 확인할 때 매번 서버에 붙는 것도 원치 않기 때문이다.
	// 접속은 UI 나 GameMode 가 ConnectToChatServer() 를 호출해서 시작한다.

	// 게임 스레드 틱 등록. 워커가 쌓아둔 큐를 여기서 비운다.
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UServerSubsystem::Tick));

	// 엔진의 접속/이동 실패를 받아 사람이 읽을 문장으로 바꿔 알린다.
	// 이게 없으면 실패가 화면에 "이동합니다..." 로 그대로 남아 원인을 알 수 없다.
	if (GEngine != nullptr)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UServerSubsystem::HandleNetworkFailure);
		TravelFailureHandle  = GEngine->OnTravelFailure().AddUObject(this, &UServerSubsystem::HandleTravelFailure);
	}

	UE_LOG(LogMOUServer, Log, TEXT("채팅 서브시스템 초기화 완료. 접속하려면 ConnectToChatServer 를 호출한다."));
}

void UServerSubsystem::Deinitialize()
{
	// 순서가 중요하다.
	// 틱을 먼저 끊어야 워커를 정리하는 도중에 Tick 이 죽은 큐를 읽는 일이 없다.
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}

	// 엔진 델리게이트를 먼저 뗀다. 안 떼면 파괴된 서브시스템으로 호출이 들어온다.
	if (GEngine != nullptr)
	{
		if (NetworkFailureHandle.IsValid()) { GEngine->OnNetworkFailure().Remove(NetworkFailureHandle); }
		if (TravelFailureHandle.IsValid())  { GEngine->OnTravelFailure().Remove(TravelFailureHandle);  }
	}

	ShutdownClient();

	// 미리 올려둔 맵이 남아 있으면 놓아준다. GameInstance 가 사라지는 시점이라
	// 어차피 정리되지만, 참조를 명시적으로 끊어두는 편이 추적하기 쉽다.
	ReleasePreloadedMap();

	Super::Deinitialize();
}

void UServerSubsystem::ShutdownClient()
{
	if (Backend.IsValid())
	{
		// 백엔드가 워커 스레드를 갖고 있으면 여기서 끝날 때까지 기다린다.
		// 기다리지 않으면 아직 살아있는 스레드가 이미 해제된 큐를 건드려서
		// "PIE 를 껐다 켜면 에디터가 통째로 죽는" 증상이 난다.
		Backend->Shutdown();
		Backend.Reset();
	}

	// 리슨서버 감시도 같이 끈다. 붙을 서버가 없는데 신호를 준비하고 있을 이유가 없다.
	bWaitingForListenServer = false;
	ListenServerWaitSeconds = 0.f;

	// ★ 게임 포트 소켓을 반드시 놓아준다. 들고 있으면 다음에 리슨서버나
	//   넷드라이버가 같은 포트를 못 연다.
	if (bProbing)
	{
		FinishReachabilityProbe(false, TEXT("종료 중이라 확인을 중단했습니다."));
	}
	CloseGameSocket();

	if (ConnectionState != EChatConnectionState::Disconnected)
	{
		SetConnectionState(EChatConnectionState::Disconnected, TEXT("클라이언트 종료"));
	}
	LoginResult = FChatLoginResult();
}

FString UServerSubsystem::GetBackendName() const
{
	return Backend.IsValid() ? Backend->GetBackendName() : TEXT("(없음)");
}

// ---------------------------------------------------------------------------
// 블루프린트 API
// ---------------------------------------------------------------------------

UServerSubsystem* UServerSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr)
	{
		return nullptr;
	}

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UServerSubsystem>() : nullptr;
}

void UServerSubsystem::ConnectToChatServer(const FString& InHost, int32 InPort)
{
	// 인자가 비어 있으면 코드가 주소를 정하지 않는다. 설정(Config/DefaultGame.ini) 또는
	// 실행 인자가 정한다. 이렇게 해야 "서버를 켠 PC 만 되는" 문제가 생기지 않는다.
	FString Host = InHost;
	int32   Port = InPort;
	if (Host.IsEmpty() || Port <= 0)
	{
		FString ResolvedHost;
		int32   ResolvedPort = 0;
		FString Source;
		UMOUServerSettings::ResolveEndpoint(ResolvedHost, ResolvedPort, &Source);

		if (Host.IsEmpty()) { Host = ResolvedHost; }
		if (Port <= 0)      { Port = ResolvedPort; }

		UE_LOG(LogMOUServer, Log, TEXT("접속 대상: %s:%d — 출처 %s"), *Host, Port, *Source);
	}

	if (Backend.IsValid())
	{
		// 이미 백엔드가 돌고 있다. 소켓 백엔드는 자체 재연결 루프를 갖고 있으므로
		// 끊긴 상태여도 새로 만들 필요가 없다.
		UE_LOG(LogMOUServer, Log, TEXT("이미 %s 백엔드가 동작 중이다. 접속 요청을 무시한다."),
			*Backend->GetBackendName());
		return;
	}

	// 어느 백엔드를 쓸지는 설정이 정한다. 코드가 종류를 아는 곳은 팩토리 하나뿐이다.
	FString BackendSource;
	const EMOULobbyBackendType BackendType = UMOUServerSettings::ResolveBackendType(&BackendSource);

	Backend = MOULobbyBackend::Create(BackendType);
	if (!Backend.IsValid())
	{
		UE_LOG(LogMOUServer, Error, TEXT("로비 백엔드 생성 실패 (종류 %d)"), static_cast<int32>(BackendType));
		return;
	}

	UE_LOG(LogMOUServer, Log, TEXT("로비 백엔드: %s — 출처 %s"), *Backend->GetBackendName(), *BackendSource);

	if (!Backend->Start(Host, Port))
	{
		UE_LOG(LogMOUServer, Error, TEXT("%s 백엔드를 시작하지 못했다."), *Backend->GetBackendName());
		Backend.Reset();
		return;
	}

	SetConnectionState(EChatConnectionState::Connecting, FString::Printf(TEXT("%s:%d"), *Host, Port));
}

FString UServerSubsystem::GetLoginResultText(EChatLoginResultBP Result)
{
	switch (Result)
	{
	case EChatLoginResultBP::Success:         return TEXT("성공");
	case EChatLoginResultBP::VersionMismatch: return TEXT("서버와 버전이 다릅니다. 양쪽을 다시 빌드해야 합니다.");
	case EChatLoginResultBP::InvalidRequest:  return TEXT("잘못된 요청입니다.");
	case EChatLoginResultBP::AccountNotFound: return TEXT("존재하지 않는 아이디입니다.");
	case EChatLoginResultBP::WrongPassword:   return TEXT("비밀번호가 올바르지 않습니다.");
	case EChatLoginResultBP::DuplicateId:     return TEXT("이미 사용 중인 아이디입니다.");
	case EChatLoginResultBP::InvalidFormat:   return TEXT("아이디 또는 비밀번호 형식이 올바르지 않습니다.");
	case EChatLoginResultBP::ServerError:     return TEXT("서버 오류입니다. 잠시 후 다시 시도해 주세요.");
	default:                                  return TEXT("알 수 없는 오류입니다.");
	}
}

bool UServerSubsystem::ValidateCredentials(const FString& LoginId, const FString& Password, FString& OutReason)
{
	// 서버와 같은 규칙을 쓴다. 길이는 UTF-8 바이트 기준이라 한글 아이디면 글자 수보다 커진다.
	const int32 IdBytes = MOUChat::GetUtf8Length(LoginId);
	const int32 PwBytes = MOUChat::GetUtf8Length(Password);

	if (IdBytes < static_cast<int32>(MOU::kMinLoginIdLen))
	{
		OutReason = FString::Printf(TEXT("아이디는 %d자 이상이어야 합니다."), MOU::kMinLoginIdLen);
		return false;
	}
	if (IdBytes >= static_cast<int32>(MOU::kMaxLoginIdLen))
	{
		OutReason = FString::Printf(TEXT("아이디가 너무 깁니다. (%d바이트 미만)"), MOU::kMaxLoginIdLen);
		return false;
	}
	if (PwBytes < static_cast<int32>(MOU::kMinPasswordLen))
	{
		OutReason = FString::Printf(TEXT("비밀번호는 %d자 이상이어야 합니다."), MOU::kMinPasswordLen);
		return false;
	}
	if (PwBytes >= static_cast<int32>(MOU::kMaxPasswordLen))
	{
		OutReason = FString::Printf(TEXT("비밀번호가 너무 깁니다. (%d바이트 미만)"), MOU::kMaxPasswordLen);
		return false;
	}

	OutReason.Empty();
	return true;
}

void UServerSubsystem::Login(const FString& LoginId, const FString& Password, int32 TeamId)
{
	// 요청은 항상 보관해둔다.
	// 연결이 끊겼다가 자동 재접속했을 때 이 값으로 다시 로그인해야 하기 때문이다.
	PendingLoginId   = LoginId;
	PendingPassword  = Password;
	PendingTeamId    = TeamId;
	bHasPendingLogin = true;

	if (ConnectionState == EChatConnectionState::Connected
		|| ConnectionState == EChatConnectionState::LoggedIn)
	{
		SendPendingLogin();
	}
	else
	{
		// 아직 TCP 가 안 붙었다. 붙는 순간 Tick 의 Connected 처리에서 자동으로 보낸다.
		// 비밀번호는 절대 로그에 남기지 않는다.
		UE_LOG(LogMOUServer, Log, TEXT("연결 전이라 로그인 요청을 보관한다: %s (팀 %d)"), *LoginId, TeamId);
	}
}

void UServerSubsystem::RegisterAccount(const FString& LoginId, const FString& Password, const FString& Nickname)
{
	// 연결 전에 불릴 수 있으므로 일단 보관한다.
	// 지금 바로 EnqueuePacket 하면, 연결이 성사되는 순간 워커가 송신 큐를 비우면서
	// 이 패킷까지 같이 버린다(그 비우기는 낡은 패킷이 LoginReq 를 앞지르는 것을 막는 장치다).
	PendingRegisterId       = LoginId;
	PendingRegisterPassword = Password;
	PendingRegisterNickname = Nickname;
	bHasPendingRegister     = true;

	if (ConnectionState == EChatConnectionState::Connected
		|| ConnectionState == EChatConnectionState::LoggedIn)
	{
		SendPendingRegister();
	}
	else
	{
		UE_LOG(LogMOUServer, Log, TEXT("연결 전이라 가입 요청을 보관한다: %s"), *LoginId);
	}
}

void UServerSubsystem::SendPendingRegister()
{
	if (!Backend.IsValid() || !bHasPendingRegister)
	{
		return;
	}

	Backend->SendRegister(PendingRegisterId, PendingRegisterPassword, PendingRegisterNickname);
}

void UServerSubsystem::SendPendingLogin()
{
	if (!Backend.IsValid() || !bHasPendingLogin)
	{
		return;
	}

	Backend->SendLogin(PendingLoginId, PendingPassword, PendingTeamId);
}

void UServerSubsystem::SendChat(EChatChannelBP Channel, const FString& Text)
{
	if (!Backend.IsValid())
	{
		UE_LOG(LogMOUServer, Warning, TEXT("백엔드가 없다. ConnectToChatServer 를 먼저 호출한다."));
		return;
	}

	// EOS 같은 백엔드는 사망자/팀 채널 판정을 할 수 없어 채팅을 맡지 않는다.
	// 조용히 버리면 "메시지가 어디로 갔지" 가 되므로 사유를 남긴다.
	if (!Backend->SupportsChat())
	{
		UE_LOG(LogMOUServer, Warning, TEXT("%s 백엔드는 채팅을 지원하지 않는다."), *Backend->GetBackendName());
		return;
	}

	// 서버는 로그인 전 채팅을 조용히 버린다(연결은 유지). 사용자가 원인을 알 수 없으므로 여기서 알린다.
	if (ConnectionState != EChatConnectionState::LoggedIn)
	{
		UE_LOG(LogMOUServer, Warning, TEXT("아직 로그인 전이라 채팅을 보낼 수 없다. (현재 상태 %d)"),
			static_cast<int32>(ConnectionState));
		return;
	}

	// System 채널은 서버만 만든다. 보내봐야 서버가 버리므로 여기서 막는다.
	if (Channel == EChatChannelBP::System)
	{
		UE_LOG(LogMOUServer, Warning, TEXT("System 채널은 클라이언트가 보낼 수 없다."));
		return;
	}

	// 길이 상한을 넘겨 자르는 일은 백엔드가 한다. 상한값이 프로토콜 사정이기 때문이다.
	Backend->SendChat(Channel, Text);
}

void UServerSubsystem::SetDeadForTest(bool bDead)
{
	if (!Backend.IsValid() || ConnectionState != EChatConnectionState::LoggedIn)
	{
		UE_LOG(LogMOUServer, Warning, TEXT("로그인 후에만 생사 상태를 바꿀 수 있다."));
		return;
	}

	Backend->SendSetDead(LoginResult.UserId, bDead);
}

// ---------------------------------------------------------------------------
// 로비
//
// 서버는 방 주소록 역할만 한다. 여기서 오가는 것은 방 메타데이터뿐이고,
// 실제 게임 트래픽은 참가자가 호스트의 리슨서버에 직접 붙어서 주고받는다.
// ---------------------------------------------------------------------------

bool UServerSubsystem::IsValidRoomPassword(const FString& RoomPassword)
{
	if (RoomPassword.Len() != static_cast<int32>(MOU::kRoomPasswordLen))
	{
		return false;
	}
	for (const TCHAR C : RoomPassword)
	{
		if (!FChar::IsDigit(C))
		{
			return false;
		}
	}
	return true;
}

FString UServerSubsystem::GetRoomResultText(EMOURoomResultBP Result)
{
	switch (Result)
	{
	case EMOURoomResultBP::Success:        return TEXT("성공");
	case EMOURoomResultBP::NotAuthed:      return TEXT("먼저 로그인해야 합니다.");
	case EMOURoomResultBP::NotFound:       return TEXT("방을 찾을 수 없습니다. 이미 닫혔을 수 있습니다.");
	case EMOURoomResultBP::WrongPassword:  return TEXT("방 비밀번호가 올바르지 않습니다.");
	case EMOURoomResultBP::Full:           return TEXT("정원이 가득 찼습니다.");
	case EMOURoomResultBP::AlreadyStarted: return TEXT("이미 시작된 게임입니다.");
	case EMOURoomResultBP::AlreadyHosting: return TEXT("이미 방을 만들었습니다. 기존 방을 먼저 닫으세요.");
	case EMOURoomResultBP::NotInRoom:      return TEXT("방에 들어가 있지 않습니다.");
	case EMOURoomResultBP::NotHost:        return TEXT("방장만 할 수 있습니다.");
	case EMOURoomResultBP::NotAllReady:    return TEXT("아직 준비하지 않은 참여자가 있습니다.");
	case EMOURoomResultBP::NotStarted:     return TEXT("아직 시작되지 않은 방입니다.");
	default:                               return TEXT("잘못된 요청입니다.");
	}
}

void UServerSubsystem::CreateRoom(const FString& Title, const FString& RoomPassword, int32 HostPort)
{
	if (!Backend.IsValid() || ConnectionState != EChatConnectionState::LoggedIn)
	{
		UE_LOG(LogMOUServer, Warning, TEXT("로그인 후에 방을 만들 수 있다."));
		OnRoomCreated.Broadcast(false, 0, EMOURoomResultBP::NotAuthed);
		return;
	}

	// 규칙(숫자 4자리)에 맞지 않는 비밀번호는 공개방으로 친다.
	// 판정을 여기서 하는 이유: "무엇이 유효한 비밀번호인가" 는 게임 규칙이지
	// 전송 방식의 문제가 아니다. 백엔드가 바뀌어도 같은 규칙이어야 한다.
	const FString EffectivePassword = IsValidRoomPassword(RoomPassword) ? RoomPassword : FString();

	// 방에 광고하는 포트와 나중에 실제 listen socket 이 bind 할 포트를 하나로 맞춘다.
	// 7777 이 이미 사용 중이면 EnsureGameSocket 이 7778 등으로 옮길 수 있는데, 예전에는
	// 방 목록만 7777을 계속 가리켜 relay bootstrap과 직접 후보가 서로 어긋날 수 있었다.
	RegisterGameEndpoint(HostPort);
	const int32 AdvertisedPort = ReservedGamePort > 0 ? ReservedGamePort : HostPort;
	Backend->CreateRoom(Title, EffectivePassword, AdvertisedPort, GetLocalLanAddress());
}

void UServerSubsystem::RequestRoomList()
{
	if (!Backend.IsValid() || ConnectionState != EChatConnectionState::LoggedIn)
	{
		UE_LOG(LogMOUServer, Warning, TEXT("로그인 후에 방 목록을 볼 수 있다."));
		OnRoomListReceived.Broadcast(TArray<FMOURoomInfo>());
		return;
	}

	Backend->RequestRoomList();
}

void UServerSubsystem::JoinRoom(int32 RoomId, const FString& RoomPassword)
{
	if (!Backend.IsValid() || ConnectionState != EChatConnectionState::LoggedIn)
	{
		FMOURoomJoinResult Failed;
		Failed.RoomId = RoomId;
		Failed.Result = EMOURoomResultBP::NotAuthed;
		OnRoomJoinCompleted.Broadcast(Failed);
		return;
	}

	const FString EffectivePassword = IsValidRoomPassword(RoomPassword) ? RoomPassword : FString();

	Backend->JoinRoom(RoomId, EffectivePassword);
}

void UServerSubsystem::LeaveRoom()
{
	if (!Backend.IsValid())
	{
		return;
	}

	Backend->LeaveRoom();

	// 서버 응답을 기다리지 않고 즉시 비운다.
	// 내가 나가는 것은 서버가 거부할 수 있는 일이 아니고, UI 가 바로 메인메뉴로
	// 돌아가야 사용자가 버튼을 두 번 누르지 않는다.
	ClearRoomState();
}

void UServerSubsystem::SetReady(bool bReady)
{
	if (!Backend.IsValid() || CurrentRoomId == 0)
	{
		return;
	}

	Backend->SetReady(bReady);
}

void UServerSubsystem::StartGame()
{
	if (!Backend.IsValid() || CurrentRoomId == 0)
	{
		return;
	}

	Backend->StartGame();
}

// ---------------------------------------------------------------------------
// 친구 / 메신저 (v7)
//
// 전부 "백엔드에 넘기기만" 한다. 결과는 Tick 이 큐에서 꺼내 델리게이트로 뿌린다.
// 여기서 곧바로 델리게이트를 쏘지 않는 이유: 서버 응답을 기다려야 하고,
// 낙관적으로 먼저 그려두면 서버가 거절했을 때 되돌려야 한다.
// ---------------------------------------------------------------------------

int32 UServerSubsystem::FindCachedFriendIndex(int64 UserId) const
{
	for (int32 i = 0; i < CachedFriends.Num(); ++i)
	{
		if (CachedFriends[i].UserId == UserId)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 UServerSubsystem::GetTotalUnreadCount() const
{
	int32 Total = 0;
	for (const FMOUFriend& Entry : CachedFriends)
	{
		Total += Entry.UnreadCount;
	}
	return Total;
}

void UServerSubsystem::RequestFriendList()
{
	if (!Backend.IsValid() || !LoginResult.bSuccess)
	{
		return;
	}

	Backend->RequestFriendList();
}

void UServerSubsystem::AddFriend(const FString& Query)
{
	if (!Backend.IsValid() || !LoginResult.bSuccess)
	{
		return;
	}

	// 빈 문자열은 보내지 않는다. 서버도 막지만 왕복을 아낀다.
	const FString Trimmed = Query.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		OnFriendAddCompleted.Broadcast(false, EMOUFriendResultBP::InvalidFormat);
		return;
	}

	Backend->AddFriend(Trimmed);
}

void UServerSubsystem::AcceptFriendRequest(int64 FromUserId)
{
	if (Backend.IsValid() && LoginResult.bSuccess)
	{
		Backend->RespondFriendRequest(FromUserId, /*bAccept=*/true);
	}
}

void UServerSubsystem::DeclineFriendRequest(int64 FromUserId)
{
	if (Backend.IsValid() && LoginResult.bSuccess)
	{
		Backend->RespondFriendRequest(FromUserId, /*bAccept=*/false);
	}
}

void UServerSubsystem::RemoveFriend(int64 TargetUserId)
{
	if (Backend.IsValid() && LoginResult.bSuccess)
	{
		Backend->RemoveFriend(TargetUserId);
	}
}

void UServerSubsystem::SendDirectMessage(int64 TargetUserId, const FString& Text)
{
	if (!Backend.IsValid() || !LoginResult.bSuccess || TargetUserId == 0)
	{
		return;
	}

	if (Text.TrimStartAndEnd().IsEmpty())
	{
		return;
	}

	Backend->SendDirectMessage(TargetUserId, Text);
}

void UServerSubsystem::OpenConversation(int64 PeerUserId)
{
	if (!Backend.IsValid() || !LoginResult.bSuccess || PeerUserId == 0)
	{
		return;
	}

	// BeforeMessageId == 0 이 "최신 페이지 + 읽음 처리" 다(서버 HandleDmHistoryReq).
	Backend->RequestDmHistory(PeerUserId, 0);

	// ★ 배지를 낙관적으로 지운다. 서버도 읽음 처리를 하지만 그 결과가 목록으로
	//   다시 내려오지는 않으므로, 여기서 지우지 않으면 **대화창을 열었는데
	//   배지가 그대로 남아** 다시 로그인할 때까지 사라지지 않는다.
	const int32 Index = FindCachedFriendIndex(PeerUserId);
	if (Index != INDEX_NONE)
	{
		CachedFriends[Index].UnreadCount = 0;
	}
}

void UServerSubsystem::LoadOlderMessages(int64 PeerUserId, int64 OldestMessageId)
{
	if (!Backend.IsValid() || !LoginResult.bSuccess || PeerUserId == 0)
	{
		return;
	}

	// ★ 0 을 넘기면 서버가 "최신 페이지" 로 해석해 읽음 처리까지 해버린다.
	//   위로 스크롤은 읽음과 무관해야 하므로 여기서 막는다.
	if (OldestMessageId <= 0)
	{
		UE_LOG(LogMOUServer, Warning,
			TEXT("LoadOlderMessages 에 0 이 들어왔다. 최신 페이지를 원하면 OpenConversation 을 쓸 것."));
		return;
	}

	Backend->RequestDmHistory(PeerUserId, OldestMessageId);
}

bool UServerSubsystem::IsSelfReady() const
{
	const int64 SelfId = LoginResult.UserId;
	for (const FMOURoomMember& Member : RoomMembers)
	{
		if (Member.UserId == SelfId)
		{
			return Member.bReady;
		}
	}
	return false;
}

void UServerSubsystem::ClearRoomState()
{
	MyRoomId      = 0;
	CurrentRoomId = 0;
	RoomMembers.Reset();
	bAllMembersReady = false;

	// 방을 떠났으면 리슨서버 감시도 의미가 없다.
	// 이걸 지우지 않으면, 방을 나갔다 새 방에 들어갔을 때 엉뚱한 시점에
	// "준비됐다" 가 나가고 서버가 NotStarted 로 거절한다.
	bWaitingForListenServer = false;
	ListenServerWaitSeconds = 0.f;

	// 참여자 쪽도 같은 이유로 지운다. 안 지우면 방을 나간 뒤에도 타이머가 돌아
	// 엉뚱한 화면에 "방장의 서버가 열리지 않았습니다" 가 뜬다.
	bGuestWaitingForHostReady = false;
	GuestWaitSeconds          = 0.f;

	// ★ 받아둔 출발 신호도 버린다. 남겨두면 다음 방에서 TravelToHost 가
	//   지난 방의 주소로 떠난다.
	PendingHostReady = FMOURoomJoinResult();
	TriedCandidateIndex = INDEX_NONE;
	TriedCandidateIndices.Reset();
	PendingHostRelayRoutes.Reset();
	PendingGuestRelayRoute = FMOUGameRelayRoute();
	ActiveTravelTransport = EMOUTravelTransport::None;
	bRelayFallbackTried = false;
	TravelAttemptSeconds = 0.f;
	UMOUIpNetDriver::ClearPendingRelayRegistrations();
	TravelRoomPassword.Reset();
}

void UServerSubsystem::UpdateRoomState(int32 RoomId, int32 CurrentPlayers, bool bInGame)
{
	if (!Backend.IsValid() || RoomId == 0)
	{
		return;
	}

	Backend->UpdateRoomState(RoomId, CurrentPlayers, bInGame);
}

void UServerSubsystem::Disconnect()
{
	bHasPendingLogin = false;   // 사용자가 의도적으로 끊은 것이므로 자동 재로그인하지 않는다
	PendingPassword.Empty();    // 비밀번호를 필요 이상으로 메모리에 두지 않는다
	ShutdownClient();
}

// ---------------------------------------------------------------------------
// 게임 스레드 틱 - 백엔드 -> UI 방향의 유일한 통로
// ---------------------------------------------------------------------------

bool UServerSubsystem::Tick(float DeltaTime)
{
	if (!Backend.IsValid())
	{
		return true;   // false 를 돌려주면 틱이 영구 해제된다. 항상 true
	}

	// 0) 방장이면 내 리슨서버가 떴는지, 참여자면 너무 오래 기다리지 않는지 확인한다.
	//    사건 처리보다 먼저 하는 이유는 없다. 서로 독립적이다.
	PollListenServer(DeltaTime);
	PollGuestHostReadyTimeout(DeltaTime);
	PollReachabilityProbe(DeltaTime);
	PollTravelConnection(DeltaTime);

	// 1) 상태 변화 처리
	FServerClientEvent Event;
	while (Backend->DequeueEvent(Event))
	{
		switch (Event.Type)
		{
		case EServerClientEventType::Connecting:
			SetConnectionState(EChatConnectionState::Connecting, Event.Detail);
			break;

		case EServerClientEventType::Connected:
			SetConnectionState(EChatConnectionState::Connected, Event.Detail);
			// 가입을 먼저 보낸다. 같은 TCP 스트림이라 서버가 이 순서대로 처리하므로,
			// "가입 후 곧바로 로그인" 이 한 번의 연결로 끝난다.
			SendPendingRegister();
			// 접속 전에 Login() 이 호출됐거나, 끊겼다 재접속한 경우 여기서 자동으로 로그인한다.
			SendPendingLogin();
			break;

		case EServerClientEventType::ConnectFailed:
			// 워커가 알아서 재시도하므로 여기서 할 일은 UI 에 알리는 것뿐이다.
			SetConnectionState(EChatConnectionState::Disconnected, Event.Detail);
			break;

		case EServerClientEventType::LoginAck:
			LoginResult = Event.Login;
			if (LoginResult.bSuccess)
			{
				SetConnectionState(EChatConnectionState::LoggedIn, LoginResult.Name);
				UE_LOG(LogMOUServer, Log, TEXT("로그인 완료. UserId=%lld, 이름=%s, 팀=%d"),
					LoginResult.UserId, *LoginResult.Name, LoginResult.TeamId);

				// ★ 친구 목록을 자동으로 한 번 받아둔다 (v7).
				//   위젯이 부르게 하면 로비를 안 여는 경로(바로 게임 참여 등)에서
				//   목록이 비어 있고, 그 상태로 온 FriendPresence 델타는 붙일 곳이
				//   없어서 버려진다. 로그인 = 목록 확보로 묶는 편이 안전하다.
				//
				//   밀린 DM 은 서버가 알아서 밀어준다(요청 불필요).
				CachedFriends.Reset();
				if (Backend.IsValid())
				{
					Backend->RequestFriendList();
				}
			}
			else if (LoginResult.Result == EChatLoginResultBP::VersionMismatch)
			{
				// 재시도해도 계속 실패한다.
				// 우리 쪽 프로토콜 번호를 아는 것은 백엔드뿐이라, 두 버전을 나란히 찍는
				// 상세 로그는 거기서 남긴다(FServerClientRunnable::HandlePacket).
				UE_LOG(LogMOUServer, Error,
					TEXT("프로토콜 버전 불일치로 로그인이 거부됐다. 서버=%d. 자세한 내용은 위 로그 참고."),
					LoginResult.ServerVersion);
			}
			else
			{
				// 아이디/비밀번호 실수는 흔한 일이라 사용자가 고칠 수 있게 사유를 그대로 남긴다.
				UE_LOG(LogMOUServer, Warning, TEXT("서버가 로그인을 거부했다: %s"),
					*UServerSubsystem::GetLoginResultText(LoginResult.Result));

				// 인증 실패는 재시도해도 같은 결과다. 보관해둔 요청을 지워
				// 재접속 때마다 틀린 비밀번호를 자동 재전송하는 것을 막는다.
				if (LoginResult.Result == EChatLoginResultBP::AccountNotFound
					|| LoginResult.Result == EChatLoginResultBP::WrongPassword
					|| LoginResult.Result == EChatLoginResultBP::InvalidFormat)
				{
					bHasPendingLogin = false;
					PendingPassword.Empty();
				}
			}
			OnChatLoginCompleted.Broadcast(LoginResult);
			break;

		case EServerClientEventType::RegisterAck:
			// 응답을 받았으므로 보관본을 지운다.
			// 안 지우면 재접속할 때마다 가입을 다시 시도해 "이미 있는 아이디" 가 반복된다.
			bHasPendingRegister = false;
			PendingRegisterPassword.Empty();

			if (Event.Login.bSuccess)
			{
				UE_LOG(LogMOUServer, Log, TEXT("계정 생성 완료. 이어서 로그인하면 된다."));
			}
			else
			{
				UE_LOG(LogMOUServer, Warning, TEXT("계정 생성 실패: %s"),
					*UServerSubsystem::GetLoginResultText(Event.Login.Result));
			}
			OnChatRegisterCompleted.Broadcast(Event.Login.bSuccess, Event.Login.Result);
			break;

		case EServerClientEventType::RoomCreateAck:
			if (Event.bRoomSuccess)
			{
				MyRoomId      = Event.RoomId;
				CurrentRoomId = Event.RoomId;   // 방장도 그 방의 멤버다
				UE_LOG(LogMOUServer, Log, TEXT("방 생성 완료. 방번호 #%d"), MyRoomId);
			}
			else
			{
				UE_LOG(LogMOUServer, Warning, TEXT("방 생성 실패: %s"),
					*UServerSubsystem::GetRoomResultText(Event.RoomResult));
			}
			OnRoomCreated.Broadcast(Event.bRoomSuccess, Event.RoomId, Event.RoomResult);
			break;

		case EServerClientEventType::RoomListAck:
			UE_LOG(LogMOUServer, Log, TEXT("방 목록 수신: %d개"), Event.Rooms.Num());
			OnRoomListReceived.Broadcast(Event.Rooms);
			break;

		case EServerClientEventType::RoomJoinAck:
			if (Event.Join.bSuccess)
			{
				CurrentRoomId = Event.Join.RoomId;   // 대기실 입장. 방장은 아니다
				UE_LOG(LogMOUServer, Log, TEXT("방 #%d 입장. 호스트 후보 %s"),
					Event.Join.RoomId, *Event.Join.ToDisplayString());
			}
			else
			{
				UE_LOG(LogMOUServer, Warning, TEXT("방 참여 실패: %s"),
					*UServerSubsystem::GetRoomResultText(Event.Join.Result));
			}
			OnRoomJoinCompleted.Broadcast(Event.Join);
			break;

		case EServerClientEventType::RoomMemberList:
			// 늦게 도착한 이전 방의 명단이 현재 대기실을 덮어쓰지 않게 방 번호를 확인한다.
			// 빠르게 나갔다 다른 방에 들어가면 실제로 이런 순서가 나온다.
			if (Event.RoomId == CurrentRoomId)
			{
				RoomMembers      = Event.Members;
				bAllMembersReady = Event.bAllReady;
				UE_LOG(LogMOUServer, Verbose, TEXT("대기실 #%d 명단 %d명 (전원준비 %s)"),
					Event.RoomId, RoomMembers.Num(), bAllMembersReady ? TEXT("O") : TEXT("X"));
				OnRoomMembersChanged.Broadcast(Event.RoomId, RoomMembers, bAllMembersReady);
			}
			break;

		case EServerClientEventType::RoomClosed:
			UE_LOG(LogMOUServer, Log, TEXT("방 #%d 이(가) 닫혔다. 방장이 나갔다."), Event.RoomId);
			ClearRoomState();
			OnRoomClosed.Broadcast(Event.RoomId, Event.CloseReason);
			break;

		case EServerClientEventType::RoomStart:
		{
			// bIsHost 를 여기서 계산해 넘긴다. 받는 쪽은 호스트냐 참여자냐에 따라
			// "리슨서버를 연다" 와 "기다린다" 로 갈리는데, 그 판단 근거가
			// 이미 여기 있으므로 UI 가 다시 따지게 하지 않는다.
			const bool bIsHost = (MyRoomId != 0 && MyRoomId == Event.RoomId);
			UE_LOG(LogMOUServer, Log, TEXT("방 #%d 게임 시작. 호스트 후보 %s (나는 %s)"),
				Event.RoomId, *Event.Join.ToDisplayString(),
				bIsHost ? TEXT("방장") : TEXT("참여자"));

			if (bIsHost)
			{
				// 지금부터 내 리슨서버가 뜨는지 지켜본다. 뜨는 순간 참여자에게 출발 신호가 나간다.
				//
				// 위젯이 아니라 여기서 켜는 이유: 방장은 곧 OpenLevel 로 맵을 갈아탄다.
				// 그 순간 로비 위젯은 파괴되므로, 위젯이 감시를 맡으면 감시자가 사라진다.
				bWaitingForListenServer = true;
				ListenServerWaitSeconds = 0.f;

				// 방장이 punch 할 대상. OpenLevel 직전에 쓴다. (v10)
				PunchTargets = Event.PunchTargets;
				UE_CLOG(PunchTargets.Num() > 0, LogMOUServer, Log,
					TEXT("[홀펀칭] 참여자 %d명의 주소를 받았다."), PunchTargets.Num());

				// 각 참여자에 대한 host-facing relay capability. UI에는 내보내지 않고
				// TravelAsHost -> 실제 listen socket bootstrap 에서만 쓴다.
				PendingHostRelayRoutes = Event.HostRelayRoutes;
				for (const FMOUGameRelayRoute& Route : PendingHostRelayRoutes)
				{
					RegisterRelayRouteFromGameSocket(Route, /*bHost=*/true);
				}
			}
			else
			{
				// 참여자는 지금부터 출발 신호를 기다린다. 끝없이 기다리지는 않는다.
				bGuestWaitingForHostReady = true;
				GuestWaitSeconds          = 0.f;
				PendingHostReady          = FMOURoomJoinResult();   // 지난 판의 값이 남아 있으면 안 된다
				TriedCandidateIndex       = INDEX_NONE;
				TriedCandidateIndices.Reset();
			}

			// ★ UI 보다 먼저 브로드캐스트하지 않는다. 위젯이 안내 문구를 띄우고
			//   BP 훅이 돌 기회를 준 뒤에 실제 행동을 한다 — OpenLevel 이 시작되면
			//   위젯은 곧 파괴되므로 순서를 뒤집으면 안내가 화면에 안 뜬다.
			OnRoomGameStarted.Broadcast(Event.Join, bIsHost);

			if (bIsHost)
			{
				TravelAsHost();
			}
			else if (bPreloadMapWhileWaiting)
			{
				// 참여자는 방장이 맵을 여는 동안 놀고 있다. 그 시간에 미리 올리면
				// 두 로딩이 겹쳐져 실제 대기 시간이 크게 준다.
				BeginPreloadMap(HostMapName);
			}
			break;
		}

		case EServerClientEventType::RoomHostReady:
		{
			// 참여자에게만 온다. 이제 붙어도 된다.
			UE_LOG(LogMOUServer, Log, TEXT("방 #%d 호스트 준비 완료. 후보 %s 로 이동한다."),
				Event.RoomId, *Event.Join.ToDisplayString());

			// ★ 브로드캐스트하고 버리지 않는다. 이 순간 로비 위젯이 이미 닫혀 있으면
			//   예전에는 정보가 통째로 사라져서 참여자가 영영 못 떠났다.
			PendingHostReady          = Event.Join;
			TriedCandidateIndex       = INDEX_NONE;
			TriedCandidateIndices.Reset();
			PendingGuestRelayRoute    = Event.GuestRelayRoute;
			bRelayFallbackTried       = false;
			ActiveTravelTransport     = EMOUTravelTransport::None;
			bGuestWaitingForHostReady = false;
			GuestWaitSeconds          = 0.f;
			RegisterRelayRouteFromGameSocket(PendingGuestRelayRoute, /*bHost=*/false);

			OnRoomHostReady.Broadcast(Event.Join);

			if (bAutoTravelOnGameStart)
			{
				TravelToHost();
			}
			break;
		}

		case EServerClientEventType::ClientEndpointAck:
			// 서버가 내 공인 게임 엔드포인트를 관측했다. 방장이 punch 할 주소가 이것이다.
			ObservedEndpoint = Event.Detail;
			UE_LOG(LogMOUServer, Log, TEXT("[엔드포인트] 서버가 내 게임 주소를 %s 로 봤다."), *ObservedEndpoint);
			break;

		case EServerClientEventType::HostProbeSent:
			// 서버가 쐈다. 지금부터 도착을 기다린다.
			if (bProbing && Event.ProbeNonce == ProbeNonce)
			{
				if (Event.bProbeSent)
				{
					// 재시도 요청의 HostProbeSent가 올 때마다 시간을 0으로 만들면
					// 실패 상한이 영원히 밀린다. 첫 발사 확인에서만 시작한다.
					if (!bProbeDispatched)
					{
						bProbeDispatched = true;
						ProbeWaitSeconds = 0.f;
						ProbeRetrySeconds = 0.f;
					}
				}
				else
				{
					// 서버가 쏘지 못했다. 기다려봐야 오지 않는다.
					FinishReachabilityProbe(false, TEXT("서버가 확인용 패킷을 보내지 못했습니다."));
				}
			}
			break;

		case EServerClientEventType::Disconnected:
			LoginResult = FChatLoginResult();
			// 연결이 끊기면 서버가 내 방을 지운다. 클라이언트 쪽 기억도 같이 비운다.
			ClearRoomState();
			// ★ 친구 캐시도 비운다. 안 비우면 재접속 후 옛 목록이 잠깐 보이고,
			//   그 위에 새 목록이 덮이면서 화면이 두 번 바뀐다.
			CachedFriends.Reset();
			SetConnectionState(EChatConnectionState::Disconnected, Event.Detail);
			break;

		// ------------------------------------------------------------------
		// 친구 (v7)
		// ------------------------------------------------------------------

		case EServerClientEventType::FriendListAck:
			CachedFriends = Event.Friends;
			UE_LOG(LogMOUServer, Log, TEXT("친구 목록 %d명 수신"), CachedFriends.Num());
			OnFriendListReceived.Broadcast(CachedFriends);
			break;

		case EServerClientEventType::FriendAddAck:
			if (!Event.bFriendSuccess)
			{
				// 사람이 읽을 문구는 위젯이 만든다(UMOUFriendText::GetFriendResultText).
				// 여기서는 진단용으로 값만 남긴다 — 로그가 UI 문구를 정하면
				// 문구를 바꿀 때 두 곳을 고쳐야 한다.
				UE_LOG(LogMOUServer, Warning, TEXT("친구 신청 실패 (사유 코드 %d)"),
					static_cast<int32>(Event.FriendResult));
			}
			OnFriendAddCompleted.Broadcast(Event.bFriendSuccess, Event.FriendResult);
			break;

		case EServerClientEventType::FriendRequestIncoming:
		{
			// 목록에도 바로 넣는다. 이게 없으면 알림만 뜨고 목록에는 안 보여서
			// 수락 버튼을 누를 곳이 없다.
			const int32 Existing = FindCachedFriendIndex(Event.Friend.UserId);
			if (Existing == INDEX_NONE)
			{
				CachedFriends.Add(Event.Friend);
			}
			else
			{
				CachedFriends[Existing] = Event.Friend;
			}

			UE_LOG(LogMOUServer, Log, TEXT("친구 신청 도착: %s (id=%lld)"),
				*Event.Friend.Nickname, Event.Friend.UserId);

			OnFriendRequestReceived.Broadcast(Event.Friend.UserId, Event.Friend.Nickname);
			OnFriendUpdated.Broadcast(Event.Friend, /*bRemoved=*/false);
			break;
		}

		case EServerClientEventType::FriendUpdate:
		{
			const int32 Existing = FindCachedFriendIndex(Event.Friend.UserId);

			if (Event.bFriendRemoved)
			{
				if (Existing != INDEX_NONE)
				{
					CachedFriends.RemoveAt(Existing);
				}
			}
			else if (Existing == INDEX_NONE)
			{
				CachedFriends.Add(Event.Friend);
			}
			else
			{
				// ★ 안 읽음 개수는 이 패킷에 없다(FriendUpdateBody 에 필드가 없다).
				//   통째로 덮으면 배지가 0 으로 지워지므로 기존 값을 지켜준다.
				const int32 KeptUnread = CachedFriends[Existing].UnreadCount;
				CachedFriends[Existing] = Event.Friend;
				CachedFriends[Existing].UnreadCount = KeptUnread;
			}

			OnFriendUpdated.Broadcast(Event.Friend, Event.bFriendRemoved);
			break;
		}

		case EServerClientEventType::FriendPresence:
		{
			// ★ 이 패킷에는 닉네임이 없다(9바이트). UserId 로 찾아 상태만 갈아끼운다.
			//   못 찾으면 아직 목록이 안 온 것이라 버린다 — 곧 FriendListAck 가
			//   최신 상태를 통째로 가져온다.
			const int32 Existing = FindCachedFriendIndex(Event.Friend.UserId);
			if (Existing != INDEX_NONE)
			{
				CachedFriends[Existing].Presence  = Event.Friend.Presence;
				CachedFriends[Existing].bIsOnline = Event.Friend.bIsOnline;
			}

			OnFriendPresenceChanged.Broadcast(Event.Friend.UserId, Event.Friend.Presence);
			break;
		}

		// ------------------------------------------------------------------
		// 메신저 (v7)
		// ------------------------------------------------------------------

		case EServerClientEventType::DirectMessage:
		{
			FMOUDirectMessage Msg = Event.DirectMessage;

			// ★ 내 것인지와 상대가 누구인지는 **여기서** 채운다. 서버는 보내는
			//   쪽에게도 같은 패킷을 되돌려주므로(에코), 내 UserId 를 아는
			//   이쪽에서 판정해야 위젯이 매번 비교하지 않는다.
			Msg.bIsMine    = (Msg.FromUserId == LoginResult.UserId);
			Msg.PeerUserId = Msg.bIsMine ? Msg.ToUserId : Msg.FromUserId;

			// 받은 것이면 안 읽음을 올린다. 대화창이 열려 있으면 위젯이
			// OpenConversation 을 다시 불러 0 으로 되돌린다.
			if (!Msg.bIsMine)
			{
				const int32 Existing = FindCachedFriendIndex(Msg.PeerUserId);
				if (Existing != INDEX_NONE)
				{
					++CachedFriends[Existing].UnreadCount;
				}
			}

			OnDirectMessageReceived.Broadcast(Msg);
			break;
		}

		case EServerClientEventType::DmHistoryAck:
		{
			// 기록의 bIsMine 도 여기서 채운다(위와 같은 이유).
			TArray<FMOUDirectMessage> History = Event.History;
			for (FMOUDirectMessage& Msg : History)
			{
				Msg.bIsMine    = (Msg.FromUserId == LoginResult.UserId);
				Msg.PeerUserId = Event.PeerUserId;
				Msg.ToUserId   = Msg.bIsMine ? Event.PeerUserId : LoginResult.UserId;
			}

			OnDmHistoryReceived.Broadcast(Event.PeerUserId, History, Event.bHasMoreHistory);
			break;
		}
		}
	}

	// 2) 수신한 채팅 처리
	FChatMessage Message;
	while (Backend->DequeueMessage(Message))
	{
		// UI 가 붙기 전(5단계 이전)에도 동작을 확인할 수 있도록 로그를 남긴다.
		UE_LOG(LogMOUServer, Log, TEXT("[%s] %s: %s"),
			ToChannelName(Message.Channel), *Message.SenderName, *Message.Text);

		// 여기가 백엔드 -> 게임 스레드 경계의 끝이다.
		// 게임 스레드에서 부르므로 이 델리게이트 안에서 UMG 위젯을 만들어도 안전하다.
		OnChatMessageReceived.Broadcast(Message);
	}

	return true;
}

// ---------------------------------------------------------------------------
// 호스트 준비 감시
//
// [v5 까지 어떻게 했었나 — 그리고 왜 바꿨나]
//   참여자가 RoomStart 를 받으면 3초를 세고 떠났다(ULobbyWidgetBase::GuestTravelDelay).
//   그 3초에는 아무 근거가 없다. 방장이 큰 맵을 저사양 PC 에서 열면 3초로 모자라
//   참여자가 아직 없는 서버에 붙으려다 튕겼고, 반대로 금방 열려도 3초를 그냥 버렸다.
//
//   제대로 된 해법은 "다 열렸다" 를 실제로 관측해서 알리는 것이다. 그 관측이 여기다.
// ---------------------------------------------------------------------------

bool UServerSubsystem::IsListenServerUp() const
{
	const UGameInstance* GI = GetGameInstance();
	const UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return false;
	}

	// GetNetMode() 가 NM_ListenServer 를 돌려준다는 것은 넷드라이버가 이미 만들어져
	// 리스닝 중이라는 뜻이다. 넷드라이버를 한 번 더 확인하는 것은 방어용이다.
	if (World->GetNetMode() != NM_ListenServer)
	{
		return false;
	}

	return World->GetNetDriver() != nullptr;
}

void UServerSubsystem::PollListenServer(float DeltaTime)
{
	if (!bWaitingForListenServer)
	{
		return;
	}

	if (IsListenServerUp())
	{
		bWaitingForListenServer = false;

		UE_LOG(LogMOUServer, Log,
			TEXT("리슨서버가 열렸다(%.1f초 소요). 참여자에게 출발 신호를 보낸다."),
			ListenServerWaitSeconds);

		// ★ 여기서 이 안내를 찍는 이유. (2026-08-28)
		//
		//   리슨서버가 떴다는 것은 **이 PC 안에서** 포트가 열렸다는 뜻일 뿐이다.
		//   다른 네트워크의 참여자가 실제로 들어오려면 **이 PC 가 있는 네트워크의
		//   공유기**가 그 포트를 이 PC 로 넘겨줘야 한다. 그 둘은 완전히 다른 문제인데,
		//   화면에는 똑같이 "이동합니다" 만 뜨기 때문에 구분이 안 된다.
		//
		//   실제로 이것 때문에 하루를 썼다 — 서버 쪽 공유기에 포워딩을 넣어두고
		//   "포워딩은 했다" 고 생각했지만, 참여자의 게임 트래픽은 서버를 아예
		//   거치지 않고 방장에게 직접 온다. 열어야 할 공유기는 방장 쪽이었다.
		//
		//   그래서 무엇을 어디에 넣어야 하는지를 값까지 채워서 찍어둔다.
		LogListenServerReachability();

		ListenServerWaitSeconds = 0.f;
		Backend->NotifyHostReady();
		return;
	}

	ListenServerWaitSeconds += DeltaTime;

	const float Timeout = UMOUServerSettings::GetHostReadyTimeoutSeconds();
	if (ListenServerWaitSeconds >= Timeout)
	{
		bWaitingForListenServer = false;
		ListenServerWaitSeconds = 0.f;

		// [왜 그래도 신호를 보내지 않는가]
		//   여기까지 왔다는 것은 리슨서버가 뜨지 않았다는 뜻이다. 그런 상태에서 신호를
		//   보내면 참여자 전원이 죽은 주소로 달려가 각자 접속 실패를 본다.
		//   대기실에 남아 있으면 적어도 방장이 방을 나갈 때 사유와 함께 정리된다.
		UE_LOG(LogMOUServer, Error,
			TEXT("%.0f초 안에 리슨서버가 열리지 않았다. 참여자에게 출발 신호를 보내지 않는다. ")
			TEXT("맵을 여는 코드(OpenLevel 의 listen 옵션)를 확인할 것. ")
			TEXT("맵 로딩이 원래 오래 걸린다면 Project Settings -> Game -> MOU Server 의 ")
			TEXT("Host Ready Timeout Seconds 를 늘릴 것."),
			Timeout);
	}
}


void UServerSubsystem::PollGuestHostReadyTimeout(float DeltaTime)
{
	if (!bGuestWaitingForHostReady)
	{
		return;
	}

	GuestWaitSeconds += DeltaTime;

	// 방장 쪽 상한에 여유를 더한다. 방장이 제 시간에 열면 그 즉시 신호가 오므로
	// 이 값은 "대기 시간" 이 아니라 "이보다 오래면 뭔가 잘못된 것" 의 경계다.
	// 여유가 없으면 방장이 상한 직전에 성공했을 때 참여자가 먼저 포기해버린다.
	const float Timeout = UMOUServerSettings::GetHostReadyTimeoutSeconds() + 10.f;
	if (GuestWaitSeconds < Timeout)
	{
		return;
	}

	bGuestWaitingForHostReady = false;
	GuestWaitSeconds          = 0.f;

	// ★ 여기가 없으면 화면이 "방장이 서버를 여는 중입니다..." 로 영원히 굳는다.
	//   서버는 죽은 주소로 보내지 않으려고 일부러 신호를 안 보내는데(정당한 판단이다),
	//   그 침묵이 참여자에게는 무한 로딩과 구분되지 않는다. 침묵도 결과다.
	const FString Reason = FString::Printf(
		TEXT("%.0f초 안에 방장의 서버가 열리지 않았습니다.\n")
		TEXT("방장이 게임을 다시 시작하면 자동으로 이동합니다."),
		Timeout);

	UE_LOG(LogMOUServer, Warning, TEXT("[참여자] %s"), *Reason);
	OnTravelFailed.Broadcast(Reason);
}

// ---------------------------------------------------------------------------
// 여행 (2026-08-29: ULobbyWidgetBase 에서 옮겨왔다)
//
// 옮긴 이유는 헤더의 ConfigureTravel 위 주석에 적어뒀다. 요약하면
// "출발 신호를 받는 주체가 위젯이면, 위젯이 닫히는 순간 아무도 안 떠난다".
// ---------------------------------------------------------------------------

void UServerSubsystem::ConfigureTravel(const FString& InHostMapName, bool bInAutoTravel, bool bInPreloadMap)
{
	HostMapName             = InHostMapName;
	bAutoTravelOnGameStart  = bInAutoTravel;
	bPreloadMapWhileWaiting = bInPreloadMap;

	UE_LOG(LogMOUServer, Verbose,
		TEXT("여행 설정: 맵='%s' 자동이동=%s 미리올리기=%s"),
		*HostMapName,
		bAutoTravelOnGameStart ? TEXT("O") : TEXT("X"),
		bPreloadMapWhileWaiting ? TEXT("O") : TEXT("X"));
}

namespace
{
	/** "192.168.0.32" -> "192.168.0." (마지막 옥텟을 뗀 /24 접두사). 실패하면 빈 문자열. */
	FString MakeSlash24Prefix(const FString& Ipv4)
	{
		int32 LastDot = INDEX_NONE;
		if (!Ipv4.FindLastChar(TEXT('.'), LastDot) || LastDot <= 0)
		{
			return FString();
		}
		return Ipv4.Left(LastDot + 1);
	}
}

// ---------------------------------------------------------------------------
// 도달성 프로브 (v9)
//
// 흐름:
//   BeginReachabilityProbe  프로브 소켓을 게임 포트에 bind, 서버에 "쏴달라"
//   HostProbeSent 수신      서버가 쐈다. 여기서부터 센다
//   PollReachabilityProbe   매 틱 논블로킹으로 들여다본다
//   FinishReachabilityProbe 결과를 알리고 소켓을 닫는다
//
// ★ 소켓을 반드시 닫아야 한다. 열어둔 채 OpenLevel 하면 리슨서버가 같은 포트를
//   잡지 못한다. 그래서 닫는 경로를 FinishReachabilityProbe 하나로 모았다.
// ---------------------------------------------------------------------------

bool UServerSubsystem::EnsureGameSocket(int32 BasePort)
{
	if (GameSocket != nullptr)
	{
		return true;   // 이미 확보했다
	}
	if (BasePort <= 0 || BasePort > 65535)
	{
		return false;
	}

	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (Sockets == nullptr)
	{
		return false;
	}

	// ★ BasePort 가 막혀 있으면 다음 번호로 올라간다.
	//
	//   한 PC 에서 인스턴스를 둘 띄우는 테스트(같은 LAN 참여자 재현)가 이 폴백
	//   없이는 통째로 깨진다 — 둘이 같은 포트를 잡을 수 없기 때문이다.
	//   실제로 잡힌 번호를 서버에 등록하므로, 번호가 밀려도 홀펀칭은 그대로 된다.
	constexpr int32 MaxAttempts = 8;

	for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	{
		const int32 TryPort = BasePort + Attempt;

		FSocket* Socket = Sockets->CreateSocket(NAME_DGram, TEXT("MOU.GamePort"), FNetworkProtocolTypes::IPv4);
		if (Socket == nullptr)
		{
			return false;
		}

		Socket->SetNonBlocking(true);

		// ★ SetReuseAddr 를 쓰지 않는다.
		//   윈도우에서 SO_REUSEADDR 는 이미 쓰고 있는 UDP 포트에 두 번째 소켓이
		//   끼어들 수 있게 만든다. 그러면 들어온 패킷이 둘 중 하나에만 가고,
		//   프로브는 "안 왔다" 고 오판한다. 이미 쓰는 중이면 bind 를 실패시켜
		//   다음 번호로 넘어가는 편이 정직하다.

		TSharedRef<FInternetAddr> BindAddr = Sockets->CreateInternetAddr();
		BindAddr->SetAnyAddress();
		BindAddr->SetPort(TryPort);

		if (Socket->Bind(*BindAddr))
		{
			GameSocket       = Socket;
			ReservedGamePort = TryPort;

			// 넷드라이버가 ClientTravel 때 같은 포트를 쓰게 한다.
			// 이것이 없으면 서버가 관측한 엔드포인트와 실제 접속 출발지가 달라져
			// 방장이 엉뚱한 곳에 punch 하게 된다.
			UMOUIpNetDriver::SetDesiredClientPort(TryPort);

			UE_LOG(LogMOUServer, Log, TEXT("[게임포트] %d 확보."), TryPort);
			return true;
		}

		Sockets->DestroySocket(Socket);
	}

	UE_LOG(LogMOUServer, Warning,
		TEXT("[게임포트] %d~%d 이 전부 사용 중이다. 홀펀칭 없이 진행한다."),
		BasePort, BasePort + MaxAttempts - 1);
	return false;
}

void UServerSubsystem::CloseGameSocket()
{
	if (GameSocket == nullptr)
	{
		return;
	}

	if (ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
	{
		GameSocket->Close();
		Sockets->DestroySocket(GameSocket);
	}
	GameSocket = nullptr;

	UE_LOG(LogMOUServer, Log, TEXT("[게임포트] %d 를 놓아준다(넷드라이버가 쓸 차례)."), ReservedGamePort);
}

void UServerSubsystem::BeginReachabilityProbe(int32 Port)
{
	if (bProbing)
	{
		UE_LOG(LogMOUServer, Warning, TEXT("[프로브] 이미 진행 중이다."));
		return;
	}
	if (!Backend.IsValid() || Port <= 0 || Port > 65535)
	{
		return;
	}

	if (!EnsureGameSocket(Port))
	{
		FinishReachabilityProbe(false,
			FString::Printf(TEXT("포트 %d 부근을 열지 못했습니다. 다른 프로그램이 쓰고 있습니다."), Port));
		return;
	}

	ProbePort         = ReservedGamePort;
	// 0 은 "아직 없음" 과 구분이 안 되므로 피한다.
	ProbeNonce        = FMath::Max(1u, static_cast<uint32>(FMath::Rand()) ^ static_cast<uint32>(FPlatformTime::Cycles()));
	ProbeWaitSeconds  = 0.f;
	ProbeTotalSeconds = 0.f;
	ProbeRetrySeconds = 0.f;
	ProbeRequestAttempts = 1;
	bProbeDispatched  = false;
	bProbing          = true;

	UE_LOG(LogMOUServer, Log, TEXT("[프로브] 포트 %d 에서 대기 시작. 서버에 발사를 청한다(nonce %u)."),
		Port, ProbeNonce);

	Backend->RequestHostProbe(ReservedGamePort, ProbeNonce);
}

void UServerSubsystem::PollReachabilityProbe(float DeltaTime)
{
	if (!bProbing || GameSocket == nullptr)
	{
		return;
	}

	ProbeTotalSeconds += DeltaTime;
	ProbeRetrySeconds += DeltaTime;

	// AddPortMapping 성공 응답과 실제 NAT 규칙 적용 사이에 지연이 있는 공유기가 있다.
	// UDP 한 발만으로 판정하면 PIE를 다시 켠 직후 결과가 흔들리므로 같은 nonce로
	// 1초 간격 최대 5회 요청한다. 서버 응답은 작고 TCP 제어 패킷도 매우 작다.
	if (ProbeRetrySeconds >= 1.f && ProbeRequestAttempts < 5 && Backend.IsValid())
	{
		ProbeRetrySeconds = 0.f;
		++ProbeRequestAttempts;
		Backend->RequestHostProbe(ReservedGamePort, ProbeNonce);
		UE_LOG(LogMOUServer, Verbose, TEXT("[프로브] 외부 UDP 발사 재요청 %d/5"), ProbeRequestAttempts);
	}

	// 서버가 "쐈다" 고 답하기 전까지는 도착을 기대할 수 없다. 다만 그 답 자체가
	// 안 올 수도 있으므로(연결이 끊겼다든지) 전체 상한을 따로 둔다.
	if (!bProbeDispatched)
	{
		if (ProbeTotalSeconds >= 10.f)
		{
			FinishReachabilityProbe(false, TEXT("서버가 프로브 요청에 응답하지 않았습니다."));
		}
		return;
	}

	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (Sockets == nullptr)
	{
		FinishReachabilityProbe(false, TEXT("소켓 시스템을 찾지 못했습니다."));
		return;
	}

	// 논블로킹이라 안 왔으면 즉시 false 로 떨어진다. 매 틱 불러도 부담이 없다.
	uint8 Buffer[64];
	int32 Read = 0;
	TSharedRef<FInternetAddr> From = Sockets->CreateInternetAddr();

	while (GameSocket->RecvFrom(Buffer, sizeof(Buffer), Read, *From))
	{
		if (Read < static_cast<int32>(sizeof(MOU::HostProbeDatagram)))
		{
			continue;   // 우리 것이 아니다
		}

		MOU::HostProbeDatagram Datagram{};
		FMemory::Memcpy(&Datagram, Buffer, sizeof(Datagram));

		// ★ Magic 과 Nonce 를 둘 다 본다. 이 포트는 곧 게임 포트라 지나가던 다른
		//   트래픽이 들어올 수 있고, 지난 판의 늦은 응답이 올 수도 있다.
		if (Datagram.Magic != MOU::kHostProbeMagic || Datagram.Nonce != ProbeNonce)
		{
			continue;
		}

		FinishReachabilityProbe(true,
			FString::Printf(TEXT("외부(%s)에서 보낸 패킷이 도착했습니다."), *From->ToString(false)));
		return;
	}

	ProbeWaitSeconds += DeltaTime;

	// 공유기 규칙 반영 지연을 포함해 7초 동안 여러 발을 모두 못 받으면 실패다.
	if (ProbeWaitSeconds >= 7.f)
	{
		FinishReachabilityProbe(false,
			TEXT("공유기가 외부 접속을 넘겨주지 않습니다. 같은 공유기 안의 인원만 참여할 수 있습니다."));
	}
}

void UServerSubsystem::FinishReachabilityProbe(bool bReachable, const FString& Detail)
{
	// ★ v10 부터 소켓을 여기서 닫지 않는다.
	//
	//   게임 포트 소켓은 이제 셋이 공유한다(프로브 / 엔드포인트 등록 / 홀펀칭).
	//   프로브가 끝났다고 닫아버리면 NAT 에 뚫어둔 구멍이 그 자리에서 사라지고,
	//   등록해둔 엔드포인트도 무효가 된다. 닫는 것은 여행 직전 한 번뿐이다
	//   (TravelAsHost / TravelToHost / Deinitialize -> CloseGameSocket).
	bProbing          = false;
	bProbeDispatched  = false;
	ProbeWaitSeconds  = 0.f;
	ProbeTotalSeconds = 0.f;
	ProbeRetrySeconds = 0.f;
	ProbeRequestAttempts = 0;
	ProbeNonce        = 0;

	UE_CLOG(bReachable, LogMOUServer, Log,
		TEXT("[프로브] 외부에서 들어올 수 있다. %s"), *Detail);
	UE_CLOG(!bReachable, LogMOUServer, Warning,
		TEXT("[프로브] 외부에서 들어올 수 **없다**. %s"), *Detail);

	// ★ 결과를 보관한다. 지금 신고가 거부돼도(방이 아직 없으면 NotInRoom)
	//   RoomCreateAck 에서 다시 보낸다. 자세한 이유는 헤더의 bHasReachabilityResult 주석.
	bHasReachabilityResult = true;
	bLastReachable         = bReachable;

	// 서버에도 알려서 방에 표시한다. 참여자가 그것을 보고 헛걸음을 피한다.
	// 이미 방 안이면 이 한 번으로 끝나고, 아직 없으면 방이 생길 때 다시 간다.
	if (Backend.IsValid())
	{
		Backend->ReportReachability(bReachable);
	}

	OnReachabilityChecked.Broadcast(bReachable, Detail);
}

// ---------------------------------------------------------------------------
// UDP 홀펀칭 (v10)
// ---------------------------------------------------------------------------

void UServerSubsystem::RegisterGameEndpoint(int32 BasePort)
{
	// UserId 가 있어야 서버가 이 데이터그램을 어느 세션에 붙일지 안다.
	if (!LoginResult.bSuccess || LoginResult.UserId == 0)
	{
		return;
	}
	if (!EnsureGameSocket(BasePort))
	{
		UE_LOG(LogMOUServer, Warning,
			TEXT("[엔드포인트] 게임 포트를 확보하지 못해 등록을 건너뛴다. 홀펀칭 없이 진행한다."));
		return;
	}

	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (Sockets == nullptr || GameSocket == nullptr)
	{
		return;
	}

	FString ServerHost;
	int32   ServerPort = 0;
	UMOUServerSettings::ResolveEndpoint(ServerHost, ServerPort);

	TSharedRef<FInternetAddr> Dest = Sockets->CreateInternetAddr();
	bool bValid = false;
	Dest->SetIp(*ServerHost, bValid);
	Dest->SetPort(ServerPort);
	if (!bValid)
	{
		UE_LOG(LogMOUServer, Warning,
			TEXT("[엔드포인트] 서버 주소 '%s' 를 IP 로 해석하지 못했다. 등록을 건너뛴다."), *ServerHost);
		return;
	}

	// ★ 이 데이터그램의 목적은 내용 전달이 아니라 **출발지를 보이는 것**이다.
	//   서버는 recvfrom 으로 공인 IP:포트를 관측해서 방장에게 알려준다.
	//   그래서 주소를 싣지 않는다 — 실으면 위조할 수 있고, 관측값이면 못 한다.
	MOU::ClientEndpointDatagram Datagram{};
	Datagram.Magic  = MOU::kClientEndpointMagic;
	Datagram.Nonce  = FMath::Max(1u, static_cast<uint32>(FMath::Rand()) ^ static_cast<uint32>(FPlatformTime::Cycles()));
	Datagram.UserId = static_cast<uint64>(LoginResult.UserId);

	// UDP 라 한 발은 사라질 수 있다. 세 발 쏘는 값이 재전송 로직보다 훨씬 싸다.
	int32 Sent = 0;
	for (int32 i = 0; i < 3; ++i)
	{
		GameSocket->SendTo(reinterpret_cast<const uint8*>(&Datagram), sizeof(Datagram), Sent, *Dest);
	}

	UE_LOG(LogMOUServer, Log,
		TEXT("[엔드포인트] 포트 %d 에서 서버(%s:%d)로 등록 데이터그램을 보냈다."),
		ReservedGamePort, *ServerHost, ServerPort);
}

void UServerSubsystem::PunchTowardPeers()
{
	if (PunchTargets.Num() == 0)
	{
		return;
	}
	if (GameSocket == nullptr)
	{
		UE_LOG(LogMOUServer, Warning,
			TEXT("[홀펀칭] 게임 소켓이 없어 punch 를 못 한다. 외부 참여자가 못 들어올 수 있다."));
		return;
	}

	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (Sockets == nullptr)
	{
		return;
	}

	// 내용은 아무래도 좋다. NAT 에 "이 상대와 이야기 중" 이라는 자국을 남기는 것이
	// 전부다. 상대는 이것을 받지 못해도 상관없다 — 상대 NAT 이 막아도 우리 쪽
	// 구멍은 이미 뚫렸고, 필요한 것은 그것뿐이다.
	const MOU::HostProbeDatagram Payload{ MOU::kHostProbeMagic, 0 };

	for (const FMOUHostCandidate& Target : PunchTargets)
	{
		if (!Target.IsValid())
		{
			continue;
		}

		TSharedRef<FInternetAddr> Dest = Sockets->CreateInternetAddr();
		bool bValid = false;
		Dest->SetIp(*Target.Address, bValid);
		Dest->SetPort(Target.Port);
		if (!bValid)
		{
			continue;
		}

		// 여러 발 쏘는 이유는 유실 대비와, 상대가 아직 소켓을 안 열었을 때를 위해서다.
		int32 Sent = 0;
		for (int32 i = 0; i < 5; ++i)
		{
			GameSocket->SendTo(reinterpret_cast<const uint8*>(&Payload), sizeof(Payload), Sent, *Dest);
		}

		UE_LOG(LogMOUServer, Log, TEXT("[홀펀칭] %s:%d 로 구멍을 뚫었다."),
			*Target.Address, Target.Port);
	}
}

void UServerSubsystem::RegisterRelayRouteFromGameSocket(const FMOUGameRelayRoute& Route, bool bHost)
{
	if (!Route.IsValid())
	{
		return;  // relay가 꺼졌거나 이 방에 배정된 경로가 없다.
	}
	if (GameSocket == nullptr)
	{
		UE_LOG(LogMOUServer, Verbose,
			TEXT("[릴레이] 게임 소켓이 아직 없어 예열 등록은 건너뛴다. 실제 넷드라이버 소켓에서 다시 등록한다."));
		return;
	}

	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (Sockets == nullptr)
	{
		return;
	}

	TSharedRef<FInternetAddr> Destination = Sockets->CreateInternetAddr();
	bool bValidAddress = false;
	Destination->SetIp(*Route.Address, bValidAddress);
	Destination->SetPort(Route.Port);
	if (!bValidAddress)
	{
		UE_LOG(LogMOUServer, Warning, TEXT("[릴레이] 주소 '%s' 를 IP로 해석하지 못했다."), *Route.Address);
		return;
	}

	const MOU::RelayRegistrationDatagram Datagram = MOU::MakeRelayRegistrationDatagram(
		Route.RouteId, bHost ? MOU::ERelayPeerRole::Host : MOU::ERelayPeerRole::Guest,
		Route.Token.GetData());
	int32 Sent = 0;
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		GameSocket->SendTo(reinterpret_cast<const uint8*>(&Datagram), sizeof(Datagram), Sent, *Destination);
	}

	UE_LOG(LogMOUServer, Log, TEXT("[릴레이] 게임 포트 %d 에서 %s 경로를 예열 등록했다: %s"),
		ReservedGamePort, bHost ? TEXT("host") : TEXT("guest"), *Route.ToGuestDisplayString());
}

FString UServerSubsystem::GetLocalLanAddress()
{
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (Sockets == nullptr)
	{
		return FString();
	}

	// GetLocalHostAddr 은 "대표" 주소 하나를 준다. 리슨서버가 실제로 바인드하는
	// 인터페이스와 같은 값이라, LogListenServerReachability 가 찍는 값과도 일치한다.
	bool bCanBindAll = false;
	const TSharedPtr<FInternetAddr> Local = Sockets->GetLocalHostAddr(*GLog, bCanBindAll);
	if (!Local.IsValid())
	{
		return FString();
	}

	const FString Address = Local->ToString(/*bAppendPort=*/false);

	// 사설 대역이 아니면 신고하지 않는다. 어차피 서버가 걸러내고, 보내봐야
	// "사설이 아닌 주소를 LAN 후보로 신고했다" 는 거부 로그만 남는다.
	if (Address.StartsWith(TEXT("127.")) || Address.IsEmpty())
	{
		return FString();
	}
	return Address;
}

FMOUHostCandidate UServerSubsystem::ChooseHostCandidate(const TArray<FMOUHostCandidate>& Candidates, bool bHostLanOnly, int32& OutIndex)
{
	OutIndex = INDEX_NONE;

	if (Candidates.Num() == 0)
	{
		return FMOUHostCandidate();
	}

	// 1순위: 나와 같은 /24 안에 있는 LAN 후보.
	//
	// ★ Kind 만 보고 고르지 않는다. 다른 사무실의 192.168.0.x 를 신고받을 수도 있고,
	//   그 주소는 내 LAN 의 엉뚱한 기기를 가리킨다. 내 어댑터와 실제로 비교해야 한다.
	TArray<FString> MyPrefixes;
	if (ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
	{
		TArray<TSharedPtr<FInternetAddr>> Adapters;
		if (Sockets->GetLocalAdapterAddresses(Adapters))
		{
			for (const TSharedPtr<FInternetAddr>& Adapter : Adapters)
			{
				if (Adapter.IsValid() && Adapter->GetProtocolType() == FNetworkProtocolTypes::IPv4)
				{
					const FString Prefix = MakeSlash24Prefix(Adapter->ToString(false));
					if (!Prefix.IsEmpty() && !Prefix.StartsWith(TEXT("127.")))
					{
						MyPrefixes.AddUnique(Prefix);
					}
				}
			}
		}
	}

	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FMOUHostCandidate& C = Candidates[Index];
		if (C.Kind != EMOUHostAddrKindBP::Lan || !C.IsValid())
		{
			continue;
		}
		if (MyPrefixes.Contains(MakeSlash24Prefix(C.Address)))
		{
			UE_LOG(LogMOUServer, Log,
				TEXT("[여행] 방장이 나와 같은 LAN 에 있다. %s 로 직접 붙는다(공유기를 거치지 않는다)."),
				*C.ToDisplayString());
			OutIndex = Index;
			return C;
		}
	}

	// 2·3순위: 공인 후보와 홀펀칭 후보. **어느 쪽을 먼저 볼지는 방의 상태가 정한다.**
	//
	//   bHostLanOnly=false : 방장이 정적으로 열려 있다(포워딩/UPnP). 그 길이 확실하다.
	//                        Punch 는 구멍이 제때 뚫렸는지에 달려 있어 덜 확실하다.
	//   bHostLanOnly=true  : 프로브가 "정적 인바운드는 죽었다" 고 판정했다.
	//                        공인 후보를 먼저 시도하면 타임아웃을 통째로 버린다.
	//
	// ★ 이 순서를 고정으로 두면 반드시 한쪽이 깨진다.
	//   실제로 관측 포트로 공인 후보를 **덮었다가** 수동 포워딩 방장(잘 되던 조합)을
	//   깼다 — 그 포트에는 포워딩이 없어서 아무도 못 들어왔다.
	//   두 길은 성격이 다르므로 둘 다 남기고, 고르는 기준만 상태에 맡긴다.
	const EMOUHostAddrKindBP FirstKind  = bHostLanOnly ? EMOUHostAddrKindBP::Punch  : EMOUHostAddrKindBP::Public;
	const EMOUHostAddrKindBP SecondKind = bHostLanOnly ? EMOUHostAddrKindBP::Public : EMOUHostAddrKindBP::Punch;

	for (const EMOUHostAddrKindBP Wanted : { FirstKind, SecondKind })
	{
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			if (Candidates[Index].Kind == Wanted && Candidates[Index].IsValid())
			{
				UE_LOG(LogMOUServer, Log, TEXT("[여행] %s 로 간다. (방장 정적 개방=%s)"),
					*Candidates[Index].ToDisplayString(),
					bHostLanOnly ? TEXT("없음") : TEXT("있음"));
				OutIndex = Index;
				return Candidates[Index];
			}
		}
	}

	// 마지막: 남은 아무 것. 여기까지 오는 것은 서버가 예상 밖의 조합을 보낸 경우다.
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (Candidates[Index].IsValid())
		{
			OutIndex = Index;
			return Candidates[Index];
		}
	}

	return FMOUHostCandidate();
}

void UServerSubsystem::SetRoomPassword(const FString& InRoomPassword)
{
	// 빈 값도 받는다 — 공개방으로 바뀌는 것이 정상적인 경우다.
	TravelRoomPassword = IsValidRoomPassword(InRoomPassword) ? InRoomPassword : FString();
}

void UServerSubsystem::TravelAsHost()
{
	if (HostMapName.IsEmpty())
	{
		// 맵을 정하지 않았다면 여행은 게임 쪽(블루프린트/게임모드)의 몫이다.
		UE_LOG(LogMOUServer, Log,
			TEXT("게임 시작됨. HostMapName 이 비어 있어 레벨은 열지 않는다 — BP 가 연다면 정상이다."));
		return;
	}

	UGameInstance* GI = GetGameInstance();
	if (GI == nullptr)
	{
		return;
	}

	// ★ 프로브가 아직 돌고 있으면 그 소켓이 게임 포트를 잡고 있다.
	//   그대로 OpenLevel 하면 리슨서버가 bind 에 실패하고, 그러면 참여자에게
	//   출발 신호가 영영 안 나간다. 확인보다 게임 시작이 우선이므로 접는다.
	//
	//   보통은 방을 만든 뒤 전원이 준비할 때까지 시간이 충분해서 여기 걸리지 않는다.
	//   혼자 만들고 바로 시작하는 경우에만 해당한다.
	if (bProbing)
	{
		UE_LOG(LogMOUServer, Warning,
			TEXT("[프로브] 아직 확인 중인데 게임이 시작됐다. 포트를 놓아주려고 확인을 접는다."));
		FinishReachabilityProbe(false, TEXT("게임이 먼저 시작되어 확인을 마치지 못했습니다."));
	}

	// 실제 listen socket 이 bind 된 직후 host capability 를 다시 보내게 예약한다.
	// GameSocket 예열 뒤 close/rebind 사이에 NAT 매핑이 바뀌어도 이 등록이 진짜다.
	TArray<FMOUPendingRelayRegistration> HostRegistrations;
	for (const FMOUGameRelayRoute& Route : PendingHostRelayRoutes)
	{
		if (!Route.IsValid())
		{
			continue;
		}
		FMOUPendingRelayRegistration Registration;
		Registration.Address = Route.Address;
		Registration.Port    = Route.Port;
		Registration.RouteId = Route.RouteId;
		Registration.Token   = Route.Token;
		HostRegistrations.Add(MoveTemp(Registration));
	}
	UMOUIpNetDriver::SetPendingHostRelayRegistrations(HostRegistrations);
	UMOUIpNetDriver::SetDesiredListenPort(ReservedGamePort);

	// ★ 순서가 중요하다: punch 를 **먼저**, 소켓 닫기를 그 다음.
	//
	//   punch 는 게임 포트에 bind 된 이 소켓에서 나가야 NAT 구멍이 그 포트에 뚫린다.
	//   소켓을 먼저 닫으면 쏠 곳이 없고, 리슨서버가 연 소켓으로 나중에 쏘면
	//   그때는 이미 참여자가 접속을 시도한 뒤다.
	PunchTowardPeers();
	CloseGameSocket();

	// listen 옵션이 있어야 이 클라이언트가 리슨서버가 된다.
	// RoomPassword 를 URL 에 같이 실어야 새 레벨의 GameMode 가 InitGame 에서
	// 그 값을 읽어 보관하고, 나중에 PreLogin 에서 참여자를 검사할 수 있다.
	FString Options = TEXT("listen");
	if (!TravelRoomPassword.IsEmpty())
	{
		Options += FString::Printf(TEXT("?RoomPassword=%s"), *TravelRoomPassword);
	}

	UE_LOG(LogMOUServer, Log, TEXT("[방장] 리슨서버로 '%s' 를 연다."), *HostMapName);
	UGameplayStatics::OpenLevel(GI, FName(*HostMapName), /*bAbsolute=*/true, Options);
}

bool UServerSubsystem::TravelToHost()
{
	if (!PendingHostReady.bSuccess)
	{
		UE_LOG(LogMOUServer, Warning,
			TEXT("[참여자] 아직 출발 신호를 못 받았다. 이동하지 않는다."));
		return false;
	}

	UGameInstance* GI = GetGameInstance();
	APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
	if (PC == nullptr)
	{
		// ★ 위젯의 GetOwningPlayer() 를 쓰지 않는 이유가 이것이다.
		//   위젯이 없어도 여행은 되어야 한다. GameInstance 는 늘 살아 있다.
		UE_LOG(LogMOUServer, Error,
			TEXT("[참여자] 로컬 플레이어 컨트롤러가 없어 이동할 수 없다."));
		return false;
	}

	int32 ChosenIndex = INDEX_NONE;
	const FMOUHostCandidate Chosen = ChooseHostCandidate(PendingHostReady.Candidates, PendingHostReady.bLanOnly, ChosenIndex);
	if (!Chosen.IsValid())
	{
		UE_LOG(LogMOUServer, Error, TEXT("[참여자] 쓸 수 있는 호스트 주소가 없다: %s"),
			*PendingHostReady.ToDisplayString());
		OnTravelFailed.Broadcast(TEXT("방장의 접속 주소를 받지 못했습니다."));
		return false;
	}

	// ★ 방장이 도달성 프로브에 실패했는데 내가 공인 후보를 골랐다면, 예전에는
	//   이 접속이 **반드시** 실패했다. 그래서 시도하지 않고 바로 사유를 띄웠다.
	//
	// [v10 에서 조건이 하나 붙었다]
	//   홀펀칭은 바로 그 "미요청 인바운드가 막힌" 경우를 뚫는 기법이다.
	//   내가 엔드포인트를 등록했다면 방장이 나에게 punch 했을 것이고, 그러면
	//   막혀 있던 길이 열려 있을 수 있다. 그때는 시도해봐야 한다 —
	//   여기서 막으면 5단계가 고치려는 경우를 4단계가 막아서는 꼴이 된다.
	//   (실제로 그 충돌 때문에 punch 가 시도조차 안 됐다)
	//
	//   등록을 못 했으면 방장은 나에게 쏠 곳을 몰랐다는 뜻이므로 예전대로 즉시 실패한다.
	const bool bMayHavePunchedHole = !ObservedEndpoint.IsEmpty();

	if (PendingHostReady.bLanOnly && Chosen.Kind != EMOUHostAddrKindBP::Lan && !bMayHavePunchedHole)
	{
		if (TryRelayFallback())
		{
			UE_LOG(LogMOUServer, Log, TEXT("[참여자] 방장이 LAN 전용으로 판정되어 직접 후보 대신 relay로 이동한다."));
			return true;
		}

		const FString Reason = TEXT(
			"이 방은 같은 공유기 안에서만 참여할 수 있습니다.\n"
			"방장 쪽 공유기가 외부 접속을 넘겨주지 않습니다.");
		UE_LOG(LogMOUServer, Warning, TEXT("[참여자] %s"), *Reason);
		OnTravelFailed.Broadcast(Reason);
		return false;
	}

	// 실패하면 다음 후보로 넘어갈 수 있게 어디까지 써봤는지 남긴다.
	TriedCandidateIndex = ChosenIndex;
	TriedCandidateIndices.Reset();
	TriedCandidateIndices.Add(ChosenIndex);
	ActiveTravelTransport = EMOUTravelTransport::Direct;
	bRelayFallbackTried = false;

	UE_LOG(LogMOUServer, Log, TEXT("[참여자] 후보 %d개 중 %s 선택 (전체: %s)"),
		PendingHostReady.Candidates.Num(), *Chosen.ToDisplayString(),
		*PendingHostReady.ToDisplayString());

	// ★ 넷드라이버가 같은 포트를 잡아야 한다. 우리가 들고 있으면 bind 가 실패하고,
	//   그러면 엔진이 임시 포트로 떨어져 방장이 뚫어둔 구멍을 못 쓰게 된다.
	//   (UMOUIpNetDriver 가 ReservedGamePort 를 쓰도록 이미 설정돼 있다)
	CloseGameSocket();

	// 어디로 떠나는지 남겨둔다. 접속에 실패하면 그 주소를 그대로 넣어
	// "어디에 못 붙었는지" 를 말해줄 수 있다.
	NotifyTravelingTo(Chosen.Address, Chosen.Port);

	// MakeTravelURL 이 "IP:포트?RoomPassword=1234" 를 만들어준다.
	PC->ClientTravel(Chosen.MakeTravelURL(TravelRoomPassword), ETravelType::TRAVEL_Absolute);
	return true;
}

bool UServerSubsystem::TryNextHostCandidate()
{
	if (!PendingHostReady.bSuccess || PendingHostReady.Candidates.Num() <= 1)
	{
		return false;
	}

	// ChooseHostCandidate 가 LAN 항목을 골랐을 때 그보다 앞의 공인 항목도 반드시
	// 한 번은 시도해야 한다. 끝에만 가는 옛 루프는 그 항목을 영영 건너뛰었다.
	const int32 CandidateCount = PendingHostReady.Candidates.Num();
	for (int32 Offset = 1; Offset < CandidateCount; ++Offset)
	{
		const int32 Index = (TriedCandidateIndex + Offset + CandidateCount) % CandidateCount;
		const FMOUHostCandidate& Next = PendingHostReady.Candidates[Index];
		if (!Next.IsValid() || TriedCandidateIndices.Contains(Index))
		{
			continue;
		}

		// 처음 선택되지 않은 LAN 후보는 이 PC의 어댑터와 같은 /24가 아니다.
		// 다른 집의 192.168.x.x를 시도하며 시간을 버리거나 엉뚱한 장비에 보내지 않는다.
		if (Next.Kind == EMOUHostAddrKindBP::Lan)
		{
			TriedCandidateIndices.Add(Index);
			continue;
		}

		UGameInstance* GI = GetGameInstance();
		APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
		if (PC == nullptr)
		{
			return false;
		}

		TriedCandidateIndex = Index;
		TriedCandidateIndices.Add(Index);

		// ★ 이 폴백이 없으면 후보 선택이 한 번 빗나갈 때마다 접속이 통째로 실패한다.
		//   /24 판정은 넷마스크를 모르고 하는 추측이라 빗나갈 수 있다. 빗나가도
		//   결과가 "예전과 같음" 에 머물게 하는 것이 이 함수의 존재 이유다.
		UE_LOG(LogMOUServer, Log, TEXT("[참여자] 다음 후보로 다시 시도한다: %s"),
			*Next.ToDisplayString());

		NotifyTravelingTo(Next.Address, Next.Port);
		PC->ClientTravel(Next.MakeTravelURL(TravelRoomPassword), ETravelType::TRAVEL_Absolute);
		return true;
	}

	return false;
}

bool UServerSubsystem::TryRelayFallback()
{
	if (bRelayFallbackTried || !PendingGuestRelayRoute.IsValid())
	{
		return false;
	}

	UGameInstance* GI = GetGameInstance();
	APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
	if (PC == nullptr)
	{
		return false;
	}

	FMOUPendingRelayRegistration Registration;
	Registration.Address = PendingGuestRelayRoute.Address;
	Registration.Port    = PendingGuestRelayRoute.Port;
	Registration.RouteId = PendingGuestRelayRoute.RouteId;
	Registration.Token   = PendingGuestRelayRoute.Token;
	UMOUIpNetDriver::SetPendingClientRelayRegistration(Registration);

	bRelayFallbackTried = true;
	ActiveTravelTransport = EMOUTravelTransport::Relay;
	NotifyTravelingTo(PendingGuestRelayRoute.Address, PendingGuestRelayRoute.Port);

	UE_LOG(LogMOUServer, Log, TEXT("[릴레이] 직접 후보가 실패해 %s 로 폴백한다."),
		*PendingGuestRelayRoute.ToGuestDisplayString());
	PC->ClientTravel(PendingGuestRelayRoute.MakeGuestTravelURL(TravelRoomPassword),
		ETravelType::TRAVEL_Absolute);
	return true;
}

// ---------------------------------------------------------------------------
// 맵 미리 올리기 (2026-08-26)
//
// 두 박자 구조에서 참여자는 "방장이 맵을 다 연 뒤에" 자기 맵을 로드했다.
// 두 로딩이 앞뒤로 붙어 있어 실제 대기 시간이 둘의 합이었다.
// 1박자 동안 참여자는 놀고 있으므로, 그 시간에 미리 올려 둘을 겹친다.
// ---------------------------------------------------------------------------

void UServerSubsystem::BeginPreloadMap(const FString& MapName)
{
	if (MapName.IsEmpty() || PreloadedMapName == MapName)
	{
		return;   // 이미 이 맵을 올리는 중이거나 올려뒀다
	}

	// 다른 맵을 새로 올리는 것이므로 이전 것은 놓아준다.
	ReleasePreloadedMap();

	// 짧은 이름("L_Game")으로 적어둔 경우가 흔하다. LoadPackageAsync 는 패키지
	// **경로**를 받으므로 실제 경로를 먼저 찾는다. 못 찾으면 조용히 포기한다 —
	// 여기서 실패해도 여행은 정상으로 진행되므로 오류가 아니다.
	FString PackagePath = MapName;
	if (!PackagePath.StartsWith(TEXT("/")))
	{
		if (!FPackageName::SearchForPackageOnDisk(MapName + FPackageName::GetMapPackageExtension(), &PackagePath))
		{
			UE_LOG(LogMOUServer, Verbose,
				TEXT("맵 미리 올리기 건너뜀: '%s' 의 패키지 경로를 찾지 못했다."), *MapName);
			return;
		}
	}

	PreloadedMapName = MapName;

	UE_LOG(LogMOUServer, Log,
		TEXT("방장이 맵을 여는 동안 '%s' 를 미리 올린다."), *PackagePath);

	TWeakObjectPtr<UServerSubsystem> WeakThis(this);
	LoadPackageAsync(PackagePath,
		FLoadPackageAsyncDelegate::CreateLambda(
			[WeakThis](const FName& /*PackageName*/, UPackage* LoadedPackage, EAsyncLoadingResult::Type Result)
			{
				if (!WeakThis.IsValid())
				{
					return;
				}

				if (Result == EAsyncLoadingResult::Succeeded && LoadedPackage != nullptr)
				{
					WeakThis->PreloadedMapPackage = LoadedPackage;
					UE_LOG(LogMOUServer, Log, TEXT("맵 미리 올리기 완료. 여행이 즉시 끝난다."));
				}
				else
				{
					// 실패를 표시로 남기지 않는다. 다음에 다시 시도할 수 있어야 한다.
					WeakThis->PreloadedMapName.Reset();
					UE_LOG(LogMOUServer, Verbose, TEXT("맵 미리 올리기 실패. 여행은 그대로 진행한다."));
				}
			}));
}

void UServerSubsystem::ReleasePreloadedMap()
{
	PreloadedMapPackage = nullptr;
	PreloadedMapName.Reset();
}


// ---------------------------------------------------------------------------
// 접속 실패를 사람이 읽을 수 있게 (2026-08-28)
//
// 참여자가 방장에게 못 붙어도 화면에는 "이동합니다..." 만 남아 있었다.
// 엔진은 이미 실패를 알고 있으므로(ENetworkFailure) 받아서 번역만 하면 된다.
// ---------------------------------------------------------------------------

void UServerSubsystem::NotifyTravelingTo(const FString& HostAddress, int32 HostPort)
{
	PendingTravelAddress = FString::Printf(TEXT("%s:%d"), *HostAddress, HostPort);
	TravelAttemptSeconds = 0.f;

	UE_LOG(LogMOUServer, Log, TEXT("[참여자] %s 로 접속을 시도한다."), *PendingTravelAddress);
}

bool UServerSubsystem::IsTravelConnectionOpen() const
{
	const UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return false;
	}

	auto IsDriverOpen = [](const UNetDriver* Driver)
	{
		return Driver != nullptr && Driver->ServerConnection != nullptr &&
			Driver->ServerConnection->GetConnectionState() == USOCK_Open;
	};

	if (IsDriverOpen(World->GetNetDriver()))
	{
		return true;
	}

	// ClientTravel 중에는 NetDriver가 아직 현재 World에 붙지 않고
	// PendingNetGame에 있다. 이것도 확인해야 느린 맵 로딩을 실패로 오판하지 않는다.
	if (GEngine != nullptr)
	{
		if (const FWorldContext* Context = GEngine->GetWorldContextFromWorld(World))
		{
			return Context->PendingNetGame != nullptr &&
				IsDriverOpen(Context->PendingNetGame->NetDriver);
		}
	}
	return false;
}

void UServerSubsystem::PollTravelConnection(float DeltaTime)
{
	if (ActiveTravelTransport == EMOUTravelTransport::None)
	{
		TravelAttemptSeconds = 0.f;
		return;
	}

	if (IsTravelConnectionOpen())
	{
		UE_LOG(LogMOUServer, Log, TEXT("[접속 성공] %s 경로로 서버 연결이 열렸다."),
			ActiveTravelTransport == EMOUTravelTransport::Relay ? TEXT("릴레이") : TEXT("직접"));
		PendingTravelAddress.Reset();
		ActiveTravelTransport = EMOUTravelTransport::None;
		TravelAttemptSeconds = 0.f;
		return;
	}

	TravelAttemptSeconds += FMath::Max(0.f, DeltaTime);
	const float Timeout = ActiveTravelTransport == EMOUTravelTransport::Relay
		? kRelayAttemptTimeoutSeconds : kDirectAttemptTimeoutSeconds;
	if (TravelAttemptSeconds < Timeout)
	{
		return;
	}

	if (ActiveTravelTransport == EMOUTravelTransport::Direct)
	{
		UE_LOG(LogMOUServer, Warning,
			TEXT("[직접 연결] %.0f초 동안 handshake 응답이 없어 다음 경로로 전환한다: %s"),
			Timeout, *PendingTravelAddress);
		if (TryNextHostCandidate() || TryRelayFallback())
		{
			return;
		}
	}
	else
	{
		UE_LOG(LogMOUServer, Error,
			TEXT("[릴레이 실패] %.0f초 동안 handshake 응답이 없다: %s"),
			Timeout, *PendingTravelAddress);
	}

	const FString Reason = ActiveTravelTransport == EMOUTravelTransport::Relay
		? TEXT("직접 연결과 자체 UDP 릴레이 모두 응답하지 않았습니다. relay 포트포워딩과 방화벽을 확인하세요.")
		: TEXT("방장의 직접 연결 후보와 자체 UDP 릴레이를 모두 사용할 수 없습니다.");
	PendingTravelAddress.Reset();
	ActiveTravelTransport = EMOUTravelTransport::None;
	TravelAttemptSeconds = 0.f;
	UMOUIpNetDriver::ClearPendingRelayRegistrations();
	OnTravelFailed.Broadcast(Reason);
}

void UServerSubsystem::HandleNetworkFailure(UWorld* /*World*/, UNetDriver* /*NetDriver*/,
                                            ENetworkFailure::Type FailureType,
                                            const FString& ErrorString)
{
	const FString Target = PendingTravelAddress.IsEmpty()
		? TEXT("호스트") : PendingTravelAddress;

	FString Reason;

	switch (FailureType)
	{
	case ENetworkFailure::ConnectionTimeout:
	case ENetworkFailure::FailureReceived:
	case ENetworkFailure::PendingConnectionFailure:
		// 압도적으로 흔한 경우다. 주소는 맞는데 그 주소로 패킷이 안 들어가는 것.
		// 무엇을 해야 하는지까지 적어야 화면만 보고도 해결할 수 있다.
		Reason = FString::Printf(
			TEXT("%s 에 접속하지 못했습니다.\n")
			TEXT("방장 PC 가 있는 네트워크의 공유기에 'UDP 7777 -> 방장 PC' 포트포워딩이 필요합니다.\n")
			TEXT("(로그인 서버 쪽 공유기 설정과는 다른 곳입니다)"),
			*Target);
		break;

	case ENetworkFailure::OutdatedClient:
	case ENetworkFailure::OutdatedServer:
		Reason = TEXT("방장과 게임 버전이 다릅니다. 같은 빌드로 맞춰야 합니다.");
		break;

	default:
		Reason = FString::Printf(TEXT("%s 접속 중 네트워크 오류가 났습니다: %s"),
			*Target, *ErrorString);
		break;
	}

	UE_LOG(LogMOUServer, Error, TEXT("[접속 실패] %s (엔진 사유: %d / %s)"),
		*Reason, static_cast<int32>(FailureType), *ErrorString);

	PendingTravelAddress.Reset();

	// ★ relay 자체의 실패를 다시 direct 후보로 되돌리면 무한 루프가 된다.
	// 직접 경로가 실패했을 때만 남은 직접 후보를 돌고, 그 뒤 relay를 정확히 한 번 쓴다.
	if (ActiveTravelTransport == EMOUTravelTransport::Direct && TryNextHostCandidate())
	{
		return;
	}
	if (ActiveTravelTransport == EMOUTravelTransport::Direct && TryRelayFallback())
	{
		return;
	}

	if (ActiveTravelTransport == EMOUTravelTransport::Relay)
	{
		Reason += TEXT("\n직접 연결과 자체 UDP 릴레이 모두 연결되지 않았습니다. 서버 relay UDP 포트 범위의 포워딩과 방화벽을 확인하세요.");
	}
	ActiveTravelTransport = EMOUTravelTransport::None;
	TravelAttemptSeconds = 0.f;

	OnTravelFailed.Broadcast(Reason);
}

void UServerSubsystem::HandleTravelFailure(UWorld* /*World*/,
                                           ETravelFailure::Type FailureType,
                                           const FString& ErrorString)
{
	// 여기까지 오는 것은 대개 맵 문제다(이름이 틀렸거나 쿠킹에서 빠졌거나).
	// 네트워크 실패와 구분해서 말해야 엉뚱한 곳을 뒤지지 않는다.
	const FString Reason = FString::Printf(
		TEXT("레벨 이동에 실패했습니다: %s\n맵 이름(HostMapName)이 맞는지 확인하세요."),
		*ErrorString);

	UE_LOG(LogMOUServer, Error, TEXT("[이동 실패] %s (사유 %d)"),
		*Reason, static_cast<int32>(FailureType));

	OnTravelFailed.Broadcast(Reason);
	PendingTravelAddress.Reset();
	ActiveTravelTransport = EMOUTravelTransport::None;
	TravelAttemptSeconds = 0.f;
	UMOUIpNetDriver::ClearPendingRelayRegistrations();
}
void UServerSubsystem::LogListenServerReachability() const
{
	// GetLocalAddr() 이 non-const 라 여기서도 const 를 걸지 않는다.
	UWorld*     World     = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	UNetDriver* NetDriver = World ? World->GetNetDriver() : nullptr;

	// 리슨서버가 실제로 듣고 있는 포트. 설정값(7777)을 그대로 찍으면 거짓말이 될 수 있다 —
	// PIE 나 실행 인자로 다른 포트가 잡히는 경우가 있기 때문이다.
	int32 ListenPort = 0;
	if (NetDriver != nullptr)
	{
		const TSharedPtr<const FInternetAddr> LocalAddr = NetDriver->GetLocalAddr();
		if (LocalAddr.IsValid())
		{
			ListenPort = LocalAddr->GetPort();
		}
	}

	// 이 PC 의 LAN IP. 공유기 설정 화면의 "내부 IP 주소" 칸에 그대로 넣을 값이다.
	FString LanIp = TEXT("<이 PC 의 LAN IP>");
	if (ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
	{
		bool bCanBindAll = false;
		const TSharedPtr<FInternetAddr> Local = Sockets->GetLocalHostAddr(*GLog, bCanBindAll);
		if (Local.IsValid())
		{
			LanIp = Local->ToString(/*bAppendPort=*/false);
		}
	}

	if (ListenPort <= 0)
	{
		UE_LOG(LogMOUServer, Warning,
			TEXT("리슨서버 포트를 확인하지 못했다. 참여자가 못 들어오면 넷드라이버 상태를 먼저 볼 것."));
		return;
	}

	UE_LOG(LogMOUServer, Log,
		TEXT("[방장] 리슨서버가 %s:%d 에서 듣고 있다."), *LanIp, ListenPort);
	UE_LOG(LogMOUServer, Log,
		TEXT("[방장] 다른 네트워크의 참여자가 들어오려면 **이 PC 의 공유기**에 다음이 있어야 한다:"));
	UE_LOG(LogMOUServer, Log,
		TEXT("[방장]     외부 UDP %d  ->  %s : %d"), ListenPort, *LanIp, ListenPort);
	UE_LOG(LogMOUServer, Log,
		TEXT("[방장] 로그인 서버 쪽 공유기에 넣은 포워딩은 이 경로와 무관하다 — ")
		TEXT("게임 트래픽은 서버를 거치지 않고 참여자가 여기로 직접 붙는다."));
}
void UServerSubsystem::SetConnectionState(EChatConnectionState NewState, const FString& Detail)
{
	if (ConnectionState == NewState)
	{
		return;   // 같은 상태를 반복 브로드캐스트하지 않는다 (재연결 시도 중 로그 폭주 방지)
	}

	ConnectionState = NewState;
	OnChatStateChanged.Broadcast(NewState, Detail);
}

// ---------------------------------------------------------------------------
// 콘솔 명령 - UI 가 없는 4단계에서 동작을 검증하기 위한 것
//
// PIE 에서 ` 키를 눌러 콘솔을 열고 아래 명령을 입력한다.
//   MOU.Chat.Server                    (지금 어느 서버를 보고 있는지 확인 — 접속 문제는 여기부터)
//   MOU.Chat.Connect                  (설정된 서버로 접속. 주소를 직접 줄 수도 있다)
//   MOU.Chat.SetServer 192.168.0.32 9000   (이 PC 에만 다른 주소 저장, 인자 없으면 초기화)
//   MOU.Chat.Register player1 secret123 홍길동
//   MOU.Chat.Login player1 secret123 0
//   MOU.Chat.Say 0 안녕하세요          (첫 인자가 채널: 0=전체 1=팀 2=사망)
//   MOU.Chat.Dead 1
//   MOU.Chat.Disconnect
//
// 5단계에서 UMG 가 붙어도 이 명령들은 디버깅용으로 남겨둔다.
// ---------------------------------------------------------------------------

namespace
{
	/**
	 * 콘솔 명령이 실행된 월드에서 채팅 서브시스템을 찾는다.
	 * PIE 창이 여러 개면 "지금 콘솔을 연 창" 의 것이 잡힌다.
	 */
	UServerSubsystem* FindServerSubsystem(UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UServerSubsystem>() : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs GChatConnectCommand(
		TEXT("MOU.Chat.Connect"),
		TEXT("서버에 접속한다. 인자를 생략하면 설정된 서버로 붙는다. 사용법: MOU.Chat.Connect [호스트] [포트]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UServerSubsystem* Chat = FindServerSubsystem(World))
				{
					// 인자를 생략하면 빈 값을 넘긴다 -> ConnectToChatServer 가 설정에서 읽는다.
					// 예전처럼 127.0.0.1 을 기본값으로 두면 콘솔로 테스트할 때마다
					// 자기 PC 로 붙어버려서 다시 같은 함정에 빠진다.
					const FString HostArg = Args.IsValidIndex(0) ? Args[0] : FString();
					const int32   PortArg = Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : 0;
					Chat->ConnectToChatServer(HostArg, PortArg);
				}
			}));

	/**
	 * 지금 어느 서버를 보고 있는지 확인한다. "왜 나만 접속이 안 되지?" 를
	 * 제일 빨리 가르는 명령이라 따로 뒀다.
	 */
	FAutoConsoleCommand GServerCommand(
		TEXT("MOU.Chat.Server"),
		TEXT("현재 설정된 서버 주소와 그 출처를 출력한다."),
		FConsoleCommandDelegate::CreateLambda(
			[]()
			{
				UE_LOG(LogMOUServer, Log, TEXT("서버: %s"), *UMOUServerSettings::GetResolvedEndpointText());
			}));

	/**
	 * 이 PC 에만 다른 서버 주소를 저장한다. 팀 공유 설정(DefaultGame.ini)은 건드리지 않는다.
	 * 인자를 주지 않으면 개인 설정을 지우고 팀 공유 값으로 되돌린다.
	 */
	FAutoConsoleCommandWithWorldAndArgs GChatSetServerCommand(
		TEXT("MOU.Chat.SetServer"),
		TEXT("이 PC 에만 서버 주소를 저장하고 다시 접속한다. 사용법: MOU.Chat.SetServer <호스트> [포트] (인자 없으면 초기화)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (Args.Num() == 0)
				{
					UMOUServerSettings::ClearEndpointOverrideForThisMachine();
				}
				else
				{
					const int32 PortArg = Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : 0;
					UMOUServerSettings::SaveEndpointOverrideForThisMachine(Args[0], PortArg);
				}

				// 이미 붙어 있던 연결은 옛 주소를 향하고 있으므로 끊고 새로 붙어야 한다.
				if (UServerSubsystem* Chat = FindServerSubsystem(World))
				{
					Chat->Disconnect();
					Chat->ConnectToChatServer();
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GChatLoginCommand(
		TEXT("MOU.Chat.Login"),
		TEXT("서버에 로그인한다. 사용법: MOU.Chat.Login <아이디> <비밀번호> [팀ID]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UServerSubsystem* Chat = FindServerSubsystem(World))
				{
					if (Args.Num() < 2)
					{
						UE_LOG(LogMOUServer, Warning,
							TEXT("사용법: MOU.Chat.Login <아이디> <비밀번호> [팀ID]"));
						return;
					}
					const int32 TeamArg = Args.IsValidIndex(2) ? FCString::Atoi(*Args[2]) : 0;
					Chat->Login(Args[0], Args[1], TeamArg);
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GChatRegisterCommand(
		TEXT("MOU.Chat.Register"),
		TEXT("계정을 만든다. 사용법: MOU.Chat.Register <아이디> <비밀번호> [닉네임]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UServerSubsystem* Chat = FindServerSubsystem(World))
				{
					if (Args.Num() < 2)
					{
						UE_LOG(LogMOUServer, Warning,
							TEXT("사용법: MOU.Chat.Register <아이디> <비밀번호> [닉네임]"));
						return;
					}
					const FString Nick = Args.IsValidIndex(2) ? Args[2] : Args[0];
					Chat->RegisterAccount(Args[0], Args[1], Nick);
				}
			}));

	// -ExecCmds= 로 넘긴 명령은 전부 엔진 초기화 직후 같은 프레임에 실행된다.
	// 그런데 로그인은 서버 왕복이 필요해서 몇 프레임 뒤에나 끝난다.
	// 그래서 헤드리스로 검증할 때 "로그인 완료 후에 방 목록 요청" 같은 순서를 만들 수 없다.
	// 이 명령은 그 간극을 메우는 테스트 보조 도구다.
	//
	//   -ExecCmds="MOU.Chat.Login id pw 0,MOU.Exec.Delayed 3 MOU.Room.List"
	FAutoConsoleCommandWithWorldAndArgs GExecDelayedCommand(
		TEXT("MOU.Exec.Delayed"),
		TEXT("N초 뒤에 콘솔 명령을 실행한다. 사용법: MOU.Exec.Delayed <초> <명령...>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (Args.Num() < 2)
				{
					UE_LOG(LogMOUServer, Warning, TEXT("사용법: MOU.Exec.Delayed <초> <명령...>"));
					return;
				}

				const float Delay = FCString::Atof(*Args[0]);

				FString Command;
				for (int32 i = 1; i < Args.Num(); ++i)
				{
					if (i > 1) { Command += TEXT(" "); }
					Command += Args[i];
				}

				// 월드가 그 사이에 사라질 수 있으므로 약한 참조로 잡는다.
				TWeakObjectPtr<UWorld> WeakWorld(World);
				FTSTicker::GetCoreTicker().AddTicker(
					FTickerDelegate::CreateLambda(
						[WeakWorld, Command](float) -> bool
						{
							if (UWorld* W = WeakWorld.Get())
							{
								GEngine->Exec(W, *Command);
							}
							return false;   // false = 한 번만 실행하고 해제
						}),
					Delay);
			}));

	FAutoConsoleCommandWithWorldAndArgs GRoomHostCommand(
		TEXT("MOU.Room.Host"),
		TEXT("방을 만든다. 사용법: MOU.Room.Host <방제목> [비번4자리] [포트]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UServerSubsystem* Chat = FindServerSubsystem(World))
				{
					const FString Title = Args.IsValidIndex(0) ? Args[0] : TEXT("테스트방");
					const FString Pw    = Args.IsValidIndex(1) ? Args[1] : FString();
					const int32   Port  = Args.IsValidIndex(2) ? FCString::Atoi(*Args[2]) : 7777;
					Chat->CreateRoom(Title, Pw, Port);
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GRoomListCommand(
		TEXT("MOU.Room.List"),
		TEXT("대기 중인 방 목록을 요청한다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>&, UWorld* World)
			{
				if (UServerSubsystem* Chat = FindServerSubsystem(World))
				{
					Chat->RequestRoomList();
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GRoomJoinCommand(
		TEXT("MOU.Room.Join"),
		TEXT("방에 참여한다. 사용법: MOU.Room.Join <방번호> [비번4자리]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UServerSubsystem* Chat = FindServerSubsystem(World))
				{
					if (!Args.IsValidIndex(0))
					{
						UE_LOG(LogMOUServer, Warning, TEXT("사용법: MOU.Room.Join <방번호> [비번4자리]"));
						return;
					}
					const FString Pw = Args.IsValidIndex(1) ? Args[1] : FString();
					Chat->JoinRoom(FCString::Atoi(*Args[0]), Pw);
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GRoomLeaveCommand(
		TEXT("MOU.Room.Leave"),
		TEXT("지금 있는 방에서 나간다. 방장이 나가면 방이 사라진다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>&, UWorld* World)
			{
				if (UServerSubsystem* Chat = FindServerSubsystem(World))
				{
					Chat->LeaveRoom();
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GRoomReadyCommand(
		TEXT("MOU.Room.Ready"),
		TEXT("준비 상태를 바꾼다. 사용법: MOU.Room.Ready [0|1] (생략하면 1)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UServerSubsystem* Chat = FindServerSubsystem(World))
				{
					const bool bReady = !Args.IsValidIndex(0) || Args[0] != TEXT("0");
					Chat->SetReady(bReady);
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GRoomStartCommand(
		TEXT("MOU.Room.Start"),
		TEXT("게임을 시작한다. 방장만, 전원 준비 완료일 때만 된다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>&, UWorld* World)
			{
				if (UServerSubsystem* Chat = FindServerSubsystem(World))
				{
					Chat->StartGame();
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GLobbyBackendCommand(
		TEXT("MOU.Lobby.Backend"),
		TEXT("지금 쓰고 있는 로비 백엔드를 출력한다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>&, UWorld* World)
			{
				FString Source;
				const EMOULobbyBackendType Configured = UMOUServerSettings::ResolveBackendType(&Source);

				// 설정값과 실제로 돌고 있는 것을 나란히 찍는다.
				// 접속 중에 설정을 바꾸면 둘이 달라지는데, 그때 "왜 안 바뀌지" 를 바로 알 수 있다.
				const UServerSubsystem* Chat = FindServerSubsystem(World);
				UE_LOG(LogMOUServer, Display, TEXT("설정: %s (출처 %s) / 실행 중: %s"),
					*MOULobbyBackend::GetTypeName(Configured), *Source,
					Chat ? *Chat->GetBackendName() : TEXT("(서브시스템 없음)"));
			}));

	FAutoConsoleCommandWithWorldAndArgs GChatSayCommand(
		TEXT("MOU.Chat.Say"),
		TEXT("채팅을 보낸다. 사용법: MOU.Chat.Say <채널 0~3> <메시지>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				UServerSubsystem* Chat = FindServerSubsystem(World);
				if (Chat == nullptr || Args.Num() < 2)
				{
					return;
				}

				const int32 ChannelValue = FCString::Atoi(*Args[0]);

				// 콘솔은 공백으로 인자를 쪼개므로 두 번째 인자부터 전부 이어 붙여 한 문장으로 만든다.
				TArray<FString> TextParts(Args.GetData() + 1, Args.Num() - 1);
				Chat->SendChat(static_cast<EChatChannelBP>(ChannelValue), FString::Join(TextParts, TEXT(" ")));
			}));

	FAutoConsoleCommandWithWorldAndArgs GChatDeadCommand(
		TEXT("MOU.Chat.Dead"),
		TEXT("[임시/8단계에서 제거 예정] 생사 상태를 바꾼다(테스트용). 사용법: MOU.Chat.Dead <0|1> ")
		TEXT("- 클라이언트가 자기 생사를 마음대로 바꿀 수 있어 살아있는 사람이 사망 채널을 엿볼 수 있다. ")
		TEXT("리슨서버 신원 미러링(8단계)이 붙으면 이 명령과 SetDeadForTest 를 함께 제거한다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UServerSubsystem* Chat = FindServerSubsystem(World))
				{
					Chat->SetDeadForTest(Args.IsValidIndex(0) ? (FCString::Atoi(*Args[0]) != 0) : true);
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GChatDisconnectCommand(
		TEXT("MOU.Chat.Disconnect"),
		TEXT("서버와의 연결을 끊는다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				if (UServerSubsystem* Chat = FindServerSubsystem(World))
				{
					Chat->Disconnect();
				}
			}));
}
