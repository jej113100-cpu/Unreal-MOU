// MOU 채팅 - 서브시스템 구현.
//
// 이 파일이 하는 일은 결국 3가지다.
//   1. 워커 스레드 수명 관리 (생성 / 안전한 파괴)
//   2. 블루프린트가 부른 함수를 패킷 바이트로 조립해서 워커에게 넘기기
//   3. 워커가 큐에 넣어둔 결과를 게임 스레드에서 꺼내 델리게이트로 뿌리기
//
// 패킷 구조체(MOU::LoginReqBody 등)를 직접 다루는 곳은 여기와 ChatClientRunnable.cpp 뿐이다.
// UI 나 게임플레이 코드는 이 파일 위쪽(블루프린트 API)만 쓴다.

#include "Chat/ChatSubsystem.h"

#include "Chat/ChatClientRunnable.h"
#include "Chat/ChatFraming.h"
#include "Chat/ServerSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/RunnableThread.h"

DEFINE_LOG_CATEGORY(LogMOUChat);

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

void UChatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 여기서 자동 접속하지 않는다.
	// 접속 시점(타이틀 화면인지, 인게임 진입 후인지)은 게임 흐름에 따라 달라야 하고,
	// PIE 로 잠깐 레벨만 확인할 때 매번 서버에 붙는 것도 원치 않기 때문이다.
	// 접속은 UI 나 GameMode 가 ConnectToChatServer() 를 호출해서 시작한다.

	// 게임 스레드 틱 등록. 워커가 쌓아둔 큐를 여기서 비운다.
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UChatSubsystem::Tick));

	UE_LOG(LogMOUChat, Log, TEXT("채팅 서브시스템 초기화 완료. 접속하려면 ConnectToChatServer 를 호출한다."));
}

void UChatSubsystem::Deinitialize()
{
	// 순서가 중요하다.
	// 틱을 먼저 끊어야 워커를 정리하는 도중에 Tick 이 죽은 큐를 읽는 일이 없다.
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}

	ShutdownClient();

	Super::Deinitialize();
}

void UChatSubsystem::ShutdownClient()
{
	if (ChatThread != nullptr)
	{
		// 1) 종료 요청 플래그를 세운다
		if (ChatClient != nullptr)
		{
			ChatClient->Stop();
		}

		// 2) 워커가 Run() 을 빠져나올 때까지 반드시 기다린다.
		//    bShouldWait 를 false 로 두면 아직 살아있는 스레드가 이미 해제된 큐를 건드려서
		//    "PIE 를 껐다 켜면 에디터가 통째로 죽는" 증상이 난다. 언리얼에서 가장 흔한 실수다.
		ChatThread->Kill(/*bShouldWait=*/true);

		delete ChatThread;
		ChatThread = nullptr;
	}

	// 3) 스레드가 완전히 끝난 뒤에 러너블을 해제한다.
	//    순서를 뒤집으면 아직 Run() 안에 있는 워커가 해제된 메모리를 만진다.
	delete ChatClient;
	ChatClient = nullptr;

	if (ConnectionState != EChatConnectionState::Disconnected)
	{
		SetConnectionState(EChatConnectionState::Disconnected, TEXT("클라이언트 종료"));
	}
	LoginResult = FChatLoginResult();
}

// ---------------------------------------------------------------------------
// 블루프린트 API
// ---------------------------------------------------------------------------

UChatSubsystem* UChatSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr)
	{
		return nullptr;
	}

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UChatSubsystem>() : nullptr;
}

