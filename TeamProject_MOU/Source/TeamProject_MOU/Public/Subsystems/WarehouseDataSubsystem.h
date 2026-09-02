#pragma once

#include "CoreMinimal.h"
#include "Game/LevelSettlementState.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Item/DeliveryData.h"
#include "WarehouseDataSubsystem.generated.h"

class AItemBase;
class UWarehouseComponent;
class UInventoryComponent;

UCLASS()
class TEAMPROJECT_MOU_API UWarehouseDataSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	void InitializeWarehouseFromDataAsset();

	// 창고 요약 데이터를 GameInstance에 저장
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Storage")
	void SaveStoredItems(const TArray<FStoredItemData>& InStoredItems);

	// 창고 아이템 개별 상태 데이터를 GameInstance에 저장
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Storage")
	void SaveStoredItemInstances(const TArray<FStoredItemInstanceData>& InStoredItemInstances);

	// 현재 저장된 창고 데이터를 복사본으로 반환
	UFUNCTION(BlueprintPure, Category = "Warehouse|Storage")
	TArray<FStoredItemData> GetStoredItemsCopy() const;

	// 현재 저장된 창고 아이템 개별 상태 데이터를 복사본으로 반환
	UFUNCTION(BlueprintPure, Category = "Warehouse|Storage")
	TArray<FStoredItemInstanceData> GetStoredItemInstancesCopy() const;

	UFUNCTION(BlueprintCallable, Category = "Warehouse|Storage")
	void ClearStoredItems();

	// 창고 컴포넌트의 현재 데이터를 그대로 저장
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Storage")
	bool SaveFromWarehouseComponent(const UWarehouseComponent* WarehouseComponent);

	// 배달/약탈 맵 창고의 물품을 기존 로비 창고 저장 데이터에 추가합니다.
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Storage")
	bool MergeFromWarehouseComponent(const UWarehouseComponent* WarehouseComponent);

	// 가장 최근 맵 창고 병합분, 즉 이번 판 약탈 정산 목록입니다.
	UFUNCTION(BlueprintPure, Category = "Warehouse|Settlement")
	TArray<FStoredItemData> GetLastLootedItemsCopy() const { return LastLootedItems; }

	UFUNCTION(BlueprintPure, Category = "Warehouse|Settlement")
	TArray<FPlayerSettlementData> GetLastPlayerLootResultsCopy() const { return LastPlayerLootResults; }

	// 레벨 이동 직전에 서버에서 호출하여 모든 플레이어의 슬롯 상태를 저장합니다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Persistence")
	int32 SaveAllPlayerInventories();

	// 새 맵에서 InventoryComponent가 준비되면 해당 플레이어의 슬롯을 복원합니다.
	bool RestorePlayerInventory(UInventoryComponent* InventoryComponent);

	// 저장된 창고 데이터를 컴포넌트로 복원
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Storage")
	bool LoadIntoWarehouseComponent(UWarehouseComponent* WarehouseComponent) const;

	UFUNCTION(BlueprintPure, Category = "Warehouse|Storage")
	int32 GetStoredQuantity(TSubclassOf<AItemBase> ItemClass) const;

	UFUNCTION(BlueprintPure, Category = "Warehouse|Storage")
	bool HasEnoughStoredItems(TSubclassOf<AItemBase> ItemClass, int32 RequiredQuantity) const;

	// 요청 목록이 현재 창고 수량 기준으로 배달 가능 상태 검사
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Delivery")
	bool CanBuildDeliveryData(const TArray<FStoredItemData>& RequestedItems) const;

	// UI에서 만든 요청 목록을 검증한 뒤 PendingDeliveryData로 저장.
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Delivery")
	bool SavePendingDeliveryDataFromRequest(const TArray<FStoredItemData>& RequestedItems);

	UFUNCTION(BlueprintCallable, Category = "Warehouse|Delivery")
	void SavePendingDeliveryData(const FDeliveryData& InDeliveryData);

	// 현재 저장된 배달 선택 데이터를 복사본으로 반환
	UFUNCTION(BlueprintPure, Category = "Warehouse|Delivery")
	FDeliveryData GetPendingDeliveryDataCopy() const;

	UFUNCTION(BlueprintCallable, Category = "Warehouse|Delivery")
	void ClearPendingDeliveryData();

private:
	UPROPERTY(Transient)
	TArray<FStoredItemData> LastLootedItems;

	UPROPERTY(Transient)
	TArray<FPlayerSettlementData> LastPlayerLootResults;

	const TArray<FStoredItemData>& GetStoredItemsInternal() const;
	const TArray<FStoredItemInstanceData>& GetStoredItemInstancesInternal() const;
	bool BuildValidatedDeliveryData(const TArray<FStoredItemData>& RequestedItems, FDeliveryData& OutDeliveryData) const;
	bool ConsumeStoredItemsForDelivery(const FDeliveryData& DeliveryData);
	FString BuildPlayerInventoryKey(const AActor* PlayerActor) const;
};
