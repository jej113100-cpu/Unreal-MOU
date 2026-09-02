// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Item/DeliveryData.h"
#include "ProjectGameInstanceBase.generated.h"

UCLASS()
class TEAMPROJECT_MOU_API UProjectGameInstanceBase : public UGameInstance
{
	GENERATED_BODY()

public:
	// GameInstance가 생성될 때 맵 로딩 감지 델리게이트를 등록
	virtual void Init() override;
	// GameInstance 종료 시 등록했던 맵 로딩 델리게이트를 해제
	virtual void Shutdown() override;

	// 현재 이동 대상 월드의 실제 맵 로딩이 완료되었는지 나타냄.
	// PreLoadMap에서 false, PostLoadMapWithWorld에서 true가 됨.
	UPROPERTY(BlueprintReadOnly, Category = "Loading")
	bool MapLoaded = true;

	// GameInstance BP에서 새 런의 초기 창고 물품 DataAsset을 지정합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Storage|Initial")
	TObjectPtr<class UWarehouseInitialDataAsset> InitialWarehouseData;

	// 레벨 이동 후에도 유지할 창고  데이터
	UPROPERTY(BlueprintReadOnly, Category = "Storage|Save")
	TArray<FStoredItemData> SavedStoredItems;

	// 레벨 이동 후에도 유지할 창고 아이템 개별 상태 데이터
	UPROPERTY(BlueprintReadOnly, Category = "Storage|Save")
	TArray<FStoredItemInstanceData> SavedStoredItemInstances;

	// 초기 창고 물품이 이번 런에서 이미 적용되었는지 확인합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Storage|Save")
	bool bWarehouseInitialized = false;

	// 레벨 이동 후 같은 플레이어의 같은 슬롯에 복원할 인벤토리 상태입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Save")
	TArray<FPlayerInventorySaveData> SavedPlayerInventories;

	// 배달 맵으로 가져갈 아이템
	UPROPERTY(BlueprintReadOnly, Category = "Delivery|Save")
	FDeliveryData PendingDeliveryData;

	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	int32 SavedGold = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	int32 SavedReputation = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	int32 SavedDebt = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	int32 SavedDebtCycle = 0;

	// 맵 이동 전 현재 경제 HalfDay를 임시 저장
	// 새로운 GameState 생성 후 상환 기한 진행 상태를 이어가기 위해 사용
	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	int32 SavedEconomyCurrentHalfDay = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	bool bHaveSavedEconomyData = false;


	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	int32 SavedDebtCycleStartHalfDay = 0;

	UFUNCTION(BlueprintCallable, Category = "Storage|Save")
	void SaveStoredItems(const TArray<FStoredItemData>& InStoredItems);

	UFUNCTION(BlueprintCallable, Category = "Storage|Save")
	void SaveStoredItemInstances(const TArray<FStoredItemInstanceData>& InStoredItemInstances);

	UFUNCTION(BlueprintCallable, Category = "Storage|Save")
	void ClearStoredItems();

	UFUNCTION(BlueprintCallable, Category = "Economy|Save")
	void SaveEconomyData();

	UFUNCTION(BlueprintCallable, Category = "Economy|Save")
	void LoadEconomyData();

	UFUNCTION(BlueprintCallable, Category = "Run")
	void ResetRunData();

	UFUNCTION(BlueprintCallable, Category = "Delivery|Save")
	void SavePendingDeliveryData(const FDeliveryData& InDeliveryData);

	UFUNCTION(BlueprintCallable, Category = "Delivery|Save")
	void ClearPendingDeliveryData();

private:
	// 맵 파일 로딩이 시작되기 직전에 호출
	// 맵 로딩 상태를 false로 변경
	void HandlePreLoadedMap(const FString& MapName);
	// 새로운 월드의 맵 파일 로딩이 끝난 직후 호출
	// 이후 Blueprint에서 Pawn / GameState 등의 플레이 준비 상태를 추가로 확인
	void HandlePostLoadMapWithWorld(UWorld* LoadedWorld);

	// PreLoadMap 델리게이트 해제를 위해 저장하는 핸들
	FDelegateHandle PreLoadMapHandle;
	// PostLoadMapWithWorld 델리게이트 해제를 위해 저장하는 핸들
	FDelegateHandle PostLoadMapHandle;
};

