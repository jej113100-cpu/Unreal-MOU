// MOU 로비 - 방 목록 / 참여 UI.
//
// [이 위젯이 하는 일]
//   콘솔 명령 MOU.Room.List / MOU.Room.Join 으로 하던 일을 화면에서 한다.
//     1. 대기 중인 방 목록을 요청하고 한 줄씩 그린다
//     2. 비밀번호 방을 고르면 숫자 4자리를 입력받는다
//     3. UChatSubsystem::JoinRoom() 을 호출하고 결과를 표시한다
//
// [시스템에서의 위치]
//     ULobbyWidgetBase (메인메뉴)
//       ├─ URoomCreateWidgetBase      방을 "만드는" 쪽
//       └─ URoomListWidgetBase     ← 이 파일. 방에 "들어가는" 쪽
//            └─ URoomListEntryWidget   목록의 한 줄
//   서버와 직접 대화하지 않는다. UChatSubsystem 하고만 대화한다.
//     보낼 때: UChatSubsystem::RequestRoomList() / JoinRoom()
//     받을 때: UChatSubsystem::OnRoomListReceived / OnRoomJoinCompleted
//   대응하는 서버 코드: MOU_Server/Server/Server.cpp 의 RoomListReq/RoomJoinReq 핸들러,
//                       MOU_Server/Server/Rooms.cpp 의 Rooms::ListWaiting() / Rooms::Join()
//
// [이 위젯은 ClientTravel 하지 않는다]
//   참여가 승인되면 호스트 주소를 받지만, 실제로 여행하는 것은 소유자
//   (ULobbyWidgetBase 또는 블루프린트)의 몫이다. 언제 떠날지는 게임 흐름이 정할 일이다.
//
// [비밀번호에 대해]
//   방 비밀번호(숫자 4자리)는 암호학적 장치가 아니라 "아는 사람만 들어오게" 하는 정도다.
//   목록만 보고 곧장 붙지 못하도록 RoomInfo 에는 호스트 주소가 아예 없고,
//   주소는 JoinRoom 이 승인된 뒤에만 내려온다. 진짜 관문은 호스트의
//   AGameModeBase::PreLogin 이며 그쪽은 아직 미구현이다(SERVER_INTEGRATION.md 12절).
//
// [ChatWidgetBase / LoginWidgetBase 와 같은 규약]
//   WBP 없이 CreateWidget 만 해도 C++ 이 기본 레이아웃을 조립한다.
//   WBP 를 만들어 부모로 지정하면 아래 BindWidgetOptional 과 같은 이름의 위젯을
//   배치하는 것만으로 디자인을 갈아끼울 수 있다.
//     URoomListWidgetBase  : RoomListBox / RoomListScrollBox / RefreshButton / CloseButton /
//                            StatusText / TitleText / PasswordPromptPanel /
//                            JoinPasswordBox / JoinConfirmButton / JoinCancelButton
//     URoomListEntryWidget : EntryTitleText / EntryHostText / EntryPlayersText /
//                            EntryLockText / EntryJoinButton

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Chat/LobbyTypes.h"
#include "Engine/TimerHandle.h"
#include "RoomListWidgetBase.generated.h"

class UButton;
class UEditableTextBox;
class UPanelWidget;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UChatSubsystem;

/** 목록의 한 줄에서 "참여" 를 눌렀을 때. 목록 위젯이 받는다. */
DECLARE_DELEGATE_OneParam(FOnRoomEntryJoinClicked, int32 /*RoomId*/);

/**
 * 참여가 승인됐을 때 C++ 소유자에게 알리는 통로 (ULobbyWidgetBase 가 받는다).
 *
 * @param Result       호스트 주소가 담긴 승인 결과. Result.MakeTravelURL() 로 URL 을 만든다
 * @param RoomPassword 사용자가 입력한 방 비밀번호(공개방이면 빈 문자열).
 *                     호스트의 PreLogin 이 검사할 값이라 여행 URL 에 다시 실어야 한다.
 */
