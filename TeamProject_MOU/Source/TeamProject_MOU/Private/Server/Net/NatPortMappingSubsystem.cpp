#include "Server/Net/NatPortMappingSubsystem.h"

#include "HAL/RunnableThread.h"
#include "Server/Chat/ChatTypes.h"          // LogMOUServer
#include "Server/Net/NatMappingRunnable.h"

// ─────────────────────────────────────────────────────────────────────
// BP enum 과 순수 C++ enum 이 어긋나면 조용히 엉뚱한 사유가 표시된다.
// 컴파일 타임에 막아둔다 (ChatFraming.h 가 서버 enum 을 검사하는 것과 같은 방식).
// ─────────────────────────────────────────────────────────────────────
static_assert(static_cast<uint8>(EMOUNatResultBP::Success)         == static_cast<uint8>(ENatMapResult::Success),         "NAT enum 불일치");
static_assert(static_cast<uint8>(EMOUNatResultBP::NoGatewayFound)  == static_cast<uint8>(ENatMapResult::NoGatewayFound),  "NAT enum 불일치");
static_assert(static_cast<uint8>(EMOUNatResultBP::CarrierGradeNat) == static_cast<uint8>(ENatMapResult::CarrierGradeNat), "NAT enum 불일치");
static_assert(static_cast<uint8>(EMOUNatResultBP::PortConflict)    == static_cast<uint8>(ENatMapResult::PortConflict),    "NAT enum 불일치");
static_assert(static_cast<uint8>(EMOUNatResultBP::GatewayRefused)  == static_cast<uint8>(ENatMapResult::GatewayRefused),  "NAT enum 불일치");
static_assert(static_cast<uint8>(EMOUNatResultBP::NetworkError)    == static_cast<uint8>(ENatMapResult::NetworkError),    "NAT enum 불일치");
static_assert(static_cast<uint8>(EMOUNatResultBP::Timeout)         == static_cast<uint8>(ENatMapResult::Timeout),         "NAT enum 불일치");
static_assert(static_cast<uint8>(EMOUNatResultBP::Unknown)         == static_cast<uint8>(ENatMapResult::Unknown),         "NAT enum 불일치");

void UNatPortMappingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UNatPortMappingSubsystem::Tick));
}

void UNatPortMappingSubsystem::Deinitialize()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}

	// ★ 마지막 안전망. 방을 나가면서 ReleasePortMapping 을 못 불렀더라도
	//   여기서는 반드시 지운다. 안 그러면 공유기에 매핑이 남는다.
	ShutdownWorker();

	Super::Deinitialize();
}

void UNatPortMappingSubsystem::BeginPortMapping(int32 InternalPort, int32 DesiredExternalPort, int32 LeaseSeconds)
{
	if (InternalPort <= 0 || InternalPort > 65535)
	{
		UE_LOG(LogMOUServer, Warning, TEXT("[NAT] 잘못된 포트 번호: %d"), InternalPort);
		return;
	}

	if (bInProgress)
	{
		UE_LOG(LogMOUServer, Warning, TEXT("[NAT] 이미 포트 열기를 진행 중이다."));
		return;
	}

	// 이전 매핑이 남아 있으면 먼저 지운다. 방을 옮길 때 옛 포트가 계속 열려 있으면 안 된다.
	ShutdownWorker();

	Worker = new FNatMappingRunnable(
		static_cast<uint16>(InternalPort),
		static_cast<uint16>(FMath::Clamp(DesiredExternalPort, 0, 65535)),
		/*bUdp=*/true,                     // UE 리플리케이션은 UDP 다. TCP 로 열면 아무 효과가 없다
		TEXT("MOU"),
		static_cast<uint32>(FMath::Max(LeaseSeconds, 0)));

	WorkerThread = FRunnableThread::Create(Worker, TEXT("MOU.NatPortMapping"));

	if (WorkerThread == nullptr)
	{
		UE_LOG(LogMOUServer, Error, TEXT("[NAT] 워커 스레드를 만들지 못했다."));
		delete Worker;
		Worker = nullptr;
		return;
	}

	bInProgress        = true;
	MappedExternalPort = 0;
	LastResult         = EMOUNatResultBP::Unknown;
	LastExternalIp.Reset();

	UE_LOG(LogMOUServer, Log, TEXT("[NAT] 포트 열기 시작. 내부 포트 %d"), InternalPort);
}

void UNatPortMappingSubsystem::ReleasePortMapping()
{
	ShutdownWorker();
}

