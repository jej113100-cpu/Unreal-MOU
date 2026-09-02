#pragma once

#include "CoreMinimal.h"
#include "Item/WeaponItemBase.h"
#include "GameplayTagContainer.h"
#include "Boomerang.generated.h"

class ACharacter;

/**
 * 부메랑 비행 단계
 *  - Idle      : 손에 들려 있음 (던질 수 있음)
 *  - Outbound  : 던져져서 앞으로 날아가는 중 (빙글빙글)
 *  - Returning : 소유자(플레이어)에게 되돌아오는 중
 * 상태는 복제되어 모든 클라이언트가 연출/판정을 동기화한다.
 */
UENUM(BlueprintType)
enum class EBoomerangState : uint8
{
	Idle		UMETA(DisplayName = "Idle (손에 있음)"),
	Outbound	UMETA(DisplayName = "Outbound (나가는 중)"),
	Returning	UMETA(DisplayName = "Returning (돌아오는 중)")
};

/**
 * ABoomerang
 * 던지면 빙글빙글 돌며 앞으로 날아갔다가 다시 던진 플레이어에게 돌아오는 무기.
 * 좌클릭(OnUse→Fire)으로 발사한다(테이저건과 동일한 부모 흐름 재사용).
 *
 * 비행은 물리 시뮬레이션이 아니라 서버 Tick에서 위치를 직접 제어(운동학적)하고,
 * SetReplicateMovement(true)로 위치/회전이 모든 클라에 복제된다.
 * 비행 중 근접 콜라이더(WeaponItemBase::MeleeCollider)로 첫 유효 타겟을 맞히면
 * 상태이상(기본 State.CC.FallDown)을 Loose 태그로 부여하고 즉시 되돌아오기로 전환한다.
 *
 * 실제 던짐/잡힘 VFX·사운드는 BP에서 OnThrown/OnCaught 이벤트로 붙인다.
 */
UCLASS()
class TEAMPROJECT_MOU_API ABoomerang : public AWeaponItemBase
{
	GENERATED_BODY()

public:
	ABoomerang();

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

#pragma region [BOOMERANG] 설정값
	// 나가는 속도 (cm/s)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Boomerang")
	float FlightSpeed = 1600.0f;

	// 돌아오는 속도 (cm/s). 보통 나가는 속도보다 조금 빠르게 잡으면 손맛이 좋다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Boomerang")
	float ReturnSpeed = 2000.0f;

	// 최대 사거리 (cm). 이 거리까지 나가면 아무도 못 맞혀도 되돌아온다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Boomerang")
	float MaxRange = 1200.0f;

	// 안전장치: 최대 비행 시간(초). 소유자를 못 찾거나 낀 경우 이 시간이 지나면 강제로 회수한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Boomerang")
	float MaxFlightTime = 5.0f;

	// 회전 속도 (deg/s) - "빙글빙글" 도는 속도. 매 프레임 SpinAxis 기준으로 회전.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Boomerang")
	float SpinSpeed = 1440.0f;

	// 회전 축 (로컬 기준). 기본 Z축 = 위에서 봤을 때 도는 형태.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Boomerang")
	FVector SpinAxis = FVector(0.0f, 0.0f, 1.0f);

	// 소유자와 이 거리(cm) 안으로 들어오면 "잡힘"으로 판정.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Boomerang")
	float CatchRadius = 120.0f;

	// 잡혔을 때 손에 재부착할 소켓 이름 (CarryingComponent의 CarrySocketName과 동일 기본값)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Boomerang")
	FName CatchSocketName = TEXT("CarrySocket");
#pragma endregion

#pragma region [BOOMERANG] 타격 설정
	// 비행 중 맞은 대상에게 부여할 상태이상 태그 (테이저와 동일한 Loose 방식).
	// 기본값은 생성자에서 State.CC.FallDown(넘어짐)으로 설정. BP에서 변경 가능.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Boomerang|Hit")
	FGameplayTag HitStatusTag;

	// 부여한 상태이상 지속시간(초). 0 이하면 부여 안 함.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Boomerang|Hit")
	float HitStatusDuration = 2.0f;

