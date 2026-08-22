// Fill out your copyright notice in the Description page of Project Settings.


#include "Base/ProjectGameStateBase.h"

#include "Net/UnrealNetwork.h"

AProjectGameStateBase::AProjectGameStateBase()
{
	Gold = 0;
	Reputation = 0;

	CurrentDebt = 200;
	DebtMultiplier = 1.5f;
	DebtCycle = 1;
}

// ==============================================
// Gold
// ==============================================

void AProjectGameStateBase::AddGold(int32 Amount)
{
	// 클라이언트가 임의로 골드를 수정하지 못하도록 함.
	// GameState의 실제 Gold 값은 서버가 관리
	if (!HasAuthority())
	{
		return;
	}

	// 0 또는 음수 골드는 AddGold로 넣지 않음.
    // 골드 감소는 SpendGold를 사용하도록 역할을 분리.
	if (Amount <= 0)
	{
		return;
	}

	// 실제 팀 골드 증가
	Gold += Amount;


	// 서버 측에서도 Gold 변경 사실을 Blueprint에 알려줌.
	// HUD 등의 UI 갱신에 사용할 수 있음.
	OnGoldUpdated(Gold);
}

bool AProjectGameStateBase::SpendGold(int32 Amount)
{
	// 서버에서만 실제 골드 차감 허용
	if (!HasAuthority())
	{
		return false;
	}

	// 0이나 음수 금액 사용 방지
	if (Amount <= 0)
	{
		return false;
	}

	// 현재 골드가 Amount보다 적으면 구매/지불 실패
	if (!CanAfford(Amount))
	{
		return false;
	}

	// 실제 골드 차감
	Gold -= Amount;

	// UI 등에 변경된 Gold 전달
	OnGoldUpdated(Gold);


	return true;
}

// 돈을 쓰지않고 살수 있는지 검사하는 함수
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

bool AProjectGameStateBase::PayDebt()
{
	if (!HasAuthority())
	{
		return false;
	}

	// 빚을 낼 돈이 부족함
	if (!SpendGold(CurrentDebt))
	{
		return  false;
	}

	// 상환 성공
	DebtCycle++;
	OnDebtCycleUpdated(DebtCycle);

	CurrentDebt = FMath::RoundToInt(CurrentDebt * DebtMultiplier);
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
}
