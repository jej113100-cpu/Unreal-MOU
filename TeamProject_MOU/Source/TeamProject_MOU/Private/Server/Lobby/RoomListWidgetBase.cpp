// MOU 로비 - 방 목록 / 참여 UI 구현.
//
// 이 파일은 소켓/패킷을 전혀 모른다. UServerSubsystem 하고만 대화한다.
//   보낼 때: UServerSubsystem::RequestRoomList() / JoinRoom()
//   받을 때: UServerSubsystem::OnRoomListReceived / OnRoomJoinCompleted

#include "Server/Lobby/RoomListWidgetBase.h"

#include "Server/ServerSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

// ===========================================================================
// URoomListEntryWidget - 목록의 한 줄
// ===========================================================================

void URoomListEntryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
	}
}

void URoomListEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (EntryJoinButton != nullptr)
	{
		EntryJoinButton->OnClicked.AddUniqueDynamic(this, &URoomListEntryWidget::HandleJoinClicked);
	}
}

// 한 줄의 기본 모양:
//   HorizontalBox
//     ├ EntryLockText     [비번]  (공개방이면 빈칸)
//     ├ EntryTitleText    방 제목 (Fill)
//     ├ EntryHostText     방장 닉네임
//     ├ EntryPlayersText  2 / 4
//     └ EntryJoinButton   참여
void URoomListEntryWidget::BuildDefaultLayout()
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RoomEntryRow"));
	WidgetTree->RootWidget = Row;

	auto AddCell = [&](UWidget* Widget, ESlateSizeRule::Type Rule, float RightPadding)
	{
		if (UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Widget))
		{
			Slot->SetSize(FSlateChildSize(Rule));
			Slot->SetVerticalAlignment(VAlign_Center);
			Slot->SetPadding(FMargin(0.f, 0.f, RightPadding, 0.f));
		}
	};

	EntryLockText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EntryLockText"));
	EntryLockText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.85f, 0.4f)));
	AddCell(EntryLockText, ESlateSizeRule::Automatic, 6.f);

	// 제목만 Fill 이다. 방 이름이 길어져도 오른쪽의 인원수와 참여 버튼은 제자리를 지킨다.
	EntryTitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EntryTitleText"));
	EntryTitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	AddCell(EntryTitleText, ESlateSizeRule::Fill, 8.f);

	EntryHostText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EntryHostText"));
	EntryHostText->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)));
	AddCell(EntryHostText, ESlateSizeRule::Automatic, 8.f);

	EntryPlayersText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EntryPlayersText"));
	EntryPlayersText->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.85f, 1.f)));
	AddCell(EntryPlayersText, ESlateSizeRule::Automatic, 8.f);

	EntryJoinButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("EntryJoinButton"));
	UTextBlock* JoinLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EntryJoinButtonLabel"));
	JoinLabel->SetText(FText::FromString(TEXT("참여")));
	EntryJoinButton->AddChild(JoinLabel);
	AddCell(EntryJoinButton, ESlateSizeRule::Automatic, 0.f);
}

void URoomListEntryWidget::SetRoomInfo(const FMOURoomInfo& InRoomInfo)
{
	RoomInfo = InRoomInfo;
	RefreshTexts();
	OnRoomInfoSet(RoomInfo);
}

void URoomListEntryWidget::RefreshTexts()
{
	if (EntryTitleText != nullptr)
	{
		EntryTitleText->SetText(FText::FromString(FString::Printf(TEXT("#%d  %s"), RoomInfo.RoomId, *RoomInfo.Title)));
	}
	if (EntryHostText != nullptr)
	{
		EntryHostText->SetText(FText::FromString(RoomInfo.HostName));
	}
	if (EntryPlayersText != nullptr)
	{
		EntryPlayersText->SetText(FText::FromString(
			FString::Printf(TEXT("%d / %d"), RoomInfo.CurrentPlayers, RoomInfo.MaxPlayers)));
	}
	if (EntryLockText != nullptr)
	{
		EntryLockText->SetText(FText::FromString(RoomInfo.bHasPassword ? TEXT("[비번]") : TEXT("")));
	}
}

void URoomListEntryWidget::RequestJoin()
{
	OnJoinClicked.ExecuteIfBound(RoomInfo.RoomId);
}

void URoomListEntryWidget::HandleJoinClicked() { RequestJoin(); }

// ===========================================================================
// URoomListWidgetBase - 목록 전체
// ===========================================================================

