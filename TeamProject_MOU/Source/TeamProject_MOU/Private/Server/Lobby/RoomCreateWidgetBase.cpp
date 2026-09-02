// MOU 로비 - 방 생성 UI 구현.
//
// 이 파일은 소켓/패킷을 전혀 모른다. UServerSubsystem 하고만 대화한다.
//   보낼 때: UServerSubsystem::CreateRoom()
//   받을 때: UServerSubsystem::OnRoomCreated

#include "Server/Lobby/RoomCreateWidgetBase.h"

// MOU::kMaxRoomTitleLen 과 MOUChat::GetUtf8Length 를 쓰기 위해 포함한다.
// ChatProtocol.h 를 직접 넣지 않고 ChatFraming.h 를 거치는 이유는
// 그쪽이 THIRD_PARTY_INCLUDES_START 로 감싸주기 때문이다.
#include "Server/Net/ChatFraming.h"
#include "Server/ServerSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

URoomCreateWidgetBase::URoomCreateWidgetBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

// ---------------------------------------------------------------------------
// 수명 주기
// ---------------------------------------------------------------------------

void URoomCreateWidgetBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// WidgetTree->RootWidget 이 이미 있으면 WBP 가 만든 레이아웃이다. 손대지 않는다.
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
	}
}

void URoomCreateWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	if (CreateButton != nullptr)
	{
		CreateButton->OnClicked.AddUniqueDynamic(this, &URoomCreateWidgetBase::HandleCreateClicked);
	}
	if (CancelButton != nullptr)
	{
		CancelButton->OnClicked.AddUniqueDynamic(this, &URoomCreateWidgetBase::HandleCancelClicked);
	}

	// NativeConstruct 는 뷰포트에 다시 붙을 때마다 불릴 수 있어 중복 구독을 막는다.
	if (!bSubscribed)
	{
		if (UServerSubsystem* Chat = GetServerSubsystem())
		{
			Chat->OnRoomCreated.AddDynamic(this, &URoomCreateWidgetBase::HandleRoomCreated);
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

	SetMessage(TEXT("방 제목을 입력하세요. 비밀번호는 비워두면 공개방이 됩니다."), false);

	if (RoomTitleBox != nullptr)
	{
		RoomTitleBox->SetKeyboardFocus();
	}

	// ★ 창이 열리는 지금 포트 열기를 시작한다. 사용자가 제목을 입력하는 몇 초가
	//   SSDP 탐색 + SOAP 왕복 시간과 겹쳐서, "방 만들기" 를 누를 때쯤이면 대개 끝나 있다.
	//   실패해도 방 만들기는 그대로 진행된다 (헤더의 bOpenPortOnShow 주석 참고).
	if (bOpenPortOnShow)
	{
		if (UNatPortMappingSubsystem* Nat = GetNatSubsystem())
		{
			if (!bNatSubscribed)
			{
				Nat->OnNatMappingFinished.AddDynamic(this, &URoomCreateWidgetBase::HandleNatMappingFinished);
				bNatSubscribed = true;
			}

			// 이미 열려 있으면(창을 닫았다 다시 연 경우) 다시 열 필요가 없다.
			if (Nat->GetMappedExternalPort() == 0 && !Nat->IsMappingInProgress())
			{
				Nat->BeginPortMapping(HostPort);
			}
		}
	}
}

void URoomCreateWidgetBase::NativeDestruct()
{
	// 구독 해제를 여기서 반드시 해야 파괴된 위젯으로 델리게이트가 날아오지 않는다.
	if (bSubscribed)
	{
		if (UServerSubsystem* Chat = GetServerSubsystem())
		{
			Chat->OnRoomCreated.RemoveDynamic(this, &URoomCreateWidgetBase::HandleRoomCreated);
		}
		bSubscribed = false;
	}

	if (bNatSubscribed)
	{
		if (UNatPortMappingSubsystem* Nat = GetNatSubsystem())
		{
			Nat->OnNatMappingFinished.RemoveDynamic(this, &URoomCreateWidgetBase::HandleNatMappingFinished);
		}
		bNatSubscribed = false;
	}

	// ★ 여기서 ReleasePortMapping 을 부르면 안 된다.
	//   이 함수는 취소할 때만이 아니라 방 생성에 성공해서 창이 닫힐 때도 불린다.
	//   성공 시 포트를 닫아버리면 정작 참가자가 못 들어온다.
	//   취소 경로의 해제는 CancelCreate 가 담당한다.

	Super::NativeDestruct();
}

// ---------------------------------------------------------------------------
// 기본 레이아웃 조립 (WBP 가 없을 때만)
//
//   CanvasPanel (화면 전체)
//     └ Border (화면 정중앙 420x260, 반투명 검정)
//         └ VerticalBox
//             ├ TitleText          "방 만들기"
//             ├ RoomTitleBox       방 제목
//             ├ RoomPasswordBox    비밀번호 4자리 (선택)
//             ├ HorizontalBox
//             │   ├ CreateButton
//             │   └ CancelButton
//             └ MessageText        안내 / 실패 사유
// ---------------------------------------------------------------------------

void URoomCreateWidgetBase::BuildDefaultLayout()
{
	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RoomCreateRootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RoomCreatePanel"));
	Panel->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.04f, 0.94f));
	Panel->SetPadding(FMargin(20.f));

	UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(Panel);
	// 화면 정중앙에 고정한다. 해상도가 바뀌어도 가운데를 유지한다.
	PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	PanelSlot->SetAutoSize(false);
	PanelSlot->SetPosition(FVector2D::ZeroVector);
	PanelSlot->SetSize(FVector2D(420.f, 260.f));

	UVerticalBox* MainBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoomCreateMainBox"));
	Panel->AddChild(MainBox);

	auto AddRow = [&](UWidget* Widget, float BottomPadding)
	{
		if (UVerticalBoxSlot* Slot = MainBox->AddChildToVerticalBox(Widget))
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			Slot->SetPadding(FMargin(0.f, 0.f, 0.f, BottomPadding));
		}
	};

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	TitleText->SetText(FText::FromString(TEXT("방 만들기")));
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	AddRow(TitleText, 12.f);

	RoomTitleBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("RoomTitleBox"));
	RoomTitleBox->SetHintText(FText::FromString(TEXT("방 제목")));
	AddRow(RoomTitleBox, 6.f);

	RoomPasswordBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("RoomPasswordBox"));
	RoomPasswordBox->SetHintText(FText::FromString(TEXT("비밀번호 숫자 4자리 (비우면 공개방)")));
	// 방 비밀번호는 계정 비밀번호와 성격이 다르다. 같은 방에 들어갈 사람끼리 말로
	// 주고받는 값이라 가리지 않는다. 오타를 눈으로 확인하는 편이 더 이롭다.
	AddRow(RoomPasswordBox, 12.f);

	UHorizontalBox* ButtonRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RoomCreateButtonRow"));
	AddRow(ButtonRow, 10.f);

	auto MakeButton = [&](const TCHAR* Name, const FString& Label) -> UButton*
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* ButtonLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *(FString(Name) + TEXT("Label")));
		ButtonLabel->SetText(FText::FromString(Label));
		Button->AddChild(ButtonLabel);

		if (UHorizontalBoxSlot* Slot = ButtonRow->AddChildToHorizontalBox(Button))
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Slot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		}
		return Button;
	};

	CreateButton = MakeButton(TEXT("CreateButton"), TEXT("방 만들기"));
	CancelButton = MakeButton(TEXT("CancelButton"), TEXT("취소"));

	MessageText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MessageText"));
	MessageText->SetAutoWrapText(true);
	MessageText->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)));
	AddRow(MessageText, 0.f);
}

