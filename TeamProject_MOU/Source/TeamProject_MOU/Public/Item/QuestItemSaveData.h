#pragma once

#include "CoreMinimal.h"
#include "QuestItemSaveData.generated.h"

// 창고/배달 시스템에서 퀘스트 패키지가 어떤 퀘스트 목표에 속한 물건인지 저장하는 데이터입니다.
USTRUCT(BlueprintType)
struct FQuestItemSaveData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest Item|Save")
	FName QuestID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest Item|Save")
	FName ObjectiveID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest Item|Save")
	FName PickupGroupID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest Item|Save")
	FName DeliveryGroupID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest Item|Save")
	FName DeliveryObjectiveID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest Item|Save")
	FName SelectedDeliveryMarkerID = NAME_None;

	bool IsValid() const
	{
		return !QuestID.IsNone() || !ObjectiveID.IsNone() || !PickupGroupID.IsNone();
	}
};
