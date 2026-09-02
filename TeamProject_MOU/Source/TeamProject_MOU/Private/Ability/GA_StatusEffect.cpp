#include "Ability/GA_StatusEffect.h"
#include "Base/CharacterBase.h"
#include "Base/BaseAttributeSet.h"
#include "Components/StatusComponent.h"
#include "Components/CarryingComponent.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimMontage.h"

UGA_StatusEffect::UGA_StatusEffect()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
}

void UGA_StatusEffect::SetEffectData(UStatusEffectDataAsset* NewData)
{
	EffectData = NewData;
	if (EffectData)
	{
		EffectType = EffectData->EffectType;
	}
}

void UGA_StatusEffect::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ACharacterBase* Char = GetCharacterFromActorInfo();
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();

	if (!EffectData && Char && Char->GetStatusComponent())
	{
		EffectData = Char->GetStatusComponent()->GetStatusEffectData(EffectType);
	}

	FName TagName = TEXT("State.Stunned");
	switch (EffectType)
	{
	case EStatusEffectType::Blindness:
		TagName = TEXT("State.Blindness");
		break;
	case EStatusEffectType::Stun:
		TagName = TEXT("State.Stunned");
		break;
	case EStatusEffectType::ElectricShock:
		TagName = TEXT("State.ElectricShock");
		break;
	case EStatusEffectType::Slow:
		TagName = TEXT("State.Slow");
		break;
	case EStatusEffectType::Knockdown:
		TagName = TEXT("State.Knockdown");
		break;
	case EStatusEffectType::Fear:
		TagName = TEXT("State.Fear");
		break;
	case EStatusEffectType::Taunt:
		TagName = TEXT("State.Taunt");
		break;
	default:
		break;
	}

	AppliedStatusTag = FGameplayTag::RequestGameplayTag(TagName, false);
	if (AppliedStatusTag.IsValid() && ASC)
	{
		ASC->AddLooseGameplayTag(AppliedStatusTag);
	}

	// 감전, 스턴, 넉다운 상태이상 시 들고 있던 물건 즉시 드랍
	if (EffectType == EStatusEffectType::ElectricShock || EffectType == EStatusEffectType::Stun || EffectType == EStatusEffectType::Knockdown)
	{
		if (UCarryingComponent* CarryingComp = Char ? Char->FindComponentByClass<UCarryingComponent>() : nullptr)
		{
			if (CarryingComp->IsCarrying())
			{
				CarryingComp->GrabOrDrop();
			}
		}
	}

	if (EffectType == EStatusEffectType::Slow)
	{
		if (UBaseAttributeSet* Attr = GetBaseAttributeSet())
		{
			if (HasAuthority(&ActivationInfo))
			{
				PreviousWalkSpeed = Attr->GetMoveSpeed();
				float SlowRate = EffectData ? EffectData->SlowRate : 0.5f;
				Attr->SetMoveSpeed(PreviousWalkSpeed * SlowRate);
			}
		}
	}

	if (EffectData && EffectData->AnimationMontage && Char)
	{
		Char->PlayAnimMontage(EffectData->AnimationMontage);
	}

	float Duration = EffectData ? EffectData->Duration : 2.0f;
	if (Duration > 0.0f && HasAuthority(&ActivationInfo))
	{
		GetWorld()->GetTimerManager().SetTimer(
			DurationTimerHandle,
			this,
			&UGA_StatusEffect::OnDurationExpired,
			Duration,
			false);
	}
}

void UGA_StatusEffect::OnDurationExpired()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_StatusEffect::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(DurationTimerHandle);
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (AppliedStatusTag.IsValid() && ASC)
	{
		ASC->RemoveLooseGameplayTag(AppliedStatusTag);
	}

	if (EffectType == EStatusEffectType::Slow)
	{
		if (UBaseAttributeSet* Attr = GetBaseAttributeSet())
		{
			if (HasAuthority(&ActivationInfo))
			{
				ACharacterBase* Char = GetCharacterFromActorInfo();
				float RestoredSpeed = Char ? Char->GetCalculatedWalkSpeed() : PreviousWalkSpeed;
				Attr->SetMoveSpeed(RestoredSpeed);
				if (Char && Char->GetCharacterMovement())
				{
					Char->GetCharacterMovement()->MaxWalkSpeed = RestoredSpeed;
				}
			}
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
