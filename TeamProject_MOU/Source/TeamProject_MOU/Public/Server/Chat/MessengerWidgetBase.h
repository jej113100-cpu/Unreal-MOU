// MOU 메신저 - 대화창 + 친구목록 통합 패널 (v7 M9).
//
// [이 위젯이 하는 일]
//   롤 클라이언트의 커뮤니티 패널 + 대화창을 합친 것이다. 로비 화면에 상주하며
//   친구 목록(UFriendListWidgetBase)을 품고, 줄에서 "메시지" 를 누르면
//   대화창(UDmWindowWidget)을 연다.
//
//     ┌──────────────┬──────────────────┐
//     │ 대화창        │  친구 목록        │
//     │ (0~N개)      │  (항상 표시)      │
//     └──────────────┴──────────────────┘
//
// [★ 대화창을 여러 개 열 수 있다]
//   롤처럼 여러 사람과 동시에 대화할 수 있어야 한다. 그래서 대화창은
//   PeerUserId 로 구분해 TMap 에 보관한다. 같은 사람을 두 번 누르면
//   새로 만들지 않고 기존 창을 앞으로 가져온다.
//
// [★★ 대화창이 닫혀 있어도 메시지는 받는다]
//   OnDirectMessageReceived 는 이 위젯이 받는다. 창이 열려 있으면 그쪽에
//   넘기고, 없으면 **아무것도 하지 않는다** - 안 읽음 배지는 서브시스템이
//   이미 올렸고 친구 목록이 그것을 그린다.
//
//   창을 자동으로 열지 않는 이유: 게임 중에 갑자기 창이 뜨면 방해가 된다.
//   배지로 알리고 열지 말지는 사용자가 정한다.
//
// [읽음 처리가 일어나는 시점]
//   OpenConversation() 을 부를 때다. 즉 **대화창을 여는 순간**이고,
//   그때 서버가 read_at 을 찍고 배지가 사라진다(CHAT_DESIGN.md 6-2절).
//   이미 열려 있는 창에 메시지가 오면 다시 읽음 처리를 해야 하므로,
//   HandleDirectMessageReceived 에서 열린 창이 있으면 다시 부른다.
//
// [WBP 로 갈아끼우려면]
//   WBP 없이 CreateWidget 만 해도 C++ 이 기본 레이아웃을 조립한다.
//     UMessengerWidgetBase : MessengerRoot / ConversationArea / FriendPanelSlot
//     UDmWindowWidget      : DmTitleText / DmScrollBox / DmMessageBox /
//                            DmInputBox / DmSendButton / DmCloseButton / DmMoreButton
//
// [대응하는 문서]
//   CHAT_DESIGN.md 6절(메신저), 10절(UI 설계)

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Server/Social/FriendTypes.h"
#include "MessengerWidgetBase.generated.h"

class UButton;
class UServerSubsystem;
class UDmWindowWidget;
class UEditableTextBox;
class UFriendListWidgetBase;
class UHorizontalBox;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

/** 대화창이 스스로 닫히겠다고 알린다. 부모가 목록에서 지운다. */
DECLARE_DELEGATE_OneParam(FOnDmWindowCloseRequested, int64 /*PeerUserId*/);

/**
 * 한 사람과의 대화창.
 *
 * ★ 자기 수명을 스스로 정하지 않는다. 닫기 버튼은 부모에게 알리기만 하고,
 *   실제로 지우는 것은 부모다 - 창이 스스로를 파괴하면 부모의 TMap 에
 *   죽은 포인터가 남는다.
 */
