#include "Chat/LoginWidgetBase.h"

#include "Blueprint/WidgetTree.h"
#include "Chat/ServerSettings.h"
#include "Chat/ChatSubsystem.h"
#include "Chat/ChatWidgetBase.h"
#include "Chat/LobbyWidgetBase.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"

ULoginWidgetBase::ULoginWidgetBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 로그인 화면은 마우스를 받아야 하므로 게임 입력을 막는 편이 자연스럽다.
	// 다만 입력 모드 전환은 게임 쪽 흐름과 충돌할 수 있어 여기서 강제하지 않는다.
	SetIsFocusable(true);
}

void ULoginWidgetBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// WBP 가 아무 위젯도 바인딩해주지 않았다면 C++ 이 직접 기본 화면을 만든다.
	// ChatWidgetBase 와 같은 규약이다 — 디자이너 작업을 기다리지 않고 검증할 수 있다.
	if (LoginIdBox == nullptr && PasswordBox == nullptr && LoginButton == nullptr)
	{
		BuildDefaultLayout();
	}
}

void ULoginWidgetBase::BuildDefaultLayout()
{
	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LoginRootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LoginPanel"));
	Panel->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.04f, 0.92f));
	Panel->SetPadding(FMargin(20.f));

	UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(Panel);
	// 화면 정중앙에 고정한다. 해상도가 바뀌어도 가운데를 유지한다.
	PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	PanelSlot->SetAutoSize(false);
	PanelSlot->SetPosition(FVector2D::ZeroVector);
	PanelSlot->SetSize(FVector2D(420.f, 320.f));

	UVerticalBox* MainBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LoginMainBox"));
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
	TitleText->SetText(FText::FromString(TEXT("MOU 채팅 로그인")));
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	AddRow(TitleText, 12.f);

	LoginIdBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("LoginIdBox"));
	LoginIdBox->SetHintText(FText::FromString(TEXT("아이디")));
	AddRow(LoginIdBox, 6.f);

	PasswordBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("PasswordBox"));
	PasswordBox->SetHintText(FText::FromString(TEXT("비밀번호")));
	// 어깨너머로 보이지 않게 가린다. 로그인 화면의 기본 예의다.
	PasswordBox->SetIsPassword(true);
	AddRow(PasswordBox, 6.f);

	NicknameBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("NicknameBox"));
	NicknameBox->SetHintText(FText::FromString(TEXT("닉네임 (가입할 때만, 비우면 아이디 사용)")));
	AddRow(NicknameBox, 12.f);

	// --- 버튼 두 개를 가로로 ---
	UHorizontalBox* ButtonRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("LoginButtonRow"));
	AddRow(ButtonRow, 10.f);

	auto MakeButton = [&](const TCHAR* Name, const FString& Label) -> UButton*
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* Label2 = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *(FString(Name) + TEXT("Label")));
		Label2->SetText(FText::FromString(Label));
		Button->AddChild(Label2);

		if (UHorizontalBoxSlot* Slot = ButtonRow->AddChildToHorizontalBox(Button))
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Slot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		}
		return Button;
	};

	LoginButton    = MakeButton(TEXT("LoginButton"),    TEXT("로그인"));
	RegisterButton = MakeButton(TEXT("RegisterButton"), TEXT("계정 만들기"));

	MessageText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MessageText"));
	MessageText->SetAutoWrapText(true);
	MessageText->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)));
	AddRow(MessageText, 0.f);
}

void ULoginWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	if (LoginButton != nullptr)
	{
		LoginButton->OnClicked.AddUniqueDynamic(this, &ULoginWidgetBase::HandleLoginClicked);
	}
	if (RegisterButton != nullptr)
	{
		RegisterButton->OnClicked.AddUniqueDynamic(this, &ULoginWidgetBase::HandleRegisterClicked);
	}

	// NativeConstruct 는 뷰포트에 다시 붙을 때마다 불릴 수 있어 중복 구독을 막는다.
	if (!bSubscribed)
	{
		if (UChatSubsystem* Chat = GetChatSubsystem())
		{
			Chat->OnChatLoginCompleted.AddDynamic(this, &ULoginWidgetBase::HandleLoginCompleted);
			Chat->OnChatRegisterCompleted.AddDynamic(this, &ULoginWidgetBase::HandleRegisterCompleted);
			Chat->OnChatStateChanged.AddDynamic(this, &ULoginWidgetBase::HandleStateChanged);
			bSubscribed = true;
		}
	}

	// 화면이 뜨자마자 서버에 붙어둔다. 사용자가 입력하는 동안 연결이 끝나 있게 된다.
	EnsureConnected();
	SetMessage(TEXT("아이디와 비밀번호를 입력하세요."), false);
}

void ULoginWidgetBase::NativeDestruct()
{
	// 구독 해제를 여기서 반드시 해야 파괴된 위젯으로 델리게이트가 날아오지 않는다.
	if (bSubscribed)
	{
		if (UChatSubsystem* Chat = GetChatSubsystem())
		{
			Chat->OnChatLoginCompleted.RemoveDynamic(this, &ULoginWidgetBase::HandleLoginCompleted);
			Chat->OnChatRegisterCompleted.RemoveDynamic(this, &ULoginWidgetBase::HandleRegisterCompleted);
			Chat->OnChatStateChanged.RemoveDynamic(this, &ULoginWidgetBase::HandleStateChanged);
		}
		bSubscribed = false;
	}

	Super::NativeDestruct();
}

// ---------------------------------------------------------------------------
// 동작
// ---------------------------------------------------------------------------

UChatSubsystem* ULoginWidgetBase::GetChatSubsystem() const
{
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		return GameInstance->GetSubsystem<UChatSubsystem>();
	}
	return nullptr;
}

void ULoginWidgetBase::EnsureConnected()
{
	UChatSubsystem* Chat = GetChatSubsystem();
	if (Chat == nullptr)
	{
		SetMessage(TEXT("채팅 시스템을 찾을 수 없습니다."), true);
		return;
	}

	if (Chat->GetConnectionState() == EChatConnectionState::Disconnected)
	{
		// 비워두면 ConnectToChatServer 가 설정에서 주소를 읽는다. 위젯이 굳이
		// 기본 주소를 알 필요는 없다 — 아는 곳이 여러 군데면 다시 어긋난다.
		Chat->ConnectToChatServer(ServerHost, ServerPort);

		// 어디에 붙는 중인지 화면에 보여준다. 접속이 안 될 때 "내가 어느 서버를
		// 보고 있었는지" 를 사용자가 바로 알 수 있어야 한다.
		const FString Target = (ServerHost.IsEmpty() || ServerPort <= 0)
			? UMOUServerSettings::GetResolvedEndpointText()
			: FString::Printf(TEXT("%s:%d"), *ServerHost, ServerPort);
		UE_LOG(LogMOUChat, Log, TEXT("로그인 화면이 접속을 시작한다: %s"), *Target);
	}
}

bool ULoginWidgetBase::ReadAndValidateInput(FString& OutId, FString& OutPassword)
{
	OutId       = LoginIdBox  ? LoginIdBox->GetText().ToString()  : FString();
	OutPassword = PasswordBox ? PasswordBox->GetText().ToString() : FString();

	// 앞뒤 공백은 사용자가 의도한 것이 아닌 경우가 대부분이라 아이디에서만 걷어낸다.
	// 비밀번호는 공백도 유효한 문자이므로 절대 건드리지 않는다.
	OutId.TrimStartAndEndInline();

	FString Reason;
	if (!UChatSubsystem::ValidateCredentials(OutId, OutPassword, Reason))
	{
		SetMessage(Reason, true);
		return false;
	}
	return true;
}

