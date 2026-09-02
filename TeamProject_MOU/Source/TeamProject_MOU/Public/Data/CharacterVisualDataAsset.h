#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "CharacterVisualDataAsset.generated.h"

/**
 * 7단계 무게 등급 (0~25%, 25~50%, 50~75%, 75~100%, 100~130%, 130~150%, 150%~)
 */
UENUM(BlueprintType)
enum class EWeightGrade : uint8
{
	Light          UMETA(DisplayName = "가벼움 (0~25%)"),
	Normal         UMETA(DisplayName = "적당 (25~50%)"),
	SlightlyHeavy  UMETA(DisplayName = "조금 무거움 (50~75%)"),
	Heavy          UMETA(DisplayName = "무거움 (75~100%)"),
	Overload1      UMETA(DisplayName = "초과 1 (100~130%)"),
	Overload2      UMETA(DisplayName = "초과 2 (130~150%)"),
	Overload3      UMETA(DisplayName = "초과 3 (150%~)")
};

/**
 * 표정 MI 인덱스, LED 색상, 발광 강도, 상태 우선순위를 통합 관리하는 비주얼 프리셋 구조체
 */
USTRUCT(BlueprintType)
struct FCharacterVisualPreset
{
	GENERATED_BODY()

	// 1. 얼굴 머티리얼 표정 인덱스 (Emotion index 스칼라 파라미터)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	float EmotionIndex = 0.0f;

	// 2. LED 발광 색상 (Emission Color 벡터 파라미터)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor LEDColor = FLinearColor(0.0f, 0.94f, 1.0f, 1.0f);

	// 3. 발광 강도 (Emission Intensity 파라미터 / 색상 배율)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual", meta = (ClampMin = "0.0"))
	float EmissionIntensity = 1.0f;

	// 4. 상태 중재 우선순위 (Priority: 높은 수치가 우선 적용됨)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	int32 Priority = 0;

	FCharacterVisualPreset()
		: EmotionIndex(0.0f)
		, LEDColor(FLinearColor(0.0f, 0.94f, 1.0f, 1.0f))
		, EmissionIntensity(1.0f)
		, Priority(0)
	{
	}

	FCharacterVisualPreset(float InEmotionIndex, const FLinearColor& InLEDColor, float InIntensity, int32 InPriority)
		: EmotionIndex(InEmotionIndex)
		, LEDColor(InLEDColor)
		, EmissionIntensity(InIntensity)
		, Priority(InPriority)
	{
	}
};

/**
 * 캐릭터 표정 및 LED 발광 색상 통합 Primary Data Asset
 */
UCLASS(BlueprintType)
class TEAMPROJECT_MOU_API UCharacterVisualDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UCharacterVisualDataAsset();

	// ---------------------------------------------------------
	// [과적(Encumbrance) 등급별 비주얼 프리셋]
	// ---------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Encumbrance")
	TMap<EWeightGrade, FCharacterVisualPreset> EncumbrancePresets;

	// ---------------------------------------------------------
	// [Gameplay Tag 기반 상태별 비주얼 프리셋]
	// ---------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Status")
	TMap<FGameplayTag, FCharacterVisualPreset> StatusTagPresets;

	// ---------------------------------------------------------
	// [기본(Default Idle) 비주얼 프리셋]
	// ---------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Default")
	FCharacterVisualPreset DefaultPreset;

	// ---------------------------------------------------------
	// [머티리얼 파라미터 이름 설정]
	// ---------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual|Parameters")
	FName EmotionScalarParamName = FName("Emotion index");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual|Parameters")
	FName EmissionColorParamName = FName("Emission Color");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual|Parameters")
	FName EmissionIntensityParamName = FName("Emission Intensity");

	// 무게 비율을 기반으로 등급(EWeightGrade)을 계산하는 헬퍼 함수
	UFUNCTION(BlueprintPure, Category = "Visual|Encumbrance")
	static EWeightGrade CalculateWeightGrade(float WeightRatio);

	// 주어진 무게 등급에 해당하는 프리셋 반환
	UFUNCTION(BlueprintPure, Category = "Visual")
	FCharacterVisualPreset GetEncumbrancePreset(EWeightGrade Grade) const;

	// 주어진 태그에 해당하는 프리셋 반환
	UFUNCTION(BlueprintPure, Category = "Visual")
	bool GetStatusPreset(const FGameplayTag& Tag, FCharacterVisualPreset& OutPreset) const;
};
