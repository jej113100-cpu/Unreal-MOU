// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/KeyBindingMenuWidget.h"
#include "UI/MOU_GameUserSettings.h"
#include "Components/Button.h"
#include "Components/InputKeySelector.h"
#include "Components/ScrollBox.h"
#include "Animation/WidgetAnimation.h"
#include "Input/Events.h"

void UKeyBindingMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 포커스 가능하도록 설정 (ESC 키 이벤트 수신을 위함)
	SetIsFocusable(true);

	// 네비게이션 버튼 바인딩
	if (Button_Back)
	{
		Button_Back->OnClicked.AddDynamic(this, &UKeyBindingMenuWidget::OnBackClicked);
	}
	if (Button_Close)
	{
		Button_Close->OnClicked.AddDynamic(this, &UKeyBindingMenuWidget::OnBackClicked);
	}
	if (Button_ResetDefaults)
	{
		Button_ResetDefaults->OnClicked.AddDynamic(this, &UKeyBindingMenuWidget::OnResetDefaultsClicked);
	}

	// 키 선택기 바인딩: 1. 이동 (WASD)
	if (KeySelector_MoveForward)
	{
		KeySelector_MoveForward->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnMoveForwardKeySelected);
	}
	if (KeySelector_MoveBackward)
	{
		KeySelector_MoveBackward->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnMoveBackwardKeySelected);
	}
	if (KeySelector_MoveLeft)
	{
		KeySelector_MoveLeft->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnMoveLeftKeySelected);
	}
	if (KeySelector_MoveRight)
	{
		KeySelector_MoveRight->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnMoveRightKeySelected);
	}

	// 키 선택기 바인딩: 2. 기본 행동
	if (KeySelector_Jump)
	{
		KeySelector_Jump->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnJumpKeySelected);
	}
	if (KeySelector_Sprint)
	{
		KeySelector_Sprint->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnSprintKeySelected);
	}
	if (KeySelector_Interact)
	{
		KeySelector_Interact->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnInteractKeySelected);
	}
	if (KeySelector_GrabDrop)
	{
		KeySelector_GrabDrop->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnGrabDropKeySelected);
	}
	if (KeySelector_Use)
	{
		KeySelector_Use->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnUseKeySelected);
	}
	if (KeySelector_Throw)
	{
		KeySelector_Throw->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnThrowKeySelected);
	}
	if (KeySelector_Slap)
	{
		KeySelector_Slap->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnSlapKeySelected);
	}
	if (KeySelector_Light)
	{
		KeySelector_Light->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnLightKeySelected);
	}
	if (KeySelector_LightColor)
	{
		KeySelector_LightColor->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnLightColorKeySelected);
	}
	if (KeySelector_Slot1)
	{
		KeySelector_Slot1->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnSlot1KeySelected);
	}
	if (KeySelector_Slot2)
	{
		KeySelector_Slot2->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnSlot2KeySelected);
	}
	if (KeySelector_Slot3)
	{
		KeySelector_Slot3->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnSlot3KeySelected);
	}
	if (KeySelector_Quest)
	{
		KeySelector_Quest->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnQuestKeySelected);
	}
	if (KeySelector_ViewEconomy)
	{
		KeySelector_ViewEconomy->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnViewEconomyKeySelected);
	}
	if (KeySelector_Emote)
	{
		KeySelector_Emote->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnEmoteKeySelected);
	}
	if (KeySelector_RadioPower)
	{
		KeySelector_RadioPower->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnRadioPowerKeySelected);
	}
	if (KeySelector_RadioTransmit)
	{
		KeySelector_RadioTransmit->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnRadioTransmitKeySelected);
	}
	if (KeySelector_VoiceMute)
	{
		KeySelector_VoiceMute->OnKeySelected.AddDynamic(this, &UKeyBindingMenuWidget::OnVoiceMuteKeySelected);
	}

	// 현재 설정된 키 로드
	RefreshUIFromSettings();

	bIsClosing = false;
	if (Anim_Open)
	{
		PlayAnimation(Anim_Open);
	}
	BP_OnMenuOpen();
}

FReply UKeyBindingMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// ESC 키를 누르면 키 설정 팝업 창을 닫고 부모 메뉴로 복귀
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		CloseMenu();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

UMOU_GameUserSettings* UKeyBindingMenuWidget::GetUserSettings() const
{
	return UMOU_GameUserSettings::GetMOUGameUserSettings();
}