void ULoginWidgetBase::TryLogin()
{
	if (bBusy)
	{
		return;
	}

	FString Id, Password;
	if (!ReadAndValidateInput(Id, Password))
	{
		return;
	}

	UChatSubsystem* Chat = GetChatSubsystem();
	if (Chat == nullptr)
	{
		SetMessage(TEXT("채팅 시스템을 찾을 수 없습니다."), true);
		return;
	}

	SetBusy(true);
	SetMessage(TEXT("로그인 중..."), false);

	EnsureConnected();
	// 아직 연결 전이면 서브시스템이 요청을 보관했다가 연결되는 순간 보낸다.
	Chat->Login(Id, Password, TeamId);
}

void ULoginWidgetBase::TryRegister()
{
	if (bBusy)
	{
		return;
	}

	FString Id, Password;
	if (!ReadAndValidateInput(Id, Password))
	{
		return;
	}

	UChatSubsystem* Chat = GetChatSubsystem();
	if (Chat == nullptr)
	{
		SetMessage(TEXT("채팅 시스템을 찾을 수 없습니다."), true);
		return;
	}

	const FString Nickname = NicknameBox ? NicknameBox->GetText().ToString().TrimStartAndEnd() : FString();

	SetBusy(true);
	SetMessage(TEXT("계정을 만드는 중..."), false);

	// 가입에 성공하면 사용자가 버튼을 한 번 더 누르지 않아도 되도록 이어서 로그인한다.
	bAutoLoginAfterRegister = true;

	EnsureConnected();
	Chat->RegisterAccount(Id, Password, Nickname);
}

void ULoginWidgetBase::HandleLoginClicked()    { TryLogin(); }
void ULoginWidgetBase::HandleRegisterClicked() { TryRegister(); }

void ULoginWidgetBase::HandleRegisterCompleted(bool bSuccess, EChatLoginResultBP Result)
{
	if (!bSuccess)
	{
		bAutoLoginAfterRegister = false;
		SetBusy(false);
		SetMessage(UChatSubsystem::GetLoginResultText(Result), true);
		return;
	}

	if (bAutoLoginAfterRegister)
	{
		bAutoLoginAfterRegister = false;
		SetMessage(TEXT("계정을 만들었습니다. 로그인 중..."), false);

		FString Id, Password;
		if (ReadAndValidateInput(Id, Password))
		{
			if (UChatSubsystem* Chat = GetChatSubsystem())
			{
				Chat->Login(Id, Password, TeamId);
				return;   // bBusy 는 로그인 응답까지 유지한다
			}
		}
	}

	SetBusy(false);
	SetMessage(TEXT("계정을 만들었습니다. 로그인해 주세요."), false);
}

void ULoginWidgetBase::HandleLoginCompleted(const FChatLoginResult& Result)
{
	SetBusy(false);

	if (!Result.bSuccess)
	{
		SetMessage(UChatSubsystem::GetLoginResultText(Result.Result), true);
		// 비밀번호는 화면에 남겨두지 않는다. 아이디는 고칠 일이 적으니 남긴다.
		if (PasswordBox != nullptr)
		{
			PasswordBox->SetText(FText::GetEmpty());
		}
		return;
	}

	SetMessage(FString::Printf(TEXT("%s 님, 환영합니다."), *Result.Name), false);

	// 성공한 뒤에는 입력칸에 자격증명을 남기지 않는다.
	if (PasswordBox != nullptr)
	{
		PasswordBox->SetText(FText::GetEmpty());
	}

	OnLoginSucceeded(Result);

	// 채팅을 먼저 띄우고 로비를 나중에 띄운다. 나중에 붙은 쪽이 위에 오므로
	// 로비 버튼이 채팅창에 가리지 않는다.
	if (bShowChatWidgetOnSuccess)
	{
		ShowChatWidget();
	}
	if (bShowLobbyWidgetOnSuccess)
	{
		ShowLobbyWidget();
	}
	if (bRemoveOnSuccess)
	{
		RemoveFromParent();
	}
}