void UChatSubsystem::ConnectToChatServer(const FString& InHost, int32 InPort)
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

		UE_LOG(LogMOUChat, Log, TEXT("접속 대상: %s:%d — 출처 %s"), *Host, Port, *Source);
	}

	if (ChatThread != nullptr)
	{
		// 이미 워커가 돌고 있다. 워커 자체가 재연결 루프를 갖고 있으므로
		// 끊긴 상태여도 새로 만들 필요가 없다.
		UE_LOG(LogMOUChat, Log, TEXT("이미 채팅 클라이언트가 동작 중이다. 접속 요청을 무시한다."));
		return;
	}

	ChatClient = new FChatClientRunnable(Host, Port);

	// 스레드 이름에 접속 대상을 넣어두면 PIE 창을 여러 개 띄웠을 때
	// 디버거의 스레드 목록에서 구분하기 쉽다.
	ChatThread = FRunnableThread::Create(
		ChatClient,
		*FString::Printf(TEXT("MOUChatClient_%s_%d"), *Host, Port),
		0,
		TPri_BelowNormal);   // 채팅은 게임 프레임보다 우선순위가 낮아도 된다

	if (ChatThread == nullptr)
	{
		UE_LOG(LogMOUChat, Error, TEXT("채팅 워커 스레드 생성 실패"));
		delete ChatClient;
		ChatClient = nullptr;
		return;
	}

	SetConnectionState(EChatConnectionState::Connecting, FString::Printf(TEXT("%s:%d"), *Host, Port));
}

FString UChatSubsystem::GetLoginResultText(EChatLoginResultBP Result)
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

bool UChatSubsystem::ValidateCredentials(const FString& LoginId, const FString& Password, FString& OutReason)
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

void UChatSubsystem::Login(const FString& LoginId, const FString& Password, int32 TeamId)
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
		UE_LOG(LogMOUChat, Log, TEXT("연결 전이라 로그인 요청을 보관한다: %s (팀 %d)"), *LoginId, TeamId);
	}
}

void UChatSubsystem::RegisterAccount(const FString& LoginId, const FString& Password, const FString& Nickname)
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
		UE_LOG(LogMOUChat, Log, TEXT("연결 전이라 가입 요청을 보관한다: %s"), *LoginId);
	}
}

void UChatSubsystem::SendPendingRegister()
{
	if (ChatClient == nullptr || !bHasPendingRegister)
	{
		return;
	}

	MOU::RegisterReqBody Request{};
	Request.Version = MOU::kProtocolVersion;
	MOUChat::CopyFixedString(Request.LoginId,  static_cast<int32>(MOU::kMaxLoginIdLen),  PendingRegisterId);
	MOUChat::CopyFixedString(Request.Password, static_cast<int32>(MOU::kMaxPasswordLen), PendingRegisterPassword);
	MOUChat::CopyFixedString(Request.Nickname, static_cast<int32>(MOU::kMaxNameLen),     PendingRegisterNickname);

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::RegisterReq, &Request, sizeof(Request)))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUChat, Log, TEXT("RegisterReq 전송: %s (닉네임 %s)"),
			*PendingRegisterId, *PendingRegisterNickname);
	}
}

void UChatSubsystem::SendPendingLogin()
{
	if (ChatClient == nullptr || !bHasPendingLogin)
	{
		return;
	}

	MOU::LoginReqBody Request{};
	Request.Version = MOU::kProtocolVersion;   // 서버가 이 값을 검사하고 다르면 거부한다
	MOUChat::CopyFixedString(Request.LoginId,  static_cast<int32>(MOU::kMaxLoginIdLen),  PendingLoginId);
	MOUChat::CopyFixedString(Request.Password, static_cast<int32>(MOU::kMaxPasswordLen), PendingPassword);
	Request.TeamId = PendingTeamId;

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::LoginReq, &Request, sizeof(Request)))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUChat, Log, TEXT("LoginReq 전송: %s (팀 %d)"), *PendingLoginId, PendingTeamId);
	}
}