URoomListWidgetBase::URoomListWidgetBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

// ---------------------------------------------------------------------------
// 수명 주기
// ---------------------------------------------------------------------------

void URoomListWidgetBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
	}
}

void URoomListWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	if (RefreshButton != nullptr)
	{
		RefreshButton->OnClicked.AddUniqueDynamic(this, &URoomListWidgetBase::HandleRefreshClicked);
	}
	if (CloseButton != nullptr)
	{
		CloseButton->OnClicked.AddUniqueDynamic(this, &URoomListWidgetBase::HandleCloseClicked);
	}
	if (JoinConfirmButton != nullptr)
	{
		JoinConfirmButton->OnClicked.AddUniqueDynamic(this, &URoomListWidgetBase::HandleJoinConfirmClicked);
	}
	if (JoinCancelButton != nullptr)
	{
		JoinCancelButton->OnClicked.AddUniqueDynamic(this, &URoomListWidgetBase::HandleJoinCancelClicked);
	}

	// NativeConstruct 는 뷰포트에 다시 붙을 때마다 불릴 수 있어 중복 구독을 막는다.
	if (!bSubscribed)
	{
		if (UServerSubsystem* Chat = GetServerSubsystem())
		{
			Chat->OnRoomListReceived.AddDynamic(this, &URoomListWidgetBase::HandleRoomListReceived);
			Chat->OnRoomJoinCompleted.AddDynamic(this, &URoomListWidgetBase::HandleRoomJoinCompleted);
			bSubscribed = true;
		}
	}

	if (bManageMouseCursor)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			InputMode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(InputMode);
			PC->SetShowMouseCursor(true);
		}
	}

	ShowPasswordPrompt(false);
	RefreshRoomList();

	// 방은 서버 메모리에만 있고 호스트가 끊기면 사라진다.
	// 주기적으로 다시 받아오지 않으면 이미 없는 방을 계속 보여주게 된다.
	if (AutoRefreshInterval > 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				RefreshTimerHandle, this, &URoomListWidgetBase::RefreshRoomList, AutoRefreshInterval, /*bLoop=*/true);
		}
	}
}

void URoomListWidgetBase::NativeDestruct()
{
	// 타이머를 먼저 끈다. 위젯이 사라진 뒤에 타이머가 돌면 죽은 객체를 호출한다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimerHandle);
	}

	if (bSubscribed)
	{
		if (UServerSubsystem* Chat = GetServerSubsystem())
		{
			Chat->OnRoomListReceived.RemoveDynamic(this, &URoomListWidgetBase::HandleRoomListReceived);
			Chat->OnRoomJoinCompleted.RemoveDynamic(this, &URoomListWidgetBase::HandleRoomJoinCompleted);
		}
		bSubscribed = false;
	}

	Super::NativeDestruct();
}

// ---------------------------------------------------------------------------
// 기본 레이아웃 조립 (WBP 가 없을 때만)
//
//   CanvasPanel (화면 전체)
//     └ Border (화면 정중앙 640x420, 반투명 검정)
//         └ VerticalBox
//             ├ HorizontalBox
//             │   ├ TitleText        "방 목록"
//             │   ├ RefreshButton
//             │   └ CloseButton
//             ├ RoomListScrollBox    (Fill)
//             │   └ RoomListBox      여기에 줄이 쌓인다
//             ├ PasswordPromptPanel  비밀번호 방을 고를 때만 보인다
//             │   ├ JoinPasswordBox
//             │   ├ JoinConfirmButton
//             │   └ JoinCancelButton
//             └ StatusText
// ---------------------------------------------------------------------------

