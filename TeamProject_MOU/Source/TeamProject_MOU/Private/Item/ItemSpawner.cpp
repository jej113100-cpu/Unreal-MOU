#include "Item/ItemSpawner.h"
#include "Item/ItemSpawnRow.h"
#include "Base/ItemBase.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

AItemSpawner::AItemSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AItemSpawner::BeginPlay()
{
	Super::BeginPlay();

	// 레벨 배치형: 서버에서만, 지정 행이 있으면 자기 위치에 자동 스폰
	if (HasAuthority() && bAutoSpawnOnBeginPlay && !RowToSpawn.IsNone())
	{
		SpawnItem(RowToSpawn);
	}
}

// [SPAWNER-001] 이 스포너 위치에 스폰
AItemBase* AItemSpawner::SpawnItem(FName RowName)
{
	return SpawnItemAt(RowName, GetActorLocation(), GetActorRotation());
}

// [SPAWNER-002] 지정 위치에 스폰 + 표 데이터 주입
AItemBase* AItemSpawner::SpawnItemAt(FName RowName, FVector Location, FRotator Rotation)
{
	// 스폰은 서버 권한에서만 (스폰된 액터는 클라로 복제됨)
	if (!HasAuthority())
	{
		return nullptr;
	}

	if (!ItemTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ItemSpawner] ItemTable이 지정되지 않음"));
		return nullptr;
	}

	// DataTable에서 행 찾기
	const FString Context = TEXT("ItemSpawner::SpawnItemAt");
	const FItemSpawnRow* Row = ItemTable->FindRow<FItemSpawnRow>(RowName, Context);
	if (!Row)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ItemSpawner] 행 '%s'을(를) 찾을 수 없음"), *RowName.ToString());
		return nullptr;
	}

	if (!Row->ItemClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ItemSpawner] 행 '%s'의 ItemClass가 비어 있음"), *RowName.ToString());
		return nullptr;
	}

	// 아이템 스폰 (Deferred): BeginPlay를 미룬 채로 액터만 먼저 만든다.
	// → 데이터 주입 후 FinishSpawning으로 BeginPlay 실행 → 소비/무기 베이스가
	//   ConsumeUseCount = MaxUseCount 초기화할 때 이미 DT값이 들어가 있음 (타이밍 버그 방지)
	const FTransform SpawnTransform(Rotation, Location);

	AItemBase* SpawnedItem = GetWorld()->SpawnActorDeferred<AItemBase>(
		Row->ItemClass, SpawnTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!SpawnedItem)
	{
		return nullptr;
	}

	// 표 데이터 주입 (ItemBase의 public 필드에 값만 대입 - ItemBase 코드는 안 건드림)
	// BeginPlay 전에 주입되므로 MaxUseCount/MaxDurability가 확정된 뒤 카운트/내구도가 초기화된다.
	SpawnedItem->ItemName = Row->ItemName;
	SpawnedItem->ItemIcon = Row->ItemIcon;
	SpawnedItem->ItemWeight = Row->ItemWeight;
	SpawnedItem->MaxUseCount = Row->MaxUseCount;
	// 무기 베이스는 BeginPlay에서 CurrentDurability = MaxDurability로 초기화하므로,
	// 여기서 MaxDurability를 넣어두면 DT 값이 그대로 시작 내구도가 된다.
	SpawnedItem->MaxDurability = Row->MaxDurability;
	SpawnedItem->CurrentDurability = Row->MaxDurability;

	// 주입 완료 후 BeginPlay 실행
	SpawnedItem->FinishSpawning(SpawnTransform);

	return SpawnedItem;
}
