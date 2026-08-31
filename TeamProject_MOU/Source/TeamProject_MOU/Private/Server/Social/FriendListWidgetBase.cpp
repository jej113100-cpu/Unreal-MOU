// MOU 친구 시스템 - 친구 목록 패널 구현 (v7 M8).
// 대응하는 설계 문서: CHAT_DESIGN.md 4절, 10절

#include "Server/Social/FriendListWidgetBase.h"

#include "Server/ServerSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
	/** 상태별 색. 롤 클라이언트처럼 초록/노랑/회색으로 구분한다. */
	FLinearColor PresenceColor(EMOUPresenceBP Presence)
	{
		switch (Presence)
		{
		case EMOUPresenceBP::Online: return FLinearColor(0.35f, 0.85f, 0.40f);
		case EMOUPresenceBP::InGame: return FLinearColor(0.95f, 0.75f, 0.25f);
		default:                     return FLinearColor(0.45f, 0.45f, 0.45f);
		}
	}

	FString PresenceLabel(EMOUPresenceBP Presence)
	{
		switch (Presence)
		{
		case EMOUPresenceBP::Online: return TEXT("온라인");
		case EMOUPresenceBP::InGame: return TEXT("게임중");
		default:                     return TEXT("오프라인");
		}
	}

	/**
	 * 정렬 순위. 낮을수록 위에 온다.
	 *
	 * ★ 받은 신청이 맨 위인 이유: **행동이 필요한 것이 먼저** 보여야 한다.
	 * ★ 온라인이 게임중보다 위인 이유: 지금 말을 걸 수 있는 사람이 먼저다.
	 */
	int32 SortRank(const FMOUFriend& F)
	{
		if (F.State == EMOUFriendStateBP::PendingIncoming) { return 0; }
		if (F.State == EMOUFriendStateBP::PendingOutgoing) { return 3; }

		switch (F.Presence)
		{
		case EMOUPresenceBP::Online: return 1;
		case EMOUPresenceBP::InGame: return 2;
		default:                     return 4;
		}
	}

	/** 사람이 읽을 실패 사유. UI 문구는 여기서만 정한다(로그 문구와 분리). */
	FString FriendResultText(EMOUFriendResultBP Result)
	{
		switch (Result)
		{
		case EMOUFriendResultBP::Success:        return TEXT("");
		case EMOUFriendResultBP::NotAuthed:      return TEXT("로그인이 필요합니다.");
		case EMOUFriendResultBP::NotFound:       return TEXT("그런 닉네임이 없습니다.");
		// ★ 지금은 사용자가 해결할 방법이 없다. #태그가 들어오면 사라질 오류다.
		case EMOUFriendResultBP::AmbiguousName:  return TEXT("같은 닉네임이 여럿입니다.");
		case EMOUFriendResultBP::AlreadyFriend:  return TEXT("이미 친구입니다.");
		case EMOUFriendResultBP::AlreadyPending: return TEXT("이미 신청했습니다.");
		case EMOUFriendResultBP::SelfRequest:    return TEXT("자기 자신에게는 신청할 수 없습니다.");
		case EMOUFriendResultBP::LimitReached:   return TEXT("친구 수가 가득 찼습니다.");
		case EMOUFriendResultBP::InvalidFormat:  return TEXT("닉네임 형식이 올바르지 않습니다.");
		default:                                 return TEXT("서버 오류로 실패했습니다.");
		}
	}
}

// ===========================================================================
// UFriendEntryWidget - 한 줄
// ===========================================================================

UFriendEntryWidget::UFriendEntryWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UFriendEntryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// WBP 가 없으면 C++ 이 조립한다(URoomListEntryWidget 과 같은 규약).
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
	}
}

void UFriendEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (EntryPrimaryButton != nullptr)
	{
		EntryPrimaryButton->OnClicked.AddUniqueDynamic(this, &UFriendEntryWidget::HandlePrimaryClicked);
	}
	if (EntrySecondaryButton != nullptr)
	{
		EntrySecondaryButton->OnClicked.AddUniqueDynamic(this, &UFriendEntryWidget::HandleSecondaryClicked);
	}
}

// 한 줄의 기본 모양:
//   HorizontalBox
//     - EntryNameText          닉네임 (Fill)
//     - EntryStatusText        온라인/게임중/오프라인 (색으로도 구분)
//     - EntryUnreadText        [3]  안 읽음 배지
//     - EntryPrimaryButton     [메시지] 또는 [수락]
//     - EntrySecondaryButton   [삭제] / [거절] / [취소]
void UFriendEntryWidget::BuildDefaultLayout()
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FriendEntryRow"));
	WidgetTree->RootWidget = Row;

	auto AddCell = [&](UWidget* Widget, ESlateSizeRule::Type Rule, float RightPadding)
	{
		if (UHorizontalBoxSlot* BoxSlot = Row->AddChildToHorizontalBox(Widget))
		{
			BoxSlot->SetSize(FSlateChildSize(Rule));
			BoxSlot->SetVerticalAlignment(VAlign_Center);
			BoxSlot->SetPadding(FMargin(0.f, 0.f, RightPadding, 0.f));
		}
	};

	EntryNameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EntryNameText"));
	// ★ 닉네임은 32바이트까지 들어올 수 있다. 패널 폭이 고정돼 있으므로
	//   길면 잘라낸다 - 안 그러면 이 줄만 옆으로 삐져나간다.
	EntryNameText->SetClipping(EWidgetClipping::ClipToBounds);
	AddCell(EntryNameText, ESlateSizeRule::Fill, 6.f);

	EntryStatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EntryStatusText"));
	AddCell(EntryStatusText, ESlateSizeRule::Automatic, 6.f);

	EntryUnreadText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EntryUnreadText"));
	EntryUnreadText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.85f, 0.3f)));
	AddCell(EntryUnreadText, ESlateSizeRule::Automatic, 6.f);

	// 버튼은 라벨을 바꿔야 하므로 안쪽 TextBlock 을 따로 들고 있는다.
	auto MakeButton = [&](const TCHAR* Name, const TCHAR* LabelName, TObjectPtr<UTextBlock>& OutLabel) -> UButton*
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), LabelName);
		Button->AddChild(Label);
		OutLabel = Label;
		return Button;
	};

	EntryPrimaryButton = MakeButton(TEXT("EntryPrimaryButton"), TEXT("PrimaryLabel"), PrimaryLabel);
	AddCell(EntryPrimaryButton, ESlateSizeRule::Automatic, 4.f);

	EntrySecondaryButton = MakeButton(TEXT("EntrySecondaryButton"), TEXT("SecondaryLabel"), SecondaryLabel);
	AddCell(EntrySecondaryButton, ESlateSizeRule::Automatic, 0.f);
}

void UFriendEntryWidget::SetFriend(const FMOUFriend& InFriend)
{
	Cached = InFriend;
	RefreshVisuals();
}

