#include "Components/StatusComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemInterface.h"
#include "Abilities/GameplayAbility.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "StatusEffect/StatusEffectAbilitySetDataAsset.h"
#include "StatusEffect/StatusEffectDataAsset.h"

UStatusComponent::UStatusComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	const FGameplayTag NPCActionTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Ability.NPC.Action")), false);
	if (NPCActionTag.IsValid())
	{
		AbilityTagsToCancelOnMovementBlockingCC.AddTag(NPCActionTag);
	}

	const FName DefaultBlockingTagNames[] =
	{
		TEXT("State.Stunned"),
		TEXT("State.ElectricShock"),
		TEXT("State.Knockdown"),
		TEXT("State.KnockedBack"),
		TEXT("State.Primary.Stuned"),
		TEXT("State.Held"),
		TEXT("State.Player.Held")
	};

	for (const FName& TagName : DefaultBlockingTagNames)
	{
		const FGameplayTag BlockingTag = FGameplayTag::RequestGameplayTag(TagName, false);
		if (BlockingTag.IsValid())
		{
			MovementBlockingCCTags.AddTag(BlockingTag);
		}
	}
}

void UStatusComponent::BeginPlay()
{
	Super::BeginPlay();

	AbilitySystemComponent = FindOwnerAbilitySystemComponent();
	GrantStatusEffectAbilities();
	BindMovementBlockingCCTagEvents();
}

void UStatusComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AbilitySystemComponent)
	{
		for (const FGameplayTag& Tag : MovementBlockingCCTags)
		{
			AbilitySystemComponent
				->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved)
				.RemoveAll(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

UAbilitySystemComponent* UStatusComponent::FindOwnerAbilitySystemComponent() const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	if (const IAbilitySystemInterface* AbilitySystemInterface = Cast<IAbilitySystemInterface>(OwnerActor))
	{
		if (UAbilitySystemComponent* OwnerASC = AbilitySystemInterface->GetAbilitySystemComponent())
		{
			return OwnerASC;
		}
	}

	return OwnerActor->FindComponentByClass<UAbilitySystemComponent>();
}

void UStatusComponent::GrantStatusEffectAbilities()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || !AbilitySetData)
	{
		return;
	}

	if (!AbilitySystemComponent)
	{
		AbilitySystemComponent = FindOwnerAbilitySystemComponent();
	}

	if (!AbilitySystemComponent)
	{
		return;
	}

	const int32 AbilityLevel = FMath::Max(1, AbilitySetData->AbilityLevel);
	for (const TSubclassOf<UGameplayAbility>& AbilityClass : AbilitySetData->Abilities)
	{
		if (!AbilityClass || AbilitySystemComponent->FindAbilitySpecFromClass(AbilityClass))
		{
			continue;
		}

		const FGameplayAbilitySpecHandle GrantedHandle = AbilitySystemComponent->GiveAbility(
			FGameplayAbilitySpec(AbilityClass, AbilityLevel));

		if (GrantedHandle.IsValid())
		{
			GrantedAbilityHandles.Add(GrantedHandle);
		}
	}
}

UStatusEffectDataAsset* UStatusComponent::GetStatusEffectData(EStatusEffectType EffectType) const
{
	if (!AbilitySetData)
	{
		return nullptr;
	}

	for (UStatusEffectDataAsset* EffectData : AbilitySetData->EffectDataAssets)
	{
		if (EffectData && EffectData->EffectType == EffectType)
		{
			return EffectData;
		}
	}

	return nullptr;
}

void UStatusComponent::BindMovementBlockingCCTagEvents()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	for (const FGameplayTag& Tag : MovementBlockingCCTags)
	{
		if (Tag.IsValid())
		{
			AbilitySystemComponent
				->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved)
				.AddUObject(this, &UStatusComponent::HandleMovementBlockingCCTagChanged);
		}
	}

	RefreshCrowdControlledBlackboard();
}

int32 UStatusComponent::GetMovementBlockingCCTagCount() const
{
	if (!AbilitySystemComponent)
	{
		return 0;
	}

	int32 TotalTagCount = 0;
	for (const FGameplayTag& Tag : MovementBlockingCCTags)
	{
		if (Tag.IsValid())
		{
			TotalTagCount += AbilitySystemComponent->GetTagCount(Tag);
		}
	}

	return TotalTagCount;
}

void UStatusComponent::HandleMovementBlockingCCTagChanged(FGameplayTag ChangedTag, int32 NewCount)
{
	RefreshCrowdControlledBlackboard();
}

