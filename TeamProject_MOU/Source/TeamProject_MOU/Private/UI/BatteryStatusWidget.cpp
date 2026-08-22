#include "UI/BatteryStatusWidget.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

UBatteryStatusWidget::UBatteryStatusWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UBatteryStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();

	TargetColor = DefaultIdleColor;
	CurrentColor = DefaultIdleColor;
}

void UBatteryStatusWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 1. 배터리 비율 및 색상 부드러운 보간 (Interpolation)
	CurrentBatteryRatio = FMath::FInterpTo(CurrentBatteryRatio, TargetBatteryRatio, InDeltaTime, InterpSpeed);
	CurrentColor = FMath::CInterpTo(CurrentColor, TargetColor, InDeltaTime, InterpSpeed);

	FLinearColor FinalColor = CurrentColor;

	// 2. 배터리 부족(15% 이하) 시 빨간색 펄스(Blink/Pulse) 경고 연출
	if (TargetBatteryRatio <= 0.15f && TargetBatteryRatio > 0.0f && bFlashlightOn)
	{
		PulseTime += InDeltaTime * LowBatteryPulseSpeed;
		float PulseAlpha = (FMath::Sin(PulseTime) + 1.0f) * 0.5f; // 0.0 ~ 1.0
		FinalColor = FMath::Lerp(FLinearColor(1.0f, 0.05f, 0.05f, 1.0f), CurrentColor, PulseAlpha);
	}
	else
	{
		PulseTime = 0.0f;
	}

	// 3. 프로그레스 바 갱신 (0.0 ~ 1.0)
	if (ProgressBar_Battery)
	{
		ProgressBar_Battery->SetPercent(FMath::Clamp(CurrentBatteryRatio, 0.0f, 1.0f));
		ProgressBar_Battery->SetFillColorAndOpacity(FinalColor);
	}

	// 4. 외곽 프레임 색상 틴트 적용 (흰색 텍스처 기준)
	if (Image_Frame)
	{
		Image_Frame->SetColorAndOpacity(FinalColor);
	}

	// 5. 퍼센트 텍스트 갱신
	if (Text_BatteryPercent)
	{
		int32 DisplayPercent = FMath::RoundToInt(CurrentBatteryRatio * 100.0f);
		Text_BatteryPercent->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), DisplayPercent)));
		Text_BatteryPercent->SetColorAndOpacity(FSlateColor(FinalColor));
	}
}

void UBatteryStatusWidget::UpdateBattery(float NewCurrentBattery, float NewMaxBattery, FLinearColor FlashlightColor, bool bIsFlashlightOn)
{
	TargetCurrentBattery = FMath::Max(0.0f, NewCurrentBattery);
	TargetMaxBattery = FMath::Max(1.0f, NewMaxBattery);

	TargetBatteryRatio = TargetCurrentBattery / TargetMaxBattery;
	bFlashlightOn = bIsFlashlightOn;

	if (TargetBatteryRatio <= 0.0f)
	{
		TargetColor = DepletedColor;
	}
	else if (bFlashlightOn)
	{
		TargetColor = FlashlightColor;
	}
	else
	{
		TargetColor = DefaultIdleColor;
	}
}

void UBatteryStatusWidget::ForceUpdateBattery(float NewCurrentBattery, float NewMaxBattery, FLinearColor FlashlightColor, bool bIsFlashlightOn)
{
	UpdateBattery(NewCurrentBattery, NewMaxBattery, FlashlightColor, bIsFlashlightOn);
	CurrentBatteryRatio = TargetBatteryRatio;
	CurrentColor = TargetColor;
}
