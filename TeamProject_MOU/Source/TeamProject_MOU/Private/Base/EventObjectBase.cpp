#include "Base/EventObjectBase.h"
#include "Player/MainCharacter.h"
#include "Base/BaseAttributeSet.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"

AEventObjectBase::AEventObjectBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// 보통 이벤트 오브젝트는 무거운 느낌으로 설정
	ItemWeight = 50.0f;
	PhysicalMassInKg = 1000.0f;
	MinRequiredGroundPoints = 3; // 9개 포인트 중 최소 3개 이상 지지되어야 밀기 유지
	GroundTraceTolerance = 50.0f; // 실제 지지 접촉면만 감지하기 위한 하방 거리
	bCanBeStoredInInventory = false;

	if (MeshComponent)
	{
		MeshComponent->SetLinearDamping(1.0f);
		MeshComponent->SetAngularDamping(1.5f);
	}

	// 지면/터널관 감지 전용 박스 컴포넌트 생성 (물리 충돌 없음, 뷰포트에서 기즈모로 조절 가능)
	GroundDetectorBox = CreateDefaultSubobject<UBoxComponent>(TEXT("GroundDetectorBox"));
	GroundDetectorBox->SetupAttachment(RootComponent);
	GroundDetectorBox->SetCollisionProfileName(TEXT("NoCollision"));
	GroundDetectorBox->SetBoxExtent(FVector(150.0f, 150.0f, 10.0f));
	GroundDetectorBox->bHiddenInGame = false;
	GroundDetectorBox->SetLineThickness(2.0f);
}

void AEventObjectBase::BeginPlay()
{
	Super::BeginPlay();

	// 평상시(대기 상태)에는 순수 언리얼 물리 엔진에 위임
	// 메쉬 형상에 따라 경사로에서 자연스럽게 안착/미끄러짐/굴러감
	if (bIsPushable)
	{
		SetPhysicsSimulateEnabled(true);
	}
}

void AEventObjectBase::SetPhysicsSimulateEnabled(bool bEnablePhysics)
{
	if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(GetRootComponent()))
	{
		// 키네마틱 전환 전/물리 활성화 전 잔여 물리 속도/각속도 소멸
		PrimComp->SetPhysicsLinearVelocity(FVector::ZeroVector);
		PrimComp->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		PrimComp->PutRigidBodyToSleep();

		PrimComp->SetSimulatePhysics(bEnablePhysics);

		if (bEnablePhysics)
		{
			// 물리 활성화 직후에도 잔여 임펄스 및 반발 속도 완전 초기화
			PrimComp->SetPhysicsLinearVelocity(FVector::ZeroVector);
			PrimComp->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);

			if (PhysicalMassInKg > 0.0f)
			{
				PrimComp->SetMassOverrideInKg(NAME_None, PhysicalMassInKg, true);
			}
			PrimComp->WakeRigidBody();
		}
	}
}

