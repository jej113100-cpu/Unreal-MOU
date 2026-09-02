// Fill out your copyright notice in the Description page of Project Settings.

#include "Base/ProjectGameInstanceBase.h"

#include "Base/ProjectGameStateBase.h"
#include "Engine/World.h"
#include "Subsystems/WarehouseDataSubsystem.h"

void UProjectGameInstanceBase::Init()
{
	Super::Init();

	// ServerTravel / OpenLevel 등으로 새로운 맵을 읽기 직전에 호출되는 델리게이트 등록
	PreLoadMapHandle =
		FCoreUObjectDelegates::PreLoadMap.AddUObject(
			this,
			&UProjectGameInstanceBase::HandlePreLoadedMap
		);

	// 새로운 월드의 맵 로딩이 끝난 직후 호출되는 델리게이트 등록
	PostLoadMapHandle =
		FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
			this,
			&UProjectGameInstanceBase::HandlePostLoadMapWithWorld
		);
}

void UProjectGameInstanceBase::Shutdown()
{
	// GameInstance가 종료될 때 등록했던 델리게이트를 반드시 해제
	if (PreLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
	}

	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	}

	Super::Shutdown();
}

void UProjectGameInstanceBase::SaveStoredItems(const TArray<FStoredItemData>& InStoredItems)
{
	SavedStoredItems = InStoredItems;
	bWarehouseInitialized = true;
}

void UProjectGameInstanceBase::SaveStoredItemInstances(const TArray<FStoredItemInstanceData>& InStoredItemInstances)
{
	SavedStoredItemInstances = InStoredItemInstances;
}

void UProjectGameInstanceBase::ClearStoredItems()
{
	SavedStoredItems.Reset();
	SavedStoredItemInstances.Reset();
}

void UProjectGameInstanceBase::SavePendingDeliveryData(const FDeliveryData& InDeliveryData)
{
	PendingDeliveryData = InDeliveryData;
}

void UProjectGameInstanceBase::ClearPendingDeliveryData()
{
	PendingDeliveryData.SelectedItems.Reset();
	PendingDeliveryData.SelectedItemInstances.Reset();
}

void UProjectGameInstanceBase::HandlePreLoadedMap(const FString& MapName)
{
	// 새로운 맵의 실제 파일 로딩이 시작됨
	MapLoaded = false;
}

void UProjectGameInstanceBase::HandlePostLoadMapWithWorld(UWorld* LoadedWorld)
{
	// 실제 새로운 월드가 정상 생성되었으면 맵 파일 로딩 완료
	// 이것만으로 게임 플레이 준비가 전부 끝난 것은 아니므로,
	// 이후 Blueprint에서 Pawn / GameState / 데이터 복구 상태를 추가 확인
	MapLoaded = (LoadedWorld != nullptr);
}

void UProjectGameInstanceBase::SaveEconomyData()
{
	UWorld* World = GetWorld();

	if (!World)
	{
		return;
	}

	AProjectGameStateBase* ProjectGameState = World->GetGameState<AProjectGameStateBase>();

	if (!ProjectGameState || !ProjectGameState->HasAuthority())
	{
		return;
	}

	// 게임 상태의 Gold / Reputation / Debt / DebtCycle / Economy HalfDay를
	// 레벨 이동 전에 GameInstance에 임시 보관
	SavedGold = ProjectGameState->Gold;
	SavedReputation = ProjectGameState->Reputation;
	SavedDebt = ProjectGameState->CurrentDebt;
	SavedDebtCycle = ProjectGameState->DebtCycle;
	SavedEconomyCurrentHalfDay = ProjectGameState->GetEconomyCurrentHalfDay();
	bHaveSavedEconomyData = true;
	SavedDebtCycleStartHalfDay = ProjectGameState->DebtCycleStartHalfDay;
}

void UProjectGameInstanceBase::LoadEconomyData()
{
	if (!bHaveSavedEconomyData)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AProjectGameStateBase* ProjectGameState = World->GetGameState<AProjectGameStateBase>();
	if (!ProjectGameState || !ProjectGameState->HasAuthority())
	{
		return;
	}

	// 저장해 둔 경제 정보를 새 레벨의 게임 상태에 다시 적용
	ProjectGameState->DebtCycleStartHalfDay = SavedDebtCycleStartHalfDay;
	ProjectGameState->SetGold(SavedGold);
	ProjectGameState->SetReputation(SavedReputation);
	ProjectGameState->SetCurrentDebt(SavedDebt);
	ProjectGameState->SetDebtCycle(SavedDebtCycle);
	ProjectGameState->SetEconomyCurrentHalfDay(SavedEconomyCurrentHalfDay);
}

void UProjectGameInstanceBase::ResetRunData()
{
	SavedGold = 0;
	SavedReputation = 0;
	SavedDebt = 0;
	SavedDebtCycle = 0;
	SavedEconomyCurrentHalfDay = 0;
	bHaveSavedEconomyData = false;
	SavedDebtCycleStartHalfDay = 0;

	ClearStoredItems();
	bWarehouseInitialized = false;
	ClearPendingDeliveryData();
	SavedPlayerInventories.Reset();
	if (UWarehouseDataSubsystem* WarehouseSubsystem = GetSubsystem<UWarehouseDataSubsystem>())
	{
		WarehouseSubsystem->InitializeWarehouseFromDataAsset();
	}
}

