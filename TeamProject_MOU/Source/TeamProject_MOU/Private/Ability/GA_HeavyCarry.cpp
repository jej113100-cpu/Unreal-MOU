#include "Ability/GA_HeavyCarry.h"
#include "Base/CharacterBase.h"
#include "Base/BaseAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

UGA_HeavyCarry::UGA_HeavyCarry()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTag HeavyTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Carrying.Heavy"), false);
	if (HeavyTag.IsValid())
	{
		FGameplayTagContainer AssetTagsContainer;
		AssetTagsContainer.AddTag(HeavyTag);
		SetAssetTags(AssetTagsContainer);
		ActivationOwnedTags.AddTag(HeavyTag);
	}

	FGameplayTag BlockSprintTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Block.Sprint"), false);
	if (BlockSprintTag.IsValid())
	{
		ActivationOwnedTags.AddTag(BlockSprintTag);
	}

	FGameplayTag BlockThrowTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Block.Throw"), false);
	if (BlockThrowTag.IsValid())
	{
		ActivationOwnedTags.AddTag(BlockThrowTag);
	}

	FGameplayTag SprintTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Sprint"), false);
	if (SprintTag.IsValid())
	{
		CancelAbilitiesWithTag.AddTag(SprintTag);
		BlockAbilitiesWithTag.AddTag(SprintTag);
	}
}

void UGA_HeavyCarry::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	ACharacterBase* Char = GetCharacterFromActorInfo();

	if (ASC && HeavyCarryEffectClass)
	{
		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		Context.AddSourceObject(Char);
		ActiveHeavyCarryEffectHandle = ASC->ApplyGameplayEffectToSelf(HeavyCarryEffectClass.GetDefaultObject(), 1.0f, Context);
	}
	else if (UBaseAttributeSet* Attr = GetBaseAttributeSet())
	{
		if (HasAuthority(&ActivationInfo) && Char)
		{
			float CarrySpeed = Char->GetCalculatedWalkSpeed();
			Attr->SetMoveSpeed(CarrySpeed);
			if (Char->GetCharacterMovement())
			{
				Char->GetCharacterMovement()->MaxWalkSpeed = CarrySpeed;
			}
		}
	}
}

void UGA_HeavyCarry::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	ACharacterBase* Char = GetCharacterFromActorInfo();

	if (ASC && ActiveHeavyCarryEffectHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(ActiveHeavyCarryEffectHandle);
		ActiveHeavyCarryEffectHandle.Invalidate();
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

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
