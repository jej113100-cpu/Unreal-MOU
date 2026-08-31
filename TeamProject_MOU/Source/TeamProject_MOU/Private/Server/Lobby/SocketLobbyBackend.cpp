// MOU 로비 - 자체 서버 백엔드 구현.
//
// 여기 있는 코드는 전부 UServerSubsystem.cpp 에서 옮겨온 것이다.
// 옮긴 이유는 동작을 바꾸려는 게 아니라, 서브시스템이 "패킷" 이라는 단어를
// 몰라도 되게 만들기 위해서다. 그 선이 그어져 있어야 EOS 백엔드를 끼울 수 있다.

#include "Server/Lobby/SocketLobbyBackend.h"

#include "Server/Net/ServerClientRunnable.h"
#include "HAL/RunnableThread.h"

FSocketLobbyBackend::~FSocketLobbyBackend()
{
	// 소유자가 Shutdown() 을 부르는 것이 정상 경로지만, 잊었을 때 스레드가 남아
	// 에디터를 통째로 죽이는 것보다는 여기서 한 번 더 정리하는 편이 안전하다.
	Shutdown();
}

bool FSocketLobbyBackend::Start(const FString& Host, int32 Port)
{
	if (ServerThread != nullptr)
	{
		// 이미 워커가 돌고 있다. 워커 자체가 재연결 루프를 갖고 있으므로
		// 끊긴 상태여도 새로 만들 필요가 없다.
		UE_LOG(LogMOUServer, Log, TEXT("이미 채팅 클라이언트가 동작 중이다. 접속 요청을 무시한다."));
		return true;
	}

	ServerClient = new FServerClientRunnable(Host, Port);

	// 스레드 이름에 접속 대상을 넣어두면 PIE 창을 여러 개 띄웠을 때
	// 디버거의 스레드 목록에서 구분하기 쉽다.
	ServerThread = FRunnableThread::Create(
		ServerClient,
		*FString::Printf(TEXT("MOUChatClient_%s_%d"), *Host, Port),
		0,
		TPri_BelowNormal);   // 채팅은 게임 프레임보다 우선순위가 낮아도 된다

	if (ServerThread == nullptr)
	{
		UE_LOG(LogMOUServer, Error, TEXT("채팅 워커 스레드 생성 실패"));
		delete ServerClient;
		ServerClient = nullptr;
		return false;
	}

	return true;
}

void FSocketLobbyBackend::Shutdown()
{
	if (ServerThread != nullptr)
	{
		// 1) 종료 요청 플래그를 세운다
		if (ServerClient != nullptr)
		{
			ServerClient->Stop();
		}

		// 2) 워커가 Run() 을 빠져나올 때까지 반드시 기다린다.
		//    bShouldWait 를 false 로 두면 아직 살아있는 스레드가 이미 해제된 큐를 건드려서
		//    "PIE 를 껐다 켜면 에디터가 통째로 죽는" 증상이 난다. 언리얼에서 가장 흔한 실수다.
		ServerThread->Kill(/*bShouldWait=*/true);

		delete ServerThread;
		ServerThread = nullptr;
	}

	// 3) 스레드가 완전히 끝난 뒤에 러너블을 해제한다.
	//    순서를 뒤집으면 아직 Run() 안에 있는 워커가 해제된 메모리를 만진다.
	delete ServerClient;
	ServerClient = nullptr;
}

bool FSocketLobbyBackend::DequeueEvent(FServerClientEvent& Out)
{
	return ServerClient != nullptr && ServerClient->DequeueEvent(Out);
}

bool FSocketLobbyBackend::DequeueMessage(FChatMessage& Out)
{
	return ServerClient != nullptr && ServerClient->DequeueMessage(Out);
}

void FSocketLobbyBackend::SendEmpty(MOU::EOpcode Opcode, const TCHAR* LogLabel)
{
	if (ServerClient == nullptr)
	{
		return;
	}

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, Opcode, nullptr, 0))
	{
		ServerClient->EnqueuePacket(MoveTemp(Packet));
		if (LogLabel != nullptr)
		{
			UE_LOG(LogMOUServer, Log, TEXT("%s 전송"), LogLabel);
		}
	}
}

// ---------------------------------------------------------------------------
// 계정
// ---------------------------------------------------------------------------