void UFriendEntryWidget::RefreshVisuals()
{
	if (EntryNameText != nullptr)
	{
		EntryNameText->SetText(FText::FromString(Cached.Nickname));
	}

	if (EntryStatusText != nullptr)
	{
		// ★ 대기 중인 신청은 접속 상태를 보여주지 않는다 - 서버가 알려주지도 않는다
		//   (신청만 걸어두고 남의 온/오프라인을 훔쳐보는 것을 막는다).
		const bool bPending = (Cached.State != EMOUFriendStateBP::Friend);
		const FString Label = bPending
			? (Cached.State == EMOUFriendStateBP::PendingIncoming ? TEXT("신청받음") : TEXT("대기 중"))
			: PresenceLabel(Cached.Presence);

		EntryStatusText->SetText(FText::FromString(Label));
		EntryStatusText->SetColorAndOpacity(FSlateColor(
			bPending ? FLinearColor(0.6f, 0.7f, 1.f) : PresenceColor(Cached.Presence)));
	}

	if (EntryUnreadText != nullptr)
	{
		// 0 이면 아예 감춘다. "[0]" 이 남으면 읽었는지 아닌지 헷갈린다.
		EntryUnreadText->SetText(Cached.UnreadCount > 0
			? FText::FromString(FString::Printf(TEXT("[%d]"), Cached.UnreadCount))
			: FText::GetEmpty());
	}

	// --- 상태별 버튼 구성 ---
	//
	// ★ 버튼을 새로 만들지 않고 라벨과 표시 여부만 바꾼다. 매번 다시 만들면
	//   델리게이트를 다시 걸어야 하고 중복 바인딩이 생기기 쉽다.
	const bool bIncoming = (Cached.State == EMOUFriendStateBP::PendingIncoming);
	const bool bOutgoing = (Cached.State == EMOUFriendStateBP::PendingOutgoing);

	if (PrimaryLabel != nullptr)
	{
		PrimaryLabel->SetText(FText::FromString(bIncoming ? TEXT("수락") : TEXT("메시지")));
	}
	if (SecondaryLabel != nullptr)
	{
		SecondaryLabel->SetText(FText::FromString(
			bIncoming ? TEXT("거절") : (bOutgoing ? TEXT("취소") : TEXT("삭제"))));
	}

	// 보낸 신청에는 "메시지" 가 의미 없다. 아직 친구가 아니라 서버가 거부한다.
	if (EntryPrimaryButton != nullptr)
	{
		EntryPrimaryButton->SetVisibility(bOutgoing ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
}

void UFriendEntryWidget::HandlePrimaryClicked()
{
	OnAction.ExecuteIfBound(Cached.UserId,
		Cached.State == EMOUFriendStateBP::PendingIncoming
			? EFriendEntryAction::Accept
			: EFriendEntryAction::Message);
}

void UFriendEntryWidget::HandleSecondaryClicked()
{
	// ★ 거절과 삭제는 서버에서 다른 요청이다(FriendRespondReq vs FriendRemoveReq).
	//   받은 신청을 Remove 로 처리하면 상대에게 알림이 가지 않는다.
	OnAction.ExecuteIfBound(Cached.UserId,
		Cached.State == EMOUFriendStateBP::PendingIncoming
			? EFriendEntryAction::Decline
			: EFriendEntryAction::Remove);
}

// ===========================================================================
// UFriendListWidgetBase - 패널
// ===========================================================================

UFriendListWidgetBase::UFriendListWidgetBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	EntryWidgetClass = UFriendEntryWidget::StaticClass();
}

void UFriendListWidgetBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
	}
}

