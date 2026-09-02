#include "Game/GameCycleState.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace GameCycle
{
	constexpr int32 EconomyTimePerDay = 2;
	constexpr int32 DaysPerDebtCycle = 7;
	constexpr int32 EconomyTimePerDebtCycle = EconomyTimePerDay * DaysPerDebtCycle;
}

AGameCycleState::AGameCycleState()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
}

void AGameCycleState::InitializeEconomyTime(int32 InitialEconomyTime)
{
	if (!HasAuthority()) return;

	EconomyTime = FMath::Max(0, InitialEconomyTime);
	LastCompletedDay = EconomyTime / GameCycle::EconomyTimePerDay;
	DebtDeadlineCount = EconomyTime / GameCycle::EconomyTimePerDebtCycle;
	ForceNetUpdate();
}

void AGameCycleState::NotifyEconomyTimeAdvanced(int32 NewEconomyTime)
{
	if (!HasAuthority() || NewEconomyTime <= EconomyTime) return;

	const int32 PreviousEconomyTime = EconomyTime;
	EconomyTime = NewEconomyTime;
	OnEconomyTimeChanged.Broadcast(GetCurrentCycleContext());

	for (int32 Time = PreviousEconomyTime + 1; Time <= EconomyTime; ++Time)
	{
		// 상환 마감일(각 7일차)은 일반 하루 경과 콜백 대신 마감 콜백만 발생시킵니다.
		const bool bDebtDeadline = Time > 0
			&& Time % GameCycle::EconomyTimePerDebtCycle == 0;
		if (Time % GameCycle::EconomyTimePerDay == 0 && !bDebtDeadline)
		{
			LastCompletedDay = Time / GameCycle::EconomyTimePerDay;
			OnDayPassed.Broadcast(BuildContext(Time));
		}

		if (bDebtDeadline)
		{
			DebtDeadlineCount = Time / GameCycle::EconomyTimePerDebtCycle;
			OnDebtDeadlineReached.Broadcast(BuildContext(Time));
		}
	}

	ForceNetUpdate();
}

FGameCycleContext AGameCycleState::GetCurrentCycleContext() const
{
	return BuildContext(EconomyTime);
}

AGameCycleState* AGameCycleState::GetGameCycleState(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World) return nullptr;

	for (TActorIterator<AGameCycleState> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

FGameCycleContext AGameCycleState::BuildContext(int32 AtEconomyTime) const
{
	FGameCycleContext Context;
	Context.EconomyTime = FMath::Max(0, AtEconomyTime);
	Context.CompletedDay = Context.EconomyTime / GameCycle::EconomyTimePerDay;
	Context.bDebtDeadline = Context.EconomyTime > 0
		&& Context.EconomyTime % GameCycle::EconomyTimePerDebtCycle == 0;

	const int32 CompletedCycles = Context.EconomyTime / GameCycle::EconomyTimePerDebtCycle;
	Context.DebtCycle = CompletedCycles + (Context.bDebtDeadline ? 0 : 1);
	Context.CompletedDayInCycle = Context.bDebtDeadline
		? GameCycle::DaysPerDebtCycle
		: (Context.EconomyTime % GameCycle::EconomyTimePerDebtCycle) / GameCycle::EconomyTimePerDay;
	Context.RemainingDays = FMath::Max(0, GameCycle::DaysPerDebtCycle - Context.CompletedDayInCycle);
	return Context;
}

void AGameCycleState::OnRep_EconomyTime()
{
	OnEconomyTimeChanged.Broadcast(GetCurrentCycleContext());
}

void AGameCycleState::OnRep_LastCompletedDay()
{
	const FGameCycleContext Context = BuildContext(LastCompletedDay * GameCycle::EconomyTimePerDay);
	if (!Context.bDebtDeadline)
	{
		OnDayPassed.Broadcast(Context);
	}
}

void AGameCycleState::OnRep_DebtDeadlineCount()
{
	OnDebtDeadlineReached.Broadcast(BuildContext(
		DebtDeadlineCount * GameCycle::EconomyTimePerDebtCycle));
}

void AGameCycleState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGameCycleState, EconomyTime);
	DOREPLIFETIME(AGameCycleState, LastCompletedDay);
	DOREPLIFETIME(AGameCycleState, DebtDeadlineCount);
}
