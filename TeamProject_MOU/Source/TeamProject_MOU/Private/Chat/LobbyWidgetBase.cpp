// MOU 로비 - 메인메뉴 + 대기실 UI 구현.
//
// 이 파일은 소켓/패킷을 전혀 모른다.
//   상태 조회: UChatSubsystem (연결 상태 / 내 신원 / 방 번호 / 대기실 명단)
//   서버 왕복: 방에 들어가기 전에는 자식 창이, 들어간 뒤에는 서브시스템 API 가 한다.
//
// [화면을 바꾸는 곳은 RefreshUI() 하나뿐이다]
//   버튼 라벨, 활성화 여부, 명단 표시를 전부 거기서 결정한다.
//   여기저기서 SetText 를 부르기 시작하면 "준비하기" 라고 적힌 버튼이
//   방을 만드는 식의 어긋남이 반드시 생긴다.

#include "Chat/LobbyWidgetBase.h"

#include "Chat/ChatSubsystem.h"
#include "Chat/RoomCreateWidgetBase.h"
#include "Chat/RoomListWidgetBase.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

ULobbyWidgetBase::ULobbyWidgetBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

// ---------------------------------------------------------------------------
// 수명 주기
// ---------------------------------------------------------------------------

void ULobbyWidgetBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
	}
}

void ULobbyWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	if (PrimaryButton != nullptr)
	{
		PrimaryButton->OnClicked.AddUniqueDynamic(this, &ULobbyWidgetBase::HandlePrimaryClicked);
	}
	if (SecondaryButton != nullptr)
	{
		SecondaryButton->OnClicked.AddUniqueDynamic(this, &ULobbyWidgetBase::HandleSecondaryClicked);
	}
	if (TertiaryButton != nullptr)
	{
		TertiaryButton->OnClicked.AddUniqueDynamic(this, &ULobbyWidgetBase::HandleTertiaryClicked);
	}

	// NativeConstruct 는 뷰포트에 다시 붙을 때마다 불릴 수 있어 중복 구독을 막는다.
	if (!bSubscribed)
	{
		if (UChatSubsystem* Chat = GetChatSubsystem())
		{
			Chat->OnChatStateChanged.AddDynamic(this, &ULobbyWidgetBase::HandleChatStateChanged);
			Chat->OnChatLoginCompleted.AddDynamic(this, &ULobbyWidgetBase::HandleLoginCompleted);
			Chat->OnRoomMembersChanged.AddDynamic(this, &ULobbyWidgetBase::HandleRoomMembersChanged);
			Chat->OnRoomClosed.AddDynamic(this, &ULobbyWidgetBase::HandleRoomClosed);
			Chat->OnRoomGameStarted.AddDynamic(this, &ULobbyWidgetBase::HandleGameStarted);
			Chat->OnRoomHostReady.AddDynamic(this, &ULobbyWidgetBase::HandleHostReady);
			bSubscribed = true;
		}
	}

	if (bManageMouseCursor)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			// GameAndUI : 로비 뒤에서 게임이 돌고 있어도 입력이 완전히 막히지 않는다.
			FInputModeGameAndUI InputMode;
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			InputMode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(InputMode);
			PC->SetShowMouseCursor(true);
		}
	}

	// 서브시스템은 레벨을 넘어가도 살아있으므로, 이 위젯이 새로 만들어졌을 때
	// 이미 방 안일 수 있다. 지금 상태를 물어보고 거기에 맞춰 연다.
	if (const UChatSubsystem* Chat = GetChatSubsystem())
	{
		UIState = (Chat->GetCurrentRoomId() != 0)
			? EMOULobbyUIState::WaitingRoom
			: EMOULobbyUIState::MainMenu;
	}

	RefreshUI();
}

