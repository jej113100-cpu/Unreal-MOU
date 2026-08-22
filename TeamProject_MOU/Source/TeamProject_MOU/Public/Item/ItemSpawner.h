#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ItemSpawner.generated.h"

class AItemBase;
class UDataTable;

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
#pragma endregion

public:
#pragma region [SPAWNER] 스폰 함수
	// [SPAWNER-001] 지정한 행의 아이템을 이 스포너 위치에 스폰 (함수 호출형 기본 오버로드)
	UFUNCTION(BlueprintCallable, Category = "Spawner")
	AItemBase* SpawnItem(FName RowName);

	// [SPAWNER-002] 지정한 행의 아이템을 지정 위치/회전에 스폰 (동적 생성용)
	UFUNCTION(BlueprintCallable, Category = "Spawner")
	AItemBase* SpawnItemAt(FName RowName, FVector Location, FRotator Rotation);
#pragma endregion
};
