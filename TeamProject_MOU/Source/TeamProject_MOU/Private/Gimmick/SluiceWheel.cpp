#include "Gimmick/SluiceWheel.h"
#include "Gimmick/SluiceGate.h"
#include "Player/MainCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Net/UnrealNetwork.h"

ASluiceWheel::ASluiceWheel()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	bIsPushable = true;
	RequiredPushers = 1;
	MaxPushDistance = 350.0f;
	PushDetachDistance = 200.0f;
	MinRequiredGroundPoints = 0;
	bShowDebugGroundTrace = false;
	ItemWeight = 0.0f;

	BasePlatformMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BasePlatformMesh"));
	BasePlatformMesh->SetupAttachment(RootComponent);

	RotatingWheelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RotatingWheelMesh"));
	RotatingWheelMesh->SetupAttachment(BasePlatformMesh);
}

void ASluiceWheel::BeginPlay()
{
	Super::BeginPlay();

	SetPhysicsSimulateEnabled(false);
	if (MeshComponent)
	{
		MeshComponent->SetSimulatePhysics(false);
	}

	CurrentTurnProgress = bStartFullyOpen ? 1.0f : 0.0f;
	VisualWheelYaw = CurrentTurnProgress * FullTurnsForFullOpen * 360.0f;

	if (RotatingWheelMesh)
	{
		RotatingWheelMesh->SetRelativeRotation(FRotator(0.0f, VisualWheelYaw, 0.0f));
	}

	if (TargetGate && HasAuthority())
	{
		TargetGate->SetTargetOpenRatio(CurrentTurnProgress);
	}
}

void ASluiceWheel::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASluiceWheel, CurrentTurnProgress);
}

void ASluiceWheel::Tick(float DeltaTime)
{
	if (bShowDebugPushDistance)
	{
		DrawDebugCircle(GetWorld(), GetActorLocation(), MaxPushDistance, 32, FColor::Green, false, -1.0f, 0, 2.0f, FVector(0, 1, 0), FVector(1, 0, 0), false);
	}

	if (HasAuthority() && bIsPushable && CurrentPushers.Num() > 0)
	{
		TArray<AMainCharacter*> PushersToDetach;
		for (AMainCharacter* Pusher : CurrentPushers)
		{
			if (!Pusher) continue;

			FVector LocalAnchor = PusherLocalAnchorMap.Contains(Pusher)
				? PusherLocalAnchorMap[Pusher]
				: GetActorTransform().InverseTransformPosition(Pusher->GetActorLocation());
			FVector WorldAnchor = GetActorTransform().TransformPosition(LocalAnchor);
			float Dist2D = FVector::Dist2D(Pusher->GetActorLocation(), WorldAnchor);

			if (Dist2D > PushDetachDistance)
			{
				PushersToDetach.Add(Pusher);
			}
		}

		for (AMainCharacter* Pusher : PushersToDetach)
		{
			Pusher->StopPushMode();
		}

		if (IsReadyToMove())
		{
			float TotalInput = 0.0f;
			int32 ActiveCount = 0;

			for (const TObjectPtr<AMainCharacter>& Pusher : CurrentPushers)
			{
				if (Pusher && FMath::Abs(Pusher->GetCurrentPushInput()) > 0.1f)
				{
					TotalInput += Pusher->GetCurrentPushInput();
					ActiveCount++;
				}
			}

			if (ActiveCount > 0)
			{
				float AvgInput = TotalInput / ActiveCount;
				float InputSign = FMath::Sign(AvgInput);
				float TotalMaxDegrees = FMath::Max(1.0f, FullTurnsForFullOpen * 360.0f);
				float DeltaDegrees = InputSign * TurnSpeedDegreesPerSec * DeltaTime;
				float PrevProgress = CurrentTurnProgress;
				float DeltaProgress = DeltaDegrees / TotalMaxDegrees;

				CurrentTurnProgress = FMath::Clamp(CurrentTurnProgress + DeltaProgress, 0.0f, 1.0f);
				float ActualDeltaDegrees = (CurrentTurnProgress - PrevProgress) * TotalMaxDegrees;

				if (!FMath::IsNearlyZero(ActualDeltaDegrees))
				{
					FVector WheelCenter = GetActorLocation();

					for (const TObjectPtr<AMainCharacter>& Pusher : CurrentPushers)
					{
						if (!Pusher) continue;

						FVector PusherLoc = Pusher->GetActorLocation();
						FVector RadialOffset = PusherLoc - WheelCenter;
						RadialOffset.Z = 0.0f;

						if (RadialOffset.IsNearlyZero())
						{
							RadialOffset = GetActorForwardVector() * HandleRadius;
						}
						else if (HandleRadius > 0.0f)
						{
							RadialOffset = RadialOffset.GetSafeNormal() * HandleRadius;
						}

						FVector NewRadialOffset = FRotator(0.0f, ActualDeltaDegrees, 0.0f).RotateVector(RadialOffset);
						FVector NewPusherLoc = WheelCenter + NewRadialOffset;
						NewPusherLoc.Z = PusherLoc.Z;

						Pusher->SetActorLocation(NewPusherLoc, false);
						Pusher->LastCharacterLocation = NewPusherLoc;

						FVector Tangent = FVector::CrossProduct(FVector::UpVector, NewRadialOffset.GetSafeNormal()).GetSafeNormal();
						if (InputSign < 0.0f)
						{
							Tangent = -Tangent;
						}

						Pusher->LockedPushDirection = Tangent;
						Pusher->SetActorRotation(Tangent.Rotation());

						FVector NewLocalAnchor = GetActorTransform().InverseTransformPosition(NewPusherLoc);
						PusherLocalAnchorMap.Add(Pusher, NewLocalAnchor);
						Pusher->PushLocalAnchor = NewLocalAnchor;
					}

					if (TargetGate)
					{
						TargetGate->SetTargetOpenRatio(CurrentTurnProgress);
					}

					OnWheelTurned.Broadcast(CurrentTurnProgress, ActualDeltaDegrees);
					OnWheelRotated(CurrentTurnProgress, VisualWheelYaw);
				}
			}
		}
	}

	UpdateWheelVisuals(DeltaTime);
}

