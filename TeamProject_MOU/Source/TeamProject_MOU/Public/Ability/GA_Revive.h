#pragma once

#include "CoreMinimal.h"
#include "Base/GameplayAbilityBase.h"
#include "GA_Revive.generated.h"

/**
 * 팀원 부활(치료/살리기) 어빌리티
 * 상호작용 키(F)를 홀드하는 동안 활성화되며, 차징 시간(ReviveDuration) 동안 유지됩니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API UGA_Revive : public UGameplayAbilityBase
{
	GENERATED_BODY()

public:
	UGA_Revive();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Revive")
	float ReviveDuration = 3.0f;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Revive")
	TSubclassOf<class UGameplayEffect> ReviveEffectClass;

	UPROPERTY(Transient)
	FActiveGameplayEffectHandle ActiveReviveEffectHandle;
};
