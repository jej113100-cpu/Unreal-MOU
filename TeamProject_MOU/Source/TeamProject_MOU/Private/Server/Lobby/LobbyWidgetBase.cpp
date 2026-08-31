// MOU 로비 - 메인메뉴 + 대기실 UI 구현.
//
// 이 파일은 소켓/패킷을 전혀 모른다.
//   상태 조회: UServerSubsystem (연결 상태 / 내 신원 / 방 번호 / 대기실 명단)
//   서버 왕복: 방에 들어가기 전에는 자식 창이, 들어간 뒤에는 서브시스템 API 가 한다.
//
// [화면을 바꾸는 곳은 RefreshUI() 하나뿐이다]
//   버튼 라벨, 활성화 여부, 명단 표시를 전부 거기서 결정한다.
//   여기저기서 SetText 를 부르기 시작하면 "준비하기" 라고 적힌 버튼이
//   방을 만드는 식의 어긋남이 반드시 생긴다.

#include "Server/Lobby/LobbyWidgetBase.h"

#include "Server/ServerSubsystem.h"
#include "Server/Lobby/RoomCreateWidgetBase.h"
#include "Server/Lobby/RoomListWidgetBase.h"
#include "Server/Net/NatPortMappingSubsystem.h"

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
		if (UServerSubsystem* Chat = GetServerSubsystem())
		{
			Chat->OnChatStateChanged.AddDynamic(this, &ULobbyWidgetBase::HandleChatStateChanged);
			Chat->OnChatLoginCompleted.AddDynamic(this, &ULobbyWidgetBase::HandleLoginCompleted);
			Chat->OnRoomMembersChanged.AddDynamic(this, &ULobbyWidgetBase::HandleRoomMembersChanged);
			Chat->OnRoomClosed.AddDynamic(this, &ULobbyWidgetBase::HandleRoomClosed);
			Chat->OnRoomGameStarted.AddDynamic(this, &ULobbyWidgetBase::HandleGameStarted);
			Chat->OnRoomHostReady.AddDynamic(this, &ULobbyWidgetBase::HandleHostReady);

			// 접속 실패를 화면에 그대로 띄운다. UFUNCTION 이 아니라 순수 델리게이트라
			// AddDynamic 이 아니라 AddUObject 를 쓴다.
			TravelFailedHandle = Chat->OnTravelFailed.AddUObject(this, &ULobbyWidgetBase::HandleTravelFailed);
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

	// ★ 여행 설정을 서브시스템에 넘긴다. (2026-08-29)
	//   값의 주인은 여전히 이 WBP 다(디자이너가 여기서 고친다). 다만 실제로 떠나는
	//   일은 서브시스템이 한다 — 이 위젯은 게임이 시작되면 닫힐 수 있고, 그러면
	//   출발 신호를 받을 사람이 사라지기 때문이다.
	//   여기서 넘겨두면 위젯이 죽어도 설정은 남는다.
	if (UServerSubsystem* Chat = GetServerSubsystem())
	{
		Chat->ConfigureTravel(HostMapName, bAutoTravelOnGameStart, bPreloadMapWhileWaiting);
	}

	// 서브시스템은 레벨을 넘어가도 살아있으므로, 이 위젯이 새로 만들어졌을 때
	// 이미 방 안일 수 있다. 지금 상태를 물어보고 거기에 맞춰 연다.
	if (const UServerSubsystem* Chat = GetServerSubsystem())
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
		if (UServerSubsystem* Chat = GetServerSubsystem())
		{
			Chat->OnChatStateChanged.RemoveDynamic(this, &ULobbyWidgetBase::HandleChatStateChanged);
			Chat->OnChatLoginCompleted.RemoveDynamic(this, &ULobbyWidgetBase::HandleLoginCompleted);
			Chat->OnRoomMembersChanged.RemoveDynamic(this, &ULobbyWidgetBase::HandleRoomMembersChanged);
			Chat->OnRoomClosed.RemoveDynamic(this, &ULobbyWidgetBase::HandleRoomClosed);
			Chat->OnRoomGameStarted.RemoveDynamic(this, &ULobbyWidgetBase::HandleGameStarted);
			Chat->OnRoomHostReady.RemoveDynamic(this, &ULobbyWidgetBase::HandleHostReady);

			if (TravelFailedHandle.IsValid())
			{
				Chat->OnTravelFailed.Remove(TravelFailedHandle);
				TravelFailedHandle.Reset();
			}
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

UServerSubsystem* ULobbyWidgetBase::GetServerSubsystem() const
{
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		return GameInstance->GetSubsystem<UServerSubsystem>();
	}
	return nullptr;
}

void ULobbyWidgetBase::RefreshUI()
{
	UServerSubsystem* Chat = GetServerSubsystem();

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

	const UServerSubsystem* Chat = GetServerSubsystem();
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

	const UServerSubsystem* Chat = GetServerSubsystem();
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
	UServerSubsystem* Chat = GetServerSubsystem();
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
	UServerSubsystem* Chat = GetServerSubsystem();
	if (Chat == nullptr || !Chat->IsRoomHost())
	{
		return;
	}

	if (!Chat->AreAllMembersReady())
	{
		SetMessage(UServerSubsystem::GetRoomResultText(EMOURoomResultBP::NotAllReady), true);
		return;
	}

	SetMessage(TEXT("게임을 시작합니다..."), false);
	Chat->StartGame();
}

void ULobbyWidgetBase::LeaveRoom()
{
	if (UServerSubsystem* Chat = GetServerSubsystem())
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

	// ★ 게임 포트를 지금 확보하고 서버에 등록한다. (v10)
	//
	//   [왜 여기인가]
	//     대기실에 앉아 있는 동안이 유일하게 여유 있는 시점이다. 게임 시작 뒤에는
	//     방장이 곧 OpenLevel 을 하고, 그 전에 punch 대상이 이미 정해져 있어야 한다.
	//
	//   [왜 참여자도 하는가]
	//     punch 를 받는 쪽이 참여자다. 서버가 참여자의 공인 엔드포인트를 관측해
	//     두지 않으면 방장은 어디로 쏠지 모른다.
	//     방장도 등록해 둔다 — 다음 판에 참여자가 될 수 있고, 그때 다시 하려면
	//     또 대기실을 거쳐야 하기 때문이다.
	if (UServerSubsystem* Chat = GetServerSubsystem())
	{
		Chat->RegisterGameEndpoint(HostPort);
	}

	SetPanelVisible(true);
	RefreshUI();

	OnEnteredWaitingRoom(RoomId, bIsHost);
}

void ULobbyWidgetBase::ReturnToMainMenu(bool bRoomClosed)
{
	UIState = EMOULobbyUIState::MainMenu;
	MyRoomPassword.Empty();
	JoinedRoomPassword.Empty();

	// ★ 방을 떠났으니 공유기에 열어둔 포트를 닫는다.
	//   방을 나가는 모든 경로(직접 나가기 / 방장 이탈로 쫓겨남)가 이 함수를 지나므로
	//   여기 한 곳에만 두면 빠뜨릴 일이 없다.
	//   참여자였다면 애초에 연 포트가 없어서 아무 일도 일어나지 않는다.
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (UNatPortMappingSubsystem* Nat = GameInstance->GetSubsystem<UNatPortMappingSubsystem>())
		{
			Nat->ReleasePortMapping();
		}
	}

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

	// ★ 여기서 더 이상 여행하지 않는다. (2026-08-29)
	//   방장의 OpenLevel 도, 참여자의 맵 미리 올리기도 전부 UServerSubsystem 이
	//   이 델리게이트를 쏜 직후에 이어서 한다. 이 위젯은 안내만 한다.
	//   자세한 이유는 UServerSubsystem::ConfigureTravel 위 주석 참고.
}

