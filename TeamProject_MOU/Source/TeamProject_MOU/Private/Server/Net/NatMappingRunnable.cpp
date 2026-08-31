#include "Server/Net/NatMappingRunnable.h"

#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Server/Chat/ChatTypes.h"   // LogMOUServer

FNatMappingRunnable::FNatMappingRunnable(uint16 InInternalPort,
                                         uint16 InDesiredExternalPort,
                                         bool bInUdp,
                                         const FString& InDescription,
                                         uint32 InLeaseSeconds)
	: InternalPort(InInternalPort)
	, DesiredExternalPort(InDesiredExternalPort != 0 ? InDesiredExternalPort : InInternalPort)
	, bUdp(bInUdp)
	, Description(InDescription)
	, LeaseSeconds(InLeaseSeconds)
{
	StopEvent = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/false);
}

FNatMappingRunnable::~FNatMappingRunnable()
{
	if (StopEvent != nullptr)
	{
		FPlatformProcess::ReturnSynchEventToPool(StopEvent);
		StopEvent = nullptr;
	}
}

bool FNatMappingRunnable::Init()
{
	// ★ Init() 은 새 스레드에서 돌지만, 이것이 끝날 때까지 FRunnableThread::Create() 를
	//   부른 게임 스레드가 대기한다. 여기서 SSDP 를 돌리면 게임 스레드가 같이 멈춘다.
	//   실제 작업은 전부 Run() 에서 한다. (FServerClientRunnable 과 같은 규칙)
	return true;
}

void FNatMappingRunnable::Stop()
{
	bStopRequested.store(true);

	// 유지 루프에서 자고 있을 수 있으므로 깨워준다.
	if (StopEvent != nullptr)
	{
		StopEvent->Trigger();
	}
}

bool FNatMappingRunnable::WaitForStop(int32 Milliseconds)
{
	if (bStopRequested.load())
	{
		return true;
	}

	if (StopEvent != nullptr)
	{
		StopEvent->Wait(FTimespan::FromMilliseconds(Milliseconds));
	}
	else
	{
		FPlatformProcess::Sleep(Milliseconds / 1000.0f);
	}

	return bStopRequested.load();
}

void FNatMappingRunnable::PushOutcome(ENatMapResult Result, uint16 ExternalPort)
{
	FNatMappingOutcome Outcome;
	Outcome.Result       = Result;
	Outcome.ExternalPort = ExternalPort;
	Outcome.InternalPort = InternalPort;
	Outcome.ExternalIp   = ExternalIp;

	Outcomes.Enqueue(MoveTemp(Outcome));
}