void FSocketLobbyBackend::SendLogin(const FString& LoginId, const FString& Password, int32 TeamId)
{
	if (ServerClient == nullptr)
	{
		return;
	}

	MOU::LoginReqBody Request{};
	Request.Version = MOU::kProtocolVersion;   // 서버가 이 값을 검사하고 다르면 거부한다
	MOUChat::CopyFixedString(Request.LoginId,  static_cast<int32>(MOU::kMaxLoginIdLen),  LoginId);
	MOUChat::CopyFixedString(Request.Password, static_cast<int32>(MOU::kMaxPasswordLen), Password);
	Request.TeamId = TeamId;

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::LoginReq, &Request, sizeof(Request)))
	{
		ServerClient->EnqueuePacket(MoveTemp(Packet));
		// 비밀번호는 절대 로그에 남기지 않는다.
		UE_LOG(LogMOUServer, Log, TEXT("LoginReq 전송: %s (팀 %d)"), *LoginId, TeamId);
	}
}

void FSocketLobbyBackend::SendRegister(const FString& LoginId, const FString& Password, const FString& Nickname)
{
	if (ServerClient == nullptr)
	{
		return;
	}

	MOU::RegisterReqBody Request{};
	Request.Version = MOU::kProtocolVersion;
	MOUChat::CopyFixedString(Request.LoginId,  static_cast<int32>(MOU::kMaxLoginIdLen),  LoginId);
	MOUChat::CopyFixedString(Request.Password, static_cast<int32>(MOU::kMaxPasswordLen), Password);
	MOUChat::CopyFixedString(Request.Nickname, static_cast<int32>(MOU::kMaxNameLen),     Nickname);

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::RegisterReq, &Request, sizeof(Request)))
	{
		ServerClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUServer, Log, TEXT("RegisterReq 전송: %s (닉네임 %s)"), *LoginId, *Nickname);
	}
}

// ---------------------------------------------------------------------------
// 채팅
// ---------------------------------------------------------------------------

void FSocketLobbyBackend::SendChat(EChatChannelBP Channel, const FString& Text)
{
	if (ServerClient == nullptr)
	{
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
		UE_LOG(LogMOUServer, Warning, TEXT("메시지가 상한(%u바이트)을 넘어 잘렸다. %d -> %d 바이트"),
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
		ServerClient->EnqueuePacket(MoveTemp(Packet));
	}
}

void FSocketLobbyBackend::SendSetDead(int64 UserId, bool bDead)
{
	if (ServerClient == nullptr)
	{
		return;
	}

	MOU::SetDeadBody Body{};
	Body.UserId = static_cast<uint64>(UserId);   // 서버는 세션 값을 쓰므로 참고용
	Body.bDead  = bDead ? 1 : 0;

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::SetDead, &Body, sizeof(Body)))
	{
		ServerClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUServer, Log, TEXT("SetDead 전송: %s"), bDead ? TEXT("사망") : TEXT("생존"));
	}
}

// ---------------------------------------------------------------------------
// 로비
//
// 서버는 방 주소록 역할만 한다. 여기서 오가는 것은 방 메타데이터뿐이고,
// 실제 게임 트래픽은 참가자가 호스트의 리슨서버에 직접 붙어서 주고받는다.
// ---------------------------------------------------------------------------

void FSocketLobbyBackend::CreateRoom(const FString& Title, const FString& RoomPassword, int32 HostPort,
                                     const FString& LanAddress)
{
	if (ServerClient == nullptr)
	{
		return;
	}

	MOU::RoomCreateReqBody Request{};
	MOUChat::CopyFixedString(Request.Title, static_cast<int32>(MOU::kMaxRoomTitleLen), Title);
	Request.HostPort   = static_cast<uint16>(HostPort);
	Request.MaxPlayers = static_cast<uint8>(MOU::kMaxPlayersInRoom);

	// ★ 사설 주소만 보낸다. 공인 주소는 여전히 서버가 accept() 에서 읽는다 — (v8)
	//   클라이언트가 공인 주소를 신고하게 두면 남의 주소를 적어 접속을 몰아줄 수 있다.
	//   서버도 이 값이 사설 대역인지 다시 검사하고, 아니면 버린다.
	//   비워 보내도 된다. 그러면 공인 후보만 남고 예전(v7)과 같이 동작한다.
	MOUChat::CopyFixedString(Request.LanAddress, static_cast<int32>(MOU::kMaxAddressLen), LanAddress);

	const bool bUsePassword = (RoomPassword.Len() == static_cast<int32>(MOU::kRoomPasswordLen));
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
		ServerClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUServer, Log, TEXT("RoomCreateReq 전송: \"%s\" 포트 %d, LAN 주소 %s %s"),
			*Title, HostPort,
			LanAddress.IsEmpty() ? TEXT("(없음)") : *LanAddress,
			bUsePassword ? TEXT("[비번]") : TEXT(""));
	}
}

