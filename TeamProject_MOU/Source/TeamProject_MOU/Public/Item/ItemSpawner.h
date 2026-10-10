#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Item/ItemSaveData.h"
#include "ItemSpawner.generated.h"

class AItemBase;
class UDataTable;

USTRUCT()
struct FDeferredDeliveryItemSpawn
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AItemBase> SpawnedItem;

	UPROPERTY()
	FStoredItemInstanceData SaveData;

	UPROPERTY()
	FTransform SpawnTransform;
};

/**
 * AItemSpawner
 * DataTable을 읽어 아이템을 월드에 스폰하는 "공장" 액터.
 * ItemBase를 건드리지 않고, 스폰한 아이템의 public 필드에 표 데이터를 주입한다.
 *
 * 두 가지 사용법:
 *   1) 레벨 배치형 - 레벨에 놓고 RowToSpawn 지정 → BeginPlay에 자기 위치에 스폰 (맵에 아이템 깔기)
 *   2) 함수 호출형 - SpawnItem(RowName, Location)을 상점/보상 등에서 호출 → 동적 스폰
 * 스폰은 서버 권한에서만 (멀티 대응).
 */
UCLASS()
class TEAMPROJECT_MOU_API AItemSpawner : public AActor
{
	GENERATED_BODY()

public:
	AItemSpawner();

protected:
	virtual void BeginPlay() override;

	/** BeginPlay 직후에는 스트리밍된 맵 메쉬의 콜리전이 아직 준비되지 않을 수 있어 자동 스폰을 지연합니다. */
	UFUNCTION()
	void SpawnConfiguredItem();

	FTimerHandle AutoSpawnTimerHandle;

#pragma region [SPAWNER] 설정값
	// 아이템 데이터 테이블 (행 구조 = FItemSpawnRow)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner")
	TObjectPtr<UDataTable> ItemTable;

	// 레벨 배치형: BeginPlay에 스폰할 행 이름. 비워두면 자동 스폰 안 함.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner")
	FName RowToSpawn = NAME_None;

	// BeginPlay에 RowToSpawn을 자동 스폰할지 여부 (레벨 배치형일 때 true)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner")
	bool bAutoSpawnOnBeginPlay = true;

	// 레벨 진입 직후 바닥 콜리전이 준비될 시간을 확보합니다. 0이면 다음 틱에 스폰합니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner", meta = (ClampMin = "0.0", Units = "s"))
	float AutoSpawnDelay = 0.5f;

	// 배달품은 스트리밍 완료 후에도 물리 씬이 안정화될 시간을 확보한 다음 활성화합니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Delivery", meta = (ClampMin = "0.0", Units = "s"))
	float DeliverySpawnDelay = 1.0f;

	// 여러 저장 아이템이 같은 스포너 좌표에서 겹치지 않도록 나선형으로 벌리는 간격입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Delivery", meta = (ClampMin = "0.0", Units = "cm"))
	float DeliverySpawnSpacing = 55.0f;

	// 작은 아이템이 스폰 순간 바닥과 겹쳐 아래로 빠지지 않도록 기준 위치보다 위에서 생성합니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Delivery", meta = (ClampMin = "0.0", Units = "cm"))
	float DeliverySpawnHeightOffset = 25.0f;

	// 확률 스폰 슬롯: 배열 크기 = 슬롯 개수, 각 칸에 DT_Item 행 이름 지정.
	// 스폰 시 이 중 한 칸을 균등 확률(1/N)로 뽑는다. 빈 칸(None)이 뽑히면 아무것도 안 나옴(꽝).
	// 예) 5칸 중 4칸만 채우면 각 20%씩 아이템 + 20% 꽝.
	// 비어 있으면 기존 RowToSpawn 단일 스폰 방식을 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner")
	TArray<FName> SpawnSlots;
#pragma endregion

public:
#pragma region [SPAWNER] 스폰 함수
	// [SPAWNER-000] 사용할 데이터 테이블을 외부에서 지정 (상점 등 함수 호출형에서 표를 갈아끼울 때)
	UFUNCTION(BlueprintCallable, Category = "Spawner")
	void SetItemTable(UDataTable* InItemTable) { ItemTable = InItemTable; }

	// [SPAWNER-001] 지정한 행의 아이템을 이 스포너 위치에 스폰 (함수 호출형 기본 오버로드)
	UFUNCTION(BlueprintCallable, Category = "Spawner")
	AItemBase* SpawnItem(FName RowName);

	// [SPAWNER-002] 지정한 행의 아이템을 지정 위치/회전에 스폰 (동적 생성용)
	UFUNCTION(BlueprintCallable, Category = "Spawner")
	AItemBase* SpawnItemAt(FName RowName, FVector Location, FRotator Rotation);

	// [SPAWNER-003] 저장된 배달품을 Deferred 상태로 만들고 맵 준비 완료 후 활성화합니다.
	UFUNCTION(BlueprintCallable, Category = "Spawner")
	AItemBase* SpawnItemFromSaveData(const FStoredItemInstanceData& ItemSaveData, FVector Location, FRotator Rotation);
#pragma endregion

private:
	// [SPAWNER-006] 동일 위치로 연속 요청된 저장 아이템을 겹치지 않는 나선형 위치로 분산합니다.
	FVector CalculateDeliverySpawnLocation(const FVector& RequestedLocation, const FRotator& Rotation);

	// [SPAWNER-004] 비동기 로딩과 스트리밍 레벨의 로드·가시성 처리가 끝났는지 검사합니다.
	bool IsDeliverySpawnWorldReady() const;

	// [SPAWNER-005] 준비 완료까지 대기한 배달품의 Construction과 BeginPlay를 실행합니다.
	void FinishDeferredDeliverySpawns();

	// [SPAWNER-007] 저장 데이터 복원 중 물리를 정지하고 실제 바닥 높이에 안전하게 배치한 뒤 물리를 활성화합니다.
	void FinalizeDeliverySpawn(AItemBase* SpawnedItem, const FStoredItemInstanceData& SaveData, const FTransform& RequestedTransform);

	UPROPERTY(Transient)
	TArray<FDeferredDeliveryItemSpawn> DeferredDeliverySpawns;

	FTimerHandle DeferredDeliverySpawnTimerHandle;
	float DeliverySpawnEarliestTime = 0.0f;
	int32 DeliverySpawnSequence = 0;
};
