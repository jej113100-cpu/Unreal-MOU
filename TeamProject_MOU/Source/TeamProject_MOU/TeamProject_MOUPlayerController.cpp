// Copyright Epic Games, Inc. All Rights Reserved.


#include "TeamProject_MOUPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "TeamProject_MOU.h"
#include "Widgets/Input/SVirtualJoystick.h"

// 로그인 화면 자동 표시를 위해 포함한다. Chat 서브시스템은 이 컨트롤러를 몰라도
// 되지만(느슨한 결합), 컨트롤러는 "게임이 시작되면 로그인 화면부터 띄운다" 는
// 정책을 알아야 하므로 여기서만 의존한다.
#include "Chat/ChatSubsystem.h"
#include "Chat/LoginWidgetBase.h"
#include "Engine/GameInstance.h"

// 음성 RPC 창구. 컨트롤러는 음성 시스템의 내부를 몰라도 되지만,
// "모든 컨트롤러가 음성 창구를 하나씩 갖는다" 는 것은 컨트롤러의 책임이다
// (채팅 로그인 위젯을 여기서 띄우는 것과 같은 이유).
#include "Voice/VoiceComponent.h"

ATeamProject_MOUPlayerController::ATeamProject_MOUPlayerController()
{
	// ★ 생성자에서 만들어야 서버와 클라이언트가 같은 컴포넌트를 갖는다.
	//   이유는 헤더의 VoiceComponent 주석 참고.
	VoiceComponent = CreateDefaultSubobject<UVoiceComponent>(TEXT("MOUVoiceComponent"));
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
	UChatSubsystem* Chat = GameInstance ? GameInstance->GetSubsystem<UChatSubsystem>() : nullptr;
	if (Chat == nullptr)
	{
		UE_LOG(LogTeamProject_MOU, Warning, TEXT("채팅 서브시스템을 찾지 못해 로그인 화면을 띄우지 못했다."));
		return;
	}

	// 이미 로그인되어 있으면 다시 묻지 않는다.
	// (방장이 방을 만들고 리슨서버로 여행해온 경우 ChatSubsystem 은 GameInstance 소유라
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
