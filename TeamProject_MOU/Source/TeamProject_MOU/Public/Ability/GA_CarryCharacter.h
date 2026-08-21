#pragma once

#include "CoreMinimal.h"
#include "Base/GameplayAbilityBase.h"
#include "GA_CarryCharacter.generated.h"

UCLASS()
class TEAMPROJECT_MOU_API UGA_CarryCharacter : public UGameplayAbilityBase
{
	GENERATED_BODY()

public:
	UGA_CarryCharacter();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry|Character")
	TSubclassOf<class UGameplayEffect> CarryCharacterEffectClass;

	UPROPERTY(Transient)
	FActiveGameplayEffectHandle ActiveCarryCharacterEffectHandle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry|Character")
	float SpeedMultiplier = 0.75f;

	float OriginalBaseMoveSpeed = 300.0f;
};
