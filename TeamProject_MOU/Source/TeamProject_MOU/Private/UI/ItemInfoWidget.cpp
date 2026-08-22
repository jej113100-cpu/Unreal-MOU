#include "UI/ItemInfoWidget.h"
#include "Components/TextBlock.h"
#include "Base/ItemBase.h"
#include "Base/PackageBase.h"

void UItemInfoWidget::UpdateItemInfo(AItemBase* Item)
{
	if (!Item)
	{
		return;
	}

	// 1. 이름 설정
	if (Text_Name)
	{
		Text_Name->SetText(Item->ItemName);
	}

	// 2. 내구도 설정
	if (Text_Durability)
	{
		FString DurabilityStr = FString::Printf(TEXT("Durability: %d / %d"), FMath::RoundToInt(Item->CurrentDurability), FMath::RoundToInt(Item->MaxDurability));
		Text_Durability->SetText(FText::FromString(DurabilityStr));

		// 내구도 경고 색상 변경 (기본값: 흰색, 경고: 빨간색)
		if (Item->CurrentDurability <= DurabilityWarningThreshold)
		{
			Text_Durability->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
		}
		else
		{
			Text_Durability->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		}
	}

	// 3. 가치 설정 (택배일 경우에만 표시)
	if (Text_Value)
	{
		APackageBase* Package = Cast<APackageBase>(Item);
		if (Package)
		{
			FString ValueStr = FString::Printf(TEXT("Value: %d"), Package->GetCurrentValue());
			Text_Value->SetText(FText::FromString(ValueStr));
			Text_Value->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			// 택배가 아니면 가치 텍스트를 숨김
			Text_Value->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}
