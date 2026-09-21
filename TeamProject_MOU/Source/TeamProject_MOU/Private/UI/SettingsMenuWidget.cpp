// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SettingsMenuWidget.h"
#include "UI/KeyBindingMenuWidget.h"
#include "UI/MOU_GameUserSettings.h"
#include "Voice/VoiceSubsystem.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Components/InputKeySelector.h"
#include "Animation/WidgetAnimation.h"
#include "Kismet/KismetSystemLibrary.h"

void USettingsMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 포커스 가능 설정 (ESC 처리)
	SetIsFocusable(true);

	// 탭 버튼 바인딩
	if (Button_GraphicsTab)
	{
		Button_GraphicsTab->OnClicked.AddDynamic(this, &USettingsMenuWidget::OnGraphicsTabClicked);
	}
	if (Button_AudioTab)
	{
		Button_AudioTab->OnClicked.AddDynamic(this, &USettingsMenuWidget::OnAudioTabClicked);
	}
	if (Button_ControlsTab)
	{
		Button_ControlsTab->OnClicked.AddDynamic(this, &USettingsMenuWidget::OnControlsTabClicked);
	}

	// 하단 액션 버튼 바인딩
	if (Button_Apply)
	{
		Button_Apply->OnClicked.AddDynamic(this, &USettingsMenuWidget::OnApplyClicked);
	}
	if (Button_ResetDefaults)
	{
		Button_ResetDefaults->OnClicked.AddDynamic(this, &USettingsMenuWidget::OnResetDefaultsClicked);
	}
	if (Button_Back)
	{
		Button_Back->OnClicked.AddDynamic(this, &USettingsMenuWidget::OnBackClicked);
	}

	// 키 설정 전용 팝업 열기 버튼 바인딩
	if (Button_OpenKeyBindings)
	{
		Button_OpenKeyBindings->OnClicked.AddDynamic(this, &USettingsMenuWidget::OnOpenKeyBindingsClicked);
	}

	// 그래픽 컨트롤 초기화
	SetupResolutions();
	SetupWindowModes();
	SetupQualityLevels();
	SetupFrameRateLimits();

	if (ComboBox_Resolution)
	{
		ComboBox_Resolution->OnSelectionChanged.AddDynamic(this, &USettingsMenuWidget::OnResolutionChanged);
	}
	if (ComboBox_WindowMode)
	{
		ComboBox_WindowMode->OnSelectionChanged.AddDynamic(this, &USettingsMenuWidget::OnWindowModeChanged);
	}
	if (ComboBox_Quality)
	{
		ComboBox_Quality->OnSelectionChanged.AddDynamic(this, &USettingsMenuWidget::OnQualityChanged);
	}
	if (CheckBox_VSync)
	{
		CheckBox_VSync->OnCheckStateChanged.AddDynamic(this, &USettingsMenuWidget::OnVSyncChanged);
	}
	if (ComboBox_FrameRateLimit)
	{
		ComboBox_FrameRateLimit->OnSelectionChanged.AddDynamic(this, &USettingsMenuWidget::OnFrameRateLimitChanged);
	}

	// 오디오 슬라이더 바인딩
	if (Slider_MasterVolume)
	{
		Slider_MasterVolume->OnValueChanged.AddDynamic(this, &USettingsMenuWidget::OnMasterVolumeChanged);
	}
	if (Slider_BGMVolume)
	{
		Slider_BGMVolume->OnValueChanged.AddDynamic(this, &USettingsMenuWidget::OnBGMVolumeChanged);
	}
	if (Slider_SFXVolume)
	{
		Slider_SFXVolume->OnValueChanged.AddDynamic(this, &USettingsMenuWidget::OnSFXVolumeChanged);
	}
	if (Slider_VoiceVolume)
	{
		Slider_VoiceVolume->OnValueChanged.AddDynamic(this, &USettingsMenuWidget::OnVoiceVolumeChanged);
	}
	if (Slider_MicSensitivity)
	{
		Slider_MicSensitivity->OnValueChanged.AddDynamic(this, &USettingsMenuWidget::OnMicSensitivityChanged);
	}

	// 조작 컨트롤 바인딩
	if (Slider_MouseSensitivity)
	{
		Slider_MouseSensitivity->OnValueChanged.AddDynamic(this, &USettingsMenuWidget::OnMouseSensitivityChanged);
	}
	if (CheckBox_InvertY)
	{
		CheckBox_InvertY->OnCheckStateChanged.AddDynamic(this, &USettingsMenuWidget::OnInvertYChanged);
	}
	if (Slider_FOV)
	{
		Slider_FOV->OnValueChanged.AddDynamic(this, &USettingsMenuWidget::OnFOVChanged);
	}

	// 키 선택기(InputKeySelector) 바인딩
	if (KeySelector_Jump)
	{
		KeySelector_Jump->OnKeySelected.AddDynamic(this, &USettingsMenuWidget::OnJumpKeySelected);
	}
	if (KeySelector_Sprint)
	{
		KeySelector_Sprint->OnKeySelected.AddDynamic(this, &USettingsMenuWidget::OnSprintKeySelected);
	}
	if (KeySelector_Interact)
	{
		KeySelector_Interact->OnKeySelected.AddDynamic(this, &USettingsMenuWidget::OnInteractKeySelected);
	}
	if (KeySelector_GrabDrop)
	{
		KeySelector_GrabDrop->OnKeySelected.AddDynamic(this, &USettingsMenuWidget::OnGrabDropKeySelected);
	}
	if (KeySelector_Use)
	{
		KeySelector_Use->OnKeySelected.AddDynamic(this, &USettingsMenuWidget::OnUseKeySelected);
	}
	if (KeySelector_Throw)
	{
		KeySelector_Throw->OnKeySelected.AddDynamic(this, &USettingsMenuWidget::OnThrowKeySelected);
	}
	if (KeySelector_Slot1)
	{
		KeySelector_Slot1->OnKeySelected.AddDynamic(this, &USettingsMenuWidget::OnSlot1KeySelected);
	}
	if (KeySelector_Slot2)
	{
		KeySelector_Slot2->OnKeySelected.AddDynamic(this, &USettingsMenuWidget::OnSlot2KeySelected);
	}
	if (KeySelector_Slot3)
	{
		KeySelector_Slot3->OnKeySelected.AddDynamic(this, &USettingsMenuWidget::OnSlot3KeySelected);
	}

	// 초기 UI 값 세팅
	RefreshUIFromSettings();
	SwitchToTab(0);
}

