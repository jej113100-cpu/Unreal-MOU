// Copyright Epic Games, Inc. All Rights Reserved.


#include "TeamProject_MOUPlayerController.h"
#include "Subsystems/WarehouseDataSubsystem.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "TeamProject_MOU.h"
#include "Widgets/Input/SVirtualJoystick.h"

// 로그인 화면 자동 표시를 위해 포함한다. Chat 서브시스템은 이 컨트롤러를 몰라도
// 되지만(느슨한 결합), 컨트롤러는 "게임이 시작되면 로그인 화면부터 띄운다" 는
// 정책을 알아야 하므로 여기서만 의존한다.
#include "Server/ServerSubsystem.h"
#include "Server/Lobby/LoginWidgetBase.h"
#include "Engine/GameInstance.h"

// 음성 RPC 창구. 컨트롤러는 음성 시스템의 내부를 몰라도 되지만,
// "모든 컨트롤러가 음성 창구를 하나씩 갖는다" 는 것은 컨트롤러의 책임이다
// (채팅 로그인 위젯을 여기서 띄우는 것과 같은 이유).
#include "Voice/VoiceComponent.h"

// 마이크/무전기 상태 표시. 로그인 위젯과 같은 이유로 여기서만 의존한다 -
// 위젯은 누가 자기를 띄우는지 몰라야 하고, 띄우는 정책은 컨트롤러 몫이다.
#include "Voice/RadioStatusWidget.h"
#include "Voice/VoiceStatusWidget.h"
#include "Player/MainCharacter.h"
#include "UI/MOU_CharacterStatusHUD.h"
#include "UI/SpectatorOverlayWidget.h"
#include "EnhancedInputComponent.h"
#include "EngineUtils.h"
#include "Blueprint/WidgetTree.h"

ATeamProject_MOUPlayerController::ATeamProject_MOUPlayerController()
{
	// ★ 생성자에서 만들어야 서버와 클라이언트가 같은 컴포넌트를 갖는다.
	//   이유는 헤더의 VoiceComponent 주석 참고.
	VoiceComponent = CreateDefaultSubobject<UVoiceComponent>(TEXT("MOUVoiceComponent"));
}

void ATeamProject_MOUPlayerController::ServerSaveWarehouseDelivery_Implementation(
	const TArray<FStoredItemData>& RequestedItems)
{
	UWarehouseDataSubsystem* Warehouse = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWarehouseDataSubsystem>() : nullptr;
	const bool bSucceeded = Warehouse && Warehouse->SavePendingDeliveryDataFromRequest(RequestedItems);
	ClientWarehouseDeliverySaveCompleted(bSucceeded);
}

void ATeamProject_MOUPlayerController::ClientWarehouseDeliverySaveCompleted_Implementation(bool bSucceeded)
{
	OnWarehouseDeliverySaveCompleted.Broadcast(bSucceeded);
}

void ATeamProject_MOUPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// only spawn touch controls on local player controllers
	if (IsLocalPlayerController() && ShouldUseTouchControls())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogTeamProject_MOU, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}

	ShowLoginWidgetIfNeeded();
	ShowVoiceWidgetsIfNeeded();
}

void ATeamProject_MOUPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}

		if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent))
		{
			if (IA_SpectateNext)
			{
				EnhancedInputComponent->BindAction(IA_SpectateNext, ETriggerEvent::Started, this, &ATeamProject_MOUPlayerController::SpectateNextPlayer);
			}
			if (IA_SpectatePrev)
			{
				EnhancedInputComponent->BindAction(IA_SpectatePrev, ETriggerEvent::Started, this, &ATeamProject_MOUPlayerController::SpectatePrevPlayer);
			}
		}
	}
}

