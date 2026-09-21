#include "Base/ItemBase.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InventoryComponent.h"
#include "Engine/Texture2D.h"
#include "Net/UnrealNetwork.h"
#include "Player/MainCharacter.h"

AItemBase::AItemBase()
{
	PrimaryActorTick.bCanEverTick = false;
	
	// [멀티플레이] 아이템 상태 및 이동 동기화 필수 설정
	bReplicates = true;
	SetReplicateMovement(true);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;
	
	// 물리 연산 및 어태치를 위해 Mobility를 Movable로 필수 설정
	MeshComponent->SetMobility(EComponentMobility::Movable);
	
	// 기본 물리 설정 (바닥에 놓여있는 상태를 기본으로 가정)
	MeshComponent->SetSimulatePhysics(true);
	MeshComponent->SetCollisionProfileName(TEXT("PhysicsActor"));
	
	// 외곽선 렌더링을 위한 커스텀 뎁스 비활성화 (포커스 시 활성화 예정)
	MeshComponent->SetRenderCustomDepth(false);
}

void AItemBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (MeshComponent)
	{
		MeshComponent->SetNotifyRigidBodyCollision(true);
		MeshComponent->OnComponentHit.AddDynamic(this, &AItemBase::OnItemHit);
	}
}

void AItemBase::BeginPlay()
{
	Super::BeginPlay();
	CurrentUseCount = MaxUseCount;
	CurrentDurability = MaxDurability;

	// 맵에 미리 배치된 아이템의 콜리전이 블루프린트 설정 등의 이유로 꼬이는 현상을 방지하기 위해,
	// 게임 시작 시 서버와 클라이언트 모두 물리 시뮬레이션 및 폰(캐릭터) 블록 상태를 강제로 초기화합니다.
	if (MeshComponent)
	{
		MeshComponent->SetCollisionProfileName(TEXT("PhysicsActor"));
		MeshComponent->SetSimulatePhysics(true);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		MeshComponent->SetNotifyRigidBodyCollision(true);
		
		// 에디터에 배치된 아이템이 바닥과 미세하게 겹쳐있을 경우, 캐릭터가 밟았을 때 파묻히는 물리 버그가 발생할 수 있습니다.
		// 이를 방지하기 위해 위치를 살짝 위로 띄워 자연스럽게 떨어지도록 유도하고 물리 엔진을 깨웁니다.
		AddActorWorldOffset(FVector(0.0f, 0.0f, 5.0f));
		MeshComponent->WakeRigidBody();
	}
}

void AItemBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AItemBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AItemBase, CurrentDurability);
	DOREPLIFETIME(AItemBase, ItemIcon);
}

void AItemBase::OnRep_ItemIcon()
{
	// 슬롯 참조보다 아이콘이 늦게 복제된 경우에도 복원된 슬롯 UI를 갱신합니다.
	AActor* InventoryOwner = GetOwner();
	if (!InventoryOwner) InventoryOwner = GetAttachParentActor();
	UInventoryComponent* Inventory = InventoryOwner
		? InventoryOwner->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inventory) return;

	for (int32 SlotIndex = 0; SlotIndex < Inventory->InventorySlots.Num(); ++SlotIndex)
	{
		if (Inventory->InventorySlots[SlotIndex] == this)
		{
			Inventory->OnInventorySlotChanged.Broadcast(SlotIndex, this);
		}
	}
}


bool AItemBase::CanBePickedUpBy(AActor* PotentialPicker) const
{
	// 이미 다른 액터에게 부착(Attach)되어 있다면 집기 불가 (아이템 가로채기 방지)
	if (GetAttachParentActor() != nullptr)
	{
		return false;
	}

	return true;
}

bool AItemBase::CanInteract_Implementation(AActor* Interactor) const
{
	return CanBePickedUpBy(Interactor);
}

void AItemBase::Interact_Implementation(AActor* Interactor)
{
	PickUp(Interactor);
}

