// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/InGameMenuWidget.h"
#include "UI/SettingsMenuWidget.h"
#include "TeamProject_MOUPlayerController.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "Animation/WidgetAnimation.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"

void UInGameMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetIsFocusable(true);

	if (Button_Resume)
	{
		Button_Resume->OnClicked.AddDynamic(this, &UInGameMenuWidget::OnResumeClicked);
	}
	if (Button_Settings)
	{
		Button_Settings->OnClicked.AddDynamic(this, &UInGameMenuWidget::OnSettingsClicked);
	}
	if (Button_ReturnToLobby)
	{
		Button_ReturnToLobby->OnClicked.AddDynamic(this, &UInGameMenuWidget::OnReturnToLobbyClicked);
	}
	if (Button_QuitDesktop)
	{
		Button_QuitDesktop->OnClicked.AddDynamic(this, &UInGameMenuWidget::OnQuitDesktopClicked);
	}

	if (Panel_MainMenu)
	{
		Panel_MainMenu->SetVisibility(ESlateVisibility::Visible);
	}

	if (SettingsMenuWidget)
	{
		SettingsMenuWidget->OnSettingsMenuClosed.AddDynamic(this, &UInGameMenuWidget::OnSettingsClosed);
		// 초기에는 설정 화면 숨김 (환경설정을 눌렀을 때 등장)
		SettingsMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 기본은 메인 메뉴 표시
	bIsShowingSettings = false;
	if (WidgetSwitcher_Menu)
	{
		WidgetSwitcher_Menu->SetActiveWidgetIndex(0);
	}

	// 1. C++ 바인딩된 메인 메뉴 슬라이드 인 애니메이션이 있다면 자동 재생
	if (Anim_MenuSlideIn)
	{
		PlayAnimation(Anim_MenuSlideIn);
	}

	// 2. 블루프린트 이벤트 호출 (에디터에서 커스텀 애니메이션 노드를 연결할 수 있음)
	BP_OnMenuOpen();
}

FReply UInGameMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		HandleBackOrEscape();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UInGameMenuWidget::ResumeGame()
{
	// 블루프린트 닫기 이벤트 통지
	BP_OnMenuClose();

	// 슬라이드 아웃 애니메이션이 유효하면 재생 후 닫기 (최대 0.5초 대기 안전장치)
	if (Anim_MenuSlideOut && Anim_MenuSlideOut->GetEndTime() > 0.05f)
	{
		PlayAnimation(Anim_MenuSlideOut);
		const float AnimLength = FMath::Min(Anim_MenuSlideOut->GetEndTime(), 0.5f);
		FTimerHandle TimerHandle;
		GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &UInGameMenuWidget::FinishCloseMenu, AnimLength, false);
	}
	else
	{
		FinishCloseMenu();
	}
}

void UInGameMenuWidget::FinishCloseMenu()
{
	if (ATeamProject_MOUPlayerController* MOU_PC = Cast<ATeamProject_MOUPlayerController>(GetOwningPlayer()))
	{
		MOU_PC->CloseInGameMenu();
	}
}

void UInGameMenuWidget::OpenSettings()
{
	bIsShowingSettings = true;
	GetWorld()->GetTimerManager().ClearTimer(SettingsHideTimerHandle);

	// 1. 메인 메뉴 패널 감추기
	if (Panel_MainMenu)
	{
		Panel_MainMenu->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 2. 위젯 스위처가 있으면 1번 슬롯(설정)으로 전환
	if (WidgetSwitcher_Menu)
	{
		WidgetSwitcher_Menu->SetActiveWidgetIndex(1);
	}

	// 3. 설정 메뉴 표시
	if (SettingsMenuWidget)
	{
		SettingsMenuWidget->SetVisibility(ESlateVisibility::Visible);
	}

	if (Anim_SettingsSlideIn)
	{
		PlayAnimation(Anim_SettingsSlideIn);
	}

	BP_OnSettingsOpen();
}

void UInGameMenuWidget::OnSettingsClosed()
{
	bIsShowingSettings = false;
	GetWorld()->GetTimerManager().ClearTimer(SettingsHideTimerHandle);

	// 위젯 스위처가 있으면 0번 슬롯(메인)으로 전환
	if (WidgetSwitcher_Menu)
	{
		WidgetSwitcher_Menu->SetActiveWidgetIndex(0);
	}

	// 슬라이드 아웃 애니메이션이 있으면 재생 후 설정창 숨기고 메인 메뉴 복원
	if (Anim_SettingsSlideOut && Anim_SettingsSlideOut->GetEndTime() > 0.05f)
	{
		PlayAnimation(Anim_SettingsSlideOut);
		const float AnimLength = FMath::Min(Anim_SettingsSlideOut->GetEndTime(), 0.5f);
		GetWorld()->GetTimerManager().SetTimer(SettingsHideTimerHandle, [this]()
		{
			if (!bIsShowingSettings)
			{
				if (SettingsMenuWidget)
				{
					SettingsMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
				}
				if (Panel_MainMenu)
				{
					Panel_MainMenu->SetVisibility(ESlateVisibility::Visible);
				}
			}
		}, AnimLength, false);
	}
	else
	{
		// 즉시 설정창 숨기고 메인 메뉴 복원
		if (SettingsMenuWidget)
		{
			SettingsMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (Panel_MainMenu)
		{
			Panel_MainMenu->SetVisibility(ESlateVisibility::Visible);
		}
	}

	BP_OnSettingsClose();
}

void UInGameMenuWidget::ReturnToLobby()
{
	if (ATeamProject_MOUPlayerController* MOU_PC = Cast<ATeamProject_MOUPlayerController>(GetOwningPlayer()))
	{
		MOU_PC->ReturnToLobby();
	}
}

void UInGameMenuWidget::QuitToDesktop()
{
	if (ATeamProject_MOUPlayerController* MOU_PC = Cast<ATeamProject_MOUPlayerController>(GetOwningPlayer()))
	{
		MOU_PC->QuitToDesktop();
	}
}

bool UInGameMenuWidget::HandleBackOrEscape()
{
	if (bIsShowingSettings)
	{
		OnSettingsClosed();
		return true;
	}

	ResumeGame();
	return true;
}

void UInGameMenuWidget::OnResumeClicked()
{
	ResumeGame();
}

void UInGameMenuWidget::OnSettingsClicked()
{
	if (bIsShowingSettings)
	{
		OnSettingsClosed();
	}
	else
	{
		OpenSettings();
	}
}

void UInGameMenuWidget::OnReturnToLobbyClicked()
{
	ReturnToLobby();
}

void UInGameMenuWidget::OnQuitDesktopClicked()
{
	QuitToDesktop();
}
