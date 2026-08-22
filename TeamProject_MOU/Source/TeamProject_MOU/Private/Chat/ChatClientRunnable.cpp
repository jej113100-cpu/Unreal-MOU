// MOU 채팅 - 워커 스레드 구현.
// 대응하는 서버 코드: MOU_Server/Server/Server.cpp 의 ClientThread()

#include "Chat/ChatClientRunnable.h"

#include "HAL/PlatformProcess.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

FChatClientRunnable::FChatClientRunnable(const FString& InHost, int32 InPort)
	: Host(InHost)
	, Port(InPort)
{
}

FChatClientRunnable::~FChatClientRunnable()
{
	// 정상 경로에서는 UChatSubsystem 이 Stop() -> Kill(true) 로 스레드를 먼저 정리하므로
	// 여기 도달했을 때 Socket 은 이미 nullptr 이다. 만약을 대비한 안전망.
	DestroySocketIfNeeded();
}

bool FChatClientRunnable::Init()
{
	// 여기서는 블로킹 작업을 하지 않는다 (헤더 주석 참고).
	// 서브시스템 포인터를 얻는 정도만 한다.
	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		UE_LOG(LogMOUChat, Error, TEXT("소켓 서브시스템을 얻지 못했다. 채팅을 사용할 수 없다."));
		return false;
	}
	return true;
}

uint32 FChatClientRunnable::Run()
{
	// 바깥 루프 = 재연결 루프.
	// 채팅 서버가 꺼져 있거나 도중에 죽어도 게임은 계속 돌아가야 하므로,
	// 실패를 치명적 오류로 보지 않고 계속 재시도한다.
	while (!bStopRequested)
	{
		PushEvent(EChatClientEventType::Connecting);

		FString ConnectError;
		if (!ConnectOnce(ConnectError))
		{
			DestroySocketIfNeeded();
			PushEvent(EChatClientEventType::ConnectFailed, ConnectError);

			// 곧바로 재시도하면 서버가 없을 때 로그가 폭주한다.
			// 다만 통째로 3초를 자면 Stop() 요청에 3초 늦게 반응하므로 잘게 쪼개서 잔다.
			for (int32 Slept = 0; Slept < ReconnectDelayMilliseconds && !bStopRequested; Slept += WaitMilliseconds)
			{
				FPlatformProcess::Sleep(WaitMilliseconds / 1000.0f);
			}
			continue;
		}

		UE_LOG(LogMOUChat, Log, TEXT("채팅 서버 연결 성공: %s:%d"), *Host, Port);
		PushEvent(EChatClientEventType::Connected);

		// 안쪽 루프 = 연결이 살아있는 동안의 송수신 루프.
		// 이전 연결에서 남은 조각이 새 연결에 섞이면 프레이밍이 깨지므로 버퍼를 비우고 시작한다.
		RecvBuffer.Reset();

		// 송신 큐도 같이 비운다.
		// 새 연결에서는 LoginReq 가 가장 먼저 나가야 하는데, 끊기기 직전 큐에 남아있던
		// 패킷이 먼저 나가면 서버가 미인증 상태로 보고 조용히 버린다(HandleChatSend 의 bAuthed 검사).
		// 게임 스레드가 Connected 이벤트를 처리해 LoginReq 를 넣기까지 한 프레임이 걸리는데,
		// 워커는 그보다 먼저 PumpSend 를 돌기 때문에 실제로 발생하는 경합이다.
		// 소비자 쪽(워커)에서 비우므로 SPSC 규칙에 어긋나지 않는다.
		{
			TArray<uint8> Discarded;
			int32 DiscardedCount = 0;
			while (OutboundPackets.Dequeue(Discarded))
			{
				++DiscardedCount;
			}
			if (DiscardedCount > 0)
			{
				UE_LOG(LogMOUChat, Warning, TEXT("재접속하면서 미전송 패킷 %d개를 버렸다."), DiscardedCount);
			}
		}
		while (!bStopRequested)
		{
			if (!PumpSend())
			{
				break;
			}
			if (!PumpRecv())
			{
				break;
			}
		}

		DestroySocketIfNeeded();
		PushEvent(EChatClientEventType::Disconnected);

		if (!bStopRequested)
		{
			UE_LOG(LogMOUChat, Warning, TEXT("채팅 서버 연결이 끊겼다. %dms 후 재시도한다."), ReconnectDelayMilliseconds);
			for (int32 Slept = 0; Slept < ReconnectDelayMilliseconds && !bStopRequested; Slept += WaitMilliseconds)
			{
				FPlatformProcess::Sleep(WaitMilliseconds / 1000.0f);
			}
		}
	}

	return 0;
}

