// MOU 서버 - 공유기에 포트를 여는 전용 워커 스레드.
//
// [이 클래스가 존재하는 이유]
//   FNatPortMapping 의 함수는 전부 블로킹이다. SSDP 응답을 기다리고 HTTP 왕복을
//   여러 번 하므로 게임 스레드에서 부르면 수 초간 화면이 멈춘다.
//   FServerClientRunnable 이 소켓을 워커로 뺀 것과 같은 이유다.
//
// [스레드 경계]
//
//     게임 스레드                            워커 스레드 (이 클래스)
//   ┌──────────────────────────┐        ┌──────────────────────────────┐
//   │ UNatPortMappingSubsystem │        │  FNatMappingRunnable         │
//   │                          │        │                              │
//   │ Tick()                   │◀─Queue─│  Discover → ExternalIp → Add │
//   │  -> 델리게이트            │        │  그 뒤 유지, 종료 시 Delete   │
//   └──────────────────────────┘        └──────────────────────────────┘
//
//   >> 이 클래스 안에서는 UObject / UMG 를 절대 건드리지 않는다. <<
//   결과는 순수 데이터(FNatMappingOutcome)로만 큐에 넣고, 게임 스레드가 꺼내 쏜다.
//
// [수명이 곧 매핑의 수명이다 — 이 설계의 핵심]
//   워커는 매핑에 성공한 뒤 바로 끝나지 않고, 종료 요청이 올 때까지 살아 있다가
//   나가면서 DeletePortMapping 을 부른다. 이렇게 묶어두지 않으면 "매핑은 했는데
//   지우는 사람이 없는" 상태가 생기고, 공유기에는 매핑이 영구히 남는다.
//   (Lease 를 지원하지 않는 공유기가 흔해서 영구 매핑으로 떨어지는 일이 잦다)
//
// [수정 시 같이 봐야 하는 곳]
//   · NatPortMapping.h/.cpp   실제 UPnP 프로토콜
//   · NatPortMappingSubsystem.h/.cpp  이 워커를 소유하는 게임 스레드 쪽

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "HAL/Runnable.h"
#include "Server/Net/NatPortMapping.h"

#include <atomic>

class FEvent;

/** 워커가 게임 스레드에 알리는 결과. 순수 데이터만 담는다. */
struct FNatMappingOutcome
{
	ENatMapResult Result = ENatMapResult::Unknown;

	/** 성공했을 때 공유기가 실제로 열어준 외부 포트. CreateRoom 에 이 값을 신고한다. */
	uint16 ExternalPort = 0;

	/** 리슨서버가 실제로 듣고 있는 포트. 매핑 성공 여부와 무관하게 그대로다. */
	uint16 InternalPort = 0;

	/** 공유기의 외부 IP. 실패했어도 알아낸 데까지는 채워서 보낸다(진단용). */
	FString ExternalIp;

	bool IsSuccess() const { return Result == ENatMapResult::Success; }
};

/**
 * 공유기에 포트를 열고, 살아있는 동안 유지하고, 나갈 때 지우는 워커.
 *
 * 소유자는 UNatPortMappingSubsystem 하나뿐이다. 직접 생성하지 않는다.
 */
class TEAMPROJECT_MOU_API FNatMappingRunnable : public FRunnable
{
public:
	FNatMappingRunnable(uint16 InInternalPort,
	                    uint16 InDesiredExternalPort,
	                    bool bInUdp,
	                    const FString& InDescription,
	                    uint32 InLeaseSeconds);

	virtual ~FNatMappingRunnable();

	// --- FRunnable ---------------------------------------------------------

	virtual bool   Init() override;
	virtual uint32 Run() override;
	virtual void   Stop() override;

	// --- 게임 스레드에서 호출 ------------------------------------------------

	/** 결과를 꺼낸다. 게임 스레드 전용. 더 없으면 false. */
	bool DequeueOutcome(FNatMappingOutcome& Out) { return Outcomes.Dequeue(Out); }

private:
	/**
	 * 종료 요청에 즉시 반응하며 잠든다. 참을 반환하면 "종료하라" 는 뜻이다.
	 * 그냥 Sleep 을 쓰면 Kill(true) 가 그 시간만큼 게임 스레드를 붙잡는다.
	 */
	bool WaitForStop(int32 Milliseconds);

	void PushOutcome(ENatMapResult Result, uint16 ExternalPort);

