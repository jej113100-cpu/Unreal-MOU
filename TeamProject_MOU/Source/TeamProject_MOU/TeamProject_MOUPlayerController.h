// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Item/DeliveryData.h"
#include "TeamProject_MOUPlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class UUserWidget;
class ULoginWidgetBase;
class URadioStatusWidget;
class UVoiceComponent;
class UVoiceStatusWidget;
class AMainCharacter;
class UMOU_CharacterStatusHUD;
class USpectatorOverlayWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWarehouseDeliverySaveCompleted, bool, bSucceeded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnViewTargetActorChanged, AActor*, NewViewTarget, bool, bIsSelf);

/**
 *  Basic PlayerController class for a third person game
 *  Manages input mappings
 */
UCLASS(abstract)
class ATeamProject_MOUPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATeamProject_MOUPlayerController();

	// Call on the widget's owning controller, on either host or client.
	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "Warehouse|Delivery")
	void ServerSaveWarehouseDelivery(const TArray<FStoredItemData>& RequestedItems);

	// Result only; inventory replication can arrive before or after this event.
	UPROPERTY(BlueprintAssignable, Category = "Warehouse|Delivery")
	FOnWarehouseDeliverySaveCompleted OnWarehouseDeliverySaveCompleted;

	UFUNCTION(Client, Reliable)
	void ClientWarehouseDeliverySaveCompleted(bool bSucceeded);

	// ---------------------------------------------------------
	// [차량 탑승 입력 전환] - 차량(AVehicleBase)이 탑승/하차 시 호출한다.
	// 캐릭터용 IMC(DefaultMappingContexts)를 걷어내고 차량용 IMC 하나만 남긴다.
	// 이 프로젝트는 입력 IMC를 컨트롤러가 관리하므로, 차량 입력 전환도 여기서 처리한다.
	// ---------------------------------------------------------

	// 차량 운전 모드로 전환: 캐릭터 IMC 제거 + 지정한 차량 IMC 추가.
	UFUNCTION(BlueprintCallable, Category = "Input|Vehicle")
	void SwitchToVehicleInput(UInputMappingContext* DrivingContext);

	// 차량 운전 모드 해제: 차량 IMC 제거 + 캐릭터 IMC(DefaultMappingContexts) 복원.
	UFUNCTION(BlueprintCallable, Category = "Input|Vehicle")
	void RestoreCharacterInput();

private:
	// SwitchToVehicleInput 으로 추가한 차량 IMC. RestoreCharacterInput 에서 제거하려고 기억한다.
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> ActiveVehicleContext;

public:
	UPROPERTY(BlueprintAssignable, Category = "Camera")
	FOnViewTargetActorChanged OnViewTargetActorChanged;

	UFUNCTION(BlueprintCallable, Category = "Spectator")
	void SpectateNextPlayer();

	UFUNCTION(BlueprintCallable, Category = "Spectator")
	void SpectatePrevPlayer();

	UFUNCTION(BlueprintCallable, Category = "Spectator")
	void SetSpectateTarget(AMainCharacter* NewTarget, float BlendTime = 0.25f);

	UFUNCTION(BlueprintPure, Category = "Spectator")
	TArray<AMainCharacter*> GetAliveTeammates() const;

	UFUNCTION(BlueprintPure, Category = "Spectator")
	AMainCharacter* GetCurrentSpectateTarget() const { return CurrentSpectateTarget.Get(); }

	UFUNCTION(BlueprintPure, Category = "Spectator")
	bool IsSpectating() const { return bIsSpectating; }

	UFUNCTION(BlueprintCallable, Category = "Spectator")
	void StartSpectating();

	UFUNCTION(BlueprintCallable, Category = "Spectator")
	void StopSpectating();

	UFUNCTION(BlueprintCallable, Category = "UI|Spectator")
	void StartDeathSpectatorSequence();

	UFUNCTION(BlueprintCallable, Category = "UI|Spectator")
	void OnTurnOffDisplayFinished();

	UFUNCTION(BlueprintCallable, Category = "UI|Spectator")
	void ShowTurnOffDisplay();

	UFUNCTION(BlueprintCallable, Category = "UI|Spectator")
	void HideTurnOffDisplay();

	UFUNCTION(BlueprintCallable, Category = "UI|Spectator")
	void ShowSpectatorOverlay();

	UFUNCTION(BlueprintCallable, Category = "UI|Spectator")
	void HideSpectatorOverlay();

	UFUNCTION(BlueprintCallable, Category = "UI|Status")
	void RegisterStatusHUDWidget(UMOU_CharacterStatusHUD* InStatusHUD);

	UFUNCTION(BlueprintCallable, Category = "UI")
	void RegisterPlayerHUDWidget(UUserWidget* InPlayerHUD);

	UFUNCTION(BlueprintCallable, Category = "UI|Status")
	void SetInGameUIHidden(bool bInHidden);

	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void OnInGameUIVisibilityChanged(bool bVisible);

