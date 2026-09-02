// Fill out your copyright notice in the Description page of Project Settings.


#include "Base/ProjectGameStateBase.h"

#include "Economy/EconomyDebtProcessor.h"
#include "Net/UnrealNetwork.h"

AProjectGameStateBase::AProjectGameStateBase()
{
	Gold = 1000;
	Reputation = 0;

	InitialDebt = 500;
	CurrentDebt = InitialDebt;

	BaseDebtIncrease = 250;
	DebtGrowthDivisor = 8.0f;

	DebtCycle = 1;

	EconomyCurrentHalfDay = 0;
	DebtPeriodHalfDay = 14;
	DebtCycleStartHalfDay = 0;
}

// ==============================================
// Gold
// ==============================================

void AProjectGameStateBase::AddGold(int32 Amount)
{
	// 클라이언트가 임의로 골드를 변경하지 못하도록 서버에서만 처리합니다.
	// GameState가 골드 상태의 권한을 가집니다.
	if (!HasAuthority())
	{
		return;
	}

	// 0 이하 금액은 AddGold에서 처리하지 않습니다.
	// 골드 차감은 SpendGold를 사용하도록 책임을 분리합니다.
	if (Amount <= 0)
	{
		return;
	}

	// 현재 골드에 금액 추가
	Gold += Amount;


	// 서버에서 변경된 골드 값을 Blueprint에 알립니다.
	// HUD 등의 UI 갱신에 사용할 수 있습니다.
	OnGoldUpdated(Gold);
}

bool AProjectGameStateBase::SpendGold(int32 Amount)
{
	// 서버에서만 골드 차감 처리
	if (!HasAuthority())
	{
		return false;
	}

	// 0 이하 금액은 사용하지 않음
	if (Amount <= 0)
	{
		return false;
	}

	// 현재 골드가 Amount보다 적으면 사용 실패
	if (!CanAfford(Amount))
	{
		return false;
	}

	// 골드 차감 처리
	Gold -= Amount;

	// UI 등에 변경된 골드 전달
	OnGoldUpdated(Gold);


	return true;
}

// 지정 금액을 지불할 수 있는지 확인하는 함수
bool AProjectGameStateBase::CanAfford(int32 Amount) const
{
	return Amount > 0 && Gold >= Amount;
}

void AProjectGameStateBase::SetGold(int32 NewGold)
{
	if (!HasAuthority())
	{
		return;
	}

	Gold = FMath::Max(0, NewGold);

	OnGoldUpdated(Gold);
}

// ==============================================
// Reputation
// ==============================================

void AProjectGameStateBase::AddReputation(int32 Amount)
{
	if (!HasAuthority())
	{
		return;
	}

	Reputation = FMath::Clamp(Reputation + Amount, -100, 100);

	OnReputationUpdated(Reputation);
}

void AProjectGameStateBase::SetReputation(int32 NewReputation)
{
	if (!HasAuthority())
	{
		return;
	}

	Reputation = FMath::Clamp(NewReputation, -100, 100);

	OnReputationUpdated(Reputation);
}

// ==============================================
// Debt
// ==============================================

// 증가량 = BaseDebtIncrease × (1 + ((DebtCycle - 1)² / DebtGrowthDivisor))
int32 AProjectGameStateBase::CalculateDebtIncrease() const
{
	const float CycleValue = static_cast<float>(FMath::Max(0, DebtCycle - 1));

	const float SafeDivisor = FMath::Max(0.01f, DebtGrowthDivisor);

	const float GrowthFactor = 1.0f + (FMath::Square(CycleValue) / SafeDivisor);

	const float Increase = static_cast<float>(BaseDebtIncrease) * GrowthFactor;

	return FMath::RoundToInt(Increase);
}

// 현재 빚 + 이번 증가량
int32 AProjectGameStateBase::CalculateNextDebt() const
{
	return CurrentDebt + CalculateDebtIncrease();
}

