// MOU 서버 - 공유기 포트 열기의 게임 스레드 진입점.
//
// [무엇을 하는가]
//   방장이 리슨서버를 열기 전에 공유기(UPnP)에 포트를 열어달라고 요청한다.
//   성공하면 공유기가 실제로 열어준 "외부" 포트를 알려주고, 그 값을 방 정보에
//   신고하면 다른 집에서도 접속할 수 있게 된다.
//
// [왜 UServerSubsystem 과 따로 두는가]
//   이 기능은 Server.exe 와 아무 관계가 없다. 대화 상대가 공유기이고,
//   EOS 로 백엔드를 갈아끼워도 이 코드는 그대로 남아야 한다(EOS 를 쓰든 말든
//   P2P 트래픽은 공유기를 지나간다). ILobbyBackend 뒤에 숨기지 않은 이유가 이것이다.
//
// [구조]
//
//   ULobbyWidgetBase / URoomCreateWidgetBase
//        │  BeginPortMapping(7777)
//        ▼
//   UNatPortMappingSubsystem (이 파일)   게임 스레드. 상태 보관 / 델리게이트
//        │  소유
//        ▼
//   FNatMappingRunnable                  워커 스레드. 블로킹 UPnP 작업
//        │  소유
//        ▼
//   FNatPortMapping                      SSDP / SOAP 프로토콜
//
// [수정 시 같이 봐야 하는 곳]
//   · NatMappingRunnable.h  워커. 매핑의 수명이 그 스레드의 수명이다
//   · UServerSubsystem::CreateRoom  성공한 외부 포트를 HostPort 로 넘기는 곳

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "NatPortMappingSubsystem.generated.h"

class FNatMappingRunnable;
class FRunnableThread;

/**
 * 블루프린트용 결과 코드.
 *
 * 순수 C++ 쪽 ENatMapResult 와 값이 1:1 로 같아야 한다.
 * (프로젝트 관례 — ChatFraming.h 가 서버 enum 과 BP enum 을 static_assert 로
 *  묶어두는 것과 같은 방식이다. 검사는 이 파일의 .cpp 상단에 있다)
 */
UENUM(BlueprintType)
enum class EMOUNatResultBP : uint8
{
	/** 포트가 열렸다. 외부에서 접속할 수 있다 */
	Success          UMETA(DisplayName = "성공"),
	/** 공유기가 UPnP 에 답하지 않는다. 미지원이거나 설정에서 꺼져 있다 */
	NoGatewayFound   UMETA(DisplayName = "공유기 없음"),
	/** 통신사 NAT 안이다. 포트포워딩으로는 못 뚫는다 — 릴레이가 필요하다 */
	CarrierGradeNat  UMETA(DisplayName = "통신사 NAT"),
	/** 쓸 수 있는 외부 포트를 못 찾았다 */
	PortConflict     UMETA(DisplayName = "포트 충돌"),
	/** 공유기가 요청을 거부했다 */
	GatewayRefused   UMETA(DisplayName = "공유기 거부"),
	NetworkError     UMETA(DisplayName = "네트워크 오류"),
	Timeout          UMETA(DisplayName = "시간 초과"),
	Unknown          UMETA(DisplayName = "알 수 없음"),
};