void USettingsMenuWidget::NativeDestruct()
{
	if (KeyBindingMenuWidget && KeyBindingMenuWidget->IsInViewport())
	{
		KeyBindingMenuWidget->RemoveFromParent();
		KeyBindingMenuWidget = nullptr;
	}

	Super::NativeDestruct();
}

void USettingsMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 마이크 레벨 프로그레스바 실시간 갱신 (옵션 창이 열려 있는 동안)
	if (ProgressBar_MicLevel)
	{
		if (UVoiceSubsystem* VoiceSubsystem = UVoiceSubsystem::Get(GetWorld()))
		{
			float Loudness = VoiceSubsystem->GetCurrentLoudness();
			ProgressBar_MicLevel->SetPercent(FMath::Clamp(Loudness, 0.0f, 1.0f));
		}
	}
}

FReply USettingsMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		if (KeyBindingMenuWidget && KeyBindingMenuWidget->IsInViewport())
		{
			KeyBindingMenuWidget->CloseMenu();
			return FReply::Handled();
		}

		CloseSettings();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

UMOU_GameUserSettings* USettingsMenuWidget::GetUserSettings() const
{
	return UMOU_GameUserSettings::GetMOUGameUserSettings();
}

void USettingsMenuWidget::SetupResolutions()
{
	if (!ComboBox_Resolution)
	{
		return;
	}

	ComboBox_Resolution->ClearOptions();

	// 일반적인 모니터 해상도 목록
	SupportedResolutions = {
		FIntPoint(1280, 720),
		FIntPoint(1600, 900),
		FIntPoint(1920, 1080),
		FIntPoint(2560, 1440),
		FIntPoint(3840, 2160)
	};

	// 현재 모니터의 데스크톱 해상도 포함 여부 확인
	if (UMOU_GameUserSettings* UserSettings = GetUserSettings())
	{
		FIntPoint DesktopResolution = UserSettings->GetDesktopResolution();
		if (!SupportedResolutions.Contains(DesktopResolution) && DesktopResolution.X > 0 && DesktopResolution.Y > 0)
		{
			SupportedResolutions.Add(DesktopResolution);
			SupportedResolutions.Sort([](const FIntPoint& A, const FIntPoint& B) {
				return (A.X * A.Y) < (B.X * B.Y);
			});
		}
	}

	for (const FIntPoint& Res : SupportedResolutions)
	{
		ComboBox_Resolution->AddOption(FString::Printf(TEXT("%d x %d"), Res.X, Res.Y));
	}
}

