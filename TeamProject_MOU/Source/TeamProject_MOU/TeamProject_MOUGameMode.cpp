// Copyright Epic Games, Inc. All Rights Reserved.

#include "TeamProject_MOUGameMode.h"

#include "Base/ItemBase.h"
#include "Base/ProjectGameInstanceBase.h"
#include "Base/ProjectGameStateBase.h"
#include "EngineUtils.h"
#include "Game/RunState.h"
#include "Game/GameCycleState.h"
#include "Game/LevelTimerConfigDataAsset.h"
#include "Game/LevelTimerState.h"
#include "Game/LevelSettlementState.h"
#include "Delivery/DeliveryManager.h"
#include "Player/MainCharacter.h"
#include "Subsystems/WarehouseDataSubsystem.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

ATeamProject_MOUGameMode::ATeamProject_MOUGameMode()
{
}

void ATeamProject_MOUGameMode::InitGameState()
{
	Super::InitGameState();
	RunState = GetWorld()->SpawnActor<ARunState>();
	GameCycleState = GetWorld()->SpawnActor<AGameCycleState>();
	LevelTimerState = GetWorld()->SpawnActor<ALevelTimerState>();
	LevelSettlementState = GetWorld()->SpawnActor<ALevelSettlementState>();
	DeliveryManager = GetWorld()->SpawnActor<ADeliveryManager>();
	if (AProjectGameStateBase* State = GetGameState<AProjectGameStateBase>())
	{
		GameCycleState->InitializeEconomyTime(State->GetEconomyCurrentHalfDay());
	}
}

void ATeamProject_MOUGameMode::BeginPlay()
{
	Super::BeginPlay();
	TryStartLevelTimer();
}

void ATeamProject_MOUGameMode::AdvanceHalfDay()
{
	AProjectGameStateBase* State = GetGameState<AProjectGameStateBase>();
	if (!State || !RunState || !GameCycleState || RunState->RunPhase != ERunPhase::Playing) return;

	// 레벨 이동으로 복구된 경제 시간은 과거 일일 콜백을 다시 발생시키지 않습니다.
	const int32 PreviousEconomyTime = State->GetEconomyCurrentHalfDay();
	if (GameCycleState->EconomyTime != PreviousEconomyTime)
	{
		GameCycleState->InitializeEconomyTime(PreviousEconomyTime);
	}

	State->AdvanceEconomyHalfDay();
	GameCycleState->NotifyEconomyTimeAdvanced(State->GetEconomyCurrentHalfDay());
}

void ATeamProject_MOUGameMode::NotifyPlayerDeath()
{
	if (bKillingPlayersForLevelTimeout) return;
	CheckAllPlayersDead();
}

void ATeamProject_MOUGameMode::NotifyDebtPaymentFailed()
{
	FinishRun(ERunEndReason::DebtPaymentFailed);
}

bool ATeamProject_MOUGameMode::NotifyLevelSettlement(const FLevelSettlementData& Result)
{
	FLevelSettlementData EnrichedResult = Result;
	EnrichSettlementData(EnrichedResult);
	if (!LevelSettlementState || !LevelSettlementState->FinalizeSettlement(EnrichedResult))
	{
		return false;
	}

	GetWorldTimerManager().ClearTimer(LevelTimerUpdateHandle);
	if (LevelTimerState)
	{
		LevelTimerState->StopTimer();
	}
	return true;
}

void ATeamProject_MOUGameMode::TryStartLevelTimer()
{
	if (!LevelTimerConfig || !LevelTimerState) return;

	const FString CurrentMapName = UGameplayStatics::GetCurrentLevelName(this, true);
	float TimeLimitSeconds = 0.0f;
	if (!LevelTimerConfig->FindTimeLimitForMap(CurrentMapName, TimeLimitSeconds))
	{
		return;
	}

	LevelTimerState->StartTimer(TimeLimitSeconds);
	GetWorldTimerManager().SetTimer(LevelTimerUpdateHandle, this,
		&ATeamProject_MOUGameMode::UpdateLevelTimer,
		LevelTimerUpdateInterval, true);
}

void ATeamProject_MOUGameMode::UpdateLevelTimer()
{
	if (!RunState || !LevelTimerState || !LevelTimerState->Snapshot.bActive) return;

	RunState->SetThreatLevel(LevelTimerState->GetProgress());
	if (LevelTimerState->GetRemainingSeconds() <= 0.0f)
	{
		GetWorldTimerManager().ClearTimer(LevelTimerUpdateHandle);
		LevelTimerState->ExpireTimer();
		KillAllPlayersByTimeLimit();
	}
}

