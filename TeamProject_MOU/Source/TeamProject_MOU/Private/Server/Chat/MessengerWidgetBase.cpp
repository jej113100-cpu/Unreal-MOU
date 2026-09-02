// MOU 메신저 - 대화창 + 통합 패널 구현 (v7 M9).
// 대응하는 설계 문서: CHAT_DESIGN.md 6절, 10절

#include "Server/Chat/MessengerWidgetBase.h"

#include "Server/ServerSubsystem.h"
#include "Server/Social/FriendListWidgetBase.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"

namespace
{
	/** 내 말풍선과 상대 말풍선의 색을 다르게 한다. */
	FLinearColor BubbleColor(bool bIsMine)
	{
		return bIsMine ? FLinearColor(0.75f, 0.88f, 1.f) : FLinearColor(0.92f, 0.92f, 0.92f);
	}
}

// ===========================================================================
// UDmWindowWidget - 대화창 하나
// ===========================================================================

UDmWindowWidget::UDmWindowWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UDmWindowWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
	}
}

// 대화창의 기본 모양:
//   VerticalBox
//     - 제목줄  [DmTitleText (Fill)] [DmCloseButton]
//     - DmMoreButton      "이전 대화 더 보기"
//     - DmScrollBox
//         └ DmMessageBox   말풍선들
//     - 입력줄  [DmInputBox (Fill)] [DmSendButton]
void UDmWindowWidget::BuildDefaultLayout()
{
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DmRoot"));
	WidgetTree->RootWidget = Root;

	auto AddRow = [&](UWidget* Widget, ESlateSizeRule::Type Rule, float BottomPadding)
	{
		if (UVerticalBoxSlot* BoxSlot = Root->AddChildToVerticalBox(Widget))
		{
			BoxSlot->SetSize(FSlateChildSize(Rule));
			BoxSlot->SetPadding(FMargin(0.f, 0.f, 0.f, BottomPadding));
		}
	};

	auto MakeButton = [&](const TCHAR* Name, const TCHAR* LabelName, const TCHAR* Label) -> UButton*
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), LabelName);
		Text->SetText(FText::FromString(Label));
		Button->AddChild(Text);
		return Button;
	};

	// 제목줄
	UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("DmTitleRow"));

	DmTitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DmTitleText"));
	if (UHorizontalBoxSlot* BoxSlot = TitleRow->AddChildToHorizontalBox(DmTitleText))
	{
		BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BoxSlot->SetVerticalAlignment(VAlign_Center);
	}

	DmCloseButton = MakeButton(TEXT("DmCloseButton"), TEXT("DmCloseLabel"), TEXT("X"));
	if (UHorizontalBoxSlot* BoxSlot = TitleRow->AddChildToHorizontalBox(DmCloseButton))
	{
		BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}
	AddRow(TitleRow, ESlateSizeRule::Automatic, 4.f);

	DmMoreButton = MakeButton(TEXT("DmMoreButton"), TEXT("DmMoreLabel"), TEXT("이전 대화 더 보기"));
	DmMoreButton->SetVisibility(ESlateVisibility::Collapsed);
	AddRow(DmMoreButton, ESlateSizeRule::Automatic, 2.f);

	DmScrollBox  = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("DmScrollBox"));
	DmMessageBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DmMessageBox"));
	DmScrollBox->AddChild(DmMessageBox);
	AddRow(DmScrollBox, ESlateSizeRule::Fill, 4.f);

	// 입력줄
	UHorizontalBox* InputRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("DmInputRow"));

	DmInputBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("DmInputBox"));
	DmInputBox->SetHintText(FText::FromString(TEXT("메시지를 입력하세요.")));
	if (UHorizontalBoxSlot* BoxSlot = InputRow->AddChildToHorizontalBox(DmInputBox))
	{
		BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BoxSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
	}

	DmSendButton = MakeButton(TEXT("DmSendButton"), TEXT("DmSendLabel"), TEXT("전송"));
	if (UHorizontalBoxSlot* BoxSlot = InputRow->AddChildToHorizontalBox(DmSendButton))
	{
		BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}
	AddRow(InputRow, ESlateSizeRule::Automatic, 0.f);
}

void UDmWindowWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (DmSendButton != nullptr)
	{
		DmSendButton->OnClicked.AddUniqueDynamic(this, &UDmWindowWidget::HandleSendClicked);
	}
	if (DmCloseButton != nullptr)
	{
		DmCloseButton->OnClicked.AddUniqueDynamic(this, &UDmWindowWidget::HandleCloseClicked);
	}
	if (DmMoreButton != nullptr)
	{
		DmMoreButton->OnClicked.AddUniqueDynamic(this, &UDmWindowWidget::HandleMoreClicked);
	}
	if (DmInputBox != nullptr)
	{
		// 엔터로도 보낼 수 있게 한다. 채팅창에서 버튼을 누르게 하면 답답하다.
		DmInputBox->OnTextCommitted.AddUniqueDynamic(this, &UDmWindowWidget::HandleInputCommitted);
	}
}

UServerSubsystem* UDmWindowWidget::GetServerSubsystem() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UServerSubsystem>() : nullptr;
}

void UDmWindowWidget::SetPeer(int64 InPeerUserId, const FString& InPeerNickname)
{
	PeerUserId   = InPeerUserId;
	PeerNickname = InPeerNickname;

	if (DmTitleText != nullptr)
	{
		DmTitleText->SetText(FText::FromString(PeerNickname));
	}
}

void UDmWindowWidget::InsertBubble(const FMOUDirectMessage& Message, bool bPrepend)
{
	if (DmMessageBox == nullptr)
	{
		return;
	}

	UTextBlock* Bubble = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Bubble->SetText(FText::FromString(
		FString::Printf(TEXT("%s%s"), Message.bIsMine ? TEXT("나: ") : TEXT(""), *Message.Text)));
	Bubble->SetColorAndOpacity(FSlateColor(BubbleColor(Message.bIsMine)));

	DmMessageBox->AddChildToVerticalBox(Bubble);

	// ★ 위로 스크롤한 결과는 **앞에** 와야 한다. AddChild 는 항상 뒤에 붙이므로
	//   넣은 뒤에 자리를 옮긴다. ShiftChild 가 없는 버전을 대비해 인덱스로 옮긴다.
	if (bPrepend)
	{
		const int32 LastIndex = DmMessageBox->GetChildrenCount() - 1;
		if (LastIndex > 0)
		{
			DmMessageBox->ShiftChild(0, Bubble);
		}
	}
}

void UDmWindowWidget::AppendMessage(const FMOUDirectMessage& Message)
{
	InsertBubble(Message, /*bPrepend=*/false);

	// 새 메시지가 오면 아래로 따라 내려간다.
	if (DmScrollBox != nullptr)
	{
		DmScrollBox->ScrollToEnd();
	}
}

