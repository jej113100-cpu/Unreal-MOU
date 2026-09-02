#pragma once

#include "CoreMinimal.h"
#include "EconomyTypes.generated.h"

// 빚 상환 기한 처리 결과
UENUM(BlueprintType)
enum class EDebtProcessResult : uint8
{
	NotDue UMETA(DisplayName = "Not Due"),	// 아직 상환 기한이 아님
	Paid   UMETA(DisplayName = "Paid"),		// 상환 성공
	Failed UMETA(DisplayName = "Failed")	// 상환 기한 도달 + Gold 부족
};