#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ItemSpawnRow.generated.h"

class AItemBase;
class UTexture2D;

/**
 * EItemCategory
 * 상점 분류용 아이템 카테고리. 상점 UI에서 탭/필터로 사용한다.
 * 카테고리를 늘리려면 여기에 값만 추가하면 된다(기존 행은 기본값 Etc 유지).
 */
UENUM(BlueprintType)
enum class EItemCategory : uint8
{
	Weapon	UMETA(DisplayName = "무기"),
	Potion	UMETA(DisplayName = "포션"),
	Etc		UMETA(DisplayName = "기타")
};

/**
 * FItemSpawnRow
 * 아이템 DataTable의 한 행(칼럼) 정의. 스포너가 이 표를 읽어 아이템을 찍어낸다.
 * 방식 1(데이터만 DT): 순수 데이터 + 스폰할 클래스만. 동작(GE/Fire 등)은 각 클래스/BP가 가진다.
 * ItemBase는 건드리지 않는다 - 스폰 후 public 필드에 값만 주입.
 */
USTRUCT(BlueprintType)
struct FItemSpawnRow : public FTableRowBase
{
	GENERATED_BODY()

	// 스폰할 아이템 액터 클래스 (예: BP_Potion_Heal, BP_TaserGun). 이 행의 핵심.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TSubclassOf<AItemBase> ItemClass;

	// 상점 분류 카테고리 (무기/포션/기타). 상점 UI에서 탭/필터로 사용. 기본값 Etc.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	EItemCategory Category = EItemCategory::Etc;

	// 표시 이름 (ItemBase.ItemName에 주입)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FText ItemName;

	// 아이콘 (ItemBase.ItemIcon에 주입)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TObjectPtr<UTexture2D> ItemIcon;

	// 무게 (ItemBase.ItemWeight에 주입)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	float ItemWeight = 1.0f;

	// 소비 아이템 여부. false면 영구 아이템(지도 등) - 닳지 않고 사라지지 않음.
	// 표에서 소비/영구를 구분하는 용도. (영구 아이템은 MaxUseCount가 의미 없음)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	bool bIsConsumable = true;

	// 최대 사용 횟수 (ItemBase.MaxUseCount에 주입 → 소비/무기 베이스가 이 값으로 복제 카운트 초기화)
	// 영구 아이템(bIsConsumable=false)에서는 사용되지 않음.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (EditCondition = "bIsConsumable"))
	int32 MaxUseCount = 1;

	// 최대 내구도 (ItemBase.MaxDurability에 주입 → 무기 베이스가 BeginPlay에서 CurrentDurability를 이 값으로 초기화).
	// 무기별로 다르게 잡는 용도(테이저·부메랑 등). 표에서 안 건드리면 각 무기 C++ 기본값과 동일(보통 100).
	// 무기 차감량은 코드(GetDurabilityCostPerUse / DurabilityCostPerHit)가 정하고, 이 값은 총량만 조정한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	float MaxDurability = 100.0f;

	// 상점 판매 가격 (상점/보상 시스템에서 사용)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	int32 Price = 0;
};
