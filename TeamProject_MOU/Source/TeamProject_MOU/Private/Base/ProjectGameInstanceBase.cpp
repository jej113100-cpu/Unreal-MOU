// Fill out your copyright notice in the Description page of Project Settings.


#include "Base/ProjectGameInstanceBase.h"
#include "Base/ProjectGameStateBase.h"
#include "Engine/World.h"

// ==================================
// Save Economy Data
// ==================================
void UProjectGameInstanceBase::SaveEconomyData()
{
	// 현재 world가 존재하지않으면 저장할 수 없기에 종료
	UWorld* World = GetWorld();

	if (!World)
	{
		return;
	}

	// 현재 맵에 사용중인 ProjectGameStateBase 가져옴
	AProjectGameStateBase* ProjectGameState = World->GetGameState<AProjectGameStateBase>();

	if (!ProjectGameState)
	{
		return;
	}

	// 경제 데이터의 실제 값은 Server가 관리하므로 Client의 GameInstance에서는 저장하지 않음.
	if (!ProjectGameState->HasAuthority())
	{
		return;
	}

	// 현재 GameState의 팀 공용 경제 데이터를 레벨 이동 후 살아있는 GamseInstance에 복사
	SavedGold = ProjectGameState->Gold;
	SavedReputation = ProjectGameState->Reputation;
	SavedDebt = ProjectGameState->CurrentDebt;
	SavedDebtCycle = ProjectGameState->DebtCycle;


	// 정상적으로 한번 이상 저장되었음을 기록
	bHaveSavedEconomyData = true;
}

// ==================================
// Load Economy Data
// ==================================

void UProjectGameInstanceBase::LoadEconomyData()
{
	// 아직 한번도 저장한 적이 없다면 처음 게임을 시작한 상황 이므로 복원할 데이터가 없음
	if (!bHaveSavedEconomyData)
	{
		return;
	}

	// 현재 월드 확인
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	
	// 맵 이동 후 새로 생성된 ProjectGameState 가져옴
	AProjectGameStateBase* ProjectGameState = World->GetGameState<AProjectGameStateBase>();
	if (!ProjectGameState)
	{
		return;
	}

	// GameState 값 변경은 Server가 관리하므로 Client의 GameInstance에서는 저장하지 않음.
	// Server에서 복원된 경제 데이터는 GameState Replication을 통해 Client에게 전달
	if (!ProjectGameState->HasAuthority())
	{
		return;
	}


	// GameInstance에 보관되어 있던 경제 데이터를 새 GameState에 다시 적용
	// 값 제한 및 UI 갱신 처리도 함께 수행
	ProjectGameState->SetGold(SavedGold);
	ProjectGameState->SetReputation(SavedReputation);
	ProjectGameState->SetCurrentDebt(SavedDebt);
	ProjectGameState->SetDebtCycle(SavedDebtCycle);
}