UCLASS()
class TEAMPROJECT_MOU_API UDmWindowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UDmWindowWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	/** 이 창이 누구와의 대화인지 정한다. 창을 만든 직후 한 번 부른다. */
	void SetPeer(int64 InPeerUserId, const FString& InPeerNickname);

	int64 GetPeerUserId() const { return PeerUserId; }

	/** 메시지 한 통을 아래에 붙인다. */
	void AppendMessage(const FMOUDirectMessage& Message);

	/**
	 * 기록을 채운다.
	 *
	 * ★★ "처음 연 것" 인지 "위로 스크롤한 것" 인지는 **이 창이 스스로 안다.**
	 *   서버 응답(DmHistoryAck)에는 그 구분이 없다 - 요청한 쪽만 아는 정보라
	 *   bAwaitingOlder 플래그로 기억해 둔다. 부모가 판단하게 만들면 부모가
	 *   창마다 그 상태를 다시 들고 있어야 해서 같은 것이 두 군데 생긴다.
	 */
	void SetHistory(const TArray<FMOUDirectMessage>& Messages, bool bHasMore);

	/** 닫기 요청. 부모가 받는다. */
	FOnDmWindowCloseRequested OnCloseRequested;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Messenger")
	TObjectPtr<UTextBlock> DmTitleText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Messenger")
	TObjectPtr<UScrollBox> DmScrollBox;

	/** 말풍선들이 쌓이는 곳 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Messenger")
	TObjectPtr<UVerticalBox> DmMessageBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Messenger")
	TObjectPtr<UEditableTextBox> DmInputBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Messenger")
	TObjectPtr<UButton> DmSendButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Messenger")
	TObjectPtr<UButton> DmCloseButton;

	/** "이전 대화 더 보기". 더 받을 것이 없으면 숨긴다 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Messenger")
	TObjectPtr<UButton> DmMoreButton;

private:
	void BuildDefaultLayout();

	/** 말풍선 한 줄을 만들어 넣는다. bPrepend 면 맨 위에. */
	void InsertBubble(const FMOUDirectMessage& Message, bool bPrepend);

	UServerSubsystem* GetServerSubsystem() const;

	UFUNCTION()
	void HandleSendClicked();

	UFUNCTION()
	void HandleCloseClicked();

	UFUNCTION()
	void HandleMoreClicked();

	UFUNCTION()
	void HandleInputCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	int64   PeerUserId = 0;
	FString PeerNickname;

	/**
	 * 지금 화면에 있는 가장 오래된 메시지의 번호. 위로 스크롤할 때 커서로 쓴다.
	 *
	 * ★ 0 이면 안 된다. 0 을 서버에 보내면 "최신 페이지" 로 해석되어
	 *   읽음 처리까지 일어난다(UServerSubsystem::LoadOlderMessages 가 막아준다).
	 */
	int64 OldestMessageId = 0;

	/**
	 * "이전 대화 더 보기" 를 눌러 응답을 기다리는 중인가.
	 *
	 * ★ 이 플래그가 곧 "다음에 올 기록을 앞에 붙일지 갈아끼울지" 를 정한다.
	 *   SetHistory 가 소비하고 false 로 되돌린다.
	 */
	bool bAwaitingOlder = false;
};

/**
 * 메신저 전체. 친구 목록 + 대화창들을 품는다.
 *
 * 로비(ULobbyWidgetBase) 위에 얹어 쓴다. 방에 들어가든 말든 계속 떠 있어야
 * 하므로 로비의 상태 전환과 무관하게 동작한다.
 */
UCLASS()
class TEAMPROJECT_MOU_API UMessengerWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	UMessengerWidgetBase(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** 그 사람과의 대화창을 연다. 이미 있으면 앞으로 가져온다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Messenger")
	void OpenConversation(int64 PeerUserId);

	/** 대화창을 닫는다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Messenger")
	void CloseConversation(int64 PeerUserId);

	/** 지금 열려 있는 대화창 수. 테스트/디버그용. */
	UFUNCTION(BlueprintPure, Category = "MOU|Messenger")
	int32 GetOpenConversationCount() const { return Windows.Num(); }

protected:
	/** 대화창들이 놓이는 곳 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Messenger")
	TObjectPtr<UHorizontalBox> ConversationArea;

	/** 친구 목록이 들어가는 자리 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Messenger")
	TObjectPtr<UVerticalBox> FriendPanelSlot;

	/** 실제 친구 목록 위젯. WBP 로 갈아끼울 수 있다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Messenger")
	TSubclassOf<UFriendListWidgetBase> FriendListClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Messenger")
	TSubclassOf<UDmWindowWidget> DmWindowClass;

	/**
	 * 동시에 열 수 있는 대화창 수.
	 *
	 * ★ 상한이 필요한 이유: 친구가 많으면 창이 화면을 다 덮는다. 넘으면
	 *   가장 오래 안 쓴 창을 닫는다(브라우저 탭과 같은 방식).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Messenger")
	int32 MaxOpenConversations = 3;

private:
	void BuildDefaultLayout();

	UServerSubsystem* GetServerSubsystem() const;

	/** 친구 목록에서 "메시지" 를 눌렀을 때. */
	void HandleConversationRequested(int64 PeerUserId);

	/** 대화창이 닫기를 요청했을 때. */
	void HandleWindowCloseRequested(int64 PeerUserId);

	UFUNCTION()
	void HandleDirectMessageReceived(const FMOUDirectMessage& Message);

	UFUNCTION()
	void HandleDmHistoryReceived(int64 PeerUserId, const TArray<FMOUDirectMessage>& Messages, bool bHasMore);

	UFUNCTION()
	void HandleFriendUpdated(const FMOUFriend& Friend, bool bRemoved);

	/** 캐시에서 닉네임을 찾는다. 없으면 "(알 수 없음)". */
	FString ResolveNickname(int64 UserId) const;

	UPROPERTY()
	TObjectPtr<UFriendListWidgetBase> FriendList;

	/** PeerUserId -> 대화창. 같은 사람의 창이 둘 생기지 않게 한다 */
	UPROPERTY()
	TMap<int64, TObjectPtr<UDmWindowWidget>> Windows;

	/**
	 * 창을 연 순서. 상한을 넘었을 때 가장 오래된 것을 닫는 데 쓴다.
	 * TMap 은 순서를 보장하지 않으므로 따로 들고 있어야 한다.
	 */
	TArray<int64> WindowOrder;
};