void URoomListWidgetBase::BuildDefaultLayout()
{
	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RoomListRootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RoomListPanel"));
	Panel->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.04f, 0.94f));
	Panel->SetPadding(FMargin(20.f));

	UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(Panel);
	PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	PanelSlot->SetAutoSize(false);
	PanelSlot->SetPosition(FVector2D::ZeroVector);
	PanelSlot->SetSize(FVector2D(640.f, 420.f));

	UVerticalBox* MainBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoomListMainBox"));
	Panel->AddChild(MainBox);

	auto AddRow = [&](UWidget* Widget, ESlateSizeRule::Type Rule, float BottomPadding)
	{
		if (UVerticalBoxSlot* Slot = MainBox->AddChildToVerticalBox(Widget))
		{
			Slot->SetSize(FSlateChildSize(Rule));
			Slot->SetPadding(FMargin(0.f, 0.f, 0.f, BottomPadding));
		}
	};

	// --- 제목줄 ---
	UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RoomListHeaderRow"));
	AddRow(HeaderRow, ESlateSizeRule::Automatic, 10.f);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	TitleText->SetText(FText::FromString(TEXT("방 목록")));
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	// 지역 변수 이름을 HeaderSlot 으로 두는 이유: UWidget 에 Slot 멤버가 있어서
	// 람다 밖에서 Slot 이라는 이름을 쓰면 C4458(멤버를 가림) 경고가 에러로 승격된다.
	if (UHorizontalBoxSlot* HeaderSlot = HeaderRow->AddChildToHorizontalBox(TitleText))
	{
		HeaderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HeaderSlot->SetVerticalAlignment(VAlign_Center);
	}

	auto MakeHeaderButton = [&](const TCHAR* Name, const FString& Label) -> UButton*
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* ButtonLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *(FString(Name) + TEXT("Label")));
		ButtonLabel->SetText(FText::FromString(Label));
		Button->AddChild(ButtonLabel);

		if (UHorizontalBoxSlot* Slot = HeaderRow->AddChildToHorizontalBox(Button))
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			Slot->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
		}
		return Button;
	};

	RefreshButton = MakeHeaderButton(TEXT("RefreshButton"), TEXT("새로고침"));
	CloseButton   = MakeHeaderButton(TEXT("CloseButton"),   TEXT("닫기"));

	// --- 목록 영역 ---
	RoomListScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("RoomListScrollBox"));
	AddRow(RoomListScrollBox, ESlateSizeRule::Fill, 8.f);

	RoomListBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoomListBox"));
	RoomListScrollBox->AddChild(RoomListBox);

	// --- 비밀번호 입력줄 ---
	UHorizontalBox* PromptRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PasswordPromptPanel"));
	PasswordPromptPanel = PromptRow;
	AddRow(PromptRow, ESlateSizeRule::Automatic, 8.f);

	JoinPasswordBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("JoinPasswordBox"));
	JoinPasswordBox->SetHintText(FText::FromString(TEXT("방 비밀번호 숫자 4자리")));
	if (UHorizontalBoxSlot* PasswordSlot = PromptRow->AddChildToHorizontalBox(JoinPasswordBox))
	{
		PasswordSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		PasswordSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	}

	auto MakePromptButton = [&](const TCHAR* Name, const FString& Label) -> UButton*
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* ButtonLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *(FString(Name) + TEXT("Label")));
		ButtonLabel->SetText(FText::FromString(Label));
		Button->AddChild(ButtonLabel);

		if (UHorizontalBoxSlot* Slot = PromptRow->AddChildToHorizontalBox(Button))
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			Slot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		}
		return Button;
	};

	JoinConfirmButton = MakePromptButton(TEXT("JoinConfirmButton"), TEXT("확인"));
	JoinCancelButton  = MakePromptButton(TEXT("JoinCancelButton"),  TEXT("취소"));

	// --- 상태줄 ---
	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetAutoWrapText(true);
	StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)));
	AddRow(StatusText, ESlateSizeRule::Automatic, 0.f);
}

// ---------------------------------------------------------------------------
// 목록
// ---------------------------------------------------------------------------

UServerSubsystem* URoomListWidgetBase::GetServerSubsystem() const
{
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		return GameInstance->GetSubsystem<UServerSubsystem>();
	}
	return nullptr;
}

void URoomListWidgetBase::RefreshRoomList()
{
	UServerSubsystem* Chat = GetServerSubsystem();
	if (Chat == nullptr)
	{
		SetStatus(TEXT("채팅 시스템을 찾을 수 없습니다."), true);
		return;
	}

	// 로그인 전이면 서버가 빈 목록만 돌려준다. 사유를 미리 알려준다.
	if (Chat->GetConnectionState() != EChatConnectionState::LoggedIn)
	{
		SetStatus(UServerSubsystem::GetRoomResultText(EMOURoomResultBP::NotAuthed), true);
		return;
	}

	Chat->RequestRoomList();
}