void FSocketLobbyBackend::RequestRoomList()
{
	SendEmpty(MOU::EOpcode::RoomListReq, nullptr);   // 자주 불리므로 로그를 남기지 않는다
}

void FSocketLobbyBackend::JoinRoom(int32 RoomId, const FString& RoomPassword)
{
	if (ServerClient == nullptr)
	{
		return;
	}

	MOU::RoomJoinReqBody Request{};
	Request.RoomId = static_cast<uint32>(RoomId);
	if (RoomPassword.Len() == static_cast<int32>(MOU::kRoomPasswordLen))
	{
		const FTCHARToUTF8 Utf8(*RoomPassword);
		FMemory::Memcpy(Request.Password, Utf8.Get(), MOU::kRoomPasswordLen);
	}

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::RoomJoinReq, &Request, sizeof(Request)))
	{
		ServerClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUServer, Log, TEXT("RoomJoinReq 전송: #%d"), RoomId);
	}
}

void FSocketLobbyBackend::LeaveRoom()
{
	SendEmpty(MOU::EOpcode::RoomLeaveReq, TEXT("RoomLeaveReq"));
}

void FSocketLobbyBackend::SetReady(bool bReady)
{
	if (ServerClient == nullptr)
	{
		return;
	}

	MOU::RoomReadyReqBody Request{};
	Request.bReady = bReady ? 1 : 0;

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::RoomReadyReq, &Request, sizeof(Request)))
	{
		ServerClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUServer, Log, TEXT("RoomReadyReq 전송: %s"), bReady ? TEXT("준비완료") : TEXT("준비해제"));
	}
}

void FSocketLobbyBackend::StartGame()
{
	SendEmpty(MOU::EOpcode::RoomStartReq, TEXT("RoomStartReq"));
}

void FSocketLobbyBackend::NotifyHostReady()
{
	SendEmpty(MOU::EOpcode::RoomHostReadyReq, TEXT("RoomHostReadyReq"));
}

// ---------------------------------------------------------------------------
// 친구 / 메신저 (v7)
//
// ★ 패킷 조립은 이 파일에서만 한다. UServerSubsystem 은 "무엇을 하고 싶은지" 만
//   말하고 바이트가 어떻게 나가는지는 모른다 — 백엔드를 EOS 로 바꿔도
//   UServerSubsystem 이 한 줄도 안 바뀌는 이유다.
// ---------------------------------------------------------------------------

void FSocketLobbyBackend::RequestFriendList()
{
	SendEmpty(MOU::EOpcode::FriendListReq, TEXT("FriendListReq"));
}

void FSocketLobbyBackend::AddFriend(const FString& Query)
{
	if (ServerClient == nullptr)
	{
		return;
	}

	MOU::FriendAddReqBody Body{};

	// ★ 파싱하지 않고 사용자가 친 문자열 그대로 넣는다. '#태그' 해석은 서버 몫이라
	//   나중에 태그가 생겨도 이 코드는 바뀌지 않는다(ChatProtocol.h 주석).
	MOUChat::CopyFixedString(Body.Query, static_cast<int32>(MOU::kMaxFriendQueryLen), Query);

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::FriendAddReq, &Body, sizeof(Body)))
	{
		ServerClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUServer, Log, TEXT("FriendAddReq 전송: \"%s\""), *Query);
	}
}

void FSocketLobbyBackend::RespondFriendRequest(int64 FromUserId, bool bAccept)
{
	if (ServerClient == nullptr || FromUserId == 0)
	{
		return;
	}

	MOU::FriendRespondReqBody Body{};
	Body.FromUserId = static_cast<uint64>(FromUserId);
	Body.bAccept    = bAccept ? 1 : 0;

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::FriendRespondReq, &Body, sizeof(Body)))
	{
		ServerClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUServer, Log, TEXT("FriendRespondReq 전송: from=%lld %s"),
			FromUserId, bAccept ? TEXT("수락") : TEXT("거절"));
	}
}

