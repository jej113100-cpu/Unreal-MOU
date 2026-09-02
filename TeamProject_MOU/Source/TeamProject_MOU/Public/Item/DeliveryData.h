#pragma once

#include "CoreMinimal.h"
#include "Base/ItemBase.h"
#include "Item/ItemSaveData.h"
#include "DeliveryData.generated.h"

// 창고에 저장된 아이템 한 종류와 수량을 표현하는 데이터입니다.
USTRUCT(BlueprintType)
struct FStoredItemData
{
	GENERATED_BODY()

	// 저장된 아이템의 클래스입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage")
	TSubclassOf<AItemBase> ItemClass;

	// 해당 아이템이 몇 개 저장되어 있는지 나타냅니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage", meta = (ClampMin = "0"))
	int32 Quantity = 0;
};

// 배달 맵으로 가져갈 아이템 목록을 묶어서 전달하는 데이터입니다.
USTRUCT(BlueprintType)
struct FDeliveryData
{
	GENERATED_BODY()

	// 플레이어가 이번 배달에 선택한 아이템 목록입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Delivery")
	TArray<FStoredItemData> SelectedItems;

	// 플레이어가 이번 배달에 선택한 아이템의 개별 상태 목록입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Delivery")
	TArray<FStoredItemInstanceData> SelectedItemInstances;

	// 유효한 배달 아이템이 하나라도 있는지 확인합니다.
	bool IsEmpty() const
	{
		for (const FStoredItemInstanceData& ItemInstance : SelectedItemInstances)
		{
			if (ItemInstance.IsValid())
			{
				return false;
			}
		}

		for (const FStoredItemData& Item : SelectedItems)
		{
			if (Item.ItemClass && Item.Quantity > 0)
			{
				return false;
			}
		}

		return true;
	}
};