// 차량 운전 모드로 전환: 캐릭터 IMC를 모두 제거하고 차량 IMC 하나만 남긴다.
// (캐릭터 IMC가 남아 있으면 W/A/S/D 같은 공용 키를 먼저 소비해 차량 액션까지 도달하지 못한다.)
void ATeamProject_MOUPlayerController::SwitchToVehicleInput(UInputMappingContext* DrivingContext)
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (!Subsystem)
	{
		return;
	}

	// 캐릭터용 IMC 전부 제거 (SetupInputComponent 에서 추가한 것과 동일 목록).
	for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
	{
		if (CurrentContext)
		{
			Subsystem->RemoveMappingContext(CurrentContext);
		}
	}
	for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
	{
		if (CurrentContext)
		{
			Subsystem->RemoveMappingContext(CurrentContext);
		}
	}

	// 차량 IMC 추가.
	if (DrivingContext)
	{
		Subsystem->AddMappingContext(DrivingContext, 0);
		ActiveVehicleContext = DrivingContext;
	}
}

// 차량 운전 모드 해제: 차량 IMC를 제거하고 캐릭터 IMC를 원래대로 복원한다.
void ATeamProject_MOUPlayerController::RestoreCharacterInput()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (!Subsystem)
	{
		return;
	}

	// 차량 IMC 제거.
	if (ActiveVehicleContext)
	{
		Subsystem->RemoveMappingContext(ActiveVehicleContext);
		ActiveVehicleContext = nullptr;
	}

	// 캐릭터용 IMC 복원 (SetupInputComponent 와 동일 규칙).
	for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
	{
		if (CurrentContext)
		{
			Subsystem->AddMappingContext(CurrentContext, 0);
		}
	}
	if (!ShouldUseTouchControls())
	{
		for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
		{
			if (CurrentContext)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}
		}
	}
}

bool ATeamProject_MOUPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

void ATeamProject_MOUPlayerController::ShowLoginWidgetIfNeeded()
{
	if (!bAutoShowLoginWidget || !IsLocalPlayerController())
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UServerSubsystem* Chat = GameInstance ? GameInstance->GetSubsystem<UServerSubsystem>() : nullptr;
	if (Chat == nullptr)
	{
		UE_LOG(LogTeamProject_MOU, Warning, TEXT("채팅 서브시스템을 찾지 못해 로그인 화면을 띄우지 못했다."));
		return;
	}

	// 이미 로그인되어 있으면 다시 묻지 않는다.
	// (방장이 방을 만들고 리슨서버로 여행해온 경우 ServerSubsystem 은 GameInstance 소유라
	//  레벨을 넘어가도 로그인 상태가 그대로 살아있다.)
	if (Chat->GetConnectionState() == EChatConnectionState::LoggedIn)
	{
		return;
	}

	UClass* WidgetClass = LoginWidgetClass ? LoginWidgetClass.Get() : ULoginWidgetBase::StaticClass();
	ULoginWidgetBase* LoginWidget = CreateWidget<ULoginWidgetBase>(this, WidgetClass);
	if (LoginWidget == nullptr)
	{
		return;
	}

	// 비워두면 위젯이 설정(Config/DefaultGame.ini)에서 읽는다. 컨트롤러가 굳이
	// 기본 주소를 알 필요는 없으므로, 예외적으로 지정했을 때만 덮어쓴다.
	LoginWidget->ServerHost = ServerHostOverride;
	LoginWidget->ServerPort = ServerPortOverride;
	LoginWidget->AddToViewport();

	// 로그인 화면은 마우스로 조작하므로 커서를 켜준다. NativeConstruct 가 입력 모드까지
	// 바꾸지는 않으므로(위젯은 게임 흐름을 몰라도 되게 만들었다) 여기서 챙긴다.
	SetShowMouseCursor(true);
}

// ---------------------------------------------------------------------------
// 마이크 / 무전기 상태 표시
//
// ★ ZOrder 를 로그인 위젯보다 낮게 둔다(기본 0). 로그인 화면이 떠 있는 동안
//   마이크 아이콘이 그 위를 덮으면 안 되기 때문이다.
//
// ★ 두 위젯 모두 "지금 상태" 를 스스로 폴링한다. 마이크가 없든 무전기가 없든
//   위젯 쪽에서 알아서 처리하므로(무전기는 스스로 접힌다), 여기서 조건을
//   따져 띄울지 말지 고르지 않는다 - 그 판단이 두 군데로 갈라지면 어긋난다.
// ---------------------------------------------------------------------------

