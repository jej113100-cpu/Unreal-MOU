#pragma once

#include "CoreMinimal.h"
#include "Base/GameplayAbilityBase.h"
#include "GA_CoopCarry.generated.h"

UCLASS()
class TEAMPROJECT_MOU_API UGA_CoopCarry : public UGameplayAbilityBase
{
	GENERATED_BODY()

public:
	UGA_CoopCarry();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UFUNCTION(BlueprintCallable, Category = "Carry|Coop")
	void SetCoopSpeedRatio(float Ratio);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry|Coop")
	TSubclassOf<class UGameplayEffect> CoopCarryEffectClass;

	UPROPERTY(Transient)
	FActiveGameplayEffectHandle ActiveCoopCarryEffectHandle;

	float CurrentRatio = 1.0f;
	float BaseWalkSpeed = 500.0f;
};
