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
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY(LogMOUServer);

namespace
{
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
		if (Port <= 0) { Port = ResolvedPort; }

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
	PendingLoginId = LoginId;
	PendingPassword = Password;
	PendingTeamId = TeamId;
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
	PendingRegisterId = LoginId;
	PendingRegisterPassword = Password;
	PendingRegisterNickname = Nickname;
	bHasPendingRegister = true;

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

	Backend->CreateRoom(Title, EffectivePassword, HostPort);
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
	MyRoomId = 0;
	CurrentRoomId = 0;
	RoomMembers.Reset();
	bAllMembersReady = false;

	// 방을 떠났으면 리슨서버 감시도 의미가 없다.
	// 이걸 지우지 않으면, 방을 나갔다 새 방에 들어갔을 때 엉뚱한 시점에
	// "준비됐다" 가 나가고 서버가 NotStarted 로 거절한다.
	bWaitingForListenServer = false;
	ListenServerWaitSeconds = 0.f;
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

	// 0) 방장이면 내 리슨서버가 떴는지 확인한다.
	//    사건 처리보다 먼저 하는 이유는 없다. 서로 독립적이다.
	PollListenServer(DeltaTime);

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
				MyRoomId = Event.RoomId;
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
				UE_LOG(LogMOUServer, Log, TEXT("방 #%d 입장. 호스트는 %s:%d"),
					Event.Join.RoomId, *Event.Join.HostAddress, Event.Join.HostPort);
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
				RoomMembers = Event.Members;
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
			UE_LOG(LogMOUServer, Log, TEXT("방 #%d 게임 시작. 호스트 %s:%d (나는 %s)"),
				Event.RoomId, *Event.Join.HostAddress, Event.Join.HostPort,
				bIsHost ? TEXT("방장") : TEXT("참여자"));

			if (bIsHost)
			{
				// 지금부터 내 리슨서버가 뜨는지 지켜본다. 뜨는 순간 참여자에게 출발 신호가 나간다.
				//
				// 위젯이 아니라 여기서 켜는 이유: 방장은 곧 OpenLevel 로 맵을 갈아탄다.
				// 그 순간 로비 위젯은 파괴되므로, 위젯이 감시를 맡으면 감시자가 사라진다.
				bWaitingForListenServer = true;
				ListenServerWaitSeconds = 0.f;
			}

			OnRoomGameStarted.Broadcast(Event.Join, bIsHost);
			break;
		}

		case EServerClientEventType::RoomHostReady:
			// 참여자에게만 온다. 이제 붙어도 된다.
			UE_LOG(LogMOUServer, Log, TEXT("방 #%d 호스트 준비 완료. %s:%d 로 이동한다."),
				Event.RoomId, *Event.Join.HostAddress, Event.Join.HostPort);
			OnRoomHostReady.Broadcast(Event.Join);
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
					const FString Pw = Args.IsValidIndex(1) ? Args[1] : FString();
					const int32   Port = Args.IsValidIndex(2) ? FCString::Atoi(*Args[2]) : 7777;
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