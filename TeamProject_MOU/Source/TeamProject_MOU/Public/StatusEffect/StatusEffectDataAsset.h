#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StatusEffectDataAsset.generated.h"

class UAnimMontage;

/* 캐릭터에게 적용할 상태이상 종류 */
UENUM(BlueprintType)
enum class EStatusEffectType : uint8
{
	Blindness		UMETA(DisplayName = "실명"),
	Stun			UMETA(DisplayName = "스턴"),
	ElectricShock	UMETA(DisplayName = "감전"),
	DamageOverTime	UMETA(DisplayName = "도트류"),
	Slow			UMETA(DisplayName = "슬로우"),
	Knockdown		UMETA(DisplayName = "넘어짐"),
	Fear			UMETA(DisplayName = "공포"),
	Taunt			UMETA(DisplayName = "도발")
};

/* 상태이상 하나의 연출과 수치를 정의하는 데이터 에셋 */
UCLASS(BlueprintType)
class TEAMPROJECT_MOU_API UStatusEffectDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/* 상태이상 종류 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect")
	EStatusEffectType EffectType = EStatusEffectType::Stun;

	/* 상태이상 적용 중 재생할 애니메이션 몽타주 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect|Common")
	TObjectPtr<UAnimMontage> AnimationMontage;

	/* 상태이상 지속시간. 0이면 해당 GA가 직접 종료 시점을 관리합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect|Common", meta = (ClampMin = "0.0", Units = "s"))
	float Duration = 1.0f;

	/* 기존 이동속도에 곱할 값. 0.5면 이동속도가 절반이 됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect|Slow", meta = (EditCondition = "EffectType == EStatusEffectType::Slow", EditConditionHides, ClampMin = "0.0", ClampMax = "1.0"))
	float SlowRate = 0.5f;

	/* 도트류 상태이상이 한 번 적용될 때 줄 피해량 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect|Damage Over Time", meta = (EditCondition = "EffectType == EStatusEffectType::DamageOverTime", EditConditionHides, ClampMin = "0.0"))
	float DamagePerTick = 1.0f;

	/* 도트 피해가 반복되는 시간 간격 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect|Damage Over Time", meta = (EditCondition = "EffectType == EStatusEffectType::DamageOverTime", EditConditionHides, ClampMin = "0.01", Units = "s"))
	float TickInterval = 1.0f;
};