void ATeamProject_MOUPlayerController::ShowVoiceWidgetsIfNeeded()
{
	// ★ 로컬 컨트롤러가 아니면 만들지 않는다. 서버가 남의 컨트롤러에도 만들면
	//   화면에는 안 보이는데 NativeTick 만 도는 위젯이 사람 수만큼 생긴다.
	if (!bAutoShowVoiceWidgets || !IsLocalPlayerController())
	{
		return;
	}

	// --- 마이크 -------------------------------------------------------------
	//
	// 이건 끌 수 있는 장식이 아니라 프라이버시 표시다(15절). 클래스를 안 넣어도
	// C++ 기본 레이아웃으로라도 반드시 뜬다.
	if (VoiceStatusWidget == nullptr)
	{
		UClass* WidgetClass = VoiceStatusWidgetClass
			? VoiceStatusWidgetClass.Get()
			: UVoiceStatusWidget::StaticClass();

		VoiceStatusWidget = CreateWidget<UVoiceStatusWidget>(this, WidgetClass);

		if (VoiceStatusWidget != nullptr)
		{
			VoiceStatusWidget->AddToViewport();
		}
		else
		{
			UE_LOG(LogTeamProject_MOU, Error,
				TEXT("마이크 상태 위젯을 만들지 못했다. VoiceStatusWidgetClass 가 UVoiceStatusWidget 을 상속하는지 확인할 것."));
		}
	}

	// --- 무전기 -------------------------------------------------------------
	//
	// 무전기가 없어도 띄운다. 위젯이 bHideWhenNoRadio 로 스스로 접히고,
	// 무전기를 줍는 순간 알아서 다시 나타난다 - 아이템을 줍고 버리는 시점마다
	// 여기서 만들고 부수면 그 타이밍을 놓치는 경로가 반드시 생긴다.
	if (RadioStatusWidget == nullptr)
	{
		UClass* WidgetClass = RadioStatusWidgetClass
			? RadioStatusWidgetClass.Get()
			: URadioStatusWidget::StaticClass();

		RadioStatusWidget = CreateWidget<URadioStatusWidget>(this, WidgetClass);

		if (RadioStatusWidget != nullptr)
		{
			RadioStatusWidget->AddToViewport();
		}
		else
		{
			UE_LOG(LogTeamProject_MOU, Error,
				TEXT("무전기 상태 위젯을 만들지 못했다. RadioStatusWidgetClass 가 URadioStatusWidget 을 상속하는지 확인할 것."));
		}
	}
}

void ATeamProject_MOUPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (!IsLocalPlayerController())
	{
		return;
	}

	AActor* CurrentVT = GetViewTarget();
	if (CurrentVT != LastViewTarget.Get())
	{
		LastViewTarget = CurrentVT;
		APawn* MyPawn = GetPawn();
		const bool bIsSelf = (CurrentVT == MyPawn && MyPawn != nullptr);

		OnViewTargetActorChanged.Broadcast(CurrentVT, bIsSelf);
		UE_LOG(LogTemp, Log, TEXT("[Camera] ViewTarget Changed to: %s (bIsSelf: %d)"), *GetNameSafe(CurrentVT), bIsSelf ? 1 : 0);

		if (!bIsSpectating)
		{
			if (!bIsSelf)
			{
				SetInGameUIHidden(true);
			}
			else
			{
				AMainCharacter* MainChar = Cast<AMainCharacter>(MyPawn);
				if (MainChar && !MainChar->bIsDead)
				{
					SetInGameUIHidden(false);
					if (StatusHUDWidget)
					{
						StatusHUDWidget->BindToCharacter(MainChar);
					}
				}
			}
		}
	}

	if (bIsSpectating)
	{
		CheckSpectateTargetAlive();
	}
}