void UDmWindowWidget::SetHistory(const TArray<FMOUDirectMessage>& Messages, bool bHasMore)
{
	if (DmMessageBox == nullptr)
	{
		return;
	}

	// ★ 플래그를 여기서 소비한다. 다음 응답은 다시 "처음 연 것" 으로 취급되므로,
	//   더 보기를 또 누르지 않는 한 앞에 붙는 일이 없다(헤더 ★★).
	const bool bPrepend = bAwaitingOlder;
	bAwaitingOlder = false;

	if (!bPrepend)
	{
		// 창을 처음 연 것이다. 통째로 갈아끼운다.
		DmMessageBox->ClearChildren();
		OldestMessageId = 0;
	}

	// 서버는 **오래된 것 -> 최신 순**으로 준다.
	//
	// ★ 앞에 붙일 때는 역순으로 넣어야 순서가 맞는다. 오래된 것부터 맨 앞에
	//   꽂으면 결과가 뒤집힌다 - 각 삽입이 앞선 것을 밀어내기 때문이다.
	if (bPrepend)
	{
		for (int32 i = Messages.Num() - 1; i >= 0; --i)
		{
			InsertBubble(Messages[i], /*bPrepend=*/true);
		}
	}
	else
	{
		for (const FMOUDirectMessage& Msg : Messages)
		{
			InsertBubble(Msg, /*bPrepend=*/false);
		}
	}

	// 가장 오래된 번호를 갱신한다. 위로 스크롤할 때 커서로 쓴다.
	if (Messages.Num() > 0)
	{
		const int64 First = Messages[0].MessageId;
		if (OldestMessageId == 0 || First < OldestMessageId)
		{
			OldestMessageId = First;
		}
	}

	if (DmMoreButton != nullptr)
	{
		DmMoreButton->SetVisibility(bHasMore ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (!bPrepend && DmScrollBox != nullptr)
	{
		DmScrollBox->ScrollToEnd();
	}
}

void UDmWindowWidget::HandleSendClicked()
{
	if (DmInputBox == nullptr)
	{
		return;
	}

	const FString Text = DmInputBox->GetText().ToString().TrimStartAndEnd();
	if (Text.IsEmpty())
	{
		return;
	}

	if (UServerSubsystem* Chat = GetServerSubsystem())
	{
		Chat->SendDirectMessage(PeerUserId, Text);
	}

	// ★ 화면에 먼저 그리지 않는다. 서버가 저장하고 **나에게도 되돌려주므로**
	//   그때 그린다. 미리 그리면 서버가 거부했을 때(친구가 아님 등) 지워야 하고,
	//   서버가 매긴 MessageId 도 모른다.
	DmInputBox->SetText(FText::GetEmpty());
}

void UDmWindowWidget::HandleInputCommitted(const FText& /*Text*/, ETextCommit::Type CommitMethod)
{
	// 포커스를 잃은 것(OnUserMovedFocus)까지 전송으로 처리하면 창을 옮기다가
	// 쓰다 만 글이 나간다. 엔터만 받는다.
	if (CommitMethod == ETextCommit::OnEnter)
	{
		HandleSendClicked();
	}
}

void UDmWindowWidget::HandleCloseClicked()
{
	// 스스로 파괴하지 않는다. 부모의 TMap 에 죽은 포인터가 남기 때문이다(헤더 ★).
	OnCloseRequested.ExecuteIfBound(PeerUserId);
}

void UDmWindowWidget::HandleMoreClicked()
{
	if (OldestMessageId <= 0)
	{
		return;   // 아직 아무것도 못 받았다
	}

	if (UServerSubsystem* Chat = GetServerSubsystem())
	{
		// ★ 다음에 올 기록은 **앞에** 붙어야 한다. 응답에는 그 구분이 없으므로
		//   요청한 이 시점에 기억해 둔다(헤더 bAwaitingOlder).
		bAwaitingOlder = true;

		// ★ LoadOlderMessages 는 읽음 처리를 하지 않는다. OpenConversation 과
		//   다른 함수인 이유가 그것이다.
		Chat->LoadOlderMessages(PeerUserId, OldestMessageId);
	}
}

// ===========================================================================
// UMessengerWidgetBase - 통합 패널
// ===========================================================================

UMessengerWidgetBase::UMessengerWidgetBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FriendListClass = UFriendListWidgetBase::StaticClass();
	DmWindowClass   = UDmWindowWidget::StaticClass();
}

void UMessengerWidgetBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
	}
}

// 기본 모양:
//   HorizontalBox
//     - ConversationArea (Fill)   대화창 0~N개
//     - FriendPanelSlot           친구 목록 (항상)
void UMessengerWidgetBase::BuildDefaultLayout()
{
	UHorizontalBox* Root = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MessengerRoot"));
	WidgetTree->RootWidget = Root;

	ConversationArea = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ConversationArea"));
	if (UHorizontalBoxSlot* BoxSlot = Root->AddChildToHorizontalBox(ConversationArea))
	{
		BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BoxSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	}

	FriendPanelSlot = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FriendPanelSlot"));
	if (UHorizontalBoxSlot* BoxSlot = Root->AddChildToHorizontalBox(FriendPanelSlot))
	{
		BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}
}

