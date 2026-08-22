#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BatteryStatusWidget.generated.h"

class UImage;
class UProgressBar;
class UTextBlock;

/**
 * UBatteryStatusWidget
 * 손전등 배터리 잔량(0~100%), 현재 발광 색상 동기화 프레임/바 틴트 제어 위젯 클래스
 */
UCLASS()
class TEAMPROJECT_MOU_API UBatteryStatusWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UBatteryStatusWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 외부(CharacterBase / HUD)에서 배터리 수치 및 발광 상태 갱신
	UFUNCTION(BlueprintCallable, Category = "UI|Battery")
	void UpdateBattery(float NewCurrentBattery, float NewMaxBattery, FLinearColor FlashlightColor = FLinearColor::White, bool bIsFlashlightOn = false);

	// 즉시 값 설정 (초기화용)
	UFUNCTION(BlueprintCallable, Category = "UI|Battery")
	void ForceUpdateBattery(float NewCurrentBattery, float NewMaxBattery, FLinearColor FlashlightColor = FLinearColor::White, bool bIsFlashlightOn = false);

protected:
	// ---------------------------------------------------------
	// [UMG 위젯 바인딩 (이름이 일치하는 위젯 자동 연결)]
	// ---------------------------------------------------------

	// 외곽 프레임 (흰색 텍스처 -> 현재 발광 색상으로 Tint Color 적용)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_Frame;

	// 프로그레스 바 (채우기 텍스처 -> 현재 발광 색상으로 Tint Color 적용)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ProgressBar_Battery;

	// 퍼센트 텍스트 (예: "100%", "45%")
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_BatteryPercent;

	// ---------------------------------------------------------
	// [보간 및 애니메이션 파라미터]
	// ---------------------------------------------------------

	// 게이지 보간 속도
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Battery|Animation")
	float InterpSpeed = 5.0f;

	// 배터리 부족(15% 이하) 시 펄스(Blink) 속도
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Battery|Animation")
	float LowBatteryPulseSpeed = 8.0f;

	// 기본(발광 꺼짐 시) 프레임/게이지 색상
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Battery|Color")
	FLinearColor DefaultIdleColor = FLinearColor(0.8f, 0.8f, 0.8f, 1.0f);

	// 배터리 방전(0%) 시 색상
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Battery|Color")
	FLinearColor DepletedColor = FLinearColor(0.3f, 0.3f, 0.3f, 0.7f);

private:
	float TargetBatteryRatio = 1.0f;
	float CurrentBatteryRatio = 1.0f;

	float TargetCurrentBattery = 100.0f;
	float TargetMaxBattery = 100.0f;

	bool bFlashlightOn = false;
	FLinearColor TargetColor = FLinearColor::White;
	FLinearColor CurrentColor = FLinearColor::White;

	float PulseTime = 0.0f;
};