void UStatusComponent::RefreshCrowdControlledBlackboard()
{
	const bool bIsMovementBlocked = GetMovementBlockingCCTagCount() > 0;
	const bool bCrowdControlStateChanged = bIsMovementBlocked != bWasMovementBlockedByCC;
	const bool bJustBecameMovementBlocked = bIsMovementBlocked && !bWasMovementBlockedByCC;

	if (bJustBecameMovementBlocked)
	{
		CancelAbilitiesBlockedByCrowdControl();
	}

	if (bCrowdControlStateChanged)
	{
		ApplyMovementBlockedState(bIsMovementBlocked);
	}

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	AAIController* AIController = OwnerPawn ? Cast<AAIController>(OwnerPawn->GetController()) : nullptr;
	if (AIController)
	{
		if (UBlackboardComponent* BlackboardComponent = AIController->GetBlackboardComponent())
		{
			BlackboardComponent->SetValueAsBool(CrowdControlledBlackboardKey, bIsMovementBlocked);
		}

		if (bStopAIMovementWhenCrowdControlled && bJustBecameMovementBlocked)
		{
			AIController->StopMovement();
		}
	}

	bWasMovementBlockedByCC = bIsMovementBlocked;

	if (bCrowdControlStateChanged)
	{
		OnCrowdControlChanged.Broadcast(bIsMovementBlocked);
	}
}

void UStatusComponent::ApplyMovementBlockedState(bool bShouldBlockMovement)
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		return;
	}

	UCharacterMovementComponent* MovementComponent = OwnerCharacter->GetCharacterMovement();
	if (!MovementComponent)
	{
		return;
	}

	// CC 시작/종료 시점 모두 잔여 속도와 누적 입력을 제거해 해제 순간 튀어나가는 현상을 막습니다.
	MovementComponent->StopMovementImmediately();
	OwnerCharacter->ConsumeMovementInputVector();

	if (bShouldBlockMovement)
	{
		if (!bMovementModeOverriddenByCC)
		{
			PreviousMovementMode = MovementComponent->MovementMode;
			PreviousCustomMovementMode = MovementComponent->CustomMovementMode;
			bMovementModeOverriddenByCC = true;
		}

		MovementComponent->DisableMovement();
		return;
	}

	if (bMovementModeOverriddenByCC)
	{
		MovementComponent->SetMovementMode(PreviousMovementMode, PreviousCustomMovementMode);
		bMovementModeOverriddenByCC = false;
	}
}

void UStatusComponent::CancelAbilitiesBlockedByCrowdControl()
{
	const AActor* OwnerActor = GetOwner();
	if (!AbilitySystemComponent || !OwnerActor || !OwnerActor->HasAuthority()
		|| AbilityTagsToCancelOnMovementBlockingCC.IsEmpty())
	{
		return;
	}

	AbilitySystemComponent->CancelAbilities(&AbilityTagsToCancelOnMovementBlockingCC);
}

bool UStatusComponent::HasStatusTag(FGameplayTag Tag) const
{
	return AbilitySystemComponent && Tag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(Tag);
}

FGameplayTagContainer UStatusComponent::GetActiveStatusTags() const
{
	FGameplayTagContainer OwnedTags;
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->GetOwnedGameplayTags(OwnedTags);
	}
	return OwnedTags;
}

void UStatusComponent::AddStatusTag(FGameplayTag Tag)
{
	AActor* OwnerActor = GetOwner();
	if (AbilitySystemComponent && OwnerActor && OwnerActor->HasAuthority() && Tag.IsValid())
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(Tag);
		UAbilitySystemBlueprintLibrary::AddLooseGameplayTags(OwnerActor, Tags, true);
	}
}

void UStatusComponent::RemoveStatusTag(FGameplayTag Tag)
{
	AActor* OwnerActor = GetOwner();
	if (AbilitySystemComponent && OwnerActor && OwnerActor->HasAuthority() && Tag.IsValid())
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(Tag);
		UAbilitySystemBlueprintLibrary::RemoveLooseGameplayTags(OwnerActor, Tags, true);
	}
}

bool UStatusComponent::CanMove() const
{
	static const FGameplayTag StunTag = FGameplayTag::RequestGameplayTag(FName("State.Primary.Stuned"), false);
	static const FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Held"), false);
	static const FGameplayTag KnockedTag = FGameplayTag::RequestGameplayTag(FName("State.KnockedBack"), false);

	return !HasStatusTag(StunTag) && !HasStatusTag(HeldTag) && !HasStatusTag(KnockedTag);
}

bool UStatusComponent::CanAct() const
{
	static const FGameplayTag StunTag = FGameplayTag::RequestGameplayTag(FName("State.Primary.Stuned"), false);
	static const FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Held"), false);

	return !HasStatusTag(StunTag) && !HasStatusTag(HeldTag);
}

bool UStatusComponent::CanSprint() const
{
	static const FGameplayTag ExhaustedTag = FGameplayTag::RequestGameplayTag(FName("State.Exhausted"), false);
	return CanMove() && !HasStatusTag(ExhaustedTag);
}

bool UStatusComponent::CanCarry() const
{
	static const FGameplayTag ArmTag = FGameplayTag::RequestGameplayTag(FName("Debuff.ArmDamaged"), false);
	return CanAct() && !HasStatusTag(ArmTag);
}
