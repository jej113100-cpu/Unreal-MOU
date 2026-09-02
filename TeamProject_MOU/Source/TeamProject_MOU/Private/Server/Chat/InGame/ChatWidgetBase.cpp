// MOU 채팅 - 채팅 UI 위젯 구현.
//
// 이 파일은 소켓/패킷을 전혀 모른다. UServerSubsystem 하고만 대화한다.
//   받을 때: OnChatMessageReceived / OnChatStateChanged / OnChatLoginCompleted 구독
//   보낼 때: UServerSubsystem::SendChat()

#include "Server/Chat/InGame/ChatWidgetBase.h"

// MOU::kProtocolVersion 을 쓰기 위해 포함한다.
// ChatProtocol.h 를 직접 넣지 않고 ChatFraming.h 를 거치는 이유는
// 그쪽이 THIRD_PARTY_INCLUDES_START 로 감싸주기 때문이다.
#include "Server/Net/ChatFraming.h"
#include "Server/ServerSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/InputComponent.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

UChatWidgetBase::UChatWidgetBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// ---------------------------------------------------------------------------
// 수명 주기
// ---------------------------------------------------------------------------

void UChatWidgetBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// WidgetTree->RootWidget 이 이미 있으면 WBP 가 만든 레이아웃이 들어온 것이다.
	// 그 경우 BindWidgetOptional 로 위젯들이 이미 연결되어 있으므로 손대지 않는다.
	// 비어 있으면 이 클래스를 C++ 단독으로 띄운 것이므로 기본 레이아웃을 조립한다.
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
	}

	if (ChatInputBox != nullptr)
	{
		ChatInputBox->OnTextCommitted.AddDynamic(this, &UChatWidgetBase::HandleInputCommitted);
	}
}

void UChatWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	// 서브시스템 델리게이트 구독.
	// 여기가 "워커 스레드 -> 게임 스레드 -> UI" 사슬의 마지막 연결점이다.
	// UServerSubsystem::Tick 이 게임 스레드에서 브로드캐스트하므로 위젯을 만져도 안전하다.
	if (UServerSubsystem* Chat = GetServerSubsystem())
	{
		if (!bSubscribed)
		{
			Chat->OnChatMessageReceived.AddDynamic(this, &UChatWidgetBase::HandleChatMessage);
			Chat->OnChatStateChanged.AddDynamic(this, &UChatWidgetBase::HandleChatStateChanged);
			Chat->OnChatLoginCompleted.AddDynamic(this, &UChatWidgetBase::HandleLoginCompleted);
			bSubscribed = true;
		}
	}
	else
	{
		AddSystemLine(TEXT("채팅 서브시스템을 찾지 못했습니다."));
	}

	// 입력 에셋 없이도 바로 쓸 수 있게, 소유 플레이어의 InputComponent 에 토글 키를 건다.
	// EnhancedInput 을 쓰더라도 레거시 InputComponent 는 함께 살아있으므로 충돌하지 않는다.
	if (bBindToggleKeyToOwningPlayer)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			if (PC->InputComponent != nullptr)
			{
				PC->InputComponent->BindKey(ChatToggleKey, IE_Pressed, this, &UChatWidgetBase::ToggleChatInput);
			}
		}
	}

	// 시작 상태: 로그만 보이고 입력창은 닫힘. 게임 조작을 방해하지 않는다.
	CloseChatInput();
	RefreshStatusText();
	RefreshChannelText();
}

