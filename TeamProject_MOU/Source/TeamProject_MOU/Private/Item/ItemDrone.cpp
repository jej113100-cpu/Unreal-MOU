#include "Item/ItemDrone.h"
#include "Base/PackageBase.h"
#include "Components/CarryingComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

AItemDrone::AItemDrone()
{
	// AItemBase 생성자가 MeshComponent(루트), 물리, 복제(bReplicates, ReplicateMovement)를 이미 세팅한다.
	PrimaryActorTick.bCanEverTick = true;

	// 일반 아이템 거치 지점 (본체 메시에 부착)
	ItemHoldPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ItemHoldPoint"));
	ItemHoldPoint->SetupAttachment(RootComponent);

	// 택배 거치 지점 (드론 머리 위). 기본값으로 위쪽에 배치해 두고 BP에서 조절 가능.
	PackageHoldPoint = CreateDefaultSubobject<USceneComponent>(TEXT("PackageHoldPoint"));
	PackageHoldPoint->SetupAttachment(RootComponent);
	PackageHoldPoint->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));
}

void AItemDrone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AItemDrone, bIsDeployed);
	DOREPLIFETIME(AItemDrone, FollowTarget);
	DOREPLIFETIME(AItemDrone, StoredItem);
	DOREPLIFETIME(AItemDrone, StoredPackage);
}

void AItemDrone::OnRep_IsDeployed()
{
	// 클라이언트에서 배치 상태 변경 시의 훅 (현재 특별 처리는 없음)
}

// [DRONE-012] 후보 오프셋 중 플레이어에서 경로가 뚫린 첫 위치를 골라 반환.
FVector AItemDrone::ChooseFollowOffset() const
{
	// 기본(오른쪽 뒤) FollowOffset을 기준으로 후보들을 만든다. Y 부호가 좌우.
	const float BackX = FollowOffset.X;   // 뒤쪽 거리(음수)
	const float SideY = FollowOffset.Y;   // 오른쪽 거리(양수)
	const float UpZ   = FollowOffset.Z;

	// 우선순위: 오른쪽뒤(기본) -> 왼쪽뒤 -> 정뒤 -> 오른쪽옆 -> 왼쪽옆
	const TArray<FVector> Candidates = {
		FVector(BackX,  SideY, UpZ),   // 오른쪽 뒤 (기본)
		FVector(BackX, -SideY, UpZ),   // 왼쪽 뒤
		FVector(BackX,   0.0f, UpZ),   // 정 뒤
		FVector( 0.0f,  SideY, UpZ),   // 오른쪽 옆
		FVector( 0.0f, -SideY, UpZ),   // 왼쪽 옆
	};

	const FTransform TargetTransform = FollowTarget->GetActorTransform();

	// 경로 판정 기준점: 플레이어 몸통 높이(발밑 트레이스가 바닥에 걸리는 것 방지).
	const FVector TraceStart = FollowTarget->GetActorLocation() + FVector(0.0f, 0.0f, UpZ);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(FollowTarget);
	QueryParams.AddIgnoredActor(this);

	for (const FVector& Cand : Candidates)
	{
		const FVector CandWorld = TargetTransform.TransformPosition(Cand);

		// 플레이어 -> 후보 지점 경로가 벽에 막히지 않으면(=플레이어가 그 위치를 "볼 수 있으면") 채택.
		FHitResult Hit;
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
			Hit, TraceStart, CandWorld, ECC_Visibility, QueryParams);

		if (!bBlocked)
		{
			return CandWorld;
		}
	}

	// 모든 후보가 막혔으면 기본(오른쪽 뒤) 위치를 반환(최후에는 sweep이 막아준다).
	return TargetTransform.TransformPosition(FollowOffset);
}

// [DRONE-004] 팔로우 목표 위치 계산
FVector AItemDrone::CalcTargetLocation()
{
	if (!FollowTarget)
	{
		return GetActorLocation();
	}

	// 플레이어가 이동 중일 때만 베이스 목표 위치를 갱신한다(막힌 방향은 뚫린 후보로 자동 우회).
	// 정지 중(이동값 0)이면 마지막 베이스 위치를 그대로 유지 → 카메라만 돌려도 드론이 안 따라 돈다.
	const bool bIsMoving = FollowTarget->GetVelocity().Size2D() > MoveThreshold;

	if (bIsMoving || !bHasCachedFollowBase)
	{
		CachedFollowBaseLocation = ChooseFollowOffset();
		bHasCachedFollowBase = true;
	}

	FVector Target = CachedFollowBaseLocation;

	// 위아래 보빙은 정지/이동과 무관하게 적용 (제자리에서 살짝 떠다니는 느낌만, 수평 이동 없음)
	if (BobbingAmplitude > 0.0f)
	{
		Target.Z += FMath::Sin(BobbingPhase) * BobbingAmplitude;
	}

	return Target;
}

