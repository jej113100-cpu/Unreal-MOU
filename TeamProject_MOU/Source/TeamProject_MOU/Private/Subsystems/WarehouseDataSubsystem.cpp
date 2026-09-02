#include "Subsystems/WarehouseDataSubsystem.h"

#include "Base/ItemBase.h"
#include "Base/PackageBase.h"
#include "Base/ProjectGameInstanceBase.h"
#include "Components/WarehouseComponent.h"
#include "Components/InventoryComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Item/PackageItemSaveData.h"
#include "Item/WarehouseInitialDataAsset.h"
#include "Player/MainCharacter.h"

void UWarehouseDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	InitializeWarehouseFromDataAsset();
}

void UWarehouseDataSubsystem::InitializeWarehouseFromDataAsset()
{
	UProjectGameInstanceBase* GameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance());
	if (!GameInstance || GameInstance->bWarehouseInitialized || !GameInstance->InitialWarehouseData) return;

	TArray<FStoredItemData> InitialItems;
	TArray<FStoredItemInstanceData> InitialInstances;
	for (const FStoredItemData& ConfiguredItem : GameInstance->InitialWarehouseData->InitialItems)
	{
		if (!ConfiguredItem.ItemClass || ConfiguredItem.Quantity <= 0) continue;
		InitialItems.Add(ConfiguredItem);

		const AItemBase* ItemDefaults = ConfiguredItem.ItemClass->GetDefaultObject<AItemBase>();
		for (int32 Count = 0; Count < ConfiguredItem.Quantity; ++Count)
		{
			FStoredItemInstanceData Instance;
			Instance.ItemClass = ConfiguredItem.ItemClass;
			if (ItemDefaults)
			{
				Instance.CurrentUseCount = ItemDefaults->MaxUseCount;
				Instance.CurrentDurability = ItemDefaults->MaxDurability;
				if (const APackageBase* PackageDefaults = Cast<APackageBase>(ItemDefaults))
				{
					FPackageItemSaveData PackageData;
					PackageData.BaseValue = PackageDefaults->BaseValue;
					PackageData.PackageType = PackageDefaults->PackageType;
					PackageData.MaxSpoilTime = PackageDefaults->MaxSpoilTime;
					PackageData.CurrentSpoilTime = PackageDefaults->MaxSpoilTime;
					PackageData.bIsBroken = false;
					Instance.ExtraSaveData.Add(FInstancedStruct::Make(PackageData));
				}
			}
			InitialInstances.Add(MoveTemp(Instance));
		}
	}

	GameInstance->SaveStoredItems(InitialItems);
	GameInstance->SaveStoredItemInstances(InitialInstances);
	UE_LOG(LogTemp, Log, TEXT("[Warehouse] Initial data asset applied. Classes=%d Instances=%d"),
		InitialItems.Num(), InitialInstances.Num());
}

void UWarehouseDataSubsystem::SaveStoredItems(const TArray<FStoredItemData>& InStoredItems)
{
	if (UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		ProjectGameInstance->SaveStoredItems(InStoredItems);
	}
}

void UWarehouseDataSubsystem::SaveStoredItemInstances(const TArray<FStoredItemInstanceData>& InStoredItemInstances)
{
	if (UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		ProjectGameInstance->SaveStoredItemInstances(InStoredItemInstances);
	}
}

TArray<FStoredItemData> UWarehouseDataSubsystem::GetStoredItemsCopy() const
{
	return GetStoredItemsInternal();
}

TArray<FStoredItemInstanceData> UWarehouseDataSubsystem::GetStoredItemInstancesCopy() const
{
	return GetStoredItemInstancesInternal();
}

void UWarehouseDataSubsystem::ClearStoredItems()
{
	if (UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		ProjectGameInstance->ClearStoredItems();
	}
}