void UChatSubsystem::SendChat(EChatChannelBP Channel, const FString& Text)
{
	if (ChatClient == nullptr)
	{
		UE_LOG(LogMOUChat, Warning, TEXT("채팅 클라이언트가 없다. ConnectToChatServer 를 먼저 호출한다."));
		return;
	}

	// 서버는 로그인 전 채팅을 조용히 버린다(연결은 유지). 사용자가 원인을 알 수 없으므로 여기서 알린다.
	if (ConnectionState != EChatConnectionState::LoggedIn)
	{
		UE_LOG(LogMOUChat, Warning, TEXT("아직 로그인 전이라 채팅을 보낼 수 없다. (현재 상태 %d)"),
			static_cast<int32>(ConnectionState));
		return;
	}

	// System 채널은 서버만 만든다. 보내봐야 서버가 버리므로 여기서 막는다.
	if (Channel == EChatChannelBP::System)
	{
		UE_LOG(LogMOUChat, Warning, TEXT("System 채널은 클라이언트가 보낼 수 없다."));
		return;
	}

	// 프로토콜의 TextLen 은 글자 수가 아니라 UTF-8 바이트 수다.
	// 상한(512바이트)을 넘겨 보내면 서버가 Malformed 로 판단해 연결을 끊어버리므로
	// 반드시 여기서 잘라서 보낸다.
	TArray<uint8> TextBytes;
	const int32 OriginalLength = MOUChat::GetUtf8Length(Text);
	const int32 SentLength     = MOUChat::EncodeUtf8Clamped(Text, static_cast<int32>(MOU::kMaxTextLen), TextBytes);

	if (SentLength <= 0)
	{
		return;   // 빈 메시지는 보내지 않는다
	}
	if (SentLength < OriginalLength)
	{
		UE_LOG(LogMOUChat, Warning, TEXT("메시지가 상한(%u바이트)을 넘어 잘렸다. %d -> %d 바이트"),
			MOU::kMaxTextLen, OriginalLength, SentLength);
	}

	MOU::ChatSendBody Body{};
	Body.TargetUserId = 0;                                   // 귓속말(9단계) 전용 필드. 그 외에는 0
	Body.TextLen      = static_cast<uint16>(SentLength);
	Body.Channel      = static_cast<uint8>(Channel);

	// 바디가 "고정부 + 가변 텍스트" 두 조각이라 BuildPacket 의 2조각 버전을 쓴다.
	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::ChatSend,
			&Body, sizeof(Body),
			TextBytes.GetData(), static_cast<uint32>(SentLength)))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
	}
}

void UChatSubsystem::SetDeadForTest(bool bDead)
{
	if (ChatClient == nullptr || ConnectionState != EChatConnectionState::LoggedIn)
	{
		UE_LOG(LogMOUChat, Warning, TEXT("로그인 후에만 생사 상태를 바꿀 수 있다."));
		return;
	}

	MOU::SetDeadBody Body{};
	Body.UserId = static_cast<uint64>(LoginResult.UserId);   // 서버는 세션 값을 쓰므로 참고용
	Body.bDead  = bDead ? 1 : 0;

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::SetDead, &Body, sizeof(Body)))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUChat, Log, TEXT("SetDead 전송: %s"), bDead ? TEXT("사망") : TEXT("생존"));
	}
}

// ---------------------------------------------------------------------------
// 로비
//
// 서버는 방 주소록 역할만 한다. 여기서 오가는 것은 방 메타데이터뿐이고,
// 실제 게임 트래픽은 참가자가 호스트의 리슨서버에 직접 붙어서 주고받는다.
// ---------------------------------------------------------------------------

bool UChatSubsystem::IsValidRoomPassword(const FString& RoomPassword)
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

FString UChatSubsystem::GetRoomResultText(EMOURoomResultBP Result)
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
	default:                               return TEXT("잘못된 요청입니다.");
	}
}

void UChatSubsystem::CreateRoom(const FString& Title, const FString& RoomPassword, int32 HostPort)
{
	if (ChatClient == nullptr || ConnectionState != EChatConnectionState::LoggedIn)
	{
		UE_LOG(LogMOUChat, Warning, TEXT("로그인 후에 방을 만들 수 있다."));
		OnRoomCreated.Broadcast(false, 0, EMOURoomResultBP::NotAuthed);
		return;
	}

	MOU::RoomCreateReqBody Request{};
	MOUChat::CopyFixedString(Request.Title, static_cast<int32>(MOU::kMaxRoomTitleLen), Title);
	Request.HostPort   = static_cast<uint16>(HostPort);
	Request.MaxPlayers = static_cast<uint8>(MOU::kMaxPlayersInRoom);

	const bool bUsePassword = IsValidRoomPassword(RoomPassword);
	Request.bHasPassword = bUsePassword ? 1 : 0;
	if (bUsePassword)
	{
		// 널 종료가 없는 고정 4바이트다. UTF-8 로 바꾼 뒤 그대로 복사한다.
		const FTCHARToUTF8 Utf8(*RoomPassword);
		FMemory::Memcpy(Request.Password, Utf8.Get(), MOU::kRoomPasswordLen);
	}

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::RoomCreateReq, &Request, sizeof(Request)))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUChat, Log, TEXT("RoomCreateReq 전송: \"%s\" 포트 %d %s"),
			*Title, HostPort, bUsePassword ? TEXT("[비번]") : TEXT(""));
	}
}