void UKeyBindingMenuWidget::RefreshUIFromSettings()
{
	UMOU_GameUserSettings* UserSettings = GetUserSettings();
	if (!UserSettings)
	{
		return;
	}

	// 1. 이동 (WASD)
	if (KeySelector_MoveForward)
	{
		KeySelector_MoveForward->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("Move_Forward"), EKeys::W)));
	}
	if (KeySelector_MoveBackward)
	{
		KeySelector_MoveBackward->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("Move_Backward"), EKeys::S)));
	}
	if (KeySelector_MoveLeft)
	{
		KeySelector_MoveLeft->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("Move_Left"), EKeys::A)));
	}
	if (KeySelector_MoveRight)
	{
		KeySelector_MoveRight->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("Move_Right"), EKeys::D)));
	}

	// 2. 기본 행동
	if (KeySelector_Jump)
	{
		KeySelector_Jump->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Jump"), EKeys::SpaceBar)));
	}
	if (KeySelector_Sprint)
	{
		KeySelector_Sprint->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Sprint"), EKeys::LeftShift)));
	}

	// 3. 상호작용 및 상호동작
	if (KeySelector_Interact)
	{
		KeySelector_Interact->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Interact"), EKeys::E)));
	}
	if (KeySelector_GrabDrop)
	{
		KeySelector_GrabDrop->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Grab_Drop"), EKeys::F)));
	}

	// 4. 전투 및 도구 사용
	if (KeySelector_Use)
	{
		KeySelector_Use->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Use"), EKeys::LeftMouseButton)));
	}
	if (KeySelector_Throw)
	{
		KeySelector_Throw->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Throw"), EKeys::RightMouseButton)));
	}
	if (KeySelector_Slap)
	{
		KeySelector_Slap->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Slap"), EKeys::Q)));
	}

	// 5. 손전등 및 조명
	if (KeySelector_Light)
	{
		KeySelector_Light->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_Light"), EKeys::T)));
	}
	if (KeySelector_LightColor)
	{
		KeySelector_LightColor->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_LightColor"), EKeys::Y)));
	}

	// 6. 인벤토리 퀵슬롯
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

	// 7. 퀘스트, 경제, 메뉴
	if (KeySelector_Quest)
	{
		KeySelector_Quest->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_CapsLock"), EKeys::CapsLock)));
	}
	if (KeySelector_ViewEconomy)
	{
		KeySelector_ViewEconomy->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_ViewEcnomoy"), EKeys::Tab)));
	}
	if (KeySelector_Emote)
	{
		KeySelector_Emote->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("IA_EmoteToggle"), EKeys::V)));
	}

	// 8. 무전 및 음성 대화
	if (KeySelector_RadioPower)
	{
		KeySelector_RadioPower->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("Radio_Power"), EKeys::Z)));
	}
	if (KeySelector_RadioTransmit)
	{
		KeySelector_RadioTransmit->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("Radio_Transmit"), EKeys::X)));
	}
	if (KeySelector_VoiceMute)
	{
		KeySelector_VoiceMute->SetSelectedKey(FInputChord(UserSettings->GetCustomKeyBinding(FName("Voice_Mute"), EKeys::C)));
	}
}

void UKeyBindingMenuWidget::ResetToDefaults()
{
	if (UMOU_GameUserSettings* UserSettings = GetUserSettings())
	{
		UserSettings->ClearAllCustomKeyBindings();
		UserSettings->SaveSettings();
		RefreshUIFromSettings();
	}
}

void UKeyBindingMenuWidget::CloseMenu()
{
	if (bIsClosing)
	{
		return;
	}
	bIsClosing = true;

	BP_OnMenuClose();

	if (Anim_Close && Anim_Close->GetEndTime() > 0.05f)
	{
		PlayAnimation(Anim_Close);
		const float AnimLength = FMath::Min(Anim_Close->GetEndTime(), 0.5f);
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(CloseTimerHandle, this, &UKeyBindingMenuWidget::FinishCloseMenu, AnimLength, false);
		}
		else
		{
			FinishCloseMenu();
		}
	}
	else
	{
		FinishCloseMenu();
	}
}

void UKeyBindingMenuWidget::FinishCloseMenu()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CloseTimerHandle);
	}
	OnKeyBindingMenuClosed.Broadcast();
	RemoveFromParent();
}

void UKeyBindingMenuWidget::RebindActionKey(FName ActionName, const FKey& NewKey)
{
	if (UMOU_GameUserSettings* UserSettings = GetUserSettings())
	{
		UserSettings->SetCustomKeyBinding(ActionName, NewKey);
		UserSettings->SaveSettings();
	}
}

// -----------------------------------------------------------------------------
// 키 선택 이벤트 핸들러들
// -----------------------------------------------------------------------------

void UKeyBindingMenuWidget::OnMoveForwardKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("Move_Forward"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnMoveBackwardKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("Move_Backward"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnMoveLeftKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("Move_Left"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnMoveRightKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("Move_Right"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnJumpKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Jump"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnSprintKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Sprint"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnInteractKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Interact"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnGrabDropKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Grab_Drop"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnUseKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Use"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnThrowKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Throw"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnSlapKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Slap"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnLightKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Light"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnLightColorKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_LightColor"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnSlot1KeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Slot1"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnSlot2KeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Slot2"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnSlot3KeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_Slot3"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnQuestKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_CapsLock"), SelectedKey.Key);
	RebindActionKey(FName("IA_QuestAsk"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnViewEconomyKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_ViewEcnomoy"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnEmoteKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("IA_EmoteToggle"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnRadioPowerKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("Radio_Power"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnRadioTransmitKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("Radio_Transmit"), SelectedKey.Key);
}

void UKeyBindingMenuWidget::OnVoiceMuteKeySelected(FInputChord SelectedKey)
{
	RebindActionKey(FName("Voice_Mute"), SelectedKey.Key);
}

// -----------------------------------------------------------------------------
// 버튼 핸들러들
// -----------------------------------------------------------------------------

void UKeyBindingMenuWidget::OnBackClicked()
{
	CloseMenu();
}

void UKeyBindingMenuWidget::OnResetDefaultsClicked()
{
	ResetToDefaults();
}