bool UWarehouseDataSubsystem::SaveFromWarehouseComponent(const UWarehouseComponent* WarehouseComponent)
{
	if (!WarehouseComponent)
	{
		return false;
	}

	const TArray<FStoredItemInstanceData> LiveInstances = WarehouseComponent->BuildStoredItemInstanceData();
	const TArray<FStoredItemInstanceData> PreviousInstances = GetStoredItemInstancesInternal();
	TArray<FStoredItemInstanceData> MergedInstances;
	TSet<int32> UsedLiveIndices;
	TSet<int32> UsedPreviousIndices;

	// 현재 요약 수량이 최종 기준입니다. 실제 액터 데이터가 있으면 최신 상태를 쓰고,
	// 로비 재진입 시 아직 액터로 복원되지 않은 물품은 이전 개별 저장 상태를 유지합니다.
	for (const FStoredItemData& StoredItem : WarehouseComponent->StoredItems)
	{
		if (!StoredItem.ItemClass || StoredItem.Quantity <= 0) continue;

		int32 SavedCount = 0;
		for (int32 Index = 0; Index < LiveInstances.Num() && SavedCount < StoredItem.Quantity; ++Index)
		{
			if (!UsedLiveIndices.Contains(Index) && LiveInstances[Index].ItemClass == StoredItem.ItemClass)
			{
				MergedInstances.Add(LiveInstances[Index]);
				UsedLiveIndices.Add(Index);
				++SavedCount;
			}
		}

		for (int32 Index = 0; Index < PreviousInstances.Num() && SavedCount < StoredItem.Quantity; ++Index)
		{
			if (!UsedPreviousIndices.Contains(Index) && PreviousInstances[Index].ItemClass == StoredItem.ItemClass)
			{
				MergedInstances.Add(PreviousInstances[Index]);
				UsedPreviousIndices.Add(Index);
				++SavedCount;
			}
		}

		// 요약 데이터만 존재하는 이전 저장도 다음 로비에서 다시 스폰될 수 있게 기본 상태를 보충합니다.
		while (SavedCount < StoredItem.Quantity)
		{
			FStoredItemInstanceData FallbackInstance;
			FallbackInstance.ItemClass = StoredItem.ItemClass;
			if (const AItemBase* ItemDefaults = StoredItem.ItemClass->GetDefaultObject<AItemBase>())
			{
				FallbackInstance.CurrentUseCount = ItemDefaults->MaxUseCount;
				FallbackInstance.CurrentDurability = ItemDefaults->MaxDurability;
				if (const APackageBase* PackageDefaults = Cast<APackageBase>(ItemDefaults))
				{
					FPackageItemSaveData PackageData;
					PackageData.BaseValue = PackageDefaults->BaseValue;
					PackageData.PackageType = PackageDefaults->PackageType;
					PackageData.MaxSpoilTime = PackageDefaults->MaxSpoilTime;
					PackageData.CurrentSpoilTime = PackageDefaults->MaxSpoilTime;
					PackageData.bIsBroken = false;
					FallbackInstance.ExtraSaveData.Add(FInstancedStruct::Make(PackageData));
				}
			}
			MergedInstances.Add(MoveTemp(FallbackInstance));
			++SavedCount;
		}
	}

	SaveStoredItems(WarehouseComponent->StoredItems);
	SaveStoredItemInstances(MergedInstances);
	UE_LOG(LogTemp, Log, TEXT("[Warehouse] Saved component. SummaryClasses=%d LiveInstances=%d PreviousInstances=%d MergedInstances=%d"),
		WarehouseComponent->StoredItems.Num(), LiveInstances.Num(), PreviousInstances.Num(), MergedInstances.Num());
	return true;
}

