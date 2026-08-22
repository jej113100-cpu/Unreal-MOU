#include "UI/MOU_CharacterStatusHUD.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Math/UnrealMathUtility.h"

void UMOU_CharacterStatusHUD::NativeConstruct()
{
	Super::NativeConstruct();

	if (Image_HPBar)
	{
		MID_HPBar = Image_HPBar->GetDynamicMaterial();
	}

	if (Image_StaminaBar)
	{
		MID_StaminaBar = Image_StaminaBar->GetDynamicMaterial();
	}
	
	// Initial State Update
	SetPortraitTextureByState(ECharacterStatusState::Happy);
}

void UMOU_CharacterStatusHUD::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	bool bHPUpdated = false;

	// Catch-Up effect for HP
	if (!FMath::IsNearlyEqual(CurrentHPPercent, TargetHPPercent, 0.001f))
	{
		CurrentHPPercent = FMath::FInterpTo(CurrentHPPercent, TargetHPPercent, InDeltaTime, CatchUpInterpSpeed);
		
		if (MID_HPBar)
		{
			// Material parameter name matching the M_UI_RadialProgressBar
			MID_HPBar->SetScalarParameterValue(FName("Percent"), CurrentHPPercent);
		}
		
		bHPUpdated = true;
	}
	else if (CurrentHPPercent != TargetHPPercent)
	{
		// 목표치 도달 시 정확한 값으로 스냅 (0.0009f 등에 머무는 현상 방지)
		CurrentHPPercent = TargetHPPercent;
		if (MID_HPBar)
		{
			MID_HPBar->SetScalarParameterValue(FName("Percent"), CurrentHPPercent);
		}
		bHPUpdated = true;
	}

	// Catch-Up effect for Stamina
	if (!FMath::IsNearlyEqual(CurrentStaminaPercent, TargetStaminaPercent, 0.001f))
	{
		CurrentStaminaPercent = FMath::FInterpTo(CurrentStaminaPercent, TargetStaminaPercent, InDeltaTime, CatchUpInterpSpeed);
		
		if (MID_StaminaBar)
		{
			MID_StaminaBar->SetScalarParameterValue(FName("Percent"), CurrentStaminaPercent);
		}
	}
	else if (CurrentStaminaPercent != TargetStaminaPercent)
	{
		CurrentStaminaPercent = TargetStaminaPercent;
		if (MID_StaminaBar)
		{
			MID_StaminaBar->SetScalarParameterValue(FName("Percent"), CurrentStaminaPercent);
		}
	}

	// Only update portrait state if HP has visually changed
	if (bHPUpdated)
	{
		UpdatePortraitState(CurrentHPPercent);
	}
}

void UMOU_CharacterStatusHUD::SetTargetHP(float NewHPPercent)
{
	TargetHPPercent = FMath::Clamp(NewHPPercent, 0.0f, 1.0f);
}

void UMOU_CharacterStatusHUD::SetTargetStamina(float NewStaminaPercent)
{
	TargetStaminaPercent = FMath::Clamp(NewStaminaPercent, 0.0f, 1.0f);
}

void UMOU_CharacterStatusHUD::ForceUpdateStatus(float NewHPPercent, float NewStaminaPercent)
{
	TargetHPPercent = FMath::Clamp(NewHPPercent, 0.0f, 1.0f);
	CurrentHPPercent = TargetHPPercent;
	
	TargetStaminaPercent = FMath::Clamp(NewStaminaPercent, 0.0f, 1.0f);
	CurrentStaminaPercent = TargetStaminaPercent;

	if (MID_HPBar)
	{
		MID_HPBar->SetScalarParameterValue(FName("Percent"), CurrentHPPercent);
	}

	if (MID_StaminaBar)
	{
		MID_StaminaBar->SetScalarParameterValue(FName("Percent"), CurrentStaminaPercent);
	}

	UpdatePortraitState(CurrentHPPercent);
}

void UMOU_CharacterStatusHUD::UpdatePortraitState(float InCurrentHP)
{
	ECharacterStatusState NewState;

	if (InCurrentHP <= 0.0f)
	{
		NewState = ECharacterStatusState::Offline; // Groggy or Dead
	}
	else if (InCurrentHP <= 0.25f)
	{
		NewState = ECharacterStatusState::Critical; // 0+ ~ 25
	}
	else if (InCurrentHP <= 0.50f)
	{
		NewState = ECharacterStatusState::Warning; // 25+ ~ 50
	}
	else if (InCurrentHP <= 0.75f)
	{
		NewState = ECharacterStatusState::OK; // 50+ ~ 75
	}
	else
	{
		NewState = ECharacterStatusState::Happy; // 75+ ~ 100
	}

	if (NewState != CurrentState)
	{
		SetPortraitTextureByState(NewState);
		CurrentState = NewState;
	}
}

void UMOU_CharacterStatusHUD::SetPortraitTextureByState(ECharacterStatusState NewState)
{
	UTexture2D* TargetPortrait = nullptr;
	UTexture2D* TargetBg = nullptr;

	switch (NewState)
	{
		case ECharacterStatusState::Happy:
			TargetPortrait = Tex_Happy;
			TargetBg = Tex_Bg_Happy;
			break;
		case ECharacterStatusState::OK:
			TargetPortrait = Tex_OK;
			TargetBg = Tex_Bg_OK;
			break;
		case ECharacterStatusState::Warning:
			TargetPortrait = Tex_Warning;
			TargetBg = Tex_Bg_Warning;
			break;
		case ECharacterStatusState::Critical:
			TargetPortrait = Tex_Critical;
			TargetBg = Tex_Bg_Critical;
			break;
		case ECharacterStatusState::Offline:
			TargetPortrait = Tex_Offline;
			TargetBg = Tex_Bg_Offline;
			break;
	}

	if (Image_CenterPortrait && TargetPortrait)
	{
		Image_CenterPortrait->SetBrushFromTexture(TargetPortrait);
	}

	if (Image_Background && TargetBg)
	{
		Image_Background->SetBrushFromTexture(TargetBg);
	}
}
