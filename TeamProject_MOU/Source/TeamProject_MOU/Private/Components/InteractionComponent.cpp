#include "Components/InteractionComponent.h"
#include "Interfaces/InteractableInterface.h"
#include "Interfaces/PushableInterface.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Player/MainCharacter.h"
#include "Kismet/KismetSystemLibrary.h"
#include "DrawDebugHelpers.h"

UInteractionComponent::UInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UInteractionComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateFocusedInteractable();
}

void UInteractionComponent::PerformInteraction()
{
	if (!FocusedActor)
	{
		return;
	}

	if (FocusedActor->Implements<UInteractableInterface>())
	{
		if (IInteractableInterface::Execute_CanInteract(FocusedActor, GetOwner()))
		{
			IInteractableInterface::Execute_Interact(FocusedActor, GetOwner());
		}
	}
	else if (FocusedActor->Implements<UPushableInterface>())
	{
		// F키를 눌렀을 때 상호작용이 아니라면 밀기(Push) 시도
		IPushableInterface::Execute_Push(FocusedActor, GetOwner(), GetOwner()->GetActorForwardVector());
	}
}

void UInteractionComponent::UpdateFocusedInteractable()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	FVector StartLocation;
	FRotator ViewRotation;

	APawn* PawnOwner = Cast<APawn>(OwnerActor);
	if (PawnOwner)
	{
		// 로컬 컨트롤러(자신)가 아닐 경우 다른 플레이어의 시야 방향으로 UI가 뜨는 것을 방지
		if (!PawnOwner->IsLocallyControlled())
		{
			return;
		}

		if (PawnOwner->GetController())
		{
			PawnOwner->GetController()->GetPlayerViewPoint(StartLocation, ViewRotation);
		}
		else
		{
			StartLocation = OwnerActor->GetActorLocation();
			ViewRotation = OwnerActor->GetActorRotation();
		}
	}
	else
	{
		StartLocation = OwnerActor->GetActorLocation();
		ViewRotation = OwnerActor->GetActorRotation();
	}

	FVector EndLocation = StartLocation + (ViewRotation.Vector() * InteractionDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerActor);

	FHitResult HitResult;
	bool bHit = GetWorld()->SweepSingleByChannel(
		HitResult,
		StartLocation,
		EndLocation,
		FQuat::Identity,
		TraceChannel,
		FCollisionShape::MakeSphere(InteractionSphereRadius),
		QueryParams
	);

	AActor* NewFocusedActor = nullptr;
	
	if (bHit && HitResult.GetActor())
	{
		AActor* HitActor = HitResult.GetActor();

		// 대상이 메인 캐릭터인 경우: 오직 그로기 상태이고 사망하지 않았으며 자신(본인)이 아닐 때만 포커스
		if (AMainCharacter* TargetChar = Cast<AMainCharacter>(HitActor))
		{
			if (TargetChar->bIsGroggy && !TargetChar->bIsDead && TargetChar != OwnerActor)
			{
				NewFocusedActor = TargetChar;
			}
		}
		else if (HitActor->Implements<UInteractableInterface>())
		{
			if (IInteractableInterface::Execute_CanInteract(HitActor, OwnerActor))
			{
				NewFocusedActor = HitActor;
			}
		}
		else if (HitActor->Implements<UPushableInterface>())
		{
			NewFocusedActor = HitActor;
		}
	}

	if (FocusedActor != NewFocusedActor)
	{
		FocusedActor = NewFocusedActor;
		OnFocusedInteractableChanged.Broadcast(FocusedActor);
	}
}
