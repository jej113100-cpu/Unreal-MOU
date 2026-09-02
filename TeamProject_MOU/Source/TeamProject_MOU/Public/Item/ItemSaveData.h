#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "ItemSaveData.generated.h"

class AItemBase;

// 창고/배달 레벨 이동에서 아이템 액터 1개의 상태를 저장하는 데이터입니다.
// 아이템 자식 클래스별 추가 정보는 ExtraSaveData에 별도 구조체로 붙입니다.
USTRUCT(BlueprintType)
struct FStoredItemInstanceData
{
	GENERATED_BODY()

	// 다시 스폰할 때 사용할 아이템 클래스입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage")
	TSubclassOf<AItemBase> ItemClass;

	// 레벨에 실제 액터로 복원할 때 사용할 위치/회전/스케일 정보입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage")
	FTransform Transform = FTransform::Identity;

	// ItemBase가 공통으로 관리하는 현재 사용 횟수입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage")
	int32 CurrentUseCount = 0;

	// ItemBase가 공통으로 관리하는 현재 내구도입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage")
	float CurrentDurability = 0.0f;

	// PackageBase, 퀘스트 패키지, 장비 등 자식 타입이 자기 전용 저장 데이터를 추가하는 공간입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage")
	TArray<FInstancedStruct> ExtraSaveData;

	bool IsValid() const
	{
		return ItemClass != nullptr;
	}
};

USTRUCT(BlueprintType)
struct FStoredInventorySlotData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Save")
	bool bOccupied = false;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Save")
	FStoredItemInstanceData ItemData;
};

USTRUCT(BlueprintType)
struct FPlayerInventorySaveData
{
	GENERATED_BODY()

	// PlayerState 이름을 우선 사용하며 이름이 없으면 PlayerId를 대체 키로 사용합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Save")
	FString PlayerKey;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Save")
	TArray<FStoredInventorySlotData> Slots;
};