DECLARE_DELEGATE_TwoParams(FOnRoomJoinApprovedNative, const FMOURoomJoinResult& /*Result*/, const FString& /*RoomPassword*/);

/** 사용자가 목록을 닫았을 때. */
DECLARE_DELEGATE(FOnRoomListClosed);

/**
 * 방 목록의 한 줄.
 *
 * 별도 위젯 클래스로 뺀 이유: UButton::OnClicked 는 인자를 실을 수 없다.
 * 줄마다 위젯 객체를 하나 두고 그 객체가 자기 RoomId 를 기억하게 하면,
 * 클릭 핸들러가 "어느 방인지" 를 자연스럽게 알게 된다.
 */
UCLASS()
class TEAMPROJECT_MOU_API URoomListEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	/** 이 줄이 보여줄 방. 목록 위젯이 줄을 만든 직후에 호출한다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void SetRoomInfo(const FMOURoomInfo& InRoomInfo);

	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	FMOURoomInfo GetRoomInfo() const { return RoomInfo; }

	/** 이 방에 들어가겠다고 목록 위젯에 알린다. WBP 가 자체 버튼을 쓸 때 호출하면 된다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void RequestJoin();

	/** WBP 가 값을 직접 그리고 싶을 때의 훅. C++ 기본 레이아웃과 함께 써도 된다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "MOU|Lobby")
	void OnRoomInfoSet(const FMOURoomInfo& Room);

	/** 목록 위젯이 바인딩한다. */
	FOnRoomEntryJoinClicked OnJoinClicked;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UTextBlock> EntryTitleText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UTextBlock> EntryHostText;

	/** "2 / 4" */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UTextBlock> EntryPlayersText;

	/** 비밀번호 방 표시. 공개방이면 비워둔다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UTextBlock> EntryLockText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UButton> EntryJoinButton;

private:
	UFUNCTION()
	void HandleJoinClicked();

	void BuildDefaultLayout();
	void RefreshTexts();

	UPROPERTY()
	FMOURoomInfo RoomInfo;
};

/**
 * 대기 중인 방을 나열하고 참여를 처리하는 위젯.
 *
 * 사용 흐름:
 *   1. 로그인이 끝난 뒤 CreateWidget<URoomListWidgetBase>() 후 AddToViewport()
 *   2. 열리면 목록을 자동으로 한 번 요청하고, 이후 주기적으로 새로고침한다
 *   3. 방을 고르면(비밀번호 방이면 4자리 입력 후) 참여를 시도한다
 *   4. 승인되면 OnRoomJoinApproved 가 불린다 — 거기서 ClientTravel 하면 된다
 */