bool UWarehouseDataSubsystem::MergeFromWarehouseComponent(const UWarehouseComponent* WarehouseComponent)
{
	if (!WarehouseComponent)
	{
		return false;
	}

	LastLootedItems.Reset();
	for (const FStoredItemData& Item : WarehouseComponent->StoredItems)
	{
		if (Item.ItemClass && Item.Quantity > 0)
		{
			LastLootedItems.Add(Item);
		}
	}

	LastPlayerLootResults.Reset();
	for (const TObjectPtr<AItemBase>& Item : WarehouseComponent->StoredItemInstances)
	{
		if (!IsValid(Item)) continue;

		const AMainCharacter* OwnerCharacter = Cast<AMainCharacter>(Item->LastOwner);
		if (!IsValid(OwnerCharacter) || !OwnerCharacter->IsPlayerControlled()) continue;

		const APlayerState* PlayerState = OwnerCharacter->GetPlayerState();
		const int32 PlayerId = PlayerState ? PlayerState->GetPlayerId() : INDEX_NONE;
		const FString PlayerName = PlayerState ? PlayerState->GetPlayerName() : OwnerCharacter->GetName();
		int32 PlayerIndex = LastPlayerLootResults.IndexOfByPredicate(
			[PlayerId, &PlayerName](const FPlayerSettlementData& Player)
			{
				return Player.PlayerId == PlayerId && Player.PlayerName == PlayerName;
			});
		if (PlayerIndex == INDEX_NONE)
		{
			FPlayerSettlementData Player;
			Player.PlayerId = PlayerId;
			Player.PlayerName = PlayerName;
			PlayerIndex = LastPlayerLootResults.Add(MoveTemp(Player));
		}

		FPlayerSettlementData& Player = LastPlayerLootResults[PlayerIndex];
		++Player.LootedItemCount;
		const int32 ItemIndex = Player.LootedItems.IndexOfByPredicate(
			[Item](const FSettlementItemEntry& Entry)
			{
				return Entry.ItemClass == Item->GetClass();
			});
		if (ItemIndex != INDEX_NONE)
		{
			++Player.LootedItems[ItemIndex].Quantity;
		}
		else
		{
			FSettlementItemEntry Entry;
			Entry.ItemClass = Item->GetClass();
			Entry.ItemName = Item->ItemName;
			Entry.ItemIcon = Item->ItemIcon;
			Entry.Quantity = 1;
			Player.LootedItems.Add(MoveTemp(Entry));
		}
	}

	TArray<FStoredItemData> MergedItems = GetStoredItemsInternal();
	TArray<FStoredItemInstanceData> MergedInstances = GetStoredItemInstancesInternal();
	const TArray<FStoredItemInstanceData> IncomingInstances = WarehouseComponent->BuildStoredItemInstanceData();
	TSet<int32> UsedIncomingIndices;

	for (const FStoredItemData& IncomingItem : WarehouseComponent->StoredItems)
	{
		if (!IncomingItem.ItemClass || IncomingItem.Quantity <= 0) continue;

		const int32 ExistingIndex = MergedItems.IndexOfByPredicate(
			[&IncomingItem](const FStoredItemData& StoredItem)
			{
				return StoredItem.ItemClass == IncomingItem.ItemClass;
			});
		if (ExistingIndex != INDEX_NONE)
		{
			MergedItems[ExistingIndex].Quantity += IncomingItem.Quantity;
		}
		else
		{
			MergedItems.Add(IncomingItem);
		}

		int32 AddedInstanceCount = 0;
		for (int32 Index = 0; Index < IncomingInstances.Num() && AddedInstanceCount < IncomingItem.Quantity; ++Index)
		{
			if (!UsedIncomingIndices.Contains(Index) && IncomingInstances[Index].ItemClass == IncomingItem.ItemClass)
			{
				MergedInstances.Add(IncomingInstances[Index]);
				UsedIncomingIndices.Add(Index);
				++AddedInstanceCount;
			}
		}

		while (AddedInstanceCount < IncomingItem.Quantity)
		{
			FStoredItemInstanceData FallbackInstance;
			FallbackInstance.ItemClass = IncomingItem.ItemClass;
			if (const AItemBase* ItemDefaults = IncomingItem.ItemClass->GetDefaultObject<AItemBase>())
			{
				FallbackInstance.CurrentUseCount = ItemDefaults->MaxUseCount;
				FallbackInstance.CurrentDurability = ItemDefaults->MaxDurability;
				if (const APackageBase* PackageDefaults = Cast<APackageBase>(ItemDefaults))
				{
					FPackageItemSaveData PackageData;
					PackageData.BaseValue = PackageDefaults->BaseValue;
					PackageData.PackageType = PackageDefaults->PackageType;
					PackageData.MaxSpoilTime = PackageDefaults->MaxSpoilTime;
					PackageData.CurrentSpoilTime = PackageDefaults->MaxSpoilTime;
					PackageData.bIsBroken = false;
					FallbackInstance.ExtraSaveData.Add(FInstancedStruct::Make(PackageData));
				}
			}
			MergedInstances.Add(MoveTemp(FallbackInstance));
			++AddedInstanceCount;
		}
	}

	SaveStoredItems(MergedItems);
	SaveStoredItemInstances(MergedInstances);
	UE_LOG(LogTemp, Log, TEXT("[Warehouse] Merged map warehouse. IncomingClasses=%d IncomingInstances=%d TotalInstances=%d"),
		WarehouseComponent->StoredItems.Num(), IncomingInstances.Num(), MergedInstances.Num());
	return true;
}

