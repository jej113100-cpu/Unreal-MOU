#include "Server/Net/NatPortMappingSubsystem.h"

#include "Async/Async.h"                    // MOU.Nat.List / Clean 을 게임 스레드 밖에서
#include "HAL/RunnableThread.h"
#include "Server/Chat/ChatTypes.h"          // LogMOUServer
#include "Server/Net/NatMappingRunnable.h"
#include "Server/Net/NatPortMapping.h"      // FNatMappingEntry (콘솔 명령이 직접 쓴다)
#include "Server/ServerSettings.h"

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

// UHT 가 UFUNCTION 기본값으로 리터럴만 받아서 BeginPortMapping 선언에 3600 이 박혀 있다.
// 상수와 어긋나면 "설정을 고쳤는데 기본값은 그대로" 가 되므로 여기서 묶어둔다.
static_assert(UNatPortMappingSubsystem::DefaultLeaseSeconds == 3600,
	"BeginPortMapping 의 LeaseSeconds 기본값(헤더의 리터럴 3600)도 같이 고칠 것");

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

void UNatPortMappingSubsystem::BeginPortMapping(int32 InternalPort, int32 DesiredExternalPort, int32 LeaseSeconds, bool bForce)
{
	if (InternalPort <= 0 || InternalPort > 65535)
	{
		UE_LOG(LogMOUServer, Warning, TEXT("[NAT] 잘못된 포트 번호: %d"), InternalPort);
		return;
	}

	// ★ 팀이 수동 포트포워딩으로 운영하기로 했으면 시도조차 하지 않는다. (2026-08-28)
	//
	//   시도해봐야 SSDP 탐색으로 최대 3초를 버리고 실패할 뿐이고, 그동안 방 만들기
	//   창에는 "공유기에 포트를 여는 중입니다..." 가 떠 있다. 실제로는 수동
	//   포워딩으로 잘 되는 상황인데 뭔가 잘못된 것처럼 보이게 만든다.
	//
	//   콘솔 명령 MOU.Nat.Open 은 bForce 로 이 판정을 건너뛴다 —
	//   "설정과 무관하게 지금 이 공유기가 UPnP 를 지원하는지 보고 싶다" 가
	//   그 명령의 존재 이유이기 때문이다.
	if (!bForce && !UMOUServerSettings::ShouldUseUpnp())
	{
		UE_LOG(LogMOUServer, Log,
			TEXT("[NAT] UPnP 가 꺼져 있어 포트 열기를 건너뛴다(수동 포트포워딩 운영). ")
			TEXT("방장이 되려면 이 PC 의 공유기에 외부 UDP %d -> 이 PC 의 LAN IP:%d 가 열려 있어야 한다."),
			InternalPort, InternalPort);
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
		MOUNat::MappingDescription,   // 청소가 알아보는 표식. 바꾸면 지난 판의 매핑을 못 지운다
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
		TEXT("공유기에 포트를 열어달라고 요청한다. 설정(bUseUpnpPortMapping)이 꺼져 있어도 강제로 시도한다. 사용법: MOU.Nat.Open [내부포트=7777]"),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			if (UNatPortMappingSubsystem* Subsystem = FindNatSubsystem())
			{
				const int32 Port = (Args.Num() > 0) ? FCString::Atoi(*Args[0]) : 7777;
				// 설정이 꺼져 있어도 진행한다. "이 공유기가 UPnP 를 지원하는가" 를
				// 지금 확인하는 것이 이 명령의 존재 이유다.
				Subsystem->BeginPortMapping(Port, 0,
					UNatPortMappingSubsystem::DefaultLeaseSeconds, /*bForce=*/true);
			}
		}));

	// ─────────────────────────────────────────────────────────────────
	// 공유기에 남은 매핑 보기 / 지우기 (2026-08-28)
	//
	// [왜 필요한가]
	//   공용 네트워크에서는 공유기 관리페이지에 못 들어간다. 그런데 크래시로 죽으면
	//   매핑이 남고, 다음 실행은 718 을 받아 포트가 밀려간다. 무엇이 남았는지 볼
	//   방법이 없으면 왜 밀리는지도 알 수 없다.
	//
	//   UPnP 규격에 조회 액션이 있어서 관리자 로그인 없이 읽을 수 있다.
	//   관리페이지가 유일한 확인 수단이 아니다.
	//
	// ★ 게임 스레드에서 돌리면 안 된다.
	//   SSDP(최대 3초) + SOAP 왕복이 전부 블로킹이다. 여기서 돌리면 그만큼 화면이
	//   멈춘다. 워커를 새로 만드는 대신 Async 로 짧게 떼어낸다 —
	//   매핑을 만들지 않으므로 FNatMappingRunnable 처럼 오래 살 이유가 없다.
	// ─────────────────────────────────────────────────────────────────

	/**
	 * List / Clean 이 공유하는 본체.
	 *
	 * @param bDelete       참이면 지운다. 거짓이면 출력만 한다(MOU.Nat.List).
	 * @param bIncludeOther 참이면 설명이 "MOU" 가 아닌 것까지 지운다(MOU.Nat.Clean all).
	 *                      ★ 기본값 거짓을 유지할 것 — 이유는 CleanCommand 주석 참고.
	 */
	void RunMappingSweep(bool bDelete, bool bIncludeOther)
	{
		Async(EAsyncExecution::Thread, [bDelete, bIncludeOther]()
		{
			FNatPortMapping Mapping;

			if (Mapping.DiscoverGateway() != ENatMapResult::Success)
			{
				UE_LOG(LogMOUServer, Warning,
					TEXT("[NAT] 공유기를 찾지 못했다. UPnP 가 꺼져 있거나 지원하지 않는 공유기다."));
				return;
			}

			const FString LocalIp = Mapping.GetLocalLanIp();

			TArray<FNatMappingEntry> Entries;
			if (Mapping.ListMappings(Entries) != ENatMapResult::Success)
			{
				UE_LOG(LogMOUServer, Warning,
					TEXT("[NAT] 매핑 목록을 읽지 못했다. 조회를 지원하지 않는 공유기일 수 있다."));
				return;
			}

			UE_LOG(LogMOUServer, Log, TEXT("[NAT] 공유기 매핑 %d개 (내 LAN IP = %s)"),
				Entries.Num(), LocalIp.IsEmpty() ? TEXT("(모름)") : *LocalIp);

			int32 MineCount = 0;

			for (int32 Index = 0; Index < Entries.Num(); ++Index)
			{
				const FNatMappingEntry& Entry = Entries[Index];
				const bool bMine = !LocalIp.IsEmpty()
					&& Entry.InternalClient.Equals(LocalIp, ESearchCase::IgnoreCase);

				UE_LOG(LogMOUServer, Log,
					TEXT("[NAT]   [%d] %s %u -> %s:%u  \"%s\"  lease=%u%s"),
					Index,
					Entry.bUdp ? TEXT("UDP") : TEXT("TCP"),
					static_cast<uint32>(Entry.ExternalPort),
					*Entry.InternalClient,
					static_cast<uint32>(Entry.InternalPort),
					*Entry.Description,
					Entry.LeaseSeconds,
					bMine ? TEXT("   <= 내 PC") : TEXT(""));

				if (bMine)
				{
					++MineCount;
				}
			}

			if (!bDelete)
			{
				UE_LOG(LogMOUServer, Log,
					TEXT("[NAT] 내 PC 를 가리키는 항목 %d개. 지우려면 MOU.Nat.Clean."), MineCount);
				return;
			}

			// ★ 삭제 조건은 두 개다. 둘 다 만족해야 지운다.
			//     1) InternalClient 가 내 LAN IP     — 남의 기기 매핑을 안 건드린다
			//     2) Description 이 "MOU"            — 손으로 넣은 포워딩을 안 지운다
			//
			//   2번이 없으면 사고가 난다. 서버 PC 에는 관리페이지에서 손으로 넣은
			//   "외부 TCP 9000 -> 서버 PC" 포워딩이 있고, 그 규칙을 UPnP 목록에
			//   같이 노출하는 공유기가 있다. 조건 1번만으로 지우면 팀 전원의 로그인이
			//   끊긴다. 되돌리려면 관리페이지에 다시 들어가야 하는데, 애초에 거기
			//   못 들어가는 상황을 풀려고 만든 명령이다.
			if (LocalIp.IsEmpty())
			{
				UE_LOG(LogMOUServer, Warning,
					TEXT("[NAT] 내 LAN IP 를 몰라 무엇이 내 것인지 판정할 수 없다. 아무것도 지우지 않는다."));
				return;
			}

			int32 Removed = 0;
			int32 Skipped = 0;

			for (const FNatMappingEntry& Entry : Entries)
			{
				if (!Entry.InternalClient.Equals(LocalIp, ESearchCase::IgnoreCase))
				{
					continue;   // 남의 것. 절대 건드리지 않는다
				}

				const bool bOurs = Entry.Description.Equals(
					MOUNat::MappingDescription, ESearchCase::CaseSensitive);
				if (!bOurs && !bIncludeOther)
				{
					++Skipped;
					UE_LOG(LogMOUServer, Log,
						TEXT("[NAT]   건너뜀: %s %u \"%s\" — 우리가 만든 것이 아니다(수동 포워딩일 수 있다)"),
						Entry.bUdp ? TEXT("UDP") : TEXT("TCP"),
						static_cast<uint32>(Entry.ExternalPort), *Entry.Description);
					continue;
				}

				if (Mapping.DeleteMappingByPort(Entry.ExternalPort, Entry.bUdp) == ENatMapResult::Success)
				{
					++Removed;
				}
			}

			UE_LOG(LogMOUServer, Log, TEXT("[NAT] %d개를 지웠다. (내 PC %s 를 가리키던 \"%s\" 항목)"),
				Removed, *LocalIp, MOUNat::MappingDescription);

			UE_CLOG(Skipped > 0, LogMOUServer, Log,
				TEXT("[NAT] %d개는 우리 것이 아니라 남겨뒀다. 그것까지 지우려면 MOU.Nat.Clean all ")
				TEXT("— 수동 포워딩이 있다면 같이 지워지니 주의할 것."), Skipped);
		});
	}

	FAutoConsoleCommand ListCommand(
		TEXT("MOU.Nat.List"),
		TEXT("공유기에 등록된 UPnP 매핑을 전부 출력한다. 읽기만 하므로 안전하다."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			RunMappingSweep(/*bDelete=*/false, /*bIncludeOther=*/false);
		}));

	FAutoConsoleCommand CleanCommand(
		TEXT("MOU.Nat.Clean"),
		TEXT("공유기 매핑 중 '이 PC 를 가리키는 MOU 항목' 만 지운다. ")
		TEXT("남의 기기 매핑과 수동 포워딩은 건드리지 않는다. ")
		TEXT("사용법: MOU.Nat.Clean [all]  (all 이면 이 PC 를 가리키는 것을 전부 — 수동 포워딩 포함)"),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			const bool bIncludeOther = Args.Num() > 0 && Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase);

			// 살아 있는 워커가 들고 있는 매핑도 같이 지우게 되므로, 먼저 정상 경로로
			// 닫는다. 안 그러면 워커는 "내 매핑이 있다" 고 믿는데 공유기에는 없는
			// 상태가 되고, 종료할 때 714 를 받는다.
			if (UNatPortMappingSubsystem* Subsystem = FindNatSubsystem())
			{
				Subsystem->ReleasePortMapping();
			}

			UE_CLOG(bIncludeOther, LogMOUServer, Warning,
				TEXT("[NAT] all 모드다. 이 PC 를 가리키는 매핑을 설명과 무관하게 전부 지운다 — ")
				TEXT("관리페이지에서 손으로 넣은 포워딩이 있으면 같이 사라진다."));

			RunMappingSweep(/*bDelete=*/true, bIncludeOther);
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
