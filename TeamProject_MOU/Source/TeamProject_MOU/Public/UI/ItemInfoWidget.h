#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ItemInfoWidget.generated.h"

class UTextBlock;
class AItemBase;

UCLASS()
class TEAMPROJECT_MOU_API UItemInfoWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 아이템 정보를 UI에 업데이트합니다.
	UFUNCTION(BlueprintCallable, Category = "ItemUI")
	void UpdateItemInfo(AItemBase* Item);

	// 블루프린트 이벤트 그래프에서 아이템 정보를 처리할 수 있는 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "ItemUI")
	void OnItemInfoUpdated(AItemBase* Item);

protected:
	// 블루프린트에서 바인딩될 텍스트 위젯들 (선택 사항으로 변경하여 없어도 크래시 방지)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Name;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Durability;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Value;

	// 내구도가 낮을 때 텍스트 색상을 빨간색으로 변경하기 위한 임계값
	UPROPERTY(EditDefaultsOnly, Category = "ItemUI")
	float DurabilityWarningThreshold = 30.0f;
};
