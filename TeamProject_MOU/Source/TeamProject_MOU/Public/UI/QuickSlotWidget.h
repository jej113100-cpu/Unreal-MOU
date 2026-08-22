#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "QuickSlotWidget.generated.h"

class AItemBase;

/**
 * 인벤토리 퀵슬롯 HUD 위젯의 베이스 클래스
 */
UCLASS()
class TEAMPROJECT_MOU_API UQuickSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 슬롯이 업데이트될 때 (아이템 변경 시) 블루프린트에서 호출될 이벤트
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Inventory UI")
	void OnSlotUpdated(int32 SlotIndex, AItemBase* ItemInSlot);

	// 손에 아이템을 들었을 때 (특정 슬롯 장착 시) 하이라이트 처리를 위한 이벤트
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Inventory UI")
	void OnSlotEquipped(int32 SlotIndex, bool bIsEquipped);
};