void UChatWidgetBase::NativeDestruct()
{
	if (UServerSubsystem* Chat = GetServerSubsystem())
	{
		Chat->OnChatMessageReceived.RemoveDynamic(this, &UChatWidgetBase::HandleChatMessage);
		Chat->OnChatStateChanged.RemoveDynamic(this, &UChatWidgetBase::HandleChatStateChanged);
		Chat->OnChatLoginCompleted.RemoveDynamic(this, &UChatWidgetBase::HandleLoginCompleted);
	}
	bSubscribed = false;

	// 위젯이 사라진 뒤에도 키 바인딩이 남아있으면 이미 파괴된 객체를 호출한다.
	// 이 위젯이 건 바인딩만 골라서 제거한다.
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (PC->InputComponent != nullptr)
		{
			PC->InputComponent->KeyBindings.RemoveAll([this](const FInputKeyBinding& Binding)
			{
				return Binding.KeyDelegate.GetUObject() == this;
			});
		}
	}

	Super::NativeDestruct();
}

// ---------------------------------------------------------------------------
// 기본 레이아웃 조립 (WBP 가 없을 때만)
//
//   CanvasPanel (화면 전체)
//     └ Border (좌하단 620x320, 반투명 검정)
//         └ VerticalBox
//             ├ StatusText            연결 상태
//             ├ ScrollBox             로그 (Fill)
//             │   └ ChatLogBox        여기에 줄이 쌓인다
//             └ HorizontalBox
//                 ├ ChannelText       [전체]
//                 └ ChatInputBox      입력창 (Fill)
// ---------------------------------------------------------------------------

void UChatWidgetBase::BuildDefaultLayout()
{
	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ChatRootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	// SelfHitTestInvisible : 자기 자신은 마우스를 먹지 않지만 자식은 먹을 수 있다.
	// 이게 없으면 화면 전체를 덮는 캔버스가 게임 클릭을 전부 가로챈다.
	RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	ChatRootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ChatRootBorder"));
	ChatRootBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.45f));
	ChatRootBorder->SetPadding(FMargin(8.f));
	ChatRootBorder->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UCanvasPanelSlot* BorderSlot = RootCanvas->AddChildToCanvas(ChatRootBorder);
	// 앵커를 좌하단(0,1)에 고정하고 정렬도 좌하단으로 맞춘다.
	// 해상도가 바뀌어도 항상 화면 왼쪽 아래에 붙어있게 된다.
	BorderSlot->SetAnchors(FAnchors(0.f, 1.f, 0.f, 1.f));
	BorderSlot->SetAlignment(FVector2D(0.f, 1.f));
	BorderSlot->SetAutoSize(false);
	BorderSlot->SetPosition(FVector2D(24.f, -24.f));
	BorderSlot->SetSize(FVector2D(620.f, 320.f));

	UVerticalBox* MainBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ChatMainBox"));
	ChatRootBorder->AddChild(MainBox);

	// --- 상태 표시줄 ---
	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)));
	if (UVerticalBoxSlot* StatusSlot = MainBox->AddChildToVerticalBox(StatusText))
	{
		StatusSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		StatusSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}

	// --- 로그 영역 ---
	ChatScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ChatScrollBox"));
	if (UVerticalBoxSlot* ScrollSlot = MainBox->AddChildToVerticalBox(ChatScrollBox))
	{
		// Fill : 남는 세로 공간을 전부 로그가 차지한다. 상태줄과 입력줄은 자기 크기만 쓴다.
		ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	ChatLogBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ChatLogBox"));
	ChatScrollBox->AddChild(ChatLogBox);

	// --- 입력줄 ---
	UHorizontalBox* InputRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ChatInputRow"));
	if (UVerticalBoxSlot* InputRowSlot = MainBox->AddChildToVerticalBox(InputRow))
	{
		InputRowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		InputRowSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	}

	ChannelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ChannelText"));
	if (UHorizontalBoxSlot* ChannelSlot = InputRow->AddChildToHorizontalBox(ChannelText))
	{
		ChannelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		ChannelSlot->SetVerticalAlignment(VAlign_Center);
		ChannelSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	}

	ChatInputBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("ChatInputBox"));
	ChatInputBox->SetHintText(FText::FromString(TEXT("메시지 입력 (Enter 전송, Esc 취소, /t 팀, /d 사망, /a 전체)")));
	if (UHorizontalBoxSlot* InputSlot = InputRow->AddChildToHorizontalBox(ChatInputBox))
	{
		InputSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
}