	// 적중 시 대상 Health에서 깎을 데미지. 0 이하면 데미지 안 줌. BP에서 조정.
	// (팀 정석 방식: Instant GE로 BaseAttributeSet.Health를 Additive 차감 → PostGameplayEffectExecute 사망처리 정상 동작)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Boomerang|Hit")
	float HitDamage = 20.0f;

	// 한 번 적중할 때마다 깎일 내구도. 최대 100 기준 10이면 10번 맞히면 소진. BP에서 조정.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Boomerang|Hit")
	float DurabilityCostPerHit = 10.0f;
#pragma endregion

#pragma region [BOOMERANG] 상태 (복제)
	// 현재 비행 단계. 복제되어 모든 클라에서 연출/판정 동기화.
	UPROPERTY(ReplicatedUsing = OnRep_FlightState, VisibleAnywhere, BlueprintReadOnly, Category = "Boomerang")
	EBoomerangState FlightState = EBoomerangState::Idle;

	// 복제된 상태 도착 시 훅 (필요 시 클라 연출용). 현재 비어있음.
	UFUNCTION()
	void OnRep_FlightState();
#pragma endregion

#pragma region [BOOMERANG] 사용/발사/타격
	// [BOOMERANG-000] 발사 override: 비행 시작 (부모 OnUse→Fire 흐름 재사용)
	//   이미 비행 중이면 무시한다.
	virtual void Fire() override;

	// 부메랑은 "던질 때"가 아니라 "적중했을 때" 내구도가 깎인다.
	// 따라서 부모의 발사 시점 차감은 끄고(false), ApplyWeaponHit에서 직접 차감한다. [WEAPON-013]
	virtual bool ShouldConsumeUseOnFire() const override { return false; }

	// 부메랑은 비행 중(던져서 손에 돌아올 때까지) "사용 중"이라 슬롯 변경을 막는다. [WEAPON-015]
	virtual bool bUsesInUseState() const override { return true; }

	// [BOOMERANG-001] 무기 공통 히트 override: 맞은 캐릭터에 상태이상 부여 + 내구도 차감 + 즉시 되돌아오기 전환
	virtual void ApplyWeaponHit_Implementation(AActor* HitActor, const FHitResult& Hit) override;
#pragma endregion

#pragma region [BOOMERANG] 연출 훅 (Blueprint VFX/사운드)
	// 던져질 때 (모든 클라)
	UFUNCTION(BlueprintImplementableEvent, Category = "Boomerang|FX")
	void OnThrown();

	// 손에 잡힐 때 (모든 클라)
	UFUNCTION(BlueprintImplementableEvent, Category = "Boomerang|FX")
	void OnCaught();
#pragma endregion

private:
#pragma region [BOOMERANG] 내부 구현
	// [BOOMERANG-002] 비행 시작 (서버). 손에서 분리 + 물리 끄고 운동학 이동 준비 + 콜라이더 on.
	void StartFlight();

	// [BOOMERANG-003] 되돌아오기 전환 (서버). Outbound일 때만 동작.
	void BeginReturn();

	// [BOOMERANG-004] 잡힘 처리 (서버). 손 소켓 재부착 + Idle 복귀.
	void CatchByOwner();

	// [BOOMERANG-005] 잡힘 연출/재부착 (모든 클라). 소켓 부착 + 장착 상태(물리 off, QueryOnly) 복원.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastCatch();

	// [BOOMERANG-006] 던짐 연출 (모든 클라). OnThrown BP 훅 재생.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastThrown();

	// 비행 방향(서버 계산, 수평면 기준). 나가는 동안 고정.
	FVector FlightDirection = FVector::ZeroVector;

	// 발사 시작 위치(사거리 계산용).
	FVector FlightStartLocation = FVector::ZeroVector;

	// 누적 비행 시간(초). 안전장치용.
	float ElapsedFlightTime = 0.0f;

	// 내구도가 0이 되어 "이번에 돌아오면 파괴"가 예약된 상태. 비행 중엔 파괴하지 않고
	// 손에 잡히는 순간(CatchByOwner)에 파괴한다. (서버에서만 사용)
	bool bPendingDestroyOnCatch = false;
#pragma endregion
};