void FChatClientRunnable::Stop()
{
	// 게임 스레드에서 호출된다. 플래그만 세우고 즉시 반환한다.
	// 워커는 최대 WaitMilliseconds 안에 이 플래그를 보고 빠져나온다.
	bStopRequested = true;
}

void FChatClientRunnable::Exit()
{
	// Run() 이 끝난 뒤 워커 스레드에서 호출된다. 소켓 정리는 여기가 마지막 기회다.
	DestroySocketIfNeeded();
}

bool FChatClientRunnable::ConnectOnce(FString& OutError)
{
	if (SocketSubsystem == nullptr)
	{
		OutError = TEXT("소켓 서브시스템 없음");
		return false;
	}

	// 1. 주소 해석.
	//    "127.0.0.1" 처럼 숫자 IP 면 SetIp 로 바로 되고,
	//    "chat.example.com" 처럼 이름이면 DNS 조회가 필요하다.
	//    DNS 조회는 블로킹이라 게임 스레드에서 하면 안 된다. 여기가 워커 스레드라 안전하다.
	TSharedPtr<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
	bool bAddrValid = false;
	Addr->SetIp(*Host, bAddrValid);

	if (!bAddrValid)
	{
		const FAddressInfoResult Info = SocketSubsystem->GetAddressInfo(
			*Host, nullptr, EAddressInfoFlags::Default, NAME_None);

		if (Info.ReturnCode == SE_NO_ERROR && Info.Results.Num() > 0)
		{
			Addr = Info.Results[0].Address->Clone();
			bAddrValid = true;
		}
	}

	if (!bAddrValid)
	{
		OutError = FString::Printf(TEXT("주소를 해석할 수 없다: %s"), *Host);
		return false;
	}
	Addr->SetPort(Port);

	// 2. 소켓 생성. NAME_Stream = TCP.
	Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("MOU Chat Client"), Addr->GetProtocolType());
	if (Socket == nullptr)
	{
		OutError = TEXT("소켓 생성 실패");
		return false;
	}

	// 블로킹 모드로 둔다. 대신 Recv 전에 Wait() 으로 타임아웃을 걸어
	// 스레드가 영원히 잠들지 않게 한다. 서버가 SO_RCVTIMEO 를 거는 것과 같은 이유다.
	Socket->SetNonBlocking(false);

	// Nagle 알고리즘을 끈다. 켜져 있으면 짧은 채팅 패킷을 모아서 보내려고
	// 최대 수백 ms 를 지연시킨다. 채팅은 대역폭보다 반응 속도가 중요하다.
	Socket->SetNoDelay(true);

	// 3. 접속. 블로킹이므로 서버가 응답하지 않으면 OS 기본 타임아웃만큼 걸릴 수 있다.
	//    (연결 거부는 즉시 실패하므로 로컬 테스트에서는 체감되지 않는다)
	if (!Socket->Connect(*Addr))
	{
		OutError = FString::Printf(TEXT("%s:%d 접속 실패 (서버가 켜져 있는지 확인)"), *Host, Port);
		return false;
	}

	return true;
}

bool FChatClientRunnable::PumpSend()
{
	TArray<uint8> Packet;
	while (OutboundPackets.Dequeue(Packet))
	{
		// send() 는 요청한 길이를 한 번에 다 보내지 않을 수 있다.
		// 전부 나갈 때까지 반복한다. 서버 Framing.cpp 의 SendAll 과 같은 로직이다.
		int32 TotalSent = 0;
		while (TotalSent < Packet.Num())
		{
			int32 Sent = 0;
			if (!Socket->Send(Packet.GetData() + TotalSent, Packet.Num() - TotalSent, Sent) || Sent <= 0)
			{
				UE_LOG(LogMOUChat, Warning, TEXT("패킷 전송 실패. 연결을 끊는다."));
				return false;
			}
			TotalSent += Sent;
		}
	}
	return true;
}