// ---------------------------------------------------------------------------
// 입력 처리
// ---------------------------------------------------------------------------

void UChatWidgetBase::OpenChatInput()
{
	if (ChatInputBox == nullptr)
	{
		return;
	}

	bInputOpen = true;
	ChatInputBox->SetVisibility(ESlateVisibility::Visible);

	// 입력 중에는 로그를 스크롤할 수 있게 마우스를 받도록 바꾼다.
	if (ChatScrollBox != nullptr)
	{
		ChatScrollBox->SetVisibility(ESlateVisibility::Visible);
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		// GameAndUI : UI 가 키보드를 먹되 게임도 계속 돌아간다.
		// UIOnly 로 하면 채팅 중에 게임이 입력을 아예 못 받아 캐릭터가 멈춘 것처럼 보인다.
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);

		if (bManageMouseCursor)
		{
			PC->SetShowMouseCursor(true);
		}
	}

	ChatInputBox->SetKeyboardFocus();
}

void UChatWidgetBase::CloseChatInput()
{
	bInputOpen = false;

	if (ChatInputBox != nullptr)
	{
		ChatInputBox->SetText(FText::GetEmpty());
		// Collapsed : 자리까지 차지하지 않게 접는다. Hidden 은 자리를 남긴다.
		ChatInputBox->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 채팅을 닫으면 로그는 계속 보이되 마우스는 통과시킨다.
	// 이게 없으면 로그 영역 위에서 클릭이 게임에 전달되지 않는다.
	if (ChatScrollBox != nullptr)
	{
		ChatScrollBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		if (bManageMouseCursor)
		{
			PC->SetShowMouseCursor(false);
		}
	}
}

void UChatWidgetBase::ToggleChatInput()
{
	if (bInputOpen)
	{
		CloseChatInput();
	}
	else
	{
		OpenChatInput();
	}
}

void UChatWidgetBase::HandleInputCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	switch (CommitMethod)
	{
	case ETextCommit::OnEnter:
		SubmitInput(Text.ToString());
		// 보내고 나면 닫아서 게임 조작으로 돌아간다.
		// 연속으로 치려면 Enter 를 다시 누르면 된다.
		CloseChatInput();
		break;

	case ETextCommit::OnCleared:          // Esc
	case ETextCommit::OnUserMovedFocus:   // 다른 곳 클릭
		CloseChatInput();
		break;

	default:
		break;
	}
}

void UChatWidgetBase::SubmitInput(const FString& RawText)
{
	FString Text = RawText.TrimStartAndEnd();
	if (Text.IsEmpty())
	{
		return;
	}

	// 슬래시 명령으로 채널을 바꾼다.
	// "/t" 만 치면 채널만 바뀌고, "/t 안녕" 이면 팀 채널로 바꾸고 바로 보낸다.
	// 서버 TestClient 의 /all /team /deadchan 과 같은 개념이다.
	if (Text.StartsWith(TEXT("/")))
	{
		FString Command;
		FString Rest;
		if (!Text.Split(TEXT(" "), &Command, &Rest))
		{
			Command = Text;
			Rest.Empty();
		}
		Command = Command.ToLower();

		bool bHandled = true;
		if (Command == TEXT("/a") || Command == TEXT("/all"))
		{
			SetActiveChannel(EChatChannelBP::All);
		}
		else if (Command == TEXT("/t") || Command == TEXT("/team"))
		{
			SetActiveChannel(EChatChannelBP::Team);
		}
		else if (Command == TEXT("/d") || Command == TEXT("/dead"))
		{
			SetActiveChannel(EChatChannelBP::Dead);
		}
		else
		{
			bHandled = false;
		}

		if (bHandled)
		{
			Text = Rest.TrimStartAndEnd();
			if (Text.IsEmpty())
			{
				return;   // 채널만 바꾸고 끝
			}
		}
		// 모르는 명령이면 그냥 일반 메시지로 취급해서 그대로 보낸다.
	}

	UServerSubsystem* Chat = GetServerSubsystem();
	if (Chat == nullptr)
	{
		AddSystemLine(TEXT("채팅 서브시스템이 없어 전송할 수 없습니다."));
		return;
	}

	// 로그인 전이면 서버가 조용히 버린다. 사용자가 원인을 알 수 있게 여기서 알려준다.
	if (Chat->GetConnectionState() != EChatConnectionState::LoggedIn)
	{
		AddSystemLine(TEXT("아직 채팅 서버에 로그인되지 않았습니다."));
		return;
	}

	// 길이 제한(UTF-8 512바이트) 검사와 자르기는 SendChat 안에서 처리한다.
	Chat->SendChat(ActiveChannel, Text);
}

