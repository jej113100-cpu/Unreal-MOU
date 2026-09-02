#include "Components/InventoryComponent.h"
#include "Base/ItemBase.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Character.h"
#include "Components/CarryingComponent.h"
#include "Subsystems/WarehouseDataSubsystem.h"
#include "TimerManager.h"

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner()->HasAuthority())
	{
		// 슬롯 초기화
		InventorySlots.Init(nullptr, MaxSlots);
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UInventoryComponent::TryRestoreSavedInventory);
	}
}

void UInventoryComponent::TryRestoreSavedInventory()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld()) return;

	if (UWarehouseDataSubsystem* WarehouseSubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UWarehouseDataSubsystem>())
	{
		if (WarehouseSubsystem->RestorePlayerInventory(this)) return;
	}

	// Spawn 직후 PlayerState가 아직 연결되지 않은 경우를 위해 제한적으로 재시도합니다.
	if (++InventoryRestoreAttempts < 60)
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UInventoryComponent::TryRestoreSavedInventory);
	}
}

void UInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UInventoryComponent, InventorySlots);
}

void UInventoryComponent::OnRep_InventorySlots()
{
	// 클라이언트에서 UI 갱신을 위해 델리게이트 호출
	for (int32 i = 0; i < InventorySlots.Num(); ++i)
	{
		OnInventorySlotChanged.Broadcast(i, InventorySlots[i]);
	}
}

void UInventoryComponent::RequestSlotAction(int32 SlotIndex)
{
	if (!GetOwner()->HasAuthority())
	{
		ServerRequestSlotAction(SlotIndex);
		return;
	}

	if (SlotIndex < 0 || SlotIndex >= InventorySlots.Num())
		return;

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) return;

	UCarryingComponent* CarryingComp = Character->FindComponentByClass<UCarryingComponent>();
	if (!CarryingComp) return;

	AItemBase* CurrentHandItem = Cast<AItemBase>(CarryingComp->GetCarriedActor());
	AItemBase* ItemInSlot = InventorySlots[SlotIndex];

	// 빈손일 때 -> 슬롯의 아이템을 꺼냄
	if (!CurrentHandItem)
	{
		if (ItemInSlot)
		{
			AItemBase* ItemToEquip = ItemInSlot;
			InventorySlots[SlotIndex] = nullptr; // 슬롯 비움
			
			// 먼저 수납 해제(렌더링 활성화 및 충돌 복구)를 시켜서 BoundingBox 계산이 정상적으로 되도록 함
			ItemToEquip->MulticastOnEquipped(GetOwner());
			
			// 델리게이트를 통해 캐릭터에게 장착 요청 (여기서 BoundingBox 기반 오프셋 계산이 일어남)
			OnEquipRequested.Broadcast(ItemToEquip);
		}
	}
	else
	{
		// 손에 물건이 있을 때
		if (CurrentHandItem->bCanBeStoredInInventory)
		{
			AItemBase* ItemToEquip = nullptr;
			
			if (ItemInSlot)
			{
				// 이미 아이템이 있다면 스왑 (Swap)
				ItemToEquip = ItemInSlot;
				InventorySlots[SlotIndex] = CurrentHandItem;
			}
			else
			{
				// 슬롯이 비어있다면 그냥 수납
				InventorySlots[SlotIndex] = CurrentHandItem;
			}
			
			// 수납되는 아이템의 처리 (화면에서 숨기기 등)
			CurrentHandItem->MulticastOnUnequipped(GetOwner());
			
			if (ItemToEquip)
			{
				ItemToEquip->MulticastOnEquipped(GetOwner());
			}
			
			// 스왑으로 꺼내야 할 아이템이 있다면 장착 요청, 없으면 손을 비우도록 nullptr 요청
			OnEquipRequested.Broadcast(ItemToEquip);
		}
		else
		{
			// 택배 등 수납 불가 아이템은 무시
			UE_LOG(LogTemp, Warning, TEXT("This item cannot be stored in the inventory."));
		}
	}

	// 상태 변경 브로드캐스트 (서버)
	OnInventorySlotChanged.Broadcast(SlotIndex, InventorySlots[SlotIndex]);
	
	// 인벤토리 변경에 따른 캐릭터 총 소지 무게 동기화
	CarryingComp->UpdateCharacterTotalWeight();
}

void UInventoryComponent::ServerRequestSlotAction_Implementation(int32 SlotIndex)
{
	RequestSlotAction(SlotIndex);
}