void ULobbyWidgetBase::NativeDestruct()
{
	// 로비가 사라질 때 열려 있던 자식 창도 같이 정리한다.
	CloseChildWidgets();

	if (bSubscribed)
	{
		if (UChatSubsystem* Chat = GetChatSubsystem())
		{
			Chat->OnChatStateChanged.RemoveDynamic(this, &ULobbyWidgetBase::HandleChatStateChanged);
			Chat->OnChatLoginCompleted.RemoveDynamic(this, &ULobbyWidgetBase::HandleLoginCompleted);
			Chat->OnRoomMembersChanged.RemoveDynamic(this, &ULobbyWidgetBase::HandleRoomMembersChanged);
			Chat->OnRoomClosed.RemoveDynamic(this, &ULobbyWidgetBase::HandleRoomClosed);
			Chat->OnRoomGameStarted.RemoveDynamic(this, &ULobbyWidgetBase::HandleGameStarted);
			Chat->OnRoomHostReady.RemoveDynamic(this, &ULobbyWidgetBase::HandleHostReady);
		}
		bSubscribed = false;
	}

	if (bManageMouseCursor)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			PC->SetInputMode(FInputModeGameOnly());
			PC->SetShowMouseCursor(false);
		}
	}

	Super::NativeDestruct();
}

// ---------------------------------------------------------------------------
// 기본 레이아웃 조립 (WBP 가 없을 때만)
//
//   CanvasPanel (화면 전체)
//     └ Border (화면 정중앙 380x420, 반투명 검정)
//         └ VerticalBox
//             ├ TitleText          "MOU 로비" / "대기실"
//             ├ StatusText         닉네임 / 방 번호
//             ├ MemberListBox      대기실 명단 (메인메뉴에서는 접힘)
//             ├ PrimaryButton      방 만들기 / 준비하기 / 게임 시작
//             ├ SecondaryButton    참여하기 / 커스터마이징
//             ├ TertiaryButton     게임 종료 / 나가기
//             └ MessageText        안내 / 실패 사유
// ---------------------------------------------------------------------------

void ULobbyWidgetBase::BuildDefaultLayout()
{
	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LobbyRootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LobbyPanel"));
	Panel->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.04f, 0.92f));
	Panel->SetPadding(FMargin(20.f));

	UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(Panel);
	PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	PanelSlot->SetAutoSize(false);
	PanelSlot->SetPosition(FVector2D::ZeroVector);
	PanelSlot->SetSize(FVector2D(380.f, 420.f));

	UVerticalBox* MainBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LobbyMainBox"));
	Panel->AddChild(MainBox);

	auto AddRow = [&](UWidget* Widget, float BottomPadding)
	{
		if (UVerticalBoxSlot* Row = MainBox->AddChildToVerticalBox(Widget))
		{
			Row->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			Row->SetPadding(FMargin(0.f, 0.f, 0.f, BottomPadding));
		}
	};

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	AddRow(TitleText, 6.f);

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.85f, 1.f)));
	AddRow(StatusText, 12.f);

	MemberListBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MemberListBox"));
	AddRow(MemberListBox, 12.f);

	auto MakeButton = [&](const TCHAR* Name, UTextBlock** OutLabel) -> UButton*
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *(FString(Name) + TEXT("Label")));
		Button->AddChild(Label);
		AddRow(Button, 8.f);

		if (OutLabel != nullptr)
		{
			*OutLabel = Label;
		}
		return Button;
	};

	// 라벨 글자는 여기서 정하지 않는다. 상태에 따라 달라지므로 RefreshUI() 가 채운다.
	UTextBlock* Label1 = nullptr;
	UTextBlock* Label2 = nullptr;
	UTextBlock* Label3 = nullptr;
	PrimaryButton   = MakeButton(TEXT("PrimaryButton"),   &Label1);
	SecondaryButton = MakeButton(TEXT("SecondaryButton"), &Label2);
	TertiaryButton  = MakeButton(TEXT("TertiaryButton"),  &Label3);
	PrimaryButtonLabel   = Label1;
	SecondaryButtonLabel = Label2;
	TertiaryButtonLabel  = Label3;

	MessageText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MessageText"));
	MessageText->SetAutoWrapText(true);
	MessageText->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)));
	AddRow(MessageText, 0.f);
}