void UChatSubsystem::RequestRoomList()
{
	if (ChatClient == nullptr || ConnectionState != EChatConnectionState::LoggedIn)
	{
		UE_LOG(LogMOUChat, Warning, TEXT("로그인 후에 방 목록을 볼 수 있다."));
		OnRoomListReceived.Broadcast(TArray<FMOURoomInfo>());
		return;
	}

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::RoomListReq, nullptr, 0))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
	}
}

void UChatSubsystem::JoinRoom(int32 RoomId, const FString& RoomPassword)
{
	if (ChatClient == nullptr || ConnectionState != EChatConnectionState::LoggedIn)
	{
		FMOURoomJoinResult Failed;
		Failed.RoomId = RoomId;
		Failed.Result = EMOURoomResultBP::NotAuthed;
		OnRoomJoinCompleted.Broadcast(Failed);
		return;
	}

	MOU::RoomJoinReqBody Request{};
	Request.RoomId = static_cast<uint32>(RoomId);
	if (IsValidRoomPassword(RoomPassword))
	{
		const FTCHARToUTF8 Utf8(*RoomPassword);
		FMemory::Memcpy(Request.Password, Utf8.Get(), MOU::kRoomPasswordLen);
	}

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::RoomJoinReq, &Request, sizeof(Request)))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUChat, Log, TEXT("RoomJoinReq 전송: #%d"), RoomId);
	}
}

void UChatSubsystem::LeaveRoom()
{
	if (ChatClient == nullptr)
	{
		return;
	}

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::RoomLeaveReq, nullptr, 0))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUChat, Log, TEXT("RoomLeaveReq 전송 (방 #%d 에서 나감)"), CurrentRoomId);
	}

	// 서버 응답을 기다리지 않고 즉시 비운다.
	// 내가 나가는 것은 서버가 거부할 수 있는 일이 아니고, UI 가 바로 메인메뉴로
	// 돌아가야 사용자가 버튼을 두 번 누르지 않는다.
	ClearRoomState();
}

void UChatSubsystem::SetReady(bool bReady)
{
	if (ChatClient == nullptr || CurrentRoomId == 0)
	{
		return;
	}

	MOU::RoomReadyReqBody Request{};
	Request.bReady = bReady ? 1 : 0;

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::RoomReadyReq, &Request, sizeof(Request)))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUChat, Log, TEXT("RoomReadyReq 전송: %s"), bReady ? TEXT("준비완료") : TEXT("준비해제"));
	}
}

void UChatSubsystem::StartGame()
{
	if (ChatClient == nullptr || CurrentRoomId == 0)
	{
		return;
	}

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::RoomStartReq, nullptr, 0))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUChat, Log, TEXT("RoomStartReq 전송 (방 #%d)"), CurrentRoomId);
	}
}

bool UChatSubsystem::IsSelfReady() const
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

void UChatSubsystem::ClearRoomState()
{
	MyRoomId      = 0;
	CurrentRoomId = 0;
	RoomMembers.Reset();
	bAllMembersReady = false;
}

