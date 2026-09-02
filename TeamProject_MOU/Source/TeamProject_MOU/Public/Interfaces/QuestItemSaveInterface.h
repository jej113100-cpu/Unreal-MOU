#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Item/QuestItemSaveData.h"
#include "QuestItemSaveInterface.generated.h"

UINTERFACE(BlueprintType)
class TEAMPROJECT_MOU_API UQuestItemSaveInterface : public UInterface
{
	GENERATED_BODY()
};

class TEAMPROJECT_MOU_API IQuestItemSaveInterface
{
	GENERATED_BODY()

public:
	// 퀘스트 패키지 BP가 자신에게 필요한 퀘스트 식별 정보를 저장 데이터로 반환합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Quest Item|Save")
	FQuestItemSaveData GetQuestItemSaveData() const;

	// 저장된 퀘스트 식별 정보를 퀘스트 패키지 BP의 변수에 다시 적용합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Quest Item|Save")
	void ApplyQuestItemSaveData(const FQuestItemSaveData& InData);
};
