// MOU 채팅 - 워커 스레드 구현.
// 대응하는 서버 코드: MOU_Server/Server/Server.cpp 의 ClientThread()

#include "Server/Net/ServerClientRunnable.h"

#include "HAL/PlatformProcess.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

FServerClientRunnable::FServerClientRunnable(const FString& InHost, int32 InPort)
	: Host(InHost)
	, Port(InPort)
{
}

FServerClientRunnable::~FServerClientRunnable()
{
	// 정상 경로에서는 UServerSubsystem 이 Stop() -> Kill(true) 로 스레드를 먼저 정리하므로
	// 여기 도달했을 때 Socket 은 이미 nullptr 이다. 만약을 대비한 안전망.
	DestroySocketIfNeeded();
}

bool FServerClientRunnable::Init()
{
	// 여기서는 블로킹 작업을 하지 않는다 (헤더 주석 참고).
	// 서브시스템 포인터를 얻는 정도만 한다.
	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		UE_LOG(LogMOUServer, Error, TEXT("소켓 서브시스템을 얻지 못했다. 채팅을 사용할 수 없다."));
		return false;
	}
	return true;
}

uint32 FServerClientRunnable::Run()
{
	// 바깥 루프 = 재연결 루프.
	// 서버가 꺼져 있거나 도중에 죽어도 게임은 계속 돌아가야 하므로,
	// 실패를 치명적 오류로 보지 않고 계속 재시도한다.
	while (!bStopRequested)
	{
		PushEvent(EServerClientEventType::Connecting);

		FString ConnectError;
		if (!ConnectOnce(ConnectError))
		{
			DestroySocketIfNeeded();
			PushEvent(EServerClientEventType::ConnectFailed, ConnectError);

			// 곧바로 재시도하면 서버가 없을 때 로그가 폭주한다.
			// 다만 통째로 3초를 자면 Stop() 요청에 3초 늦게 반응하므로 잘게 쪼개서 잔다.
			for (int32 Slept = 0; Slept < ReconnectDelayMilliseconds && !bStopRequested; Slept += WaitMilliseconds)
			{
				FPlatformProcess::Sleep(WaitMilliseconds / 1000.0f);
			}
			continue;
		}

		UE_LOG(LogMOUServer, Log, TEXT("서버 연결 성공: %s:%d"), *Host, Port);
		PushEvent(EServerClientEventType::Connected);

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
				UE_LOG(LogMOUServer, Warning, TEXT("재접속하면서 미전송 패킷 %d개를 버렸다."), DiscardedCount);
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
		PushEvent(EServerClientEventType::Disconnected);

		if (!bStopRequested)
		{
			UE_LOG(LogMOUServer, Warning, TEXT("서버 연결이 끊겼다. %dms 후 재시도한다."), ReconnectDelayMilliseconds);
			for (int32 Slept = 0; Slept < ReconnectDelayMilliseconds && !bStopRequested; Slept += WaitMilliseconds)
			{
				FPlatformProcess::Sleep(WaitMilliseconds / 1000.0f);
			}
		}
	}

	return 0;
}

void FServerClientRunnable::Stop()
{
	// 게임 스레드에서 호출된다. 플래그만 세우고 즉시 반환한다.
	// 워커는 최대 WaitMilliseconds 안에 이 플래그를 보고 빠져나온다.
	bStopRequested = true;
}

void FServerClientRunnable::Exit()
{
	// Run() 이 끝난 뒤 워커 스레드에서 호출된다. 소켓 정리는 여기가 마지막 기회다.
	DestroySocketIfNeeded();
}

bool FServerClientRunnable::ConnectOnce(FString& OutError)
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

bool FServerClientRunnable::PumpSend()
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
				UE_LOG(LogMOUServer, Warning, TEXT("패킷 전송 실패. 연결을 끊는다."));
				return false;
			}
			TotalSent += Sent;
		}
	}
	return true;
}

