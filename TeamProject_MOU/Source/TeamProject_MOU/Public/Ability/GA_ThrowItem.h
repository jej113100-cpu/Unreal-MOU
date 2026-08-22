#pragma once

#include "CoreMinimal.h"
#include "Base/GameplayAbilityBase.h"
#include "GA_ThrowItem.generated.h"

/**
 * 아이템 및 캐릭터 투척 어빌리티
 */
UCLASS()
class TEAMPROJECT_MOU_API UGA_ThrowItem : public UGameplayAbilityBase
{
	GENERATED_BODY()

public:
	UGA_ThrowItem();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Throw")
	TObjectPtr<class UAnimMontage> ThrowMontage;
};