void UChatSubsystem::UpdateRoomState(int32 RoomId, int32 CurrentPlayers, bool bInGame)
{
	if (ChatClient == nullptr || RoomId == 0)
	{
		return;
	}

	MOU::RoomStateUpdateBody Request{};
	Request.RoomId = static_cast<uint32>(RoomId);
	// v5 부터 서버는 이 값을 무시한다. 구조체 크기를 맞추려고 채워 보낼 뿐이다.
	Request.CurrentPlayers = static_cast<uint8>(FMath::Clamp(CurrentPlayers, 0, 255));
	Request.State          = static_cast<uint8>(bInGame ? MOU::ERoomState::InGame : MOU::ERoomState::Waiting);

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::RoomStateUpdate, &Request, sizeof(Request)))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
	}
}

void UChatSubsystem::Disconnect()
{
	bHasPendingLogin = false;   // 사용자가 의도적으로 끊은 것이므로 자동 재로그인하지 않는다
	PendingPassword.Empty();    // 비밀번호를 필요 이상으로 메모리에 두지 않는다
	ShutdownClient();
}

// ---------------------------------------------------------------------------
// 게임 스레드 틱 - 워커 -> UI 방향의 유일한 통로
// ---------------------------------------------------------------------------

bool UChatSubsystem::Tick(float /*DeltaTime*/)
{
	if (ChatClient == nullptr)
	{
		return true;   // false 를 돌려주면 틱이 영구 해제된다. 항상 true
	}

	// 1) 상태 변화 처리
	FChatClientEvent Event;
	while (ChatClient->DequeueEvent(Event))
	{
		switch (Event.Type)
		{
		case EChatClientEventType::Connecting:
			SetConnectionState(EChatConnectionState::Connecting, Event.Detail);
			break;

		case EChatClientEventType::Connected:
			SetConnectionState(EChatConnectionState::Connected, Event.Detail);
			// 가입을 먼저 보낸다. 같은 TCP 스트림이라 서버가 이 순서대로 처리하므로,
			// "가입 후 곧바로 로그인" 이 한 번의 연결로 끝난다.
			SendPendingRegister();
			// 접속 전에 Login() 이 호출됐거나, 끊겼다 재접속한 경우 여기서 자동으로 로그인한다.
			SendPendingLogin();
			break;

		case EChatClientEventType::ConnectFailed:
			// 워커가 알아서 재시도하므로 여기서 할 일은 UI 에 알리는 것뿐이다.
			SetConnectionState(EChatConnectionState::Disconnected, Event.Detail);
			break;

		case EChatClientEventType::LoginAck:
			LoginResult = Event.Login;
			if (LoginResult.bSuccess)
			{
				SetConnectionState(EChatConnectionState::LoggedIn, LoginResult.Name);
				UE_LOG(LogMOUChat, Log, TEXT("로그인 완료. UserId=%lld, 이름=%s, 팀=%d"),
					LoginResult.UserId, *LoginResult.Name, LoginResult.TeamId);
			}
			else if (LoginResult.Result == EChatLoginResultBP::VersionMismatch)
			{
				// 재시도해도 계속 실패한다. 원인을 바로 알 수 있게 상세히 남긴다.
				UE_LOG(LogMOUChat, Error,
					TEXT("프로토콜 버전 불일치로 로그인이 거부됐다. 클라이언트=%d, 서버=%d. ")
					TEXT("Server.exe 와 언리얼 프로젝트를 같은 커밋으로 다시 빌드할 것."),
					static_cast<int32>(MOU::kProtocolVersion), LoginResult.ServerVersion);
			}
			else
			{
				// 아이디/비밀번호 실수는 흔한 일이라 사용자가 고칠 수 있게 사유를 그대로 남긴다.
				UE_LOG(LogMOUChat, Warning, TEXT("서버가 로그인을 거부했다: %s"),
					*UChatSubsystem::GetLoginResultText(LoginResult.Result));

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

		case EChatClientEventType::RegisterAck:
			// 응답을 받았으므로 보관본을 지운다.
			// 안 지우면 재접속할 때마다 가입을 다시 시도해 "이미 있는 아이디" 가 반복된다.
			bHasPendingRegister = false;
			PendingRegisterPassword.Empty();

			if (Event.Login.bSuccess)
			{
				UE_LOG(LogMOUChat, Log, TEXT("계정 생성 완료. 이어서 로그인하면 된다."));
			}
			else
			{
				UE_LOG(LogMOUChat, Warning, TEXT("계정 생성 실패: %s"),
					*UChatSubsystem::GetLoginResultText(Event.Login.Result));
			}
			OnChatRegisterCompleted.Broadcast(Event.Login.bSuccess, Event.Login.Result);
			break;

		case EChatClientEventType::RoomCreateAck:
			if (Event.bRoomSuccess)
			{
				MyRoomId      = Event.RoomId;
				CurrentRoomId = Event.RoomId;   // 방장도 그 방의 멤버다
				UE_LOG(LogMOUChat, Log, TEXT("방 생성 완료. 방번호 #%d"), MyRoomId);
			}
			else
			{
				UE_LOG(LogMOUChat, Warning, TEXT("방 생성 실패: %s"),
					*UChatSubsystem::GetRoomResultText(Event.RoomResult));
			}
			OnRoomCreated.Broadcast(Event.bRoomSuccess, Event.RoomId, Event.RoomResult);
			break;

		case EChatClientEventType::RoomListAck:
			UE_LOG(LogMOUChat, Log, TEXT("방 목록 수신: %d개"), Event.Rooms.Num());
			OnRoomListReceived.Broadcast(Event.Rooms);
			break;

		case EChatClientEventType::RoomJoinAck:
			if (Event.Join.bSuccess)
			{
				CurrentRoomId = Event.Join.RoomId;   // 대기실 입장. 방장은 아니다
				UE_LOG(LogMOUChat, Log, TEXT("방 #%d 입장. 호스트는 %s:%d"),
					Event.Join.RoomId, *Event.Join.HostAddress, Event.Join.HostPort);
			}
			else
			{
				UE_LOG(LogMOUChat, Warning, TEXT("방 참여 실패: %s"),
					*UChatSubsystem::GetRoomResultText(Event.Join.Result));
			}
			OnRoomJoinCompleted.Broadcast(Event.Join);
			break;

		case EChatClientEventType::RoomMemberList:
			// 늦게 도착한 이전 방의 명단이 현재 대기실을 덮어쓰지 않게 방 번호를 확인한다.
			// 빠르게 나갔다 다른 방에 들어가면 실제로 이런 순서가 나온다.
			if (Event.RoomId == CurrentRoomId)
			{
				RoomMembers      = Event.Members;
				bAllMembersReady = Event.bAllReady;
				UE_LOG(LogMOUChat, Verbose, TEXT("대기실 #%d 명단 %d명 (전원준비 %s)"),
					Event.RoomId, RoomMembers.Num(), bAllMembersReady ? TEXT("O") : TEXT("X"));
				OnRoomMembersChanged.Broadcast(Event.RoomId, RoomMembers, bAllMembersReady);
			}
			break;

		case EChatClientEventType::RoomClosed:
			UE_LOG(LogMOUChat, Log, TEXT("방 #%d 이(가) 닫혔다. 방장이 나갔다."), Event.RoomId);
			ClearRoomState();
			OnRoomClosed.Broadcast(Event.RoomId, Event.CloseReason);
			break;

		case EChatClientEventType::RoomStart:
		{
			// bIsHost 를 여기서 계산해 넘긴다. 받는 쪽은 호스트냐 참여자냐에 따라
			// OpenLevel(listen) 과 ClientTravel 로 갈리는데, 그 판단 근거가
			// 이미 여기 있으므로 UI 가 다시 따지게 하지 않는다.
			const bool bIsHost = (MyRoomId != 0 && MyRoomId == Event.RoomId);
			UE_LOG(LogMOUChat, Log, TEXT("방 #%d 게임 시작. 호스트 %s:%d (나는 %s)"),
				Event.RoomId, *Event.Join.HostAddress, Event.Join.HostPort,
				bIsHost ? TEXT("방장") : TEXT("참여자"));
			OnRoomGameStarted.Broadcast(Event.Join, bIsHost);
			break;
		}

		case EChatClientEventType::Disconnected:
			LoginResult = FChatLoginResult();
			// 연결이 끊기면 서버가 내 방을 지운다. 클라이언트 쪽 기억도 같이 비운다.
			ClearRoomState();
			SetConnectionState(EChatConnectionState::Disconnected, Event.Detail);
			break;
		}
	}

	// 2) 수신한 채팅 처리
	FChatMessage Message;
	while (ChatClient->DequeueMessage(Message))
	{
		// UI 가 붙기 전(5단계 이전)에도 동작을 확인할 수 있도록 로그를 남긴다.
		UE_LOG(LogMOUChat, Log, TEXT("[%s] %s: %s"),
			ToChannelName(Message.Channel), *Message.SenderName, *Message.Text);

		// 여기가 워커 스레드 -> 게임 스레드 경계의 끝이다.
		// 게임 스레드에서 부르므로 이 델리게이트 안에서 UMG 위젯을 만들어도 안전하다.
		OnChatMessageReceived.Broadcast(Message);
	}

	return true;
}

void UChatSubsystem::SetConnectionState(EChatConnectionState NewState, const FString& Detail)
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
	UChatSubsystem* FindChatSubsystem(UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UChatSubsystem>() : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs GChatConnectCommand(
		TEXT("MOU.Chat.Connect"),
		TEXT("채팅 서버에 접속한다. 인자를 생략하면 설정된 서버로 붙는다. 사용법: MOU.Chat.Connect [호스트] [포트]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
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
		TEXT("현재 설정된 채팅 서버 주소와 그 출처를 출력한다."),
		FConsoleCommandDelegate::CreateLambda(
			[]()
			{
				UE_LOG(LogMOUChat, Log, TEXT("채팅 서버: %s"), *UMOUServerSettings::GetResolvedEndpointText());
			}));

	/**
	 * 이 PC 에만 다른 서버 주소를 저장한다. 팀 공유 설정(DefaultGame.ini)은 건드리지 않는다.
	 * 인자를 주지 않으면 개인 설정을 지우고 팀 공유 값으로 되돌린다.
	 */
	FAutoConsoleCommandWithWorldAndArgs GChatSetServerCommand(
		TEXT("MOU.Chat.SetServer"),
		TEXT("이 PC 에만 채팅 서버 주소를 저장하고 다시 접속한다. 사용법: MOU.Chat.SetServer <호스트> [포트] (인자 없으면 초기화)"),
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
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
				{
					Chat->Disconnect();
					Chat->ConnectToChatServer();
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GChatLoginCommand(
		TEXT("MOU.Chat.Login"),
		TEXT("채팅 서버에 로그인한다. 사용법: MOU.Chat.Login <아이디> <비밀번호> [팀ID]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
				{
					if (Args.Num() < 2)
					{
						UE_LOG(LogMOUChat, Warning,
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
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
				{
					if (Args.Num() < 2)
					{
						UE_LOG(LogMOUChat, Warning,
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
					UE_LOG(LogMOUChat, Warning, TEXT("사용법: MOU.Exec.Delayed <초> <명령...>"));
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
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
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
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
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
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
				{
					if (!Args.IsValidIndex(0))
					{
						UE_LOG(LogMOUChat, Warning, TEXT("사용법: MOU.Room.Join <방번호> [비번4자리]"));
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
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
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
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
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
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
				{
					Chat->StartGame();
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GChatSayCommand(
		TEXT("MOU.Chat.Say"),
		TEXT("채팅을 보낸다. 사용법: MOU.Chat.Say <채널 0~3> <메시지>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				UChatSubsystem* Chat = FindChatSubsystem(World);
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
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
				{
					Chat->SetDeadForTest(Args.IsValidIndex(0) ? (FCString::Atoi(*Args[0]) != 0) : true);
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GChatDisconnectCommand(
		TEXT("MOU.Chat.Disconnect"),
		TEXT("채팅 서버와의 연결을 끊는다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				if (UChatSubsystem* Chat = FindChatSubsystem(World))
				{
					Chat->Disconnect();
				}
			}));
}
