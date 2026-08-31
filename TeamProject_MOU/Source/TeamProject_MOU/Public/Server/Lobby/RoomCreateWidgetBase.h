// MOU 로비 - 방 생성 UI.
//
// [이 위젯이 하는 일]
//   콘솔 명령 MOU.Room.Host 로 하던 일을 화면에서 한다.
//     1. 방 제목 + 방 비밀번호(숫자 4자리, 선택) 입력
//     2. UServerSubsystem::CreateRoom() 호출
//     3. OnRoomCreated 응답을 받아 성공/실패를 화면에 표시
//
// [시스템에서의 위치]
//     ULobbyWidgetBase (메인메뉴)
//       ├─ URoomCreateWidgetBase   ← 이 파일. 방을 "만드는" 쪽
//       └─ URoomListWidgetBase        방에 "들어가는" 쪽
//   서버와 직접 대화하지 않는다. UServerSubsystem 하고만 대화한다.
//     보낼 때: UServerSubsystem::CreateRoom()
//     받을 때: UServerSubsystem::OnRoomCreated
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
#include "Server/Lobby/LobbyTypes.h"
#include "Server/Net/NatPortMappingSubsystem.h"   // EMOUNatResultBP
#include "RoomCreateWidgetBase.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UServerSubsystem;

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
	 * 이 창이 열리는 순간 공유기(UPnP)에 포트를 열어달라고 요청할지.
	 *
	 * [왜 여기서 시작하는가]
	 *   포트 열기는 SSDP 탐색 + SOAP 왕복이라 몇 초가 걸린다. "방 만들기" 를 누른 뒤에
	 *   시작하면 그 시간만큼 사용자가 멈춘 화면을 본다. 창이 열리고 사용자가 제목을
	 *   입력하는 동안 백그라운드로 끝내두면 지연이 사라진다.
	 *
	 *   로그인 시점에 미리 하지 않는 이유는, 접속자 대부분이 참가자라서 끝내 호스트가
	 *   되지 않을 사람의 포트까지 여는 셈이기 때문이다. 포트 매핑이 필요한 것은
	 *   "밖에서 나에게 들어오는" 연결뿐이고, 그건 방장에게만 해당한다.
	 *   이 창을 열었다는 것은 호스트가 될 의사를 밝힌 것이므로 여기가 가장 이른 지점이다.
	 *
	 * 실패해도 방 생성은 그대로 진행된다. 같은 네트워크에서는 어차피 접속되기 때문이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	bool bOpenPortOnShow = true;

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

	/** 포트 열기가 끝났다. 성공이든 실패든 온다. */
	UFUNCTION()
	void HandleNatMappingFinished(EMOUNatResultBP Result, int32 ExternalPort, const FString& ExternalIp);

	/** 실제 외부 UDP 도달성 확인이 끝났다. 방 생성 대기 중이면 여기서 이어간다. */
	UFUNCTION()
	void HandleReachabilityChecked(bool bReachable, const FString& Detail);

	// --- 내부 -------------------------------------------------------------

	void BuildDefaultLayout();

	/**
	 * 검사를 통과한 값으로 실제 요청을 보낸다.
	 *
	 * TryCreateRoom 에서 분리한 이유: 포트 열기가 아직 진행 중이면 곧바로 보낼 수 없고,
	 * 매핑이 끝난 뒤에 같은 요청을 이어서 보내야 하기 때문이다.
	 */
	void SubmitCreateRoom();

	/** 방 정보에 신고할 포트. 매핑에 성공했으면 "외부" 포트, 아니면 HostPort 그대로. */
	int32 ResolveAdvertisedPort() const;

	class UNatPortMappingSubsystem* GetNatSubsystem() const;

	/** 응답을 기다리는 동안 버튼을 잠근다. 중복 요청을 막는다. */
	void SetBusy(bool bBusy);

	UServerSubsystem* GetServerSubsystem() const;

	/** 응답 대기 중인지. */
	bool bBusy = false;

	bool bSubscribed = false;

	/** NAT 델리게이트 구독 여부. OnRoomCreated 와 따로 관리한다. */
	bool bNatSubscribed = false;

	/**
	 * 사용자가 "방 만들기" 를 눌렀지만 포트 열기가 아직 안 끝나서 대기 중인가.
	 *
	 * 이게 없으면 매핑 도중에 누른 사용자는 HostPort 로 방이 만들어지고,
	 * 잠시 뒤 공유기가 다른 외부 포트를 열어줘도 방 정보는 이미 틀린 값으로 나가 있다.
	 */
	bool bCreateWaitingForNat = false;

	/** 사용자가 방 만들기를 눌렀지만 외부 도달성 프로브가 아직 진행 중인가. */
	bool bCreateWaitingForProbe = false;

	/** 검사를 통과한 방 제목. 매핑을 기다리는 동안 들고 있어야 한다. */
	FString SubmittedTitle;

	/**
	 * 요청할 때 쓴 방 비밀번호.
	 *
	 * RoomCreateAck 에는 비밀번호가 실려오지 않으므로(서버가 되돌려줄 이유가 없다)
	 * 성공 시 소유자에게 넘겨주려면 클라이언트가 기억하고 있어야 한다.
	 */
	FString SubmittedPassword;
};
