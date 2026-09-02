#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CharacterVisualDataAsset.h"
#include "WeightStatusWidget.generated.h"

class UImage;
class UProgressBar;
class UTextBlock;
class UTexture2D;
class UMaterialInstanceDynamic;

/**
 * UWeightStatusWidget
 * 가방 및 플레이어의 실시간 무게, 5단계 표정, 흰색 텍스처 기반 프레임/바 틴트 제어 위젯 클래스
 */
UCLASS()
class TEAMPROJECT_MOU_API UWeightStatusWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UWeightStatusWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 외부(CharacterBase / HUD)에서 무게 수치 갱신
	UFUNCTION(BlueprintCallable, Category = "UI|Weight")
	void UpdateWeight(float NewCurrentWeight, float NewMaxWeight);

	// 즉시 값 설정 (보간 없이 초기화할 때 사용)
	UFUNCTION(BlueprintCallable, Category = "UI|Weight")
	void ForceUpdateWeight(float NewCurrentWeight, float NewMaxWeight);

	// 현재 무게 등급 반환
	UFUNCTION(BlueprintPure, Category = "UI|Weight")
	EWeightGrade GetCurrentWeightGrade() const { return CurrentGrade; }

protected:
	// ---------------------------------------------------------
	// [UMG 위젯 바인딩 (이름이 일치하는 위젯 자동 연결)]
	// ---------------------------------------------------------

	// 외곽 프레임 (흰색 텍스처 -> 상태별 Tint Color 적용)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_Frame;

	// 프로그레스 바 (채우기 텍스처 -> 상태별 Tint Color 적용)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ProgressBar_Weight;

	// 프로그레스 바로 Image를 사용하는 경우 (DMI 기반 제어 지원)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_WeightBar;

	// 로봇 얼굴 표정 이미지
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_FacePortrait;

	// 퍼센트 텍스트 (예: "45%")
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_WeightPercent;

	// ---------------------------------------------------------
	// [7단계 로봇 표정 텍스처 (블루프린트 디테일창에서 할당)]
	// ---------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Portrait")
	TObjectPtr<UTexture2D> Tex_Face_Light;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Portrait")
	TObjectPtr<UTexture2D> Tex_Face_Normal;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Portrait")
	TObjectPtr<UTexture2D> Tex_Face_SlightlyHeavy;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Portrait")
	TObjectPtr<UTexture2D> Tex_Face_Heavy;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Portrait")
	TObjectPtr<UTexture2D> Tex_Face_Overload1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Portrait")
	TObjectPtr<UTexture2D> Tex_Face_Overload2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Portrait")
	TObjectPtr<UTexture2D> Tex_Face_Overload3;

	// ---------------------------------------------------------
	// [7단계 색상 설정 (기본값 설정됨, 에디터에서 변경 가능)]
	// ---------------------------------------------------------

	// 가벼움: 하늘색 (0~25%)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Color")
	FLinearColor Color_Light = FLinearColor(0.0f, 0.94f, 1.0f, 1.0f);

	// 적당: 연두색 (25~50%)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Color")
	FLinearColor Color_Normal = FLinearColor(0.33f, 1.0f, 0.27f, 1.0f);

	// 조금 무거움: 옐로우/골드 (50~75%)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Color")
	FLinearColor Color_SlightlyHeavy = FLinearColor(1.0f, 0.85f, 0.0f, 1.0f);

	// 무거움: 주황 (75~100%)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Color")
	FLinearColor Color_Heavy = FLinearColor(1.0f, 0.55f, 0.0f, 1.0f);

	// 초과 1: 핑크/마젠타 (100~130%)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Color")
	FLinearColor Color_Overload1 = FLinearColor(1.0f, 0.2f, 0.47f, 1.0f);

	// 초과 2: 다홍/레드 (130~150%)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Color")
	FLinearColor Color_Overload2 = FLinearColor(1.0f, 0.1f, 0.1f, 1.0f);

	// 초과 3: 강렬한 레드 (150%~)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Color")
	FLinearColor Color_Overload3 = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);

	// ---------------------------------------------------------
	// [보간 및 애니메이션 파라미터]
	// ---------------------------------------------------------

	// 게이지 보간 속도
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Animation")
	float InterpSpeed = 5.0f;

	// 초과 2 상태 시 펄스(깜빡임) 속도
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Weight|Animation")
	float PulseSpeed = 6.0f;

private:
	float TargetWeightRatio = 0.0f;
	float CurrentWeightRatio = 0.0f;

	float TargetCurrentWeight = 0.0f;
	float TargetMaxWeight = 100.0f;

	FLinearColor TargetColor = FLinearColor::White;
	FLinearColor CurrentColor = FLinearColor::White;

	EWeightGrade CurrentGrade = EWeightGrade::Light;

	float PulseTime = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DMI_WeightBar;

	// 내부 연산 및 갱신 헬퍼
	void CalculateWeightGrade(float Ratio);
	void ApplyGradeAppearance(EWeightGrade Grade);
};
