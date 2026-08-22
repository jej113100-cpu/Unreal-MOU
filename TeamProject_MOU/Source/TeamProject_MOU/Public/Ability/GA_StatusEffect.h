#pragma once

#include "CoreMinimal.h"
#include "Base/GameplayAbilityBase.h"
#include "StatusEffect/StatusEffectDataAsset.h"
#include "GA_StatusEffect.generated.h"

UCLASS()
class TEAMPROJECT_MOU_API UGA_StatusEffect : public UGameplayAbilityBase
{
	GENERATED_BODY()

public:
	UGA_StatusEffect();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UFUNCTION(BlueprintCallable, Category = "StatusEffect")
	void SetEffectData(UStatusEffectDataAsset* NewData);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "StatusEffect")
	EStatusEffectType EffectType = EStatusEffectType::Stun;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "StatusEffect")
	TObjectPtr<UStatusEffectDataAsset> EffectData;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "StatusEffect")
	TSubclassOf<class UGameplayEffect> StatusGameplayEffectClass;

	UPROPERTY(Transient)
	FActiveGameplayEffectHandle ActiveEffectHandle;

	FTimerHandle DurationTimerHandle;
	float PreviousWalkSpeed = 500.0f;
	FGameplayTag AppliedStatusTag;

	void OnDurationExpired();
};
