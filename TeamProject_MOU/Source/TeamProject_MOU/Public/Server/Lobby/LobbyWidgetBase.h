// MOU 로비 - 메인메뉴 + 대기실 UI.
//
// [이 위젯이 하는 일]
//   로그인 다음에 오는 화면 전부다. 화면 하나가 상태에 따라 세 얼굴을 갖는다.
//
//     상태          1번 버튼            2번 버튼        3번 버튼
//     ─────────────────────────────────────────────────────────────
//     메인메뉴      방 만들기           참여하기        게임 종료
//     대기실(참여)  준비하기 / 준비해제  커스터마이징    나가기
//     대기실(방장)  게임 시작 *         커스터마이징    나가기
//
//     * 참여자가 전원 준비해야 켜진다. 판정은 서버가 한다(RoomMemberList.bAllReady).
//
//   버튼을 상태마다 새로 만들지 않고 같은 세 개의 라벨만 바꾸는 이유:
//   위젯이 하나면 WBP 로 갈아끼울 때도 배치가 한 번으로 끝나고,
//   "지금 무엇을 할 수 있는가" 가 항상 같은 자리에 있어 눈이 헤매지 않는다.
//
// [시스템에서의 위치]
//     ULoginWidgetBase (로그인)
//       └─ ULobbyWidgetBase (메인메뉴 + 대기실)   ← 이 파일
//            ├─ URoomCreateWidgetBase             방을 "만드는" 창
//            └─ URoomListWidgetBase               방에 "들어가는" 창
//   두 자식 창은 방에 들어갈 때까지만 쓰인다. 들어간 뒤로는 이 위젯이 대기실이 된다.
//
// [대기실 상태는 서버가 갖고 있다]
//   누가 방에 있고 누가 준비했는지는 전부 서버가 진실을 안다.
//   이 위젯은 UServerSubsystem::OnRoomMembersChanged 로 오는 스냅샷을 그릴 뿐이고,
//   자기가 세거나 추측하지 않는다. 그래서 두 클라이언트가 다른 그림을 볼 일이 없다.
//
// [방장이 나가면 방이 사라진다 — 이양하지 않는다]
//   호스트가 곧 리슨서버라서, 호스트 프로세스가 죽으면 게임 세션 자체가 없어진다.
//   남은 사람을 새 호스트로 세우려면 리슨서버를 다시 열고 전원이 새 주소로
//   재접속해야 하는데 UE 가 이를 기본 지원하지 않는다.
//   그래서 서버가 RoomClosed 를 보내고, 남은 사람은 메인메뉴로 돌아간다.
//
// [여행 — 두 박자로 나뉜다]
//   게임 시작 시점에만 여행한다(방을 만들거나 참여할 때가 아니다).
//   그런데 방장과 참여자가 떠나는 시점이 다르다.
//
//     1박자  OnRoomGameStarted   방장이 리슨서버를 열기 시작한다.
//                                참여자는 "호스트가 서버를 여는 중" 을 보며 기다린다.
//     2박자  OnRoomHostReady     리슨서버가 실제로 떴다. 참여자가 그때 떠난다.
//
//   두 박자로 나눈 이유는 1박자에서 참여자가 떠나면 아직 열리지 않은 주소에 붙으려다
//   튕기기 때문이다. v5 까지는 고정 3초를 기다려 때웠는데, 그 값은 느린 PC 에서
//   모자라고 빠른 PC 에서는 낭비였다. 이제는 실제로 뜬 것을 보고 신호를 보낸다.
//
//     방장  : HostMapName 이 채워져 있으면 OpenLevel(맵, "listen"). 비어 있으면
//             여행은 게임 쪽(블루프린트/게임모드)의 몫이다 — 맵 이름은 위젯이 정할 일이 아니다.
//     참여자: bAutoTravelOnGameStart 가 켜져 있으면 2박자에서 ClientTravel
//
//   [빠진 것] 호스트의 AGameModeBase::PreLogin 에서 RoomPassword URL 옵션을
//   다시 검사하는 부분이 없다. 자세한 내용은 SERVER_INTEGRATION.md 12절 3번.
//
// [WBP 로 갈아끼우려면]
//   WBP 없이 CreateWidget 만 해도 C++ 이 기본 레이아웃을 조립한다.
//   WBP 를 만들어 부모로 지정하면 아래 BindWidgetOptional 과 같은 이름의 위젯을
//   배치하는 것만으로 디자인이 바뀐다.
//     필요한 이름: PrimaryButton / SecondaryButton / TertiaryButton
//                  PrimaryButtonLabel / SecondaryButtonLabel / TertiaryButtonLabel
//                  TitleText / StatusText / MessageText / MemberListBox

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Server/Chat/ChatTypes.h"
#include "Server/Lobby/LobbyTypes.h"
#include "LobbyWidgetBase.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;
class UServerSubsystem;
class URoomCreateWidgetBase;
class URoomListWidgetBase;