// ---------------------------------------------------------------------------
// 동작
// ---------------------------------------------------------------------------

UServerSubsystem* URoomCreateWidgetBase::GetServerSubsystem() const
{
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		return GameInstance->GetSubsystem<UServerSubsystem>();
	}
	return nullptr;
}

void URoomCreateWidgetBase::TryCreateRoom()
{
	if (bBusy)
	{
		return;
	}

	UServerSubsystem* Chat = GetServerSubsystem();
	if (Chat == nullptr)
	{
		SetMessage(TEXT("채팅 시스템을 찾을 수 없습니다."), true);
		return;
	}

	// 서버도 같은 규칙을 검사하지만, 여기서 먼저 걸러주면 왕복 없이 즉시 알려줄 수 있다.
	if (Chat->GetConnectionState() != EChatConnectionState::LoggedIn)
	{
		SetMessage(UServerSubsystem::GetRoomResultText(EMOURoomResultBP::NotAuthed), true);
		return;
	}
	if (Chat->GetMyRoomId() != 0)
	{
		SetMessage(UServerSubsystem::GetRoomResultText(EMOURoomResultBP::AlreadyHosting), true);
		return;
	}

	FString Title = RoomTitleBox ? RoomTitleBox->GetText().ToString() : FString();
	Title.TrimStartAndEndInline();
	if (Title.IsEmpty())
	{
		SetMessage(TEXT("방 제목을 입력하세요."), true);
		return;
	}

	// 제목 상한은 글자 수가 아니라 UTF-8 바이트 수다(널 종료 포함 48바이트).
	// CopyFixedString 이 알아서 자르지만, 잘린 이름으로 방이 열리면
	// 사용자는 왜 그런지 모른다. 보내기 전에 알려주고 멈춘다.
	const int32 TitleBytes = MOUChat::GetUtf8Length(Title);
	if (TitleBytes > static_cast<int32>(MOU::kMaxRoomTitleLen) - 1)
	{
		SetMessage(FString::Printf(TEXT("방 제목이 너무 깁니다. (한글 %d자 정도까지)"),
			(static_cast<int32>(MOU::kMaxRoomTitleLen) - 1) / 3), true);
		return;
	}

	FString RoomPassword = RoomPasswordBox ? RoomPasswordBox->GetText().ToString() : FString();
	RoomPassword.TrimStartAndEndInline();
	// 비워두면 공개방. 뭔가 적었다면 반드시 숫자 4자리여야 한다.
	// "1234 를 넣었는데 공개방이 되어 있더라" 같은 사고를 막으려고 조용히 무시하지 않는다.
	if (!RoomPassword.IsEmpty() && !UServerSubsystem::IsValidRoomPassword(RoomPassword))
	{
		SetMessage(TEXT("방 비밀번호는 숫자 4자리여야 합니다. (공개방으로 만들려면 비워두세요)"), true);
		return;
	}

	SubmittedTitle    = Title;
	SubmittedPassword = RoomPassword;

	SetBusy(true);

	// ★ 포트 열기가 아직 진행 중이면 여기서 보내지 않는다.
	//   지금 보내면 HostPort 로 방이 등록되는데, 잠시 뒤 공유기가 다른 외부 포트를
	//   열어주면 방 정보에 이미 틀린 포트가 나가 있게 된다. 매핑이 끝나면
	//   HandleNatMappingFinished 가 이어서 보낸다.
	if (UNatPortMappingSubsystem* Nat = GetNatSubsystem())
	{
		if (Nat->IsMappingInProgress())
		{
			bCreateWaitingForNat = true;
			SetMessage(TEXT("공유기에 포트를 여는 중입니다..."), false);
			return;
		}
	}

	SubmitCreateRoom();
}

