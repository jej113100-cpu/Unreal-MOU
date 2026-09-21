// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "Framework/Commands/InputChord.h"
#include "KeyBindingMenuWidget.generated.h"

class UButton;
class UInputKeySelector;
class UScrollBox;
class UMOU_GameUserSettings;
class UWidgetAnimation;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnKeyBindingMenuClosed);

/**
 * 인게임 키 설정 전용 팝업 위젯 클래스.
 * 이동, 상호작용, 액션, 아이템 슬롯, 보이스 등 주요 인게임 조작 키를 UInputKeySelector를 통해
 * 독립된 팝업 창에서 여유롭게 변경하고, UMOU_GameUserSettings 및 Enhanced Input에 즉시 반영합니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API UKeyBindingMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** UI 컨트롤들에 현재 설정된 키값들을 동기화 */
	UFUNCTION(BlueprintCallable, Category = "KeyBinding")
	void RefreshUIFromSettings();

	/** 모든 키 바인딩을 기본값으로 복원 */
	UFUNCTION(BlueprintCallable, Category = "KeyBinding")
	void ResetToDefaults();

	/** 키 설정 창 닫기 */
	UFUNCTION(BlueprintCallable, Category = "KeyBinding")
	void CloseMenu();

	/** 닫기 애니메이션 완료 후 완전히 창을 제거할 때 호출 */
	UFUNCTION(BlueprintCallable, Category = "KeyBinding")
	void FinishCloseMenu();

	/** 액션 키 리매핑 실행 및 즉시 엔진 적용 */
	UFUNCTION(BlueprintCallable, Category = "KeyBinding")
	void RebindActionKey(FName ActionName, const FKey& NewKey);

	UPROPERTY(BlueprintAssignable, Category = "KeyBinding")
	FOnKeyBindingMenuClosed OnKeyBindingMenuClosed;

protected:
	// =========================================================================
	// [애니메이션 바인딩 & 블루프린트 연출 이벤트]
	// =========================================================================
	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnimOptional), Category = "Animation")
	TObjectPtr<UWidgetAnimation> Anim_Open;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnimOptional), Category = "Animation")
	TObjectPtr<UWidgetAnimation> Anim_Close;

	UFUNCTION(BlueprintImplementableEvent, Category = "KeyBinding|Animation")
	void BP_OnMenuOpen();

	UFUNCTION(BlueprintImplementableEvent, Category = "KeyBinding|Animation")
	void BP_OnMenuClose();

	// =========================================================================
	// [네비게이션 및 액션 버튼] (선택적 바인딩)
	// =========================================================================
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UButton> Button_Back;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UButton> Button_Close;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UButton> Button_ResetDefaults;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UScrollBox> ScrollBox_KeyList;

	// =========================================================================
	// [키 바인딩 UInputKeySelector 목록] (모두 BindWidgetOptional)
	// =========================================================================

	// --- 1. 이동 (WASD) ---
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_MoveForward;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_MoveBackward;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_MoveLeft;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_MoveRight;

	// --- 2. 기본 행동 ---
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Jump;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Sprint;

	// --- 3. 상호작용 및 상호동작 ---
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Interact;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_GrabDrop;

	// --- 4. 전투 및 도구 사용 ---
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Use;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Throw;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Slap;

	// --- 5. 손전등 및 조명 ---
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Light;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_LightColor;

	// --- 6. 인벤토리 퀵슬롯 ---
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Slot1;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Slot2;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Slot3;

	// --- 7. 퀘스트, 경제, 메뉴 ---
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Quest;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_ViewEconomy;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_Emote;

	// --- 8. 무전 및 음성 대화 ---
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_RadioPower;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_RadioTransmit;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|KeyBinding")
	TObjectPtr<UInputKeySelector> KeySelector_VoiceMute;

private:
	// 키 선택 델리게이트 이벤트 핸들러들
	UFUNCTION()
	void OnMoveForwardKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnMoveBackwardKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnMoveLeftKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnMoveRightKeySelected(FInputChord SelectedKey);

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
	void OnSlapKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnLightKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnLightColorKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnSlot1KeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnSlot2KeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnSlot3KeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnQuestKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnViewEconomyKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnEmoteKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnRadioPowerKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnRadioTransmitKeySelected(FInputChord SelectedKey);

	UFUNCTION()
	void OnVoiceMuteKeySelected(FInputChord SelectedKey);

	// 버튼 클릭 핸들러들
	UFUNCTION()
	void OnBackClicked();

	UFUNCTION()
	void OnResetDefaultsClicked();

	UMOU_GameUserSettings* GetUserSettings() const;

	FTimerHandle CloseTimerHandle;
	bool bIsClosing = false;
};