/** 로비 화면이 지금 무엇을 보여주고 있는지. */
UENUM(BlueprintType)
enum class EMOULobbyUIState : uint8
{
	/** 방 만들기 / 참여하기 / 게임 종료 */
	MainMenu    UMETA(DisplayName = "메인메뉴"),
	/** 방 안. 방장인지는 UServerSubsystem::IsRoomHost() 로 갈린다 */
	WaitingRoom UMETA(DisplayName = "대기실")
};

/**
 * 로비 메인메뉴 겸 대기실.
 *
 * 사용 흐름:
 *   1. 로그인 성공 후 자동으로 뜬다 (ULoginWidgetBase::bShowLobbyWidgetOnSuccess)
 *   2. 방을 만들거나 목록에서 골라 들어가면 화면이 대기실로 바뀐다
 *   3. 참여자가 전원 준비하면 방장의 "게임 시작" 이 켜진다
 *   4. 시작하면 OnGameStarted 가 불린다 — 거기서 여행한다
 */
UCLASS()
class TEAMPROJECT_MOU_API ULobbyWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	ULobbyWidgetBase(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- 설정 -------------------------------------------------------------

	/** 방 생성 창. 비워두면 URoomCreateWidgetBase 의 C++ 기본 레이아웃을 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	TSubclassOf<URoomCreateWidgetBase> RoomCreateWidgetClass;

	/** 방 목록 창. 비워두면 URoomListWidgetBase 의 C++ 기본 레이아웃을 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	TSubclassOf<URoomListWidgetBase> RoomListWidgetClass;

	/** 리슨서버가 실제로 열 포트. 방 생성 창에 그대로 넘긴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	int32 HostPort = 7777;

	/**
	 * 게임이 시작될 때 방장이 리슨서버로 열 맵. **비워두면 여행하지 않는다.**
	 *
	 * 비워두는 것이 기본값인 이유: 맵 이름은 게임 쪽 사정이고, 틀린 이름으로
	 * OpenLevel 을 부르면 그냥 검은 화면이 된다. 대기실/게임 맵이 정해지면
	 * 이 값을 채우거나 OnGameStarted 훅에서 직접 여행하면 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	FString HostMapName;

	/**
	 * 호스트의 리슨서버가 열렸다는 신호를 받으면 참여자가 자동으로 ClientTravel 할지.
	 *
	 * [v6 에서 안전해졌다]
	 *   v5 까지 이 값의 기본이 false 였던 이유는 "방장이 리슨서버를 다 열기 전에 붙으면
	 *   튕긴다" 였고, 그래서 GuestTravelDelay 라는 고정 3초 시차로 때웠다.
	 *   이제는 방장의 리슨서버가 실제로 뜬 뒤에야 신호가 오므로 그 위험이 없다.
	 *   시차 값도 함께 사라졌다.
	 *
	 *   끄고 싶다면 여기서 끄고 OnHostReady 훅에서 직접 여행하면 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	bool bAutoTravelOnGameStart = true;

	/**
	 * 참여자가 대기하는 동안 목적지 맵을 미리 메모리에 올려둘지. (2026-08-26)
	 *
	 * [왜 필요한가 — 두 번의 로딩이 직렬이었다]
	 *   두 박자 구조상 참여자는 이렇게 기다린다:
	 *
	 *     1박자 ─── 방장이 맵 로드 ───▶ 리슨서버 뜸
	 *                                      │
	 *                          2박자 ──────┴──▶ 참여자가 맵 로드 ───▶ 입장
	 *
	 *   두 로딩이 겹치지 않고 앞뒤로 붙어 있어서, 참여자가 실제로 기다리는 시간은
	 *   **두 로딩 시간의 합**이었다. 큰 맵에서 이 합이 그대로 체감된다.
	 *
	 *   그런데 1박자 동안 참여자는 아무것도 하지 않고 놀고 있다. 그 시간에 맵을
	 *   미리 올려두면 2박자의 ClientTravel 이 거의 즉시 끝난다 — 직렬이 병렬이 된다.
	 *
	 * [왜 참여자가 맵 이름을 알 수 있나]
	 *   HostMapName 은 WBP 에 박히는 값이라 방장과 참여자가 같은 값을 갖는다.
	 *   서버가 맵 이름을 내려주지 않아도 되는 이유다. (맵을 방마다 다르게 고르게
	 *   되면 그때는 프로토콜에 맵 이름을 실어야 하고, 이 최적화도 그 값을 써야 한다.)
	 *
	 * [★ 기본이 false 인 이유 (2026-08-28)]
	 *   참여자 여행 경로에 손을 대는 최적화인데 **실기로 검증되지 않았다.**
	 *   맵 패키지를 LoadPackageAsync 로 미리 올리는 것이 ClientTravel 의 LoadMap 과
	 *   어떻게 맞물리는지는 엔진 버전과 맵 구성을 타므로, 접속 문제를 쫓는 동안
	 *   변수를 하나라도 줄이려고 꺼둔다.
	 *
	 *   켜서 시험할 때는 **접속이 확실히 되는 상태에서** 켜고, LogMOUServer 에
	 *   "맵 미리 올리기 완료" 가 찍힌 뒤 실제로 여행이 빨라지는지 본다.
	 *   참여자만 못 넘어가는 증상이 다시 생기면 이것부터 끈다.
	 *
	 * [실패해도 안전하다]
	 *   미리 올리기가 실패하거나 늦어도 여행은 그대로 진행된다. 이 기능은 순수하게
	 *   "빨라지면 좋고 아니면 말고" 다. 그래서 실패를 오류로 취급하지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	bool bPreloadMapWhileWaiting = false;

	/** 방 만들기 / 참여하기 창이 열려 있는 동안 이 화면을 숨길지. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	bool bHideWhileChildOpen = true;

	/**
	 * 이 위젯이 마우스 커서와 입력 모드를 관리할지.
	 * 자식 창(방 만들기/목록)은 이 값이 false 로 내려가므로, 자식이 닫혀도 커서가 꺼지지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Lobby")
	bool bManageMouseCursor = true;

	// --- 블루프린트 API ---------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	EMOULobbyUIState GetUIState() const { return UIState; }

	/** 방 생성 창을 연다. 메인메뉴에서만 의미가 있다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void OpenRoomCreate();

	/** 방 목록 창을 연다. 메인메뉴에서만 의미가 있다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void OpenRoomList();

	/** 내 준비 상태를 뒤집는다. 참여자만 의미가 있다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void ToggleReady();

	/** 게임을 시작한다. 방장만, 전원 준비 완료일 때만. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void RequestStartGame();

	/** 대기실에서 나가 메인메뉴로 돌아간다. 방장이 나가면 방이 사라진다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void LeaveRoom();

	/** 커스터마이징 화면. 아직 미구현이라 훅만 호출한다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void OpenCustomize();

	/** 게임을 종료한다. 메인메뉴에서만 의미가 있다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void QuitGame();

	/** 사용자에게 보여줄 안내 문구를 바꾼다. */
	UFUNCTION(BlueprintCallable, Category = "MOU|Lobby")
	void SetMessage(const FString& Text, bool bIsError);

	/**
	 * 내가 만든 방의 비밀번호. 공개방이거나 방이 없으면 빈 문자열.
	 *
	 * [임시 보관소] 원래 이 값은 호스트의 GameMode 가 들고 있다가 PreLogin 에서
	 * 참여자를 검사하는 데 써야 한다. 그 부분이 아직 없어서 지금은 위젯이 들고 있다.
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Lobby")
	FString GetMyRoomPassword() const { return MyRoomPassword; }

	// --- 블루프린트 훅 -----------------------------------------------------

	/** 대기실에 들어갔다(방을 만들었거나 참여했다). */
	UFUNCTION(BlueprintImplementableEvent, Category = "MOU|Lobby")
	void OnEnteredWaitingRoom(int32 RoomId, bool bIsHost);

	/** 대기실에서 나와 메인메뉴로 돌아왔다. bRoomClosed 면 방장이 나가서 쫓겨난 것이다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "MOU|Lobby")
	void OnLeftWaitingRoom(bool bRoomClosed);

	/**
	 * 게임이 시작됐다. **참여자는 아직 떠나지 않는다.**
	 *
	 *   방장  : 여기서 리슨서버를 연다. HostMapName 이 채워져 있으면 자동으로도 한다.
	 *   참여자: 대기 화면을 띄우고 OnHostReady 를 기다린다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MOU|Lobby")
	void OnGameStarted(const FMOURoomJoinResult& Host, bool bIsHost, const FString& RoomPassword);

	/**
	 * 방장의 리슨서버가 열렸다. **참여자는 여기서 떠난다.**
	 *
	 * 방장에게는 오지 않는다 — 이 신호를 만든 것이 방장 자신이다.
	 * bAutoTravelOnGameStart 가 켜져 있으면 이 훅 다음에 자동으로 ClientTravel 한다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MOU|Lobby")
	void OnHostReady(const FMOURoomJoinResult& Host, const FString& RoomPassword);

	/** 커스터마이징 버튼을 눌렀다. 화면이 생기면 여기서 띄운다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "MOU|Lobby")
	void OnCustomizeRequested();

protected:
	// --- 위젯 바인딩 (WBP 에 같은 이름이 있으면 자동 연결) -------------------
	//
	// 버튼 이름이 Primary/Secondary/Tertiary 인 이유는 상태마다 하는 일이 달라서다.
	// HostButton 같은 이름을 붙이면 대기실에서 "준비하기" 를 담당할 때 거짓말이 된다.

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UTextBlock> TitleText;

	/** 로그인 상태 / 내 닉네임 / 대기실 방 번호. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UTextBlock> StatusText;

	/** 방 만들기 / 준비하기 / 게임 시작 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UButton> PrimaryButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UTextBlock> PrimaryButtonLabel;

	/** 참여하기 / 커스터마이징 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UButton> SecondaryButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UTextBlock> SecondaryButtonLabel;

	/** 게임 종료 / 나가기 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UButton> TertiaryButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UTextBlock> TertiaryButtonLabel;

	/** 대기실 명단이 한 줄씩 쌓인다. 메인메뉴에서는 접힌다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UVerticalBox> MemberListBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Lobby")
	TObjectPtr<UTextBlock> MessageText;

private:
	// --- 델리게이트 수신부 (AddDynamic 대상이라 전부 UFUNCTION) --------------

	UFUNCTION()
	void HandleChatStateChanged(EChatConnectionState NewState, const FString& Detail);

	UFUNCTION()
	void HandleLoginCompleted(const FChatLoginResult& Result);

	UFUNCTION()
	void HandleRoomMembersChanged(int32 RoomId, const TArray<FMOURoomMember>& Members, bool bAllReady);

	UFUNCTION()
	void HandleRoomClosed(int32 RoomId, EMOURoomCloseReasonBP Reason);

	UFUNCTION()
	void HandleGameStarted(const FMOURoomJoinResult& Host, bool bIsHost);

	UFUNCTION()
	void HandleHostReady(const FMOURoomJoinResult& Host);

	UFUNCTION()
	void HandlePrimaryClicked();

	UFUNCTION()
	void HandleSecondaryClicked();

	UFUNCTION()
	void HandleTertiaryClicked();

	// --- 자식 창에서 올라오는 결과 (네이티브 델리게이트) ---------------------

	void HandleRoomCreateFinished(int32 RoomId, const FString& RoomPassword);
	void HandleRoomCreateCancelled();
	void HandleRoomJoinApproved(const FMOURoomJoinResult& Result, const FString& RoomPassword);
	void HandleRoomListClosed();

	// --- 내부 -------------------------------------------------------------

	void BuildDefaultLayout();

	/** 대기실로 전환한다. */
	void EnterWaitingRoom(int32 RoomId, bool bIsHost);

	/** 메인메뉴로 되돌린다. bRoomClosed 면 방장이 나가서 쫓겨난 경우다. */
	void ReturnToMainMenu(bool bRoomClosed);

	/**
	 * 상태에 맞게 버튼 라벨/활성화와 명단을 다시 그린다.
	 * 화면을 바꾸는 곳은 여기 하나뿐이다 — 상태와 화면이 어긋나지 않게 하려는 것이다.
	 */
	void RefreshUI();

	/** 대기실 명단을 다시 그린다. */
	void RebuildMemberList();

	void CloseChildWidgets();
	void SetPanelVisible(bool bVisible);

	// ★ TravelAsHost / TravelAsClient / BeginPreloadHostMap 은 2026-08-29 에
	//   UServerSubsystem 으로 옮겼다(각각 TravelAsHost / TravelToHost / BeginPreloadMap).
	//
	//   이 위젯은 게임이 시작되면 닫힐 수 있고, 닫히면 NativeDestruct 가 델리게이트를
	//   해제한다. 그러면 출발 신호(OnRoomHostReady)를 받을 사람이 사라져 참여자가
	//   영영 안 떠난다. 실제로 그 버그를 며칠 쫓았다.
	//
	//   아래 HostMapName / bAutoTravelOnGameStart / bPreloadMapWhileWaiting 은
	//   그대로 남는다 — **값의 주인은 여전히 WBP 다.** NativeConstruct 에서
	//   UServerSubsystem::ConfigureTravel 로 넘겨준다.

	/** 접속/이동 실패 사유를 화면에 띄운다. UServerSubsystem::OnTravelFailed 구독. */
	void HandleTravelFailed(const FString& Reason);

	FDelegateHandle TravelFailedHandle;

	UServerSubsystem* GetServerSubsystem() const;

	UPROPERTY()
	TObjectPtr<URoomCreateWidgetBase> RoomCreateWidget;

	UPROPERTY()
	TObjectPtr<URoomListWidgetBase> RoomListWidget;

	UPROPERTY()
	EMOULobbyUIState UIState = EMOULobbyUIState::MainMenu;

	/** 내가 만든 방의 비밀번호. GetMyRoomPassword() 참고. */
	FString MyRoomPassword;

	/** 참여자로 들어갈 때 입력한 방 비밀번호. 여행 URL 에 다시 실어야 한다. */
	FString JoinedRoomPassword;

	bool bSubscribed = false;
};