bool AEventObjectBase::CheckGroundSupport(FHitResult& OutFloorHit, int32& OutSupportedCount, float& OutSupportZ) const
{
	OutSupportedCount = 0;

	FVector Center = GetActorLocation();
	FVector ScaledExtent = FVector(150.0f, 150.0f, 10.0f);
	FQuat Rot = GetActorQuat();

	if (GroundDetectorBox)
	{
		Center = GroundDetectorBox->GetComponentLocation();
		ScaledExtent = GroundDetectorBox->GetScaledBoxExtent();
		Rot = GroundDetectorBox->GetComponentQuat();
	}

	FVector Forward = Rot.GetForwardVector();
	FVector Right = Rot.GetRightVector();
	FVector Up = Rot.GetUpVector();

	FVector BottomCenter = Center - (Up * ScaledExtent.Z);
	OutSupportZ = BottomCenter.Z;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	for (const TObjectPtr<AMainCharacter>& Pusher : CurrentPushers)
	{
		if (Pusher) Params.AddIgnoredActor(Pusher);
	}

	// [9-포인트 3x3 그리드 구성: 정중앙 1개, 4개 코너 모서리, 4개 변 중앙]
	TArray<FVector> SamplePoints;
	SamplePoints.Add(BottomCenter); // 1. 정중앙 하단

	// 4개 코너 모서리 (Corner Points)
	SamplePoints.Add(BottomCenter + Forward * ScaledExtent.X + Right * ScaledExtent.Y); // 2. 전방 우측 모서리
	SamplePoints.Add(BottomCenter + Forward * ScaledExtent.X - Right * ScaledExtent.Y); // 3. 전방 좌측 모서리
	SamplePoints.Add(BottomCenter - Forward * ScaledExtent.X + Right * ScaledExtent.Y); // 4. 후방 우측 모서리
	SamplePoints.Add(BottomCenter - Forward * ScaledExtent.X - Right * ScaledExtent.Y); // 5. 후방 좌측 모서리

	// 4개 변의 가운데 (Edge Midpoints)
	SamplePoints.Add(BottomCenter + Forward * ScaledExtent.X); // 6. 전방 중앙
	SamplePoints.Add(BottomCenter - Forward * ScaledExtent.X); // 7. 후방 중앙
	SamplePoints.Add(BottomCenter + Right * ScaledExtent.Y);   // 8. 우측 중앙
	SamplePoints.Add(BottomCenter - Right * ScaledExtent.Y);   // 9. 좌측 중앙

	FVector SumNormal = FVector::ZeroVector;
	float MaxHitZ = -FLT_MAX;
	FHitResult FirstHit;
	bool bFoundHit = false;

	const float UpOffset = 25.0f;

	for (const FVector& Pt : SamplePoints)
	{
		FVector TraceStart = Pt + (Up * UpOffset);
		FVector TraceEnd = Pt - FVector(0, 0, GroundTraceTolerance);
		
		FHitResult Hit;
		bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params);

		if (bHit)
		{
			OutSupportedCount++;
			SumNormal += Hit.ImpactNormal;
			MaxHitZ = FMath::Max(MaxHitZ, Hit.ImpactPoint.Z);

			if (!bFoundHit)
			{
				FirstHit = Hit;
				bFoundHit = true;
			}

			if (bShowDebugGroundTrace)
			{
				DrawDebugLine(GetWorld(), TraceStart, Hit.ImpactPoint, FColor::Green, false, -1.0f, 0, 2.0f);
				DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 10.0f, FColor::Green, false, -1.0f);
			}
		}
		else
		{
			if (bShowDebugGroundTrace)
			{
				DrawDebugLine(GetWorld(), TraceStart, TraceEnd, FColor::Red, false, -1.0f, 0, 1.5f);
				DrawDebugPoint(GetWorld(), TraceEnd, 8.0f, FColor::Red, false, -1.0f);
			}
		}
	}

	if (bFoundHit && OutSupportedCount > 0)
	{
		OutFloorHit = FirstHit;
		OutFloorHit.ImpactNormal = (SumNormal / OutSupportedCount).GetSafeNormal();
		if (OutFloorHit.ImpactNormal.IsNearlyZero())
		{
			OutFloorHit.ImpactNormal = FVector::UpVector;
		}
		OutSupportZ = MaxHitZ;
	}
	else
	{
		OutFloorHit.ImpactNormal = FVector::UpVector;
		OutSupportZ = BottomCenter.Z;
	}

	if (bShowDebugGroundTrace)
	{
		DrawDebugBox(GetWorld(), Center, ScaledExtent, Rot, FColor::Cyan, false, -1.0f, 0, 1.5f);

		bool bPassed = OutSupportedCount >= MinRequiredGroundPoints;
		FString StatusText = FString::Printf(TEXT("Ground: %d/%d (Min:%d) - %s"), 
			OutSupportedCount, SamplePoints.Num(), MinRequiredGroundPoints, bPassed ? TEXT("SUPPORTED") : TEXT("FALLING"));
		DrawDebugString(GetWorld(), Center + FVector(0, 0, ScaledExtent.Z + 30.0f), StatusText, nullptr, 
			bPassed ? FColor::Green : FColor::Red, 0.0f, true, 1.2f);
	}

	return OutSupportedCount >= MinRequiredGroundPoints;
}

void AEventObjectBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AEventObjectBase, CurrentPushers);
}

void AEventObjectBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bShowDebugGroundTrace && CurrentPushers.Num() == 0)
	{
		FHitResult IdleFloorHit;
		int32 IdleSupportedCount = 0;
		float IdleSupportZ = 0.0f;
		CheckGroundSupport(IdleFloorHit, IdleSupportedCount, IdleSupportZ);
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
				UE_LOG(LogTemp, Warning, TEXT("[%s] 푸셔(%s)가 손 앵커에서 %fcm 멀어져(허용 %fcm) 밀기 모드가 자동 해제됩니다!"),
					*GetName(), *Pusher->GetName(), Dist2D, PushDetachDistance);
			}
		}

		for (AMainCharacter* Pusher : PushersToDetach)
		{
			Pusher->StopPushMode();
		}

		if (IsReadyToMove())
		{
			FHitResult FloorHit;
			int32 SupportedPoints = 0;
			float SupportZ = 0.0f;
			bool bIsSupported = CheckGroundSupport(FloorHit, SupportedPoints, SupportZ);

			if (!bIsSupported)
			{
				MulticastFallOffLedge();
				return;
			}

			FVector TotalPushDir = FVector::ZeroVector;
			float TotalPushSpeed = 0.0f;
			int32 ActiveCount = 0;

			for (const TObjectPtr<AMainCharacter>& Pusher : CurrentPushers)
			{
				if (Pusher && FMath::Abs(Pusher->GetCurrentPushInput()) > 0.1f)
				{
					float Input = Pusher->GetCurrentPushInput();
					float PushInputSign = FMath::Sign(Input);
					TotalPushDir += Pusher->LockedPushDirection * PushInputSign;

					float Speed = Pusher->GetCharacterMovement() ? Pusher->GetCharacterMovement()->MaxWalkSpeed : 300.0f;
					TotalPushSpeed += Speed;
					ActiveCount++;
				}
			}

			if (ActiveCount > 0)
			{
				FVector PushDir = TotalPushDir / ActiveCount;
				PushDir.Z = 0.0f;
				PushDir.Normalize();

				FVector SlopeMoveDir = FVector::VectorPlaneProject(PushDir, FloorHit.ImpactNormal).GetSafeNormal();
				if (SlopeMoveDir.IsNearlyZero())
				{
					SlopeMoveDir = PushDir;
				}

				float AvgSpeed = TotalPushSpeed / ActiveCount;
				FVector DeltaMove = SlopeMoveDir * (AvgSpeed * DeltaTime);

				if (!DeltaMove.IsNearlyZero())
				{
					FVector DetectorCenter = GroundDetectorBox ? GroundDetectorBox->GetComponentLocation() : GetActorLocation();
					FVector DetectorExtents = GroundDetectorBox ? GroundDetectorBox->GetScaledBoxExtent() : FVector(150.0f, 150.0f, 20.0f);

					FVector TraceStart = DetectorCenter + FVector(0, 0, DetectorExtents.Z + 20.0f);
					FVector TraceEnd = TraceStart + DeltaMove;

					FCollisionQueryParams BoxParams;
					BoxParams.AddIgnoredActor(this);
					for (const TObjectPtr<AMainCharacter>& Pusher : CurrentPushers)
					{
						if (Pusher) BoxParams.AddIgnoredActor(Pusher);
					}

					FHitResult BoxHit;
					bool bHitObstacle = GetWorld()->SweepSingleByChannel(
						BoxHit, TraceStart, TraceEnd,
						GetActorQuat(), ECC_Visibility,
						FCollisionShape::MakeBox(FVector(DetectorExtents.X * 0.85f, DetectorExtents.Y * 0.85f, DetectorExtents.Z * 0.8f)), BoxParams);

					if (!bHitObstacle || FMath::Abs(BoxHit.ImpactNormal.Z) >= 0.5f)
					{
						FVector NewLocation = GetActorLocation() + DeltaMove;

						if (GroundDetectorBox)
						{
							float CurrentBottomZ = GroundDetectorBox->GetComponentLocation().Z - GroundDetectorBox->GetScaledBoxExtent().Z;
							float TargetBottomZ = SupportZ;
							float DiffZ = TargetBottomZ - CurrentBottomZ;

							if (DiffZ > 0.0f)
							{
								NewLocation.Z += DiffZ;
							}
						}

						SetActorLocation(NewLocation, false);
					}
				}
			}
		}
	}




	// 디버그 라인 그리기 (GroundDetectorBox 중심 기준)
	if (bShowDebugPushDistance)
	{
		FVector BoxCenter = GroundDetectorBox ? GroundDetectorBox->GetComponentLocation() : GetActorLocation();
		DrawDebugCircle(GetWorld(), BoxCenter, MaxPushDistance, 32, FColor::Green, false, -1.0f, 0, 2.0f, FVector(0, 1, 0), FVector(1, 0, 0), false);
	}
}


