#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Item/DeliveryData.h"
#include "WarehouseComponent.generated.h"

class AActor;
class AItemBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWarehouseItemsChanged);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UWarehouseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWarehouseComponent();

	// 창고에 들어온 아이템 수량 및 종류
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Warehouse")
	TArray<FStoredItemData> StoredItems;

	// 현재 창고 영역 안에 있는 아이템
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Warehouse")
	TArray<TObjectPtr<AItemBase>> StoredItemInstances;

	// 창고 내용이 바뀌었을 때 UI나 외부 BP가 갱신할 수 있도록 알립니다.
	UPROPERTY(BlueprintAssignable, Category = "Warehouse")
	FOnWarehouseItemsChanged OnWarehouseItemsChanged;

	UFUNCTION(BlueprintCallable, Category = "Warehouse")
	bool CanStoreItemClass(TSubclassOf<AItemBase> ItemClass) const;

	UFUNCTION(BlueprintCallable, Category = "Warehouse")
	bool CanStoreItemInstance(const AItemBase* ItemInstance) const;

	UFUNCTION(BlueprintCallable, Category = "Warehouse")
	bool AddStoredItem(TSubclassOf<AItemBase> ItemClass, int32 Quantity = 1);

	UFUNCTION(BlueprintCallable, Category = "Warehouse")
	bool AddStoredItemFromInstance(const AItemBase* ItemInstance, int32 Quantity = 1);

	UFUNCTION(BlueprintCallable, Category = "Warehouse")
	bool RemoveStoredItem(TSubclassOf<AItemBase> ItemClass, int32 Quantity = 1);

	UFUNCTION(BlueprintCallable, Category = "Warehouse")
	int32 GetStoredQuantity(TSubclassOf<AItemBase> ItemClass) const;

	UFUNCTION(BlueprintCallable, Category = "Warehouse")
	bool ContainsItem(TSubclassOf<AItemBase> ItemClass) const;

	UFUNCTION(BlueprintCallable, Category = "Warehouse")
	void ClearStoredItems();

	// 현재 창고 영역 안의 실제 아이템 액터들을 개별 저장 데이터로 변환
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Persistence")
	TArray<FStoredItemInstanceData> BuildStoredItemInstanceData() const;

	// 창고 영역에 아이템 들어왓을시 콜백함수
	UFUNCTION(BlueprintCallable, Category = "Warehouse")
	bool HandleActorEnteredWarehouse(AActor* OtherActor);

	// 창고 영역에서 아이템  나갔을때 콜백함수
	UFUNCTION(BlueprintCallable, Category = "Warehouse")
	bool HandleActorExitedWarehouse(AActor* OtherActor);

	// 현재 창고 요약 데이터를 게임 인스턴스 서브시스템에 저장
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Persistence")
	bool SaveStoredItemsToGameInstance();

	// 게임 인스턴스 서브시스템에 저장된 창고 데이터 불.러오기	UFUNCTION(BlueprintCallable, Category = "Warehouse|Persistence")
	bool LoadStoredItemsFromGameInstance();

	// 요청된 아이템 목록이 현재 창고 수량으로 가능한지 검사
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Delivery")
	bool BuildDeliveryData(const TArray<FStoredItemData>& RequestedItems, FDeliveryData& OutDeliveryData) const;

private:
	int32 FindStoredItemIndex(TSubclassOf<AItemBase> ItemClass) const;
	void BroadcastWarehouseChanged();
};
