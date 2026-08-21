#include "Ability/GA_Emote.h"
#include "Base/CharacterBase.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"

UGA_Emote::UGA_Emote()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTag EmoteTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Emote"), false);
	if (EmoteTag.IsValid())
	{
		FGameplayTagContainer AssetTagsContainer;
		AssetTagsContainer.AddTag(EmoteTag);
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

bool UGA_Emote::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, OUT FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	ACharacterBase* Char = GetCharacterFromActorInfo();
	return Char && Char->CanAct();
}

void UGA_Emote::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
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
		if (Char->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false)))
		{
			// 밀기 모드 어빌리티 취소
			static const FGameplayTagContainer PushTagContainer(FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing")));
			if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
			{
				ASC->CancelAbilities(&PushTagContainer);
			}
		}

		if (EmoteMontage)
		{
			Char->PlayAnimMontage(EmoteMontage);
		}
	}
}

void UGA_Emote::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	ACharacterBase* Char = GetCharacterFromActorInfo();
	if (Char && EmoteMontage)
	{
		Char->StopAnimMontage(EmoteMontage);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
