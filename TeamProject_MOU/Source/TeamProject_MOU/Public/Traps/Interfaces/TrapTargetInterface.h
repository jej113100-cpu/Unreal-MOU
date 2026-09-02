#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Traps/TrapEnums.h"
#include "GameplayEffect.h"
#include "TrapTargetInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UTrapTargetInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 함정의 피해, 상태이상(GAS), 물리 충격, 배터리 방전, 물품 강제 드랍 효과를
 * 대상(플레이어, 몬스터, 패키지)이 수신하기 위한 공통 인터페이스
 */
class TEAMPROJECT_MOU_API ITrapTargetInterface
{
	GENERATED_BODY()

public:
	/** 함정에 의한 대미지를 적용합니다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Trap|Target")
	void ApplyTrapDamage(float DamageAmount, AActor* Causer);

	/** 함정에 의한 GAS GameplayEffect를 적용합니다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Trap|Target")
	void ApplyTrapStatusEffect(TSubclassOf<UGameplayEffect> EffectClass, AActor* Causer);

	/** 함정에 의한 물리 임펄스/넉백을 적용합니다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Trap|Target")
	void ApplyTrapImpulse(FVector ImpulseVector);

	/** 소지 중인 아이템이나 패키지를 강제로 떨어뜨리게 합니다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Trap|Target")
	void ForceDropCarriedItem();

	/** 손전등/장비 배터리를 방전시킵니다. (플레이어 전용) */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Trap|Target")
	void DrainBattery(float DrainAmount);

	/** 함정 상호작용 알림을 받습니다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Trap|Target")
	void OnTrapHazardEncountered(ETrapHazardType HazardType, AActor* TrapActor);
};
