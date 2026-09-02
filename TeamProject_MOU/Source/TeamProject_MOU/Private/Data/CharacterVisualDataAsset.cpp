#include "Data/CharacterVisualDataAsset.h"

UCharacterVisualDataAsset::UCharacterVisualDataAsset()
{
	// 기본 프리셋 (Fallback)
	DefaultPreset = FCharacterVisualPreset(0.0f, FLinearColor(0.0f, 0.94f, 1.0f, 1.0f), 1.0f, 0);

	// 7단계 과적 기본 프리셋 구성 (0~25%, 25~50%, 50~75%, 75~100%, 100~130%, 130~150%, 150%~)
	EncumbrancePresets.Add(EWeightGrade::Light,         FCharacterVisualPreset(0.0f, FLinearColor(0.0f, 0.94f, 1.0f, 1.0f), 1.0f, 10)); // 가벼움 (0~25%)
	EncumbrancePresets.Add(EWeightGrade::Normal,        FCharacterVisualPreset(0.0f, FLinearColor(0.33f, 1.0f, 0.27f, 1.0f), 1.0f, 20)); // 적당 (25~50%)
	EncumbrancePresets.Add(EWeightGrade::SlightlyHeavy, FCharacterVisualPreset(1.0f, FLinearColor(1.0f, 0.85f, 0.0f, 1.0f), 1.2f, 30)); // 조금 무거움 (50~75%)
	EncumbrancePresets.Add(EWeightGrade::Heavy,         FCharacterVisualPreset(1.0f, FLinearColor(1.0f, 0.55f, 0.0f, 1.0f), 1.4f, 40)); // 무거움 (75~100%)
	EncumbrancePresets.Add(EWeightGrade::Overload1,     FCharacterVisualPreset(2.0f, FLinearColor(1.0f, 0.2f, 0.47f, 1.0f), 1.6f, 50)); // 초과 1 (100~130%)
	EncumbrancePresets.Add(EWeightGrade::Overload2,     FCharacterVisualPreset(2.0f, FLinearColor(1.0f, 0.1f, 0.1f, 1.0f), 1.8f, 55)); // 초과 2 (130~150%)
	EncumbrancePresets.Add(EWeightGrade::Overload3,     FCharacterVisualPreset(3.0f, FLinearColor(1.0f, 0.0f, 0.0f, 1.0f), 2.0f, 60)); // 초과 3 (150%~)
}

EWeightGrade UCharacterVisualDataAsset::CalculateWeightGrade(float WeightRatio)
{
	if (WeightRatio >= 1.50f)
	{
		return EWeightGrade::Overload3; // 초과 3 (150%~)
	}
	else if (WeightRatio >= 1.30f)
	{
		return EWeightGrade::Overload2; // 초과 2 (130~150%)
	}
	else if (WeightRatio >= 1.00f)
	{
		return EWeightGrade::Overload1; // 초과 1 (100~130%)
	}
	else if (WeightRatio >= 0.75f)
	{
		return EWeightGrade::Heavy; // 무거움 (75~100%)
	}
	else if (WeightRatio >= 0.50f)
	{
		return EWeightGrade::SlightlyHeavy; // 조금 무거움 (50~75%)
	}
	else if (WeightRatio >= 0.25f)
	{
		return EWeightGrade::Normal; // 적당 (25~50%)
	}
	return EWeightGrade::Light; // 가벼움 (0~25%)
}

FCharacterVisualPreset UCharacterVisualDataAsset::GetEncumbrancePreset(EWeightGrade Grade) const
{
	if (const FCharacterVisualPreset* Found = EncumbrancePresets.Find(Grade))
	{
		return *Found;
	}
	return DefaultPreset;
}

bool UCharacterVisualDataAsset::GetStatusPreset(const FGameplayTag& Tag, FCharacterVisualPreset& OutPreset) const
{
	if (const FCharacterVisualPreset* Found = StatusTagPresets.Find(Tag))
	{
		OutPreset = *Found;
		return true;
	}
	return false;
}