void USettingsMenuWidget::SetupWindowModes()
{
	if (!ComboBox_WindowMode)
	{
		return;
	}

	ComboBox_WindowMode->ClearOptions();
	ComboBox_WindowMode->AddOption(TEXT("전체화면 (Fullscreen)"));
	ComboBox_WindowMode->AddOption(TEXT("경계없는 창모드 (Borderless)"));
	ComboBox_WindowMode->AddOption(TEXT("창모드 (Windowed)"));
}

void USettingsMenuWidget::SetupQualityLevels()
{
	if (!ComboBox_Quality)
	{
		return;
	}

	ComboBox_Quality->ClearOptions();
	ComboBox_Quality->AddOption(TEXT("낮음 (Low)"));
	ComboBox_Quality->AddOption(TEXT("보통 (Medium)"));
	ComboBox_Quality->AddOption(TEXT("높음 (High)"));
	ComboBox_Quality->AddOption(TEXT("최고 (Epic)"));
	ComboBox_Quality->AddOption(TEXT("시네마틱 (Cinematic)"));
}

void USettingsMenuWidget::SetupFrameRateLimits()
{
	if (!ComboBox_FrameRateLimit)
	{
		return;
	}

	ComboBox_FrameRateLimit->ClearOptions();
	ComboBox_FrameRateLimit->AddOption(TEXT("무제한 (Unlimited)"));
	ComboBox_FrameRateLimit->AddOption(TEXT("30 FPS"));
	ComboBox_FrameRateLimit->AddOption(TEXT("60 FPS"));
	ComboBox_FrameRateLimit->AddOption(TEXT("120 FPS"));
	ComboBox_FrameRateLimit->AddOption(TEXT("144 FPS"));
}

