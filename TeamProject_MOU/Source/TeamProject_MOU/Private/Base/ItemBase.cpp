#include "Base/ItemBase.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "UI/ItemInfoWidget.h"
#include "Net/UnrealNetwork.h"

AItemBase::AItemBase()
{
	PrimaryActorTick.bCanEverTick = true;
	
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

	// UI 위젯 컴포넌트 생성 및 설정
	InfoWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("InfoWidgetComponent"));
	InfoWidgetComponent->SetupAttachment(RootComponent);
	InfoWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	InfoWidgetComponent->SetDrawSize(FVector2D(300.0f, 150.0f));
	InfoWidgetComponent->SetVisibility(false);
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
}

void AItemBase::OnRep_CurrentDurability()
{
	// 내구도가 변경되었을 때, 만약 로컬 플레이어가 이 아이템을 보고 있어서 위젯이 켜져 있다면 UI를 갱신합니다.
	if (InfoWidgetComponent && InfoWidgetComponent->IsVisible())
	{
		if (UItemInfoWidget* InfoWidget = Cast<UItemInfoWidget>(InfoWidgetComponent->GetUserWidgetObject()))
		{
			InfoWidget->UpdateItemInfo(this);
		}
	}
}

bool AItemBase::CanInteract_Implementation(AActor* Interactor) const
{
	return true;
}

void AItemBase::Interact_Implementation(AActor* Interactor)
{
	PickUp(Interactor);
}

FText AItemBase::GetInteractPrompt_Implementation() const
{
	return FText::Format(NSLOCTEXT("Interaction", "PickupPrompt", "{0} 줍기"), ItemName);
}

void AItemBase::ShowItemInfo()
{
	if (InfoWidgetComponent)
	{
		InfoWidgetComponent->SetVisibility(true);
		
		if (UItemInfoWidget* InfoWidget = Cast<UItemInfoWidget>(InfoWidgetComponent->GetUserWidgetObject()))
		{
			InfoWidget->UpdateItemInfo(this);
		}
	}
}

void AItemBase::HideItemInfo()
{
	if (InfoWidgetComponent)
	{
		InfoWidgetComponent->SetVisibility(false);
	}
}

void AItemBase::MulticastPickUp_Implementation(AActor* Picker)
{
	// 픽업 시 자신이 띄워둔 UI 무조건 숨김
	HideItemInfo();

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
	MulticastThrow(ThrowVelocity, Thrower);
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