// ---------------------------------------------------------------------------
// 화면 갱신 — 상태를 화면으로 옮기는 유일한 함수
// ---------------------------------------------------------------------------

UChatSubsystem* ULobbyWidgetBase::GetChatSubsystem() const
{
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		return GameInstance->GetSubsystem<UChatSubsystem>();
	}
	return nullptr;
}

void ULobbyWidgetBase::RefreshUI()
{
	UChatSubsystem* Chat = GetChatSubsystem();

	const bool bLoggedIn = (Chat != nullptr) && (Chat->GetConnectionState() == EChatConnectionState::LoggedIn);
	const bool bInRoom   = (UIState == EMOULobbyUIState::WaitingRoom);
	const bool bIsHost   = bInRoom && Chat != nullptr && Chat->IsRoomHost();

	auto SetLabel = [](UTextBlock* Label, const FString& Text)
	{
		if (Label != nullptr)
		{
			Label->SetText(FText::FromString(Text));
		}
	};
	auto SetEnabled = [](UButton* Button, bool bEnabled)
	{
		if (Button != nullptr)
		{
			Button->SetIsEnabled(bEnabled);
		}
	};

	if (TitleText != nullptr)
	{
		TitleText->SetText(FText::FromString(bInRoom ? TEXT("대기실") : TEXT("MOU 로비")));
	}

	if (!bInRoom)
	{
		// --- 메인메뉴 ---
		SetLabel(PrimaryButtonLabel,   TEXT("방 만들기"));
		SetLabel(SecondaryButtonLabel, TEXT("참여하기"));
		SetLabel(TertiaryButtonLabel,  TEXT("게임 종료"));

		// 로그인 전에는 서버가 거부하므로 눌러봐야 소용없다. 아예 잠근다.
		SetEnabled(PrimaryButton,   bLoggedIn);
		SetEnabled(SecondaryButton, bLoggedIn);
		SetEnabled(TertiaryButton,  true);
	}
	else if (bIsHost)
	{
		// --- 대기실 (방장) ---
		// 게임 시작은 참여자가 전원 준비했을 때만 켜진다. 판정은 서버가 내려준 값이다.
		const bool bAllReady = (Chat != nullptr) && Chat->AreAllMembersReady();

		SetLabel(PrimaryButtonLabel,   bAllReady ? TEXT("게임 시작") : TEXT("게임 시작 (준비 대기 중)"));
		SetLabel(SecondaryButtonLabel, TEXT("커스터마이징"));
		SetLabel(TertiaryButtonLabel,  TEXT("나가기"));

		SetEnabled(PrimaryButton,   bAllReady);
		SetEnabled(SecondaryButton, true);
		SetEnabled(TertiaryButton,  true);
	}
	else
	{
		// --- 대기실 (참여자) ---
		const bool bReady = (Chat != nullptr) && Chat->IsSelfReady();

		SetLabel(PrimaryButtonLabel,   bReady ? TEXT("준비 해제") : TEXT("준비하기"));
		SetLabel(SecondaryButtonLabel, TEXT("커스터마이징"));
		SetLabel(TertiaryButtonLabel,  TEXT("나가기"));

		SetEnabled(PrimaryButton,   true);
		SetEnabled(SecondaryButton, true);
		SetEnabled(TertiaryButton,  true);
	}

	// 상태줄
	if (StatusText != nullptr)
	{
		FString Status;
		if (Chat == nullptr)
		{
			Status = TEXT("채팅 시스템을 찾을 수 없습니다.");
		}
		else if (bInRoom)
		{
			Status = FString::Printf(TEXT("방 #%d — %s"),
				Chat->GetCurrentRoomId(), bIsHost ? TEXT("방장") : TEXT("참여자"));
		}
		else
		{
			switch (Chat->GetConnectionState())
			{
			case EChatConnectionState::LoggedIn:
				Status = FString::Printf(TEXT("%s 님으로 접속 중"), *Chat->GetLoginResult().Name);
				break;
			case EChatConnectionState::Connected:
				Status = TEXT("서버에 연결됨. 로그인 대기 중...");
				break;
			case EChatConnectionState::Connecting:
				Status = TEXT("서버에 연결하는 중...");
				break;
			default:
				Status = TEXT("서버에 연결되어 있지 않습니다.");
				break;
			}
		}
		StatusText->SetText(FText::FromString(Status));
	}

	RebuildMemberList();
}

