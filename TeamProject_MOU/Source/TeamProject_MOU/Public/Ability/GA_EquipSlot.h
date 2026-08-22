#pragma once

#include "CoreMinimal.h"
#include "Base/GameplayAbilityBase.h"
#include "GA_EquipSlot.generated.h"

/**
 * 퀵슬롯 아이템 장착 / 수납 어빌리티 (몽타주 없이 즉시 전환)
 */
UCLASS()
class TEAMPROJECT_MOU_API UGA_EquipSlot : public UGameplayAbilityBase
{
	GENERATED_BODY()

public:
	UGA_EquipSlot();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	// 장착/수납할 인벤토리 슬롯 번호 (0, 1, 2)
	UPROPERTY(BlueprintReadWrite, Category = "Equip")
	int32 TargetSlotIndex = 0;
};
