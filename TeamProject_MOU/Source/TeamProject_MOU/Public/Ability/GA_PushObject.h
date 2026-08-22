#pragma once

#include "CoreMinimal.h"
#include "Base/GameplayAbilityBase.h"
#include "GA_PushObject.generated.h"

UCLASS()
class TEAMPROJECT_MOU_API UGA_PushObject : public UGameplayAbilityBase
{
	GENERATED_BODY()

public:
	UGA_PushObject();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Push")
	TSubclassOf<class UGameplayEffect> PushEffectClass;

	UPROPERTY(Transient)
	FActiveGameplayEffectHandle ActivePushEffectHandle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Push")
	float PushWalkSpeed = 150.0f;

	float OriginalBaseMoveSpeed = 300.0f;
	bool bOriginalOrientRotation = true;
};