void ATeamProject_MOUGameMode::KillAllPlayersByTimeLimit()
{
	FinalizeFailedSettlement(ELevelSettlementReason::TimeExpired);
	bKillingPlayersForLevelTimeout = true;
	for (TActorIterator<AMainCharacter> It(GetWorld()); It; ++It)
	{
		AMainCharacter* Character = *It;
		if (!Character->IsPlayerControlled() || Character->bIsDead) continue;

		// 기존 사망 Ability/드롭 흐름을 그대로 사용하되 그로기 단계를 건너뜁니다.
		Character->DownCount = FMath::Max(1, Character->DownCount);
		Character->HandleHealthZero();
	}
	bKillingPlayersForLevelTimeout = false;

	BeginLevelTimeoutSequence();
}

void ATeamProject_MOUGameMode::BeginLevelTimeoutSequence()
{
	if (!HasAuthority() || !RunState
		|| RunState->RunPhase == ERunPhase::GameOver
		|| RunState->RunPhase == ERunPhase::Resetting)
	{
		return;
	}

	// UI 연출이 끝날 때까지 현재 레벨에 머뭅니다. 아이템은 저장하지 않고 모두 유실시킵니다.
	RunState->SetRunState(ERunPhase::GameOver, ERunEndReason::LevelTimeExpired);
	GetWorldTimerManager().ClearTimer(LevelTimerUpdateHandle);
	DestroyPlayerOwnedItems();
}

void ATeamProject_MOUGameMode::CompleteLevelTimeoutSequence()
{
	if (!HasAuthority() || bTimeoutTravelStarted || !RunState)
	{
		return;
	}

	if (RunState->RunPhase != ERunPhase::GameOver
		|| RunState->RunEndReason != ERunEndReason::LevelTimeExpired)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("CompleteLevelTimeoutSequence ignored: the run did not end by level timeout."));
		return;
	}

	bTimeoutTravelStarted = true;
	TravelToLobbyAfterWipe();
}

void ATeamProject_MOUGameMode::CheckAllPlayersDead()
{
	bool bFoundPlayer = false;
	for (TActorIterator<AMainCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsPlayerControlled()) continue;
		bFoundPlayer = true;
		if (!It->bIsDead) return;
	}
	if (bFoundPlayer)
	{
		FinalizeFailedSettlement(ELevelSettlementReason::AllPlayersDead);
		FinishRun(ERunEndReason::AllPlayersDead);
	}
}

void ATeamProject_MOUGameMode::FinalizeFailedSettlement(ELevelSettlementReason Reason)
{
	if (!LevelSettlementState || LevelSettlementState->IsFinalized()) return;

	FLevelSettlementData Result;
	Result.Reason = Reason;
	Result.bSucceeded = false;
	EnrichSettlementData(Result);
	Result.FinalThreatLevel = RunState ? RunState->ThreatLevel : 0.0f;
	if (LevelTimerState)
	{
		Result.LevelPlayTimeSeconds = LevelTimerState->Snapshot.DurationSeconds
			- LevelTimerState->GetRemainingSeconds();
	}
	LevelSettlementState->FinalizeSettlement(Result);
}

