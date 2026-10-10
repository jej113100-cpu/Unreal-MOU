#include "Item/ItemSpawner.h"
#include "Item/ItemSpawnRow.h"
#include "Base/ItemBase.h"
#include "Engine/DataTable.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"

AItemSpawner::AItemSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AItemSpawner::BeginPlay()
{
	Super::BeginPlay();
	DeliverySpawnEarliestTime = GetWorld()
		? GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, DeliverySpawnDelay)
		: 0.0f;

	// 레벨 배치형: 서버 권한에서만 자동 스폰
	if (!HasAuthority() || !bAutoSpawnOnBeginPlay)
	{
		return;
	}

	// 스트리밍 레벨/레벨 인스턴스의 메쉬 콜리전이 등록되기 전에 물리 아이템이
	// 생성되면 바닥을 통과할 수 있다. 최소한 다음 틱, 기본값으로는 0.5초 뒤에
	// 자동 스폰하여 월드 초기화와 물리 씬 등록이 끝날 시간을 준다.
	if (AutoSpawnDelay > 0.0f)
	{
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			GetWorldTimerManager().SetTimer(
				AutoSpawnTimerHandle, this, &AItemSpawner::SpawnConfiguredItem, AutoSpawnDelay, false);
		}));
	}
	else
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &AItemSpawner::SpawnConfiguredItem);
	}
}

void AItemSpawner::SpawnConfiguredItem()
{
	if (!HasAuthority() || !bAutoSpawnOnBeginPlay)
	{
		return;
	}

	// 확률 슬롯 방식: 슬롯이 있으면 균등 확률(1/N)로 한 칸을 뽑아 스폰.
	// 빈 칸(None)이 뽑히면 아무것도 스폰하지 않는다(꽝).
	if (SpawnSlots.Num() > 0)
	{
		const int32 PickedIndex = FMath::RandRange(0, SpawnSlots.Num() - 1);
		const FName PickedRow = SpawnSlots[PickedIndex];

		if (PickedRow.IsNone())
		{
			// 빈 칸(None)이 뽑힘 = 꽝. 아무것도 스폰하지 않는다.
			return;
		}

		SpawnItem(PickedRow);
		return;
	}

	// 슬롯 미사용 시: 기존 단일 RowToSpawn 방식 (하위 호환)
	if (!RowToSpawn.IsNone())
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

// [SPAWNER-003] 저장된 배달품을 Deferred 상태로 만들고 맵 준비 완료 후 활성화합니다.
AItemBase* AItemSpawner::SpawnItemFromSaveData(const FStoredItemInstanceData& ItemSaveData, FVector Location, FRotator Rotation)
{
	// 스폰은 서버 권한에서만 (스폰된 액터는 클라로 복제됨)
	if (!HasAuthority())
	{
		return nullptr;
	}

	if (!ItemSaveData.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ItemSpawner] 저장 데이터의 ItemClass가 비어 있음"));
		return nullptr;
	}

	// BP_DeliverItemSpawner는 배열의 모든 아이템에 같은 GetActorLocation을 넘긴다.
	// 그대로 활성화하면 물리 바디가 한 점에 겹쳐 작은 아이템이 튕기거나 바닥을 관통하므로
	// 요청 순서에 따라 스포너 주변으로 분산하고 약간 위에서 떨어뜨린다.
	const FVector SafeSpawnLocation = CalculateDeliverySpawnLocation(Location, Rotation);
	const FTransform SpawnTransform(Rotation, SafeSpawnLocation);
	AItemBase* SpawnedItem = GetWorld()->SpawnActorDeferred<AItemBase>(
		ItemSaveData.ItemClass, SpawnTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!SpawnedItem)
	{
		return nullptr;
	}

	if (!IsDeliverySpawnWorldReady())
	{
		FDeferredDeliveryItemSpawn& DeferredSpawn = DeferredDeliverySpawns.AddDefaulted_GetRef();
		DeferredSpawn.SpawnedItem = SpawnedItem;
		DeferredSpawn.SaveData = ItemSaveData;
		DeferredSpawn.SpawnTransform = SpawnTransform;

		if (!GetWorldTimerManager().IsTimerActive(DeferredDeliverySpawnTimerHandle))
		{
			GetWorldTimerManager().SetTimer(
				DeferredDeliverySpawnTimerHandle,
				this,
				&AItemSpawner::FinishDeferredDeliverySpawns,
				0.1f,
				true);
		}

		return SpawnedItem;
	}

	FinalizeDeliverySpawn(SpawnedItem, ItemSaveData, SpawnTransform);

	return SpawnedItem;
}

// [SPAWNER-006] 황금각 나선 배치로 개수와 무관하게 각 아이템 사이의 물리 겹침을 줄입니다.
FVector AItemSpawner::CalculateDeliverySpawnLocation(const FVector& RequestedLocation, const FRotator& Rotation)
{
	const int32 SpawnIndex = DeliverySpawnSequence++;
	const float Radius = DeliverySpawnSpacing * FMath::Sqrt(static_cast<float>(SpawnIndex));
	const float AngleRadians = static_cast<float>(SpawnIndex) * 2.39996323f;
	const FVector LocalOffset(
		FMath::Cos(AngleRadians) * Radius,
		FMath::Sin(AngleRadians) * Radius,
		FMath::Max(0.0f, DeliverySpawnHeightOffset));
	const FRotator YawOnlyRotation(0.0f, Rotation.Yaw, 0.0f);
	return RequestedLocation + YawOnlyRotation.RotateVector(LocalOffset);
}