void ULobbyWidgetBase::RebuildMemberList()
{
	if (MemberListBox == nullptr)
	{
		return;
	}

	// 메인메뉴에서는 자리까지 접는다. Hidden 은 빈 공간을 남긴다.
	if (UIState != EMOULobbyUIState::WaitingRoom)
	{
		MemberListBox->ClearChildren();
		MemberListBox->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	MemberListBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// 명단은 최대 4명(kMaxPlayersInRoom)이라 매번 다시 만들어도 부담이 없다.
	// 재사용 로직을 두면 나간 사람의 줄이 남는 종류의 버그가 생긴다.
	MemberListBox->ClearChildren();

	const UChatSubsystem* Chat = GetChatSubsystem();
	if (Chat == nullptr)
	{
		return;
	}

	const TArray<FMOURoomMember> Members = Chat->GetRoomMembers();
	for (const FMOURoomMember& Member : Members)
	{
		UTextBlock* Line = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());

		// 방장은 준비 여부를 묻지 않으므로 체크 대신 역할을 보여준다.
		const FString Mark = Member.bIsHost ? TEXT("★") : (Member.bReady ? TEXT("●") : TEXT("○"));
		const FString Role = Member.bIsHost ? TEXT(" (방장)") : (Member.bReady ? TEXT(" — 준비완료") : TEXT(" — 대기중"));
		Line->SetText(FText::FromString(FString::Printf(TEXT("%s %s%s"), *Mark, *Member.Name, *Role)));

		Line->SetColorAndOpacity(FSlateColor(Member.bIsHost
			? FLinearColor(1.f, 0.85f, 0.4f)
			: (Member.bReady ? FLinearColor(0.5f, 1.f, 0.5f) : FLinearColor(0.7f, 0.7f, 0.7f))));

		if (UVerticalBoxSlot* Row = MemberListBox->AddChildToVerticalBox(Line))
		{
			Row->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			Row->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
		}
	}
}

void ULobbyWidgetBase::SetMessage(const FString& Text, bool bIsError)
{
	if (MessageText == nullptr)
	{
		return;
	}
	MessageText->SetText(FText::FromString(Text));
	MessageText->SetColorAndOpacity(FSlateColor(bIsError
		? FLinearColor(1.f, 0.45f, 0.45f)
		: FLinearColor(0.75f, 0.75f, 0.75f)));
}

// ---------------------------------------------------------------------------
// 버튼 — 상태에 따라 다른 일을 한다
// ---------------------------------------------------------------------------

void ULobbyWidgetBase::HandlePrimaryClicked()
{
	if (UIState == EMOULobbyUIState::MainMenu)
	{
		OpenRoomCreate();
		return;
	}

	const UChatSubsystem* Chat = GetChatSubsystem();
	if (Chat != nullptr && Chat->IsRoomHost())
	{
		RequestStartGame();
	}
	else
	{
		ToggleReady();
	}
}

void ULobbyWidgetBase::HandleSecondaryClicked()
{
	if (UIState == EMOULobbyUIState::MainMenu)
	{
		OpenRoomList();
	}
	else
	{
		OpenCustomize();
	}
}

void ULobbyWidgetBase::HandleTertiaryClicked()
{
	if (UIState == EMOULobbyUIState::MainMenu)
	{
		QuitGame();
	}
	else
	{
		LeaveRoom();
	}
}

// ---------------------------------------------------------------------------
// 대기실 동작
// ---------------------------------------------------------------------------

