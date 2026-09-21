// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/GameUserSettings.h"
#include "SettingsMenuWidget.generated.h"

class UButton;
class USlider;
class UCheckBox;
class UComboBoxString;
class UProgressBar;
class UTextBlock;
class UWidgetSwitcher;
class UInputKeySelector;
class UMOU_GameUserSettings;
class UKeyBindingMenuWidget;
class UWidgetAnimation;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSettingsMenuClosed);

/**
 * 인게임 환경설정 위젯 C++ 베이스 클래스.
 * 그래픽, 오디오(마이크 레벨 미터 포함), 조작/키설정 3개 탭을 관리하며
 * UMOU_GameUserSettings와 연동하여 ini 영구 저장 및 엔진 런타임 적용을 처리합니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API USettingsMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** UI 컨트롤에 현재 설정값을 불러와 동기화 */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void RefreshUIFromSettings();

	/** 변경된 설정값을 엔진에 적용하고 ini 파일에 저장 */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ApplySettings();

	/** 모든 설정을 기본값으로 복원 */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ResetToDefaults();

	/** 설정 메뉴 닫기 (뒤로가기) */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void CloseSettings();

	/** 탭 전환 (0: 그래픽, 1: 사운드, 2: 조작) */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SwitchToTab(int32 TabIndex);

	/** 키 리매핑 요청 */
	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void RebindActionKey(FName ActionName, const FKey& NewKey);

	/** 키 설정 전용 팝업 창 열기 */
	UFUNCTION(BlueprintCallable, Category = "Settings|KeyBinding")
	void OpenKeyBindingMenu();

	UPROPERTY(BlueprintAssignable, Category = "Settings")
	FOnSettingsMenuClosed OnSettingsMenuClosed;

protected:
	// =========================================================================
	// [애니메이션 바인딩 & 블루프린트 이벤트]
	// =========================================================================
	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnimOptional), Category = "Animation")
	TObjectPtr<UWidgetAnimation> Anim_SlideIn;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnimOptional), Category = "Animation")
	TObjectPtr<UWidgetAnimation> Anim_SlideOut;

	UFUNCTION(BlueprintImplementableEvent, Category = "Settings|Animation")
	void BP_OnKeyBindingOpened();

	UFUNCTION(BlueprintImplementableEvent, Category = "Settings|Animation")
	void BP_OnKeyBindingClosed();

	// =========================================================================
	// [바인드 위젯: 탭 및 네비게이션] (모두 선택적 바인딩)
	// =========================================================================
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UButton> Button_GraphicsTab;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UButton> Button_AudioTab;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UButton> Button_ControlsTab;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UWidgetSwitcher> WidgetSwitcher_Tabs;

	// =========================================================================
	// [바인드 위젯: 하단 액션 버튼]
	// =========================================================================
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UButton> Button_Apply;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UButton> Button_ResetDefaults;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UButton> Button_Back;

	// =========================================================================
	// [바인드 위젯: 그래픽 탭]
	// =========================================================================
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Graphics")
	TObjectPtr<UComboBoxString> ComboBox_Resolution;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Graphics")
	TObjectPtr<UComboBoxString> ComboBox_WindowMode;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Graphics")
	TObjectPtr<UComboBoxString> ComboBox_Quality;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Graphics")
	TObjectPtr<UCheckBox> CheckBox_VSync;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Graphics")
	TObjectPtr<UComboBoxString> ComboBox_FrameRateLimit;

	// =========================================================================
	// [바인드 위젯: 사운드 탭]
	// =========================================================================
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Audio")
	TObjectPtr<USlider> Slider_MasterVolume;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Audio")
	TObjectPtr<UTextBlock> Text_MasterVolume;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Audio")
	TObjectPtr<USlider> Slider_BGMVolume;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Audio")
	TObjectPtr<UTextBlock> Text_BGMVolume;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Audio")
	TObjectPtr<USlider> Slider_SFXVolume;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Audio")
	TObjectPtr<UTextBlock> Text_SFXVolume;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Audio")
	TObjectPtr<USlider> Slider_VoiceVolume;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Audio")
	TObjectPtr<UTextBlock> Text_VoiceVolume;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Audio")
	TObjectPtr<USlider> Slider_MicSensitivity;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Audio")
	TObjectPtr<UTextBlock> Text_MicSensitivity;

	/** 실시간 마이크 입력 레벨 게이지 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Audio")
	TObjectPtr<UProgressBar> ProgressBar_MicLevel;

	// =========================================================================
	// [바인드 위젯: 조작 및 화면 탭]
	// =========================================================================
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Controls")
	TObjectPtr<USlider> Slider_MouseSensitivity;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Controls")
	TObjectPtr<UTextBlock> Text_MouseSensitivity;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Controls")
	TObjectPtr<UCheckBox> CheckBox_InvertY;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Controls")
	TObjectPtr<USlider> Slider_FOV;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Controls")
	TObjectPtr<UTextBlock> Text_FOV;

	// =========================================================================
	// [바인드 위젯: 키 리매핑 (모두 선택적 바인딩)]
	// =========================================================================
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Jump;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Sprint;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Interact;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_GrabDrop;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Use;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Throw;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Slot1;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Slot2;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Slot3;

	// =========================================================================
	// [키 설정 전용 팝업 창 연동]
	// =========================================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|KeyBinding")
	TSubclassOf<UKeyBindingMenuWidget> KeyBindingMenuWidgetClass;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UButton> Button_OpenKeyBindings;

	UPROPERTY(Transient)
	TObjectPtr<UKeyBindingMenuWidget> KeyBindingMenuWidget;

private:
	// 위젯 이벤트 바인딩 핸들러들
	UFUNCTION()
	void OnGraphicsTabClicked();

	UFUNCTION()
	void OnAudioTabClicked();

	UFUNCTION()
	void OnControlsTabClicked();

	UFUNCTION()
	void OnOpenKeyBindingsClicked();

	UFUNCTION()
	void OnKeyBindingMenuClosed();

	UFUNCTION()
	void OnJumpKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnSprintKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnInteractKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnGrabDropKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnUseKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnThrowKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnSlot1KeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnSlot2KeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnSlot3KeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnApplyClicked();

	UFUNCTION()
	void OnResetDefaultsClicked();

	UFUNCTION()
	void OnBackClicked();

	UFUNCTION()
	void OnResolutionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnWindowModeChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnVSyncChanged(bool bIsChecked);

	UFUNCTION()
	void OnFrameRateLimitChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnMasterVolumeChanged(float Value);

	UFUNCTION()
	void OnBGMVolumeChanged(float Value);

	UFUNCTION()
	void OnSFXVolumeChanged(float Value);

	UFUNCTION()
	void OnVoiceVolumeChanged(float Value);

	UFUNCTION()
	void OnMicSensitivityChanged(float Value);

	UFUNCTION()
	void OnMouseSensitivityChanged(float Value);

	UFUNCTION()
	void OnInvertYChanged(bool bIsChecked);

	UFUNCTION()
	void OnFOVChanged(float Value);

	void SetupResolutions();
	void SetupWindowModes();
	void SetupQualityLevels();
	void SetupFrameRateLimits();

	TArray<FIntPoint> SupportedResolutions;
	UMOU_GameUserSettings* GetUserSettings() const;
};
