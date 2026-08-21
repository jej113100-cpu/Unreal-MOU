#include "Ability/GA_CoopCarry.h"
#include "Base/CharacterBase.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

UGA_CoopCarry::UGA_CoopCarry()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTag CoopTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Carrying.Coop"), false);
	if (CoopTag.IsValid())
	{
		FGameplayTagContainer AssetTagsContainer;
		AssetTagsContainer.AddTag(CoopTag);
		SetAssetTags(AssetTagsContainer);
	}
}

void UGA_CoopCarry::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ACharacterBase* Char = GetCharacterFromActorInfo();
	if (Char && Char->GetCharacterMovement())
	{
		BaseWalkSpeed = Char->GetCharacterMovement()->MaxWalkSpeed;
	}
}

void UGA_CoopCarry::SetCoopSpeedRatio(float Ratio)
{
	CurrentRatio = FMath::Clamp(Ratio, 0.1f, 1.0f);

	ACharacterBase* Char = GetCharacterFromActorInfo();
	if (Char && Char->GetCharacterMovement())
	{
		Char->GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed * CurrentRatio;
	}
}

void UGA_CoopCarry::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	ACharacterBase* Char = GetCharacterFromActorInfo();
	if (Char && Char->GetCharacterMovement())
	{
		Char->GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
