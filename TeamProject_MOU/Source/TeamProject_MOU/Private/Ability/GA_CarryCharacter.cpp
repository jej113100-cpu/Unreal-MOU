#include "Ability/GA_CarryCharacter.h"
#include "Base/CharacterBase.h"
#include "Base/BaseAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

UGA_CarryCharacter::UGA_CarryCharacter()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTag CarryCharTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Carrying.Character"), false);
	if (CarryCharTag.IsValid())
	{
		FGameplayTagContainer AssetTagsContainer;
		AssetTagsContainer.AddTag(CarryCharTag);
		SetAssetTags(AssetTagsContainer);
		ActivationOwnedTags.AddTag(CarryCharTag);
	}

	FGameplayTag HeavyTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Carrying.Heavy"), false);
	if (HeavyTag.IsValid())
	{
		ActivationBlockedTags.AddTag(HeavyTag);
	}
}

void UGA_CarryCharacter::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	ACharacterBase* Char = GetCharacterFromActorInfo();

	if (ASC && CarryCharacterEffectClass)
	{
		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		Context.AddSourceObject(Char);
		ActiveCarryCharacterEffectHandle = ASC->ApplyGameplayEffectToSelf(CarryCharacterEffectClass.GetDefaultObject(), 1.0f, Context);
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

void UGA_CarryCharacter::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	ACharacterBase* Char = GetCharacterFromActorInfo();

	if (ASC && ActiveCarryCharacterEffectHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(ActiveCarryCharacterEffectHandle);
		ActiveCarryCharacterEffectHandle.Invalidate();
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
