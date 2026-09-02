#pragma once

#include "CoreMinimal.h"
#include "Item/WeaponItemBase.h"
#include "TaserGun.generated.h"

class USceneComponent;

/**
 * ATaserGun
 * 전투 아이템 - 테이저건 (원거리 즉발 무기). 좌클릭(OnUse) 시 카메라 조준 방향으로
 * 피아식별 트레이스(WeaponItemBase::FireHitscan)를 발사해 맞은 캐릭터를
 * 일정 시간 감전(State.CC.Electirc) 상태로 만든다.
 * 전기 이펙트(VFX)는 블루프린트에서 BlueprintImplementableEvent로 처리.
 */
UCLASS()
class TEAMPROJECT_MOU_API ATaserGun : public AWeaponItemBase
{
	GENERATED_BODY()

public:
	ATaserGun();

#pragma region [TASER] 컴포넌트
	// 총구 지점. BP에서 메시의 총구 위치로 옮겨두면 VFX가 여기서 시작한다.
	// (StaticMesh라 소켓이 없어 별도 컴포넌트로 총구 위치를 잡음)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Taser")
	TObjectPtr<USceneComponent> MuzzlePoint;
#pragma endregion

#pragma region [TASER] 설정값
	// 트레이스 사거리 (cm) - 카메라 시점 기준 전방
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Taser")
	float TraceDistance = 1500.0f;

	// 맞은 대상의 기절 지속 시간 (초)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Taser")
	float StunDuration = 3.0f;
#pragma endregion

#pragma region [TASER] 사용/발사
	// [TASER-001] 발사 override: 카메라 조준 방향으로 피아식별 트레이스 + VFX
	// (좌클릭→횟수차감→서버권한 분기는 부모 WeaponItemBase가 처리)
	virtual void Fire() override;

	// [TASER-007] 발사 1회당 내구도 25 차감 (명중 무관). 최대 100이라 4번 쏘면 소진. [WEAPON-014]
	virtual float GetDurabilityCostPerUse() const override { return 25.0f; }

	// 테이저는 발사 후 짧은 쿨(FireCooldown) 동안 "사용 중"이라 슬롯 변경을 막는다. [WEAPON-015]
	virtual bool bUsesInUseState() const override { return true; }

	// 발사 후 슬롯 잠금이 유지되는 시간 (초). 이 시간 뒤 FinishUse로 해제.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Taser")
	float FireCooldown = 0.5f;

private:
	// 발사 쿨다운 타이머 핸들
	FTimerHandle FireCooldownTimer;
protected:

	// [TASER-006] 무기 공통 히트 처리 override: 맞은 캐릭터에 기절 부여
	virtual void ApplyWeaponHit_Implementation(AActor* HitActor, const FHitResult& Hit) override;
#pragma endregion

#pragma region [TASER] 연출 훅 (Blueprint VFX)
	// [TASER-005] 발사 이펙트 훅 (전기 줄기 등). 시작/끝 지점 전달, 모든 클라 재생
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayFireEffect(FVector Start, FVector End, bool bHit);

	// 블루프린트에서 실제 나이아가라/케이블 VFX를 붙이는 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "Taser|FX")
	void OnFireEffect(FVector Start, FVector End, bool bHit);
#pragma endregion

};