FString UWarehouseDataSubsystem::BuildPlayerInventoryKey(const AActor* PlayerActor) const
{
	const APawn* Pawn = Cast<APawn>(PlayerActor);
	const APlayerState* PlayerState = Pawn ? Pawn->GetPlayerState() : nullptr;
	if (!PlayerState) return FString();

	const FString PlayerName = PlayerState->GetPlayerName();
	if (!PlayerName.IsEmpty())
	{
		return FString::Printf(TEXT("Player:%s"), *PlayerName);
	}
	return FString::Printf(TEXT("PlayerId:%d"), PlayerState->GetPlayerId());
}

int32 UWarehouseDataSubsystem::SaveAllPlayerInventories()
{
	UWorld* World = GetWorld();
	UProjectGameInstanceBase* GameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance());
	if (!World || !GameInstance || World->GetNetMode() == NM_Client) return 0;

	TArray<FPlayerInventorySaveData> SavedInventories;
	int32 SavedItemCount = 0;
	for (TActorIterator<AMainCharacter> It(World); It; ++It)
	{
		AMainCharacter* Character = *It;
		UInventoryComponent* Inventory = Character->GetInventoryComponent();
		const FString PlayerKey = BuildPlayerInventoryKey(Character);
		if (!Inventory || PlayerKey.IsEmpty()) continue;

		FPlayerInventorySaveData PlayerData;
		PlayerData.PlayerKey = PlayerKey;
		PlayerData.Slots.SetNum(Inventory->InventorySlots.Num());
		for (int32 SlotIndex = 0; SlotIndex < Inventory->InventorySlots.Num(); ++SlotIndex)
		{
			AItemBase* Item = Inventory->InventorySlots[SlotIndex];
			if (!IsValid(Item)) continue;

			FStoredInventorySlotData& SlotData = PlayerData.Slots[SlotIndex];
			Item->SaveItemToData(SlotData.ItemData);
			SlotData.bOccupied = SlotData.ItemData.IsValid();
			SavedItemCount += SlotData.bOccupied ? 1 : 0;
		}
		SavedInventories.Add(MoveTemp(PlayerData));
	}

	GameInstance->SavedPlayerInventories = MoveTemp(SavedInventories);
	UE_LOG(LogTemp, Log, TEXT("[Inventory] Saved players=%d items=%d"),
		GameInstance->SavedPlayerInventories.Num(), SavedItemCount);
	return SavedItemCount;
}

bool UWarehouseDataSubsystem::RestorePlayerInventory(UInventoryComponent* InventoryComponent)
{
	if (!InventoryComponent || !InventoryComponent->GetOwner()
		|| !InventoryComponent->GetOwner()->HasAuthority()) return false;

	UWorld* World = InventoryComponent->GetWorld();
	UProjectGameInstanceBase* GameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance());
	const FString PlayerKey = BuildPlayerInventoryKey(InventoryComponent->GetOwner());
	if (!World || !GameInstance || PlayerKey.IsEmpty()) return false;

	const int32 PlayerDataIndex = GameInstance->SavedPlayerInventories.IndexOfByPredicate(
		[&PlayerKey](const FPlayerInventorySaveData& Data) { return Data.PlayerKey == PlayerKey; });
	if (PlayerDataIndex == INDEX_NONE) return false;
	const FPlayerInventorySaveData PlayerData = GameInstance->SavedPlayerInventories[PlayerDataIndex];

	const int32 SlotCount = FMath::Min(InventoryComponent->InventorySlots.Num(), PlayerData.Slots.Num());
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		const FStoredInventorySlotData& SlotData = PlayerData.Slots[SlotIndex];
		if (!SlotData.bOccupied || !SlotData.ItemData.IsValid()) continue;

		const FTransform SpawnTransform(FRotator::ZeroRotator,
			InventoryComponent->GetOwner()->GetActorLocation());
		AItemBase* Item = World->SpawnActorDeferred<AItemBase>(
			SlotData.ItemData.ItemClass, SpawnTransform, InventoryComponent->GetOwner(), nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Item) continue;

		Item->FinishSpawning(SpawnTransform);
		Item->LoadItemFromData(SlotData.ItemData);
		Item->MulticastOnUnequipped(InventoryComponent->GetOwner());
		InventoryComponent->InventorySlots[SlotIndex] = Item;
		InventoryComponent->OnInventorySlotChanged.Broadcast(SlotIndex, Item);
	}

	// 레벨 이동 복원 데이터는 한 번만 소비하여 같은 맵 재스폰 시 아이템이 복제되지 않게 합니다.
	GameInstance->SavedPlayerInventories.RemoveAt(PlayerDataIndex);
	UE_LOG(LogTemp, Log, TEXT("[Inventory] Restored player=%s slots=%d"), *PlayerKey, SlotCount);
	return true;
}