uint32 FNatMappingRunnable::Run()
{
	// ── 1. 공유기 찾기 ─────────────────────────────────────────────────
	{
		const ENatMapResult Result = Mapping.DiscoverGateway();
		if (Result != ENatMapResult::Success)
		{
			// 실패가 곧 오류는 아니다. UPnP 를 지원하지 않는 공유기라는 뜻일 뿐이고,
			// 호출자는 LAN 전용 동작으로 그대로 진행하면 된다.
			PushOutcome(Result, 0);
			return 0;
		}
	}

	if (bStopRequested.load())
	{
		return 0;
	}

	// ── 2. 외부 IP 확인 (CGNAT 판별) ───────────────────────────────────
	{
		const ENatMapResult Result = Mapping.GetExternalIP(ExternalIp);
		if (Result == ENatMapResult::CarrierGradeNat)
		{
			// ★ 통신사 NAT 안이다. 포트를 열어봐야 그 위에서 막히므로 시도하지 않는다.
			//   이 경로가 곧 "EOS/Steam 릴레이로 가야 하는" 분기점이다.
			PushOutcome(Result, 0);
			return 0;
		}
		if (Result != ENatMapResult::Success)
		{
			// 외부 IP 를 못 읽는 공유기도 있다. 매핑 자체는 될 수 있으니 계속 간다.
			UE_LOG(LogMOUServer, Warning, TEXT("[NAT] 외부 IP 를 확인하지 못했다. 매핑은 계속 시도한다."));
		}
	}

	if (bStopRequested.load())
	{
		return 0;
	}

	// ── 3. 지난 판의 찌꺼기 청소 ───────────────────────────────────────
	// 크래시로 죽으면 6단계가 안 돌아 매핑이 남는다. 그것을 그대로 두면 아래에서
	// 718 을 받아 포트가 밀려간다. 여기서 먼저 치우면 매번 7777 로 돌아온다.
	CleanupStaleMappings();

	if (bStopRequested.load())
	{
		return 0;
	}

	// ── 4. 매핑 확보 ───────────────────────────────────────────────────
	//
	// 먼저 "이미 우리 PC 로 오게 열려 있는가" 를 본다. 걸리는 것은 사용자가 공유기
	// 관리페이지에서 손으로 넣은 포워딩이다 — 우리가 만든 것이었다면 3단계가 이미
	// 지웠기 때문이다. 그런 것이 있으면 덮어쓰지 않고 그대로 쓴다.
	// (덮어쓰면 6단계에서 지워버려 사용자의 수동 설정이 사라진다)
	if (!TryReuseExistingMapping())
	{
		// 충돌(718)이면 외부 포트를 하나씩 올려가며 다시 시도한다.
		// 리슨서버는 InternalPort 그대로 두고, 성공한 "외부" 포트만 방 정보에 신고하면
		// 프로토콜도 Server.exe 도 바뀌지 않는다.
		ENatMapResult AddResult = ENatMapResult::Unknown;
		uint16        Attempted = DesiredExternalPort;

		for (int32 Attempt = 0; Attempt < MaxPortAttempts; ++Attempt)
		{
			if (bStopRequested.load())
			{
				return 0;
			}

			Attempted = static_cast<uint16>(DesiredExternalPort + Attempt);
			AddResult = Mapping.AddMapping(InternalPort, Attempted, bUdp, Description, LeaseSeconds, Handle);

			if (AddResult != ENatMapResult::PortConflict)
			{
				break;   // 성공했거나, 포트를 바꿔도 소용없는 실패다
			}

			UE_LOG(LogMOUServer, Log, TEXT("[NAT] 외부 포트 %u 가 사용 중이다. 다음 번호로 시도한다."),
			       static_cast<uint32>(Attempted));
		}

		if (AddResult != ENatMapResult::Success)
		{
			PushOutcome(AddResult, 0);
			return 0;
		}
	}

	PushOutcome(ENatMapResult::Success, Handle.ExternalPort);

	// ── 5. 유지 ────────────────────────────────────────────────────────
	// 여기서 바로 끝내면 안 된다. 이 스레드의 수명이 곧 매핑의 수명이고,
	// 나가면서 지우는 것이 유일하게 보장된 정리 경로다.
	//
	// Lease 를 받은 경우에는 만료 전에 갱신한다. 절반 지점에서 갱신하는 것은
	// 한 번 실패해도 다음 기회가 남도록 하기 위해서다.
	//
	// ★ 빌려 쓰는 매핑(bOwnsMapping=false)은 갱신하지 않는다.
	//   갱신은 곧 AddPortMapping 재호출이라 사용자의 수동 항목을 우리 설명으로
	//   덮어쓰게 된다. 남의 것은 읽기만 하고 그대로 둔다 — 어차피 수동 항목은
	//   대개 영구(lease=0)라 만료되지도 않는다.
	const bool bShouldRefresh = bOwnsMapping && LeaseSeconds > 0;

	double NextRefreshTime = bShouldRefresh
		? FPlatformTime::Seconds() + (LeaseSeconds / 2.0)
		: TNumericLimits<double>::Max();

	while (!WaitForStop(KeepAlivePollMilliseconds))
	{
		if (bShouldRefresh && FPlatformTime::Seconds() >= NextRefreshTime)
		{
			const ENatMapResult RefreshResult = Mapping.RefreshMaping(Handle, LeaseSeconds);
			if (RefreshResult != ENatMapResult::Success)
			{
				UE_LOG(LogMOUServer, Warning, TEXT("[NAT] 매핑 갱신에 실패했다. 만료되면 외부 접속이 끊긴다."));
			}
			NextRefreshTime = FPlatformTime::Seconds() + (LeaseSeconds / 2.0);
		}
	}

	// ── 6. 해제 ────────────────────────────────────────────────────────
	// 핸들이 제어 URL 을 들고 있어서 SSDP 재탐색 없이 바로 지울 수 있다.
	// 이 호출이 몇 백 ms 걸릴 수 있는데, Kill(true) 가 그만큼 기다리는 것은 정상이다 —
	// 여기서 안 지우면 공유기에 남는다.
	//
	// ★ 우리가 만든 것만 지운다. 빌려 쓴 수동 포워딩을 지우면 사용자가 공유기
	//   관리페이지에 다시 들어가 넣어야 하고, 공용 네트워크에서는 그것이 불가능하다.
	if (!bOwnsMapping)
	{
		UE_LOG(LogMOUServer, Log,
		       TEXT("[NAT] 외부 %u 는 원래 열려 있던 포워딩이라 그대로 둔다."),
		       static_cast<uint32>(Handle.ExternalPort));
	}
	else if (Handle.IsValid())
	{
		const ENatMapResult DeleteResult = Mapping.DeleteMapping(Handle);
		if (DeleteResult != ENatMapResult::Success)
		{
			UE_LOG(LogMOUServer, Warning,
			       TEXT("[NAT] 매핑 해제 실패. 공유기에 외부 포트 %u 가 남았을 수 있다."),
			       static_cast<uint32>(Handle.ExternalPort));
		}
	}

	return 0;
}