void URoomCreateWidgetBase::SubmitCreateRoom()
{
	UServerSubsystem* Server = GetServerSubsystem();
	if (Server == nullptr)
	{
		SetBusy(false);
		SetMessage(TEXT("서버 시스템을 찾을 수 없습니다."), true);
		return;
	}

	SetMessage(TEXT("방을 만드는 중..."), false);
	Server->CreateRoom(SubmittedTitle, SubmittedPassword, ResolveAdvertisedPort());
}

int32 URoomCreateWidgetBase::ResolveAdvertisedPort() const
{
	// 공유기가 열어준 외부 포트가 있으면 그것을 신고한다.
	// 리슨서버는 HostPort 그대로 열린다 — 공유기가 외부 포트를 내부 포트로 넘겨주므로
	// 바뀌는 것은 "밖에서 부를 주소" 뿐이고, 프로토콜도 Server.exe 도 그대로다.
	if (const UNatPortMappingSubsystem* Nat = GetNatSubsystem())
	{
		const int32 External = Nat->GetMappedExternalPort();
		if (External > 0)
		{
			return External;
		}
	}
	return HostPort;
}

UNatPortMappingSubsystem* URoomCreateWidgetBase::GetNatSubsystem() const
{
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		return GameInstance->GetSubsystem<UNatPortMappingSubsystem>();
	}
	return nullptr;
}

