#include "Ability/GA_Sprint.h"
#include "Base/CharacterBase.h"
#include "Base/BaseAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

UGA_Sprint::UGA_Sprint()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTag SprintTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Sprint"), false);
	if (SprintTag.IsValid())
	{
		FGameplayTagContainer AssetTagsContainer;
		AssetTagsContainer.AddTag(SprintTag);
		SetAssetTags(AssetTagsContainer);
	}

	FGameplayTag BlockSprintTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Block.Sprint"), false);
	if (BlockSprintTag.IsValid())
	{
		ActivationBlockedTags.AddTag(BlockSprintTag);
	}

	FGameplayTag StunTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);
	if (StunTag.IsValid())
	{
		ActivationBlockedTags.AddTag(StunTag);
	}

	FGameplayTag OverloadedTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Encumbered.Overloaded"), false);
	if (OverloadedTag.IsValid())
	{
		ActivationBlockedTags.AddTag(OverloadedTag);
	}

	FGameplayTag ImmobileTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Encumbered.Immobile"), false);
	if (ImmobileTag.IsValid())
	{
		ActivationBlockedTags.AddTag(ImmobileTag);
	}

	FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
	if (PushingTag.IsValid())
	{
		ActivationBlockedTags.AddTag(PushingTag);
	}

	FGameplayTag GroggyTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Groggy"), false);
	if (GroggyTag.IsValid())
	{
		ActivationBlockedTags.AddTag(GroggyTag);
	}

	FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Dead"), false);
	if (DeadTag.IsValid())
	{
		ActivationBlockedTags.AddTag(DeadTag);
	}
}

bool UGA_Sprint::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, OUT FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	ACharacterBase* Char = GetCharacterFromActorInfo();
	if (!Char || !Char->CanMove())
	{
		return false;
	}

	if (UBaseAttributeSet* Attr = GetBaseAttributeSet())
	{
		if (Attr->GetStemina() <= 0.0f)
		{
			return false;
		}
	}

	return true;
}

void UGA_Sprint::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	ACharacterBase* Char = GetCharacterFromActorInfo();

	if (ASC && SprintEffectClass)
	{
		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		Context.AddSourceObject(Char);
		ActiveSprintEffectHandle = ASC->ApplyGameplayEffectToSelf(SprintEffectClass.GetDefaultObject(), 1.0f, Context);
	}
	else if (UBaseAttributeSet* Attr = GetBaseAttributeSet())
	{
		if (HasAuthority(&ActivationInfo))
		{
			OriginalBaseMoveSpeed = Attr->GetMoveSpeed();
			Attr->SetMoveSpeed(OriginalBaseMoveSpeed * SprintSpeedMultiplier);
		}
	}
}

void UGA_Sprint::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (ASC && ActiveSprintEffectHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(ActiveSprintEffectHandle);
		ActiveSprintEffectHandle.Invalidate();
	}
	else if (UBaseAttributeSet* Attr = GetBaseAttributeSet())
	{
		if (HasAuthority(&ActivationInfo))
		{
			Attr->SetMoveSpeed(OriginalBaseMoveSpeed);
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