void ASluiceWheel::SetTurnProgress(float NewProgress)
{
	CurrentTurnProgress = FMath::Clamp(NewProgress, 0.0f, 1.0f);

	if (!HasAuthority())
	{
		ServerSetTurnProgress(NewProgress);
	}

	if (TargetGate)
	{
		TargetGate->SetTargetOpenRatio(CurrentTurnProgress);
	}
}

void ASluiceWheel::ServerSetTurnProgress_Implementation(float NewProgress)
{
	SetTurnProgress(NewProgress);
}

void ASluiceWheel::OnRep_CurrentTurnProgress()
{
	if (TargetGate)
	{
		TargetGate->SetTargetOpenRatio(CurrentTurnProgress);
	}

	OnWheelTurned.Broadcast(CurrentTurnProgress, 0.0f);
	OnWheelRotated(CurrentTurnProgress, CurrentTurnProgress * FullTurnsForFullOpen * 360.0f);
}

void ASluiceWheel::UpdateWheelVisuals(float DeltaTime)
{
	float TargetYaw = CurrentTurnProgress * FullTurnsForFullOpen * 360.0f;
	VisualWheelYaw = (DeltaTime > 0.0f)
		? FMath::FInterpTo(VisualWheelYaw, TargetYaw, DeltaTime, 10.0f)
		: TargetYaw;

	if (RotatingWheelMesh)
	{
		RotatingWheelMesh->SetRelativeRotation(FRotator(0.0f, VisualWheelYaw, 0.0f));
	}
}

bool ASluiceWheel::CanInteract_Implementation(AActor* Interactor) const
{
	if (!bIsPushable || !Interactor)
	{
		return false;
	}

	float Dist2D = FVector::Dist2D(Interactor->GetActorLocation(), GetActorLocation());
	if (Dist2D <= MaxPushDistance)
	{
		return true;
	}

	if (RotatingWheelMesh)
	{
		float CompDist = FVector::Dist2D(Interactor->GetActorLocation(), RotatingWheelMesh->GetComponentLocation());
		if (CompDist <= MaxPushDistance)
		{
			return true;
		}
	}

	return false;
}

void ASluiceWheel::Interact_Implementation(AActor* Interactor)
{
	if (!bIsPushable || !Interactor)
	{
		return;
	}

	if (!CanInteract_Implementation(Interactor))
	{
		return;
	}

	if (AMainCharacter* MainChar = Cast<AMainCharacter>(Interactor))
	{
		MainChar->StartPushMode(this);

		FVector WheelCenter = GetActorLocation();
		FVector CharLoc = MainChar->GetActorLocation();
		FVector RadialOffset = CharLoc - WheelCenter;
		RadialOffset.Z = 0.0f;

		if (!RadialOffset.IsNearlyZero())
		{
			FVector Tangent = FVector::CrossProduct(FVector::UpVector, RadialOffset.GetSafeNormal()).GetSafeNormal();
			MainChar->LockedPushDirection = Tangent;
			MainChar->SetActorRotation(Tangent.Rotation());

			FVector NewLocalAnchor = GetActorTransform().InverseTransformPosition(CharLoc);
			PusherLocalAnchorMap.Add(MainChar, NewLocalAnchor);
			MainChar->PushLocalAnchor = NewLocalAnchor;
		}
	}
}