/**
 * 포트 열기가 끝났다.
 *
 * ★ 실패해도 반드시 온다. 호출자는 실패를 받으면 "같은 네트워크에서만 접속 가능"
 *   안내를 띄우고 원래 흐름을 그대로 진행하면 된다 — 포트 열기는 부가 기능이지
 *   방을 만드는 전제 조건이 아니다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMOUOnNatMappingFinished,
	EMOUNatResultBP, Result, int32, ExternalPort, const FString&, ExternalIp);

UCLASS()
class TEAMPROJECT_MOU_API UNatPortMappingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// --- UGameInstanceSubsystem --------------------------------------------
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- 진입점 -------------------------------------------------------------

	/**
	 * 매핑에 걸어둘 기본 Lease 길이(초). (2026-08-28: 0 → 3600)
	 *
	 * [왜 영구 매핑을 그만뒀나]
	 *   영구 매핑은 DeleteMapping 을 불러야만 사라진다. 그런데 게임이 크래시나
	 *   강제 종료로 죽으면 그 호출이 아예 안 일어나고, 매핑이 공유기에 영원히 남는다.
	 *   그 다음 실행은 718(PortConflict)을 받아 7778, 7779... 로 밀려가고,
	 *   밀린 포트로 방이 광고되면 아무도 못 들어온다.
	 *   공용 네트워크에서는 관리페이지에 못 들어가 손으로 지울 수도 없다.
	 *
	 *   Lease 를 걸어두면 **죽어도 최대 이 시간 뒤에 공유기가 알아서 지운다.**
	 *   워커가 살아 있는 동안은 절반 지점(30분)마다 갱신하므로 게임 중에 끊기지 않는다.
	 *
	 * ★ Lease 를 지원하지 않는 공유기(UPnP 오류 725)는 AddMapping 이 영구 매핑으로
	 *   자동 폴백한다. 그 경우를 대비해 FNatMappingRunnable::CleanupStaleMappings 가
	 *   시작할 때 지난 판의 찌꺼기를 치운다. 두 방어가 서로를 보완한다.
	 */
	static constexpr int32 DefaultLeaseSeconds = 3600;

	/**
	 * 공유기에 포트를 열어달라고 요청한다. 즉시 반환하고 실제 작업은 워커가 한다.
	 * 결과는 OnNatMappingFinished 로 온다 (성공이든 실패든 반드시 한 번).
	 *
	 * @param InternalPort        리슨서버가 실제로 듣는 포트. 보통 7777
	 * @param DesiredExternalPort 원하는 외부 포트. 0 이면 InternalPort 와 같게 시도한다.
	 *                            이미 쓰이는 중이면 워커가 옆 번호로 알아서 옮긴다
	 * @param LeaseSeconds        0 이면 영구 매핑. 기본값은 DefaultLeaseSeconds 와
	 *                            같은 3600 이다 — 0 을 넘기지 말 것. 이유는 위 상수 주석 참고.
	 *                            (UHT 가 기본값으로 리터럴만 받아서 상수를 못 쓴다.
	 *                             두 값이 어긋나지 않도록 아래 static_assert 로 묶어뒀다)
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|NAT")
	void BeginPortMapping(int32 InternalPort = 7777, int32 DesiredExternalPort = 0, int32 LeaseSeconds = 3600, bool bForce = false);

	/**
	 * 열어둔 포트를 닫는다. 방에서 나갈 때 부른다.
	 *
	 * ★ 안 부르면 공유기에 매핑이 남는다. Deinitialize 에서도 부르므로 게임을
	 *   정상 종료하면 결국 정리되지만, 방을 나갈 때마다 정리하는 편이 옳다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|NAT")
	void ReleasePortMapping();

	// --- 조회 ---------------------------------------------------------------

	/** 지금 포트 열기를 진행 중인가. UI 에 "포트 여는 중..." 을 띄울 때 */
	UFUNCTION(BlueprintPure, Category = "MOU|NAT")
	bool IsMappingInProgress() const { return bInProgress; }

	/**
	 * 공유기가 열어준 외부 포트. 0 이면 매핑이 없다는 뜻이다.
	 * ★ 방을 만들 때 HostPort 로 넘겨야 하는 값이 이것이다 — 0 이면 InternalPort 를 쓴다.
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|NAT")
	int32 GetMappedExternalPort() const { return MappedExternalPort; }

	/** 마지막 시도의 결과 */
	UFUNCTION(BlueprintPure, Category = "MOU|NAT")
	EMOUNatResultBP GetLastResult() const { return LastResult; }

	/** 공유기의 외부 IP. 진단용 표시에만 쓴다 */
	UFUNCTION(BlueprintPure, Category = "MOU|NAT")
	const FString& GetExternalIp() const { return LastExternalIp; }

	/** 실패 사유를 사용자에게 보여줄 문구로 바꾼다 */
	UFUNCTION(BlueprintPure, Category = "MOU|NAT")
	static FText GetNatResultText(EMOUNatResultBP Result);

	// --- 델리게이트 ----------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "MOU|NAT")
	FMOUOnNatMappingFinished OnNatMappingFinished;

private:
	bool Tick(float DeltaTime);

	/** 워커를 정리한다. Stop -> Kill(true) -> delete 순서를 반드시 지킨다. */
	void ShutdownWorker();

	FNatMappingRunnable* Worker       = nullptr;
	FRunnableThread*     WorkerThread = nullptr;

	FTSTicker::FDelegateHandle TickHandle;

	bool            bInProgress        = false;
	int32           MappedExternalPort = 0;
	EMOUNatResultBP LastResult         = EMOUNatResultBP::Unknown;
	FString         LastExternalIp;
};
