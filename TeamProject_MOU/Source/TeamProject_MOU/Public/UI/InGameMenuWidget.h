// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InGameMenuWidget.generated.h"

class UButton;
class UWidgetSwitcher;
class USettingsMenuWidget;
class UWidgetAnimation;

/**
 * 인게임 ESC 메뉴(일시정지 메뉴) 메인 위젯 클래스.
 * [게임으로 돌아가기], [환경설정], [로비로 나가기], [게임 종료] 기능을 제공하며
 * 환경설정 세부 위젯(USettingsMenuWidget)과의 전환 및 ESC 키 처리를 담당합니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API UInGameMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** 게임으로 돌아가기 (메뉴 닫기) */
	UFUNCTION(BlueprintCallable, Category = "InGameMenu")
	void ResumeGame();

	/** 환경설정 화면 열기 */
	UFUNCTION(BlueprintCallable, Category = "InGameMenu")
	void OpenSettings();

	/** 로비 레벨로 나가기 */
	UFUNCTION(BlueprintCallable, Category = "InGameMenu")
	void ReturnToLobby();

	/** 게임 종료 */
	UFUNCTION(BlueprintCallable, Category = "InGameMenu")
	void QuitToDesktop();

	/** ESC 키 또는 뒤로가기 입력 처리 */
	UFUNCTION(BlueprintCallable, Category = "InGameMenu")
	bool HandleBackOrEscape();

protected:
	// =========================================================================
	// [바인드 위젯: 메인 메뉴 버튼들] (선택적 바인딩)
	// =========================================================================
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UButton> Button_Resume;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UButton> Button_Settings;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UButton> Button_ReturnToLobby;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UButton> Button_QuitDesktop;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UWidget> Panel_MainMenu;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<UWidgetSwitcher> WidgetSwitcher_Menu;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
	TObjectPtr<USettingsMenuWidget> SettingsMenuWidget;

	// =========================================================================
	// [애니메이션 바인딩 & 블루프린트 이벤트] (좌/우 슬라이드 인-아웃 지원)
	// =========================================================================
	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnimOptional), Category = "Animation")
	TObjectPtr<UWidgetAnimation> Anim_MenuSlideIn;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnimOptional), Category = "Animation")
	TObjectPtr<UWidgetAnimation> Anim_MenuSlideOut;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnimOptional), Category = "Animation")
	TObjectPtr<UWidgetAnimation> Anim_SettingsSlideIn;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnimOptional), Category = "Animation")
	TObjectPtr<UWidgetAnimation> Anim_SettingsSlideOut;

	UFUNCTION(BlueprintImplementableEvent, Category = "InGameMenu|Animation")
	void BP_OnMenuOpen();

	UFUNCTION(BlueprintImplementableEvent, Category = "InGameMenu|Animation")
	void BP_OnSettingsOpen();

	UFUNCTION(BlueprintImplementableEvent, Category = "InGameMenu|Animation")
	void BP_OnSettingsClose();

	UFUNCTION(BlueprintImplementableEvent, Category = "InGameMenu|Animation")
	void BP_OnMenuClose();

	/** 슬라이드 아웃 애니메이션 완료 후 완전히 메뉴를 닫을 때 호출 */
	UFUNCTION(BlueprintCallable, Category = "InGameMenu")
	void FinishCloseMenu();

private:
	UFUNCTION()
	void OnResumeClicked();

	UFUNCTION()
	void OnSettingsClicked();

	UFUNCTION()
	void OnReturnToLobbyClicked();

	UFUNCTION()
	void OnQuitDesktopClicked();

	UFUNCTION()
	void OnSettingsClosed();

	bool bIsShowingSettings = false;
	FTimerHandle SettingsHideTimerHandle;
};