UServerSubsystem* UMessengerWidgetBase::GetServerSubsystem() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UServerSubsystem>() : nullptr;
}

void UMessengerWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	// 친구 목록을 만들어 붙인다.
	if (FriendList == nullptr && FriendPanelSlot != nullptr)
	{
		// ★ 삼항 연산자는 TSubclassOf<T> 와 UClass* 사이에서 모호해진다(C2445).
		TSubclassOf<UFriendListWidgetBase> ListClass = FriendListClass;
		if (!ListClass)
		{
			ListClass = UFriendListWidgetBase::StaticClass();
		}

		FriendList = CreateWidget<UFriendListWidgetBase>(GetOwningPlayer(), ListClass);
		if (FriendList != nullptr)
		{
			// 목록은 "누굴 눌렀는지" 만 알린다. 창을 만드는 것은 여기다.
			FriendList->OnConversationRequested.BindUObject(
				this, &UMessengerWidgetBase::HandleConversationRequested);
			FriendPanelSlot->AddChildToVerticalBox(FriendList);
		}
	}

	if (UServerSubsystem* Chat = GetServerSubsystem())
	{
		Chat->OnDirectMessageReceived.AddUniqueDynamic(this, &UMessengerWidgetBase::HandleDirectMessageReceived);
		Chat->OnDmHistoryReceived.AddUniqueDynamic(this, &UMessengerWidgetBase::HandleDmHistoryReceived);
		Chat->OnFriendUpdated.AddUniqueDynamic(this, &UMessengerWidgetBase::HandleFriendUpdated);
	}
}

void UMessengerWidgetBase::NativeDestruct()
{
	// 서브시스템이 이 위젯보다 오래 살므로 반드시 해제한다.
	if (UServerSubsystem* Chat = GetServerSubsystem())
	{
		Chat->OnDirectMessageReceived.RemoveAll(this);
		Chat->OnDmHistoryReceived.RemoveAll(this);
		Chat->OnFriendUpdated.RemoveAll(this);
	}

	Super::NativeDestruct();
}

FString UMessengerWidgetBase::ResolveNickname(int64 UserId) const
{
	if (const UServerSubsystem* Chat = GetServerSubsystem())
	{
		for (const FMOUFriend& Entry : Chat->GetFriendsRef())
		{
			if (Entry.UserId == UserId)
			{
				return Entry.Nickname;
			}
		}
	}
	return TEXT("(알 수 없음)");
}

