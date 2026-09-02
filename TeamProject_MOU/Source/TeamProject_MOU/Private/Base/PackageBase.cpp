#include "Base/PackageBase.h"
#include "Base/CharacterBase.h"
#include "Base/BaseAttributeSet.h"
#include "Item/PackageItemSaveData.h"
#include "Interfaces/QuestItemSaveInterface.h"
#include "AbilitySystemComponent.h"
#include "Components/CarryingComponent.h"
#include "Components/CharacterVisualComponent.h"
#include "Data/CharacterVisualDataAsset.h"
#include "Components/CapsuleComponent.h"
#include "Player/MainCharacter.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Ability/GA_CoopCarry.h"

APackageBase::APackageBase()
{
	PrimaryActorTick.bCanEverTick = true;
	bCanBeStoredInInventory = false;

	// 운반을 위한 전방/후방 손잡이 생성 (일반/무거운 물품 공통 기반)
	Handle_Front = CreateDefaultSubobject<USceneComponent>(TEXT("Handle_Front"));
	Handle_Front->SetupAttachment(RootComponent);

	Handle_Back = CreateDefaultSubobject<USceneComponent>(TEXT("Handle_Back"));
	Handle_Back->SetupAttachment(RootComponent);

	MaxCarryDistance = 80.0f;
}

void APackageBase::BeginPlay()
{
	Super::BeginPlay();
	
	CurrentSpoilTime = MaxSpoilTime;
}

bool APackageBase::CanBePickedUpBy(AActor* PotentialPicker) const
{
	if (!PotentialPicker)
	{
		return false;
	}

	// 이미 본인이 들고 있는 경우 중복 줍기 불가
	if (CurrentCarriers.Contains(PotentialPicker))
	{
		return false;
	}

	// 무거운 택배(Heavy)의 경우 최대 2명까지 허용
	if (PackageType == EPackageType::Heavy)
	{
		return CurrentCarriers.Num() < 2;
	}

	// 일반 물품은 다른 사람이 1명이라도 들고 있으면 줍기 불가 (가로채기 차단)
	return CurrentCarriers.Num() == 0 && GetAttachParentActor() == nullptr;
}

void APackageBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// 메시에 물리 충돌 이벤트 바인딩
	if (MeshComponent)
	{
		MeshComponent->SetNotifyRigidBodyCollision(true); // Hit 이벤트 발생 허용
		MeshComponent->OnComponentHit.AddDynamic(this, &APackageBase::OnPackageHit);
	}
}

void APackageBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 상하기 쉬운 물품의 경우 유통기한 감소
	if (PackageType == EPackageType::Perishable && CurrentSpoilTime > 0.0f)
	{
		CurrentSpoilTime -= DeltaTime;
		if (CurrentSpoilTime <= 0.0f)
		{
			// 시간 만료: 가치는 GetCurrentValue()에서 동적으로 0 처리되므로
			// 여기서 BaseValue를 강제로 깎을 필요 없음. (단, UI 갱신을 위해 파손 연출이나 OnRep 활용 가능)
		}
	}

	// 서버에서만 운반 로직 수행
	if (HasAuthority())
	{
		// [전환 유예 타이머] 매 프레임 감산
		if (TransitionGracePeriod > 0.0f)
		{
			TransitionGracePeriod = FMath::Max(0.0f, TransitionGracePeriod - DeltaTime);
		}

		// [Heavy 전용] 운반 중 패키지 위치/회전 업데이트
		if (PackageType == EPackageType::Heavy && CurrentCarriers.Num() > 0)
		{
			UpdateHeavyPackagePosition(DeltaTime);
		}

		// [각자 잡고 있는 손잡이 기준 80cm 이탈 검사] 전환 유예가 끝난 상태에서 운반자가 본인 손잡이에서 80cm 이상 멀어지면 놓침
		if (TransitionGracePeriod <= 0.0f)
		{
			if (PackageType == EPackageType::Heavy && CurrentCarriers.Num() >= 2)
			{
				TArray<AActor*> CarriersToDrop;

				for (int32 i = 0; i < CurrentCarriers.Num(); ++i)
				{
					AActor* Carrier = CurrentCarriers[i];
					if (!Carrier) continue;

					USceneComponent* HandleComp = (i == 0) ? Handle_Front.Get() : Handle_Back.Get();
					FVector TargetHandlePos = HandleComp ? HandleComp->GetComponentLocation() : GetActorLocation();

					// 운반자의 손 위치 (신체 중심 + 앞쪽 30cm)
					FVector CarrierHandPos = Carrier->GetActorLocation() + Carrier->GetActorForwardVector() * 30.0f;

					// 2D 수평 거리(XY)로 손과 손잡이 사이의 거리 측정 (Z축 높이 차이로 인한 즉시 풀림 방지)
					float DistFromHandle = FVector::DistXY(CarrierHandPos, TargetHandlePos);

					const float AllowedTolerance = FMath::Min(MaxCarryDistance, 80.0f);
					if (DistFromHandle > AllowedTolerance)
					{
						CarriersToDrop.Add(Carrier);
						UE_LOG(LogTemp, Warning, TEXT("[%s] 운반자 %d(%s)가 손잡이에서 %fcm 멀어져(허용 %fcm) 택배를 놓칩니다!"), 
							*GetName(), i, *Carrier->GetName(), DistFromHandle, AllowedTolerance);
					}
				}

				for (AActor* Carrier : CarriersToDrop)
				{
					if (UCarryingComponent* CarryComp = Carrier->FindComponentByClass<UCarryingComponent>())
					{
						CarryComp->GrabOrDrop();
					}
					else
					{
						RemoveCarrier(Carrier);
					}
				}
			}
			else if (CurrentCarriers.Num() == 1)
			{
				// 1인 드래그 시: 박스와 캐릭터의 수평 거리가 200cm를 초과할 때만 드랍
				AActor* Carrier = CurrentCarriers[0];
				if (Carrier)
				{
					if (FVector::DistXY(Carrier->GetActorLocation(), GetActorLocation()) > 200.0f)
					{
						if (UCarryingComponent* CarryComp = Carrier->FindComponentByClass<UCarryingComponent>())
						{
							CarryComp->GrabOrDrop();
						}
						else
						{
							RemoveCarrier(Carrier);
						}
					}
				}
			}
		}

		// [내구도 시스템] 끌림 상태 지속 대미지
		if (!bIsBroken && CurrentCarriers.Num() > 0 && CurrentSpeedRatio < 0.5f)
		{
			DamagePackage(5.0f * DeltaTime);
		}
	}
}