bool FChatClientRunnable::PumpRecv()
{
	// 읽을 게 없으면 WaitMilliseconds 만큼 자다가 깬다.
	// 타임아웃은 오류가 아니다. 아무 일도 없었을 뿐이므로 그대로 루프를 돈다.
	if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(WaitMilliseconds)))
	{
		return true;
	}

	uint8 Temp[1024];
	int32 BytesRead = 0;
	if (!Socket->Recv(Temp, UE_ARRAY_COUNT(Temp), BytesRead))
	{
		// 블로킹 소켓이라 보통 여기 오지 않지만, 플랫폼에 따라 WouldBlock 이 올 수 있다.
		// 그건 정상이므로 연결을 유지한다.
		const ESocketErrors LastError = SocketSubsystem->GetLastErrorCode();
		if (LastError == SE_EWOULDBLOCK)
		{
			return true;
		}
		UE_LOG(LogMOUChat, Warning, TEXT("Recv 실패 (오류 코드 %d)"), static_cast<int32>(LastError));
		return false;
	}

	// Wait 이 true 를 줬는데 0바이트면 서버가 연결을 정상 종료(FIN)한 것이다.
	if (BytesRead <= 0)
	{
		UE_LOG(LogMOUChat, Log, TEXT("서버가 연결을 종료했다."));
		return false;
	}

	RecvBuffer.Append(Temp, BytesRead);

	// 한 번의 Recv 에 패킷이 여러 개 붙어 왔을 수 있으므로 다 꺼낼 때까지 돈다.
	for (;;)
	{
		MOU::PacketHeader Header{};
		const MOUChat::EFrameResult Result = MOUChat::TryExtractPacket(RecvBuffer, Header, BodyScratch);

		if (Result == MOUChat::EFrameResult::NeedMore)
		{
			break;
		}
		if (Result == MOUChat::EFrameResult::Malformed)
		{
			// 여기까지 왔다는 건 스트림 동기화가 깨졌다는 뜻이다.
			// 어디부터가 다음 패킷인지 알 방법이 없으므로 연결을 끊고 새로 붙는 수밖에 없다.
			UE_LOG(LogMOUChat, Error, TEXT("비정상 패킷 크기. 스트림이 깨졌으므로 재접속한다."));
			return false;
		}

		HandlePacket(Header, BodyScratch);
	}

	return true;
}