void UFriendListWidgetBase::BuildDefaultLayout()
{
	// ★★ 폭을 못 박는다. 이게 없으면 패널이 내용물을 따라 커졌다 작아져서,
	//   제목이 "(0/0)" 에서 "(12/93)" 으로 바뀌는 것만으로 로비 화면이 밀린다
	//   (헤더 PanelWidth 주석). 높이는 부모가 정하게 두므로 폭만 지정한다.
	USizeBox* SizeRoot = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FriendSizeRoot"));
	SizeRoot->SetWidthOverride(PanelWidth);
	WidgetTree->RootWidget = SizeRoot;

	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FriendRoot"));
	SizeRoot->AddChild(Root);

	auto AddRow = [&](UWidget* Widget, ESlateSizeRule::Type Rule, float BottomPadding)
	{
		if (UVerticalBoxSlot* BoxSlot = Root->AddChildToVerticalBox(Widget))
		{
			BoxSlot->SetSize(FSlateChildSize(Rule));
			BoxSlot->SetPadding(FMargin(0.f, 0.f, 0.f, BottomPadding));
		}
	};

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	TitleText->SetText(FText::FromString(TEXT("커뮤니티")));
	// 폭이 고정됐으므로 넘치는 글자는 잘라낸다. 안 자르면 SizeBox 를 뚫고 나간다.
	TitleText->SetClipping(EWidgetClipping::ClipToBounds);
	AddRow(TitleText, ESlateSizeRule::Automatic, 4.f);

	// 친구 추가 줄: [입력창][추가]
	UHorizontalBox* AddRowBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("AddRow"));

	AddFriendBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("AddFriendBox"));
	AddFriendBox->SetHintText(FText::FromString(TEXT("닉네임으로 친구 추가")));
	if (UHorizontalBoxSlot* BoxSlot = AddRowBox->AddChildToHorizontalBox(AddFriendBox))
	{
		BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BoxSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
	}

	AddFriendButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("AddFriendButton"));
	UTextBlock* AddLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AddFriendLabel"));
	AddLabel->SetText(FText::FromString(TEXT("추가")));
	AddFriendButton->AddChild(AddLabel);
	if (UHorizontalBoxSlot* BoxSlot = AddRowBox->AddChildToHorizontalBox(AddFriendButton))
	{
		BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}
	AddRow(AddRowBox, ESlateSizeRule::Automatic, 4.f);

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	// 오류 문구가 길 수 있다. 폭을 넘기지 말고 줄바꿈으로 흘린다.
	StatusText->SetAutoWrapText(true);
	AddRow(StatusText, ESlateSizeRule::Automatic, 4.f);

	// 목록은 스크롤 안에 둔다. 최대 93명까지 들어갈 수 있다.
	FriendScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("FriendScrollBox"));
	FriendListBox   = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FriendListBox"));
	FriendScrollBox->AddChild(FriendListBox);
	AddRow(FriendScrollBox, ESlateSizeRule::Fill, 0.f);
}

UServerSubsystem* UFriendListWidgetBase::GetServerSubsystem() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UServerSubsystem>() : nullptr;
}

void UFriendListWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	if (AddFriendButton != nullptr)
	{
		AddFriendButton->OnClicked.AddUniqueDynamic(this, &UFriendListWidgetBase::HandleAddFriendClicked);
	}

	UServerSubsystem* Chat = GetServerSubsystem();
	if (Chat == nullptr)
	{
		SetStatus(TEXT("채팅 서브시스템을 찾지 못했습니다."), /*bIsError=*/true);
		return;
	}

	Chat->OnFriendListReceived.AddUniqueDynamic(this, &UFriendListWidgetBase::HandleFriendListReceived);
	Chat->OnFriendUpdated.AddUniqueDynamic(this, &UFriendListWidgetBase::HandleFriendUpdated);
	Chat->OnFriendPresenceChanged.AddUniqueDynamic(this, &UFriendListWidgetBase::HandleFriendPresenceChanged);
	Chat->OnFriendAddCompleted.AddUniqueDynamic(this, &UFriendListWidgetBase::HandleFriendAddCompleted);
	Chat->OnDirectMessageReceived.AddUniqueDynamic(this, &UFriendListWidgetBase::HandleDirectMessageReceived);

	// ★ 캐시를 먼저 읽는다. OnFriendListReceived 는 로그인 직후 한 번만 오므로,
	//   로비를 닫았다 다시 열어 이 위젯이 새로 만들어지면 그 신호를 놓친다.
	//   이게 없으면 로비를 다시 열 때마다 목록이 빈 채로 시작한다(헤더 ★).
	RebuildList();
}