FText AItemBase::GetInteractPrompt_Implementation() const
{
	return FText::Format(NSLOCTEXT("Interaction", "PickupPrompt", "{0} 줍기"), ItemName);
}

void AItemBase::MulticastPickUp_Implementation(AActor* Picker)
{
	// 모든 클라이언트에서 물리 비활성화, 충돌은 QueryOnly로 변경(물리적 밀어내기 방지)
	MeshComponent->SetSimulatePhysics(false);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	
	if (Picker)
	{
		// 짐을 든 본인(Picker)과 택배 간의 충돌만 무시 (다른 플레이어는 여전히 막힘)
		MeshComponent->IgnoreActorWhenMoving(Picker, true);
		// 캐릭터의 CapsuleComponent(루트)에도 역방향 무시 설정
		TArray<UPrimitiveComponent*> PickerPrimComps;
		Picker->GetComponents<UPrimitiveComponent>(PickerPrimComps);
		for (UPrimitiveComponent* Comp : PickerPrimComps)
		{
			Comp->IgnoreActorWhenMoving(this, true);
		}
	}
}

void AItemBase::PickUp_Implementation(AActor* Picker)
{
	bWasThrown = false;
	LastOwner = Picker;
	
	// 물리 비활성화는 모든 클라이언트가 알아야 함
	MulticastPickUp(Picker);
	
	// 여기서 플레이어의 인벤토리 및 CarryingComponent 연동을 진행합니다.
	// (추후 MainCharacter 및 Component에서 호출 처리 연동)
}

void AItemBase::MulticastDrop_Implementation(FVector DropLocation, AActor* Dropper)
{
	SetActorLocation(DropLocation);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	
	// 물리 재활성화
	MeshComponent->SetSimulatePhysics(true);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	
	// 운반할 때 설정했던 충돌 무시 옵션 해제
	MeshComponent->ClearMoveIgnoreActors();
	
	AActor* TargetDropper = Dropper;
	if (!TargetDropper)
	{
		TargetDropper = LastOwner;
	}
	
	if (TargetDropper)
	{
		TArray<UPrimitiveComponent*> PickerPrimComps;
		TargetDropper->GetComponents<UPrimitiveComponent>(PickerPrimComps);
		for (UPrimitiveComponent* Comp : PickerPrimComps)
		{
			Comp->IgnoreActorWhenMoving(this, false);
		}
	}
}

void AItemBase::Drop_Implementation(FVector DropLocation, AActor* Dropper)
{
	bWasThrown = false;
	MulticastDrop(DropLocation, Dropper);
}

void AItemBase::MulticastThrow_Implementation(FVector ThrowVelocity, AActor* Thrower)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	
	MeshComponent->SetSimulatePhysics(true);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->AddImpulse(ThrowVelocity, NAME_None, true);

	MeshComponent->ClearMoveIgnoreActors();
	
	AActor* TargetThrower = Thrower;
	if (!TargetThrower)
	{
		TargetThrower = LastOwner;
	}
	
	if (TargetThrower)
	{
		TArray<UPrimitiveComponent*> PickerPrimComps;
		TargetThrower->GetComponents<UPrimitiveComponent>(PickerPrimComps);
		for (UPrimitiveComponent* Comp : PickerPrimComps)
		{
			Comp->IgnoreActorWhenMoving(this, false);
		}
	}
}

void AItemBase::Throw_Implementation(FVector ThrowVelocity, AActor* Thrower)
{
	bWasThrown = true;
	LastThrower = Thrower ? Thrower : LastOwner.Get();
	MulticastThrow(ThrowVelocity, Thrower);
}