void URoomListWidgetBase::HandleRoomListReceived(const TArray<FMOURoomInfo>& Rooms)
{
	CachedRooms = Rooms;
	RebuildEntries(Rooms);

	// 비밀번호를 입력하던 중에 새로고침이 돌아 그 방이 사라졌다면 입력창을 닫는다.
	// 안 그러면 없는 방에 비밀번호를 넣고 "없는 방" 이라는 답만 받게 된다.
	if (PendingJoinRoomId != 0)
	{
		const bool bStillThere = CachedRooms.ContainsByPredicate(
			[this](const FMOURoomInfo& Room) { return Room.RoomId == PendingJoinRoomId; });
		if (!bStillThere)
		{
			ShowPasswordPrompt(false);
			PendingJoinRoomId = 0;
			SetStatus(TEXT("고른 방이 사라졌습니다. 목록에서 다시 선택하세요."), true);
			return;
		}
	}

	if (Rooms.Num() == 0)
	{
		SetStatus(TEXT("대기 중인 방이 없습니다. 방을 만들어보세요."), false);
	}
	else if (PendingJoinRoomId == 0)
	{
		SetStatus(FString::Printf(TEXT("방 %d개"), Rooms.Num()), false);
	}
}

void URoomListWidgetBase::RebuildEntries(const TArray<FMOURoomInfo>& Rooms)
{
	if (RoomListBox == nullptr)
	{
		return;
	}

	// 줄을 재사용하지 않고 통째로 다시 만든다.
	// 방 개수 상한이 20 이라(kMaxRoomsInList) 매 초 다시 만들어도 부담이 없고,
	// 재사용 로직을 두면 "사라진 방의 버튼이 남아있는" 종류의 버그가 생긴다.
	RoomListBox->ClearChildren();
	EntryWidgets.Reset();

	UClass* EntryClass = EntryWidgetClass ? EntryWidgetClass.Get() : URoomListEntryWidget::StaticClass();

	for (const FMOURoomInfo& Room : Rooms)
	{
		URoomListEntryWidget* Entry = CreateWidget<URoomListEntryWidget>(this, EntryClass);
		if (Entry == nullptr)
		{
			continue;
		}

		// 줄이 자기 RoomId 를 들고 있으므로, 클릭이 오면 어느 방인지 알 수 있다.
		Entry->OnJoinClicked.BindUObject(this, &URoomListWidgetBase::HandleEntryJoinClicked);
		Entry->SetRoomInfo(Room);

		if (UVerticalBoxSlot* EntrySlot = RoomListBox->AddChildToVerticalBox(Entry))
		{
			EntrySlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			EntrySlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		}
		EntryWidgets.Add(Entry);
	}
}

// ---------------------------------------------------------------------------
// 참여
// ---------------------------------------------------------------------------

void URoomListWidgetBase::HandleEntryJoinClicked(int32 RoomId)
{
	BeginJoin(RoomId);
}

void URoomListWidgetBase::BeginJoin(int32 RoomId)
{
	if (bBusy || RoomId == 0)
	{
		return;
	}

	const FMOURoomInfo* Room = CachedRooms.FindByPredicate(
		[RoomId](const FMOURoomInfo& Candidate) { return Candidate.RoomId == RoomId; });

	// 목록에 없는 방이면 그냥 보내본다. 판정은 어차피 서버가 한다.
	const bool bNeedsPassword = (Room != nullptr) && Room->bHasPassword;

	if (!bNeedsPassword)
	{
		PendingJoinRoomId = 0;
		ShowPasswordPrompt(false);
		SendJoinRequest(RoomId, FString());
		return;
	}

	// 비밀번호 방이다. 바로 보내지 않고 입력을 먼저 받는다.
	PendingJoinRoomId = RoomId;
	ShowPasswordPrompt(true);
	SetStatus(FString::Printf(TEXT("방 #%d 의 비밀번호(숫자 4자리)를 입력하세요."), RoomId), false);
}

void URoomListWidgetBase::ConfirmJoinWithPassword()
{
	if (bBusy || PendingJoinRoomId == 0)
	{
		return;
	}

	FString RoomPassword = JoinPasswordBox ? JoinPasswordBox->GetText().ToString() : FString();
	RoomPassword.TrimStartAndEndInline();

	// 서버도 검사하지만, 형식이 틀린 것은 왕복 없이 즉시 알려줄 수 있다.
	if (!UServerSubsystem::IsValidRoomPassword(RoomPassword))
	{
		SetStatus(TEXT("방 비밀번호는 숫자 4자리여야 합니다."), true);
		return;
	}

	SendJoinRequest(PendingJoinRoomId, RoomPassword);
}

