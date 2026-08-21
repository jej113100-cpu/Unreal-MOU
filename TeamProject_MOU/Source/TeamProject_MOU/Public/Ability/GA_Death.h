#pragma once

#include "CoreMinimal.h"
#include "Base/GameplayAbilityBase.h"
#include "GA_Death.generated.h"

/**
 * 완전 사망(2차 다운) 어빌리티
 */
UCLASS()
class TEAMPROJECT_MOU_API UGA_Death : public UGameplayAbilityBase
{
	GENERATED_BODY()

public:
	UGA_Death();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