void APackageBase::UpdateHeavyPackagePosition(float DeltaTime)
{
	if (CurrentCarriers.Num() == 0) return;

	AActor* FrontCarrier = CurrentCarriers[0];
	if (!FrontCarrier) return;

	ACharacter* FrontChar = Cast<ACharacter>(FrontCarrier);
	if (!FrontChar || !FrontChar->GetMesh()) return;

	// 소켓이 없을 경우 오른손 본(hand_r)을 직접 사용
	FVector FrontHandPos;
	if (FrontChar->GetMesh()->DoesSocketExist(CarrySocketName))
	{
		FrontHandPos = FrontChar->GetMesh()->GetSocketLocation(CarrySocketName);
	}
	else
	{
		// 소켓이 없으면 캐릭터 위치에서 앞쪽 60cm를 기본값으로 사용
		FrontHandPos = FrontCarrier->GetActorLocation()
			+ FrontCarrier->GetActorForwardVector() * 60.0f
			+ FVector(0, 0, 30.0f);
	}

	// Handle_Front의 로컬 오프셋 (이 값만큼 패키지 원점을 역방향으로 밀어야 함)
	FVector HFLocal = Handle_Front ? Handle_Front->GetRelativeLocation() : FVector::ZeroVector;

	if (CurrentCarriers.Num() == 1)
	{
		// [1인 드래그 운반] DragSocket(등/어깨) 기준으로 패키지 부착
		FVector DragPos;
		if (FrontChar->GetMesh()->DoesSocketExist(DragSocketName))
		{
			DragPos = FrontChar->GetMesh()->GetSocketLocation(DragSocketName);
		}
		else
		{
			// DragSocket 없으면 캐릭터 뒤쪽 어깨 높이 위치로 폴백
			DragPos = FrontCarrier->GetActorLocation()
				- FrontCarrier->GetActorForwardVector() * 30.0f  // 뒤쪽
				+ FVector(0, 0, 80.0f);  // 어깨 높이
		}

		// [오프셋 제거] 아까 추가했던 좌우 보정(SideOffset)이 오히려 중앙 정렬을 방해했으므로 완전히 제거합니다.
		// DragPos는 캐릭터의 소켓 위치를 그대로 사용합니다.

		// 1인 드래그 회전: 
		// 1. 캐릭터 등 뒤(+180) 및 지면 경사(-20 Pitch)로 기본 드래그 쿼터니언 계산 (수평 Roll 0 고정)
		FRotator WorldDragRot = FrontCarrier->GetActorRotation();
		WorldDragRot.Yaw += 180.0f;
		WorldDragRot.Pitch = -20.0f;
		WorldDragRot.Roll = 0.0f;

		// 2. 메쉬 자체의 로컬 회전 오프셋(SingleCarryRotationOffset, 예: Yaw -90)을 먼저 적용한 후 월드 드래그 방향으로 눕힘
		// 이렇게 쿼터니언으로 합성해야 Yaw를 돌렸을 때 Pitch(기울기)가 Roll(옆으로 눕기)로 왜곡되지 않고 정상적으로 바닥으로 처집니다!
		FQuat FinalDragQuat = WorldDragRot.Quaternion() * SingleCarryRotationOffset.Quaternion();
		FRotator DragRot = FinalDragQuat.Rotator();

		// DragSocket 위치에서 Handle_Front 오프셋만큼 역방향으로 밀어 패키지 원점 계산
		FVector RotatedHF = FinalDragQuat.RotateVector(HFLocal);
		FVector NewPackageLoc = DragPos - RotatedHF;

		SetActorLocationAndRotation(NewPackageLoc, DragRot, false, nullptr, ETeleportType::None);
	}
	else if (CurrentCarriers.Num() >= 2)
	{
		// [2인 운반] 두 운반자의 위치를 기반으로 안정적으로 박스 위치/회전 계산 (떨림 방지)
		AActor* BackCarrier = CurrentCarriers[1];
		if (!BackCarrier) return;

		ACharacter* BackChar = Cast<ACharacter>(BackCarrier);
		if (!BackChar) return;

		// 손 소켓의 애니메이션 미세 진동으로 인한 흔들림을 방지하기 위해 신체 기준 안정적 높이 위치 사용
		FVector FrontPos = FrontCarrier->GetActorLocation() + FrontCarrier->GetActorForwardVector() * 30.0f + FVector(0, 0, 30.0f);
		FVector BackPos = BackCarrier->GetActorLocation() + BackCarrier->GetActorForwardVector() * 30.0f + FVector(0, 0, 30.0f);

		// 패키지 방향: 앞사람 → 뒷사람 방향
		FVector Dir = BackPos - FrontPos;
		if (Dir.IsNearlyZero()) return;

		// 핸들 간의 로컬 방향 벡터 (Front -> Back)
		FVector HBLocal = Handle_Back ? Handle_Back->GetRelativeLocation() : FVector(-100, 0, 0);
		FVector LocalDir = HBLocal - HFLocal;
		if (LocalDir.IsNearlyZero()) LocalDir = FVector(-1, 0, 0); // 폴백

		FQuat TargetQuat = FQuat::FindBetweenVectors(LocalDir, Dir);
		FRotator TargetRot = TargetQuat.Rotator();

		// Handle_Front 위치 기준으로 패키지 원점 계산
		FVector RotatedHF = TargetQuat.RotateVector(HFLocal);
		FVector TargetOrigin = FrontPos - RotatedHF;

		// 부드러운 위치/회전 보간 적용 (옆으로 이동 시 떨림 현상 완전 방지)
		if (DeltaTime > 0.0f)
		{
			FVector SmoothLoc = FMath::VInterpTo(GetActorLocation(), TargetOrigin, DeltaTime, 30.0f);
			FRotator SmoothRot = FMath::RInterpTo(GetActorRotation(), TargetRot, DeltaTime, 30.0f);
			SetActorLocationAndRotation(SmoothLoc, SmoothRot, false, nullptr, ETeleportType::None);
		}
		else
		{
			SetActorLocationAndRotation(TargetOrigin, TargetRot, false, nullptr, ETeleportType::None);
		}
	}
}

void APackageBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APackageBase, CurrentCarriers);
	DOREPLIFETIME(APackageBase, bIsBroken);  // 파손 상태 복제: 클라이언트 접속 시에도 동기화
}

void APackageBase::OnRep_CurrentCarriers()
{
	// 클라이언트에서 운반자 목록이 변경될 때 수행할 로직 (필요시 추가)
}

void APackageBase::OnRep_bIsBroken()
{
	// 서버에서 bIsBroken이 true로 바뀌면 클라이언트에서 이 함수가 자동 호출됨
	if (bIsBroken)
	{
		// 블루프린트에서 구현한 파손 메시 교체 / 파티클 등 연출 실행
		OnPackageBroken();
	}
}

void APackageBase::OnUse_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[%s] 택배는 사용할 수 없습니다! (옮기기만 가능)"), *GetName());
}

bool APackageBase::CanInteract_Implementation(AActor* Interactor) const
{
	// 이미 최대 인원(2명)이 운반 중인 무거운 택배가 아니면 상호작용/에임 정보 표시 허용
	if (PackageType == EPackageType::Heavy && CurrentCarriers.Num() >= 2)
	{
		return false;
	}
	return true;
}

void APackageBase::SaveItemToData_Implementation(FStoredItemInstanceData& OutData) const
{
	Super::SaveItemToData_Implementation(OutData);

	FPackageItemSaveData PackageSaveData;
	PackageSaveData.BaseValue = BaseValue;
	PackageSaveData.PackageType = PackageType;
	PackageSaveData.MaxSpoilTime = MaxSpoilTime;
	PackageSaveData.CurrentSpoilTime = CurrentSpoilTime;
	PackageSaveData.bIsBroken = bIsBroken;

	OutData.ExtraSaveData.Add(FInstancedStruct::Make(PackageSaveData));

	if (GetClass()->ImplementsInterface(UQuestItemSaveInterface::StaticClass()))
	{
		const FQuestItemSaveData QuestSaveData = IQuestItemSaveInterface::Execute_GetQuestItemSaveData(this);
		if (QuestSaveData.IsValid())
		{
			OutData.ExtraSaveData.Add(FInstancedStruct::Make(QuestSaveData));
		}
	}
}

void APackageBase::LoadItemFromData_Implementation(const FStoredItemInstanceData& InData)
{
	Super::LoadItemFromData_Implementation(InData);

	for (const FInstancedStruct& ExtraData : InData.ExtraSaveData)
	{
		if (const FPackageItemSaveData* PackageSaveData = ExtraData.GetPtr<FPackageItemSaveData>())
		{
			BaseValue = PackageSaveData->BaseValue;
			PackageType = PackageSaveData->PackageType;
			MaxSpoilTime = PackageSaveData->MaxSpoilTime;
			CurrentSpoilTime = PackageSaveData->CurrentSpoilTime;
			bIsBroken = PackageSaveData->bIsBroken;

			OnRep_bIsBroken();
			break;
		}
	}

	if (GetClass()->ImplementsInterface(UQuestItemSaveInterface::StaticClass()))
	{
		for (const FInstancedStruct& ExtraData : InData.ExtraSaveData)
		{
			if (const FQuestItemSaveData* QuestSaveData = ExtraData.GetPtr<FQuestItemSaveData>())
			{
				IQuestItemSaveInterface::Execute_ApplyQuestItemSaveData(this, *QuestSaveData);
				return;
			}
		}
	}
}

