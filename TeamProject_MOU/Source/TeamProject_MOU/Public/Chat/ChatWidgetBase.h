// MOU 채팅 - 채팅 UI 위젯 (5단계).
//
// [이 위젯이 채팅 시스템에서 차지하는 위치]
//
//   Server.exe
//        ↕ TCP
//   FChatClientRunnable  (워커 스레드)
//        ↕ TQueue
//   UChatSubsystem       (게임 스레드) ── OnChatMessageReceived ──┐
//                                     ── OnChatStateChanged ─────┤
//                                     ── OnChatLoginCompleted ───┤
//   UChatWidgetBase  <- 여기 ◀────────────────────────────────────┘
//        ↕ SendChat()
//
//   이 위젯은 소켓이나 패킷을 전혀 모른다. UChatSubsystem 의 델리게이트를 구독해서
//   받은 FChatMessage 를 한 줄씩 화면에 붙이고, 사용자가 입력한 문자열을
//   SendChat() 으로 넘길 뿐이다.
//
// [WBP 없이도 동작한다 - 디자이너 작업과의 관계]
//
//   이 클래스는 두 가지 방식으로 쓸 수 있다.
//
//   1) C++ 단독 (지금 상태)
//      WBP 를 만들지 않고 CreateWidget<UChatWidgetBase>() 로 바로 띄우면,
//      NativeOnInitialized 에서 기본 레이아웃을 C++ 로 조립한다.
//      UI 디자이너 작업을 기다리지 않고 서버/클라이언트 파트를 검증하기 위한 것이다.
//
//   2) WBP 를 만들어 이 클래스를 부모로 지정 (나중에 디자이너가 할 작업)
//      아래 BindWidgetOptional 프로퍼티와 "이름이 같은" 위젯을 WBP 에 배치하면
//      C++ 이 그 위젯을 그대로 집어서 쓴다. 기본 레이아웃 조립은 건너뛴다.
//      필요한 이름: ChatRootBorder / ChatScrollBox / ChatLogBox /
//                   ChatInputBox / StatusText / ChannelText
//      전부 Optional 이라 일부만 배치해도 컴파일은 되지만,
//      ChatLogBox 와 ChatInputBox 가 없으면 로그 표시와 입력이 동작하지 않는다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Chat/ChatTypes.h"
#include "InputCoreTypes.h"
#include "ChatWidgetBase.generated.h"

class UBorder;
class UEditableTextBox;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UChatSubsystem;

/**
 * 채팅 로그 + 입력창.
 *
 * 사용 흐름:
 *   1. 어딘가에서 UChatSubsystem::ConnectToChatServer() / Login() 을 호출해 접속한다
 *   2. 이 위젯을 CreateWidget 해서 AddToViewport() 한다
 *   3. Enter 로 입력창을 열고, 다시 Enter 로 전송한다
 *
 * 접속과 UI 는 서로를 기다리지 않는다. 접속 전에 위젯을 띄워도 되고,
 * 위젯 없이 접속만 해도 된다. 상태는 StatusText 에 표시된다.
 */