UCLASS()
class TEAMPROJECT_MOU_API URoomListWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	URoomListWidgetBase(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- 설정 -------------------------------------------------------------

	/** 목록의 한 줄로 쓸 위젯. 비워두면 URoomListEntryWidget 의 C++ 기본 레이아웃을 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	TSubclassOf<URoomListEntryWidget> EntryWidgetClass;

	/**
	 * 자동 새로고침 간격(초). 0 이하면 새로고침 버튼으로만 갱신한다.
	 *
	 * 방은 서버 메모리에만 있고 호스트 연결이 끊기면 조용히 사라진다.
	 * 주기적으로 다시 받아오지 않으면 이미 없는 방을 계속 보여주게 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	float AutoRefreshInterval = 3.f;

	/** 참여에 성공하면 이 위젯을 자동으로 화면에서 없앨지. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	bool bRemoveOnSuccess = true;

	/**
	 * 이 위젯이 마우스 커서를 직접 켜고 끌지.
	 * 기본값이 false 인 이유는 URoomCreateWidgetBase 와 같다 — 보통 로비가 이미 켜뒀다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	bool bManageMouseCursor = false;

	// --- 블루프린트 API ---------------------------------------------------

	/** 서버에 방 목록을 다시 요청한다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void RefreshRoomList();

	/**
	 * 방 참여를 시작한다.
	 * 비밀번호 방이면 곧바로 보내지 않고 비밀번호 입력창을 먼저 연다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void BeginJoin(int32 RoomId);

	/** 입력창의 비밀번호로 참여를 확정한다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void ConfirmJoinWithPassword();

	/** 비밀번호 입력을 취소하고 목록으로 돌아간다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void CancelPasswordPrompt();

	/** 목록을 닫는다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void CloseList();

	/** 사용자에게 보여줄 안내 문구를 바꾼다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void SetStatus(const FString& Text, bool bIsError);

	/**
	 * 참여가 승인됐을 때 블루프린트가 이어서 할 일을 넣는 훅.
	 *
	 * 여기서 여행한다: ClientTravel(Result.MakeTravelURL(RoomPassword), TRAVEL_Absolute).
	 * 호스트가 아직 리슨서버를 열지 않았다면 이 여행은 실패한다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MOU|Lobby")
	void OnRoomJoinApproved(const FMOURoomJoinResult& Result, const FString& RoomPassword);

	// --- C++ 소유자용 (ULobbyWidgetBase 가 바인딩한다) ----------------------

	FOnRoomJoinApprovedNative OnRoomJoinApprovedNative;
	FOnRoomListClosed         OnRoomListClosed;

protected:
	// --- 위젯 바인딩 (WBP 에 같은 이름이 있으면 자동 연결) -------------------

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UScrollBox> RoomListScrollBox;

	/** 여기에 URoomListEntryWidget 이 한 줄씩 쌓인다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UVerticalBox> RoomListBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UButton> RefreshButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UButton> CloseButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UTextBlock> StatusText;

	/** 비밀번호 입력줄 전체. 비밀번호 방을 고를 때만 보인다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UPanelWidget> PasswordPromptPanel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UEditableTextBox> JoinPasswordBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UButton> JoinConfirmButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UButton> JoinCancelButton;

private:
	// --- 델리게이트 수신부 (AddDynamic 대상이라 전부 UFUNCTION) --------------

	UFUNCTION()
	void HandleRoomListReceived(const TArray<FMOURoomInfo>& Rooms);

	UFUNCTION()
	void HandleRoomJoinCompleted(const FMOURoomJoinResult& Result);

	UFUNCTION()
	void HandleRefreshClicked();

	UFUNCTION()
	void HandleCloseClicked();

	UFUNCTION()
	void HandleJoinConfirmClicked();

	UFUNCTION()
	void HandleJoinCancelClicked();

	// --- 내부 -------------------------------------------------------------

	void BuildDefaultLayout();

	/** 받은 목록으로 줄을 다시 만든다. */
	void RebuildEntries(const TArray<FMOURoomInfo>& Rooms);

	/** 줄에서 참여 버튼을 눌렀을 때. FOnRoomEntryJoinClicked 로 바인딩한다. */
	void HandleEntryJoinClicked(int32 RoomId);

	/** 비밀번호 없이 곧바로 참여 요청을 보낸다. */
	void SendJoinRequest(int32 RoomId, const FString& RoomPassword);

	void ShowPasswordPrompt(bool bShow);

	/** 응답을 기다리는 동안 버튼을 잠근다. */
	void SetBusy(bool bBusy);

	UChatSubsystem* GetChatSubsystem() const;

	/** 지금 화면에 떠 있는 줄들. 목록을 다시 받을 때 통째로 지우고 새로 만든다. */
	UPROPERTY()
	TArray<TObjectPtr<URoomListEntryWidget>> EntryWidgets;

	/** 마지막으로 받은 목록. 비밀번호 방인지 판정할 때 다시 본다. */
	TArray<FMOURoomInfo> CachedRooms;

	/** 비밀번호 입력을 기다리는 방. 0 이면 대기 중이 아니다. */
	int32 PendingJoinRoomId = 0;

	/** 참여 요청에 실제로 실어 보낸 비밀번호. 승인되면 여행 URL 에 다시 쓴다. */
	FString SubmittedPassword;

	FTimerHandle RefreshTimerHandle;

	bool bBusy = false;
	bool bSubscribed = false;
};
