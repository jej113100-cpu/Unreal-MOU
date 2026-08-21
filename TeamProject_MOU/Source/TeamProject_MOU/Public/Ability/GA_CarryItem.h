#pragma once

#include "CoreMinimal.h"
#include "Base/GameplayAbilityBase.h"
#include "GA_CarryItem.generated.h"

UCLASS()
class TEAMPROJECT_MOU_API UGA_CarryItem : public UGameplayAbilityBase
{
	GENERATED_BODY()

public:
	UGA_CarryItem();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
};