void AItemDrone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 배치되어 팔로우 중일 때만 동작. 손에 들려 있거나 바닥에 있을 땐 일반 아이템처럼 가만히 있는다.
	if (!bIsDeployed)
	{
		return;
	}

	// 아이템 보관 중 내구도 소모는 서버(권위)에서만 계산 (CurrentDurability는 Replicated).
	if (HasAuthority())
	{
		TickDurabilityDrain(DeltaTime);
	}

	// 팔로우는 서버(권위)에서만 계산하고, 결과 위치는 SetReplicateMovement로 클라에 전파된다.
	if (!HasAuthority() || !FollowTarget)
	{
		return;
	}

	BobbingPhase += DeltaTime * BobbingSpeed;

	const FVector TargetLoc = CalcTargetLocation();
	const FVector NewLoc = FMath::VInterpTo(GetActorLocation(), TargetLoc, DeltaTime, FollowInterpSpeed);

	// bSweep=true: 목표(오른쪽 뒤)로 가는 경로에 벽/오브젝트가 있으면 그 앞에서 막힌다(뚫기 방지).
	// 장애물이 사라지면 매 프레임 다시 목표로 VInterp하므로 자연히 원위치(오른쪽 뒤)로 복귀한다.
	SetActorLocation(NewLoc, /*bSweep=*/true);

	// 드론이 플레이어를 바라보도록 회전 (수평만). 실제 위치는 sweep으로 막혔을 수 있으니 현재 위치 사용.
	const FVector CurrentLoc = GetActorLocation();
	FVector LookDir = FollowTarget->GetActorLocation() - CurrentLoc;
	LookDir.Z = 0.0f;
	if (!LookDir.IsNearlyZero())
	{
		const FRotator NewRot = FMath::RInterpTo(GetActorRotation(), LookDir.Rotation(), DeltaTime, FollowInterpSpeed);
		SetActorRotation(NewRot);
	}
}

// ---------------------------------------------------------
// [좌클릭 사용] 손에서 배치 -> 팔로우 시작
// ---------------------------------------------------------
void AItemDrone::OnUse_Implementation()
{
	// 이미 배치된 상태면 무시 (손에 든 상태에서만 사용 가능)
	if (bIsDeployed)
	{
		return;
	}

	// 배치는 항상 서버에서 처리 (상태·복제 신뢰성). 클라에서 불리면 ServerDeploy로 위임한다.
	// WeaponItemBase::OnUse -> ServerFire 와 동일 패턴. [WEAPON-000]
	if (!HasAuthority())
	{
		ServerDeploy();
		return;
	}

	// OnUse는 MainCharacter가 손에 든 아이템에 대해 호출한다. 소유자(든 사람)를 찾는다.
	ACharacter* User = Cast<ACharacter>(GetOwner());
	if (!User)
	{
		User = Cast<ACharacter>(GetAttachParentActor());
	}
	if (!User)
	{
		User = Cast<ACharacter>(LastOwner);
	}
	if (!User)
	{
		return;
	}

	DeployAndFollow(User);
}

// [DRONE-008] 클라이언트 -> 서버 배치 위임
void AItemDrone::ServerDeploy_Implementation()
{
	// 서버에서 실행되므로 OnUse의 서버 경로를 그대로 탄다.
	OnUse_Implementation();
}

// 집을 때 소유권 설정 → 클라가 ServerDeploy RPC를 보낼 수 있게 한다.
// (Owner 없으면 "No owning connection"으로 ServerRPC가 버려짐)
void AItemDrone::PickUp_Implementation(AActor* Picker)
{
	Super::PickUp_Implementation(Picker);

	if (HasAuthority() && Picker)
	{
		SetOwner(Picker);
	}
}

