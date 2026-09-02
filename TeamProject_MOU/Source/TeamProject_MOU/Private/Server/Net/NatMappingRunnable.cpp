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

	// ── 3. 매핑 추가 ───────────────────────────────────────────────────
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

	PushOutcome(ENatMapResult::Success, Handle.ExternalPort);

	// ── 4. 유지 ────────────────────────────────────────────────────────
	// 여기서 바로 끝내면 안 된다. 이 스레드의 수명이 곧 매핑의 수명이고,
	// 나가면서 지우는 것이 유일하게 보장된 정리 경로다.
	//
	// Lease 를 받은 경우에는 만료 전에 갱신한다. 절반 지점에서 갱신하는 것은
	// 한 번 실패해도 다음 기회가 남도록 하기 위해서다.
	double NextRefreshTime = (LeaseSeconds > 0)
		? FPlatformTime::Seconds() + (LeaseSeconds / 2.0)
		: TNumericLimits<double>::Max();

	while (!WaitForStop(KeepAlivePollMilliseconds))
	{
		if (LeaseSeconds > 0 && FPlatformTime::Seconds() >= NextRefreshTime)
		{
			const ENatMapResult RefreshResult = Mapping.RefreshMaping(Handle, LeaseSeconds);
			if (RefreshResult != ENatMapResult::Success)
			{
				UE_LOG(LogMOUServer, Warning, TEXT("[NAT] 매핑 갱신에 실패했다. 만료되면 외부 접속이 끊긴다."));
			}
			NextRefreshTime = FPlatformTime::Seconds() + (LeaseSeconds / 2.0);
		}
	}

	// ── 5. 해제 ────────────────────────────────────────────────────────
	// 핸들이 제어 URL 을 들고 있어서 SSDP 재탐색 없이 바로 지울 수 있다.
	// 이 호출이 몇 백 ms 걸릴 수 있는데, Kill(true) 가 그만큼 기다리는 것은 정상이다 —
	// 여기서 안 지우면 공유기에 남는다.
	if (Handle.IsValid())
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