void APackageBase::MulticastPickUp_Implementation(AActor* Picker)
{
	Super::MulticastPickUp_Implementation(Picker);

	// 무거운 택배는 서버와 모든 클라이언트에서 상호작용은 유지하되 물리 및 캐릭터 충돌을 끕니다. (동기화 불일치/떨림 방지)
	if (PackageType == EPackageType::Heavy)
	{
		MeshComponent->SetSimulatePhysics(false);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
}

void APackageBase::MulticastDrop_Implementation(FVector DropLocation, AActor* Dropper)
{
	Super::MulticastDrop_Implementation(DropLocation, Dropper);

	// 내려놓을 때는 서버와 모든 클라이언트에서 충돌을 정상 복구합니다.
	if (PackageType == EPackageType::Heavy)
	{
		MeshComponent->SetSimulatePhysics(true);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}
}

void APackageBase::AddCarrier(AActor* Carrier)
{
	if (!Carrier || CurrentCarriers.Contains(Carrier)) return;

	if (PackageType == EPackageType::Heavy && CurrentCarriers.Num() >= 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] 무거운 택배는 최대 2명까지 운반 가능합니다."), *GetName());
		return;
	}

	if (CurrentCarriers.Num() == 0)
	{
		// 1인 픽업 시점의 물리 처리는 MulticastPickUp_Implementation에서 클라이언트 동기화와 함께 처리됩니다.
	}
	else if (CurrentCarriers.Num() == 1 && PackageType == EPackageType::Heavy)
	{
		AActor* FrontCarrier = CurrentCarriers[0];

		// 박스 핸들 간 물리적 거리 (기본 160cm)
		float HandleDist = 160.0f;
		if (Handle_Front && Handle_Back)
		{
			HandleDist = FVector::Dist(Handle_Front->GetRelativeLocation(), Handle_Back->GetRelativeLocation());
		}
		if (HandleDist < 80.0f || HandleDist > 300.0f)
		{
			HandleDist = 160.0f;
		}

		// 1인 드래그 시 상자가 놓여 있는 방향(FrontCarrier -> Box) 기준으로 2번째 운반자의 위치를 배치
		FVector BoxDir = GetActorLocation() - FrontCarrier->GetActorLocation();
		BoxDir.Z = 0.0f;
		if (BoxDir.IsNearlyZero())
		{
			BoxDir = -FrontCarrier->GetActorForwardVector();
			BoxDir.Z = 0.0f;
		}
		BoxDir.Normalize();

		// 2번째 운반자는 FrontCarrier로부터 (HandleDist + 60cm) 거리에 배치되어 FrontCarrier를 마주봄
		FVector FinalLocation = FrontCarrier->GetActorLocation() + BoxDir * (HandleDist + 60.0f);
		FinalLocation.Z = FrontCarrier->GetActorLocation().Z;

		// 2번째 운반자가 FrontCarrier를 마주보는 회전
		FVector DirToFront = FrontCarrier->GetActorLocation() - FinalLocation;
		DirToFront.Z = 0.0f;
		const FRotator FaceRotation = DirToFront.IsNearlyZero()
			? Carrier->GetActorRotation()
			: DirToFront.Rotation();

		Carrier->SetActorLocationAndRotation(FinalLocation, FaceRotation, false, nullptr, ETeleportType::TeleportPhysics);
		if (APawn* CarrierPawn = Cast<APawn>(Carrier))
		{
			if (AController* CarrierController = CarrierPawn->GetController())
			{
				CarrierController->SetControlRotation(FaceRotation);
			}
		}

		// 첫 번째 운반자도 즉시 2번째 운반자 방향을 바라보게 회전
		FVector DirToBack = FinalLocation - FrontCarrier->GetActorLocation();
		DirToBack.Z = 0.0f;
		if (!DirToBack.IsNearlyZero())
		{
			FrontCarrier->SetActorRotation(DirToBack.Rotation());
			if (APawn* FrontPawn = Cast<APawn>(FrontCarrier))
			{
				if (AController* FrontController = FrontPawn->GetController())
				{
					FrontController->SetControlRotation(DirToBack.Rotation());
				}
			}
		}

		TransitionGracePeriod = 1.0f;
	}

	CurrentCarriers.Add(Carrier);

	if (PackageType == EPackageType::Heavy && CurrentCarriers.Num() == 2)
	{
		// 2인 운반 위치로 즉시 스냅 (DeltaTime = 0.0f로 즉시 배치하여 손잡이 이탈 방지)
		UpdateHeavyPackagePosition(0.0f);
	}

	UpdateCarriersSpeedModifier();
}