float AEventObjectBase::GetPushResistance_Implementation() const
{
	return ItemWeight;
}

void AEventObjectBase::Push_Implementation(AActor* Pusher, FVector PushDirection)
{
}

bool AEventObjectBase::CanInteract_Implementation(AActor* Interactor) const
{
	if (!bIsPushable || !Interactor)
	{
		return false;
	}

	FVector ClosestPoint;
	if (GroundDetectorBox)
	{
		GroundDetectorBox->GetClosestPointOnCollision(Interactor->GetActorLocation(), ClosestPoint);
	}
	else
	{
		ClosestPoint = GetActorLocation();
	}
	float Dist2D = FVector::Dist2D(Interactor->GetActorLocation(), ClosestPoint);
	return Dist2D <= MaxPushDistance;
}

void AEventObjectBase::Interact_Implementation(AActor* Interactor)
{
	if (!Interactor) return;

	if (!bIsPushable)
	{
		return;
	}

	FVector ClosestPoint;
	if (GroundDetectorBox)
	{
		GroundDetectorBox->GetClosestPointOnCollision(Interactor->GetActorLocation(), ClosestPoint);
	}
	else
	{
		ClosestPoint = GetActorLocation();
	}
	float Dist2D = FVector::Dist2D(Interactor->GetActorLocation(), ClosestPoint);
	if (Dist2D > MaxPushDistance)
	{
		return;
	}

	if (AMainCharacter* MainChar = Cast<AMainCharacter>(Interactor))
	{
		MainChar->StartPushMode(this);
	}
}

void AEventObjectBase::UpdatePushersWeight()

{
	if (ItemWeight <= 0.0f || !HasAuthority())
	{
		return;
	}

	float WeightPerPusher = CurrentPushers.Num() > 0 ? (ItemWeight / CurrentPushers.Num()) : 0.0f;

	for (AMainCharacter* Pusher : CurrentPushers)
	{
		if (Pusher && Pusher->BaseAttribute)
		{
			UBaseAttributeSet* AttrSet = Pusher->BaseAttribute;

			// 기존에 분배된 무게가 있다면 일단 차감 (재분배를 위함)
			if (AppliedWeightMap.Contains(Pusher))
			{
				AttrSet->SetCurrentWeight(FMath::Max(0.0f, AttrSet->GetCurrentWeight() - AppliedWeightMap[Pusher]));
			}

			// 새로운 N분의 1 무게 부여
			AttrSet->SetCurrentWeight(AttrSet->GetCurrentWeight() + WeightPerPusher);
			AppliedWeightMap.Add(Pusher, WeightPerPusher);

			// 캐릭터 이동속도 및 과적 상태 즉시 갱신
			Pusher->UpdateCharacterSpeed();
		}
	}
}

void AEventObjectBase::AddPusher(AMainCharacter* Pusher)
{
	if (HasAuthority() && Pusher && !CurrentPushers.Contains(Pusher))
	{
		// 첫 번째 푸셔가 진입할 때 물리 시뮬레이션을 끄고 키네마틱(스크립트 이동) 모드로 전환
		if (CurrentPushers.Num() == 0)
		{
			SetPhysicsSimulateEnabled(false);
		}

		CurrentPushers.Add(Pusher);
		PusherLocalAnchorMap.Add(Pusher, GetActorTransform().InverseTransformPosition(Pusher->GetActorLocation()));
		UpdatePushersWeight();
	}
}

void AEventObjectBase::RemovePusher(AMainCharacter* Pusher)
{
	if (HasAuthority() && Pusher && CurrentPushers.Contains(Pusher))
	{
		if (AppliedWeightMap.Contains(Pusher))
		{
			if (Pusher->BaseAttribute)
			{
				Pusher->BaseAttribute->SetCurrentWeight(FMath::Max(0.0f, Pusher->BaseAttribute->GetCurrentWeight() - AppliedWeightMap[Pusher]));
				Pusher->UpdateCharacterSpeed();
			}
			AppliedWeightMap.Remove(Pusher);
		}

		PusherLocalAnchorMap.Remove(Pusher);
		CurrentPushers.Remove(Pusher);
		UpdatePushersWeight();

		// 모든 푸셔가 이탈했을 때 다시 엔진 순수 물리 시뮬레이션(중력) 모드로 복귀
		if (CurrentPushers.Num() == 0)
		{
			SetPhysicsSimulateEnabled(true);
		}
	}
}