void ULobbyWidgetBase::ToggleReady()
{
	UChatSubsystem* Chat = GetChatSubsystem();
	if (Chat == nullptr || Chat->GetCurrentRoomId() == 0)
	{
		return;
	}

	// 낙관적으로 화면을 먼저 바꾸지 않는다. 서버가 갱신된 명단을 돌려주면
	// OnRoomMembersChanged 에서 RefreshUI 가 돌면서 버튼 글자가 바뀐다.
	// 이렇게 해야 화면과 서버가 어긋나지 않는다.
	Chat->SetReady(!Chat->IsSelfReady());
}

void ULobbyWidgetBase::RequestStartGame()
{
	UChatSubsystem* Chat = GetChatSubsystem();
	if (Chat == nullptr || !Chat->IsRoomHost())
	{
		return;
	}

	if (!Chat->AreAllMembersReady())
	{
		SetMessage(UChatSubsystem::GetRoomResultText(EMOURoomResultBP::NotAllReady), true);
		return;
	}

	SetMessage(TEXT("게임을 시작합니다..."), false);
	Chat->StartGame();
}

void ULobbyWidgetBase::LeaveRoom()
{
	if (UChatSubsystem* Chat = GetChatSubsystem())
	{
		Chat->LeaveRoom();
	}
	ReturnToMainMenu(/*bRoomClosed=*/false);
	SetMessage(TEXT("방에서 나왔습니다."), false);
}

void ULobbyWidgetBase::OpenCustomize()
{
	// 화면이 아직 없다. 훅만 부르고 사용자에게는 솔직하게 알린다.
	OnCustomizeRequested();
	SetMessage(TEXT("커스터마이징은 아직 준비 중입니다."), false);
}

void ULobbyWidgetBase::QuitGame()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, /*bIgnorePlatformRestrictions=*/false);
}

// ---------------------------------------------------------------------------
// 상태 전환
// ---------------------------------------------------------------------------

void ULobbyWidgetBase::EnterWaitingRoom(int32 RoomId, bool bIsHost)
{
	UIState = EMOULobbyUIState::WaitingRoom;
	SetPanelVisible(true);
	RefreshUI();

	OnEnteredWaitingRoom(RoomId, bIsHost);
}

void ULobbyWidgetBase::ReturnToMainMenu(bool bRoomClosed)
{
	UIState = EMOULobbyUIState::MainMenu;
	MyRoomPassword.Empty();
	JoinedRoomPassword.Empty();

	// v5 까지는 여기서 예약해둔 여행 타이머를 취소했다. 이제 예약이 없다 —
	// 참여자는 방장의 리슨서버가 실제로 뜬 뒤에 오는 신호를 받고서야 떠나고,
	// 방을 나가면 서버가 그 신호를 보내지 않기 때문이다.

	SetPanelVisible(true);
	RefreshUI();

	OnLeftWaitingRoom(bRoomClosed);
}

void ULobbyWidgetBase::HandleChatStateChanged(EChatConnectionState NewState, const FString& /*Detail*/)
{
	// 연결이 끊기면 서브시스템이 방 상태를 비운다. 화면도 메인메뉴로 되돌린다.
	if (NewState == EChatConnectionState::Disconnected && UIState == EMOULobbyUIState::WaitingRoom)
	{
		ReturnToMainMenu(/*bRoomClosed=*/true);
		SetMessage(TEXT("서버와의 연결이 끊겨 방에서 나왔습니다."), true);
		return;
	}
	RefreshUI();
}

void ULobbyWidgetBase::HandleLoginCompleted(const FChatLoginResult& /*Result*/)
{
	RefreshUI();
}

void ULobbyWidgetBase::HandleRoomMembersChanged(int32 /*RoomId*/, const TArray<FMOURoomMember>& /*Members*/, bool /*bAllReady*/)
{
	// 명단 자체는 서브시스템이 들고 있다. 여기서는 다시 그리기만 한다.
	RefreshUI();
}