TArray<AMainCharacter*> ATeamProject_MOUPlayerController::GetAliveTeammates() const
{
	TArray<AMainCharacter*> AliveList;
	UWorld* World = GetWorld();
	if (!World)
	{
		return AliveList;
	}

	APawn* MyPawn = GetPawn();
	for (TActorIterator<AMainCharacter> It(World); It; ++It)
	{
		AMainCharacter* Char = *It;
		if (!Char || Char == MyPawn)
		{
			continue;
		}
		if (!Char->IsPlayerControlled())
		{
			continue;
		}
		if (Char->bIsDead)
		{
			continue;
		}

		AliveList.Add(Char);
	}
	return AliveList;
}

void ATeamProject_MOUPlayerController::SpectateNextPlayer()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	TArray<AMainCharacter*> AliveList = GetAliveTeammates();
	if (AliveList.Num() == 0)
	{
		StopSpectating();
		return;
	}

	CurrentSpectateIndex++;
	if (CurrentSpectateIndex >= AliveList.Num())
	{
		CurrentSpectateIndex = 0;
	}

	SetSpectateTarget(AliveList[CurrentSpectateIndex]);
}

void ATeamProject_MOUPlayerController::SpectatePrevPlayer()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	TArray<AMainCharacter*> AliveList = GetAliveTeammates();
	if (AliveList.Num() == 0)
	{
		StopSpectating();
		return;
	}

	CurrentSpectateIndex--;
	if (CurrentSpectateIndex < 0)
	{
		CurrentSpectateIndex = AliveList.Num() - 1;
	}

	SetSpectateTarget(AliveList[CurrentSpectateIndex]);
}

void ATeamProject_MOUPlayerController::SetSpectateTarget(AMainCharacter* NewTarget, float BlendTime)
{
	if (!NewTarget || NewTarget->bIsDead)
	{
		return;
	}

	CurrentSpectateTarget = NewTarget;

	SetViewTargetWithBlend(NewTarget, BlendTime, EViewTargetBlendFunction::VTBlend_EaseInOut, 2.0f, true);

	if (StatusHUDWidget)
	{
		StatusHUDWidget->BindToCharacter(NewTarget);
		StatusHUDWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	if (PlayerHUDWidget)
	{
		PlayerHUDWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	UpdateSpectatorOverlay();
}

void ATeamProject_MOUPlayerController::StartSpectating()
{
	if (!IsLocalPlayerController() || bIsSpectating)
	{
		return;
	}

	bIsSpectating = true;

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (SpectatorMappingContext)
		{
			Subsystem->AddMappingContext(SpectatorMappingContext, SpectatorMappingPriority);
		}
	}

	ShowSpectatorOverlay();

	TArray<AMainCharacter*> AliveList = GetAliveTeammates();
	if (AliveList.Num() > 0)
	{
		CurrentSpectateIndex = 0;
		SetSpectateTarget(AliveList[0], 0.0f);
	}
	else
	{
		StopSpectating();
	}
}

void ATeamProject_MOUPlayerController::StopSpectating()
{
	bIsSpectating = false;
	CurrentSpectateTarget = nullptr;
	CurrentSpectateIndex = -1;

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (SpectatorMappingContext)
		{
			Subsystem->RemoveMappingContext(SpectatorMappingContext);
		}
	}

	HideSpectatorOverlay();

	if (PlayerHUDWidget)
	{
		PlayerHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (StatusHUDWidget)
	{
		StatusHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void ATeamProject_MOUPlayerController::CheckSpectateTargetAlive()
{
	if (!CurrentSpectateTarget.IsValid() || CurrentSpectateTarget->bIsDead)
	{
		SpectateNextPlayer();
	}
}

void ATeamProject_MOUPlayerController::StartDeathSpectatorSequence()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	SetInGameUIHidden(true);
	ShowTurnOffDisplay();

	GetWorldTimerManager().ClearTimer(SpectatorTransitionTimerHandle);
	if (DeathSpectatorDelay > 0.0f)
	{
		GetWorldTimerManager().SetTimer(SpectatorTransitionTimerHandle, this,
			&ATeamProject_MOUPlayerController::OnTurnOffDisplayFinished,
			DeathSpectatorDelay, false);
	}
}

void ATeamProject_MOUPlayerController::OnTurnOffDisplayFinished()
{
	GetWorldTimerManager().ClearTimer(SpectatorTransitionTimerHandle);
	HideTurnOffDisplay();
	StartSpectating();
}

void ATeamProject_MOUPlayerController::ShowTurnOffDisplay()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (!TurnOffDisplayWidget && TurnOffDisplayWidgetClass)
	{
		TurnOffDisplayWidget = CreateWidget<UUserWidget>(this, TurnOffDisplayWidgetClass);
	}

	if (TurnOffDisplayWidget && !TurnOffDisplayWidget->IsInViewport())
	{
		TurnOffDisplayWidget->AddToViewport(1000);
	}
}

void ATeamProject_MOUPlayerController::HideTurnOffDisplay()
{
	if (TurnOffDisplayWidget && TurnOffDisplayWidget->IsInViewport())
	{
		TurnOffDisplayWidget->RemoveFromParent();
	}
}

void ATeamProject_MOUPlayerController::ShowSpectatorOverlay()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (!SpectatorOverlayWidget && SpectatorOverlayWidgetClass)
	{
		SpectatorOverlayWidget = CreateWidget<USpectatorOverlayWidget>(this, SpectatorOverlayWidgetClass);
	}

	if (SpectatorOverlayWidget && !SpectatorOverlayWidget->IsInViewport())
	{
		SpectatorOverlayWidget->AddToViewport(500);
	}

	UpdateSpectatorOverlay();
}