void ATeamProject_MOUGameMode::EnrichSettlementData(FLevelSettlementData& Result) const
{
	if (DeliveryManager)
	{
		const FDeliveryProgress& Delivery = DeliveryManager->Progress;
		Result.EarnedGold = Delivery.EarnedGold;
		Result.DeliveredItemCount = Delivery.DeliveredItemCount;
		Result.FailedDeliveryCount = Delivery.BrokenItemCount;
		Result.DeliveredItems = Delivery.DeliveredItems;
		Result.PlayerResults = Delivery.PlayerResults;
	}

	Result.DeathCount = 0;
	Result.KnockdownCount = 0;
	for (TActorIterator<AMainCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsPlayerControlled()) continue;
		const int32 PlayerDeathCount = It->bIsDead ? 1 : 0;
		Result.DeathCount += PlayerDeathCount;
		Result.KnockdownCount += It->KnockdownCount;

		const APlayerState* PlayerState = It->GetPlayerState();
		const int32 PlayerId = PlayerState ? PlayerState->GetPlayerId() : INDEX_NONE;
		const FString PlayerName = PlayerState ? PlayerState->GetPlayerName() : It->GetName();
		int32 PlayerIndex = Result.PlayerResults.IndexOfByPredicate(
			[PlayerId, &PlayerName](const FPlayerSettlementData& Player)
			{
				return Player.PlayerId == PlayerId && Player.PlayerName == PlayerName;
			});
		if (PlayerIndex == INDEX_NONE)
		{
			FPlayerSettlementData Player;
			Player.PlayerId = PlayerId;
			Player.PlayerName = PlayerName;
			PlayerIndex = Result.PlayerResults.Add(MoveTemp(Player));
		}
		Result.PlayerResults[PlayerIndex].KnockdownCount = It->KnockdownCount;
	}

	Result.LootedItems.Reset();
	Result.LootedItemCount = 0;
	if (const UWarehouseDataSubsystem* WarehouseSubsystem =
		GetGameInstance()->GetSubsystem<UWarehouseDataSubsystem>())
	{
		for (const FStoredItemData& LootedItem : WarehouseSubsystem->GetLastLootedItemsCopy())
		{
			if (!LootedItem.ItemClass || LootedItem.Quantity <= 0) continue;

			FSettlementItemEntry Entry;
			Entry.ItemClass = LootedItem.ItemClass;
			Entry.Quantity = LootedItem.Quantity;
			if (const AItemBase* ItemDefaults = LootedItem.ItemClass->GetDefaultObject<AItemBase>())
			{
				Entry.ItemName = ItemDefaults->ItemName;
				Entry.ItemIcon = ItemDefaults->ItemIcon;
			}
			Result.LootedItemCount += Entry.Quantity;
			Result.LootedItems.Add(MoveTemp(Entry));
		}

		for (const FPlayerSettlementData& LootResult : WarehouseSubsystem->GetLastPlayerLootResultsCopy())
		{
			int32 PlayerIndex = Result.PlayerResults.IndexOfByPredicate(
				[&LootResult](const FPlayerSettlementData& Player)
				{
					return Player.PlayerId == LootResult.PlayerId
						&& Player.PlayerName == LootResult.PlayerName;
				});
			if (PlayerIndex == INDEX_NONE)
			{
				PlayerIndex = Result.PlayerResults.Add(LootResult);
			}
			else
			{
				Result.PlayerResults[PlayerIndex].LootedItemCount = LootResult.LootedItemCount;
				Result.PlayerResults[PlayerIndex].LootedItems = LootResult.LootedItems;
			}
		}
	}
}

void ATeamProject_MOUGameMode::FinishRun(ERunEndReason Reason)
{
	if (!RunState || RunState->RunPhase == ERunPhase::GameOver || RunState->RunPhase == ERunPhase::Resetting) return;

	RunState->SetRunState(ERunPhase::GameOver, Reason);
	GetWorldTimerManager().ClearTimer(LevelTimerUpdateHandle);
	DestroyPlayerOwnedItems();

	if (Reason == ERunEndReason::AllPlayersDead || Reason == ERunEndReason::LevelTimeExpired)
	{
		TravelToLobbyAfterWipe();
		return;
	}

	OnRunGameOver(Reason);
	GetWorldTimerManager().SetTimer(ResetTimerHandle, this,
		&ATeamProject_MOUGameMode::ResetRunToDayOne,
		FMath::Max(GameOverResetDelay, KINDA_SMALL_NUMBER), false);
}

void ATeamProject_MOUGameMode::DestroyPlayerOwnedItems()
{
	for (TActorIterator<AItemBase> It(GetWorld()); It; ++It)
	{
		AItemBase* Item = *It;
		if (Item && IsValid(Item->LastOwner) && Item->LastOwner->IsA<AMainCharacter>())
		{
			Item->Destroy();
		}
	}
}

void ATeamProject_MOUGameMode::ResetRunToDayOne()
{
	if (RunState)
	{
		RunState->SetRunState(ERunPhase::Resetting, RunState->RunEndReason);
	}
	if (UProjectGameInstanceBase* GameInstance = GetGameInstance<UProjectGameInstanceBase>())
	{
		GameInstance->ResetRunData();
	}
	const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(this, true);
	GetWorld()->ServerTravel(CurrentLevelName, false);
}

void ATeamProject_MOUGameMode::TravelToLobbyAfterWipe()
{
	if (UProjectGameInstanceBase* GameInstance = GetGameInstance<UProjectGameInstanceBase>())
	{
		GameInstance->ResetRunData();
	}

	const FSoftObjectPath LobbyPath = LobbyMap.ToSoftObjectPath();
	const FString LobbyPackageName = LobbyPath.GetLongPackageName();
	if (LobbyPackageName.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("LobbyMap is not configured. Cannot travel after party wipe."));
		return;
	}

	RunState->SetRunState(ERunPhase::Resetting, RunState->RunEndReason);
	GetWorld()->ServerTravel(LobbyPackageName, false);
}