void ULobbyWidgetBase::HandleRoomClosed(int32 RoomId, EMOURoomCloseReasonBP /*Reason*/)
{
	if (UIState != EMOULobbyUIState::WaitingRoom)
	{
		return;
	}

	ReturnToMainMenu(/*bRoomClosed=*/true);
	SetMessage(FString::Printf(
		TEXT("방장이 나가서 방 #%d 이(가) 사라졌습니다."), RoomId), true);
}

void ULobbyWidgetBase::HandleGameStarted(const FMOURoomJoinResult& Host, bool bIsHost)
{
	const FString RoomPassword = bIsHost ? MyRoomPassword : JoinedRoomPassword;

	// 참여자에게는 "이동합니다" 가 아니라 "기다립니다" 라고 말해야 한다.
	// 실제로 떠나는 것은 HandleHostReady 이고, 그 사이에 몇 초가 흐를 수 있다.
	// 화면이 멈춘 것처럼 보이지 않게 지금 무엇을 기다리는지 적어준다.
	SetMessage(bIsHost
		? TEXT("게임을 시작합니다. 리슨서버를 엽니다...")
		: TEXT("게임이 시작됐습니다. 방장이 서버를 여는 중입니다..."), false);

	OnGameStarted(Host, bIsHost, RoomPassword);

	if (bIsHost)
	{
		// 참여자에게 갈 출발 신호는 이 위젯이 보내지 않는다.
		// OpenLevel 이 시작되면 이 위젯은 곧 파괴되기 때문이다.
		// 리슨서버가 실제로 뜬 것을 확인하고 알리는 일은 UChatSubsystem 이 맡는다.
		TravelAsHost();
	}
	// 참여자는 여기서 아무것도 하지 않는다. HandleHostReady 를 기다린다.
}

void ULobbyWidgetBase::HandleHostReady(const FMOURoomJoinResult& Host)
{
	// 방장에게는 이 신호가 오지 않는다. 와도 이미 자기 맵에 있으므로 할 일이 없다.
	if (UIState != EMOULobbyUIState::WaitingRoom)
	{
		return;
	}

	SetMessage(FString::Printf(TEXT("호스트 %s:%d 로 이동합니다..."),
		*Host.HostAddress, Host.HostPort), false);

	OnHostReady(Host, JoinedRoomPassword);

	if (bAutoTravelOnGameStart)
	{
		TravelAsClient(Host, JoinedRoomPassword);
	}
}

// ---------------------------------------------------------------------------
// 자식 창 열고 닫기
// ---------------------------------------------------------------------------