// ─────────────────────────────────────────────────────────────────────
// 지난 판의 찌꺼기 청소 (2026-08-28)
// ─────────────────────────────────────────────────────────────────────

int32 FNatMappingRunnable::CleanupStaleMappings()
{
	// 내 LAN IP 를 모르면 소유자 판정을 할 수 없다. 판정 없이 지우는 것은
	// 남의 매핑을 지우는 것과 같으므로, 모르면 아무것도 하지 않는다.
	const FString LocalIp = Mapping.GetLocalLanIp();
	if (LocalIp.IsEmpty())
	{
		UE_LOG(LogMOUServer, Verbose,
		       TEXT("[NAT] 내 LAN IP 를 몰라 찌꺼기 청소를 건너뛴다."));
		return 0;
	}

	// ★ 테이블 전체를 훑지 않는다. 우리가 밀려갈 수 있는 범위만 본다.
	//
	//   전체 훑기(ListMappings)는 항목 하나당 SOAP 왕복 한 번이라, 매핑이 많은
	//   공유기에서는 방 만들기가 몇 초씩 늦어진다. 그런데 지울 대상은 **우리가
	//   만들 수 있었던 것뿐**이고, 우리 코드가 쓰는 외부 포트는 아래 AddMapping 의
	//   재시도 규칙상 [DesiredExternalPort, +MaxPortAttempts) 안에만 있다.
	//   그 범위만 GetSpecificPortMappingEntry 로 콕 집어 물어보면 왕복이 8번으로 고정된다.
	//
	//   (MOU.Nat.List 는 여전히 전체를 훑는다 — 거기서는 전체 그림이 목적이다)
	int32 Removed = 0;

	for (int32 Attempt = 0; Attempt < MaxPortAttempts; ++Attempt)
	{
		if (bStopRequested.load())
		{
			break;
		}

		const uint16 Candidate = static_cast<uint16>(DesiredExternalPort + Attempt);

		FNatMappingEntry Entry;
		bool bFound = false;
		if (Mapping.GetMappingFor(Candidate, bUdp, Entry, bFound) != ENatMapResult::Success || !bFound)
		{
			continue;   // 비어 있거나 못 읽었다. 둘 다 지울 것이 없다는 뜻이다
		}

		// ★ 두 조건을 모두 만족해야 지운다. 하나라도 빠지면 남의 매핑이나
		//   사용자가 손으로 넣은 포워딩을 지우게 된다.
		const bool bMine = Entry.InternalClient.Equals(LocalIp, ESearchCase::IgnoreCase);
		const bool bOurs = Entry.Description.Equals(Description, ESearchCase::CaseSensitive);
		if (!bMine || !bOurs)
		{
			continue;
		}

		if (Mapping.DeleteMappingByPort(Candidate, bUdp) == ENatMapResult::Success)
		{
			++Removed;
		}
	}

	if (Removed > 0)
	{
		UE_LOG(LogMOUServer, Log,
		       TEXT("[NAT] 지난 실행이 남긴 매핑 %d개를 지웠다. (내 PC %s 를 가리키던 \"%s\" 항목)"),
		       Removed, *LocalIp, *Description);
	}

	return Removed;
}