// ---------------------------------------------------------------------------
// 채널
// ---------------------------------------------------------------------------

void UChatWidgetBase::SetActiveChannel(EChatChannelBP NewChannel)
{
	// System 은 서버만 만들 수 있으므로 사용자가 고를 수 없다.
	if (NewChannel == EChatChannelBP::System)
	{
		return;
	}

	ActiveChannel = NewChannel;
	RefreshChannelText();
}

void UChatWidgetBase::CycleChannel()
{
	switch (ActiveChannel)
	{
	case EChatChannelBP::All:  SetActiveChannel(EChatChannelBP::Team); break;
	case EChatChannelBP::Team: SetActiveChannel(EChatChannelBP::Dead); break;
	default:                   SetActiveChannel(EChatChannelBP::All);  break;
	}
}

// ---------------------------------------------------------------------------
// 서브시스템에서 오는 이벤트
// ---------------------------------------------------------------------------

void UChatWidgetBase::HandleChatMessage(const FChatMessage& Message)
{
	// 서버가 채운 시각과 이름을 그대로 쓴다. 둘 다 위조 불가능한 값이다.
	const FString Line = FString::Printf(TEXT("[%s] %s: %s"),
		GetChannelName(Message.Channel),
		*Message.SenderName,
		*Message.Text);

	AppendLine(Line, GetChannelColor(Message.Channel));
}

void UChatWidgetBase::HandleChatStateChanged(EChatConnectionState NewState, const FString& Detail)
{
	RefreshStatusText();

	switch (NewState)
	{
	case EChatConnectionState::Connecting:
		AddSystemLine(TEXT("채팅 서버에 연결 중..."));
		break;
	case EChatConnectionState::Connected:
		AddSystemLine(TEXT("연결됨. 로그인 대기 중..."));
		break;
	case EChatConnectionState::Disconnected:
		AddSystemLine(Detail.IsEmpty()
			? TEXT("채팅 서버 연결이 끊겼습니다. 자동으로 재시도합니다.")
			: FString::Printf(TEXT("채팅 서버 연결 끊김: %s"), *Detail));
		break;
	default:
		break;
	}
}

void UChatWidgetBase::HandleLoginCompleted(const FChatLoginResult& Result)
{
	RefreshStatusText();

	if (Result.bSuccess)
	{
		// 서버가 확정한 이름을 표시한다. 내가 입력한 이름과 다를 수 있다.
		AddSystemLine(FString::Printf(TEXT("%s(으)로 접속했습니다. (팀 %d)"), *Result.Name, Result.TeamId));
	}
	else if (Result.Result == EChatLoginResultBP::VersionMismatch)
	{
		// 재접속해도 계속 실패하는 종류의 실패다. 무엇을 해야 하는지까지 알려준다.
		AddSystemLine(FString::Printf(
			TEXT("프로토콜 버전이 맞지 않습니다. (클라 %d / 서버 %d) 양쪽을 다시 빌드해야 합니다."),
			static_cast<int32>(MOU::kProtocolVersion), Result.ServerVersion));
	}
	else
	{
		AddSystemLine(TEXT("서버가 로그인을 거부했습니다."));
	}
}

