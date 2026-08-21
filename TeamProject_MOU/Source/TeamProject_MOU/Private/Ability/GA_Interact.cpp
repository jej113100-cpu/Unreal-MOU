#include "Ability/GA_Interact.h"
#include "Base/CharacterBase.h"
#include "Components/InteractionComponent.h"
#include "AbilitySystemComponent.h"

UGA_Interact::UGA_Interact()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTag InteractTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Interact"), false);
	if (InteractTag.IsValid())
	{
		FGameplayTagContainer AssetTagsContainer;
		AssetTagsContainer.AddTag(InteractTag);
		SetAssetTags(AssetTagsContainer);
	}

	FGameplayTag StunTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);
	FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Held"), false);
	FGameplayTag GroggyTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Groggy"), false);
	FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Dead"), false);

	FGameplayTagContainer BlockedTags;
	if (StunTag.IsValid()) BlockedTags.AddTag(StunTag);
	if (HeldTag.IsValid()) BlockedTags.AddTag(HeldTag);
	if (GroggyTag.IsValid()) BlockedTags.AddTag(GroggyTag);
	if (DeadTag.IsValid()) BlockedTags.AddTag(DeadTag);
	ActivationBlockedTags = BlockedTags;
}

bool UGA_Interact::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, OUT FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	ACharacterBase* Char = GetCharacterFromActorInfo();
	return Char && Char->CanAct();
}

void UGA_Interact::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
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
		if (UInteractionComponent* InteractComp = Char->FindComponentByClass<UInteractionComponent>())
		{
			InteractComp->PerformInteraction();
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}