// 배치(팔로우) 상태에서는 E키 줍기 대상에서 제외한다.
// (CarryingComponent::GrabOrDrop의 SphereOverlap이 이 함수로 필터하므로,
//  false를 반환하면 허공에서 E를 눌러도 드론이 잡히지 않는다)
bool AItemDrone::CanBePickedUpBy(AActor* PotentialPicker) const
{
	if (bIsDeployed)
	{
		return false;
	}

	return Super::CanBePickedUpBy(PotentialPicker);
}

// [DRONE-007] 손에서 배치되어 팔로우 시작
void AItemDrone::DeployAndFollow(ACharacter* User)
{
	// 든 사람의 손을 비운다 (CarryingComponent가 CarriedActor 복제/무게 갱신 처리).
	if (UCarryingComponent* CarryingComp = User->FindComponentByClass<UCarryingComponent>())
	{
		if (CarryingComp->GetCarriedActor() == this)
		{
			CarryingComp->ClearCarriedItem();
		}
	}

	// 손(스켈레탈 메시)에서 떼어내 월드에 독립시킨다.
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	// 배치 상태 및 팔로우 대상 설정.
	FollowTarget = User;
	bIsDeployed = true;

	// 목표 위치 캐시 초기화 (다음 Tick에서 현재 플레이어 위치 기준으로 새로 잡도록)
	bHasCachedFollowBase = false;

	// 배치 후에는 드론 소유권이 회수에 쓰이지 않는다(회수는 InteractionComponent의 서버 RPC가 담당).
	// 특정 플레이어에 소유가 묶여 있으면 "그 사람만" 처리 가능한 오해를 부르므로 정리한다.
	SetOwner(nullptr);

	// 공중에 떠서 따라다닌다. 물리는 끄되(직접 위치 제어), 벽/오브젝트에 막히도록 쿼리 콜리전은 유지한다.
	// - 월드 static/dynamic: Block  -> 이동 sweep이 벽 앞에서 멈춤(뚫기 방지, "밀려남")
	// - Pawn(플레이어): Overlap     -> 플레이어를 밀어내지 않음
	if (MeshComponent)
	{
		MeshComponent->SetSimulatePhysics(false);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeshComponent->SetCollisionResponseToAllChannels(ECR_Overlap);
		MeshComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		MeshComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}
}

// ---------------------------------------------------------
// [F키 상호작용] 배치 상태면 맡기기/회수, 아니면 기본(줍기)
// ---------------------------------------------------------
bool AItemDrone::CanInteract_Implementation(AActor* Interactor) const
{
	// 배치되지 않았으면 일반 아이템처럼 "줍기" 판정 (AItemBase 기본)
	if (!bIsDeployed)
	{
		return Super::CanInteract_Implementation(Interactor);
	}

	// 배치 상태: 맡기기/회수 판정
	ACharacter* Character = Cast<ACharacter>(Interactor);
	if (!Character)
	{
		return false;
	}

	UCarryingComponent* CarryingComp = Character->FindComponentByClass<UCarryingComponent>();
	if (!CarryingComp)
	{
		return false;
	}

	AItemBase* HandItem = Cast<AItemBase>(CarryingComp->GetCarriedActor());

	// 손에 아이템이 있으면: 해당 슬롯(택배=머리위 / 일반=아래)이 비어있고 맡길 수 있으면 가능.
	if (HandItem)
	{
		const bool bIsPackage = (Cast<APackageBase>(HandItem) != nullptr);
		const bool bSlotEmpty = bIsPackage ? (StoredPackage == nullptr) : (StoredItem == nullptr);
		return bSlotEmpty && CanStoreItem(HandItem);
	}

	// 빈손이면: 두 슬롯 중 하나라도 차 있으면 회수 가능.
	return (StoredItem != nullptr) || (StoredPackage != nullptr);
}

// [DRONE-009] 이 아이템을 드론에 맡길 수 있는지 판정
bool AItemDrone::CanStoreItem(const AItemBase* Item) const
{
	if (!Item || Item == this)
	{
		return false;
	}

	// 지금 손에서 뗄 수 없는 상태(사용 중 등)면 맡길 수 없다.
	if (!Item->CanBeDropped())
	{
		return false;
	}

	// 택배: Heavy / Quest 타입은 거부, 그 외 타입은 허용 (bCanBeStoredInInventory와 무관)
	if (const APackageBase* Package = Cast<APackageBase>(Item))
	{
		if (Package->PackageType == EPackageType::Heavy || Package->PackageType == EPackageType::Quest)
		{
			return false;
		}
		return true;
	}

	// 일반 아이템: 인벤토리 수납 가능한 것만 허용 (택배 외 수납불가 아이템 배제)
	return Item->bCanBeStoredInInventory;
}