void USettingsMenuWidget::RefreshUIFromSettings()
{
	UMOU_GameUserSettings* UserSettings = GetUserSettings();
	if (!UserSettings)
	{
		return;
	}

	// 1. 해상도
	if (ComboBox_Resolution)
	{
		FIntPoint CurrentRes = UserSettings->GetScreenResolution();
		FString CurrentResStr = FString::Printf(TEXT("%d x %d"), CurrentRes.X, CurrentRes.Y);
		ComboBox_Resolution->SetSelectedOption(CurrentResStr);
	}

	// 2. 화면 모드
	if (ComboBox_WindowMode)
	{
		EWindowMode::Type Mode = UserSettings->GetFullscreenMode();
		switch (Mode)
		{
		case EWindowMode::Fullscreen:
			ComboBox_WindowMode->SetSelectedIndex(0);
			break;
		case EWindowMode::WindowedFullscreen:
			ComboBox_WindowMode->SetSelectedIndex(1);
			break;
		case EWindowMode::Windowed:
		default:
			ComboBox_WindowMode->SetSelectedIndex(2);
			break;
		}
	}

	// 3. 퀄리티 레벨
	if (ComboBox_Quality)
	{
		int32 Quality = UserSettings->GetOverallScalabilityLevel();
		if (Quality >= 0 && Quality <= 4)
		{
			ComboBox_Quality->SetSelectedIndex(Quality);
		}
		else
		{
			ComboBox_Quality->SetSelectedIndex(2); // 기본 높음
		}
	}

	// 4. VSync
	if (CheckBox_VSync)
	{
		CheckBox_VSync->SetIsChecked(UserSettings->IsVSyncEnabled());
	}

	// 5. 프레임 레이트 제한
	if (ComboBox_FrameRateLimit)
	{
		float Limit = UserSettings->GetFrameRateLimit();
		if (Limit <= 0.0f)
		{
			ComboBox_FrameRateLimit->SetSelectedIndex(0); // 무제한
		}
		else if (FMath::IsNearlyEqual(Limit, 30.0f, 1.0f))
		{
			ComboBox_FrameRateLimit->SetSelectedIndex(1);
		}
		else if (FMath::IsNearlyEqual(Limit, 60.0f, 1.0f))
		{
			ComboBox_FrameRateLimit->SetSelectedIndex(2);
		}
		else if (FMath::IsNearlyEqual(Limit, 120.0f, 1.0f))
		{
			ComboBox_FrameRateLimit->SetSelectedIndex(3);
		}
		else if (FMath::IsNearlyEqual(Limit, 144.0f, 1.0f))
		{
			ComboBox_FrameRateLimit->SetSelectedIndex(4);
		}
		else
		{
			ComboBox_FrameRateLimit->SetSelectedIndex(0);
		}
	}

	// 6. 오디오 슬라이더 및 텍스트
	if (Slider_MasterVolume)
	{
		Slider_MasterVolume->SetValue(UserSettings->GetMasterVolume());
	}
	if (Text_MasterVolume)
	{
		Text_MasterVolume->SetText(FText::AsPercent(UserSettings->GetMasterVolume()));
	}

	if (Slider_BGMVolume)
	{
		Slider_BGMVolume->SetValue(UserSettings->GetBGMVolume());
	}
	if (Text_BGMVolume)
	{
		Text_BGMVolume->SetText(FText::AsPercent(UserSettings->GetBGMVolume()));
	}

	if (Slider_SFXVolume)
	{
		Slider_SFXVolume->SetValue(UserSettings->GetSFXVolume());
	}
	if (Text_SFXVolume)
	{
		Text_SFXVolume->SetText(FText::AsPercent(UserSettings->GetSFXVolume()));
	}

	if (Slider_VoiceVolume)
	{
		Slider_VoiceVolume->SetValue(UserSettings->GetVoiceVolume());
	}
	if (Text_VoiceVolume)
	{
		Text_VoiceVolume->SetText(FText::AsPercent(UserSettings->GetVoiceVolume()));
	}

	if (Slider_MicSensitivity)
	{
		Slider_MicSensitivity->SetValue(UserSettings->GetMicSensitivity());
	}
	if (Text_MicSensitivity)
	{
		Text_MicSensitivity->SetText(FText::AsPercent(UserSettings->GetMicSensitivity()));
	}

	// 7. 조작 및 화면
	if (Slider_MouseSensitivity)
	{
		Slider_MouseSensitivity->SetValue(UserSettings->GetMouseSensitivity());
	}
	if (Text_MouseSensitivity)
	{
		Text_MouseSensitivity->SetText(FText::AsNumber(UserSettings->GetMouseSensitivity()));
	}

	if (CheckBox_InvertY)
	{
		CheckBox_InvertY->SetIsChecked(UserSettings->GetInvertY());
	}

	if (Slider_FOV)
	{
		Slider_FOV->SetValue(UserSettings->GetFieldOfView());
	}
	if (Text_FOV)
	{
		Text_FOV->SetText(FText::FromString(FString::Printf(TEXT("%d°"), FMath::RoundToInt(UserSettings->GetFieldOfView()))));
	}

	// 8. 키 바인딩 동기화
	if (KeySelector_Jump)
	{
		KeySelector_Jump->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Jump"), EKeys::SpaceBar)));
	}
	if (KeySelector_Sprint)
	{
		KeySelector_Sprint->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Sprint"), EKeys::LeftShift)));
	}
	if (KeySelector_Interact)
	{
		KeySelector_Interact->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Interact"), EKeys::E)));
	}
	if (KeySelector_GrabDrop)
	{
		KeySelector_GrabDrop->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Grab_Drop"), EKeys::F)));
	}
	if (KeySelector_Use)
	{
		KeySelector_Use->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Use"), EKeys::LeftMouseButton)));
	}
	if (KeySelector_Throw)
	{
		KeySelector_Throw->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Throw"), EKeys::RightMouseButton)));
	}
	if (KeySelector_Slot1)
	{
		KeySelector_Slot1->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Slot1"), EKeys::One)));
	}
	if (KeySelector_Slot2)
	{
		KeySelector_Slot2->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Slot2"), EKeys::Two)));
	}
	if (KeySelector_Slot3)
	{
		KeySelector_Slot3->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Slot3"), EKeys::Three)));
	}
}

void USettingsMenuWidget::ApplySettings()
{
	UMOU_GameUserSettings* UserSettings = GetUserSettings();
	if (!UserSettings)
	{
		return;
	}

	UserSettings->ApplySettings(false);
	UserSettings->SaveSettings();
}

void USettingsMenuWidget::ResetToDefaults()
{
	UMOU_GameUserSettings* UserSettings = GetUserSettings();
	if (!UserSettings)
	{
		return;
	}

	UserSettings->SetToDefaults();
	UserSettings->ApplySettings(false);
	UserSettings->SaveSettings();
	RefreshUIFromSettings();
}

void USettingsMenuWidget::CloseSettings()
{
	OnSettingsMenuClosed.Broadcast();
}

void USettingsMenuWidget::SwitchToTab(int32 TabIndex)
{
	if (WidgetSwitcher_Tabs)
	{
		WidgetSwitcher_Tabs->SetActiveWidgetIndex(TabIndex);
	}
}