void URoomCreateWidgetBase::HandleNatMappingFinished(EMOUNatResultBP Result, int32 ExternalPort, const FString& ExternalIp)
{
	// 사용자가 이미 "방 만들기" 를 눌러 기다리고 있었다면, 지금이 보낼 때다.
	if (bCreateWaitingForNat)
	{
		bCreateWaitingForNat = false;
		SubmitCreateRoom();
		return;
	}

	// 아직 입력 중이다. 결과만 알려주고 흐름은 막지 않는다.
	if (Result == EMOUNatResultBP::Success)
	{
		SetMessage(FString::Printf(
			TEXT("공유기에 포트를 열었습니다. 다른 네트워크에서도 접속할 수 있습니다. (%s:%d)"),
			ExternalIp.IsEmpty() ? TEXT("외부IP") : *ExternalIp, ExternalPort), false);
	}
	else
	{
		// ★ 실패해도 오류가 아니다. 방 만들기를 막지 않는다.
		//   "밖에서는 못 들어온다" 고 단정하지도 않는다 — 공유기에 포트포워딩이
		//   수동으로 걸려 있으면 UPnP 가 실패해도 외부에서 들어온다. 문구는
		//   GetNatResultText 가 그 사실에 맞게 들고 있다.
		SetMessage(UNatPortMappingSubsystem::GetNatResultText(Result).ToString(), false);
	}
}

void URoomCreateWidgetBase::CancelCreate()
{
	// ★ 호스트가 되기를 그만뒀으므로 열어둔 포트를 닫는다.
	//   성공 시에는 닫지 않는다 — 그때는 매핑이 계속 살아 있어야 참가자가 들어온다.
	if (UNatPortMappingSubsystem* Nat = GetNatSubsystem())
	{
		Nat->ReleasePortMapping();
	}

	bCreateWaitingForNat = false;

	OnRoomCreateCancelled.ExecuteIfBound();
	RemoveFromParent();
}

void URoomCreateWidgetBase::HandleCreateClicked() { TryCreateRoom(); }
void URoomCreateWidgetBase::HandleCancelClicked() { CancelCreate(); }

void URoomCreateWidgetBase::HandleRoomCreated(bool bSuccess, int32 RoomId, EMOURoomResultBP Result)
{
	SetBusy(false);

	if (!bSuccess)
	{
		SubmittedPassword.Empty();
		SetMessage(UServerSubsystem::GetRoomResultText(Result), true);
		return;
	}

	SetMessage(FString::Printf(TEXT("방 #%d 을(를) 만들었습니다."), RoomId), false);

	// 소유자(로비)와 블루프린트에 같은 정보를 넘긴다.
	// 여기서 리슨서버를 여는 것이 다음 차례지만, 맵 이름은 게임 쪽 사정이라
	// 이 위젯이 결정하지 않는다.
	const FString UsedPassword = SubmittedPassword;
	SubmittedPassword.Empty();

	OnRoomCreateSucceeded(RoomId, UsedPassword);
	OnRoomCreateFinished.ExecuteIfBound(RoomId, UsedPassword);

	if (bRemoveOnSuccess)
	{
		RemoveFromParent();
	}
}

void URoomCreateWidgetBase::SetBusy(bool bInBusy)
{
	bBusy = bInBusy;
	if (CreateButton != nullptr)
	{
		CreateButton->SetIsEnabled(!bInBusy);
	}
}

void URoomCreateWidgetBase::SetMessage(const FString& Text, bool bIsError)
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