bool FServerClientRunnable::PumpRecv()
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
		UE_LOG(LogMOUServer, Warning, TEXT("Recv 실패 (오류 코드 %d)"), static_cast<int32>(LastError));
		return false;
	}

	// Wait 이 true 를 줬는데 0바이트면 서버가 연결을 정상 종료(FIN)한 것이다.
	if (BytesRead <= 0)
	{
		UE_LOG(LogMOUServer, Log, TEXT("서버가 연결을 종료했다."));
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
			UE_LOG(LogMOUServer, Error, TEXT("비정상 패킷 크기. 스트림이 깨졌으므로 재접속한다."));
			return false;
		}

		HandlePacket(Header, BodyScratch);
	}

	return true;
}

void FServerClientRunnable::HandlePacket(const MOU::PacketHeader& Header, const TArray<uint8>& Body)
{
	switch (static_cast<MOU::EOpcode>(Header.Opcode))
	{
	case MOU::EOpcode::LoginAck:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::LoginAckBody)))
		{
			UE_LOG(LogMOUServer, Warning, TEXT("LoginAck 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::LoginAckBody Ack{};
		FMemory::Memcpy(&Ack, Body.GetData(), sizeof(Ack));

		FServerClientEvent Event;
		Event.Type                = EServerClientEventType::LoginAck;
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
			UE_LOG(LogMOUServer, Error,
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
			UE_LOG(LogMOUServer, Warning, TEXT("RegisterAck 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::RegisterAckBody Ack{};
		FMemory::Memcpy(&Ack, Body.GetData(), sizeof(Ack));

		// 가입 결과는 신원이 아니므로 UserId/Name 은 채우지 않는다.
		// bSuccess 와 Result 만 의미가 있다.
		FServerClientEvent Event;
		Event.Type                = EServerClientEventType::RegisterAck;
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
			UE_LOG(LogMOUServer, Warning, TEXT("RoomCreateAck 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::RoomCreateAckBody Ack{};
		FMemory::Memcpy(&Ack, Body.GetData(), sizeof(Ack));

		FServerClientEvent Event;
		Event.Type         = EServerClientEventType::RoomCreateAck;
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
			UE_LOG(LogMOUServer, Warning, TEXT("RoomListAck 크기가 부족하다 (%d바이트)"), Body.Num());
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
			UE_LOG(LogMOUServer, Warning,
				TEXT("RoomListAck 가 %d개라고 했지만 %d개분만 도착했다"), Declared, Available);
		}

		FServerClientEvent Event;
		Event.Type = EServerClientEventType::RoomListAck;
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
			UE_LOG(LogMOUServer, Warning, TEXT("RoomJoinAck 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::RoomJoinAckBody Ack{};
		FMemory::Memcpy(&Ack, Body.GetData(), sizeof(Ack));

		FServerClientEvent Event;
		Event.Type             = EServerClientEventType::RoomJoinAck;
		Event.Join.bSuccess    = (Ack.bSuccess != 0);
		Event.Join.RoomId      = static_cast<int32>(Ack.RoomId);
		Event.Join.Result      = static_cast<EMOURoomResultBP>(Ack.Result);
		MOUChat::ReadHostCandidates(Ack.Candidates, Ack.CandidateCount, Event.Join.Candidates);
		Event.Join.bLanOnly    = (Ack.bLanOnly != 0);
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::RoomMemberList:
	{
		const int32 FixedSize = static_cast<int32>(sizeof(MOU::RoomMemberListBody));
		if (Body.Num() < FixedSize)
		{
			UE_LOG(LogMOUServer, Warning, TEXT("RoomMemberList 크기가 부족하다 (%d바이트)"), Body.Num());
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
			UE_LOG(LogMOUServer, Warning,
				TEXT("RoomMemberList 가 %d명이라고 했지만 %d명분만 도착했다"), Declared, Available);
		}

		FServerClientEvent Event;
		Event.Type      = EServerClientEventType::RoomMemberList;
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
			UE_LOG(LogMOUServer, Warning, TEXT("RoomClosed 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::RoomClosedBody Closed{};
		FMemory::Memcpy(&Closed, Body.GetData(), sizeof(Closed));

		FServerClientEvent Event;
		Event.Type        = EServerClientEventType::RoomClosed;
		Event.RoomId      = static_cast<int32>(Closed.RoomId);
		Event.CloseReason = static_cast<EMOURoomCloseReasonBP>(Closed.Reason);
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::RoomStart:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::RoomStartBody)))
		{
			UE_LOG(LogMOUServer, Warning, TEXT("RoomStart 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::RoomStartBody Start{};
		FMemory::Memcpy(&Start, Body.GetData(), sizeof(Start));

		// 호스트 주소를 RoomJoinAck 와 같은 그릇에 담는다.
		// 받는 쪽에서 MakeTravelURL() 을 그대로 쓸 수 있어 처리 경로가 하나로 모인다.
		FServerClientEvent Event;
		Event.Type             = EServerClientEventType::RoomStart;
		Event.RoomId           = static_cast<int32>(Start.RoomId);
		Event.Join.bSuccess    = true;
		Event.Join.RoomId      = static_cast<int32>(Start.RoomId);
		MOUChat::ReadHostCandidates(Start.Candidates, Start.CandidateCount, Event.Join.Candidates);

		// 홀펀칭 대상 (v10). 방장에게만 의미가 있고 참여자는 무시한다.
		Event.PunchTargets.Reset();
		{
			const int32 SafeCount = FMath::Min<int32>(Start.PunchTargetCount, MOU::kMaxPlayersInRoom);
			for (int32 i = 0; i < SafeCount; ++i)
			{
				FMOUHostCandidate Peer;
				Peer.Address = MOUChat::ReadFixedString(Start.PunchTargets[i].Address, static_cast<int32>(MOU::kMaxAddressLen));
				Peer.Port    = static_cast<int32>(Start.PunchTargets[i].Port);
				if (Peer.IsValid())
				{
					Event.PunchTargets.Add(MoveTemp(Peer));
				}
			}
		}

		// relay host-facing 경로는 방장에게만 내려온다. 다른 참여자는 Count=0 이다.
		Event.HostRelayRoutes.Reset();
		const int32 RelayCount = FMath::Min<int32>(Start.RelayRouteCount, MOU::kMaxRelayRoutes);
		for (int32 i = 0; i < RelayCount; ++i)
		{
			FMOUGameRelayRoute Route = MOUChat::ReadRelayHostRoute(Start.RelayRoutes[i]);
			if (Route.IsValid())
			{
				Event.HostRelayRoutes.Add(MoveTemp(Route));
			}
		}
		Event.Join.bLanOnly    = (Start.bLanOnly != 0);
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::RoomHostReady:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::RoomHostReadyBody)))
		{
			UE_LOG(LogMOUServer, Warning, TEXT("RoomHostReady 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::RoomHostReadyBody Ready{};
		FMemory::Memcpy(&Ready, Body.GetData(), sizeof(Ready));

		// RoomStart 와 같은 그릇에 담는다. 받는 쪽에서 MakeTravelURL() 을 그대로 쓴다.
		FServerClientEvent Event;
		Event.Type             = EServerClientEventType::RoomHostReady;
		Event.RoomId           = static_cast<int32>(Ready.RoomId);
		Event.Join.bSuccess    = true;
		Event.Join.RoomId      = static_cast<int32>(Ready.RoomId);
		MOUChat::ReadHostCandidates(Ready.Candidates, Ready.CandidateCount, Event.Join.Candidates);
		Event.Join.bLanOnly    = (Ready.bLanOnly != 0);
		Event.GuestRelayRoute  = MOUChat::ReadRelayGuestRoute(Ready.Relay);
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::ClientEndpointAck:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::ClientEndpointAckBody)))
		{
			break;
		}

		MOU::ClientEndpointAckBody Ack{};
		FMemory::Memcpy(&Ack, Body.GetData(), sizeof(Ack));

		FServerClientEvent Event;
		Event.Type       = EServerClientEventType::ClientEndpointAck;
		Event.ProbeNonce = Ack.Nonce;
		Event.bProbeSent = (Ack.bObserved != 0);
		Event.Detail     = FString::Printf(TEXT("%s:%d"),
			*MOUChat::ReadFixedString(Ack.Address, static_cast<int32>(MOU::kMaxAddressLen)),
			static_cast<int32>(Ack.Port));
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::HostProbeSent:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::HostProbeSentBody)))
		{
			UE_LOG(LogMOUServer, Warning, TEXT("HostProbeSent 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::HostProbeSentBody Sent{};
		FMemory::Memcpy(&Sent, Body.GetData(), sizeof(Sent));

		FServerClientEvent Event;
		Event.Type       = EServerClientEventType::HostProbeSent;
		Event.ProbeNonce = Sent.Nonce;
		Event.bProbeSent = (Sent.bSent != 0);
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::ChatBroadcast:
	{
		const int32 FixedSize = static_cast<int32>(sizeof(MOU::ChatBroadcastBody));
		if (Body.Num() < FixedSize)
		{
			UE_LOG(LogMOUServer, Warning, TEXT("ChatBroadcast 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::ChatBroadcastBody Broadcast{};
		FMemory::Memcpy(&Broadcast, Body.GetData(), FixedSize);

		// 서버가 선언한 TextLen 과 실제 도착한 바이트 수가 맞는지 확인한다.
		// 이 검사가 없으면 TextLen 이 큰 값일 때 버퍼 밖을 읽는다.
		// 서버가 HandleChatSend 에서 하는 검사와 같은 방어를 클라이언트에서도 한다.
		if (FixedSize + static_cast<int32>(Broadcast.TextLen) != Body.Num())
		{
			UE_LOG(LogMOUServer, Warning, TEXT("ChatBroadcast 의 TextLen(%u) 이 실제 크기(%d) 와 맞지 않는다."),
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

	// ------------------------------------------------------------------
	// 친구 (v7)
	// ------------------------------------------------------------------

	case MOU::EOpcode::FriendListAck:
	{
		const int32 FixedSize = static_cast<int32>(sizeof(MOU::FriendListAckBody));
		if (Body.Num() < FixedSize)
		{
			UE_LOG(LogMOUServer, Warning, TEXT("FriendListAck 크기가 부족하다 (%d바이트)"), Body.Num());
			break;
		}

		MOU::FriendListAckBody Head{};
		FMemory::Memcpy(&Head, Body.GetData(), sizeof(Head));

		// RoomMemberList 와 같은 이유로, 서버가 주장하는 개수를 실제 바이트로 검산한다.
		const int32 EntrySize = static_cast<int32>(sizeof(MOU::FriendEntry));
		const int32 Declared  = static_cast<int32>(Head.Count);
		const int32 Available = (Body.Num() - FixedSize) / EntrySize;
		const int32 SafeCount = FMath::Min(Declared, Available);

		if (SafeCount < Declared)
		{
			UE_LOG(LogMOUServer, Warning,
				TEXT("FriendListAck 가 %d명이라고 했지만 %d명분만 도착했다"), Declared, Available);
		}

		FServerClientEvent Event;
		Event.Type = EServerClientEventType::FriendListAck;
		Event.Friends.Reserve(SafeCount);

		for (int32 i = 0; i < SafeCount; ++i)
		{
			MOU::FriendEntry Src{};
			FMemory::Memcpy(&Src, Body.GetData() + FixedSize + i * EntrySize, sizeof(Src));

			FMOUFriend Entry;
			Entry.UserId      = static_cast<int64>(Src.UserId);
			Entry.Nickname    = MOUChat::ReadFixedString(Src.Nickname, static_cast<int32>(MOU::kMaxNameLen));
			Entry.State       = static_cast<EMOUFriendStateBP>(Src.State);
			Entry.Presence    = static_cast<EMOUPresenceBP>(Src.Presence);
			Entry.UnreadCount = static_cast<int32>(Src.UnreadCount);
			Entry.bIsOnline   = (Entry.Presence != EMOUPresenceBP::Offline);
			Event.Friends.Add(MoveTemp(Entry));
		}

		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::FriendAddAck:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::FriendAddAckBody)))
		{
			break;
		}

		MOU::FriendAddAckBody Ack{};
		FMemory::Memcpy(&Ack, Body.GetData(), sizeof(Ack));

		FServerClientEvent Event;
		Event.Type           = EServerClientEventType::FriendAddAck;
		Event.bFriendSuccess = (Ack.bSuccess != 0);
		Event.FriendResult   = static_cast<EMOUFriendResultBP>(Ack.Result);
		Event.TargetUserId   = static_cast<int64>(Ack.TargetUserId);
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::FriendRequestIncoming:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::FriendRequestIncomingBody)))
		{
			break;
		}

		MOU::FriendRequestIncomingBody Note{};
		FMemory::Memcpy(&Note, Body.GetData(), sizeof(Note));

		FServerClientEvent Event;
		Event.Type            = EServerClientEventType::FriendRequestIncoming;
		Event.Friend.UserId   = static_cast<int64>(Note.FromUserId);
		Event.Friend.Nickname = MOUChat::ReadFixedString(Note.FromNickname, static_cast<int32>(MOU::kMaxNameLen));
		// 받은 신청이므로 상태는 정해져 있다. 서버가 따로 안 보낸다.
		Event.Friend.State    = EMOUFriendStateBP::PendingIncoming;
		Event.Friend.Presence = EMOUPresenceBP::Offline;
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::FriendUpdate:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::FriendUpdateBody)))
		{
			break;
		}

		MOU::FriendUpdateBody Src{};
		FMemory::Memcpy(&Src, Body.GetData(), sizeof(Src));

		FServerClientEvent Event;
		Event.Type            = EServerClientEventType::FriendUpdate;
		Event.bFriendRemoved  = (Src.bRemoved != 0);
		Event.Friend.UserId   = static_cast<int64>(Src.UserId);
		Event.Friend.Nickname = MOUChat::ReadFixedString(Src.Nickname, static_cast<int32>(MOU::kMaxNameLen));
		Event.Friend.State    = static_cast<EMOUFriendStateBP>(Src.State);
		Event.Friend.Presence = static_cast<EMOUPresenceBP>(Src.Presence);
		Event.Friend.bIsOnline = (Event.Friend.Presence != EMOUPresenceBP::Offline);
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::FriendPresence:
	{
		if (Body.Num() < static_cast<int32>(sizeof(MOU::FriendPresenceBody)))
		{
			break;
		}

		MOU::FriendPresenceBody Src{};
		FMemory::Memcpy(&Src, Body.GetData(), sizeof(Src));

		FServerClientEvent Event;
		Event.Type            = EServerClientEventType::FriendPresence;
		Event.Friend.UserId   = static_cast<int64>(Src.UserId);
		Event.Friend.Presence = static_cast<EMOUPresenceBP>(Src.Presence);
		Event.Friend.bIsOnline = (Event.Friend.Presence != EMOUPresenceBP::Offline);
		// ★ Nickname 은 비어 있다. 이 패킷은 9바이트라 이름을 싣지 않는다 -
		//   받는 쪽이 기존 목록에서 UserId 로 찾아 상태만 갈아끼워야 한다.
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	// ------------------------------------------------------------------
	// 메신저 (v7)
	// ------------------------------------------------------------------

	case MOU::EOpcode::DirectMessage:
	{
		const int32 FixedSize = static_cast<int32>(sizeof(MOU::DirectMessageBody));
		if (Body.Num() < FixedSize)
		{
			break;
		}

		MOU::DirectMessageBody Src{};
		FMemory::Memcpy(&Src, Body.GetData(), FixedSize);

		// ChatBroadcast 와 같은 방어. 이게 없으면 위조된 TextLen 에 버퍼 밖을 읽는다.
		if (FixedSize + static_cast<int32>(Src.TextLen) != Body.Num())
		{
			UE_LOG(LogMOUServer, Warning,
				TEXT("DirectMessage 의 TextLen(%u) 이 실제 크기(%d) 와 맞지 않는다."),
				Src.TextLen, Body.Num());
			break;
		}

		FServerClientEvent Event;
		Event.Type = EServerClientEventType::DirectMessage;
		Event.DirectMessage.MessageId  = static_cast<int64>(Src.MessageId);
		Event.DirectMessage.FromUserId = static_cast<int64>(Src.FromUserId);
		Event.DirectMessage.ToUserId   = static_cast<int64>(Src.ToUserId);
		Event.DirectMessage.Timestamp  = static_cast<int64>(Src.Timestamp);
		Event.DirectMessage.Text       = MOUChat::Utf8ToString(Body.GetData() + FixedSize, Src.TextLen);
		// bIsMine / PeerUserId 는 내 UserId 를 아는 UServerSubsystem 이 채운다.
		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	case MOU::EOpcode::DmHistoryAck:
	{
		const int32 FixedSize = static_cast<int32>(sizeof(MOU::DmHistoryAckBody));
		if (Body.Num() < FixedSize)
		{
			break;
		}

		MOU::DmHistoryAckBody Head{};
		FMemory::Memcpy(&Head, Body.GetData(), sizeof(Head));

		FServerClientEvent Event;
		Event.Type            = EServerClientEventType::DmHistoryAck;
		Event.PeerUserId      = static_cast<int64>(Head.PeerUserId);
		Event.bHasMoreHistory = (Head.bHasMore != 0);
		Event.History.Reserve(Head.Count);

		// ★★ DmEntry 는 **고정 크기가 아니다.** 뒤에 TextLen 바이트가 따라오므로
		//   배열 인덱싱으로 읽을 수 없고 앞에서부터 순회해야 한다.
		//   매 단계에서 남은 바이트를 확인하지 않으면 버퍼 밖을 읽는다.
		int32 Offset = FixedSize;

		for (uint16 i = 0; i < Head.Count; ++i)
		{
			if (Offset + static_cast<int32>(sizeof(MOU::DmEntry)) > Body.Num())
			{
				UE_LOG(LogMOUServer, Warning, TEXT("DmHistoryAck 가 중간에 잘렸다 (%d번째)"), i);
				break;
			}

			MOU::DmEntry Entry{};
			FMemory::Memcpy(&Entry, Body.GetData() + Offset, sizeof(Entry));
			Offset += static_cast<int32>(sizeof(Entry));

			if (Offset + static_cast<int32>(Entry.TextLen) > Body.Num())
			{
				UE_LOG(LogMOUServer, Warning, TEXT("DmHistoryAck 본문이 잘렸다 (%d번째)"), i);
				break;
			}

			FMOUDirectMessage Msg;
			Msg.MessageId  = static_cast<int64>(Entry.MessageId);
			Msg.FromUserId = static_cast<int64>(Entry.FromUserId);
			Msg.Timestamp  = static_cast<int64>(Entry.Timestamp);
			Msg.Text       = MOUChat::Utf8ToString(Body.GetData() + Offset, Entry.TextLen);
			Msg.PeerUserId = Event.PeerUserId;
			Offset += static_cast<int32>(Entry.TextLen);

			Event.History.Add(MoveTemp(Msg));
		}

		InboundEvents.Enqueue(MoveTemp(Event));
		break;
	}

	default:
		// 서버가 나중에 새 오피코드를 추가해도 구버전 클라이언트가 죽지 않게 무시만 한다.
		// (서버는 반대로 모르는 오피코드가 오면 연결을 끊는다. 서버 쪽이 더 엄격한 게 맞다)
		UE_LOG(LogMOUServer, Verbose, TEXT("처리하지 않는 오피코드 %u 를 무시한다."), Header.Opcode);
		break;
	}
}

void FServerClientRunnable::EnqueuePacket(TArray<uint8>&& Packet)
{
	OutboundPackets.Enqueue(MoveTemp(Packet));
}

void FServerClientRunnable::PushEvent(EServerClientEventType Type, const FString& Detail)
{
	FServerClientEvent Event;
	Event.Type   = Type;
	Event.Detail = Detail;
	InboundEvents.Enqueue(MoveTemp(Event));
}

void FServerClientRunnable::DestroySocketIfNeeded()
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