void UNatPortMappingSubsystem::ShutdownWorker()
{
	if (WorkerThread != nullptr)
	{
		// ★ 이 순서를 지키지 않으면 PIE 를 껐다 켤 때 에디터가 통째로 죽는다.
		//   (FServerClientRunnable 정리와 같은 규칙)
		//   1. Stop() 으로 종료를 요청하고
		//   2. Kill(true) 로 워커가 Run() 을 빠져나올 때까지 기다린다
		//      — 이때 워커가 DeletePortMapping 을 부르므로 몇 백 ms 걸릴 수 있다.
		//        그 대기가 곧 "공유기에서 포트를 확실히 지웠다" 는 보장이다.
		//   3. 스레드가 완전히 끝난 뒤에 객체를 지운다
		if (Worker != nullptr)
		{
			Worker->Stop();
		}

		WorkerThread->Kill(/*bShouldWait=*/true);
		delete WorkerThread;
		WorkerThread = nullptr;
	}

	if (Worker != nullptr)
	{
		delete Worker;
		Worker = nullptr;
	}

	bInProgress        = false;
	MappedExternalPort = 0;
}

bool UNatPortMappingSubsystem::Tick(float /*DeltaTime*/)
{
	if (Worker == nullptr)
	{
		return true;
	}

	// 워커가 넣어둔 결과를 게임 스레드에서 꺼내 쏜다.
	// 델리게이트 브로드캐스트가 여기서 일어나야 하는 이유는, 구독자가 UMG 위젯이라
	// 워커 스레드에서 부르면 UObject 를 건드리게 되기 때문이다.
	FNatMappingOutcome Outcome;
	while (Worker->DequeueOutcome(Outcome))
	{
		bInProgress        = false;
		LastResult         = static_cast<EMOUNatResultBP>(Outcome.Result);
		LastExternalIp     = Outcome.ExternalIp;
		MappedExternalPort = Outcome.IsSuccess() ? Outcome.ExternalPort : 0;

		if (Outcome.IsSuccess())
		{
			UE_LOG(LogMOUServer, Log,
			       TEXT("[NAT] 포트 열기 성공. 외부 %d -> 내부 %d (외부 IP %s)"),
			       static_cast<int32>(Outcome.ExternalPort),
			       static_cast<int32>(Outcome.InternalPort),
			       LastExternalIp.IsEmpty() ? TEXT("확인 못함") : *LastExternalIp);
		}
		else
		{
			UE_LOG(LogMOUServer, Warning, TEXT("[NAT] 포트 열기 실패: %s"),
			       *GetNatResultText(LastResult).ToString());
		}

		OnNatMappingFinished.Broadcast(LastResult, MappedExternalPort, LastExternalIp);

		// ★ 실패했으면 워커는 이미 스스로 끝났다. 지울 매핑도 없으므로 정리해 둔다.
		//   성공했으면 워커를 살려둔다 — 그 스레드의 수명이 곧 매핑의 수명이고,
		//   ReleasePortMapping 이 불릴 때 나가면서 공유기에서 지운다.
		if (!Outcome.IsSuccess())
		{
			ShutdownWorker();
			break;
		}
	}

	return true;   // 계속 틱한다
}