// ---------------------------------------------------------------------------
// 로그 표시
// ---------------------------------------------------------------------------

void UChatWidgetBase::AddSystemLine(const FString& Text)
{
	AppendLine(FString::Printf(TEXT("[안내] %s"), *Text), GetChannelColor(EChatChannelBP::System));
}

void UChatWidgetBase::AppendLine(const FString& Line, const FLinearColor& Color)
{
	if (ChatLogBox == nullptr || WidgetTree == nullptr)
	{
		return;
	}

	UTextBlock* LineText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	LineText->SetText(FText::FromString(Line));
	LineText->SetColorAndOpacity(FSlateColor(Color));
	LineText->SetAutoWrapText(true);   // 긴 메시지가 상자 밖으로 나가지 않게 줄바꿈
	ChatLogBox->AddChild(LineText);

	// 오래된 줄부터 제거한다. 이게 없으면 장시간 플레이에서 위젯이 무한히 쌓인다.
	while (MaxLogLines > 0 && ChatLogBox->GetChildrenCount() > MaxLogLines)
	{
		ChatLogBox->RemoveChildAt(0);
	}

	if (ChatScrollBox != nullptr)
	{
		ChatScrollBox->ScrollToEnd();
	}
}

void UChatWidgetBase::RefreshStatusText()
{
	if (StatusText == nullptr)
	{
		return;
	}

	const UServerSubsystem* Chat = GetServerSubsystem();
	if (Chat == nullptr)
	{
		StatusText->SetText(FText::FromString(TEXT("채팅: 사용 불가")));
		return;
	}

	FString StatusLine;
	switch (Chat->GetConnectionState())
	{
	case EChatConnectionState::Disconnected: StatusLine = TEXT("채팅: 연결 끊김");   break;
	case EChatConnectionState::Connecting:   StatusLine = TEXT("채팅: 연결 중...");  break;
	case EChatConnectionState::Connected:    StatusLine = TEXT("채팅: 로그인 중..."); break;
	case EChatConnectionState::LoggedIn:
		StatusLine = FString::Printf(TEXT("채팅: %s (팀 %d)"),
			*Chat->GetLoginResult().Name, Chat->GetLoginResult().TeamId);
		break;
	}

	StatusText->SetText(FText::FromString(StatusLine));
}

void UChatWidgetBase::RefreshChannelText()
{
	if (ChannelText == nullptr)
	{
		return;
	}

	ChannelText->SetText(FText::FromString(FString::Printf(TEXT("[%s]"), GetChannelName(ActiveChannel))));
	ChannelText->SetColorAndOpacity(FSlateColor(GetChannelColor(ActiveChannel)));
}

// ---------------------------------------------------------------------------
// 유틸
// ---------------------------------------------------------------------------

UServerSubsystem* UChatWidgetBase::GetServerSubsystem() const
{
	// UServerSubsystem::Get 은 WorldContext 로 GameInstance 를 찾는다.
	// PIE 창이 여러 개면 이 위젯이 속한 창의 서브시스템이 잡힌다.
	return UServerSubsystem::Get(this);
}

const TCHAR* UChatWidgetBase::GetChannelName(EChatChannelBP Channel)
{
	// 서버 Server.cpp 의 ChannelName() 과 같은 문자열을 쓴다.
	switch (Channel)
	{
	case EChatChannelBP::All:     return TEXT("전체");
	case EChatChannelBP::Team:    return TEXT("팀");
	case EChatChannelBP::Dead:    return TEXT("사망");
	case EChatChannelBP::Whisper: return TEXT("귓속말");
	case EChatChannelBP::System:  return TEXT("시스템");
	default:                      return TEXT("알수없음");
	}
}