void UFriendListWidgetBase::NativeDestruct()
{
	// ★ 타이머도 델리게이트와 같은 이유로 반드시 끈다. 남아 있으면 파괴된
	//   위젯의 ClearStatus 를 부르게 된다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StatusClearTimer);
	}

	// ★ 반드시 해제한다. 서브시스템은 게임 인스턴스 수명이라 이 위젯보다 오래
	//   사는데, 델리게이트가 남아 있으면 파괴된 위젯을 호출하게 된다.
	if (UServerSubsystem* Chat = GetServerSubsystem())
	{
		Chat->OnFriendListReceived.RemoveAll(this);
		Chat->OnFriendUpdated.RemoveAll(this);
		Chat->OnFriendPresenceChanged.RemoveAll(this);
		Chat->OnFriendAddCompleted.RemoveAll(this);
		Chat->OnDirectMessageReceived.RemoveAll(this);
	}

	Super::NativeDestruct();
}

int32 UFriendListWidgetBase::GetEntryCount() const
{
	return EntryWidgets.Num();
}

void UFriendListWidgetBase::SetStatus(const FString& Text, bool bIsError)
{
	if (StatusText == nullptr)
	{
		return;
	}

	StatusText->SetText(FText::FromString(Text));
	StatusText->SetColorAndOpacity(FSlateColor(
		bIsError ? FLinearColor(1.f, 0.45f, 0.4f) : FLinearColor(0.7f, 0.8f, 0.7f)));

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// ★ 이전 타이머를 반드시 먼저 끈다. 안 끄면 먼저 걸린 것이 나중 문구를
	//   지워버린다 — 연달아 신청하면 두 번째 결과가 바로 사라지는 증상이 된다.
	World->GetTimerManager().ClearTimer(StatusClearTimer);

	// 빈 문자열이면 지울 것도 없다. 타이머만 끄고 끝낸다.
	if (Text.IsEmpty() || StatusClearSeconds <= 0.f)
	{
		return;
	}

	World->GetTimerManager().SetTimer(
		StatusClearTimer, this, &UFriendListWidgetBase::ClearStatus,
		StatusClearSeconds, /*bLoop=*/false);
}

void UFriendListWidgetBase::ClearStatus()
{
	if (StatusText != nullptr)
	{
		StatusText->SetText(FText::GetEmpty());
	}
}

void UFriendListWidgetBase::RebuildList()
{
	if (FriendListBox == nullptr)
	{
		return;
	}

	const UServerSubsystem* Chat = GetServerSubsystem();
	if (Chat == nullptr)
	{
		return;
	}

	// ★ 캐시를 복사해서 정렬한다. 서브시스템의 배열을 직접 정렬하면 그쪽이
	//   특정 순서를 전제로 하는 코드를 갖게 될 때 조용히 깨진다.
	TArray<FMOUFriend> Sorted = Chat->GetFriendsRef();

	Sorted.Sort([](const FMOUFriend& A, const FMOUFriend& B)
	{
		const int32 RA = SortRank(A);
		const int32 RB = SortRank(B);
		if (RA != RB)
		{
			return RA < RB;
		}
		// 같은 그룹 안에서는 닉네임순. 대소문자를 구분하면 목록이 들쭉날쭉해진다.
		return A.Nickname.Compare(B.Nickname, ESearchCase::IgnoreCase) < 0;
	});

	// 줄 위젯을 통째로 다시 만든다.
	//
	// ★ 93개가 상한이라 매번 다시 만들어도 부담이 없다. 부분 갱신은 "어느 줄이
	//   어느 친구인지" 를 따로 추적해야 해서 어긋날 여지가 생긴다.
	//   목록이 훨씬 커지면 그때 UListView 로 바꾼다.
	FriendListBox->ClearChildren();
	EntryWidgets.Reset();

	// ★ 삼항 연산자로 쓰면 안 된다. TSubclassOf<T> 와 UClass* 가 서로 변환
	//   가능해서 공용 타입이 모호해진다(C2445). if 로 풀어 쓴다.
	TSubclassOf<UFriendEntryWidget> EntryClass = EntryWidgetClass;
	if (!EntryClass)
	{
		EntryClass = UFriendEntryWidget::StaticClass();
	}

	for (const FMOUFriend& Entry : Sorted)
	{
		UFriendEntryWidget* Row = CreateWidget<UFriendEntryWidget>(GetOwningPlayer(), EntryClass);
		if (Row == nullptr)
		{
			continue;
		}

		Row->SetFriend(Entry);
		Row->OnAction.BindUObject(this, &UFriendListWidgetBase::HandleEntryAction);

		FriendListBox->AddChildToVerticalBox(Row);
		EntryWidgets.Add(Row);
	}

	if (TitleText != nullptr)
	{
		int32 OnlineCount = 0;
		for (const FMOUFriend& Entry : Sorted)
		{
			if (Entry.State == EMOUFriendStateBP::Friend && Entry.bIsOnline)
			{
				++OnlineCount;
			}
		}
		TitleText->SetText(FText::FromString(
			FString::Printf(TEXT("커뮤니티 (%d/%d)"), OnlineCount, Sorted.Num())));
	}
}