void USettingsMenuWidget::RebindActionKey(FName ActionName, const FKey& NewKey)
{
	if (UMOU_GameUserSettings* UserSettings = GetUserSettings())
	{
		UserSettings->SetCustomKeyBinding(ActionName, NewKey);
	}
}

// -----------------------------------------------------------------------------
// 이벤트 핸들러들
// -----------------------------------------------------------------------------

void USettingsMenuWidget::OnGraphicsTabClicked()
{
	SwitchToTab(0);
}

void USettingsMenuWidget::OnAudioTabClicked()
{
	SwitchToTab(1);
}

void USettingsMenuWidget::OnControlsTabClicked()
{
	SwitchToTab(2);
}

void USettingsMenuWidget::OnApplyClicked()
{
	ApplySettings();
}

void USettingsMenuWidget::OnResetDefaultsClicked()
{
	ResetToDefaults();
}

void USettingsMenuWidget::OnBackClicked()
{
	CloseSettings();
}

void USettingsMenuWidget::OnResolutionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	UMOU_GameUserSettings* UserSettings = GetUserSettings();
	if (!UserSettings)
	{
		return;
	}

	int32 Index = ComboBox_Resolution ? ComboBox_Resolution->GetSelectedIndex() : -1;
	if (SupportedResolutions.IsValidIndex(Index))
	{
		UserSettings->SetScreenResolution(SupportedResolutions[Index]);
	}
}

void USettingsMenuWidget::OnWindowModeChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	UMOU_GameUserSettings* UserSettings = GetUserSettings();
	if (!UserSettings)
	{
		return;
	}

	int32 Index = ComboBox_WindowMode ? ComboBox_WindowMode->GetSelectedIndex() : -1;
	switch (Index)
	{
	case 0:
		UserSettings->SetFullscreenMode(EWindowMode::Fullscreen);
		break;
	case 1:
		UserSettings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
		break;
	case 2:
		UserSettings->SetFullscreenMode(EWindowMode::Windowed);
		break;
	default:
		break;
	}
}

void USettingsMenuWidget::OnQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	UMOU_GameUserSettings* UserSettings = GetUserSettings();
	if (!UserSettings)
	{
		return;
	}

	int32 Index = ComboBox_Quality ? ComboBox_Quality->GetSelectedIndex() : -1;
	if (Index >= 0 && Index <= 4)
	{
		UserSettings->SetOverallScalabilityLevel(Index);
	}
}

void USettingsMenuWidget::OnVSyncChanged(bool bIsChecked)
{
	if (UMOU_GameUserSettings* UserSettings = GetUserSettings())
	{
		UserSettings->SetVSyncEnabled(bIsChecked);
	}
}

void USettingsMenuWidget::OnFrameRateLimitChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	UMOU_GameUserSettings* UserSettings = GetUserSettings();
	if (!UserSettings)
	{
		return;
	}

	int32 Index = ComboBox_FrameRateLimit ? ComboBox_FrameRateLimit->GetSelectedIndex() : -1;
	switch (Index)
	{
	case 0: // 무제한
		UserSettings->SetFrameRateLimit(0.0f);
		break;
	case 1: // 30
		UserSettings->SetFrameRateLimit(30.0f);
		break;
	case 2: // 60
		UserSettings->SetFrameRateLimit(60.0f);
		break;
	case 3: // 120
		UserSettings->SetFrameRateLimit(120.0f);
		break;
	case 4: // 144
		UserSettings->SetFrameRateLimit(144.0f);
		break;
	default:
		break;
	}
}

void USettingsMenuWidget::OnMasterVolumeChanged(float Value)
{
	if (UMOU_GameUserSettings* UserSettings = GetUserSettings())
	{
		UserSettings->SetMasterVolume(Value);
		if (Text_MasterVolume)
		{
			Text_MasterVolume->SetText(FText::AsPercent(Value));
		}
	}
}

void USettingsMenuWidget::OnBGMVolumeChanged(float Value)
{
	if (UMOU_GameUserSettings* UserSettings = GetUserSettings())
	{
		UserSettings->SetBGMVolume(Value);
		if (Text_BGMVolume)
		{
			Text_BGMVolume->SetText(FText::AsPercent(Value));
		}
	}
}

