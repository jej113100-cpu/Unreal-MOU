#pragma once

#include "CoreMinimal.h"
#include "Base/PackageBase.h"
#include "PackageItemSaveData.generated.h"

// PackageBase가 ItemBase 공통 저장 데이터에 추가로 붙이는 택배 전용 상태입니다.
USTRUCT(BlueprintType)
struct FPackageItemSaveData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Package|Save")
	int32 BaseValue = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Package|Save")
	EPackageType PackageType = EPackageType::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Package|Save")
	float MaxSpoilTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Package|Save")
	float CurrentSpoilTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Package|Save")
	bool bIsBroken = false;
};