void APackageBase::RemoveCarrier(AActor* Carrier)
{
	if (!Carrier || !CurrentCarriers.Contains(Carrier)) return;

	// 무게 원상복구
	if (AppliedWeightMap.Contains(Carrier))
	{
		if (ACharacterBase* BaseChar = Cast<ACharacterBase>(Carrier))
		{
			if (UBaseAttributeSet* AttrSet = BaseChar->BaseAttribute)
			{
				AttrSet->SetCurrentWeight(FMath::Max(0.0f, AttrSet->GetCurrentWeight() - AppliedWeightMap[Carrier]));
			}
		}
		AppliedWeightMap.Remove(Carrier);
	}

	// 기존 개별 무시(IgnoreActorWhenMoving) 해제 로직은 제거 (이제 채널 단위 무시를 사용하므로)

	CurrentCarriers.Remove(Carrier);
	UpdateCarriersSpeedModifier();

	// [2→1 전환] 다음 Tick 이전에 즉시 1인 드래그 위치로 선점 배치 (떨림 방지)
	// UpdateHeavyPackagePosition()의 1인 로직과 동일한 계산을 RemoveCarrier 시점에 미리 수행합니다.
	if (CurrentCarriers.Num() == 1 && PackageType == EPackageType::Heavy)
	{
		// [제안 B] 0.2초 유예 기간 설정: 전환 직후 이탈 검사 스킵 (강제 드랍 연쇄 방지)
		TransitionGracePeriod = 0.2f;

		AActor* RemainingCarrier = CurrentCarriers[0];
		if (ACharacter* RemainingChar = Cast<ACharacter>(RemainingCarrier))
		{
			FVector DragPos;
			if (RemainingChar->GetMesh()->DoesSocketExist(DragSocketName))
			{
				DragPos = RemainingChar->GetMesh()->GetSocketLocation(DragSocketName);
			}
			else
			{
				DragPos = RemainingCarrier->GetActorLocation()
					- RemainingCarrier->GetActorForwardVector() * 30.0f
					+ FVector(0, 0, 80.0f);
			}

			FRotator WorldDragRot = RemainingCarrier->GetActorRotation();
			WorldDragRot.Yaw += 180.0f;
			WorldDragRot.Pitch = -20.0f;
			WorldDragRot.Roll = 0.0f;

			FQuat FinalDragQuat = WorldDragRot.Quaternion() * SingleCarryRotationOffset.Quaternion();
			FRotator DragRot = FinalDragQuat.Rotator();

			const FVector HFLocal = Handle_Front ? Handle_Front->GetRelativeLocation() : FVector::ZeroVector;
			const FVector NewPackageLoc = DragPos - FinalDragQuat.RotateVector(HFLocal);

			SetActorLocationAndRotation(NewPackageLoc, DragRot, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	// 아무도 들고 있지 않게 되면 물리 및 충돌 재활성 (클라이언트 동기화를 위해 MulticastDrop이 담당함)
	if (CurrentCarriers.Num() == 0)
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
}

void APackageBase::UpdateCarriersSpeedModifier()
{
	if (ItemWeight <= 0.0f)
	{
		return;
	}

	float WeightPerCarrier = CurrentCarriers.Num() > 0 ? (ItemWeight / CurrentCarriers.Num()) : 0.0f;

	// 참여한 모든 플레이어에게 무게 균등 분배
	for (AActor* Carrier : CurrentCarriers)
	{
		if (ACharacterBase* BaseChar = Cast<ACharacterBase>(Carrier))
		{
			if (UBaseAttributeSet* AttrSet = BaseChar->BaseAttribute)
			{
				// 기존에 분배된 무게가 있다면 일단 차감 (재분배를 위함)
				if (AppliedWeightMap.Contains(Carrier))
				{
					AttrSet->SetCurrentWeight(FMath::Max(0.0f, AttrSet->GetCurrentWeight() - AppliedWeightMap[Carrier]));
				}

				// 새로운 N분의 1 무게 부여
				AttrSet->SetCurrentWeight(AttrSet->GetCurrentWeight() + WeightPerCarrier);
				AppliedWeightMap.Add(Carrier, WeightPerCarrier);
			}
		}
	}

	CurrentSpeedRatio = 1.0f;
}

void APackageBase::OnPackageHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// 캐릭터가 밟거나 가볍게 부딪히는 것은 무시 (물리적 낙하/충돌만 취급)
	if (bIsBroken || OtherActor == this)
	{
		return;
	}

	// 충돌 순간의 물리 속도(충격량)를 구함
	float ImpactSpeed = HitComponent->GetComponentVelocity().Size();

	// 플레이어(MainCharacter)와 부딪혔을 경우 넉다운/피격 처리 검사
	if (AMainCharacter* HitPlayer = Cast<AMainCharacter>(OtherActor))
	{
		// 임팩트 속도가 설정된 넉다운 기준치 이상일 때만 기절(Knockdown) 처리
		if (ImpactSpeed >= KnockdownThresholdSpeed)
		{
			HitPlayer->Knockdown();
			UE_LOG(LogTemp, Warning, TEXT("[%s] 플레이어를 세게 타격하여 기절시켰습니다! 속도: %f"), *GetName(), ImpactSpeed);
		}
		else if (ImpactSpeed > 150.0f)
		{
			HitPlayer->PlayHitReaction(0.5f);
			UE_LOG(LogTemp, Log, TEXT("[%s] 플레이어 피격 반응 발생! 속도: %f"), *GetName(), ImpactSpeed);
		}
		
		// 플레이어와의 충돌에서는 패키지 자체의 내구도 감소는 진행하지 않음(기획에 따라 변경 가능)
		return;
	}

	// 일반 ACharacterBase와의 충돌 (NPC 등)
	if (ACharacterBase* HitChar = Cast<ACharacterBase>(OtherActor))
	{
		if (ImpactSpeed > 150.0f && HitChar->GetVisualComponent())
		{
			static const FGameplayTag HitTag = FGameplayTag::RequestGameplayTag(FName("State.Player.HitReaction"), false);
			HitChar->GetVisualComponent()->SetTagTemporaryOverride(HitTag, 0.5f);
		}
		return;
	}

	// 500 이상의 속도로 부딪혔을 때만 대미지 처리 (살짝 내려놓는 것은 100~300 내외)
	if (ImpactSpeed > 500.0f)
	{
		// 충격 속도에 비례하여 대미지 계산 (예: 1000 속도면 10 대미지)
		float CalculatedDamage = (ImpactSpeed - 500.0f) * 0.02f;
		if (CalculatedDamage > 1.0f)
		{
			DamagePackage(CalculatedDamage);
			UE_LOG(LogTemp, Warning, TEXT("[%s] 강한 충돌 감지! 속도: %f -> 대미지: %f"), *GetName(), ImpactSpeed, CalculatedDamage);
		}
	}
}

void APackageBase::DamagePackage(float DamageAmount)
{
	if (bIsBroken || DamageAmount <= 0.0f)
	{
		return;
	}

	// [중요] DamagePackage는 서버에서만 실행해야 함.
	// 클라이언트 물리 Hit 이벤트로 실수로 호출되는 것을 방지.
	if (!HasAuthority())
	{
		return;
	}
	
	CurrentDurability -= DamageAmount;
	
	// 서버 자신(로컬)의 UI도 즉시 갱신되도록 수동으로 OnRep 호출
	OnRep_CurrentDurability();
	
	// 파손 처리
	if (CurrentDurability <= 0.0f)
	{
		CurrentDurability = 0.0f;
		bIsBroken = true;  // [Replicated] 이 값이 바뀌면 클라이언트의 OnRep_bIsBroken이 자동 호출됨
		
		UE_LOG(LogTemp, Error, TEXT("[%s] 택배 파손! 가치가 0원이 되었습니다."), *GetName());
		
		// 서버 자신도 연출 실행 (OnRep은 서버에서는 호출 안 되므로 직접 호출)
		OnPackageBroken();
	}
}

int32 APackageBase::GetCurrentValue() const
{
	// 1. 상하기 쉬운 음식이고, 유통기한이 다 지났다면 가치 0원
	if (PackageType == EPackageType::Perishable && CurrentSpoilTime <= 0.0f)
	{
		return 0;
	}

	// 2. 파손된 경우 0원
	if (bIsBroken || CurrentDurability <= 0.0f)
	{
		return 0;
	}

	// 3. 내구도 비례 가치 계산
	if (MaxDurability > 0.0f)
	{
		float DurabilityRatio = FMath::Clamp(CurrentDurability / MaxDurability, 0.0f, 1.0f);
		return FMath::RoundToInt(BaseValue * DurabilityRatio);
	}

	return BaseValue;
}

void APackageBase::MulticastOnPackageBroken_Implementation()
{
	// 블루프린트 이벤트를 모든 클라이언트에서 실행 (메시 교체, 파티클 등)
	// OnRep_bIsBroken에서도 호출되므로 혹시 모를 이중 호출에 주의
	OnPackageBroken();
}



float APackageBase::GetPushResistance_Implementation() const
{
	return ItemWeight;
}

void APackageBase::Push_Implementation(AActor* Pusher, FVector PushDirection)
{
	// 바닥에 놓여있을 때만 밀기 가능
	if (CurrentCarriers.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("택배가 %s 방향으로 밀렸습니다! 저항: %f"), *PushDirection.ToString(), ItemWeight);
	}
}

void APackageBase::ApplyTrapDamage_Implementation(float DamageAmount, AActor* Causer)
{
	if (!HasAuthority())
	{
		return;
	}

	// 깨지기 쉬운 물품(Fragile)은 함정 대미지 2배
	float FinalDamage = (PackageType == EPackageType::Fragile) ? (DamageAmount * 2.0f) : DamageAmount;
	DamagePackage(FinalDamage);
}

void APackageBase::ApplyTrapStatusEffect_Implementation(TSubclassOf<UGameplayEffect> EffectClass, AActor* Causer)
{
	if (!HasAuthority())
	{
		return;
	}

	// 상하기 쉬운 물품(Perishable)이 독/가스/화염에 노출되면 유통기한 급속 감소
	if (PackageType == EPackageType::Perishable)
	{
		CurrentSpoilTime = FMath::Max(0.0f, CurrentSpoilTime - 5.0f);
	}
}

void APackageBase::ApplyTrapImpulse_Implementation(FVector ImpulseVector)
{
	if (MeshComponent && MeshComponent->IsSimulatingPhysics())
	{
		MeshComponent->AddImpulse(ImpulseVector, NAME_None, true);
	}
}

void APackageBase::ForceDropCarriedItem_Implementation()
{
	if (!HasAuthority() || CurrentCarriers.Num() == 0)
	{
		return;
	}

	MulticastDrop(GetActorLocation());
}

void APackageBase::DrainBattery_Implementation(float DrainAmount)
{
	// 패키지는 배터리 없음
}

void APackageBase::OnTrapHazardEncountered_Implementation(ETrapHazardType HazardType, AActor* TrapActor)
{
	// 위험 물품(Dangerous)이 화염이나 전기를 만나면 폭발 또는 파손
	if (PackageType == EPackageType::Dangerous && (HazardType == ETrapHazardType::Damage || HazardType == ETrapHazardType::ElectricShock))
	{
		DamagePackage(100.0f);
	}
}
