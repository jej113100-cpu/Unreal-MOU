#pragma once

#include "CoreMinimal.h"
#include "Base/GameplayAbilityBase.h"
#include "GA_Knockdown.generated.h"

class UAnimMontage;

/**
 * 기절 / 넉다운 상태를 관리하는 Gameplay Ability
 */
UCLASS()
class TEAMPROJECT_MOU_API UGA_Knockdown : public UGameplayAbilityBase
{
	GENERATED_BODY()

public:
	UGA_Knockdown();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	/* 넉다운 재생 몽타주 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Knockdown")
	TObjectPtr<UAnimMontage> KnockdownMontage;

	/* 기절/넉다운 기본 지속시간 (초) - 몽타주 길이가 0 이하일 때 폴백 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Knockdown", meta = (ClampMin = "0.1"))
	float FallbackDuration = 2.0f;

	FTimerHandle KnockdownTimerHandle;

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageInterrupted();

	void OnKnockdownDurationExpired();
};
