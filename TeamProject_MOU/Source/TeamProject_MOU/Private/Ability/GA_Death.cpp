#include "Ability/GA_Death.h"
#include "Player/MainCharacter.h"
#include "Components/InventoryComponent.h"
#include "Components/CarryingComponent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Base/ItemBase.h"

UGA_Death::UGA_Death()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Dead"), false);
	if (DeadTag.IsValid())
	{
		FGameplayTagContainer AssetTagsContainer;
		AssetTagsContainer.AddTag(DeadTag);
		SetAssetTags(AssetTagsContainer);
	}

	FGameplayTag SprintTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Sprint"), false);
	FGameplayTag ThrowTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Throw"), false);
	FGameplayTag JumpTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Jump"), false);
	FGameplayTag EmoteTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Emote"), false);
	FGameplayTag InteractTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Interact"), false);
	FGameplayTag EquipTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Equip"), false);

	if (SprintTag.IsValid()) BlockAbilitiesWithTag.AddTag(SprintTag);
	if (ThrowTag.IsValid()) BlockAbilitiesWithTag.AddTag(ThrowTag);
	if (JumpTag.IsValid()) BlockAbilitiesWithTag.AddTag(JumpTag);
	if (EmoteTag.IsValid()) BlockAbilitiesWithTag.AddTag(EmoteTag);
	if (InteractTag.IsValid()) BlockAbilitiesWithTag.AddTag(InteractTag);
	if (EquipTag.IsValid()) BlockAbilitiesWithTag.AddTag(EquipTag);
}

void UGA_Death::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
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
		MainChar->DownCount = 2;
		MainChar->bIsDead = true;

		// 들고 있던 물건 떨어뜨리기
		if (UCarryingComponent* CarryingComp = MainChar->FindComponentByClass<UCarryingComponent>())
		{
			if (CarryingComp->IsCarrying())
			{
				CarryingComp->GrabOrDrop();
			}
		}

		// 인벤토리 아이템 사방 드랍
		if (UInventoryComponent* InvComp = MainChar->FindComponentByClass<UInventoryComponent>())
		{
			for (int32 i = 0; i < InvComp->InventorySlots.Num(); ++i)
			{
				if (AItemBase* Item = InvComp->InventorySlots[i])
				{
					Item->MulticastOnEquipped(nullptr);
					Item->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
					Item->SetActorLocation(MainChar->GetActorLocation() + FVector(0.0f, 0.0f, 30.0f));

					FVector RandDir = FMath::VRand();
					RandDir.Z = FMath::Abs(RandDir.Z) + 0.5f;
					RandDir.Normalize();

					Item->Throw(RandDir * 400.0f, MainChar);
					InvComp->InventorySlots[i] = nullptr;
					InvComp->OnInventorySlotChanged.Broadcast(i, nullptr);
				}
			}
		}

		if (UCarryingComponent* CarryingComp = MainChar->FindComponentByClass<UCarryingComponent>())
		{
			CarryingComp->UpdateCharacterTotalWeight();
		}

		// 사망 표정 변경 및 이벤트 방송
		MainChar->MulticastOnDeath();
	}
}
