#include "Game/RunState.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

ARunState::ARunState()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
}

void ARunState::SetRunState(ERunPhase NewPhase, ERunEndReason NewReason)
{
	if (!HasAuthority()) return;
	RunPhase = NewPhase;
	RunEndReason = NewReason;
	OnRunStateUpdated(RunPhase, RunEndReason);
	ForceNetUpdate();
}

void ARunState::SetThreatLevel(float NewThreatLevel)
{
	if (!HasAuthority()) return;
	ThreatLevel = FMath::Clamp(NewThreatLevel, 0.0f, 1.0f);
	OnThreatLevelChanged.Broadcast(ThreatLevel);
	OnThreatLevelUpdated(ThreatLevel);
}

ARunState* ARunState::GetRunState(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World) return nullptr;

	for (TActorIterator<ARunState> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ARunState::OnRep_RunPhase()
{
	OnRunStateUpdated(RunPhase, RunEndReason);
}

void ARunState::OnRep_RunEndReason()
{
	OnRunStateUpdated(RunPhase, RunEndReason);
}

void ARunState::OnRep_ThreatLevel()
{
	OnThreatLevelChanged.Broadcast(ThreatLevel);
	OnThreatLevelUpdated(ThreatLevel);
}

void ARunState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARunState, RunPhase);
	DOREPLIFETIME(ARunState, RunEndReason);
	DOREPLIFETIME(ARunState, ThreatLevel);
}
