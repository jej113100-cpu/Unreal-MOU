// MOU 로비 - 자체 서버 백엔드 구현.
//
// 여기 있는 코드는 전부 UChatSubsystem.cpp 에서 옮겨온 것이다.
// 옮긴 이유는 동작을 바꾸려는 게 아니라, 서브시스템이 "패킷" 이라는 단어를
// 몰라도 되게 만들기 위해서다. 그 선이 그어져 있어야 EOS 백엔드를 끼울 수 있다.

#include "Chat/SocketLobbyBackend.h"

#include "Chat/ChatClientRunnable.h"
#include "HAL/RunnableThread.h"

FSocketLobbyBackend::~FSocketLobbyBackend()
{
	// 소유자가 Shutdown() 을 부르는 것이 정상 경로지만, 잊었을 때 스레드가 남아
	// 에디터를 통째로 죽이는 것보다는 여기서 한 번 더 정리하는 편이 안전하다.
	Shutdown();
}

bool FSocketLobbyBackend::Start(const FString& Host, int32 Port)
{
	if (ChatThread != nullptr)
	{
		// 이미 워커가 돌고 있다. 워커 자체가 재연결 루프를 갖고 있으므로
		// 끊긴 상태여도 새로 만들 필요가 없다.
		UE_LOG(LogMOUChat, Log, TEXT("이미 채팅 클라이언트가 동작 중이다. 접속 요청을 무시한다."));
		return true;
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
		return false;
	}

	return true;
}

void FSocketLobbyBackend::Shutdown()
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
}

bool FSocketLobbyBackend::DequeueEvent(FChatClientEvent& Out)
{
	return ChatClient != nullptr && ChatClient->DequeueEvent(Out);
}

bool FSocketLobbyBackend::DequeueMessage(FChatMessage& Out)
{
	return ChatClient != nullptr && ChatClient->DequeueMessage(Out);
}

void FSocketLobbyBackend::SendEmpty(MOU::EOpcode Opcode, const TCHAR* LogLabel)
{
	if (ChatClient == nullptr)
	{
		return;
	}

	TArray<uint8> Packet;
	if (MOUChat::BuildPacket(Packet, Opcode, nullptr, 0))
	{
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		if (LogLabel != nullptr)
		{
			UE_LOG(LogMOUChat, Log, TEXT("%s 전송"), LogLabel);
		}
	}
}

// ---------------------------------------------------------------------------
// 계정
// ---------------------------------------------------------------------------

void FSocketLobbyBackend::SendLogin(const FString& LoginId, const FString& Password, int32 TeamId)
{
	if (ChatClient == nullptr)
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
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		// 비밀번호는 절대 로그에 남기지 않는다.
		UE_LOG(LogMOUChat, Log, TEXT("LoginReq 전송: %s (팀 %d)"), *LoginId, TeamId);
	}
}

void FSocketLobbyBackend::SendRegister(const FString& LoginId, const FString& Password, const FString& Nickname)
{
	if (ChatClient == nullptr)
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
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUChat, Log, TEXT("RegisterReq 전송: %s (닉네임 %s)"), *LoginId, *Nickname);
	}
}

// ---------------------------------------------------------------------------
// 채팅
// ---------------------------------------------------------------------------

void FSocketLobbyBackend::SendChat(EChatChannelBP Channel, const FString& Text)
{
	if (ChatClient == nullptr)
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

void FSocketLobbyBackend::SendSetDead(int64 UserId, bool bDead)
{
	if (ChatClient == nullptr)
	{
		return;
	}

	MOU::SetDeadBody Body{};
	Body.UserId = static_cast<uint64>(UserId);   // 서버는 세션 값을 쓰므로 참고용
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

void FSocketLobbyBackend::CreateRoom(const FString& Title, const FString& RoomPassword, int32 HostPort)
{
	if (ChatClient == nullptr)
	{
		return;
	}

	MOU::RoomCreateReqBody Request{};
	MOUChat::CopyFixedString(Request.Title, static_cast<int32>(MOU::kMaxRoomTitleLen), Title);
	Request.HostPort   = static_cast<uint16>(HostPort);
	Request.MaxPlayers = static_cast<uint8>(MOU::kMaxPlayersInRoom);

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
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUChat, Log, TEXT("RoomCreateReq 전송: \"%s\" 포트 %d %s"),
			*Title, HostPort, bUsePassword ? TEXT("[비번]") : TEXT(""));
	}
}

void FSocketLobbyBackend::RequestRoomList()
{
	SendEmpty(MOU::EOpcode::RoomListReq, nullptr);   // 자주 불리므로 로그를 남기지 않는다
}

void FSocketLobbyBackend::JoinRoom(int32 RoomId, const FString& RoomPassword)
{
	if (ChatClient == nullptr)
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
		ChatClient->EnqueuePacket(MoveTemp(Packet));
		UE_LOG(LogMOUChat, Log, TEXT("RoomJoinReq 전송: #%d"), RoomId);
	}
}

void FSocketLobbyBackend::LeaveRoom()
{
	SendEmpty(MOU::EOpcode::RoomLeaveReq, TEXT("RoomLeaveReq"));
}

void FSocketLobbyBackend::SetReady(bool bReady)
{
	if (ChatClient == nullptr)
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

void FSocketLobbyBackend::StartGame()
{
	SendEmpty(MOU::EOpcode::RoomStartReq, TEXT("RoomStartReq"));
}

void FSocketLobbyBackend::NotifyHostReady()
{
	SendEmpty(MOU::EOpcode::RoomHostReadyReq, TEXT("RoomHostReadyReq"));
}

void FSocketLobbyBackend::UpdateRoomState(int32 RoomId, int32 CurrentPlayers, bool bInGame)
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