// [SPAWNER-004] 비동기 로딩과 스트리밍 레벨의 로드·가시성 처리가 끝났는지 검사합니다.
bool AItemSpawner::IsDeliverySpawnWorldReady() const
{
	const UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay() || World->IsInSeamlessTravel() || IsAsyncLoading())
	{
		return false;
	}

	if (World->GetTimeSeconds() < DeliverySpawnEarliestTime || World->IsVisibilityRequestPending())
	{
		return false;
	}

	for (const ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (!StreamingLevel)
		{
			continue;
		}

		if (StreamingLevel->ShouldBeLoaded() && !StreamingLevel->IsLevelLoaded())
		{
			return false;
		}

		if (StreamingLevel->ShouldBeVisible() && !StreamingLevel->IsLevelVisible())
		{
			return false;
		}
	}

	return true;
}

// [SPAWNER-005] 준비 완료까지 대기한 배달품의 Construction과 BeginPlay를 실행합니다.
void AItemSpawner::FinishDeferredDeliverySpawns()
{
	if (!HasAuthority() || !IsDeliverySpawnWorldReady())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(DeferredDeliverySpawnTimerHandle);

	for (FDeferredDeliveryItemSpawn& DeferredSpawn : DeferredDeliverySpawns)
	{
		AItemBase* SpawnedItem = DeferredSpawn.SpawnedItem;
		if (!IsValid(SpawnedItem))
		{
			continue;
		}

		FinalizeDeliverySpawn(SpawnedItem, DeferredSpawn.SaveData, DeferredSpawn.SpawnTransform);
	}

	DeferredDeliverySpawns.Reset();
}

// [SPAWNER-007] 저장 데이터 복원 중 물리를 정지하고 실제 바닥 높이에 안전하게 배치한 뒤 물리를 활성화합니다.
void AItemSpawner::FinalizeDeliverySpawn(
	AItemBase* SpawnedItem,
	const FStoredItemInstanceData& SaveData,
	const FTransform& RequestedTransform)
{
	if (!IsValid(SpawnedItem))
	{
		return;
	}

	SpawnedItem->FinishSpawning(RequestedTransform);

	UStaticMeshComponent* ItemMesh = SpawnedItem->MeshComponent;
	if (ItemMesh)
	{
		// BeginPlay에서 켜진 물리가 저장 Transform을 적용하는 순간 계산되지 않도록 즉시 정지합니다.
		ItemMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		ItemMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		ItemMesh->SetSimulatePhysics(false);
		ItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 사용 횟수와 내구도 등 저장 상태를 먼저 복원합니다. 여기서 저장 당시 Transform도 잠시 적용됩니다.
	SpawnedItem->LoadItemFromData(SaveData);

	// 배달맵에서는 저장 위치를 사용하지 않고, 스포너가 계산한 XY/회전으로 되돌립니다.
	SpawnedItem->SetActorLocationAndRotation(
		RequestedTransform.GetLocation(),
		RequestedTransform.Rotator(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	FVector FinalLocation = RequestedTransform.GetLocation();
	if (ItemMesh && GetWorld())
	{
		ItemMesh->UpdateBounds();
		// 액터 피벗이 메시 중앙에 있다는 보장이 없으므로, 피벗에서 메시 최하단까지의 실제 오프셋을 사용합니다.
		// BP_Map처럼 피벗/상대 위치가 치우친 메시에서 BoxExtent만 더하면 공중에 뜨는 현상이 발생합니다.
		const float MeshBottomWorldZ = ItemMesh->Bounds.Origin.Z - ItemMesh->Bounds.BoxExtent.Z;
		const float ActorToMeshBottomOffset =
			MeshBottomWorldZ - SpawnedItem->GetActorLocation().Z;

		// 스포너가 바닥과 조금 겹쳐 있거나 위에 떠 있어도 실제 WorldStatic 표면을 찾아 배치합니다.
		const FVector TraceStart = FinalLocation + FVector(0.0f, 0.0f, 300.0f);
		const FVector TraceEnd = FinalLocation - FVector(0.0f, 0.0f, 2000.0f);
		FCollisionObjectQueryParams ObjectQueryParams;
		ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DeliveryItemGroundTrace), false, SpawnedItem);
		QueryParams.AddIgnoredActor(this);

		FHitResult GroundHit;
		if (GetWorld()->LineTraceSingleByObjectType(
			GroundHit,
			TraceStart,
			TraceEnd,
			ObjectQueryParams,
			QueryParams))
		{
			constexpr float GroundClearance = 3.0f;
			FinalLocation.Z = GroundHit.ImpactPoint.Z + GroundClearance - ActorToMeshBottomOffset;
		}
	}

	SpawnedItem->SetActorLocationAndRotation(
		FinalLocation,
		RequestedTransform.Rotator(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	if (ItemMesh)
	{
		ItemMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
		ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		ItemMesh->SetSimulatePhysics(true);
		ItemMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		ItemMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		ItemMesh->WakeRigidBody();
	}

	SpawnedItem->ForceNetUpdate();
}
