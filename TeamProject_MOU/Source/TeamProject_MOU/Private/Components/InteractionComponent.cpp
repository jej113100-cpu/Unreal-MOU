#include "Components/InteractionComponent.h"
#include "Interfaces/InteractableInterface.h"
#include "Interfaces/PushableInterface.h"
#include "Base/EventObjectBase.h"
#include "Item/ItemDrone.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Player/MainCharacter.h"
#include "Kismet/KismetSystemLibrary.h"
#include "DrawDebugHelpers.h"

UInteractionComponent::UInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.05f; // 20Hz (초당 20회 검사 - 연산량 대폭 절감)
}

void UInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	// 비-로컬 플레이어/서버인 경우 불필요한 Tick을 비활성화하여 Tick Dispatch 비용 제거
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (PawnOwner && !PawnOwner->IsLocallyControlled())
	{
		SetComponentTickEnabled(false);
	}
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

		// 배치된 아이템 드론의 맡기기/회수는 서버에서 상태가 바뀌어야 한다.
		// 로컬 클라에서는 위 Interact가 아무 상태도 바꾸지 않으므로(서버 권위), 서버에서 재실행하도록 위임한다.
		// (드론 자신의 Server RPC는 배치자 소유에 묶여 다른 클라가 못 쓰지만, 이 컴포넌트는 상호작용하는
		//  플레이어 소유라 누가 배치했든 서버에 도달한다)
		if (AActor* OwnerActorPtr = GetOwner())
		{
			if (!OwnerActorPtr->HasAuthority())
			{
				if (const AItemDrone* Drone = Cast<AItemDrone>(FocusedActor))
				{
					if (Drone->bIsDeployed)
					{
						ServerRunInteract(FocusedActor);
					}
				}
			}
		}

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

void UInteractionComponent::ServerRunInteract_Implementation(AActor* TargetActor)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !TargetActor)
	{
		return;
	}

	// 서버에서 다시 CanInteract로 유효성 확인 후 Interact 실행 (클라 값 신뢰하지 않음).
	if (TargetActor->Implements<UInteractableInterface>())
	{
		if (IInteractableInterface::Execute_CanInteract(TargetActor, OwnerActor))
		{
			IInteractableInterface::Execute_Interact(TargetActor, OwnerActor);
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
				if (const bool* Cached = BPInteractableClassCache.Find(ActorClass))
				{
					bIsBPInteractable = *Cached;
				}
				else
				{
					static const FName NAME_InteractWith(TEXT("InteractWith"));
					static const FName NAME_Interact(TEXT("Interact"));

					for (const FImplementedInterface& Interface : ActorClass->Interfaces)
					{
						if (Interface.Class && Interface.Class->GetName().Contains(TEXT("Interaction")))
						{
							bIsBPInteractable = true;
							break;
						}
					}

					if (!bIsBPInteractable && (ActorClass->FindFunctionByName(NAME_InteractWith) || ActorClass->FindFunctionByName(NAME_Interact)))
					{
						bIsBPInteractable = true;
					}

					BPInteractableClassCache.Add(ActorClass, bIsBPInteractable);
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