void FSocketLobbyBackend::RemoveFriend(int64 TargetUserId)
{
	if (ServerClient == nullptr || TargetUserId == 0)
	{
		return;
	}

	MOU::FriendRemoveReqBody Body{};
	Body.TargetUserId = static_cast<uint64>(TargetUserId);

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::FriendRemoveReq, &Body, sizeof(Body)))
	{
		ServerClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUServer, Log, TEXT("FriendRemoveReq 전송: target=%lld"), TargetUserId);
	}
}

void FSocketLobbyBackend::SendDirectMessage(int64 TargetUserId, const FString& Text)
{
	if (ServerClient == nullptr || TargetUserId == 0)
	{
		return;
	}

	// SendChat 과 같은 이유로 UTF-8 바이트 기준으로 자른다. 상한을 넘겨 보내면
	// 서버가 Malformed 로 보고 연결을 끊는다.
	TArray<uint8> TextBytes;
	const int32 OriginalLength = MOUChat::GetUtf8Length(Text);
	const int32 SentLength     = MOUChat::EncodeUtf8Clamped(Text, static_cast<int32>(MOU::kMaxTextLen), TextBytes);

	if (SentLength <= 0)
	{
		return;   // 빈 메시지는 보내지 않는다
	}
	if (SentLength < OriginalLength)
	{
		UE_LOG(LogMOUServer, Warning, TEXT("DM 이 상한(%u바이트)을 넘어 잘렸다. %d -> %d 바이트"),
			MOU::kMaxTextLen, OriginalLength, SentLength);
	}

	MOU::DirectMessageSendBody Body{};
	Body.TargetUserId = static_cast<uint64>(TargetUserId);
	Body.TextLen      = static_cast<uint16>(SentLength);

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::DirectMessageSend,
			&Body, sizeof(Body),
			TextBytes.GetData(), static_cast<uint32>(SentLength)))
	{
		ServerClient->EnqueuePacket(MoveTemp(Packet));
	}
}

void FSocketLobbyBackend::RequestDmHistory(int64 PeerUserId, int64 BeforeMessageId)
{
	if (ServerClient == nullptr || PeerUserId == 0)
	{
		return;
	}

	MOU::DmHistoryReqBody Body{};
	Body.PeerUserId      = static_cast<uint64>(PeerUserId);
	Body.BeforeMessageId = static_cast<uint64>(BeforeMessageId);

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::DmHistoryReq, &Body, sizeof(Body)))
	{
		ServerClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUServer, Verbose, TEXT("DmHistoryReq 전송: peer=%lld before=%lld"),
			PeerUserId, BeforeMessageId);
	}
}

void FSocketLobbyBackend::UpdateRoomState(int32 RoomId, int32 CurrentPlayers, bool bInGame)
{
	if (ServerClient == nullptr || RoomId == 0)
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
		ServerClient->EnqueuePacket(MoveTemp(Packet));
	}
}

// ---------------------------------------------------------------------------
// 도달성 프로브 (v9)
// ---------------------------------------------------------------------------

void FSocketLobbyBackend::RequestHostProbe(int32 Port, uint32 Nonce)
{
	if (ServerClient == nullptr)
	{
		return;
	}

	MOU::HostProbeReqBody Request{};
	Request.Nonce = Nonce;
	Request.Port  = static_cast<uint16>(Port);

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::HostProbeReq, &Request, sizeof(Request)))
	{
		ServerClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUServer, Log, TEXT("HostProbeReq 전송: 포트 %d (nonce %u)"), Port, Nonce);
	}
}

void FSocketLobbyBackend::ReportReachability(bool bReachable)
{
	if (ServerClient == nullptr)
	{
		return;
	}

	MOU::RoomReachabilityReqBody Request{};
	Request.bReachable = bReachable ? 1 : 0;

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, MOU::EOpcode::RoomReachabilityReq, &Request, sizeof(Request)))
	{
		ServerClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUServer, Log, TEXT("RoomReachabilityReq 전송: %s"),
			bReachable ? TEXT("외부 접속 가능") : TEXT("같은 LAN 전용"));
	}
}
