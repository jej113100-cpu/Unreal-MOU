#include "Ability/GA_EquipSlot.h"
#include "Base/CharacterBase.h"
#include "Components/InventoryComponent.h"
#include "AbilitySystemComponent.h"

UGA_EquipSlot::UGA_EquipSlot()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTag EquipTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Equip"), false);
	if (EquipTag.IsValid())
	{
		FGameplayTagContainer AssetTagsContainer;
		AssetTagsContainer.AddTag(EquipTag);
		SetAssetTags(AssetTagsContainer);
	}

	FGameplayTag HeavyCarryTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Carrying.Heavy"), false);
	FGameplayTag StunTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);
	FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
	FGameplayTag GroggyTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Groggy"), false);
	FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Dead"), false);

	FGameplayTagContainer BlockedTags;
	if (HeavyCarryTag.IsValid()) BlockedTags.AddTag(HeavyCarryTag);
	if (StunTag.IsValid()) BlockedTags.AddTag(StunTag);
	if (PushingTag.IsValid()) BlockedTags.AddTag(PushingTag);
	if (GroggyTag.IsValid()) BlockedTags.AddTag(GroggyTag);
	if (DeadTag.IsValid()) BlockedTags.AddTag(DeadTag);
	ActivationBlockedTags = BlockedTags;
}

bool UGA_EquipSlot::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, OUT FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	ACharacterBase* Char = GetCharacterFromActorInfo();
	return Char && Char->CanAct();
}

void UGA_EquipSlot::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ACharacterBase* Char = GetCharacterFromActorInfo();
	if (Char)
	{
		if (UInventoryComponent* InvComp = Char->FindComponentByClass<UInventoryComponent>())
		{
			InvComp->RequestSlotAction(TargetSlotIndex);
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}