UCLASS()
class TEAMPROJECT_MOU_API UChatWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	UChatWidgetBase(const FObjectInitializer& ObjectInitializer);

	// --- UUserWidget ------------------------------------------------------

	/** WidgetTree 가 준비된 직후, RebuildWidget 보다 먼저 불린다. 기본 레이아웃을 여기서 조립한다. */
	virtual void NativeOnInitialized() override;

	/** 뷰포트에 붙을 때. 서브시스템 델리게이트 구독을 여기서 한다. */
	virtual void NativeConstruct() override;

	/** 뷰포트에서 떨어질 때. 구독 해제를 반드시 여기서 해야 댕글링 델리게이트가 안 남는다. */
	virtual void NativeDestruct() override;

	// --- 블루프린트 API ---------------------------------------------------

	/** 입력창을 열고 키보드 포커스를 준다. 이때부터 게임 조작 대신 타이핑이 된다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat")
	void OpenChatInput();

	/** 입력창을 닫고 게임 조작으로 되돌린다. 입력 중이던 내용은 버린다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat")
	void CloseChatInput();

	UFUNCTION(BlueprintCallable, Category = "MOU|Chat")
	void ToggleChatInput();

	/**
	 * 앞으로 보낼 채널을 바꾼다.
	 * 사망 채널은 서버가 자격을 검사하므로, 살아있으면 보내도 조용히 버려진다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat")
	void SetActiveChannel(EChatChannelBP NewChannel);

	/** 전체 -> 팀 -> 사망 -> 전체 순으로 순환한다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat")
	void CycleChannel();

	UFUNCTION(BlueprintPure, Category = "MOU|Chat")
	EChatChannelBP GetActiveChannel() const { return ActiveChannel; }

	UFUNCTION(BlueprintPure, Category = "MOU|Chat")
	bool IsChatInputOpen() const { return bInputOpen; }

	/**
	 * 서버에서 온 게 아닌, 클라이언트가 직접 만든 안내 문구를 로그에 추가한다.
	 * 연결 상태 변화 같은 것을 표시하는 데 쓴다. 서버로 전송되지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Chat")
	void AddSystemLine(const FString& Text);

	// --- 설정 -------------------------------------------------------------

	/**
	 * 로그에 유지할 최대 줄 수. 넘으면 오래된 줄부터 지운다.
	 * 제한이 없으면 장시간 플레이에서 위젯이 계속 쌓여 프레임이 떨어진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Chat")
	int32 MaxLogLines = 200;

	/**
	 * 소유 플레이어 컨트롤러의 InputComponent 에 채팅 토글 키를 직접 바인딩할지.
	 *
	 * 켜두면 별도 입력 에셋(IA_...) 없이 바로 Enter 로 채팅을 열 수 있다.
	 * 게임 쪽에서 EnhancedInput 으로 채팅 키를 따로 만들 거라면 false 로 끄고
	 * 그 액션에서 ToggleChatInput() 을 호출하면 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Chat")
	bool bBindToggleKeyToOwningPlayer = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Chat", meta = (EditCondition = "bBindToggleKeyToOwningPlayer"))
	FKey ChatToggleKey = EKeys::Enter;

	/**
	 * 입력창을 열고 닫을 때 마우스 커서 표시를 이 위젯이 바꿀지.
	 * 게임 쪽에서 커서를 직접 관리한다면 false 로 꺼서 충돌을 막는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Chat")
	bool bManageMouseCursor = true;

protected:
	// --- 위젯 바인딩 ------------------------------------------------------
	// WBP 에 같은 이름의 위젯이 있으면 자동으로 연결된다.
	// 없으면 null 이고, 그때는 BuildDefaultLayout() 이 C++ 로 만들어 채운다.

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Chat")
	TObjectPtr<UBorder> ChatRootBorder;

	/** 로그를 담는 스크롤 영역. 새 줄이 추가되면 맨 아래로 자동 스크롤한다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Chat")
	TObjectPtr<UScrollBox> ChatScrollBox;

	/** 실제 줄(UTextBlock)들이 쌓이는 세로 박스. ChatScrollBox 안에 있어야 한다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Chat")
	TObjectPtr<UVerticalBox> ChatLogBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Chat")
	TObjectPtr<UEditableTextBox> ChatInputBox;

	/** 연결 상태 표시줄 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Chat")
	TObjectPtr<UTextBlock> StatusText;

	/** 입력창 왼쪽의 현재 채널 표시 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Chat")
	TObjectPtr<UTextBlock> ChannelText;

private:
	// --- 서브시스템 델리게이트 수신부 --------------------------------------
	// AddDynamic 으로 바인딩하므로 전부 UFUNCTION 이어야 한다.

	UFUNCTION()
	void HandleChatMessage(const FChatMessage& Message);

	UFUNCTION()
	void HandleChatStateChanged(EChatConnectionState NewState, const FString& Detail);

	UFUNCTION()
	void HandleLoginCompleted(const FChatLoginResult& Result);

	UFUNCTION()
	void HandleInputCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	// --- 내부 -------------------------------------------------------------

	/** WBP 가 없을 때 C++ 로 기본 레이아웃을 조립한다. */
	void BuildDefaultLayout();

	/** 입력 문자열을 해석해서 채널 전환 또는 전송을 수행한다. */
	void SubmitInput(const FString& RawText);

	/** 로그에 한 줄 추가하고, 넘치면 오래된 줄을 지우고, 맨 아래로 스크롤한다. */
	void AppendLine(const FString& Line, const FLinearColor& Color);

	void RefreshStatusText();
	void RefreshChannelText();

	UChatSubsystem* GetChatSubsystem() const;

	static FLinearColor GetChannelColor(EChatChannelBP Channel);
	static const TCHAR* GetChannelName(EChatChannelBP Channel);

	/** 지금 입력할 채널. 전송할 때마다 이 값이 붙는다. */
	UPROPERTY()
	EChatChannelBP ActiveChannel = EChatChannelBP::All;

	bool bInputOpen = false;

	/** 델리게이트를 중복 구독하지 않기 위한 플래그. NativeConstruct 가 두 번 불릴 수 있다. */
	bool bSubscribed = false;
};
