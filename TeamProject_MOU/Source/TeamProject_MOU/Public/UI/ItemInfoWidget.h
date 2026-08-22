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

protected:
	// 블루프린트에서 바인딩될 텍스트 위젯들
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Name;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Durability;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Value;

	// 내구도가 낮을 때 텍스트 색상을 빨간색으로 변경하기 위한 임계값
	UPROPERTY(EditDefaultsOnly, Category = "ItemUI")
	float DurabilityWarningThreshold = 30.0f;
};
