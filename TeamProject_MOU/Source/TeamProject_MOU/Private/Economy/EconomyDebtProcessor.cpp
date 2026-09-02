#include "Economy/EconomyDebtProcessor.h"
#include "Base/ProjectGameStateBase.h"

EDebtProcessResult FEconomyDebtProcessor::Process(
	AProjectGameStateBase* GameState)
{
	// 유효한 GameState인지 확인
	if (!IsValid(GameState))
	{
		return EDebtProcessResult::NotDue;
	}

	// 실제 경제 상태 변경은 서버에서만 처리
	if (!GameState->HasAuthority())
	{
		return EDebtProcessResult::NotDue;
	}

	if (!GameState->IsDebtDue())
	{
		return EDebtProcessResult::NotDue;
	}

	// 이 함수는 OnDebtDeadlineReached에서만 호출
	// 7일차에 현재 시점의 CurrentDebt 강제 상환 시도
	if (GameState->PayDebt())
	{
		return EDebtProcessResult::Paid;
	}

	// 상환 기한에 도달했지만 Gold 부족
	return EDebtProcessResult::Failed;
}