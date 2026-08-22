#include "Ability/GA_PushObject.h"
#include "Base/CharacterBase.h"
#include "Base/BaseAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

UGA_PushObject::UGA_PushObject()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTag PushTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
	if (PushTag.IsValid())
	{
		FGameplayTagContainer AssetTagsContainer;
		AssetTagsContainer.AddTag(PushTag);
		SetAssetTags(AssetTagsContainer);
		ActivationOwnedTags.AddTag(PushTag);
	}

	FGameplayTag SprintTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Sprint"), false);
	if (SprintTag.IsValid())
	{
		CancelAbilitiesWithTag.AddTag(SprintTag);
		BlockAbilitiesWithTag.AddTag(SprintTag);
	}

	FGameplayTag BlockSprintTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Block.Sprint"), false);
	if (BlockSprintTag.IsValid())
	{
		ActivationOwnedTags.AddTag(BlockSprintTag);
	}
}

void UGA_PushObject::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	ACharacterBase* Char = GetCharacterFromActorInfo();

	if (Char && Char->GetCharacterMovement())
	{
		bOriginalOrientRotation = Char->GetCharacterMovement()->bOrientRotationToMovement;
		Char->GetCharacterMovement()->bOrientRotationToMovement = false;
	}

	if (ASC && PushEffectClass)
	{
		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		Context.AddSourceObject(Char);
		ActivePushEffectHandle = ASC->ApplyGameplayEffectToSelf(PushEffectClass.GetDefaultObject(), 1.0f, Context);
	}
	else if (UBaseAttributeSet* Attr = GetBaseAttributeSet())
	{
		if (HasAuthority(&ActivationInfo) && Char)
		{
			float PushSpeed = Char->GetCalculatedWalkSpeed();
			Attr->SetMoveSpeed(PushSpeed);
			if (Char->GetCharacterMovement())
			{
				Char->GetCharacterMovement()->MaxWalkSpeed = PushSpeed;
			}
		}
	}
}

void UGA_PushObject::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	ACharacterBase* Char = GetCharacterFromActorInfo();

	if (ASC && ActivePushEffectHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(ActivePushEffectHandle);
		ActivePushEffectHandle.Invalidate();
	}
	else if (UBaseAttributeSet* Attr = GetBaseAttributeSet())
	{
		if (HasAuthority(&ActivationInfo) && Char)
		{
			float BaseSpeed = Char->GetCalculatedWalkSpeed();
			Attr->SetMoveSpeed(BaseSpeed);
			if (Char->GetCharacterMovement())
			{
				Char->GetCharacterMovement()->MaxWalkSpeed = BaseSpeed;
			}
		}
	}

	if (Char && Char->GetCharacterMovement())
	{
		Char->GetCharacterMovement()->bOrientRotationToMovement = bOriginalOrientRotation;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