void AItemDrone::Interact_Implementation(AActor* Interactor)
{
	// 배치되지 않았으면 기본 줍기 흐름 (AItemBase가 PickUp/CarryingComponent 서버 처리를 담당)
	if (!bIsDeployed)
	{
		Super::Interact_Implementation(Interactor);
		return;
	}

	// 배치 상태의 맡기기/회수는 서버에서만 상태를 바꾼다.
	// 클라의 상호작용은 InteractionComponent::ServerRunInteract가 서버에서 이 함수를 재실행해 주므로,
	// 클라 로컬 호출은 여기서 무시한다.
	if (!HasAuthority())
	{
		return;
	}

	HandleInteractOnServer(Cast<ACharacter>(Interactor));
}

// [DRONE-010] 서버에서 맡기기/회수 실제 처리
void AItemDrone::HandleInteractOnServer(ACharacter* Character)
{
	if (!bIsDeployed || !Character)
	{
		return;
	}

	UCarryingComponent* CarryingComp = Character->FindComponentByClass<UCarryingComponent>();
	if (!CarryingComp)
	{
		return;
	}

	AItemBase* HandItem = Cast<AItemBase>(CarryingComp->GetCarriedActor());

	if (HandItem)
	{
		// 맡기기: 손에 든 것을 해당 슬롯에 넣는다 (슬롯이 비어있고 허용되는 경우만).
		const bool bIsPackage = (Cast<APackageBase>(HandItem) != nullptr);
		const bool bSlotEmpty = bIsPackage ? (StoredPackage == nullptr) : (StoredItem == nullptr);
		if (bSlotEmpty && CanStoreItem(HandItem))
		{
			StoreItemFromHand(Character, HandItem);
		}
	}
	else
	{
		// 회수: 빈손이면 일반 아이템을 우선, 없으면 택배를 꺼낸다.
		if (StoredItem)
		{
			RetrieveItemToHand(Character, /*bRetrievePackage=*/false);
		}
		else if (StoredPackage)
		{
			RetrieveItemToHand(Character, /*bRetrievePackage=*/true);
		}
	}
}

FText AItemDrone::GetInteractPrompt_Implementation() const
{
	if (!bIsDeployed)
	{
		// 바닥/집기 상태: AItemBase 기본 줍기 프롬프트
		return Super::GetInteractPrompt_Implementation();
	}

	if (StoredItem || StoredPackage)
	{
		return NSLOCTEXT("Interaction", "DroneRetrievePrompt", "드론에서 아이템 꺼내기");
	}
	return NSLOCTEXT("Interaction", "DroneStorePrompt", "드론에 아이템 맡기기");
}

// [DRONE-001] 손 -> 드론 (맡기기). 택배는 StoredPackage(머리위), 일반은 StoredItem(아래) 슬롯.
void AItemDrone::StoreItemFromHand(ACharacter* Interactor, AItemBase* HandItem)
{
	UCarryingComponent* CarryingComp = Interactor->FindComponentByClass<UCarryingComponent>();
	if (!CarryingComp || !HandItem || HandItem == this)
	{
		return;
	}

	const bool bIsPackage = (Cast<APackageBase>(HandItem) != nullptr);

	// 택배는 운반자 리스트/분배 무게가 걸려 있으므로, 손을 비우기 전에 운반자에서 제거한다.
	// (기존 Drop 흐름과 동일. RemoveCarrier가 무게 원상복구까지 처리)
	if (APackageBase* Package = Cast<APackageBase>(HandItem))
	{
		Package->RemoveCarrier(Interactor);
	}

	// 손을 비운다 (CarryingComponent가 CarriedActor 복제/무게 갱신까지 처리).
	CarryingComp->ClearCarriedItem();

	// 해당 슬롯에 등록하고 거치 지점에 부착.
	if (bIsPackage)
	{
		StoredPackage = HandItem;
	}
	else
	{
		StoredItem = HandItem;
	}
	MulticastAttachToDrone(HandItem);

	// 내구도 소모는 "드론 자신"의 CurrentDurability를 Tick에서 깎는다(슬롯별 계산 불필요).
	// 보관물이 하나라도 있으면 TickDurabilityDrain이 자동으로 소모를 진행한다.
}

