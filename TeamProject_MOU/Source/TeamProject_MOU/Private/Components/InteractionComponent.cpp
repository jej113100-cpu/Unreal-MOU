#include "Components/InteractionComponent.h"
#include "Interfaces/InteractableInterface.h"
#include "Interfaces/PushableInterface.h"
#include "Base/EventObjectBase.h"
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

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	// 1. 대상이 메인 캐릭터(그로기 부활 대상)인 경우
	if (AMainCharacter* TargetChar = Cast<AMainCharacter>(FocusedActor))
	{
		if (TargetChar->bIsGroggy && !TargetChar->bIsDead)
		{
			OnInteractExecuted.Broadcast(FocusedActor);
			return;
		}
	}

	// 2. C++ IInteractableInterface 구현 액터 (ItemBase, PackageBase, EventObjectBase 등)
	if (FocusedActor->Implements<UInteractableInterface>())
	{
		IInteractableInterface::Execute_Interact(FocusedActor, OwnerActor);
		OnInteractExecuted.Broadcast(FocusedActor);
	}
	// 3. 블루프린트 상호작용 액터 (퀘스트 NPC 등)
	else
	{
		// C++의 불안전한 ProcessEvent 직접 호출을 제거하고,
		// OnInteractExecuted를 통해 BP_EmoPlayer의 표준 BPI_Interaction으로 안전하게 1회 전달
		OnInteractExecuted.Broadcast(FocusedActor);
	}
}

void UInteractionComponent::UpdateFocusedInteractable()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	APawn* PawnOwner = Cast<APawn>(OwnerActor);
	if (PawnOwner)
	{
		if (!PawnOwner->IsLocallyControlled())
		{
			return;
		}
	}

	FVector CamLoc = OwnerActor->GetActorLocation() + FVector(0.0f, 0.0f, 40.0f);
	FRotator CamRot = OwnerActor->GetActorRotation();

	if (PawnOwner && PawnOwner->GetController())
	{
		PawnOwner->GetController()->GetPlayerViewPoint(CamLoc, CamRot);
	}

	AActor* NewFocusedActor = nullptr;

	auto EvaluateCandidate = [&](AActor* HitActor) -> bool
	{
		if (!HitActor || HitActor == OwnerActor) return false;

		// 1. 대상이 메인 캐릭터인 경우: 그로기 상태일 때만 포커스
		if (AMainCharacter* TargetChar = Cast<AMainCharacter>(HitActor))
		{
			if (TargetChar->bIsGroggy && !TargetChar->bIsDead && TargetChar != OwnerActor)
			{
				NewFocusedActor = TargetChar;
				return true;
			}
		}
		// 2. C++ IInteractableInterface 구현 액터
		else if (HitActor->Implements<UInteractableInterface>())
		{
			if (IInteractableInterface::Execute_CanInteract(HitActor, OwnerActor))
			{
				NewFocusedActor = HitActor;
				return true;
			}
		}
		// 3. C++ IPushableInterface 구현 액터
		else if (HitActor->Implements<UPushableInterface>())
		{
			NewFocusedActor = HitActor;
			return true;
		}
		// 4. 블루프린트 상호작용 인터페이스/함수 보유 액터 (퀘스트 NPC 등)
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

				if (!bIsBPInteractable && (ActorClass->FindFunctionByName(FName("InteractWith")) || ActorClass->FindFunctionByName(FName("Interact"))))
				{
					bIsBPInteractable = true;
				}
			}

			if (bIsBPInteractable)
			{
				NewFocusedActor = HitActor;
				return true;
			}
		}

		return false;
	};

	float MaxInteractDist = FMath::Max(InteractionDistance, 350.0f);

	FVector CamEnd = CamLoc + (CamRot.Vector() * MaxInteractDist);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerActor);

	// 1차: Visibility 채널 구체 스윕
	FHitResult HitResult;
	bool bHit = GetWorld()->SweepSingleByChannel(
		HitResult,
		CamLoc,
		CamEnd,
		FQuat::Identity,
		TraceChannel,
		FCollisionShape::MakeSphere(FMath::Max(InteractionSphereRadius, 30.0f)),
		QueryParams
	);

	if (bHit && HitResult.GetActor())
	{
		EvaluateCandidate(HitResult.GetActor());
	}

	// 2차: Visibility로 감지되지 않은 경우 (Pawn/WorldDynamic 콜리전을 사용하는 퀘스트 NPC 등)
	if (!NewFocusedActor)
	{
		FCollisionObjectQueryParams ObjectQueryParams;
		ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);
		ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);
		ObjectQueryParams.AddObjectTypesToQuery(ECC_PhysicsBody);

		TArray<FHitResult> ObjectHitResults;
		bool bObjHit = GetWorld()->SweepMultiByObjectType(
			ObjectHitResults,
			CamLoc,
			CamEnd,
			FQuat::Identity,
			ObjectQueryParams,
			FCollisionShape::MakeSphere(FMath::Max(InteractionSphereRadius, 35.0f)),
			QueryParams
		);

		if (bObjHit)
		{
			for (const FHitResult& ObjHit : ObjectHitResults)
			{
				if (ObjHit.GetActor() && EvaluateCandidate(ObjHit.GetActor()))
				{
					break;
				}
			}
		}
	}

	if (FocusedActor != NewFocusedActor)
	{
		FocusedActor = NewFocusedActor;
		OnFocusedInteractableChanged.Broadcast(FocusedActor);
	}
}
