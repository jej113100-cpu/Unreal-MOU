#include "Server/Net/MOUIpNetDriver.h"

#include "Server/Chat/ChatTypes.h"   // LogMOUServer
#include "Server/Net/ChatFraming.h"  // MOU::RelayRegistrationDatagram
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Misc/ScopeLock.h"

namespace
{
	void SendRelayRegistration(FSocket* Socket, const FMOUPendingRelayRegistration& Registration,
	                           MOU::ERelayPeerRole Peer)
	{
		if (Socket == nullptr || !Registration.IsPresent())
		{
			return;
		}

		ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (Sockets == nullptr)
		{
			return;
		}

		TSharedRef<FInternetAddr> Destination = Sockets->CreateInternetAddr();
		bool bValidAddress = false;
		Destination->SetIp(*Registration.Address, bValidAddress);
		Destination->SetPort(Registration.Port);
		if (!bValidAddress)
		{
			UE_LOG(LogMOUServer, Warning, TEXT("[릴레이] 주소 '%s' 를 IP로 해석하지 못해 등록을 건너뛴다."),
				*Registration.Address);
			return;
		}

		const MOU::RelayRegistrationDatagram Datagram = MOU::MakeRelayRegistrationDatagram(
			Registration.RouteId, Peer, Registration.Token.GetData());
		int32 Sent = 0;
		for (int32 Attempt = 0; Attempt < 3; ++Attempt)
		{
			Socket->SendTo(reinterpret_cast<const uint8*>(&Datagram), sizeof(Datagram), Sent, *Destination);
		}

		UE_LOG(LogMOUServer, Log, TEXT("[릴레이] 실제 UE 소켓에서 %s:%d 로 %s 등록을 보냈다."),
			*Registration.Address, Registration.Port,
			Peer == MOU::ERelayPeerRole::Host ? TEXT("host") : TEXT("guest"));
	}
}

std::atomic<int32> UMOUIpNetDriver::DesiredClientPort{ 0 };
std::atomic<int32> UMOUIpNetDriver::DesiredListenPort{ 0 };
FCriticalSection UMOUIpNetDriver::RelayRegistrationMutex;
TArray<FMOUPendingRelayRegistration> UMOUIpNetDriver::PendingHostRelayRegistrations;
FMOUPendingRelayRegistration UMOUIpNetDriver::PendingClientRelayRegistration;

void UMOUIpNetDriver::SetDesiredClientPort(int32 Port)
{
	// 범위를 벗어난 값은 0(엔진 기본)으로 떨어뜨린다. 여기서 이상한 값을 통과시키면
	// bind 가 실패하고 접속 자체가 안 되는데, 그 실패는 원인이 한참 뒤에 드러난다.
	const int32 Clamped = (Port > 0 && Port <= 65535) ? Port : 0;
	DesiredClientPort.store(Clamped);

	UE_LOG(LogMOUServer, Log, TEXT("[넷드라이버] 클라이언트 바인드 포트를 %s 로 설정했다."),
		Clamped == 0 ? TEXT("임시 포트(기본)") : *FString::FromInt(Clamped));
}

int32 UMOUIpNetDriver::GetDesiredClientPort()
{
	return DesiredClientPort.load();
}

void UMOUIpNetDriver::SetDesiredListenPort(int32 Port)
{
	DesiredListenPort.store((Port > 0 && Port <= 65535) ? Port : 0);
}

void UMOUIpNetDriver::SetPendingHostRelayRegistrations(
	const TArray<FMOUPendingRelayRegistration>& Registrations)
{
	FScopeLock Lock(&RelayRegistrationMutex);
	PendingHostRelayRegistrations = Registrations;
}

void UMOUIpNetDriver::SetPendingClientRelayRegistration(
	const FMOUPendingRelayRegistration& Registration)
{
	FScopeLock Lock(&RelayRegistrationMutex);
	PendingClientRelayRegistration = Registration;
}

void UMOUIpNetDriver::ClearPendingRelayRegistrations()
{
	FScopeLock Lock(&RelayRegistrationMutex);
	PendingHostRelayRegistrations.Reset();
	PendingClientRelayRegistration = FMOUPendingRelayRegistration();
	DesiredListenPort.store(0);
}

int UMOUIpNetDriver::GetClientPort()
{
	const int32 Port = DesiredClientPort.load();
	if (Port == 0)
	{
		// 확보하지 못했다. 엔진 기본대로 임시 포트를 쓴다 —
		// 홀펀칭만 못 하고 접속 자체는 예전처럼 된다.
		return Super::GetClientPort();
	}

	UE_LOG(LogMOUServer, Log, TEXT("[넷드라이버] 클라이언트가 포트 %d 로 붙는다."), Port);
	return Port;
}

bool UMOUIpNetDriver::InitBase(bool bInitAsClient, FNetworkNotify* InNotify, const FURL& URL,
	bool bReuseAddressAndPort, FString& Error)
{
	const bool bInitialized = Super::InitBase(bInitAsClient, InNotify, URL, bReuseAddressAndPort, Error);
	if (!bInitialized || !bInitAsClient)
	{
		return bInitialized;
	}

	FMOUPendingRelayRegistration Registration;
	{
		FScopeLock Lock(&RelayRegistrationMutex);
		Registration = PendingClientRelayRegistration;
		PendingClientRelayRegistration = FMOUPendingRelayRegistration();
	}
	SendRelayRegistration(GetSocket(), Registration, MOU::ERelayPeerRole::Guest);
	return true;
}

bool UMOUIpNetDriver::InitListen(FNetworkNotify* InNotify, FURL& ListenURL,
	bool bReuseAddressAndPort, FString& Error)
{
	const int32 RequestedPort = DesiredListenPort.exchange(0);
	if (RequestedPort > 0 && RequestedPort <= 65535)
	{
		// URL 옵션 문자열에 의존하지 않고, NetDriver가 실제 bind에 쓸 값을 직접 정한다.
		ListenURL.Port = RequestedPort;
	}

	const bool bInitialized = Super::InitListen(InNotify, ListenURL, bReuseAddressAndPort, Error);
	if (!bInitialized)
	{
		return false;
	}

	TArray<FMOUPendingRelayRegistration> Registrations;
	{
		FScopeLock Lock(&RelayRegistrationMutex);
		Registrations = MoveTemp(PendingHostRelayRegistrations);
		PendingHostRelayRegistrations.Reset();
	}
	for (const FMOUPendingRelayRegistration& Registration : Registrations)
	{
		SendRelayRegistration(GetSocket(), Registration, MOU::ERelayPeerRole::Host);
	}
	return true;
}
