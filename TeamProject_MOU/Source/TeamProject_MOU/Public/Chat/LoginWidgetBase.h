// MOU 채팅 - 로그인 / 계정 생성 UI.
//
// [이 위젯이 하는 일]
//   콘솔 명령(MOU.Chat.Connect / MOU.Chat.Login)으로 하던 일을 화면에서 한다.
//     1. 채팅 서버에 접속
//     2. 아이디/비밀번호로 로그인, 또는 계정 생성
//     3. 성공하면 스스로 사라지고 채팅 위젯과 로비 메인메뉴(ULobbyWidgetBase)를 띄운다
//
// [ChatWidgetBase 와 같은 규약]
//   WBP 없이 CreateWidget 만 해도 C++ 이 기본 레이아웃을 조립한다.
//   WBP 를 만들어 이 클래스를 부모로 지정하면, 아래 BindWidgetOptional 과
//   같은 이름의 위젯을 배치하는 것만으로 디자인을 갈아끼울 수 있다.
//     필요한 이름: LoginIdBox / PasswordBox / NicknameBox /
//                  LoginButton / RegisterButton / MessageText / TitleText
//
// [비밀번호 취급 — 지켜야 할 것]
//   - PasswordBox 는 IsPassword 를 켜서 화면에 ●●● 로만 보이게 한다.
//   - 비밀번호는 로그(UE_LOG)에 절대 남기지 않는다.
//   - 로그인 성공/실패 후 입력칸을 비운다.
//   - 그래도 전송 구간은 평문이다. TLS 가 없다는 점은 변하지 않는다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Chat/ChatTypes.h"
#include "LoginWidgetBase.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;
class UChatSubsystem;

/**
 * 접속 + 로그인 + 계정 생성을 한 화면에서 처리하는 위젯.
 *
 * 사용 흐름:
 *   1. 게임 시작 화면에서 CreateWidget<ULoginWidgetBase>() 후 AddToViewport()
 *   2. 사용자가 아이디/비밀번호를 넣고 로그인 또는 가입
 *   3. 로그인이 성공하면 OnLoginSucceeded 가 불리고, 이 위젯은 스스로 사라진다
 */