bool UWarehouseDataSubsystem::LoadIntoWarehouseComponent(UWarehouseComponent* WarehouseComponent) const
{
	if (!WarehouseComponent)
	{
		return false;
	}

	// 저장된 요약 데이터만 복원하고, 실제 액터 인스턴스 목록은 새 맵 기준으로 비워 둡니다.
	WarehouseComponent->StoredItems = GetStoredItemsInternal();
	WarehouseComponent->StoredItemInstances.Reset();
	WarehouseComponent->OnWarehouseItemsChanged.Broadcast();
	return true;
}

int32 UWarehouseDataSubsystem::GetStoredQuantity(TSubclassOf<AItemBase> ItemClass) const
{
	if (!ItemClass)
	{
		return 0;
	}

	for (const FStoredItemData& StoredItem : GetStoredItemsInternal())
	{
		if (StoredItem.ItemClass == ItemClass)
		{
			return StoredItem.Quantity;
		}
	}

	return 0;
}

bool UWarehouseDataSubsystem::HasEnoughStoredItems(TSubclassOf<AItemBase> ItemClass, int32 RequiredQuantity) const
{
	return RequiredQuantity > 0 && GetStoredQuantity(ItemClass) >= RequiredQuantity;
}

bool UWarehouseDataSubsystem::CanBuildDeliveryData(const TArray<FStoredItemData>& RequestedItems) const
{
	FDeliveryData DummyData;
	return BuildValidatedDeliveryData(RequestedItems, DummyData);
}

bool UWarehouseDataSubsystem::SavePendingDeliveryDataFromRequest(const TArray<FStoredItemData>& RequestedItems)
{
	FDeliveryData DeliveryData;
	if (!BuildValidatedDeliveryData(RequestedItems, DeliveryData))
	{
		return false;
	}

	if (!ConsumeStoredItemsForDelivery(DeliveryData))
	{
		return false;
	}

	SavePendingDeliveryData(DeliveryData);
	return true;
}

void UWarehouseDataSubsystem::SavePendingDeliveryData(const FDeliveryData& InDeliveryData)
{
	if (UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		ProjectGameInstance->SavePendingDeliveryData(InDeliveryData);
	}
}

FDeliveryData UWarehouseDataSubsystem::GetPendingDeliveryDataCopy() const
{
	if (const UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		return ProjectGameInstance->PendingDeliveryData;
	}

	return FDeliveryData();
}

void UWarehouseDataSubsystem::ClearPendingDeliveryData()
{
	if (UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		ProjectGameInstance->ClearPendingDeliveryData();
	}
}

const TArray<FStoredItemData>& UWarehouseDataSubsystem::GetStoredItemsInternal() const
{
	if (const UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		return ProjectGameInstance->SavedStoredItems;
	}

	static const TArray<FStoredItemData> EmptyItems;
	return EmptyItems;
}

const TArray<FStoredItemInstanceData>& UWarehouseDataSubsystem::GetStoredItemInstancesInternal() const
{
	if (const UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		return ProjectGameInstance->SavedStoredItemInstances;
	}

	static const TArray<FStoredItemInstanceData> EmptyItems;
	return EmptyItems;
}

