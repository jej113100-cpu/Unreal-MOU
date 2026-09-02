// MOU 친구 시스템 - 친구 목록 패널 (v7 M8).
//
// [이 위젯이 하는 일]
//   롤 클라이언트의 "커뮤니티" 패널에 해당한다. 로비 화면 오른쪽에 상주하며
//   친구/신청을 한 줄씩 보여주고, 줄마다 상황에 맞는 버튼을 붙인다.
//
//     받은 신청  ● kimta        [수락] [거절]   ← 행동이 필요하므로 항상 맨 위
//     온라인     ● averty       [메시지] [삭제]
//     게임중     ● 칼바람식k    [메시지] [삭제]
//     보낸 신청  ○ hazzys       [취소]          ← 회색, 대기 중
//     오프라인   ○ NormalOne    [메시지] [삭제]
//
// [★ 목록을 다시 요청하지 않는다 — 델타로만 갱신한다]
//   UServerSubsystem 이 주는 신호는 세 가지고, 각자 역할이 다르다:
//
//     OnFriendListReceived    로그인 직후 한 번. 통째로 다시 그린다
//     OnFriendUpdated         한 줄 추가/수정/삭제
//     OnFriendPresenceChanged 한 줄의 상태만 (가장 자주 온다)
//
//   상태가 바뀔 때마다 목록 전체를 다시 받으면 친구 93명이면 4KB 를 매번
//   주고받게 된다. 그래서 서버가 델타만 보내고, 이 위젯이 그것을 반영한다.
//
// [★ 위젯이 늦게 만들어졌을 때]
//   OnFriendListReceived 는 로그인 직후 한 번만 온다. 로비를 닫았다 다시 열면
//   이 위젯은 새로 만들어지므로 그 신호를 놓친다. 그래서 NativeConstruct 에서
//   UServerSubsystem::GetFriends() 로 **캐시를 먼저 읽어** 채운다.
//   이게 없으면 로비를 다시 열 때마다 목록이 빈 채로 시작한다.
//
// [정렬]
//   받은 신청 -> 온라인 -> 게임중 -> 보낸 신청 -> 오프라인, 그 안에서 닉네임순.
//   받은 신청이 맨 위인 이유는 **행동이 필요한 것이 먼저** 보여야 하기 때문이고,
//   온라인이 게임중보다 위인 이유는 지금 말을 걸 수 있는 사람이 먼저이기 때문이다.
//
// [WBP 로 갈아끼우려면]
//   WBP 없이 CreateWidget 만 해도 C++ 이 기본 레이아웃을 조립한다.
//   같은 이름의 위젯을 배치하면 디자인만 바뀐다.
//     UFriendListWidgetBase : FriendListBox / FriendScrollBox / AddFriendBox /
//                             AddFriendButton / StatusText / TitleText
//     UFriendEntryWidget    : EntryNameText / EntryStatusText / EntryUnreadText /
//                             EntryPrimaryButton / EntrySecondaryButton
//
// [대응하는 문서]
//   CHAT_DESIGN.md 4절(친구), 10절(UI 설계)

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TimerHandle.h"   // 상태 문구 자동 삭제 타이머
#include "Server/Social/FriendTypes.h"
#include "FriendListWidgetBase.generated.h"

class UButton;
class UServerSubsystem;
class UEditableTextBox;
class UFriendEntryWidget;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

/** 줄 하나에서 어떤 버튼을 눌렀는지. 부모가 이걸 보고 무엇을 할지 정한다. */
UENUM()
enum class EFriendEntryAction : uint8
{
	/** 대화창 열기 (친구일 때) */
	Message,
	/** 받은 신청 수락 */
	Accept,
	/** 받은 신청 거절 */
	Decline,
	/** 친구 삭제, 또는 내가 보낸 신청 취소 */
	Remove,
};

DECLARE_DELEGATE_TwoParams(FOnFriendEntryAction, int64 /*UserId*/, EFriendEntryAction /*Action*/);

/**
 * "이 사람과 대화창을 열어달라" 는 요청.
 *
 * ★ UCLASS 바깥에 둔다. DECLARE_DELEGATE 를 클래스 본문 안에 쓰면 UHT 가
 *   중첩 클래스 정의로 만나 파싱이 흔들릴 수 있다 - 리플렉션 대상이 아닌
 *   순수 델리게이트는 항상 파일 스코프에 둔다.
 */
DECLARE_DELEGATE_OneParam(FOnConversationRequested, int64 /*PeerUserId*/);

/**
 * 친구 목록의 한 줄.
 *
 * ★ 자기가 직접 서브시스템을 부르지 않는다. 무엇을 눌렀는지만 부모에게 알린다 —
 *   줄마다 서브시스템을 만지면 "누가 목록을 갱신하는가" 가 흩어져서,
 *   같은 줄이 두 경로로 갱신되는 상황이 생긴다.
 */
UCLASS()
class TEAMPROJECT_MOU_API UFriendEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFriendEntryWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	/** 이 줄이 표시할 내용을 채운다. 상태에 따라 버튼 구성이 바뀐다. */
	void SetFriend(const FMOUFriend& InFriend);

	/** 지금 표시 중인 상대의 UserId. */
	int64 GetUserId() const { return Cached.UserId; }

	/** 버튼을 눌렀을 때 부모가 받는다. */
	FOnFriendEntryAction OnAction;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Friend")
	TObjectPtr<UTextBlock> EntryNameText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Friend")
	TObjectPtr<UTextBlock> EntryStatusText;

	/** 안 읽음 배지. 0 이면 숨긴다 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Friend")
	TObjectPtr<UTextBlock> EntryUnreadText;

	/** 상황에 따라 [메시지] 또는 [수락] */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Friend")
	TObjectPtr<UButton> EntryPrimaryButton;

	/** 상황에 따라 [삭제] / [거절] / [취소] */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Friend")
	TObjectPtr<UButton> EntrySecondaryButton;