void AItemBase::OnItemHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// 충돌에 의한 게임플레이 판정(피격, 기절, 대미지)은 서버에서만 권위적으로 단일 처리 (중복 판정 방지)
	if (!HasAuthority() || !OtherActor || OtherActor == this)
	{
		return;
	}

	// 던진 본인과의 즉각 충돌(자폭) 방지
	if (OtherActor == LastThrower.Get())
	{
		return;
	}

	// 충돌 순간의 물리 속도(충격량)를 구함
	float ImpactSpeed = HitComponent ? HitComponent->GetComponentVelocity().Size() : 0.0f;

	// 플레이어(MainCharacter)와 부딪혔을 경우 피격/넉다운 처리
	if (AMainCharacter* HitPlayer = Cast<AMainCharacter>(OtherActor))
	{
		// 던져졌거나 일정 속도(80 이상)로 플레이어에게 부딪혔을 때만 반응 (바닥에 멈춘 물체에 닿았을 때 오발동 방지)
		if (bWasThrown || ImpactSpeed > 80.0f)
		{
			HandlePlayerHit(HitPlayer, ImpactSpeed);
			bWasThrown = false;
		}
		return;
	}

	// 지면이나 벽 등 다른 물체에 부딪쳐 멈추면 던져짐 상태 해제
	if (ImpactSpeed < 80.0f)
	{
		bWasThrown = false;
	}
}

void AItemBase::HandlePlayerHit(AMainCharacter* HitPlayer, float ImpactSpeed)
{
	if (!HitPlayer)
	{
		return;
	}

	// 일반 아이템 기본 동작: 속도에 상관없이 피격(맞는) 애니메이션만 출력
	HitPlayer->PlayHitReaction(0.5f);
	UE_LOG(LogTemp, Log, TEXT("[%s] 일반 아이템 충돌! 피격 애니메이션 출력 (속도: %f)"), *GetName(), ImpactSpeed);
}

// [ITEM-100] 사용 입력 해제의 기본 동작은 비워 둔다.
void AItemBase::OnUseReleased_Implementation()
{
}

void AItemBase::OnUse_Implementation()
{
	// 일반 아이템 사용 로직 (자식 클래스에서 구현)
	if (CurrentUseCount > 0)
	{
		CurrentUseCount--;
	}
}

void AItemBase::MulticastOnEquipped_Implementation(AActor* Equipper)
{
	OnEquipped(Equipper);
}

void AItemBase::OnEquipped_Implementation(AActor* Equipper)
{
	// 인벤토리에서 나와서 손에 들려질 때
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	// 플레이어와 충돌하지 않도록 예외 처리 (PickUp과 동일한 상태)
	if (Equipper)
	{
		MeshComponent->IgnoreActorWhenMoving(Equipper, true);
		TArray<UPrimitiveComponent*> PickerPrimComps;
		Equipper->GetComponents<UPrimitiveComponent>(PickerPrimComps);
		for (UPrimitiveComponent* Comp : PickerPrimComps)
		{
			Comp->IgnoreActorWhenMoving(this, true);
		}
	}
}

void AItemBase::MulticastOnUnequipped_Implementation(AActor* Equipper)
{
	OnUnequipped(Equipper);
}

void AItemBase::OnUnequipped_Implementation(AActor* Equipper)
{
	// 인벤토리에 보관될 때 (모습 감추기, 충돌 및 물리 완벽 비활성화)
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetSimulatePhysics(false);
	
	// 안전하게 액터를 플레이어에게 어태치하여 잃어버리지 않게 함 (투명한 주머니 역할)
	if (Equipper)
	{
		AttachToActor(Equipper, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
}

void AItemBase::SaveItemToData_Implementation(FStoredItemInstanceData& OutData) const
{
	OutData.ItemClass = GetClass();
	OutData.ItemIcon = ItemIcon;
	OutData.Transform = GetActorTransform();
	OutData.CurrentUseCount = CurrentUseCount;
	OutData.CurrentDurability = CurrentDurability;
	OutData.ExtraSaveData.Reset();
}

void AItemBase::LoadItemFromData_Implementation(const FStoredItemInstanceData& InData)
{
	// 아이콘 필드가 없던 기존 데이터는 클래스 기본 아이콘을 유지합니다.
	if (InData.ItemIcon)
	{
		ItemIcon = InData.ItemIcon;
	}
	CurrentUseCount = InData.CurrentUseCount;
	CurrentDurability = InData.CurrentDurability;
	SetActorTransform(InData.Transform);
}