void ULoginWidgetBase::HandleStateChanged(EChatConnectionState NewState, const FString& Detail)
{
	// 연결이 끊긴 상태만 알려준다. 나머지는 로그인 결과 메시지가 덮어쓰므로 조용히 둔다.
	if (NewState == EChatConnectionState::Disconnected)
	{
		SetBusy(false);
		// 어느 서버에 실패했는지까지 같이 보여준다. 팀 작업에서는 "서버가 꺼져 있다" 보다
		// "엉뚱한 주소를 보고 있다" 가 훨씬 흔한 원인이라 주소가 화면에 있어야 한다.
		const FString Target = UMOUServerSettings::GetResolvedEndpointText();
		SetMessage(Detail.IsEmpty()
			? FString::Printf(TEXT("채팅 서버(%s)에 연결할 수 없습니다. 서버가 켜져 있는지 확인하세요."), *Target)
			: FString::Printf(TEXT("연결 끊김: %s (대상 %s)"), *Detail, *Target), true);
	}
}

void ULoginWidgetBase::ShowChatWidget()
{
	APlayerController* PC = GetOwningPlayer();
	if (PC == nullptr)
	{
		return;
	}

	// 디자이너 WBP 가 지정돼 있으면 그것을, 없으면 C++ 기본 위젯을 쓴다.
	UClass* WidgetClass = ChatWidgetClass ? ChatWidgetClass.Get() : UChatWidgetBase::StaticClass();
	if (UUserWidget* ChatWidget = CreateWidget<UUserWidget>(PC, WidgetClass))
	{
		ChatWidget->AddToViewport();
	}
}

void ULoginWidgetBase::ShowLobbyWidget()
{
	APlayerController* PC = GetOwningPlayer();
	if (PC == nullptr)
	{
		return;
	}

	UClass* WidgetClass = LobbyWidgetClass ? LobbyWidgetClass.Get() : ULobbyWidgetBase::StaticClass();
	if (UUserWidget* LobbyWidget = CreateWidget<UUserWidget>(PC, WidgetClass))
	{
		LobbyWidget->AddToViewport();
	}
}

void ULoginWidgetBase::SetBusy(bool bInBusy)
{
	bBusy = bInBusy;
	if (LoginButton != nullptr)
	{
		LoginButton->SetIsEnabled(!bInBusy);
	}
	if (RegisterButton != nullptr)
	{
		RegisterButton->SetIsEnabled(!bInBusy);
	}
}

void ULoginWidgetBase::SetMessage(const FString& Text, bool bIsError)
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
// 콘솔 명령 - 게임 플로우에 로그인 화면을 붙이기 전에 UI 를 검증하기 위한 것.
//
//   MOU.Chat.ShowLogin      로그인 위젯을 띄운다
//
// 게임에서 정식으로 쓸 때는 이 명령이 아니라, 타이틀 화면에서
// CreateWidget<ULoginWidgetBase>() -> AddToViewport() 를 직접 호출하면 된다.
// ---------------------------------------------------------------------------

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs GShowLoginCommand(
	TEXT("MOU.Chat.ShowLogin"),
	TEXT("로그인 UI 를 띄운다. 사용법: MOU.Chat.ShowLogin [호스트] [포트]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			if (PC == nullptr)
			{
				return;
			}

			ULoginWidgetBase* Widget = CreateWidget<ULoginWidgetBase>(PC, ULoginWidgetBase::StaticClass());
			if (Widget == nullptr)
			{
				return;
			}

			// NativeConstruct 에서 접속하므로 뷰포트에 붙이기 전에 설정을 끝내야 한다.
			if (Args.IsValidIndex(0)) { Widget->ServerHost = Args[0]; }
			if (Args.IsValidIndex(1)) { Widget->ServerPort = FCString::Atoi(*Args[1]); }

			Widget->AddToViewport();

			// 로그인 화면은 마우스로 조작하므로 커서를 켜준다.
			PC->SetShowMouseCursor(true);
		}));
#endif