private:
	void BuildDefaultLayout();
	void RefreshVisuals();

	UFUNCTION()
	void HandlePrimaryClicked();

	UFUNCTION()
	void HandleSecondaryClicked();

	/** 버튼 안의 라벨. 텍스트를 바꾸려면 이걸 들고 있어야 한다 */
	UPROPERTY()
	TObjectPtr<UTextBlock> PrimaryLabel;

	UPROPERTY()
	TObjectPtr<UTextBlock> SecondaryLabel;

	FMOUFriend Cached;
};

/**
 * 친구 목록 패널 전체.
 *
 * 로비 화면에 상주한다. 방에 들어가든 말든 계속 떠 있어야 하므로
 * ULobbyWidgetBase 의 상태 전환(메인메뉴 <-> 대기실)과 무관하게 동작한다.
 */
UCLASS()
class TEAMPROJECT_MOU_API UFriendListWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	UFriendListWidgetBase(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/**
	 * 어떤 친구의 대화창을 열어달라는 요청. 메신저 위젯이 여기 바인딩한다.
	 *
	 * ★ 이 위젯이 대화창을 직접 만들지 않는 이유: 대화창은 여러 개가 뜰 수
	 *   있고 그 배치는 메신저 전체가 관리한다. 목록은 "누굴 눌렀는지" 만 알린다.
	 */
	FOnConversationRequested OnConversationRequested;

	/** 지금 표시 중인 줄 수. 테스트/디버그용. */
	UFUNCTION(BlueprintPure, Category = "MOU|Friend")
	int32 GetEntryCount() const;

protected:
	/** 줄들이 쌓이는 곳 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Friend")
	TObjectPtr<UVerticalBox> FriendListBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Friend")
	TObjectPtr<UScrollBox> FriendScrollBox;

	/** 친구 추가 입력창 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Friend")
	TObjectPtr<UEditableTextBox> AddFriendBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Friend")
	TObjectPtr<UButton> AddFriendButton;

	/** 결과/오류 표시 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Friend")
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Friend")
	TObjectPtr<UTextBlock> TitleText;

	/** 한 줄에 쓸 위젯 클래스. WBP 로 갈아끼울 수 있다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Friend")
	TSubclassOf<UFriendEntryWidget> EntryWidgetClass;

	/**
	 * 패널의 **고정 폭**(픽셀).
	 *
	 * ★★ 고정하지 않으면 패널이 내용물 크기를 따라간다. 제목이
	 *   "커뮤니티 (0/0)" 에서 "커뮤니티 (12/93)" 으로 바뀌는 것만으로 폭이 변해
	 *   **로비 화면 전체가 밀린다.** 친구가 접속할 때마다 UI 가 흔들리는 셈이라
	 *   가장 눈에 거슬리는 종류다.
	 *
	 *   그래서 SizeBox 로 폭을 못 박고, 넘치는 글자는 잘라낸다(말줄임).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Friend", meta = (ClampMin = "120"))
	float PanelWidth = 280.f;

	/**
	 * 상태 문구가 저절로 사라지기까지의 시간(초). 0 이하면 안 지운다.
	 *
	 * ★ "친구 신청을 보냈습니다." 가 화면에 영원히 남아 있으면, 다음에 무슨
	 *   일이 일어났는지와 구분이 안 된다 — 방금 보낸 건지 아까 보낸 건지
	 *   알 수 없어서 정보가 아니라 잡음이 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Friend", meta = (ClampMin = "0.0"))
	float StatusClearSeconds = 3.0f;

private:
	void BuildDefaultLayout();

	/** 캐시를 정렬해 줄을 다시 만든다. */
	void RebuildList();

	/** 이 위젯이 쓰는 서브시스템. 없으면 nullptr(단독 실행/테스트). */
	UServerSubsystem* GetServerSubsystem() const;

	/**
	 * 상태 문구를 띄운다. StatusClearSeconds 뒤에 저절로 사라진다.
	 *
	 * ★ 새 문구가 오면 이전 타이머를 **반드시 취소하고** 다시 건다.
	 *   안 그러면 먼저 걸린 타이머가 나중 문구를 지워버린다.
	 */
	void SetStatus(const FString& Text, bool bIsError);

	/** 타이머가 부른다. 문구를 비운다. */
	void ClearStatus();

	// --- 서브시스템 델리게이트 ---
	UFUNCTION()
	void HandleFriendListReceived(const TArray<FMOUFriend>& InFriends);

	UFUNCTION()
	void HandleFriendUpdated(const FMOUFriend& InFriend, bool bRemoved);

	UFUNCTION()
	void HandleFriendPresenceChanged(int64 UserId, EMOUPresenceBP Presence);

	UFUNCTION()
	void HandleFriendAddCompleted(bool bSuccess, EMOUFriendResultBP Result);

	UFUNCTION()
	void HandleDirectMessageReceived(const FMOUDirectMessage& Message);

	UFUNCTION()
	void HandleAddFriendClicked();

	/** 줄에서 버튼을 눌렀을 때. 여기서만 서브시스템을 부른다. */
	void HandleEntryAction(int64 UserId, EFriendEntryAction Action);

	/** 화면에 그려진 줄들. 캐시(서브시스템)와 별개로 위젯 수명만 관리한다 */
	UPROPERTY()
	TArray<TObjectPtr<UFriendEntryWidget>> EntryWidgets;

	/** 상태 문구 자동 삭제 타이머. NativeDestruct 에서 반드시 해제한다 */
	FTimerHandle StatusClearTimer;
};
