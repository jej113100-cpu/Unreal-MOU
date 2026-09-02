#pragma once

#include "CoreMinimal.h"
#include "RunTypes.generated.h"

UENUM(BlueprintType)
enum class ERunPhase : uint8
{
	Playing,
	ProcessingDeadline,
	GameOver,
	Resetting
};

UENUM(BlueprintType)
enum class ERunEndReason : uint8
{
	None,
	DebtPaymentFailed,
	LevelTimeExpired,
	AllPlayersDead
};