bool AEventObjectBase::IsReadyToMove() const
{
	if (CurrentPushers.Num() < RequiredPushers)
	{
		return false;
	}

	// [대향/마주보고 밀기 충돌 검사] 두 명 이상의 활성 푸셔가 서로 마주보고 밀 경우 힘겨루기 상태로 이동 불가 (정지)
	if (CurrentPushers.Num() >= 2)
	{
		for (int32 i = 0; i < CurrentPushers.Num(); ++i)
		{
			AMainCharacter* PusherA = CurrentPushers[i];
			if (!PusherA || FMath::Abs(PusherA->GetCurrentPushInput()) < 0.1f) continue;

			FVector DirA = PusherA->LockedPushDirection * FMath::Sign(PusherA->GetCurrentPushInput());
			DirA.Z = 0.0f;
			DirA.Normalize();

			for (int32 j = i + 1; j < CurrentPushers.Num(); ++j)
			{
				AMainCharacter* PusherB = CurrentPushers[j];
				if (!PusherB || FMath::Abs(PusherB->GetCurrentPushInput()) < 0.1f) continue;

				FVector DirB = PusherB->LockedPushDirection * FMath::Sign(PusherB->GetCurrentPushInput());
				DirB.Z = 0.0f;
				DirB.Normalize();

				// 두 힘의 방향이 120도 이상 마주보거나 상반되면(Dot < -0.5f) 힘겨루기로 정지
				if (FVector::DotProduct(DirA, DirB) < -0.5f)
				{
					return false;
				}
			}
		}
	}

	// 1인 상자: 적어도 1명 이상의 푸셔가 W(+1.0) 또는 S(-1.0) 입력을 주고 있으면 이동 가능
	if (RequiredPushers <= 1)
	{
		for (const TObjectPtr<AMainCharacter>& Pusher : CurrentPushers)
		{
			if (Pusher && FMath::Abs(Pusher->GetCurrentPushInput()) > 0.1f)
			{
				return true;
			}
		}
		return false;
	}

	// 2인 이상 필수 협동 상자: 최소 RequiredPushers 수만큼의 인원이 모두 동일한 방향으로 입력을 줘야 이동 가능
	float CommonSign = 0.0f;
	int32 ActivePusherCount = 0;

	for (const TObjectPtr<AMainCharacter>& Pusher : CurrentPushers)
	{
		if (Pusher)
		{
			float Input = Pusher->GetCurrentPushInput();
			if (FMath::Abs(Input) > 0.1f)
			{
				float Sign = FMath::Sign(Input);
				if (CommonSign == 0.0f)
				{
					CommonSign = Sign;
				}
				else if (CommonSign != Sign)
				{
					// 서로 반대 방향으로 밀고 당김 (충돌 -> 이동 불가)
					return false;
				}
				ActivePusherCount++;
			}
		}
	}

	return ActivePusherCount >= RequiredPushers;
}

void AEventObjectBase::MulticastFallOffLedge_Implementation()
{
	FallOffLedge();
}

void AEventObjectBase::FallOffLedge()
{
	// 상자를 밀던 모든 플레이어의 무게 회수 및 밀기 모드 강제 해제
	TArray<TObjectPtr<AMainCharacter>> PushersCopy = CurrentPushers;
	for (AMainCharacter* Pusher : PushersCopy)
	{
		if (Pusher)
		{
			if (AppliedWeightMap.Contains(Pusher) && Pusher->BaseAttribute)
			{
				Pusher->BaseAttribute->SetCurrentWeight(FMath::Max(0.0f, Pusher->BaseAttribute->GetCurrentWeight() - AppliedWeightMap[Pusher]));
				Pusher->UpdateCharacterSpeed();
			}
			Pusher->StopPushMode();
		}
	}
	CurrentPushers.Empty();
	AppliedWeightMap.Empty();
	PusherLocalAnchorMap.Empty();

	// 물리 시뮬레이션을 다시 켜서 바닥/낭떠러지로 자연스럽게 추락/낙하/미끄러지도록 처리
	SetPhysicsSimulateEnabled(true);
}

