#include "Ability/GA_Jump.h"
#include "Base/CharacterBase.h"
#include "GameFramework/Character.h"

UGA_Jump::UGA_Jump()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTag JumpTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Jump"), false);
	if (JumpTag.IsValid())
	{
		FGameplayTagContainer AssetTagsContainer;
		AssetTagsContainer.AddTag(JumpTag);
		SetAssetTags(AssetTagsContainer);
	}

	FGameplayTag BlockJumpTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Block.Jump"), false);
	FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
	FGameplayTag ImmobileTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Encumbered.Immobile"), false);
	FGameplayTag StunTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);
	FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Held"), false);
	FGameplayTag GroggyTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Groggy"), false);
	FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Dead"), false);

	FGameplayTagContainer BlockedTags;
	if (BlockJumpTag.IsValid()) BlockedTags.AddTag(BlockJumpTag);
	if (PushingTag.IsValid()) BlockedTags.AddTag(PushingTag);
	if (ImmobileTag.IsValid()) BlockedTags.AddTag(ImmobileTag);
	if (StunTag.IsValid()) BlockedTags.AddTag(StunTag);
	if (HeldTag.IsValid()) BlockedTags.AddTag(HeldTag);
	if (GroggyTag.IsValid()) BlockedTags.AddTag(GroggyTag);
	if (DeadTag.IsValid()) BlockedTags.AddTag(DeadTag);
	ActivationBlockedTags = BlockedTags;
}

bool UGA_Jump::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, OUT FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	ACharacterBase* Char = GetCharacterFromActorInfo();
	return Char && Char->CanMove() && Char->CanJump();
}

void UGA_Jump::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
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
		Char->Jump();
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}
