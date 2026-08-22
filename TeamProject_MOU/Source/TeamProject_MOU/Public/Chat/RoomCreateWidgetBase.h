// MOU 로비 - 방 생성 UI.
//
// [이 위젯이 하는 일]
//   콘솔 명령 MOU.Room.Host 로 하던 일을 화면에서 한다.
//     1. 방 제목 + 방 비밀번호(숫자 4자리, 선택) 입력
//     2. UChatSubsystem::CreateRoom() 호출
//     3. OnRoomCreated 응답을 받아 성공/실패를 화면에 표시
//
// [시스템에서의 위치]
//     ULobbyWidgetBase (메인메뉴)
//       ├─ URoomCreateWidgetBase   ← 이 파일. 방을 "만드는" 쪽
//       └─ URoomListWidgetBase        방에 "들어가는" 쪽
//   서버와 직접 대화하지 않는다. UChatSubsystem 하고만 대화한다.
//     보낼 때: UChatSubsystem::CreateRoom()
//     받을 때: UChatSubsystem::OnRoomCreated
//   대응하는 서버 코드: MOU_Server/Server/Server.cpp 의 RoomCreateReq 핸들러,
//                       MOU_Server/Server/Rooms.cpp 의 Rooms::Create()
//
// [이 위젯은 리슨서버를 열지 않는다]
//   방을 만드는 것과 리슨서버를 여는 것은 별개다. 여기서는 로비 서버에 "방 하나 등록해줘"
//   라고만 하고, 실제 ServerTravel 은 소유자(ULobbyWidgetBase 또는 블루프린트)가 한다.
//   그래야 맵 이름 같은 게임 쪽 사정을 이 위젯이 몰라도 된다.
//
// [ChatWidgetBase / LoginWidgetBase 와 같은 규약]
//   WBP 없이 CreateWidget 만 해도 C++ 이 기본 레이아웃을 조립한다.
//   WBP 를 만들어 이 클래스를 부모로 지정하면, 아래 BindWidgetOptional 과
//   같은 이름의 위젯을 배치하는 것만으로 디자인을 갈아끼울 수 있다.
//     필요한 이름: RoomTitleBox / RoomPasswordBox /
//                  CreateButton / CancelButton / MessageText / TitleText

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Chat/LobbyTypes.h"
#include "RoomCreateWidgetBase.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UChatSubsystem;

/**
 * 방 생성이 끝났을 때 C++ 소유자에게 알리는 통로 (ULobbyWidgetBase 가 받는다).
 *
 * 블루프린트는 이 델리게이트 대신 OnRoomCreateSucceeded 이벤트를 쓴다.
 * 다이나믹 델리게이트가 아닌 이유: 소유자-자식 사이의 1:1 연결이라
 * 리플렉션이 필요 없고, 방 비밀번호를 블루프린트 그래프에 흘리지 않는 편이 낫다.
 *
 * @param RoomId       서버가 확정한 방 번호
 * @param RoomPassword 사용자가 입력한 방 비밀번호(공개방이면 빈 문자열).
 *                     방장은 이 값을 리슨서버 URL 옵션으로 다시 실어야 하므로 같이 넘긴다.
 */
DECLARE_DELEGATE_TwoParams(FOnRoomCreateFinished, int32 /*RoomId*/, const FString& /*RoomPassword*/);

/** 사용자가 방 만들기를 취소했을 때. */
DECLARE_DELEGATE(FOnRoomCreateCancelled);

/**
 * 방 제목과 비밀번호를 입력받아 로비 서버에 방을 등록하는 위젯.
 *
 * 사용 흐름:
 *   1. 로그인이 끝난 뒤 CreateWidget<URoomCreateWidgetBase>() 후 AddToViewport()
 *   2. 사용자가 제목(필수) / 비밀번호(선택, 숫자 4자리)를 입력하고 "방 만들기"
 *   3. 성공하면 OnRoomCreateSucceeded 가 불리고 이 위젯은 스스로 사라진다
 */
UCLASS()
class TEAMPROJECT_MOU_API URoomCreateWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	URoomCreateWidgetBase(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- 설정 -------------------------------------------------------------

	/**
	 * 리슨서버가 실제로 열 포트. 이 값이 방 목록에 실려 나가고,
	 * 참여자는 이 포트로 호스트에게 붙는다. 게임의 리슨서버 포트와 반드시 같아야 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	int32 HostPort = 7777;

	/** 방 생성에 성공하면 이 위젯을 자동으로 화면에서 없앨지. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	bool bRemoveOnSuccess = true;

	/**
	 * 이 위젯이 마우스 커서를 직접 켜고 끌지.
	 *
	 * 기본값이 false 인 이유: 보통은 ULobbyWidgetBase 안에서 열리고,
	 * 그때 커서는 로비가 이미 켜둔 상태다. 둘 다 관리하면 이 위젯이 닫힐 때
	 * 커서가 사라져 뒤에 남은 로비를 조작할 수 없게 된다.
	 * 이 위젯만 단독으로 띄우는 화면이라면 true 로 바꾼다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	bool bManageMouseCursor = false;

	// --- 블루프린트 API ---------------------------------------------------

	/** 입력값을 검사하고 서버에 방 생성을 요청한다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void TryCreateRoom();

	/** 취소. 위젯을 닫고 소유자에게 알린다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void CancelCreate();

	/** 사용자에게 보여줄 안내 문구를 바꾼다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void SetMessage(const FString& Text, bool bIsError);

	/**
	 * 방이 만들어졌을 때 블루프린트가 이어서 할 일을 넣는 훅.
	 *
	 * 여기서 리슨서버를 연다: OpenLevel(맵이름, true, "listen").
	 * RoomPassword 는 호스트의 GameMode::PreLogin 이 참여자를 검사할 때 쓸 값이다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MOU|Lobby")
	void OnRoomCreateSucceeded(int32 RoomId, const FString& RoomPassword);

	// --- C++ 소유자용 (ULobbyWidgetBase 가 바인딩한다) ----------------------

	FOnRoomCreateFinished  OnRoomCreateFinished;
	FOnRoomCreateCancelled OnRoomCreateCancelled;

protected:
	// --- 위젯 바인딩 (WBP 에 같은 이름이 있으면 자동 연결) -------------------

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UEditableTextBox> RoomTitleBox;

	/** 숫자 4자리. 비워두면 공개방이 된다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UEditableTextBox> RoomPasswordBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UButton> CreateButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UButton> CancelButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UTextBlock> MessageText;

private:
	// --- 델리게이트 수신부 (AddDynamic 대상이라 전부 UFUNCTION) --------------

	UFUNCTION()
	void HandleRoomCreated(bool bSuccess, int32 RoomId, EMOURoomResultBP Result);

	UFUNCTION()
	void HandleCreateClicked();

	UFUNCTION()
	void HandleCancelClicked();

	// --- 내부 -------------------------------------------------------------

	void BuildDefaultLayout();

	/** 응답을 기다리는 동안 버튼을 잠근다. 중복 요청을 막는다. */
	void SetBusy(bool bBusy);

	UChatSubsystem* GetChatSubsystem() const;

	/** 응답 대기 중인지. */
	bool bBusy = false;

	bool bSubscribed = false;

	/**
	 * 요청할 때 쓴 방 비밀번호.
	 *
	 * RoomCreateAck 에는 비밀번호가 실려오지 않으므로(서버가 되돌려줄 이유가 없다)
	 * 성공 시 소유자에게 넘겨주려면 클라이언트가 기억하고 있어야 한다.
	 */
	FString SubmittedPassword;
};