void USettingsMenuWidget::OnSFXVolumeChanged(float Value)
{
	if (UMOU_GameUserSettings* UserSettings = GetUserSettings())
	{
		UserSettings->SetSFXVolume(Value);
		if (Text_SFXVolume)
		{
			Text_SFXVolume->SetText(FText::AsPercent(Value));
		}
	}
}

void USettingsMenuWidget::OnVoiceVolumeChanged(float Value)
{
	if (UMOU_GameUserSettings* UserSettings = GetUserSettings())
	{
		UserSettings->SetVoiceVolume(Value);
		if (Text_VoiceVolume)
		{
			Text_VoiceVolume->SetText(FText::AsPercent(Value));
		}
	}
}

void USettingsMenuWidget::OnMicSensitivityChanged(float Value)
{
	if (UMOU_GameUserSettings* UserSettings = GetUserSettings())
	{
		UserSettings->SetMicSensitivity(Value);
		if (Text_MicSensitivity)
		{
			Text_MicSensitivity->SetText(FText::AsPercent(Value));
		}
	}
}

void USettingsMenuWidget::OnMouseSensitivityChanged(float Value)
{
	if (UMOU_GameUserSettings* UserSettings = GetUserSettings())
	{
		UserSettings->SetMouseSensitivity(Value);
		if (Text_MouseSensitivity)
		{
			Text_MouseSensitivity->SetText(FText::AsNumber(Value));
		}
	}
}

void USettingsMenuWidget::OnInvertYChanged(bool bIsChecked)
{
	if (UMOU_GameUserSettings* UserSettings = GetUserSettings())
	{
		UserSettings->SetInvertY(bIsChecked);
	}
}

void USettingsMenuWidget::OnFOVChanged(float Value)
{
	if (UMOU_GameUserSettings* UserSettings = GetUserSettings())
	{
		UserSettings->SetFieldOfView(Value);
		if (Text_FOV)
		{
			Text_FOV->SetText(FText::FromString(FString::Printf(TEXT("%d°"), FMath::RoundToInt(Value))));
		}
	}
}

void USettingsMenuWidget::OnJumpKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Jump"), SelectedKey.Key);
}

void USettingsMenuWidget::OnSprintKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Sprint"), SelectedKey.Key);
}

void USettingsMenuWidget::OnInteractKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Interact"), SelectedKey.Key);
}

void USettingsMenuWidget::OnGrabDropKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Grab_Drop"), SelectedKey.Key);
}

void USettingsMenuWidget::OnUseKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Use"), SelectedKey.Key);
}

void USettingsMenuWidget::OnThrowKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Throw"), SelectedKey.Key);
}

void USettingsMenuWidget::OnSlot1KeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Slot1"), SelectedKey.Key);
}

void USettingsMenuWidget::OnSlot2KeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Slot2"), SelectedKey.Key);
}

void USettingsMenuWidget::OnSlot3KeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Slot3"), SelectedKey.Key);
}

void USettingsMenuWidget::OpenKeyBindingMenu()
{
	if (KeyBindingMenuWidget && KeyBindingMenuWidget->IsInViewport())
	{
		return;
	}

	if (!KeyBindingMenuWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SettingsMenuWidget] KeyBindingMenuWidgetClass가 지정되지 않았습니다! WBP_SettingsMenuWidget의 디테일 패널에서 WBP_KeyBindingMenu를 설정해주세요."));
		return;
	}

	KeyBindingMenuWidget = CreateWidget<UKeyBindingMenuWidget>(this, KeyBindingMenuWidgetClass);
	if (KeyBindingMenuWidget)
	{
		KeyBindingMenuWidget->OnKeyBindingMenuClosed.AddDynamic(this, &USettingsMenuWidget::OnKeyBindingMenuClosed);
		KeyBindingMenuWidget->AddToViewport(150);

		// 키 설정 팝업이 뜨면 이전 위젯(설정 메뉴)을 닫음 (숨김)
		SetVisibility(ESlateVisibility::Collapsed);
		BP_OnKeyBindingOpened();
	}
}

void USettingsMenuWidget::OnOpenKeyBindingsClicked()
{
	OpenKeyBindingMenu();
}

void USettingsMenuWidget::OnKeyBindingMenuClosed()
{
	KeyBindingMenuWidget = nullptr;

	// 키 설정 창이 닫히면 다시 이전 위젯(설정 메뉴)을 표시
	SetVisibility(ESlateVisibility::Visible);

	if (Anim_SlideIn)
	{
		PlayAnimation(Anim_SlideIn);
	}

	BP_OnKeyBindingClosed();
	SetFocus();
}