bool FNatMappingRunnable::TryReuseExistingMapping()
{
	const FString LocalIp = Mapping.GetLocalLanIp();
	if (LocalIp.IsEmpty())
	{
		return false;   // 소유자 판정을 못 하면 빌려 쓸지도 판단할 수 없다
	}

	FNatMappingEntry Entry;
	bool bFound = false;
	if (Mapping.GetMappingFor(DesiredExternalPort, bUdp, Entry, bFound) != ENatMapResult::Success
		|| !bFound)
	{
		return false;   // 비어 있다. 새로 만들면 된다
	}

	// 남의 기기로 가는 매핑이다. 빌릴 수 없으니 아래 718 재시도가 옆 포트를 찾게 둔다.
	if (!Entry.InternalClient.Equals(LocalIp, ESearchCase::IgnoreCase))
	{
		return false;
	}

	// 우리 PC 로 오긴 하는데 리슨서버가 듣는 포트가 아니다.
	// 이걸 빌리면 패킷이 엉뚱한 포트로 들어와 접속이 안 된다.
	if (Entry.InternalPort != InternalPort)
	{
		UE_LOG(LogMOUServer, Warning,
		       TEXT("[NAT] 외부 %u 가 내 PC 의 %u 로 가게 열려 있다(리슨서버는 %u). 빌려 쓸 수 없다."),
		       static_cast<uint32>(DesiredExternalPort),
		       static_cast<uint32>(Entry.InternalPort),
		       static_cast<uint32>(InternalPort));
		return false;
	}

	if (!Entry.bEnabled)
	{
		return false;   // 꺼져 있는 항목이다. 있으나 마나
	}

	// 여기까지 왔으면 "밖에서 우리 리슨서버로 오는 길" 이 이미 나 있다.
	// 우리가 만든 것은 3단계에서 지워졌으므로, 이것은 사용자가 손으로 넣은 포워딩이다.
	Handle = FNatMappingHandle();
	Handle.ExternalPort = DesiredExternalPort;
	Handle.InternalPort = InternalPort;
	Handle.bUdp         = bUdp;
	Handle.Description  = Entry.Description;
	bOwnsMapping        = false;   // ★ 나갈 때 지우지 않는다

	UE_LOG(LogMOUServer, Log,
	       TEXT("[NAT] 외부 %u -> %s:%u 가 이미 열려 있다(\"%s\"). 새로 만들지 않고 그대로 쓴다."),
	       static_cast<uint32>(DesiredExternalPort), *LocalIp,
	       static_cast<uint32>(InternalPort), *Entry.Description);

	return true;
}
