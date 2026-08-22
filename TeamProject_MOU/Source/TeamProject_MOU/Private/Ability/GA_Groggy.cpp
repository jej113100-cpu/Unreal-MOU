#include "Ability/GA_Groggy.h"
#include "Player/MainCharacter.h"
#include "Components/CarryingComponent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"

UGA_Groggy::UGA_Groggy()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	FGameplayTag GroggyTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Groggy"), false);
	if (GroggyTag.IsValid())
	{
		FGameplayTagContainer AssetTagsContainer;
		AssetTagsContainer.AddTag(GroggyTag);
		SetAssetTags(AssetTagsContainer);
	}

	FGameplayTag SprintTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Sprint"), false);
	FGameplayTag ThrowTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Throw"), false);
	FGameplayTag JumpTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Jump"), false);
	FGameplayTag EmoteTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Emote"), false);
	FGameplayTag InteractTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Interact"), false);

	if (SprintTag.IsValid()) BlockAbilitiesWithTag.AddTag(SprintTag);
	if (ThrowTag.IsValid()) BlockAbilitiesWithTag.AddTag(ThrowTag);
	if (JumpTag.IsValid()) BlockAbilitiesWithTag.AddTag(JumpTag);
	if (EmoteTag.IsValid()) BlockAbilitiesWithTag.AddTag(EmoteTag);
	if (InteractTag.IsValid()) BlockAbilitiesWithTag.AddTag(InteractTag);
}

void UGA_Groggy::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AMainCharacter* MainChar = Cast<AMainCharacter>(GetCharacterFromActorInfo());
	if (MainChar)
	{
		MainChar->DownCount = 1;
		MainChar->bIsGroggy = true;

		// 들고 있던 물건 떨어뜨리기
		if (UCarryingComponent* CarryingComp = MainChar->FindComponentByClass<UCarryingComponent>())
		{
			if (CarryingComp->IsCarrying())
			{
				CarryingComp->GrabOrDrop();
			}
		}

		// 달리기 / 밀기 취소
		MainChar->OnSprintEnd();
		if (MainChar->bIsPushingMode)
		{
			MainChar->StopPushMode();
		}

		// 표정 변경
		MainChar->MulticastOnEnterGroggy();
	}
}

void UGA_Groggy::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	AMainCharacter* MainChar = Cast<AMainCharacter>(GetCharacterFromActorInfo());
	if (MainChar)
	{
		MainChar->bIsGroggy = false;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