void ULobbyWidgetBase::HandleHostReady(const FMOURoomJoinResult& Host)
{
	// ★ 예전에는 여기에 "대기실이 아니면 return" 이 있었다. 지웠다. (2026-08-29)
	//   그 조건 때문에, 게임 시작과 함께 화면이 바뀌어 UIState 가 달라진 참여자는
	//   출발 신호를 받고도 조용히 무시했다. 이제 떠나는 일은 서브시스템이 하므로
	//   이 함수가 무엇을 하든 이동은 보장된다. 여기서는 안내만 한다.
	SetMessage(FString::Printf(TEXT("호스트 %s 로 이동합니다..."),
		*Host.ToDisplayString()), false);

	OnHostReady(Host, JoinedRoomPassword);

	// ClientTravel 은 UServerSubsystem::TravelToHost 가 한다.
	// bAutoTravelOnGameStart 를 껐다면 BP 가 원하는 시점에 그 함수를 부르면 된다.
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

	// ★ 서브시스템에도 넘긴다. 리슨서버 URL 에 실릴 값이고, 그 일을 하는 시점에는
	//   이 위젯이 이미 사라졌을 수 있다.
	if (UServerSubsystem* Chat = GetServerSubsystem())
	{
		Chat->SetRoomPassword(RoomPassword);
	}

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

	// ★ 방장 쪽과 같은 이유. ClientTravel URL 에 실릴 값인데, 그때 이 위젯이
	//   남아 있으리라는 보장이 없다.
	if (UServerSubsystem* Chat = GetServerSubsystem())
	{
		Chat->SetRoomPassword(RoomPassword);
	}

	EnterWaitingRoom(Result.RoomId, /*bIsHost=*/false);
	SetMessage(TEXT("방에 들어왔습니다. 준비를 누르면 방장이 시작할 수 있습니다."), false);
}

void ULobbyWidgetBase::HandleRoomListClosed()
{
	RoomListWidget = nullptr;
	SetPanelVisible(true);
}

// ---------------------------------------------------------------------------
// 여행 실패 안내
//
// 여기서부터가 로비 서버의 관할 밖이다. 방 목록은 "주소록" 일 뿐이고,
// 실제 게임 트래픽은 참여자가 호스트의 리슨서버에 직접 붙어서 오간다.
//
// ★ 실제로 떠나는 코드는 2026-08-29 에 UServerSubsystem 으로 옮겼다.
//   위젯은 레벨 이동과 함께 파괴되므로, 여기서 출발 신호를 기다리면
//   게임 시작 시 위젯을 닫는 순간 아무도 안 떠난다.
//   이 파일에는 "사용자에게 무엇을 보여줄 것인가" 만 남는다.
// ---------------------------------------------------------------------------

void ULobbyWidgetBase::HandleTravelFailed(const FString& Reason)
{
	// 대기실에 있든 이미 메인메뉴로 돌아왔든 알려준다. 접속 실패는 어느
	// 화면에서 받든 사용자가 알아야 하는 정보다.
	SetMessage(Reason, /*bIsError=*/true);

	// 대기실에 남아 있으면 "이동합니다..." 상태로 굳어 있으므로 풀어준다.
	// 방 자체는 서버에 살아 있으니 메인메뉴로 쫓아내지는 않는다 —
	// 방장이 포워딩을 고치고 다시 시작하면 그대로 다시 붙을 수 있다.
	RefreshUI();
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