void UFriendListWidgetBase::HandleFriendListReceived(const TArray<FMOUFriend>&)
{
	// 인자를 쓰지 않는 이유: 서브시스템이 이미 캐시에 넣어두었고 RebuildList 가
	// 그것을 읽는다. 여기서 인자를 따로 쓰면 두 경로가 생겨 어긋날 수 있다.
	RebuildList();
}

void UFriendListWidgetBase::HandleFriendUpdated(const FMOUFriend&, bool)
{
	RebuildList();
}

void UFriendListWidgetBase::HandleFriendPresenceChanged(int64, EMOUPresenceBP)
{
	// ★ 상태가 바뀌면 정렬 순서도 바뀐다(온라인이 위로 올라온다).
	//   그래서 줄 하나만 고치지 않고 다시 그린다.
	RebuildList();
}

void UFriendListWidgetBase::HandleDirectMessageReceived(const FMOUDirectMessage& Message)
{
	// 안 읽음 배지가 바뀌었을 수 있다. 내가 보낸 것은 배지와 무관하다.
	if (!Message.bIsMine)
	{
		RebuildList();
	}
}

void UFriendListWidgetBase::HandleFriendAddCompleted(bool bSuccess, EMOUFriendResultBP Result)
{
	if (bSuccess)
	{
		SetStatus(TEXT("친구 신청을 보냈습니다."), /*bIsError=*/false);
		if (AddFriendBox != nullptr)
		{
			AddFriendBox->SetText(FText::GetEmpty());
		}
	}
	else
	{
		SetStatus(FriendResultText(Result), /*bIsError=*/true);
	}
}

void UFriendListWidgetBase::HandleAddFriendClicked()
{
	if (AddFriendBox == nullptr)
	{
		return;
	}

	const FString Query = AddFriendBox->GetText().ToString().TrimStartAndEnd();
	if (Query.IsEmpty())
	{
		SetStatus(TEXT("닉네임을 입력하세요."), /*bIsError=*/true);
		return;
	}

	if (UServerSubsystem* Chat = GetServerSubsystem())
	{
		SetStatus(TEXT("신청 중..."), /*bIsError=*/false);
		Chat->AddFriend(Query);
	}
}

void UFriendListWidgetBase::HandleEntryAction(int64 UserId, EFriendEntryAction Action)
{
	UServerSubsystem* Chat = GetServerSubsystem();
	if (Chat == nullptr)
	{
		return;
	}

	switch (Action)
	{
	case EFriendEntryAction::Message:
		// 대화창은 이 위젯이 만들지 않는다. 메신저 전체가 배치를 관리한다(헤더 ★).
		OnConversationRequested.ExecuteIfBound(UserId);
		break;

	case EFriendEntryAction::Accept:
		Chat->AcceptFriendRequest(UserId);
		break;

	case EFriendEntryAction::Decline:
		Chat->DeclineFriendRequest(UserId);
		break;

	case EFriendEntryAction::Remove:
		Chat->RemoveFriend(UserId);
		break;
	}
}
