#pragma once

#include "CoreMinimal.h"
#include "Base/GameplayAbilityBase.h"
#include "GA_Groggy.generated.h"

/**
 * 그로기(1차 다운) 어빌리티
 */
UCLASS()
class TEAMPROJECT_MOU_API UGA_Groggy : public UGameplayAbilityBase
{
	GENERATED_BODY()

public:
	UGA_Groggy();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
};