	/**
	 * 지난 실행이 남긴 우리 매핑을 지운다. AddMapping 직전에 한 번. (2026-08-28)
	 *
	 * [왜 필요한가]
	 *   크래시나 강제 종료로 죽으면 Run() 끝의 DeleteMapping 이 안 불린다.
	 *   그러면 공유기에 매핑이 남고, 다음 실행은 718(PortConflict)을 받아
	 *   7778, 7779... 로 밀려간다. 밀린 포트로 방이 광고되면 아무도 못 들어온다.
	 *   공용 네트워크에서는 관리페이지로 지울 수도 없다.
	 *
	 * ★ 두 조건을 **모두** 만족할 때만 지운다.
	 *     1) InternalClient 가 내 LAN IP 다   — 남의 기기 매핑을 건드리지 않는다
	 *     2) Description 이 우리 것과 같다     — 사용자가 손으로 넣은 포워딩을 지키다
	 *   공용 네트워크에는 남의 매핑이 같이 들어 있다. 포트 번호나 설명 하나만 보고
	 *   지우면 남의 서비스를 끊는다.
	 *
	 * @return 지운 개수. 실패해도 매핑 자체는 진행하므로 오류로 올리지 않는다.
	 */
	int32 CleanupStaleMappings();

	/**
	 * 이미 우리 PC 로 오게 열려 있는 매핑이 있으면 그것을 그대로 쓴다. (2026-08-28)
	 *
	 * [왜 필요한가 — 실제로 사고가 나는 경로]
	 *   서버 PC 처럼 공유기에 손으로 "외부 UDP 7777 -> 내 PC:7777" 을 넣어둔 사람이
	 *   방장을 하면, 지금까지는 AddPortMapping 이 **그 항목을 덮어썼다.** 규격상
	 *   매핑의 식별자가 (외부포트, 프로토콜) 이라 같은 자리에 다시 쓰는 것이 곧
	 *   덮어쓰기이기 때문이다. 그리고 게임을 끄면 6단계가 그것을 **지운다.**
	 *   결과는 "분명히 포트포워딩을 해뒀는데 어느 순간 사라졌다" 다.
	 *
	 *   이미 길이 나 있으면 새로 뚫을 이유가 없다. 그대로 쓰고, 우리가 만든 것이
	 *   아니므로 나갈 때도 건드리지 않는다.
	 *
	 * ★ 3단계(CleanupStaleMappings) 뒤에 불러야 한다.
	 *   그래야 여기 걸리는 것이 "우리 것이 아닌" 항목뿐임이 보장된다.
	 *   순서가 뒤집히면 우리가 지난 판에 남긴 찌꺼기를 수동 포워딩으로 착각해
	 *   영원히 안 지우게 된다.
	 */
	bool TryReuseExistingMapping();

	// --- 생성 후 불변 -------------------------------------------------------
	uint16  InternalPort        = 0;
	uint16  DesiredExternalPort = 0;
	bool    bUdp                = true;
	FString Description;
	uint32  LeaseSeconds        = 0;

	// --- 워커 스레드 전용 (게임 스레드에서 접근 금지) ------------------------
	FNatPortMapping   Mapping;
	FNatMappingHandle Handle;
	FString           ExternalIp;

	/**
	 * 이 매핑을 우리가 만들었는가.
	 *
	 * 거짓이면 TryReuseExistingMapping 이 남의 것(사용자의 수동 포워딩)을 빌려 쓰는
	 * 중이라는 뜻이다. 그때는 나가면서 **지우지 않는다** — 우리가 만들지 않은 것을
	 * 지우면 사용자의 공유기 설정이 사라진다.
	 */
	bool bOwnsMapping = true;

	// --- 스레드 간 공유 ------------------------------------------------------
	std::atomic<bool> bStopRequested{ false };

	/** Stop() 이 워커를 즉시 깨우기 위한 것. 없으면 종료가 대기 시간만큼 늦어진다. */
	FEvent* StopEvent = nullptr;

	TQueue<FNatMappingOutcome, EQueueMode::Spsc> Outcomes;

	// --- 튜닝 값 -------------------------------------------------------------

	/**
	 * 외부 포트가 충돌(UPnP 718)했을 때 번호를 올려가며 다시 시도하는 횟수.
	 * 다른 기기가 이미 그 포트를 쓰고 있을 뿐이므로 옆 번호는 대개 비어 있다.
	 */
	static constexpr int32 MaxPortAttempts = 8;

	/** 유지 루프가 깨어나는 주기. 종료 반응 속도와 무관하다(StopEvent 가 깨운다). */
	static constexpr int32 KeepAlivePollMilliseconds = 1000;
};