UCLASS()
class TEAMPROJECT_MOU_API ULoginWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	ULoginWidgetBase(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- 설정 -------------------------------------------------------------

	/**
	 * 접속할 채팅 서버. **비워두는 것이 기본이자 권장값이다.**
	 *
	 * 비어 있으면(호스트가 빈 문자열, 포트가 0) 접속 직전에
	 * UMOUServerSettings 가 정한 주소를 쓴다 — 즉 Config/DefaultGame.ini 의
	 * 팀 공유 주소다. 팀원 전원이 같은 서버를 보게 하려면 여기를 건드리지 않으면 된다.
	 *
	 * ★ 여기에 127.0.0.1 을 적으면 안 된다. 그 값은 "이 게임이 돌고 있는 PC" 를 뜻해서
	 *   서버를 켜지 않은 팀원은 자기 자신에게 붙으려다 항상 실패한다.
	 *   특정 위젯만 다른 서버를 봐야 할 때만 실제 IP 를 적는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Login")
	FString ServerHost;

	/** 0 이면 ServerHost 와 마찬가지로 설정값을 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Login")
	int32 ServerPort = 0;

	/**
	 * 로그인에 쓸 팀 ID.
	 *
	 * 계정에 저장되는 값이 아니다. 팀은 매 판 게임이 정하는 것이므로
	 * 나중에 게임 로직이 이 값을 채워 넣게 된다. 지금은 테스트용 기본값이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Login")
	int32 TeamId = 0;

	/** 로그인 성공 시 이 위젯을 자동으로 화면에서 없앨지. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Login")
	bool bRemoveOnSuccess = true;

	/**
	 * 로그인 성공 시 채팅 위젯을 자동으로 띄울지.
	 * 별도의 HUD 흐름이 있다면 꺼두고 OnLoginSucceeded 에서 직접 처리하면 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Login")
	bool bShowChatWidgetOnSuccess = true;

	/**
	 * 띄울 채팅 위젯 클래스. 비워두면 UChatWidgetBase 를 그대로 쓴다.
	 * 디자이너가 만든 WBP_Chat 이 있으면 여기에 지정한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Login")
	TSubclassOf<UUserWidget> ChatWidgetClass;

	/**
	 * 로그인 성공 시 로비 메인메뉴(ULobbyWidgetBase)를 자동으로 띄울지.
	 *
	 * 로그인 다음에 올 화면은 보통 "방 만들기 / 참여하기" 다. 따로 붙이지 않아도
	 * 로그인 -> 로비 -> 방 흐름이 이어지도록 기본값을 켜뒀다.
	 * 타이틀 화면이 따로 있는 흐름이라면 꺼두고 OnLoginSucceeded 에서 직접 처리한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Login")
	bool bShowLobbyWidgetOnSuccess = true;

	/**
	 * 띄울 로비 위젯 클래스. 비워두면 ULobbyWidgetBase 를 그대로 쓴다.
	 * 디자이너가 만든 WBP_Lobby 가 있으면 여기에 지정한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Login")
	TSubclassOf<UUserWidget> LobbyWidgetClass;

	// --- 블루프린트 API ---------------------------------------------------

	/** 입력된 아이디/비밀번호로 로그인한다. 서버에 연결되어 있지 않으면 먼저 연결한다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Login")
	void TryLogin();

	/** 입력된 값으로 계정을 만든다. 성공하면 이어서 자동으로 로그인한다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Login")
	void TryRegister();

	/** 사용자에게 보여줄 안내 문구를 바꾼다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Login")
	void SetMessage(const FString& Text, bool bIsError);

	/** 로그인이 성공했을 때 블루프린트가 이어서 할 일을 넣는 훅. */
	UFUNCTION(BlueprintImplementableEvent, Category = "MOU|Login")
	void OnLoginSucceeded(const FChatLoginResult& Result);

protected:
	// --- 위젯 바인딩 (WBP 에 같은 이름이 있으면 자동 연결) -------------------

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Login")
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Login")
	TObjectPtr<UEditableTextBox> LoginIdBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Login")
	TObjectPtr<UEditableTextBox> PasswordBox;

	/** 계정 생성에만 쓰인다. 비워두면 아이디를 닉네임으로 쓴다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Login")
	TObjectPtr<UEditableTextBox> NicknameBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Login")
	TObjectPtr<UButton> LoginButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Login")
	TObjectPtr<UButton> RegisterButton;

	/** 실패 사유나 진행 상태를 보여주는 줄. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Login")
	TObjectPtr<UTextBlock> MessageText;

private:
	// --- 델리게이트 수신부 (AddDynamic 대상이라 전부 UFUNCTION) --------------

	UFUNCTION()
	void HandleLoginCompleted(const FChatLoginResult& Result);

	UFUNCTION()
	void HandleRegisterCompleted(bool bSuccess, EChatLoginResultBP Result);

	UFUNCTION()
	void HandleStateChanged(EChatConnectionState NewState, const FString& Detail);

	UFUNCTION()
	void HandleLoginClicked();

	UFUNCTION()
	void HandleRegisterClicked();

	// --- 내부 -------------------------------------------------------------

	void BuildDefaultLayout();

	/** 서버에 아직 안 붙었으면 붙인다. 이미 붙어있으면 아무것도 하지 않는다. */
	void EnsureConnected();

	/** 입력값을 읽고 형식을 검사한다. 실패하면 MessageText 에 사유를 표시하고 false. */
	bool ReadAndValidateInput(FString& OutId, FString& OutPassword);

	/** 로그인/가입 버튼을 잠그거나 푼다. 응답을 기다리는 동안 중복 요청을 막는다. */
	void SetBusy(bool bBusy);

	void ShowChatWidget();

	void ShowLobbyWidget();

	UChatSubsystem* GetChatSubsystem() const;

	/** 가입 성공 직후 자동 로그인을 하기 위한 플래그. */
	bool bAutoLoginAfterRegister = false;

	/** 응답 대기 중인지. 버튼 중복 클릭을 막는다. */
	bool bBusy = false;

	bool bSubscribed = false;
};
