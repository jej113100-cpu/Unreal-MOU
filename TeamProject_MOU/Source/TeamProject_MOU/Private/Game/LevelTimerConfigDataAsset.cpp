#include "Game/LevelTimerConfigDataAsset.h"

bool ULevelTimerConfigDataAsset::FindTimeLimitForMap(
	const FString& MapName, float& OutTimeLimitSeconds) const
{
	for (const FLevelTimerMapSetting& Setting : MapSettings)
	{
		if (!Setting.Level.IsNull() && Setting.Level.GetAssetName().Equals(MapName, ESearchCase::IgnoreCase))
		{
			OutTimeLimitSeconds = FMath::Max(1.0f, Setting.TimeLimitSeconds);
			return true;
		}
	}

	return false;
}