void URoomListWidgetBase::CancelPasswordPrompt()
{
	PendingJoinRoomId = 0;
	ShowPasswordPrompt(false);
	SetStatus(TEXT("참여를 취소했습니다."), false);
}

void URoomListWidgetBase::SendJoinRequest(int32 RoomId, const FString& RoomPassword)
{
	UServerSubsystem* Chat = GetServerSubsystem();
	if (Chat == nullptr)
	{
		SetStatus(TEXT("채팅 시스템을 찾을 수 없습니다."), true);
		return;
	}

	// 승인되면 이 값을 여행 URL 에 다시 실어야 한다. 서버는 되돌려주지 않는다.
	SubmittedPassword = RoomPassword;

	SetBusy(true);
	SetStatus(FString::Printf(TEXT("방 #%d 에 참여하는 중..."), RoomId), false);
	Chat->JoinRoom(RoomId, RoomPassword);
}

void URoomListWidgetBase::HandleRoomJoinCompleted(const FMOURoomJoinResult& Result)
{
	SetBusy(false);

	if (!Result.bSuccess)
	{
		SetStatus(UServerSubsystem::GetRoomResultText(Result.Result), true);

		// 비밀번호가 틀렸을 때만 입력창을 열어둔 채로 다시 시도하게 한다.
		// 정원 초과나 없는 방이면 비밀번호를 고쳐봐야 소용없으므로 목록으로 돌려보낸다.
		if (Result.Result != EMOURoomResultBP::WrongPassword)
		{
			PendingJoinRoomId = 0;
			ShowPasswordPrompt(false);
		}
		SubmittedPassword.Empty();
		return;
	}

	SetStatus(FString::Printf(TEXT("참여 승인. 호스트 %s"), *Result.ToDisplayString()), false);

	PendingJoinRoomId = 0;
	ShowPasswordPrompt(false);

	// 여행은 소유자/블루프린트의 몫이다. 언제 떠날지는 게임 흐름이 정한다.
	const FString UsedPassword = SubmittedPassword;
	SubmittedPassword.Empty();

	OnRoomJoinApproved(Result, UsedPassword);
	OnRoomJoinApprovedNative.ExecuteIfBound(Result, UsedPassword);

	if (bRemoveOnSuccess)
	{
		RemoveFromParent();
	}
}

void URoomListWidgetBase::CloseList()
{
	OnRoomListClosed.ExecuteIfBound();
	RemoveFromParent();
}

void URoomListWidgetBase::HandleRefreshClicked()     { RefreshRoomList(); }
void URoomListWidgetBase::HandleCloseClicked()       { CloseList(); }
void URoomListWidgetBase::HandleJoinConfirmClicked() { ConfirmJoinWithPassword(); }
void URoomListWidgetBase::HandleJoinCancelClicked()  { CancelPasswordPrompt(); }

// ---------------------------------------------------------------------------
// 표시
// ---------------------------------------------------------------------------

void URoomListWidgetBase::ShowPasswordPrompt(bool bShow)
{
	if (PasswordPromptPanel == nullptr)
	{
		return;
	}

	// Collapsed 로 접어야 자리까지 사라진다. Hidden 은 빈 공간을 남긴다.
	PasswordPromptPanel->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	if (JoinPasswordBox != nullptr)
	{
		// 방을 바꿔 고를 때 이전 방의 비밀번호가 남아있지 않게 항상 비운다.
		JoinPasswordBox->SetText(FText::GetEmpty());
		if (bShow)
		{
			JoinPasswordBox->SetKeyboardFocus();
		}
	}
}

void URoomListWidgetBase::SetBusy(bool bInBusy)
{
	bBusy = bInBusy;
	if (JoinConfirmButton != nullptr)
	{
		JoinConfirmButton->SetIsEnabled(!bInBusy);
	}
	for (const TObjectPtr<URoomListEntryWidget>& Entry : EntryWidgets)
	{
		if (Entry != nullptr)
		{
			Entry->SetIsEnabled(!bInBusy);
		}
	}
}

void URoomListWidgetBase::SetStatus(const FString& Text, bool bIsError)
{
	if (StatusText == nullptr)
	{
		return;
	}
	StatusText->SetText(FText::FromString(Text));
	StatusText->SetColorAndOpacity(FSlateColor(bIsError
		? FLinearColor(1.f, 0.45f, 0.45f)
		: FLinearColor(0.75f, 0.75f, 0.75f)));
}