FLinearColor UChatWidgetBase::GetChannelColor(EChatChannelBP Channel)
{
	switch (Channel)
	{
	case EChatChannelBP::All:     return FLinearColor(0.92f, 0.92f, 0.92f);
	case EChatChannelBP::Team:    return FLinearColor(0.35f, 0.75f, 1.00f);
	case EChatChannelBP::Dead:    return FLinearColor(0.60f, 0.60f, 0.60f);
	case EChatChannelBP::Whisper: return FLinearColor(1.00f, 0.55f, 0.90f);
	case EChatChannelBP::System:  return FLinearColor(1.00f, 0.85f, 0.40f);
	default:                      return FLinearColor::White;
	}
}

// ---------------------------------------------------------------------------
// 콘솔 명령 - 게임 플로우에 위젯 생성 코드를 넣기 전에 UI 를 검증하기 위한 것.
//
//   MOU.Chat.ShowUI     채팅 위젯을 뷰포트에 띄운다
//   MOU.Chat.HideUI     떼어낸다
//   MOU.Chat.ToggleInput 입력창 열기/닫기 (Enter 키 바인딩이 안 먹을 때 대체용)
//
// 실제 게임에서는 PlayerController 나 HUD 가 BeginPlay 에서
// CreateWidget<UChatWidgetBase>(PC, ChatWidgetClass) -> AddToViewport() 하면 된다.
// ---------------------------------------------------------------------------

namespace
{
	/**
	 * 콘솔로 띄운 위젯을 월드별로 기억한다. 디버그 용도로만 쓴다.
	 *
	 * 월드마다 따로 두는 이유: PIE 에서 플레이어 수를 2 이상으로 올리면 창마다 월드가 따로 생긴다.
	 * 전역 변수 하나로 관리하면 두 번째 창에서 ShowUI 를 쳐도 "이미 떠 있다"로 막혀서
	 * 사망 채널처럼 두 창을 나란히 봐야 검증되는 기능을 눈으로 확인할 수 없다.
	 */
	TMap<TWeakObjectPtr<UWorld>, TWeakObjectPtr<UChatWidgetBase>> GDebugChatWidgets;

	/** 파괴된 월드/위젯 항목을 정리한다. PIE 를 반복하면 죽은 키가 쌓이기 때문이다. */
	void PruneDebugChatWidgets()
	{
		for (auto It = GDebugChatWidgets.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid() || !It.Value().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	UChatWidgetBase* FindDebugChatWidget(UWorld* World)
	{
		PruneDebugChatWidgets();
		const TWeakObjectPtr<UChatWidgetBase>* Found = GDebugChatWidgets.Find(World);
		return Found ? Found->Get() : nullptr;
	}

	APlayerController* GetFirstLocalPlayerController(UWorld* World)
	{
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs GChatShowUICommand(
		TEXT("MOU.Chat.ShowUI"),
		TEXT("채팅 위젯을 화면에 띄운다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				if (FindDebugChatWidget(World) != nullptr)
				{
					return;   // 이 창에는 이미 떠 있다
				}

				APlayerController* PC = GetFirstLocalPlayerController(World);
				if (PC == nullptr)
				{
					return;
				}

				UChatWidgetBase* Widget = CreateWidget<UChatWidgetBase>(PC, UChatWidgetBase::StaticClass());
				if (Widget != nullptr)
				{
					Widget->AddToViewport();
					GDebugChatWidgets.Add(World, Widget);
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GChatHideUICommand(
		TEXT("MOU.Chat.HideUI"),
		TEXT("채팅 위젯을 화면에서 제거한다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				if (UChatWidgetBase* Widget = FindDebugChatWidget(World))
				{
					Widget->RemoveFromParent();
				}
				GDebugChatWidgets.Remove(World);
			}));

	FAutoConsoleCommandWithWorldAndArgs GChatToggleInputCommand(
		TEXT("MOU.Chat.ToggleInput"),
		TEXT("채팅 입력창을 열거나 닫는다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				if (UChatWidgetBase* Widget = FindDebugChatWidget(World))
				{
					Widget->ToggleChatInput();
				}
			}));
}