void ULobbyWidgetBase::SetPanelVisible(bool bVisible)
{
	if (!bHideWhileChildOpen)
	{
		return;
	}
	// Collapsed 로 접으면 클릭도 가지 않는다. 자식 창 뒤의 버튼이 눌리는 것을 막는다.
	SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void ULobbyWidgetBase::OpenRoomCreate()
{
	if (UIState != EMOULobbyUIState::MainMenu || RoomCreateWidget != nullptr)
	{
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (PC == nullptr)
	{
		return;
	}

	UClass* WidgetClass = RoomCreateWidgetClass ? RoomCreateWidgetClass.Get() : URoomCreateWidgetBase::StaticClass();
	RoomCreateWidget = CreateWidget<URoomCreateWidgetBase>(PC, WidgetClass);
	if (RoomCreateWidget == nullptr)
	{
		SetMessage(TEXT("방 생성 창을 만들지 못했습니다."), true);
		return;
	}

	// 커서는 로비가 이미 관리하고 있다. 자식이 또 만지면 닫힐 때 커서가 사라진다.
	RoomCreateWidget->bManageMouseCursor = false;
	// 성공 통지를 받으면 로비는 참조를 버린다. 자식이 스스로 닫지 않으면
	// 아무도 소유하지 않는 창이 화면에 남는다.
	RoomCreateWidget->bRemoveOnSuccess = true;
	RoomCreateWidget->HostPort = HostPort;
	RoomCreateWidget->OnRoomCreateFinished.BindUObject(this, &ULobbyWidgetBase::HandleRoomCreateFinished);
	RoomCreateWidget->OnRoomCreateCancelled.BindUObject(this, &ULobbyWidgetBase::HandleRoomCreateCancelled);

	RoomCreateWidget->AddToViewport();
	SetPanelVisible(false);
}

void ULobbyWidgetBase::OpenRoomList()
{
	if (UIState != EMOULobbyUIState::MainMenu || RoomListWidget != nullptr)
	{
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (PC == nullptr)
	{
		return;
	}

	UClass* WidgetClass = RoomListWidgetClass ? RoomListWidgetClass.Get() : URoomListWidgetBase::StaticClass();
	RoomListWidget = CreateWidget<URoomListWidgetBase>(PC, WidgetClass);
	if (RoomListWidget == nullptr)
	{
		SetMessage(TEXT("방 목록 창을 만들지 못했습니다."), true);
		return;
	}

	RoomListWidget->bManageMouseCursor = false;
	RoomListWidget->bRemoveOnSuccess   = true;   // 위와 같은 이유
	RoomListWidget->OnRoomJoinApprovedNative.BindUObject(this, &ULobbyWidgetBase::HandleRoomJoinApproved);
	RoomListWidget->OnRoomListClosed.BindUObject(this, &ULobbyWidgetBase::HandleRoomListClosed);

	RoomListWidget->AddToViewport();
	SetPanelVisible(false);
}

void ULobbyWidgetBase::CloseChildWidgets()
{
	if (RoomCreateWidget != nullptr)
	{
		// 델리게이트를 먼저 끊는다. RemoveFromParent 가 취소 통지를 유발하면
		// 여기서 다시 CloseChildWidgets 로 들어올 수 있다.
		RoomCreateWidget->OnRoomCreateFinished.Unbind();
		RoomCreateWidget->OnRoomCreateCancelled.Unbind();
		RoomCreateWidget->RemoveFromParent();
		RoomCreateWidget = nullptr;
	}
	if (RoomListWidget != nullptr)
	{
		RoomListWidget->OnRoomJoinApprovedNative.Unbind();
		RoomListWidget->OnRoomListClosed.Unbind();
		RoomListWidget->RemoveFromParent();
		RoomListWidget = nullptr;
	}
}

// ---------------------------------------------------------------------------
// 자식 창에서 올라오는 결과
// ---------------------------------------------------------------------------

void ULobbyWidgetBase::HandleRoomCreateFinished(int32 RoomId, const FString& RoomPassword)
{
	// 자식은 스스로 뷰포트에서 빠졌다(bRemoveOnSuccess). 참조만 정리한다.
	RoomCreateWidget = nullptr;
	MyRoomPassword   = RoomPassword;

	EnterWaitingRoom(RoomId, /*bIsHost=*/true);
	SetMessage(TEXT("방을 열었습니다. 참여자가 모두 준비하면 시작할 수 있습니다."), false);
}

void ULobbyWidgetBase::HandleRoomCreateCancelled()
{
	RoomCreateWidget = nullptr;
	SetPanelVisible(true);
}

void ULobbyWidgetBase::HandleRoomJoinApproved(const FMOURoomJoinResult& Result, const FString& RoomPassword)
{
	RoomListWidget     = nullptr;
	JoinedRoomPassword = RoomPassword;

	EnterWaitingRoom(Result.RoomId, /*bIsHost=*/false);
	SetMessage(TEXT("방에 들어왔습니다. 준비를 누르면 방장이 시작할 수 있습니다."), false);
}

void ULobbyWidgetBase::HandleRoomListClosed()
{
	RoomListWidget = nullptr;
	SetPanelVisible(true);
}

// ---------------------------------------------------------------------------
// 여행
//
// 여기서부터가 로비 서버의 관할 밖이다. 방 목록은 "주소록" 일 뿐이고,
// 실제 게임 트래픽은 참여자가 호스트의 리슨서버에 직접 붙어서 오간다.
// ---------------------------------------------------------------------------

void ULobbyWidgetBase::TravelAsHost()
{
	if (HostMapName.IsEmpty())
	{
		// 맵을 정하지 않았다면 여행은 게임 쪽(블루프린트/게임모드)의 몫이다.
		SetMessage(TEXT("게임 시작됨. (HostMapName 이 비어 있어 레벨은 열지 않았습니다)"), false);
		return;
	}

	// listen 옵션이 있어야 이 클라이언트가 리슨서버가 된다.
	// RoomPassword 를 URL 에 같이 실어야 새 레벨의 GameMode 가 InitGame 에서
	// 그 값을 읽어 보관하고, 나중에 PreLogin 에서 참여자를 검사할 수 있다.
	FString Options = TEXT("listen");
	if (!MyRoomPassword.IsEmpty())
	{
		Options += FString::Printf(TEXT("?RoomPassword=%s"), *MyRoomPassword);
	}

	UGameplayStatics::OpenLevel(this, FName(*HostMapName), /*bAbsolute=*/true, Options);
}

void ULobbyWidgetBase::TravelAsClient(const FMOURoomJoinResult& Host, const FString& RoomPassword)
{
	APlayerController* PC = GetOwningPlayer();
	if (PC == nullptr)
	{
		return;
	}

	// MakeTravelURL 이 "IP:포트?RoomPassword=1234" 를 만들어준다.
	PC->ClientTravel(Host.MakeTravelURL(RoomPassword), ETravelType::TRAVEL_Absolute);
}

// ---------------------------------------------------------------------------
// 콘솔 명령 - 게임 플로우에 로비를 붙이기 전에 UI 를 검증하기 위한 것.
//
//   MOU.Lobby.Show   로비를 띄운다
//   MOU.Lobby.Hide   로비를 화면에서 제거한다
//
// 게임에서 정식으로 쓸 때는 로그인 성공 뒤에 자동으로 뜬다
// (ULoginWidgetBase 의 bShowLobbyWidgetOnSuccess).
// ---------------------------------------------------------------------------

#if !UE_BUILD_SHIPPING
namespace
{
	/**
	 * 콘솔로 띄운 로비를 월드별로 기억한다. ChatWidgetBase 의 GDebugChatWidgets 와 같은 이유다 —
	 * PIE 창을 여러 개 띄우면 창마다 월드가 따로 생기므로 전역 하나로는 두 번째 창을 못 띄운다.
	 */
	TMap<TWeakObjectPtr<UWorld>, TWeakObjectPtr<ULobbyWidgetBase>> GDebugLobbyWidgets;

	void PruneDebugLobbyWidgets()
	{
		for (auto It = GDebugLobbyWidgets.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid() || !It.Value().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GLobbyShowCommand(
		TEXT("MOU.Lobby.Show"),
		TEXT("로비를 띄운다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				PruneDebugLobbyWidgets();
				if (GDebugLobbyWidgets.Contains(World))
				{
					return;   // 이 창에는 이미 떠 있다
				}

				APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
				if (PC == nullptr)
				{
					return;
				}

				ULobbyWidgetBase* Widget = CreateWidget<ULobbyWidgetBase>(PC, ULobbyWidgetBase::StaticClass());
				if (Widget != nullptr)
				{
					Widget->AddToViewport();
					GDebugLobbyWidgets.Add(World, Widget);
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GLobbyHideCommand(
		TEXT("MOU.Lobby.Hide"),
		TEXT("로비를 화면에서 제거한다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				PruneDebugLobbyWidgets();
				if (const TWeakObjectPtr<ULobbyWidgetBase>* Found = GDebugLobbyWidgets.Find(World))
				{
					if (ULobbyWidgetBase* Widget = Found->Get())
					{
						Widget->RemoveFromParent();
					}
				}
				GDebugLobbyWidgets.Remove(World);
			}));
}
#endif
