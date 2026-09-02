#include "Ability/GA_Knockdown.h"
#include "Player/MainCharacter.h"
#include "Components/CarryingComponent.h"
#include "Components/CharacterVisualComponent.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "TimerManager.h"

UGA_Knockdown::UGA_Knockdown()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	FGameplayTag KnockdownTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Knockdown"), false);
	FGameplayTag StunnedTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);

	FGameplayTagContainer AssetTagsContainer;
	if (KnockdownTag.IsValid()) AssetTagsContainer.AddTag(KnockdownTag);
	if (StunnedTag.IsValid()) AssetTagsContainer.AddTag(StunnedTag);
	SetAssetTags(AssetTagsContainer);

	FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Dead"), false);
	if (DeadTag.IsValid())
	{
		ActivationBlockedTags.AddTag(DeadTag);
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

void UGA_Knockdown::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AMainCharacter* MainChar = Cast<AMainCharacter>(GetCharacterFromActorInfo());
	if (!MainChar)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	MainChar->SetIsStunned(true);

	// 1. 부활 차징 중이었다면 취소
	if (MainChar->bIsHoldingRevive)
	{
		MainChar->CancelReviveHold();
	}

	// 2. 들고 있던 물건 떨어뜨리기
	if (UCarryingComponent* CarryingComp = MainChar->FindComponentByClass<UCarryingComponent>())
	{
		if (CarryingComp->IsCarrying())
		{
			CarryingComp->GrabOrDrop();
		}
	}

	// 3. 달리기 및 밀기 강제 취소
	MainChar->StopSprinting();
	if (MainChar->bIsPushingMode)
	{
		MainChar->StopPushMode();
	}

	// 4. 네트워크 복제 몽타주 태스크 실행
	float AnimDuration = FallbackDuration;
	if (KnockdownMontage)
	{
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			NAME_None,
			KnockdownMontage,
			1.0f,
			NAME_None,
			false,
			1.0f);

		if (MontageTask)
		{
			MontageTask->OnCompleted.AddDynamic(this, &UGA_Knockdown::OnMontageCompleted);
			MontageTask->OnInterrupted.AddDynamic(this, &UGA_Knockdown::OnMontageInterrupted);
			MontageTask->OnCancelled.AddDynamic(this, &UGA_Knockdown::OnMontageInterrupted);
			MontageTask->ReadyForActivation();
		}

		if (KnockdownMontage->GetPlayLength() > 0.0f)
		{
			AnimDuration = KnockdownMontage->GetPlayLength();
		}
	}

	// 5. 넉다운 표정 연출
	if (UCharacterVisualComponent* VisualComp = MainChar->FindComponentByClass<UCharacterVisualComponent>())
	{
		static const FGameplayTag KnockdownStateTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Knockdown"), false);
		VisualComp->SetTagTemporaryOverride(KnockdownStateTag, AnimDuration);
	}

	// 6. 서버에서 안전 타이머 가동 (몽타주 미지정 또는 네트워크 유실 대비)
	if (HasAuthority(&ActivationInfo))
	{
		GetWorld()->GetTimerManager().SetTimer(
			KnockdownTimerHandle,
			this,
			&UGA_Knockdown::OnKnockdownDurationExpired,
			AnimDuration,
			false);
	}
}

void UGA_Knockdown::OnMontageCompleted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_Knockdown::OnMontageInterrupted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UGA_Knockdown::OnKnockdownDurationExpired()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_Knockdown::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(KnockdownTimerHandle);
	}

	AMainCharacter* MainChar = Cast<AMainCharacter>(GetCharacterFromActorInfo());
	if (MainChar)
	{
		MainChar->SetIsStunned(false);

		if (UCharacterVisualComponent* VisualComp = MainChar->FindComponentByClass<UCharacterVisualComponent>())
		{
			VisualComp->ClearTemporaryOverride();
			VisualComp->RefreshVisualState();
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