bool AProjectGameStateBase::PayDebt()
{
	if (!HasAuthority())
	{
		return false;
	}

	// 현재 빚만큼 골드가 부족하면 상환 실패
	if (!SpendGold(CurrentDebt))
	{
		return false;
	}

	// 현재 DebtCycle 기준으로 다음 빚 금액 계산
	const int32 NextDebt = CalculateNextDebt();

	// 상환 성공 후 다음 회차로 이동
	DebtCycle++;
	// 다음 회차 빚 설정
	CurrentDebt = NextDebt;
	// 상환 성공 시 새 빚 회차 시작
	DebtCycleStartHalfDay = EconomyCurrentHalfDay;

	// 변경 환료 후 BP/UI에 알림
	OnDebtCycleUpdated(DebtCycle);
	OnDebtUpdated(CurrentDebt);

	return true;
}

void AProjectGameStateBase::SetCurrentDebt(int32 NewDebt)
{
	if (!HasAuthority())
	{
		return;
	}

	CurrentDebt = FMath::Max(0, NewDebt);

	OnDebtUpdated(CurrentDebt);
}

void AProjectGameStateBase::SetDebtCycle(int32 NewDebtCycle)
{
	if (!HasAuthority())
	{
		return;
	}

	DebtCycle = FMath::Max(1, NewDebtCycle);

	OnDebtCycleUpdated(DebtCycle);
}

EDebtProcessResult AProjectGameStateBase::ProcessDebtDeadline()
{
	return FEconomyDebtProcessor::Process(this);
}

// ==============================================
// Economy Time
// ==============================================

// 경제 시간 1 HalfDay 증가
void AProjectGameStateBase::AdvanceEconomyHalfDay()
{
	if (!HasAuthority())
	{
		return;
	}

	EconomyCurrentHalfDay++;

	OnEconomyHalfDayUpdated(EconomyCurrentHalfDay);
}

// 현재 경제 HalfDay 반환
int32 AProjectGameStateBase::GetEconomyCurrentHalfDay() const
{
	return EconomyCurrentHalfDay;
}

// 경제 시간을 직접 설정
void AProjectGameStateBase::SetEconomyCurrentHalfDay(int32 NewHalfDay)
{
	if (!HasAuthority())
	{
		return;
	}

	EconomyCurrentHalfDay = FMath::Max(0, NewHalfDay);

	OnEconomyHalfDayUpdated(EconomyCurrentHalfDay);
}

// 다음 빚 상환 HalfDay 반환
int32 AProjectGameStateBase::GetNextDebtDueHalfDay() const
{
	return DebtCycleStartHalfDay + DebtPeriodHalfDay;
}

// 다음 빚 상환 기한에 도달했는지 확인
bool AProjectGameStateBase::IsDebtDue() const
{
	return EconomyCurrentHalfDay >= GetNextDebtDueHalfDay();
}

// 빚 상환까지 남은 HalfDay
int32 AProjectGameStateBase::GetRemainingDebtHalfDay() const
{
	return FMath::Max(0,GetNextDebtDueHalfDay() - EconomyCurrentHalfDay);
}

// ==============================================
// RepNotify
// ==============================================

void AProjectGameStateBase::OnRep_Gold()
{
	OnGoldUpdated(Gold);
}

void AProjectGameStateBase::OnRep_Reputation()
{
	OnReputationUpdated(Reputation);
}
void AProjectGameStateBase::OnRep_CurrentDebt()
{
	OnDebtUpdated(CurrentDebt);
}

void AProjectGameStateBase::OnRep_DebtCycle()
{
	OnDebtCycleUpdated(DebtCycle);
}

void AProjectGameStateBase::OnRep_EconomyCurrentHalfDay()
{
	OnEconomyHalfDayUpdated(EconomyCurrentHalfDay);
}

// ==============================================
// Replication
// ==============================================

void AProjectGameStateBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AProjectGameStateBase, Gold);
	DOREPLIFETIME(AProjectGameStateBase, Reputation);
	DOREPLIFETIME(AProjectGameStateBase, CurrentDebt);
	DOREPLIFETIME(AProjectGameStateBase, DebtCycle);
	DOREPLIFETIME(AProjectGameStateBase, EconomyCurrentHalfDay);
	DOREPLIFETIME(AProjectGameStateBase, DebtCycleStartHalfDay);
}


