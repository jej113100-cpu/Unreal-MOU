#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LevelTimerConfigDataAsset.generated.h"

USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FLevelTimerMapSetting
{
	GENERATED_BODY()

	// 타이머를 사용할 게임 맵입니다. 로비 맵은 목록에 넣지 않습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Timer")
	TSoftObjectPtr<UWorld> Level;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Timer",
		meta = (ClampMin = "1.0", Units = "s"))
	float TimeLimitSeconds = 600.0f;
};

UCLASS(BlueprintType)
class TEAMPROJECT_MOU_API ULevelTimerConfigDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Timer")
	TArray<FLevelTimerMapSetting> MapSettings;

	bool FindTimeLimitForMap(const FString& MapName, float& OutTimeLimitSeconds) const;
};
