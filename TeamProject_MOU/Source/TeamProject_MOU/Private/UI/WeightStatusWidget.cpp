#include "UI/WeightStatusWidget.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"

UWeightStatusWidget::UWeightStatusWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UWeightStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();

	TargetColor = Color_Light;
	CurrentColor = Color_Light;
	ApplyGradeAppearance(EWeightGrade::Light);
}

void UWeightStatusWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 1. 비율 및 색상 부드러운 보간 (Interpolation)
	CurrentWeightRatio = FMath::FInterpTo(CurrentWeightRatio, TargetWeightRatio, InDeltaTime, InterpSpeed);
	CurrentColor = FMath::CInterpTo(CurrentColor, TargetColor, InDeltaTime, InterpSpeed);

	FLinearColor FinalFrameColor = CurrentColor;
	FLinearColor FinalBarColor = CurrentColor;

	// 2. 초과 상태(100% 이상: Overload1, Overload2, Overload3) 시 펄스(Blink/Pulse) 네온 연출
	bool bIsOverloaded = (CurrentGrade == EWeightGrade::Overload1 || CurrentGrade == EWeightGrade::Overload2 || CurrentGrade == EWeightGrade::Overload3);
	if (bIsOverloaded)
	{
		PulseTime += InDeltaTime * PulseSpeed;
		float PulseAlpha = (FMath::Sin(PulseTime) + 1.0f) * 0.5f; // 0.0 ~ 1.0

		// 현재 초과 색상과 밝은 화이트/레드 사이를 주기적으로 진동
		FinalFrameColor = FMath::Lerp(CurrentColor, FLinearColor(1.0f, 0.4f, 0.4f, 1.0f), PulseAlpha * 0.6f);
		FinalBarColor = FMath::Lerp(CurrentColor, FLinearColor(1.0f, 0.6f, 0.6f, 1.0f), PulseAlpha * 0.5f);
	}
	else
	{
		PulseTime = 0.0f;
	}

	// 3. 프로그레스 바 갱신 (0.0 ~ 1.0)
	if (ProgressBar_Weight)
	{
		// 프로그레스 바 게이지 채우기 (100% 이상도 1.0으로 꽉 참)
		ProgressBar_Weight->SetPercent(FMath::Clamp(CurrentWeightRatio, 0.0f, 1.0f));
		ProgressBar_Weight->SetFillColorAndOpacity(FinalBarColor);
	}

	// 4. 외곽 프레임 색상 틴트 적용 (흰색 텍스처 기준)
	if (Image_Frame)
	{
		Image_Frame->SetColorAndOpacity(FinalFrameColor);
	}

	// 5. 로봇 얼굴 표정: 평소에는 원본 색상(White) 유지, 초과 상태에서만 펄스 애니메이션 효과 적용
	if (Image_FacePortrait)
	{
		if (bIsOverloaded)
		{
			Image_FacePortrait->SetColorAndOpacity(FinalFrameColor);
		}
		else
		{
			Image_FacePortrait->SetColorAndOpacity(FLinearColor::White);
		}
	}

	// 6. 퍼센트 텍스트 갱신
	if (Text_WeightPercent)
	{
		int32 DisplayPercent = FMath::RoundToInt(CurrentWeightRatio * 100.0f);
		Text_WeightPercent->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), DisplayPercent)));
		Text_WeightPercent->SetColorAndOpacity(FSlateColor(FinalFrameColor));
	}
}

void UWeightStatusWidget::UpdateWeight(float NewCurrentWeight, float NewMaxWeight)
{
	TargetCurrentWeight = FMath::Max(0.0f, NewCurrentWeight);
	TargetMaxWeight = FMath::Max(1.0f, NewMaxWeight);

	TargetWeightRatio = TargetCurrentWeight / TargetMaxWeight;

	CalculateWeightGrade(TargetWeightRatio);
}

void UWeightStatusWidget::ForceUpdateWeight(float NewCurrentWeight, float NewMaxWeight)
{
	UpdateWeight(NewCurrentWeight, NewMaxWeight);
	CurrentWeightRatio = TargetWeightRatio;
	CurrentColor = TargetColor;
}

void UWeightStatusWidget::CalculateWeightGrade(float Ratio)
{
	EWeightGrade NewGrade = UCharacterVisualDataAsset::CalculateWeightGrade(Ratio);

	if (CurrentGrade != NewGrade)
	{
		CurrentGrade = NewGrade;
		ApplyGradeAppearance(CurrentGrade);
	}
}

void UWeightStatusWidget::ApplyGradeAppearance(EWeightGrade Grade)
{
	UTexture2D* FaceTextureToSet = nullptr;

	switch (Grade)
	{
	case EWeightGrade::Light:
		TargetColor = Color_Light;
		FaceTextureToSet = Tex_Face_Light;
		break;

	case EWeightGrade::Normal:
		TargetColor = Color_Normal;
		FaceTextureToSet = Tex_Face_Normal;
		break;

	case EWeightGrade::SlightlyHeavy:
		TargetColor = Color_SlightlyHeavy;
		FaceTextureToSet = Tex_Face_SlightlyHeavy;
		break;

	case EWeightGrade::Heavy:
		TargetColor = Color_Heavy;
		FaceTextureToSet = Tex_Face_Heavy;
		break;

	case EWeightGrade::Overload1:
		TargetColor = Color_Overload1;
		FaceTextureToSet = Tex_Face_Overload1;
		break;

	case EWeightGrade::Overload2:
		TargetColor = Color_Overload2;
		FaceTextureToSet = Tex_Face_Overload2;
		break;

	case EWeightGrade::Overload3:
		TargetColor = Color_Overload3;
		FaceTextureToSet = Tex_Face_Overload3;
		break;
	}

	// 로봇 얼굴 텍스처 교체
	if (Image_FacePortrait && FaceTextureToSet)
	{
		Image_FacePortrait->SetBrushFromTexture(FaceTextureToSet);
	}
}
