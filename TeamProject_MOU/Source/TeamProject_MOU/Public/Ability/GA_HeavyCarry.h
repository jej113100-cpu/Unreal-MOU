#pragma once

#include "CoreMinimal.h"
#include "Base/GameplayAbilityBase.h"
#include "GA_HeavyCarry.generated.h"

UCLASS()
class TEAMPROJECT_MOU_API UGA_HeavyCarry : public UGameplayAbilityBase
{
	GENERATED_BODY()

public:
	UGA_HeavyCarry();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry|Heavy")
	TSubclassOf<class UGameplayEffect> HeavyCarryEffectClass;

	UPROPERTY(Transient)
	FActiveGameplayEffectHandle ActiveHeavyCarryEffectHandle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry|Heavy")
	float SpeedMultiplier = 0.6f;

	float OriginalBaseMoveSpeed = 300.0f;
};