void FChatClientRunnable::HandlePacket(const MOU::PacketHeader& Header, const TArray<uint8>& Body)
{
	switch (static_cast<MOU::EOpcode>(Header.Opcode))
	{
	case MOU::EOpcode::LoginAck:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::LoginAckBody)))
		{
			UE_LOG(LogMOUChat, Warning, TEXT("LoginAck 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::LoginAckBody Ack{};
		FMemory::Memcpy(&Ack, Body.GetData(), sizeof(Ack));

		FChatClientEvent Event;
		Event.Type                = EChatClientEventType::LoginAck;
		Event.Login.bSuccess      = (Ack.bSuccess != 0);
		Event.Login.UserId        = static_cast<int64>(Ack.UserId);
		Event.Login.TeamId        = Ack.TeamId;
		Event.Login.Name          = MOUChat::ReadFixedString(Ack.Name, static_cast<int32>(MOU::kMaxNameLen));
		Event.Login.Result        = static_cast<EChatLoginResultBP>(Ack.Result);
		Event.Login.ServerVersion = static_cast<int32>(Ack.ServerVersion);

		// 버전 불일치만 여기서 상세히 남긴다. 우리 쪽 프로토콜 번호를 아는 곳이
		// 이 층뿐이기 때문이다(서브시스템은 프로토콜을 모른다).
		if (!Event.Login.bSuccess && Event.Login.Result == EChatLoginResultBP::VersionMismatch)
		{
			UE_LOG(LogMOUChat, Error,
				TEXT("프로토콜 버전 불일치. 클라이언트=%d, 서버=%d. ")
				TEXT("Server.exe 와 언리얼 프로젝트를 같은 커밋으로 다시 빌드할 것."),
				static_cast<int32>(MOU::kProtocolVersion), Event.Login.ServerVersion);
		}

		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::RegisterAck:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::RegisterAckBody)))
		{
			UE_LOG(LogMOUChat, Warning, TEXT("RegisterAck 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::RegisterAckBody Ack{};
		FMemory::Memcpy(&Ack, Body.GetData(), sizeof(Ack));

		// 가입 결과는 신원이 아니므로 UserId/Name 은 채우지 않는다.
		// bSuccess 와 Result 만 의미가 있다.
		FChatClientEvent Event;
		Event.Type                = EChatClientEventType::RegisterAck;
		Event.Login.bSuccess      = (Ack.bSuccess != 0);
		Event.Login.Result        = static_cast<EChatLoginResultBP>(Ack.Result);
		Event.Login.ServerVersion = static_cast<int32>(Ack.ServerVersion);
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::RoomCreateAck:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::RoomCreateAckBody)))
		{
			UE_LOG(LogMOUChat, Warning, TEXT("RoomCreateAck 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::RoomCreateAckBody Ack{};
		FMemory::Memcpy(&Ack, Body.GetData(), sizeof(Ack));

		FChatClientEvent Event;
		Event.Type         = EChatClientEventType::RoomCreateAck;
		Event.RoomId       = static_cast<int32>(Ack.RoomId);
		Event.bRoomSuccess = (Ack.bSuccess != 0);
		Event.RoomResult   = static_cast<EMOURoomResultBP>(Ack.Result);
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::RoomListAck:
	{
		const int32 FixedSize = static_cast<int32>(sizeof(MOU::RoomListAckBody));
		if (Body.Num() < FixedSize)
		{
			UE_LOG(LogMOUChat, Warning, TEXT("RoomListAck 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::RoomListAckBody Head{};
		FMemory::Memcpy(&Head, Body.GetData(), sizeof(Head));

		// 서버가 보냈다고 주장하는 개수를 그대로 믿지 않는다.
		// 실제로 도착한 바이트 수로 검산해야 배열 밖을 읽지 않는다.
		const int32 InfoSize = static_cast<int32>(sizeof(MOU::RoomInfo));
		const int32 Declared = static_cast<int32>(Head.Count);
		const int32 Available = (Body.Num() - FixedSize) / InfoSize;
		const int32 SafeCount = FMath::Min(Declared, Available);

		if (SafeCount < Declared)
		{
			UE_LOG(LogMOUChat, Warning,
				TEXT("RoomListAck 가 %d개라고 했지만 %d개분만 도착했다"), Declared, Available);
		}

		FChatClientEvent Event;
		Event.Type = EChatClientEventType::RoomListAck;
		Event.Rooms.Reserve(SafeCount);

		for (int32 i = 0; i < SafeCount; ++i)
		{
			MOU::RoomInfo Src{};
			FMemory::Memcpy(&Src, Body.GetData() + FixedSize + i * InfoSize, sizeof(Src));

			FMOURoomInfo Info;
			Info.RoomId         = static_cast<int32>(Src.RoomId);
			Info.HostUserId     = static_cast<int64>(Src.HostUserId);
			Info.Title          = MOUChat::ReadFixedString(Src.Title, static_cast<int32>(MOU::kMaxRoomTitleLen));
			Info.HostName       = MOUChat::ReadFixedString(Src.HostName, static_cast<int32>(MOU::kMaxNameLen));
			Info.CurrentPlayers = Src.CurrentPlayers;
			Info.MaxPlayers     = Src.MaxPlayers;
			Info.bHasPassword   = (Src.bHasPassword != 0);
			Info.State          = static_cast<EMOURoomStateBP>(Src.State);
			Event.Rooms.Add(MoveTemp(Info));
		}

		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::RoomJoinAck:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::RoomJoinAckBody)))
		{
			UE_LOG(LogMOUChat, Warning, TEXT("RoomJoinAck 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::RoomJoinAckBody Ack{};
		FMemory::Memcpy(&Ack, Body.GetData(), sizeof(Ack));

		FChatClientEvent Event;
		Event.Type             = EChatClientEventType::RoomJoinAck;
		Event.Join.bSuccess    = (Ack.bSuccess != 0);
		Event.Join.RoomId      = static_cast<int32>(Ack.RoomId);
		Event.Join.HostPort    = static_cast<int32>(Ack.HostPort);
		Event.Join.Result      = static_cast<EMOURoomResultBP>(Ack.Result);
		Event.Join.HostAddress = MOUChat::ReadFixedString(Ack.HostAddress, static_cast<int32>(MOU::kMaxAddressLen));
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::RoomMemberList:
	{
		const int32 FixedSize = static_cast<int32>(sizeof(MOU::RoomMemberListBody));
		if (Body.Num() < FixedSize)
		{
			UE_LOG(LogMOUChat, Warning, TEXT("RoomMemberList 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::RoomMemberListBody Head{};
		FMemory::Memcpy(&Head, Body.GetData(), sizeof(Head));

		// RoomListAck 와 같은 이유로, 서버가 주장하는 개수를 실제 바이트 수로 검산한다.
		const int32 InfoSize  = static_cast<int32>(sizeof(MOU::RoomMemberInfo));
		const int32 Declared  = static_cast<int32>(Head.Count);
		const int32 Available = (Body.Num() - FixedSize) / InfoSize;
		const int32 SafeCount = FMath::Min(Declared, Available);

		if (SafeCount < Declared)
		{
			UE_LOG(LogMOUChat, Warning,
				TEXT("RoomMemberList 가 %d명이라고 했지만 %d명분만 도착했다"), Declared, Available);
		}

		FChatClientEvent Event;
		Event.Type      = EChatClientEventType::RoomMemberList;
		Event.RoomId    = static_cast<int32>(Head.RoomId);
		Event.bAllReady = (Head.bAllReady != 0);
		Event.Members.Reserve(SafeCount);

		for (int32 i = 0; i < SafeCount; ++i)
		{
			MOU::RoomMemberInfo Src{};
			FMemory::Memcpy(&Src, Body.GetData() + FixedSize + i * InfoSize, sizeof(Src));

			FMOURoomMember Member;
			Member.UserId  = static_cast<int64>(Src.UserId);
			Member.Name    = MOUChat::ReadFixedString(Src.Name, static_cast<int32>(MOU::kMaxNameLen));
			Member.bIsHost = (Src.bIsHost != 0);
			Member.bReady  = (Src.bReady != 0);
			Event.Members.Add(MoveTemp(Member));
		}

		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::RoomClosed:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::RoomClosedBody)))
		{
			UE_LOG(LogMOUChat, Warning, TEXT("RoomClosed 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::RoomClosedBody Closed{};
		FMemory::Memcpy(&Closed, Body.GetData(), sizeof(Closed));

		FChatClientEvent Event;
		Event.Type        = EChatClientEventType::RoomClosed;
		Event.RoomId      = static_cast<int32>(Closed.RoomId);
		Event.CloseReason = static_cast<EMOURoomCloseReasonBP>(Closed.Reason);
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::RoomStart:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::RoomStartBody)))
		{
			UE_LOG(LogMOUChat, Warning, TEXT("RoomStart 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::RoomStartBody Start{};
		FMemory::Memcpy(&Start, Body.GetData(), sizeof(Start));

		// 호스트 주소를 RoomJoinAck 와 같은 그릇에 담는다.
		// 받는 쪽에서 MakeTravelURL() 을 그대로 쓸 수 있어 처리 경로가 하나로 모인다.
		FChatClientEvent Event;
		Event.Type             = EChatClientEventType::RoomStart;
		Event.RoomId           = static_cast<int32>(Start.RoomId);
		Event.Join.bSuccess    = true;
		Event.Join.RoomId      = static_cast<int32>(Start.RoomId);
		Event.Join.HostPort    = static_cast<int32>(Start.HostPort);
		Event.Join.HostAddress = MOUChat::ReadFixedString(Start.HostAddress, static_cast<int32>(MOU::kMaxAddressLen));
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::RoomHostReady:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::RoomHostReadyBody)))
		{
			UE_LOG(LogMOUChat, Warning, TEXT("RoomHostReady 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::RoomHostReadyBody Ready{};
		FMemory::Memcpy(&Ready, Body.GetData(), sizeof(Ready));

		// RoomStart 와 같은 그릇에 담는다. 받는 쪽에서 MakeTravelURL() 을 그대로 쓴다.
		FChatClientEvent Event;
		Event.Type             = EChatClientEventType::RoomHostReady;
		Event.RoomId           = static_cast<int32>(Ready.RoomId);
		Event.Join.bSuccess    = true;
		Event.Join.RoomId      = static_cast<int32>(Ready.RoomId);
		Event.Join.HostPort    = static_cast<int32>(Ready.HostPort);
		Event.Join.HostAddress = MOUChat::ReadFixedString(Ready.HostAddress, static_cast<int32>(MOU::kMaxAddressLen));
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::ChatBroadcast:
	{
		const int32 FixedSize = static_cast<int32>(sizeof(MOU::ChatBroadcastBody));
		if (Body.Num() < FixedSize)
		{
			UE_LOG(LogMOUChat, Warning, TEXT("ChatBroadcast 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::ChatBroadcastBody Broadcast{};
		FMemory::Memcpy(&Broadcast, Body.GetData(), FixedSize);

		// 서버가 선언한 TextLen 과 실제 도착한 바이트 수가 맞는지 확인한다.
		// 이 검사가 없으면 TextLen 이 큰 값일 때 버퍼 밖을 읽는다.
		// 서버가 HandleChatSend 에서 하는 검사와 같은 방어를 클라이언트에서도 한다.
		if (FixedSize + static_cast<int32>(Broadcast.TextLen) != Body.Num())
		{
			UE_LOG(LogMOUChat, Warning, TEXT("ChatBroadcast 의 TextLen(%u) 이 실제 크기(%d) 와 맞지 않는다."),
				Broadcast.TextLen, Body.Num());
			break;
		}

		FChatMessage Message;
		Message.SenderUserId = static_cast<int64>(Broadcast.SenderUserId);
		Message.SenderName   = MOUChat::ReadFixedString(Broadcast.SenderName, static_cast<int32>(MOU::kMaxNameLen));
		Message.Channel      = static_cast<EChatChannelBP>(Broadcast.Channel);
		Message.Timestamp    = FDateTime::FromUnixTimestamp(Broadcast.Timestamp);
		Message.Text         = MOUChat::Utf8ToString(Body.GetData() + FixedSize, Broadcast.TextLen);

		InboundMessages.Enqueue(MoveTemp(Message));
		break;
	}

	default:
		// 서버가 나중에 새 오피코드를 추가해도 구버전 클라이언트가 죽지 않게 무시만 한다.
		// (서버는 반대로 모르는 오피코드가 오면 연결을 끊는다. 서버 쪽이 더 엄격한 게 맞다)
		UE_LOG(LogMOUChat, Verbose, TEXT("처리하지 않는 오피코드 %u 를 무시한다."), Header.Opcode);
		break;
	}
}

void FChatClientRunnable::EnqueuePacket(TArray<uint8>&& Packet)
{
	OutboundPackets.Enqueue(MoveTemp(Packet));
}

void FChatClientRunnable::PushEvent(EChatClientEventType Type, const FString& Detail)
{
	FChatClientEvent Event;
	Event.Type   = Type;
	Event.Detail = Detail;
	InboundEvents.Enqueue(MoveTemp(Event));
}

void FChatClientRunnable::DestroySocketIfNeeded()
{
	if (Socket != nullptr && SocketSubsystem != nullptr)
	{
		// Close 로 FIN 을 보낸 뒤 소켓 객체를 반납한다.
		// DestroySocket 을 빼먹으면 PIE 를 반복할 때마다 소켓 핸들이 샌다.
		Socket->Close();
		SocketSubsystem->DestroySocket(Socket);
	}
	Socket = nullptr;
}
