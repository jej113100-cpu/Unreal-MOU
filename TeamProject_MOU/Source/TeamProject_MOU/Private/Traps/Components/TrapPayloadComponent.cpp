#include "Traps/Components/TrapPayloadComponent.h"
#include "Traps/Data/TrapDataAsset.h"
#include "Traps/Interfaces/TrapTargetInterface.h"
#include "GameplayEffect.h"

UTrapPayloadComponent::UTrapPayloadComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTrapPayloadComponent::ExecutePayloadOnActor(AActor* TargetActor, const UTrapDataAsset* InTrapData)
{
	if (!TargetActor || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (!TargetActor->GetClass()->ImplementsInterface(UTrapTargetInterface::StaticClass()))
	{
		return;
	}

	float Damage = InTrapData ? InTrapData->BaseDamage : FallbackDamage;
	float BatteryDrain = InTrapData ? InTrapData->BatteryDrainAmount : 0.0f;
	float ImpulseStrength = InTrapData ? InTrapData->ImpulseStrength : 0.0f;
	bool bForceDrop = InTrapData ? InTrapData->bForceDropItem : false;
	if (DefaultHazardType == ETrapHazardType::ElectricShock)
	{
		bForceDrop = true;
	}
	TSubclassOf<UGameplayEffect> EffectClass = InTrapData ? InTrapData->StatusEffectClass : nullptr;

	// 1. 대미지 적용
	if (Damage > 0.0f)
	{
		ITrapTargetInterface::Execute_ApplyTrapDamage(TargetActor, Damage, GetOwner());
	}

	// 2. GAS 상태이상(GameplayEffect) 적용
	if (EffectClass)
	{
		ITrapTargetInterface::Execute_ApplyTrapStatusEffect(TargetActor, EffectClass, GetOwner());
	}

	// 3. 물리 밀침 인가
	if (ImpulseStrength > 0.0f)
	{
		FVector Direction = CustomImpulseDirection.IsNearlyZero() ? (TargetActor->GetActorLocation() - GetOwner()->GetActorLocation()).GetSafeNormal() : CustomImpulseDirection;
		ITrapTargetInterface::Execute_ApplyTrapImpulse(TargetActor, Direction * ImpulseStrength);
	}

	// 4. 배터리 방전
	if (BatteryDrain > 0.0f)
	{
		ITrapTargetInterface::Execute_DrainBattery(TargetActor, BatteryDrain);
	}

	// 5. 물품 강제 드랍
	if (bForceDrop)
	{
		ITrapTargetInterface::Execute_ForceDropCarriedItem(TargetActor);
	}

	// 6. 상호작용 알림
	ITrapTargetInterface::Execute_OnTrapHazardEncountered(TargetActor, DefaultHazardType, GetOwner());
}

void UTrapPayloadComponent::ExecutePayloadOnActors(const TArray<AActor*>& TargetActors, const UTrapDataAsset* InTrapData)
{
	for (AActor* Target : TargetActors)
	{
		ExecutePayloadOnActor(Target, InTrapData);
	}
}
