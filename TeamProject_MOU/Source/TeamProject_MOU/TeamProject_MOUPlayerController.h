// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Item/DeliveryData.h"
#include "Item/TerminalShopTypes.h"
#include "TeamProject_MOUPlayerController.generated.h"

class UDataTable;
class UInputMappingContext;
class UInputAction;
class UUserWidget;
class URadioStatusWidget;
class UVoiceComponent;
class UVoiceStatusWidget;
class AMainCharacter;
class UMOU_CharacterStatusHUD;
class USpectatorOverlayWidget;
class UInGameMenuWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWarehouseDeliverySaveCompleted, bool, bSucceeded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTerminalPurchaseCompleted, bool, bSucceeded, const FText&, ErrorMessage);
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
	// [LOBBYLOAD-007] 소유 클라이언트가 최초 로비 플레이 준비 완료를 서버에 보고합니다.
	UFUNCTION(Server, Reliable)
	void ServerReportLobbyEntryReady(int64 UserId);
	// [LOBBYLOAD-008] 전원 준비 상태를 확인하여 로딩 표시와 로컬 조작 잠금을 갱신합니다.
	void UpdateLobbyEntryWait();
	// [LOBBYLOAD-009] 입장 대기 중 이동·점프·상호작용을 포함한 게임 입력을 차단합니다.
	virtual void BuildInputStack(TArray<UInputComponent*>& InputStack) override;
	// [LOBBYLOAD-010] 소유 Pawn과 필수 복제 데이터가 준비되었는지 검사합니다.
	UFUNCTION(BlueprintNativeEvent, Category = "Loading|LobbyEntry")
	bool IsLobbyEntryLocallyReady() const;
	bool bLobbyEntryInputLocked = false;
	double NextLobbyEntryReadyReport = 0.0;
    // [LATEJOIN-014] 호스트가 생존 관전 대상을 선택하여 먼 거리의 대상도 복제되게 합니다.
    UFUNCTION(Server, Reliable)
    void ServerCycleLateJoinTarget(int32 Direction);
    // 서버에서 결정하며 맵의 정상적인 이동 동안 유지합니다.
    // [LATEJOIN-007] 심리스 이동으로 컨트롤러가 교체되어도 합류 제한을 보존합니다.
    virtual void SeamlessTravelTo(APlayerController* NewPC) override;
    FString PlaySessionId;
    UPROPERTY(Replicated)
    bool bWaitForSafeLobby = false;
    // [LATEJOIN-004] 도중 합류의 관전 제한을 소유 클라이언트로 복제합니다.
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	ATeamProject_MOUPlayerController();

	// [SETTLEMENT-000] 정산 UI의 확인 상태를 서버에 전달합니다.
	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "Run|Settlement")
	void ServerSetSettlementConfirmed(bool bConfirmed);

	// Call on the widget's owning controller, on either host or client.
	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "Warehouse|Delivery")
	void ServerSaveWarehouseDelivery(const TArray<FStoredItemData>& RequestedItems);

	// Add button: reserve immediately; the Done button must not save this list again.
	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "Warehouse|Delivery")
	void ServerAddWarehouseDeliveryItem(TSubclassOf<AItemBase> ItemClass, int32 Quantity = 1);

	UPROPERTY(BlueprintAssignable, Category = "Warehouse|Delivery")
	FOnWarehouseDeliverySaveCompleted OnWarehouseDeliveryAddCompleted;

	UFUNCTION(Client, Reliable)
	void ClientWarehouseDeliveryAddCompleted(bool bSucceeded);

	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "Warehouse|Delivery")
	void ServerRemoveWarehouseDeliveryItem(TSubclassOf<AItemBase> ItemClass, int32 Quantity = 1);

	UPROPERTY(BlueprintAssignable, Category = "Warehouse|Delivery")
	FOnWarehouseDeliverySaveCompleted OnWarehouseDeliveryRemoveCompleted;

	UFUNCTION(Client, Reliable)
	void ClientWarehouseDeliveryRemoveCompleted(bool bSucceeded);

	// Result only; inventory replication can arrive before or after this event.
	UPROPERTY(BlueprintAssignable, Category = "Warehouse|Delivery")
	FOnWarehouseDeliverySaveCompleted OnWarehouseDeliverySaveCompleted;

	UFUNCTION(Client, Reliable)
	void ClientWarehouseDeliverySaveCompleted(bool bSucceeded);

	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "Economy")
	void ServerSpendGold(int32 Amount);

	// [TSHOP-015] 구매 요청을 서버에서 검증하고 골드 차감과 공용 창고 저장을 확정합니다.
	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "Terminal Shop")
	void ServerRequestTerminalPurchase(const TArray<FTerminalCartItem>& Items);

	UFUNCTION(Client, Reliable, Category = "Terminal Shop")
	void ClientTerminalPurchaseCompleted(bool bSucceeded, const FText& ErrorMessage);

	UPROPERTY(BlueprintAssignable, Category = "Terminal Shop")
	FOnTerminalPurchaseCompleted OnTerminalPurchaseCompleted;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Terminal Shop")
	TObjectPtr<UDataTable> TerminalShopItemTable;

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
	// [LATEJOIN-009] 다음 생존자를 관전하며 도중 합류자는 대상이 없어도 대기합니다.
	void SpectateNextPlayer();

	UFUNCTION(BlueprintCallable, Category = "Spectator")
	// [LATEJOIN-010] 이전 생존자를 관전하며 도중 합류자는 대상이 없어도 대기합니다.
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
	// [LATEJOIN-008] 사망 또는 도중 합류 관전을 시작하고 대상 복제를 기다립니다.
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

	// [PCUI-002] HUD 내부의 상태·마이크·무전기 위젯을 찾아 컨트롤러 참조에 연결한다.
	UFUNCTION(BlueprintCallable, Category = "UI")
	void RegisterPlayerHUDWidget(UUserWidget* InPlayerHUD);

	UFUNCTION(BlueprintCallable, Category = "UI|Status")
	void SetInGameUIHidden(bool bInHidden);

	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void OnInGameUIVisibilityChanged(bool bVisible);

	// --- 인게임 메뉴 (ESC 일시정지) -------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "UI|InGameMenu")
	void ToggleInGameMenu();

	UFUNCTION(BlueprintCallable, Category = "UI|InGameMenu")
	void OpenInGameMenu();

	UFUNCTION(BlueprintCallable, Category = "UI|InGameMenu")
	void CloseInGameMenu();

	UFUNCTION(BlueprintPure, Category = "UI|InGameMenu")
	bool IsInGameMenuOpen() const { return bIsInGameMenuOpen; }

	// [LOBBYRETURN-001] 로그인 연결은 유지하고 방을 나간 뒤 메인로비 레벨로 이동한다.
	UFUNCTION(BlueprintCallable, Category = "UI|InGameMenu")
	void ReturnToLobby();

	// [REJOIN-006] 서버 연결을 정리하여 퇴장 처리를 유도한 뒤 게임을 종료한다.
	UFUNCTION(BlueprintCallable, Category = "UI|InGameMenu")
	void QuitToDesktop();

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ApplyUserSettingsToPlayer();

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

	/** PlayerHUD에 배치된 마이크 위젯. 키 설정 동기화를 위해 참조한다. */
	UPROPERTY()
	TObjectPtr<UVoiceStatusWidget> VoiceStatusWidget;

	/** PlayerHUD에 배치된 무전기 위젯. 키 설정 동기화를 위해 참조한다. */
	UPROPERTY()
	TObjectPtr<URadioStatusWidget> RadioStatusWidget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Spectator")
	TObjectPtr<UInputMappingContext> SpectatorMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Spectator")
	TObjectPtr<UInputAction> IA_SpectateNext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Spectator")
	TObjectPtr<UInputAction> IA_SpectatePrev;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Spectator")
	TObjectPtr<UInputAction> IA_SpectateLook;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Spectator")
	TObjectPtr<UInputAction> IA_SpectateZoom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Spectator")
	int32 SpectatorMappingPriority = 100;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Spectator")
	TObjectPtr<class ASpectatorCameraActor> SpectatorCameraActor;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|InGameMenu")
	TSubclassOf<UInGameMenuWidget> InGameMenuWidgetClass;

	UPROPERTY(BlueprintReadOnly, Category = "UI|InGameMenu")
	TObjectPtr<UInGameMenuWidget> InGameMenuWidget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|InGameMenu")
	TObjectPtr<UInputAction> IA_Menu;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Spectator")
	float DeathSpectatorDelay = 3.0f;

	FTimerHandle SpectatorTransitionTimerHandle;

	/** Gameplay initialization */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PreClientTravel(const FString& PendingURL, ETravelType TravelType, bool bIsSeamlessTravel) override;

	// [LATEJOIN-011] 복제된 합류 제한에 따라 관전을 시작하고 기존 카메라를 갱신합니다.
	virtual void PlayerTick(float DeltaTime) override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	void OnSpectatorLook(const struct FInputActionValue& Value);
	void OnSpectatorZoom(const struct FInputActionValue& Value);
	void OnSpectatorMouseWheel(float Val);
	void OnSpectatorTurn(float Val);
	void OnSpectatorLookUp(float Val);

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

private:
	TWeakObjectPtr<AMainCharacter> CurrentSpectateTarget;
	// 재참여 관전 대상이 준비될 때까지 서버 요청 간격을 제한합니다.
	double NextSpectateTargetRetryTime = 0.0;
	int32 CurrentSpectateIndex = -1;
	bool bIsSpectating = false;
	bool bIsDeathSequenceActive = false;

	TWeakObjectPtr<AActor> LastViewTarget;

	void UpdateSpectatorOverlay();
	void CheckSpectateTargetAlive();

	bool bIsInGameMenuOpen = false;

	// Enhanced Input 액션별 기본 키 캐시 (기본값 복원용)
	TMap<FName, FKey> DefaultKeyBindingsCache;
};
