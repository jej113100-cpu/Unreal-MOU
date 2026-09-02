#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Item/DeliveryData.h"
#include "WarehouseInitialDataAsset.generated.h"

/** 새 런 시작 시 창고에 최초 1회 지급할 물품 목록입니다. */
UCLASS(BlueprintType)
class TEAMPROJECT_MOU_API UWarehouseInitialDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warehouse", meta = (TitleProperty = "ItemClass"))
	TArray<FStoredItemData> InitialItems;
};
