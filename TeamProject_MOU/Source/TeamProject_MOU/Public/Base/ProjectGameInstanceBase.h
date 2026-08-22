// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "ProjectGameInstanceBase.generated.h"

/**
 * 
 */
UCLASS()
class TEAMPROJECT_MOU_API UProjectGameInstanceBase : public UGameInstance
{
	GENERATED_BODY()

public:
	// ====================================
	// Economy Save Data
	// ====================================

	// 맵 이동 전 GameState의 팀 공용 Gold값 임시 저장
	// GameState는 레벨 변경되면 새로 생성, GameInstance 레벨 이동 후에도 유지
	// 이전 맵의 Gold를 다음 맵으로 넘기기 위해 사용
	// 실제 플레이 중 사용하는 Gold 값은 GameState의 Gold, SavedGold는 맵 이동을 위한 보관용 값
	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	int32 SavedGold = 0;

	// 맵 이동 전 GameState의 팀 공용 Reputation 값을 임시로 저장.
	// Reputation 역시 실제 값은 GameState가 관리,
	// GameInstance에서는 레벨 이동 중 값을 보존하는 용도로만 사용
	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	int32 SavedReputation = 0;

	// ====================================
	// Debt Save Data
	// ====================================

	// 맵 이동 전 GameState의 팀 공용 부채 값을 임시로 저장.
	// GameState는 레벨 변경되면 새로 생성, GameInstance 레벨 이동 후에도 유지
	// 이전 맵의 부채를 다음 맵으로 넘기기 위해 사용
	// 실제 플레이 중 사용하는 부채 값은 GameState의 CurrentDebt, SavedDebt는 맵 이동을 위한 보관용 값
	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	int32 SavedDebt = 0;

	// 맵 이동 전 GameState의 팀 공용 상환 주기를 임시로 저장.
	// GameState는 레벨 변경되면 새로 생성, GameInstance 레벨 이동 후에도 유지
	// 이전 맵의 상환 주기를 다음 맵으로 넘기기 위해 사용
	// 실제 플레이 중 사용하는 부채 상환 주기는 GameState의 DebtCycle, SavedDebtCycle는 맵 이동을 위한 보관용 값
	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	int32 SavedDebtCycle = 0;


	// 경제 데이터가 저장되어있는지 확인
	// 첫 게임 시작 시에는 저장데이터가 없으므로 false, SavedEconmyData 정상적으로 실행하면 true
	// 이후 새로운 GameState가 생성됐을 때 저장된 데이터가 있을 경우에만 복원하기 위해 사용
	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	bool bHaveSavedEconomyData = false;

	// ====================================
	// Economy Save / Load
	// ====================================

	// 현재 GameState의 골드/평판/부채/상환 주기를 GameInstance에 임시 저장
	// 맵 이동 직전에 호출, server의 GameState 값만 저장, client에서 호출, 실제저장 x
	UFUNCTION(BlueprintCallable, Category = "Economy|Save")
	void SaveEconomyData();

	// GameInstance에 보관되어 있는 골드/평판/부채/상환 주기를 현재 맵에 새로운 GameState에 복원
	// 맵 이동 후 새로운 GameState가 만들어지고 
	// 복원된 GameState값은 GameState Replication을 통해 각 client에 자동 전달
	UFUNCTION(BlueprintCallable, Category = "Economy|Save")
	void LoadEconomyData();
};