FText UNatPortMappingSubsystem::GetNatResultText(EMOUNatResultBP Result)
{
	switch (Result)
	{
	case EMOUNatResultBP::Success:
		return NSLOCTEXT("MOUNat", "Success", "공유기에 포트를 열었습니다.");

	// ★ 아래 실패 문구들이 "같은 네트워크에서만 접속할 수 있습니다" 라고 단정하면 안 된다.
	//   UPnP 는 포트를 *자동으로* 여는 수단일 뿐이고, 공유기에 포트포워딩이 수동으로
	//   걸려 있으면 (그리고 서버가 --public-ip 로 공인 주소를 방에 기록하면) UPnP 가
	//   실패해도 외부 네트워크에서 멀쩡히 들어온다. 실제 운영 환경이 그 구성이라,
	//   예전 문구는 다른 네트워크의 참가자가 잘 들어오고 있는데도 "못 들어온다" 고
	//   거짓말을 하고 있었다. 확인된 사실(자동으로 못 열었다)만 말한다.
	//   유일한 예외가 CarrierGradeNat 이다 — 그때는 수동 포워딩도 소용이 없다.
	case EMOUNatResultBP::NoGatewayFound:
		return NSLOCTEXT("MOUNat", "NoGateway",
			"공유기가 UPnP 에 응답하지 않아 포트를 자동으로 열지 못했습니다. "
			"공유기에 포트포워딩이 되어 있으면 외부에서도 접속할 수 있습니다.");

	case EMOUNatResultBP::CarrierGradeNat:
		return NSLOCTEXT("MOUNat", "Cgnat",
			"통신사 NAT 환경이라 포트를 열 수 없습니다. 같은 네트워크에서만 접속할 수 있습니다.");

	case EMOUNatResultBP::PortConflict:
		return NSLOCTEXT("MOUNat", "Conflict",
			"쓸 수 있는 외부 포트를 찾지 못했습니다. "
			"공유기에 포트포워딩이 되어 있으면 외부에서도 접속할 수 있습니다.");

	case EMOUNatResultBP::GatewayRefused:
		return NSLOCTEXT("MOUNat", "Refused",
			"공유기가 포트 열기 요청을 거부했습니다. "
			"공유기에 포트포워딩이 되어 있으면 외부에서도 접속할 수 있습니다.");

	case EMOUNatResultBP::NetworkError:
		return NSLOCTEXT("MOUNat", "NetworkError",
			"네트워크 오류로 포트를 자동으로 열지 못했습니다. "
			"공유기에 포트포워딩이 되어 있으면 외부에서도 접속할 수 있습니다.");

	case EMOUNatResultBP::Timeout:
		return NSLOCTEXT("MOUNat", "Timeout",
			"공유기가 제때 응답하지 않아 포트를 자동으로 열지 못했습니다. "
			"공유기에 포트포워딩이 되어 있으면 외부에서도 접속할 수 있습니다.");

	default:
		return NSLOCTEXT("MOUNat", "Unknown",
			"알 수 없는 이유로 포트를 자동으로 열지 못했습니다. "
			"공유기에 포트포워딩이 되어 있으면 외부에서도 접속할 수 있습니다.");
	}
}

// ─────────────────────────────────────────────────────────────────────
// 검증용 콘솔 명령
//
// 실제 공유기가 있어야 의미가 있으므로 개발 빌드에만 넣는다.
//   MOU.Nat.Open [내부포트]   포트 열기 시도
//   MOU.Nat.Close             열어둔 포트 닫기
//   MOU.Nat.Status            지금 상태 출력
// ─────────────────────────────────────────────────────────────────────
#if !UE_BUILD_SHIPPING
namespace
{
	UNatPortMappingSubsystem* FindNatSubsystem()
	{
		if (GEngine == nullptr)
		{
			return nullptr;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() != nullptr && Context.OwningGameInstance != nullptr)
			{
				if (UNatPortMappingSubsystem* Subsystem =
					Context.OwningGameInstance->GetSubsystem<UNatPortMappingSubsystem>())
				{
					return Subsystem;
				}
			}
		}
		return nullptr;
	}

	FAutoConsoleCommand OpenCommand(
		TEXT("MOU.Nat.Open"),
		TEXT("공유기에 포트를 열어달라고 요청한다. 사용법: MOU.Nat.Open [내부포트=7777]"),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			if (UNatPortMappingSubsystem* Subsystem = FindNatSubsystem())
			{
				const int32 Port = (Args.Num() > 0) ? FCString::Atoi(*Args[0]) : 7777;
				Subsystem->BeginPortMapping(Port);
			}
		}));

	FAutoConsoleCommand CloseCommand(
		TEXT("MOU.Nat.Close"),
		TEXT("열어둔 포트를 닫는다."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			if (UNatPortMappingSubsystem* Subsystem = FindNatSubsystem())
			{
				Subsystem->ReleasePortMapping();
				UE_LOG(LogMOUServer, Log, TEXT("[NAT] 포트를 닫았다."));
			}
		}));

	FAutoConsoleCommand StatusCommand(
		TEXT("MOU.Nat.Status"),
		TEXT("지금 포트 매핑 상태를 출력한다."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			if (UNatPortMappingSubsystem* Subsystem = FindNatSubsystem())
			{
				UE_LOG(LogMOUServer, Log,
				       TEXT("[NAT] 진행중=%s 외부포트=%d 외부IP=%s 결과=%s"),
				       Subsystem->IsMappingInProgress() ? TEXT("예") : TEXT("아니오"),
				       Subsystem->GetMappedExternalPort(),
				       Subsystem->GetExternalIp().IsEmpty() ? TEXT("(없음)") : *Subsystem->GetExternalIp(),
				       *UNatPortMappingSubsystem::GetNatResultText(Subsystem->GetLastResult()).ToString());
			}
		}));
}
#endif   // !UE_BUILD_SHIPPING
