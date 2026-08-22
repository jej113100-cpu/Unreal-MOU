#include "Ability/GA_ThrowItem.h"
#include "Base/CharacterBase.h"
#include "Components/CarryingComponent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"

UGA_ThrowItem::UGA_ThrowItem()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTag ThrowTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Throw"), false);
	if (ThrowTag.IsValid())
	{
		FGameplayTagContainer AssetTagsContainer;
		AssetTagsContainer.AddTag(ThrowTag);
		SetAssetTags(AssetTagsContainer);
	}

	FGameplayTag BlockThrowTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Block.Throw"), false);
	FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
	FGameplayTag StunTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);
	FGameplayTag GroggyTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Groggy"), false);
	FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Dead"), false);
	
	FGameplayTagContainer BlockedTags;
	if (BlockThrowTag.IsValid()) BlockedTags.AddTag(BlockThrowTag);
	if (PushingTag.IsValid()) BlockedTags.AddTag(PushingTag);
	if (StunTag.IsValid()) BlockedTags.AddTag(StunTag);
	if (GroggyTag.IsValid()) BlockedTags.AddTag(GroggyTag);
	if (DeadTag.IsValid()) BlockedTags.AddTag(DeadTag);
	ActivationBlockedTags = BlockedTags;
}

bool UGA_ThrowItem::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, OUT FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	ACharacterBase* Char = GetCharacterFromActorInfo();
	if (!Char) return false;

	UCarryingComponent* CarryingComp = Char->FindComponentByClass<UCarryingComponent>();
	if (!CarryingComp || !CarryingComp->IsCarrying())
	{
		return false;
	}

	return true;
}

void UGA_ThrowItem::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
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
		if (ThrowMontage)
		{
			Char->PlayAnimMontage(ThrowMontage);
		}

		UCarryingComponent* CarryingComp = Char->FindComponentByClass<UCarryingComponent>();
		if (CarryingComp)
		{
			CarryingComp->Throw();
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}
