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

	AActor* TargetActor = FocusedActor;

	if (TargetActor->Implements<UInteractableInterface>())
	{
		if (IInteractableInterface::Execute_CanInteract(TargetActor, GetOwner()))
		{
			IInteractableInterface::Execute_Interact(TargetActor, GetOwner());
			OnInteractExecuted.Broadcast(TargetActor);
		}
	}
	else
	{
		// 블루프린트 상호작용 인터페이스(BPI_Interaction)나 NPC 등 상호작용 실행
		OnInteractExecuted.Broadcast(TargetActor);

		// 캐릭터가 아닌 순수 Pushable 오브젝트(이벤트 기믹 박스 등)인 경우에만 Push 처리
		if (!TargetActor->IsA<ACharacter>() && TargetActor->Implements<UPushableInterface>())
		{
			IPushableInterface::Execute_Push(TargetActor, GetOwner(), GetOwner()->GetActorForwardVector());
		}
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

		// 1. 대상이 메인 캐릭터인 경우: 오직 그로기 상태이고 사망하지 않았으며 자신(본인)이 아닐 때만 포커스
		if (AMainCharacter* TargetChar = Cast<AMainCharacter>(HitActor))
		{
			if (TargetChar->bIsGroggy && !TargetChar->bIsDead && TargetChar != OwnerActor)
			{
				NewFocusedActor = TargetChar;
			}
		}
		// 2. C++ IInteractableInterface 구현 액터
		else if (HitActor->Implements<UInteractableInterface>())
		{
			if (IInteractableInterface::Execute_CanInteract(HitActor, OwnerActor))
			{
				NewFocusedActor = HitActor;
			}
		}
		// 3. C++ IPushableInterface 구현 액터
		else if (HitActor->Implements<UPushableInterface>())
		{
			NewFocusedActor = HitActor;
		}
		// 4. 블루프린트 상호작용 인터페이스(BPI_Interaction) 또는 InteractWith 함수를 보유한 액터 (퀘스트 NPC, 퀘스트 오브젝트 등)
		else
		{
			bool bIsBPInteractable = false;
			if (UClass* ActorClass = HitActor->GetClass())
			{
				for (const FImplementedInterface& Interface : ActorClass->Interfaces)
				{
					if (Interface.Class && Interface.Class->GetName().Contains(TEXT("Interaction")))
					{
						bIsBPInteractable = true;
						break;
					}
				}

				if (!bIsBPInteractable && ActorClass->FindFunctionByName(FName("InteractWith")))
				{
					bIsBPInteractable = true;
				}
			}

			if (bIsBPInteractable)
			{
				NewFocusedActor = HitActor;
			}
		}
	}

	if (FocusedActor != NewFocusedActor)
	{
		FocusedActor = NewFocusedActor;
		OnFocusedInteractableChanged.Broadcast(FocusedActor);
	}
}