bool UWarehouseDataSubsystem::BuildValidatedDeliveryData(const TArray<FStoredItemData>& RequestedItems, FDeliveryData& OutDeliveryData) const
{
	OutDeliveryData.SelectedItems.Reset();
	OutDeliveryData.SelectedItemInstances.Reset();

	TSet<int32> UsedInstanceIndices;
	const TArray<FStoredItemInstanceData>& StoredItemInstances = GetStoredItemInstancesInternal();

	for (const FStoredItemData& RequestedItem : RequestedItems)
	{
		if (!RequestedItem.ItemClass || RequestedItem.Quantity <= 0)
		{
			continue;
		}

		// 창고에 없는 수량을 요청하면 전체 요청을 실패 처리합니다.
		if (GetStoredQuantity(RequestedItem.ItemClass) < RequestedItem.Quantity)
		{
			OutDeliveryData.SelectedItems.Reset();
			return false;
		}

		OutDeliveryData.SelectedItems.Add(RequestedItem);

		int32 AddedInstanceCount = 0;
		for (int32 InstanceIndex = 0; InstanceIndex < StoredItemInstances.Num() && AddedInstanceCount < RequestedItem.Quantity; ++InstanceIndex)
		{
			const FStoredItemInstanceData& StoredItemInstance = StoredItemInstances[InstanceIndex];
			if (UsedInstanceIndices.Contains(InstanceIndex) || StoredItemInstance.ItemClass != RequestedItem.ItemClass)
			{
				continue;
			}

			OutDeliveryData.SelectedItemInstances.Add(StoredItemInstance);
			UsedInstanceIndices.Add(InstanceIndex);
			++AddedInstanceCount;
		}

		// 이전 저장 데이터처럼 개별 상태가 없는 경우에도 배달 테스트가 막히지 않도록 클래스 기반 기본 데이터를 채웁니다.
		while (AddedInstanceCount < RequestedItem.Quantity)
		{
			FStoredItemInstanceData FallbackItemInstance;
			FallbackItemInstance.ItemClass = RequestedItem.ItemClass;

			// 개별 저장 이력이 없는 구형/요약 데이터는 클래스 기본 상태로 생성합니다.
			// 구조체의 기본 내구도(0)를 그대로 사용하면 정상 Package도 보상 0원이 됩니다.
			if (const AItemBase* ItemDefaults = RequestedItem.ItemClass->GetDefaultObject<AItemBase>())
			{
				FallbackItemInstance.CurrentUseCount = ItemDefaults->MaxUseCount;
				FallbackItemInstance.CurrentDurability = ItemDefaults->MaxDurability;

				if (const APackageBase* PackageDefaults = Cast<APackageBase>(ItemDefaults))
				{
					FPackageItemSaveData PackageData;
					PackageData.BaseValue = PackageDefaults->BaseValue;
					PackageData.PackageType = PackageDefaults->PackageType;
					PackageData.MaxSpoilTime = PackageDefaults->MaxSpoilTime;
					PackageData.CurrentSpoilTime = PackageDefaults->MaxSpoilTime;
					PackageData.bIsBroken = false;
					FallbackItemInstance.ExtraSaveData.Add(FInstancedStruct::Make(PackageData));
				}
			}
			OutDeliveryData.SelectedItemInstances.Add(FallbackItemInstance);
			++AddedInstanceCount;
		}
	}

	return !OutDeliveryData.IsEmpty();
}

bool UWarehouseDataSubsystem::ConsumeStoredItemsForDelivery(const FDeliveryData& DeliveryData)
{
	UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance());
	if (!ProjectGameInstance || DeliveryData.IsEmpty())
	{
		return false;
	}

	TArray<FStoredItemData> UpdatedStoredItems = ProjectGameInstance->SavedStoredItems;
	for (const FStoredItemData& SelectedItem : DeliveryData.SelectedItems)
	{
		if (!SelectedItem.ItemClass || SelectedItem.Quantity <= 0)
		{
			continue;
		}

		const int32 StoredItemIndex = UpdatedStoredItems.IndexOfByPredicate(
			[&SelectedItem](const FStoredItemData& StoredItem)
			{
				return StoredItem.ItemClass == SelectedItem.ItemClass;
			});

		if (StoredItemIndex == INDEX_NONE || UpdatedStoredItems[StoredItemIndex].Quantity < SelectedItem.Quantity)
		{
			return false;
		}

		UpdatedStoredItems[StoredItemIndex].Quantity -= SelectedItem.Quantity;
		if (UpdatedStoredItems[StoredItemIndex].Quantity <= 0)
		{
			UpdatedStoredItems.RemoveAt(StoredItemIndex);
		}
	}

	TArray<FStoredItemInstanceData> UpdatedStoredItemInstances = ProjectGameInstance->SavedStoredItemInstances;
	for (const FStoredItemInstanceData& SelectedItemInstance : DeliveryData.SelectedItemInstances)
	{
		if (!SelectedItemInstance.ItemClass)
		{
			continue;
		}

		const int32 StoredItemInstanceIndex = UpdatedStoredItemInstances.IndexOfByPredicate(
			[&SelectedItemInstance](const FStoredItemInstanceData& StoredItemInstance)
			{
				return StoredItemInstance.ItemClass == SelectedItemInstance.ItemClass;
			});

		if (StoredItemInstanceIndex != INDEX_NONE)
		{
			UpdatedStoredItemInstances.RemoveAt(StoredItemInstanceIndex);
		}
	}

	ProjectGameInstance->SaveStoredItems(UpdatedStoredItems);
	ProjectGameInstance->SaveStoredItemInstances(UpdatedStoredItemInstances);
	return true;
}
