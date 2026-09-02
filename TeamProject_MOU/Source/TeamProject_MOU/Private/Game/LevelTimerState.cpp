#include "Game/LevelTimerState.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

ALevelTimerState::ALevelTimerState()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
}

void ALevelTimerState::StartTimer(float DurationSeconds)
{
	if (!HasAuthority()) return;

	Snapshot.DurationSeconds = FMath::Max(1.0f, DurationSeconds);
	Snapshot.ServerEndTime = GetSynchronizedServerTime() + Snapshot.DurationSeconds;
	Snapshot.bActive = true;
	Snapshot.bExpired = false;
	OnLevelTimerStateChanged.Broadcast(Snapshot);
	ForceNetUpdate();
}

void ALevelTimerState::ExpireTimer()
{
	if (!HasAuthority() || !Snapshot.bActive) return;

	Snapshot.bActive = false;
	Snapshot.bExpired = true;
	OnLevelTimerStateChanged.Broadcast(Snapshot);
	OnLevelTimerExpired.Broadcast();
	ForceNetUpdate();
}

void ALevelTimerState::StopTimer()
{
	if (!HasAuthority() || !Snapshot.bActive) return;

	Snapshot.bActive = false;
	Snapshot.bExpired = false;
	OnLevelTimerStateChanged.Broadcast(Snapshot);
	ForceNetUpdate();
}

float ALevelTimerState::GetRemainingSeconds() const
{
	if (!Snapshot.bActive) return 0.0f;
	return FMath::Max(0.0f, Snapshot.ServerEndTime - GetSynchronizedServerTime());
}

float ALevelTimerState::GetProgress() const
{
	if (Snapshot.DurationSeconds <= 0.0f) return 0.0f;
	return FMath::Clamp(1.0f - GetRemainingSeconds() / Snapshot.DurationSeconds, 0.0f, 1.0f);
}

ALevelTimerState* ALevelTimerState::GetLevelTimerState(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World) return nullptr;

	for (TActorIterator<ALevelTimerState> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ALevelTimerState::OnRep_Snapshot()
{
	OnLevelTimerStateChanged.Broadcast(Snapshot);
	if (Snapshot.bExpired)
	{
		OnLevelTimerExpired.Broadcast();
	}
}

float ALevelTimerState::GetSynchronizedServerTime() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : 0.0f;
}

void ALevelTimerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ALevelTimerState, Snapshot);
}