void UMessengerWidgetBase::OpenConversation(int64 PeerUserId)
{
	if (PeerUserId == 0 || ConversationArea == nullptr)
	{
		return;
	}

	UServerSubsystem* Chat = GetServerSubsystem();
	if (Chat == nullptr)
	{
		return;
	}

	// 이미 열려 있으면 새로 만들지 않는다.
	if (TObjectPtr<UDmWindowWidget>* Existing = Windows.Find(PeerUserId))
	{
		if (*Existing != nullptr)
		{
			// 다시 눌렀다는 것은 보겠다는 뜻이다. 읽음 처리를 다시 태운다.
			Chat->OpenConversation(PeerUserId);

			// 최근 사용으로 올린다(상한을 넘었을 때 이 창이 먼저 닫히지 않게).
			WindowOrder.Remove(PeerUserId);
			WindowOrder.Add(PeerUserId);
			return;
		}
	}

	// ★ 상한을 넘으면 가장 오래 안 쓴 창을 닫는다. 안 그러면 창이 화면을 다 덮는다.
	while (WindowOrder.Num() >= MaxOpenConversations && WindowOrder.Num() > 0)
	{
		CloseConversation(WindowOrder[0]);
	}

	// ★ 삼항 연산자는 TSubclassOf<T> 와 UClass* 사이에서 모호해진다(C2445).
	TSubclassOf<UDmWindowWidget> WindowClass = DmWindowClass;
	if (!WindowClass)
	{
		WindowClass = UDmWindowWidget::StaticClass();
	}

	UDmWindowWidget* Window = CreateWidget<UDmWindowWidget>(GetOwningPlayer(), WindowClass);
	if (Window == nullptr)
	{
		return;
	}

	Window->SetPeer(PeerUserId, ResolveNickname(PeerUserId));
	Window->OnCloseRequested.BindUObject(this, &UMessengerWidgetBase::HandleWindowCloseRequested);

	if (UHorizontalBoxSlot* BoxSlot = ConversationArea->AddChildToHorizontalBox(Window))
	{
		BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BoxSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
	}

	Windows.Add(PeerUserId, Window);
	WindowOrder.Add(PeerUserId);

	// ★ 이 호출이 곧 "읽음" 이다. 기록을 받고 배지가 사라진다(헤더 주석).
	Chat->OpenConversation(PeerUserId);
}

void UMessengerWidgetBase::CloseConversation(int64 PeerUserId)
{
	if (TObjectPtr<UDmWindowWidget>* Found = Windows.Find(PeerUserId))
	{
		if (*Found != nullptr)
		{
			(*Found)->RemoveFromParent();
		}
		Windows.Remove(PeerUserId);
	}

	WindowOrder.Remove(PeerUserId);
}

void UMessengerWidgetBase::HandleConversationRequested(int64 PeerUserId)
{
	OpenConversation(PeerUserId);
}

void UMessengerWidgetBase::HandleWindowCloseRequested(int64 PeerUserId)
{
	CloseConversation(PeerUserId);
}

void UMessengerWidgetBase::HandleDirectMessageReceived(const FMOUDirectMessage& Message)
{
	// ★ 창이 없으면 아무것도 하지 않는다. 자동으로 열지 않는 이유는 헤더 ★★.
	//   안 읽음 배지는 서브시스템이 이미 올렸고 친구 목록이 그것을 그린다.
	TObjectPtr<UDmWindowWidget>* Found = Windows.Find(Message.PeerUserId);
	if (Found == nullptr || *Found == nullptr)
	{
		return;
	}

	(*Found)->AppendMessage(Message);

	// 창이 열려 있는데 새 메시지가 왔다 = 지금 보고 있다 = 읽은 것이다.
	// 다시 읽음 처리를 태우지 않으면 배지가 다시 올라간 채로 남는다.
	if (!Message.bIsMine)
	{
		if (UServerSubsystem* Chat = GetServerSubsystem())
		{
			Chat->OpenConversation(Message.PeerUserId);
		}
	}
}

void UMessengerWidgetBase::HandleDmHistoryReceived(int64 PeerUserId,
	const TArray<FMOUDirectMessage>& Messages, bool bHasMore)
{
	TObjectPtr<UDmWindowWidget>* Found = Windows.Find(PeerUserId);
	if (Found == nullptr || *Found == nullptr)
	{
		return;   // 기록이 왔는데 창이 이미 닫혔다
	}

	// 앞에 붙일지 갈아끼울지는 창이 스스로 안다(bAwaitingOlder).
	// 여기서 판단하면 부모가 창마다 그 상태를 또 들고 있어야 한다.
	(*Found)->SetHistory(Messages, bHasMore);
}

void UMessengerWidgetBase::HandleFriendUpdated(const FMOUFriend& Friend, bool bRemoved)
{
	// 친구가 아니게 되면 대화창을 닫는다. 열어둬도 서버가 전송을 거부하므로
	// 창만 남아 "보냈는데 안 간다" 가 된다.
	if (bRemoved)
	{
		CloseConversation(Friend.UserId);
	}
}