// [DRONE-002] 드론 -> 손 (회수). bRetrievePackage=true면 택배 슬롯, false면 일반 슬롯을 꺼낸다.
void AItemDrone::RetrieveItemToHand(ACharacter* Interactor, bool bRetrievePackage)
{
	UCarryingComponent* CarryingComp = Interactor ? Interactor->FindComponentByClass<UCarryingComponent>() : nullptr;
	if (!CarryingComp)
	{
		return;
	}

	AItemBase* Item = bRetrievePackage ? StoredPackage : StoredItem;
	if (!Item)
	{
		return;
	}

	// 슬롯을 비운다. (드론 내구도 소모는 남은 다른 슬롯이 있으면 계속, 둘 다 비면 Tick이 자동으로 멈춤)
	if (bRetrievePackage)
	{
		StoredPackage = nullptr;
	}
	else
	{
		StoredItem = nullptr;
	}

	// 드론 거치에서 떼어낸다.
	Item->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	// 인벤토리에서 꺼낼 때와 동일하게, EquipItem(단순 부착) 전에 OnEquipped로
	// 표시/충돌 상태를 먼저 복구해야 BoundingBox 오프셋 계산과 렌더링이 정상 동작한다.
	Item->MulticastOnEquipped(Interactor);

	// 택배는 맡길 때 RemoveCarrier로 뺐으므로, 회수 시 다시 운반자로 등록해 무게/협동 상태를 맞춘다.
	if (APackageBase* Package = Cast<APackageBase>(Item))
	{
		Package->AddCarrier(Interactor);
	}

	// 손에 쥐어준다. EquipItem이 CarriedActor 설정·무게 갱신·MulticastEquipItem까지 담당.
	CarryingComp->EquipItem(Item);
}

// [DRONE-003] 아이템을 드론 거치 지점에 부착 (모든 클라 동기화)
void AItemDrone::MulticastAttachToDrone_Implementation(AItemBase* Item)
{
	if (!Item)
	{
		return;
	}

	// 택배는 머리 위(PackageHoldPoint), 일반 아이템은 ItemHoldPoint에 거치.
	USceneComponent* HoldPoint = Cast<APackageBase>(Item) ? PackageHoldPoint : ItemHoldPoint;
	if (!HoldPoint)
	{
		return;
	}

	// 인벤토리 수납과 유사하게 충돌·물리를 비활성화한 뒤(단, 화면에는 보이도록) 부착한다.
	Item->SetActorHiddenInGame(false);
	if (Item->MeshComponent)
	{
		Item->MeshComponent->SetSimulatePhysics(false);
		Item->MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	Item->AttachToComponent(HoldPoint, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
}

// [DRONE-006] 아이템 보관 중 내구도 소모 (서버 전용). 일반/택배 두 슬롯 모두 처리.
void AItemDrone::TickDurabilityDrain(float DeltaTime)
{
	// 뭔가를 하나라도 보관 중일 때만 드론 내구도가 깎인다. 아무것도 없으면 소모 정지.
	if (!StoredItem && !StoredPackage)
	{
		return;
	}

	if (DurabilityDrainDuration <= 0.0f)
	{
		return;
	}

	// 드론 자신의 내구도를 DurabilityDrainDuration(초)에 걸쳐 0이 되도록 깎는다.
	const float DrainPerSecond = MaxDurability / DurabilityDrainDuration;
	CurrentDurability -= DrainPerSecond * DeltaTime;

	// 드론 내구도 소진: 보관물 Drop 후 드론 파괴.
	if (CurrentDurability <= 0.0f)
	{
		CurrentDurability = 0.0f;
		BreakDrone();
	}
}

// [DRONE-011] 드론 내구도 소진 시: 보관 중인 아이템/택배를 바닥에 떨어뜨리고 드론을 파괴
void AItemDrone::BreakDrone()
{
	// 보관 중인 것들을 드론에서 떼어 바닥에 Drop (AItemBase::Drop이 물리/충돌 복구 + Detach 처리).
	auto DropSlot = [this](TObjectPtr<AItemBase>& Slot)
	{
		if (!Slot)
		{
			return;
		}

		AItemBase* Item = Slot;
		Slot = nullptr;

		// 드론 위치 근처에 떨어뜨린다.
		const FVector DropLoc = Item->GetActorLocation();
		Item->Drop(DropLoc, nullptr);
	};

	DropSlot(StoredItem);
	DropSlot(StoredPackage);

	// 드론 파괴.
	Destroy();
}