protected:
	/**
	 * 음성 송수신 창구 (VOICE_INTEGRATION.md 6절).
	 *
	 * ★ 런타임에 붙이지 않고 **생성자에서** 만드는 것이 중요하다.
	 *   RPC 는 보내는 쪽과 받는 쪽에 같은 컴포넌트가 있어야 목적지를 찾는다.
	 *   생성자에서 만들면 서버와 모든 클라이언트가 똑같이 갖게 되지만,
	 *   나중에 붙이면 한쪽에만 있는 순간이 생겨 **RPC 가 조용히 사라진다.**
	 *
	 *   폰이 아니라 컨트롤러에 두는 이유는 VoiceComponent.h 상단 주석에 있다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MOU|Voice")
	TObjectPtr<UVoiceComponent> VoiceComponent;


	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/**
	 * PIE/게임 시작 시 채팅 로그인 화면을 자동으로 띄울지.
	 *
	 * 이미 로그인되어 있으면(예: 방장이 방을 만들고 리슨서버로 여행해온 경우)
	 * 다시 묻지 않는다 — ShowLoginWidgetIfNeeded() 가 UServerSubsystem 의 연결 상태로 판단한다.
	 */
	UPROPERTY(EditAnywhere, Category = "MOU|Chat")
	bool bAutoShowLoginWidget = true;

	/** 자동으로 띄울 로그인 위젯 클래스. 비워두면 ULoginWidgetBase 의 C++ 기본 레이아웃을 쓴다. */
	UPROPERTY(EditAnywhere, Category = "MOU|Chat")
	TSubclassOf<ULoginWidgetBase> LoginWidgetClass;

	/**
	 * 이 컨트롤러만 다른 채팅 서버를 보게 할 때 쓰는 **예외용** 값. 평소에는 비워둔다.
	 *
	 * 비어 있으면 Config/DefaultGame.ini 의 팀 공유 주소(UMOUServerSettings)를 쓴다.
	 * 예전에는 여기에 127.0.0.1 이 박혀 있었는데, 그 값은 "이 게임이 돌고 있는 PC" 라는
	 * 뜻이라 서버를 켜지 않은 팀원은 자기 자신에게 접속하려다 항상 실패했다.
	 * 그래서 기본값을 없애고, 주소를 아는 곳을 설정 한 군데로 모았다.
	 */
	UPROPERTY(EditAnywhere, Category = "MOU|Chat")
	FString ServerHostOverride;

	/** 0 이면 ServerHostOverride 와 마찬가지로 설정값을 쓴다. */
	UPROPERTY(EditAnywhere, Category = "MOU|Chat")
	int32 ServerPortOverride = 0;

	// --- 음성/무전 상태 표시 -------------------------------------------------
	//
	// ★ 여기서 띄우는 이유는 로그인 위젯과 같다. 위젯 자신은 누가 자기를
	//   띄우는지 몰라야 하고(그래야 콘솔로도, 레벨 BP 로도 띄울 수 있다),
	//   "게임이 시작되면 마이크 상태부터 화면에 올린다" 는 **정책**은
	//   컨트롤러의 책임이다.
	//
	//   특히 마이크 표시는 VOICE_INTEGRATION.md 15절의 프라이버시 요구사항이라
	//   ("마이크가 열려 있는 동안 화면에 항상 표시한다") 콘솔 명령에 맡겨둘 수
	//   없다 - 아무도 안 치면 요구사항이 지켜지지 않는다.

	/**
	 * 게임 시작 시 마이크/무전기 상태 위젯을 자동으로 띄울지.
	 *
	 * ★ 끄는 것은 **UI 를 직접 조립하는 HUD 를 따로 만들 때**뿐이다. 그 경우
	 *   그쪽에서 같은 위젯을 반드시 띄워야 한다(마이크 표시는 선택이 아니다).
	 */
	UPROPERTY(EditAnywhere, Category = "MOU|Voice")
	bool bAutoShowVoiceWidgets = true;

	/**
	 * 띄울 마이크 상태 위젯 클래스. **여기에 WBP 를 넣으면 된다.**
	 *
	 * 비워두면 UVoiceStatusWidget 의 C++ 기본 레이아웃(우상단 텍스트)이 뜬다 -
	 * LoginWidgetClass 와 같은 규칙이라, 아트가 아직 없어도 게임은 돈다.
	 */
	UPROPERTY(EditAnywhere, Category = "MOU|Voice", meta = (EditCondition = "bAutoShowVoiceWidgets"))
	TSubclassOf<UVoiceStatusWidget> VoiceStatusWidgetClass;

	/** 띄울 무전기 상태 위젯 클래스. 비워두면 C++ 기본 레이아웃(우하단 텍스트). */
	UPROPERTY(EditAnywhere, Category = "MOU|Voice", meta = (EditCondition = "bAutoShowVoiceWidgets"))
	TSubclassOf<URadioStatusWidget> RadioStatusWidgetClass;

	/**
	 * 위 둘의 인스턴스. **UPROPERTY 로 들고 있어야 GC 가 안 물어간다.**
	 *
	 * AddToViewport 만으로도 뷰포트가 참조를 잡지만, 나중에 RemoveFromParent
	 * 하는 순간 주인이 사라진다. MobileControlsWidget 과 같은 이유로 들고 있는다.
	 */
	UPROPERTY()
	TObjectPtr<UVoiceStatusWidget> VoiceStatusWidget;

	UPROPERTY()
	TObjectPtr<URadioStatusWidget> RadioStatusWidget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Spectator")
	TObjectPtr<UInputMappingContext> SpectatorMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Spectator")
	TObjectPtr<UInputAction> IA_SpectateNext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Spectator")
	TObjectPtr<UInputAction> IA_SpectatePrev;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Spectator")
	int32 SpectatorMappingPriority = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Spectator")
	TSubclassOf<UUserWidget> TurnOffDisplayWidgetClass;

	UPROPERTY(BlueprintReadWrite, Category = "UI|Spectator")
	TObjectPtr<UUserWidget> TurnOffDisplayWidget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Spectator")
	TSubclassOf<USpectatorOverlayWidget> SpectatorOverlayWidgetClass;

	UPROPERTY(BlueprintReadOnly, Category = "UI|Spectator")
	TObjectPtr<USpectatorOverlayWidget> SpectatorOverlayWidget;

	UPROPERTY(BlueprintReadWrite, Category = "UI|Status")
	TObjectPtr<UMOU_CharacterStatusHUD> StatusHUDWidget;

	UPROPERTY(BlueprintReadWrite, Category = "UI")
	TObjectPtr<UUserWidget> PlayerHUDWidget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Spectator")
	float DeathSpectatorDelay = 3.0f;

	FTimerHandle SpectatorTransitionTimerHandle;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	virtual void PlayerTick(float DeltaTime) override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

private:
	TWeakObjectPtr<AMainCharacter> CurrentSpectateTarget;
	int32 CurrentSpectateIndex = -1;
	bool bIsSpectating = false;

	TWeakObjectPtr<AActor> LastViewTarget;

	void UpdateSpectatorOverlay();
	void CheckSpectateTargetAlive();

	/** bAutoShowLoginWidget 이 켜져 있고 아직 로그인 전이면 로그인 위젯을 띄운다. */
	void ShowLoginWidgetIfNeeded();

	/**
	 * 마이크/무전기 상태 위젯을 띄운다. 이미 떠 있으면 아무것도 안 한다.
	 *
	 * ★ 로컬 컨트롤러에서만 뜬다. 서버가 남의 컨트롤러에도 만들면 화면에는
	 *   안 보이는데 NativeTick 만 도는 위젯이 사람 수만큼 생긴다.
	 */
	void ShowVoiceWidgetsIfNeeded();

};