void ATeamProject_MOUPlayerController::HideSpectatorOverlay()
{
	if (SpectatorOverlayWidget && SpectatorOverlayWidget->IsInViewport())
	{
		SpectatorOverlayWidget->RemoveFromParent();
	}
}

void ATeamProject_MOUPlayerController::UpdateSpectatorOverlay()
{
	if (SpectatorOverlayWidget)
	{
		TArray<AMainCharacter*> AliveList = GetAliveTeammates();
		SpectatorOverlayWidget->SetSpectatorInfo(CurrentSpectateTarget.Get(), AliveList.Num());
	}
}

void ATeamProject_MOUPlayerController::RegisterStatusHUDWidget(UMOU_CharacterStatusHUD* InStatusHUD)
{
	StatusHUDWidget = InStatusHUD;
	if (InStatusHUD)
	{
		if (AMainCharacter* MainChar = Cast<AMainCharacter>(GetPawn()))
		{
			InStatusHUD->BindToCharacter(MainChar);
		}
	}
}

void ATeamProject_MOUPlayerController::RegisterPlayerHUDWidget(UUserWidget* InPlayerHUD)
{
	PlayerHUDWidget = InPlayerHUD;
	if (InPlayerHUD)
	{
		if (InPlayerHUD->WidgetTree)
		{
			InPlayerHUD->WidgetTree->ForEachWidget([this](UWidget* Widget)
			{
				if (UMOU_CharacterStatusHUD* FoundStatusHUD = Cast<UMOU_CharacterStatusHUD>(Widget))
				{
					RegisterStatusHUDWidget(FoundStatusHUD);
				}
			});
		}
	}
}

void ATeamProject_MOUPlayerController::SetInGameUIHidden(bool bInHidden)
{
	if (PlayerHUDWidget)
	{
		PlayerHUDWidget->SetVisibility(bInHidden ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
	if (StatusHUDWidget)
	{
		StatusHUDWidget->SetVisibility(bInHidden ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
	OnInGameUIVisibilityChanged(!bInHidden);
}
