// MOU 음성 - 무전기 상태 표시 위젯 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7-3절, 14절 V6 / Radio.h 의 규칙표

#include "Voice/RadioStatusWidget.h"

#include "Voice/Radio.h"
#include "Voice/RadioComponent.h"
#include "Voice/VoiceComponent.h"
#include "Voice/VoiceSubsystem.h"
#include "Voice/VoiceTypes.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Components/InputComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

URadioStatusWidget::URadioStatusWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 매 프레임 문자열을 새로 만들지 않으려고 RefreshInterval 로 주기를 직접
	// 조절한다. 그래도 NativeTick 자체는 켜져 있어야 그 타이머가 돈다.
	SetIsFocusable(false);
}

// ---------------------------------------------------------------------------
// 수명 주기
// ---------------------------------------------------------------------------

void URadioStatusWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
	}
}

void URadioStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// UVoiceStatusWidget 과 같은 패턴: 별도 입력 에셋 없이 소유 플레이어의
	// InputComponent 에 직접 건다. EnhancedInput 이 붙어 있어도 레거시
	// InputComponent 는 함께 살아 있으므로 충돌하지 않는다.
	if (bBindKeysToOwningPlayer)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			if (PC->InputComponent != nullptr)
			{
				// NativeConstruct 는 재호출될 수 있고, 일반 HUD 위젯 외에
				// MOU.Voice.RadioUI 디버그 위젯도 만들어질 수 있다. 둘 다
				// PlayerController 입력에 Z를 걸면 한 번의 입력으로 전원이
				// ON -> OFF가 된다. 이 플레이어에는 RadioStatusWidget 계열의
				// 키 바인딩을 항상 하나만 유지한다.
				PC->InputComponent->KeyBindings.RemoveAll([](const FInputKeyBinding& Binding)
				{
					const UObject* BoundObject = Binding.KeyDelegate.GetUObject();
					return BoundObject != nullptr && BoundObject->IsA<URadioStatusWidget>();
				});

				PC->InputComponent->BindKey(PowerToggleKey, IE_Pressed, this, &URadioStatusWidget::HandlePowerKeyPressed);

				// ★ PTT 는 누름과 뗌이 **둘 다** 필요하다. 뗌을 안 걸면 한 번
				//   누른 순간부터 영원히 송신 상태로 남는다.
				PC->InputComponent->BindKey(TransmitKey, IE_Pressed, this, &URadioStatusWidget::HandleTransmitKeyPressed);
				PC->InputComponent->BindKey(TransmitKey, IE_Released, this, &URadioStatusWidget::HandleTransmitKeyReleased);
			}
		}
	}

	TimeSinceLastRefresh = RefreshInterval; // 뜨자마자 한 번은 바로 갱신되게
	ApplyRadioState(EvaluateRadioState());
	UpdateBatteryBar();
	RefreshStatusText();
}

void URadioStatusWidget::NativeDestruct()
{
	// ★ 사라지기 전에 송신을 끊는다. 위젯이 없어지면 X 를 뗀 것을 받을 수 없어서
	//   송신이 켜진 채로 영영 남는다.
	if (UVoiceSubsystem* Voice = GetVoiceSubsystem())
	{
		Voice->SetRadioTransmitting(false);
	}

	// 위젯이 사라진 뒤에도 키 바인딩이 남아 있으면 이미 파괴된 객체를 호출한다.
	// 이 위젯이 건 바인딩만 골라서 제거한다(UVoiceStatusWidget 과 동일한 이유).
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (PC->InputComponent != nullptr)
		{
			PC->InputComponent->KeyBindings.RemoveAll([this](const FInputKeyBinding& Binding)
			{
				return Binding.KeyDelegate.GetUObject() == this;
			});
		}
	}

	Super::NativeDestruct();
}

void URadioStatusWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	TimeSinceLastRefresh += InDeltaTime;
	if (TimeSinceLastRefresh >= RefreshInterval)
	{
		TimeSinceLastRefresh = 0.f;

		ApplyRadioState(EvaluateRadioState());

		// ★ 마이크의 음량 바와 달리 배터리는 매 프레임 갱신할 이유가 없다.
		//   보간도 안 한다 - 배터리는 초당 몇 퍼센트씩 천천히 줄어드는 값이라
		//   0.1초 간격으로 넣어도 이미 부드럽다.
		UpdateBatteryBar();

		RefreshStatusText();
	}
}

// ---------------------------------------------------------------------------
// 무전기 찾기
//
// ★ 매번 새로 찾는다. 캐시하면 무전기를 떨구거나 뺏긴 순간 죽은 포인터를
//   들고 있게 되고, 무엇보다 "지금 이 순간 무전기가 있는가" 자체가 이 UI 가
//   보여줘야 하는 정보다. 부착 액터 몇 개를 훑는 비용은 0.1초에 한 번이다.
// ---------------------------------------------------------------------------

APawn* URadioStatusWidget::GetLocalPawn() const
{
	APlayerController* PC = GetOwningPlayer();
	return PC ? PC->GetPawn() : nullptr;
}

URadioComponent* URadioStatusWidget::FindLocalRadioComponent() const
{
	const APawn* Pawn = GetLocalPawn();

	if (!IsValid(Pawn))
	{
		return nullptr;
	}

	// 재귀로 훑는다. 손에 든 무전기는 캐릭터 메시의 소켓에 붙어 한 단계 더
	// 들어가 있을 수 있다 - 몇 단계인지 가정하지 않는다(ARadio::FindCarriedBy 와 동일).
	//
	// 인벤토리에 있는 것도 같이 잡힌다. AItemBase::OnUnequipped 가 플레이어에
	// 붙여두기 때문이다("투명한 주머니"). 그래서 인벤토리 구현이 바뀌어도
	// 여기가 안 깨진다.
	TArray<AActor*> Attached;
	Pawn->GetAttachedActors(Attached, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/true);

	for (AActor* Actor : Attached)
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		// ★ 클래스가 아니라 컴포넌트로 찾는다. 진짜 아이템(ARadio)과 테스트
		//   무전기(AVoiceDebugRadio)의 공통분모가 이것뿐이라서다(헤더 상단 ★).
		if (URadioComponent* Comp = Actor->FindComponentByClass<URadioComponent>())
		{
			return Comp;
		}
	}

	return nullptr;
}

ARadio* URadioStatusWidget::FindLocalRadioItem() const
{
	URadioComponent* Comp = FindLocalRadioComponent();
	return Comp ? Cast<ARadio>(Comp->GetOwner()) : nullptr;
}

UVoiceSubsystem* URadioStatusWidget::GetVoiceSubsystem() const
{
	const APlayerController* PC = GetOwningPlayer();
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UVoiceSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------
// 입력 처리
// ---------------------------------------------------------------------------

void URadioStatusWidget::HandlePowerKeyPressed()
{
	// ★ 캐시하지 않고 지금 다시 찾는다. 마지막 갱신(최대 0.1초 전) 이후에
	//   무전기를 떨궜을 수 있다.
	URadioComponent* Comp = FindLocalRadioComponent();

	if (Comp == nullptr)
	{
		return; // 없는 무전기를 켤 수는 없다. 화면에는 이미 "무전기 없음" 이 떠 있다.
	}

	// 인벤토리의 무전기는 켜진 상태에서 수신만 한다. 전원을 바꾸려면
	// 반드시 손에 다시 꺼내야 한다.
	if (!Comp->IsInHand())
	{
		RefreshStatusText();
		return;
	}

	// 진짜 아이템은 자기 힘으로 서버까지 간다(ARadio::SetPowered 가 Server RPC 를 탄다).
	if (ARadio* Item = Cast<ARadio>(Comp->GetOwner()))
	{
		Item->TogglePower();

		// 다음 주기까지 기다리지 않고 즉시 반영. 키를 눌렀는데 최대 0.1초 동안
		// 화면이 그대로면 "안 눌렸나" 하고 한 번 더 누르게 된다.
		ApplyRadioState(EvaluateRadioState());
		RefreshStatusText();
		return;
	}

	// 테스트 무전기(AVoiceDebugRadio)에는 그 경로가 없다. URadioComponent::SetPowered
	// 는 서버 전용이라 클라에서 부르면 아무 일도 안 일어나므로, 콘솔 명령이 쓰는
	// 것과 같은 디버그 RPC 로 돌린다.
	if (UVoiceSubsystem* Voice = GetVoiceSubsystem())
	{
		if (UVoiceComponent* VoiceComp = Voice->GetVoiceComponent())
		{
			VoiceComp->ServerDebugSetRadioPower(!Comp->IsPoweredOn());
		}
	}

	ApplyRadioState(EvaluateRadioState());
	RefreshStatusText();
}

void URadioStatusWidget::HandleTransmitKeyPressed()
{
	URadioComponent* Comp = FindLocalRadioComponent();

	// 인벤토리에서는 수신만 허용한다. 로컬 송신 요청 상태도 켜지 않게 해야
	// UI가 "송신 중"으로 잘못 표시되거나 의도치 않은 송신이 생기지 않는다.
	if (Comp == nullptr || !Comp->IsInHand() || !Comp->IsPoweredOn())
	{
		if (UVoiceSubsystem* Voice = GetVoiceSubsystem())
		{
			Voice->SetRadioTransmitting(false);
		}
		RefreshStatusText();
		return;
	}

	if (ARadio* Item = FindLocalRadioItem())
	{
		Item->StartTransmit();
	}
	else
	{
		// 테스트 무전기. 송신은 어차피 로컬 상태라 서브시스템을 직접 켠다
		// (ARadio::StartTransmit 이 하는 일과 똑같다).
		if (UVoiceSubsystem* Voice = GetVoiceSubsystem())
		{
			Voice->SetRadioTransmitting(true);
		}
	}

	ApplyRadioState(EvaluateRadioState());
	RefreshStatusText();
}

void URadioStatusWidget::HandleTransmitKeyReleased()
{
	// ★ 무전기를 못 찾아도 무조건 끊는다(헤더 주석). 누른 사이에 떨어뜨렸으면
	//   ARadio 를 못 찾는데, 그렇다고 안 끊으면 송신이 켜진 채로 남는다.
	if (UVoiceSubsystem* Voice = GetVoiceSubsystem())
	{
		Voice->SetRadioTransmitting(false);
	}

	ApplyRadioState(EvaluateRadioState());
	RefreshStatusText();
}

// ---------------------------------------------------------------------------
// 상태 표시
// ---------------------------------------------------------------------------

namespace
{
	/** 배터리 게이지 칸 수. UVoiceStatusWidget 의 입력 게이지와 눈높이를 맞춘다. */
	constexpr int32 GBatteryCells = 20;

	/** 이 아래로는 빨간색. "곧 꺼진다" 를 미리 알려주는 선이다. */
	constexpr float GBatteryLowThreshold = 0.2f;

	FString MakeBatteryGauge(float Percent01)
	{
		const int32 FilledCells = FMath::Clamp(FMath::RoundToInt(Percent01 * GBatteryCells), 0, GBatteryCells);

		FString Bar;
		Bar.Reserve(GBatteryCells);

		for (int32 Cell = 0; Cell < GBatteryCells; ++Cell)
		{
			Bar.AppendChar(Cell < FilledCells ? TEXT('=') : TEXT('.'));
		}

		return Bar;
	}

	/** cm 를 m 로. 반경은 cm 로 들고 있지만 사람이 읽기엔 m 가 낫다. */
	FString FormatRadius(float Centimeters)
	{
		return FString::Printf(TEXT("%.0fm"), Centimeters / 100.f);
	}

	/**
	 * 상태 텍스트의 상태별 기본색. 아이콘 색은 변경하지 않는다.
	 *
	 * ★ 값은 기존 텍스트 표시가 쓰던 색 그대로다. 이미 화면에서 검증된 색이라
	 *   새로 정할 이유가 없다.
	 */
	FLinearColor GetDefaultRadioTint(ERadioIconState State)
	{
		switch (State)
		{
		case ERadioIconState::Off:          return FLinearColor(0.55f, 0.55f, 0.55f);
		case ERadioIconState::Transmitting: return FLinearColor(1.f,   0.55f, 0.2f);
		case ERadioIconState::Receiving:    return FLinearColor(0.4f,  0.8f,  1.f);
		case ERadioIconState::On:           return FLinearColor(0.3f,  1.f,   0.3f);
		case ERadioIconState::None:
		default:                            return FLinearColor(0.5f,  0.5f,  0.5f);
		}
	}
}

// ---------------------------------------------------------------------------
// 상태 판정
//
// ★ 여기서 UI 를 만지지 않는다. 판정과 표현을 갈라놓아야 글자 표시와 아이콘
//   표시가 **같은 상태 머신**을 쓴다(UVoiceStatusWidget 과 같은 이유).
// ---------------------------------------------------------------------------

ERadioIconState URadioStatusWidget::EvaluateRadioState() const
{
	const URadioComponent* Comp = FindLocalRadioComponent();

	if (Comp == nullptr)
	{
		return ERadioIconState::None;
	}

	if (!Comp->IsPoweredOn())
	{
		// ★ 꺼져 있으면 송신도 수신도 없다. 전원을 먼저 보는 이유다 -
		//   꺼진 무전기에 "수신 중" 이 뜨면 그 자체로 거짓말이다.
		return ERadioIconState::Off;
	}

	const UVoiceSubsystem* Voice = GetVoiceSubsystem();

	if (Voice == nullptr)
	{
		return ERadioIconState::On;
	}

	// ★ 송신이 수신을 이긴다(VoiceTypes.h 의 ERadioIconState 주석).
	//   둘 다 성립할 때 놓치면 안 되는 쪽은 송신이다 - 이 게임에서 송신은
	//   곧 내 위치가 새는 것이라, 켜진 줄 모르는 편이 훨씬 위험하다.
	//
	//   여기서 IsInHand 를 보지 않는 것은 의도적이다. 인벤토리에 넣은 채로
	//   X 를 눌러도 서버가 조용히 거부하는데, 그때 아이콘이 "송신 중" 으로
	//   보여야 **왜 안 나가는지** 를 글자 줄에서 찾아보게 된다.
	if (Voice->IsRadioTransmitting())
	{
		return ERadioIconState::Transmitting;
	}

	if (Voice->IsReceivingRadio())
	{
		return ERadioIconState::Receiving;
	}

	return ERadioIconState::On;
}

// ---------------------------------------------------------------------------
// 상태 -> 아이콘
// ---------------------------------------------------------------------------

void URadioStatusWidget::ApplyRadioState(ERadioIconState NewState)
{
	// ★ 안 바뀌었으면 아무것도 안 한다. 매 주기 다시 넣으면 OnRadioStateChanged
	//   가 계속 불려서 WBP 애니메이션이 첫 프레임에서 되감긴다.
	if (bRadioStateApplied && NewState == CachedState)
	{
		return;
	}

	const ERadioIconState OldState = CachedState;

	CachedState        = NewState;
	bRadioStateApplied = true;

	// ★ 무전기가 없으면 내용을 접는다. 마이크와 다르다 - 무전기를 안 가진 것은
	//   정상 상태라 화면을 차지할 이유가 없다.
	//   bHideWhenNoRadio 를 끄면 "무전기 없음" 글자가 그대로 보인다(테스트용).
	//
	// ★★ **위젯 자신(this)은 절대 접지 않는다.** 접는 것은 내용물뿐이다.
	//   이 위젯은 NativeTick 폴링으로 "무전기가 생겼는지" 를 스스로 알아내는데,
	//   자기 자신을 Collapsed 로 만들면 그 이후로 틱이 계속 도는지가 위젯이 담긴
	//   패널의 구현에 달리게 된다. 한 번이라도 안 돌면 **다시 켤 사람이 아무도
	//   없다** - 무전기를 주워도 영영 안 나타난다.
	//   빈 위젯이 틱만 도는 비용은 0.1초에 한 번 무전기를 찾는 것뿐이라, 이
	//   위험을 감수할 이유가 전혀 없다.
	if (bHideWhenNoRadio)
	{
		SetContentVisible(NewState != ERadioIconState::None);
	}

	if (RadioIcon != nullptr)
	{
		// 비워둔 상태는 브러시를 안 건드린다 - WBP 에서 찍어둔 그림이 남는다.
		if (const FSlateBrush* Brush = IconBrushes.Find(NewState))
		{
			RadioIcon->SetBrush(*Brush);
		}

		// 상태별 표현은 브러시 교체만 한다. WBP에서 지정한 이미지 색은 보존한다.
	}

	OnRadioStateChanged(NewState, OldState);
}

void URadioStatusWidget::SetContentVisible(bool bVisible)
{
	const ESlateVisibility Wanted = bVisible
		? ESlateVisibility::SelfHitTestInvisible
		: ESlateVisibility::Collapsed;

	// ContentRoot 를 지정했으면 그것 하나만 접는다. WBP 에 배경처럼 따로 둔
	// 장식까지 같이 접히므로 이쪽이 낫다.
	if (ContentRoot != nullptr)
	{
		ContentRoot->SetVisibility(Wanted);
		return;
	}

	// 안 지정했으면 아는 것만 각각 접는다.
	if (RadioIcon != nullptr)
	{
		RadioIcon->SetVisibility(Wanted);
	}
	if (BatteryBar != nullptr)
	{
		// 배터리 바는 UpdateBatteryBar 가 따로 접기도 한다(테스트 무전기).
		// 여기서는 켤 때만 손대고, 끄는 것은 그쪽 판단을 덮어쓰지 않는다.
		if (bVisible)
		{
			BatteryBar->SetVisibility(Wanted);
		}
		else
		{
			BatteryBar->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (StatusText != nullptr)
	{
		StatusText->SetVisibility(Wanted);
	}
}

// ---------------------------------------------------------------------------
// 배터리 바
// ---------------------------------------------------------------------------

void URadioStatusWidget::UpdateBatteryBar()
{
	// ★ 배터리는 ARadio 에만 있다. CurrentDurability 가 AItemBase 의 것이라
	//   테스트 무전기(AVoiceDebugRadio)에는 아예 없다(RefreshStatusText 와 같은 근거).
	const ARadio* Item = FindLocalRadioItem();

	bBatteryLow = (Item != nullptr) && (Item->GetBatteryPercent() <= GBatteryLowThreshold);

	if (BatteryBar == nullptr)
	{
		return; // WBP 를 안 쓰면 글자 게이지(MakeBatteryGauge)가 대신한다.
	}

	if (Item == nullptr)
	{
		// 테스트 무전기는 배터리 개념 자체가 없다. 0% 로 그리면 "방전됨" 으로
		// 잘못 읽히므로 바를 아예 접는다.
		BatteryBar->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// 보간하지 않는다. 배터리는 초당 몇 퍼센트씩 천천히 줄어드는 값이라
	// 0.1초 간격으로 그대로 넣어도 이미 부드럽다 - 보간을 걸면 오히려 실제
	// 잔량보다 늦게 따라와서 "곧 꺼진다" 를 늦게 알리게 된다.
	BatteryBar->SetVisibility(ESlateVisibility::HitTestInvisible);
	BatteryBar->SetPercent(Item->GetBatteryPercent());

	// 색만 여기서 바꾼다. 깜빡임 같은 연출은 WBP 가 IsBatteryLow 로 건다.
	BatteryBar->SetFillColorAndOpacity(bBatteryLow
		? FLinearColor(0.95f, 0.3f, 0.3f)
		: FLinearColor(0.3f, 1.f, 0.3f));
}

void URadioStatusWidget::RefreshStatusText()
{
	if (StatusText == nullptr)
	{
		return;
	}

	URadioComponent* Comp = FindLocalRadioComponent();

	// ★ 상태 판정을 여기서 다시 하지 않는다. EvaluateRadioState 하나가 아이콘과
	//   글자 양쪽의 근거다 - 판정이 두 벌이면 상태를 하나 추가할 때 한쪽만
	//   고치게 되고, 그러면 아이콘과 글자가 서로 다른 말을 한다.
	const ERadioIconState State = EvaluateRadioState();

	// --- 무전기가 없다 ------------------------------------------------------
	//
	// 이 경우를 명시적으로 알려주지 않으면 "Z 를 눌러도 아무 일도 안 일어난다" 로
	// 헤매게 된다. 어떻게 하나 만드는지까지 같이 알려준다.
	// (bHideWhenNoRadio 가 켜져 있으면 위젯째로 접혀서 이 글자도 안 보인다)
	if (State == ERadioIconState::None || Comp == nullptr)
	{
		StatusText->SetText(FText::FromString(
			TEXT("[무전기 없음]\nMOU.Voice.Radio.Spawn 으로 테스트용을 손에 들 수 있다")));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)));
		return;
	}

	const ARadio* Item = Cast<ARadio>(Comp->GetOwner());

	const bool bPowered      = (State != ERadioIconState::Off);
	const bool bInHand       = Comp->IsInHand();
	const bool bTransmitting = (State == ERadioIconState::Transmitting);
	const bool bReceiving    = (State == ERadioIconState::Receiving);

	// --- 1줄: 전원 + 소지 상태 ----------------------------------------------
	//
	// Radio.h 규칙표의 "손에 듦 / 인벤토리" 열이 이 줄이다. 둘을 구분해서
	// 보여줘야 "왜 송신이 안 되지"(= 인벤토리에 있어서) 를 바로 알 수 있다.
	const FString CarryLine = FString::Printf(
		TEXT("[무전기] 전원 %s · %s"),
		bPowered ? TEXT("ON") : TEXT("OFF"),
		bInHand ? TEXT("손에 듦") : TEXT("인벤토리"));

	// --- 2줄: 배터리 --------------------------------------------------------
	//
	// ★ 배터리는 ARadio 에만 있다. CurrentDurability 는 AItemBase 의 것이라
	//   테스트 무전기(AVoiceDebugRadio)에는 아예 없다. 줄 수를 고정하려고
	//   없을 때도 자리는 남긴다 - 상태에 따라 줄 수가 바뀌면 글자가 위아래로
	//   흔들려서 오히려 읽기 어렵다(UVoiceStatusWidget 과 같은 이유).
	FString BatteryLine;

	if (Item == nullptr)
	{
		BatteryLine = TEXT("\n배터리 (테스트 무전기 - 없음)");
	}
	else if (BatteryBar != nullptr)
	{
		// ★ WBP 의 BatteryBar 가 붙어 있으면 글자 막대는 빼고 숫자만 남긴다.
		//   같은 정보를 두 벌 그리면 화면만 시끄럽다.
		BatteryLine = FString::Printf(TEXT("\n배터리 %3.0f%%"), Item->GetBatteryPercent() * 100.f);
	}
	else
	{
		const float Percent = Item->GetBatteryPercent();
		BatteryLine = FString::Printf(TEXT("\n배터리 [%s] %3.0f%%"), *MakeBatteryGauge(Percent), Percent * 100.f);
	}

	// --- 3줄: 반경 ----------------------------------------------------------
	//
	// 인벤토리에 넣으면 StowedRadiusScale 이 곱해진다. **실효값**을 보여줘야
	// 그게 실제로 적용됐는지 확인할 수 있다(RadioComponent.h 의 ★).
	const FString RadiusLine = bShowRadiusInfo
		? FString::Printf(TEXT("\n반경 사람 %s / NPC %s"),
			*FormatRadius(Comp->GetEffectiveHearRadius()),
			*FormatRadius(Comp->GetEffectiveNoiseRadius()))
		: FString();

	// --- 4줄: 조작 안내 / 송신 상태 -----------------------------------------
	//
	// ★ 송신이 안 되는 이유를 여기서 말해준다. 서버가 조용히 거부하기 때문에
	//   (FindUsableRadioFor) 이 줄이 없으면 X 를 눌러도 왜 아무 일이 없는지
	//   알 방법이 없다.
	// ★ 분기 순서를 EvaluateRadioState 와 똑같이 맞춘다. 글자와 아이콘이
	//   다른 우선순위를 쓰면 "아이콘은 송신인데 글자는 수신" 같은 것이 나온다.
	FString ActionLine;

	// 상태 텍스트만 기본색으로 구분한다. 아이콘은 브러시만 바뀐다.
	FLinearColor Color = GetDefaultRadioTint(State);

	if (bTransmitting)
	{
		// 지금 실제로 무전이 나가고 있다. 가장 강한 신호를 준다 - 이 게임에서
		// 송신은 곧 위치가 새는 것이라, 켜진 줄 모르고 있으면 안 된다.
		ActionLine = FString::Printf(TEXT("\n[송신 중] %s"), *TransmitKey.ToString());
	}
	else if (bReceiving)
	{
		ActionLine = TEXT("\n[수신 중] 무전이 들어오고 있다");
	}
	else if (!bPowered && bInHand)
	{
		ActionLine = FString::Printf(TEXT("\n%s 로 켜기"), *PowerToggleKey.ToString());
	}
	else if (!bPowered)
	{
		ActionLine = TEXT("\n전원 조작은 손에 들어야 가능");
	}
	else if (bInHand)
	{
		ActionLine = FString::Printf(TEXT("\n%s 홀드로 송신"), *TransmitKey.ToString());
	}

	// ★ 인벤토리 경고는 위 분기를 **덮어쓰지 않고 덧붙인다.**
	//   수신 중에도 이 사실은 그대로라서다 - 규칙표의 "인벤토리 = 송신 불가" 칸을
	//   수신이 가려버리면, 무전이 들어오는 동안 X 를 눌러도 왜 안 나가는지
	//   화면 어디에도 안 남는다. 배터리가 닳는다는 것도 같이 알려야
	//   "꺼둘까" 가 선택이 된다.
	if (bPowered && !bInHand && !bTransmitting)
	{
		ActionLine += TEXT("\n수신만 (송신하려면 손에 들 것) · 배터리는 닳는 중");

		if (!bReceiving)
		{
			Color = FLinearColor(1.f, 0.85f, 0.3f);
		}
	}

	// 배터리가 얼마 없으면 위 판정을 덮어쓴다. 곧 꺼진다는 것이 다른 무엇보다
	// 먼저 눈에 들어와야 한다 - 방전되면 전원이 스스로 꺼진다(ARadio::DrainBattery).
	if (Item != nullptr && bPowered && Item->GetBatteryPercent() <= GBatteryLowThreshold)
	{
		Color = FLinearColor(0.95f, 0.3f, 0.3f);
	}

	StatusText->SetText(FText::FromString(CarryLine + BatteryLine + RadiusLine + ActionLine));
	StatusText->SetColorAndOpacity(FSlateColor(Color));
}

// ---------------------------------------------------------------------------
// 기본 레이아웃 (WBP 가 없을 때만)
//
//   CanvasPanel (화면 전체)
//     └ StatusText (우하단)
//
// ★ 자리를 우하단으로 잡은 이유: 우상단은 UVoiceStatusWidget(마이크 상태),
//   좌하단은 ChatWidgetBase(채팅 로그)가 이미 쓴다. 셋을 같이 띄워놓고
//   테스트하는 일이 잦으므로 겹치지 않게 나눠 둔다.
// ---------------------------------------------------------------------------

void URadioStatusWidget::BuildDefaultLayout()
{
	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RadioStatusRootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetVisibility(ESlateVisibility::SelfHitTestInvisible); // 마우스를 먹지 않는다
	StatusText->SetJustification(ETextJustify::Right);

	UCanvasPanelSlot* TextSlot = RootCanvas->AddChildToCanvas(StatusText);
	// 우하단(1,1)에 고정. 해상도가 바뀌어도 항상 같은 자리에 붙는다.
	TextSlot->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
	TextSlot->SetAlignment(FVector2D(1.f, 1.f));
	TextSlot->SetAutoSize(true);
	TextSlot->SetPosition(FVector2D(-24.f, -24.f));
}

// ---------------------------------------------------------------------------
// 콘솔 명령 - UVoiceStatusWidget 의 MOU.Voice.ShowUI 와 같은 패턴.
//
// 실제 게임에서는 무전기를 처음 주웠을 때 등에서
// CreateWidget<URadioStatusWidget>(PC, ...) -> AddToViewport() 하면 된다.
// 지금은 게임 플로우에 안 엮여 있으므로 콘솔로 검증한다.
// ---------------------------------------------------------------------------

namespace
{
	/** 월드마다 따로 기억한다. PIE 다중 창에서 창별로 독립 검증하기 위함. */
	TMap<TWeakObjectPtr<UWorld>, TWeakObjectPtr<URadioStatusWidget>> GDebugRadioStatusWidgets;

	void PruneDebugRadioStatusWidgets()
	{
		for (auto It = GDebugRadioStatusWidgets.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid() || !It.Value().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	URadioStatusWidget* FindDebugRadioStatusWidget(UWorld* World)
	{
		PruneDebugRadioStatusWidgets();

		if (const TWeakObjectPtr<URadioStatusWidget>* Found = GDebugRadioStatusWidgets.Find(World))
		{
			if (URadioStatusWidget* Widget = Found->Get())
			{
				return Widget;
			}
		}

		// ★ 이 맵만 보면 **콘솔이 만든 것밖에 못 찾는다.**
		//   지금은 ATeamProject_MOUPlayerController::BeginPlay 가 시작할 때
		//   이미 하나 띄우므로, 그것을 못 보면 ShowUI 를 칠 때마다 위젯이 하나씩
		//   더 쌓여 화면에 글자가 겹쳐 보인다. 뷰포트에 있는 것까지 훑는다.
		if (World == nullptr)
		{
			return nullptr;
		}

		TArray<UUserWidget*> InViewport;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, InViewport, URadioStatusWidget::StaticClass(), /*TopLevelOnly=*/false);

		return InViewport.Num() > 0 ? Cast<URadioStatusWidget>(InViewport[0]) : nullptr;
	}

	/**
	 * ★ 하나의 토글 명령으로 둔다(MOU.Voice.Mute / Codec 과 같은 방식).
	 *
	 *   Show/Hide 두 개로 나눌 수도 있지만, 이 위젯은 Z/X 를 **바인딩까지 하므로**
	 *   껐다 켜는 일이 잦다 - 다른 키 테스트를 하려면 잠깐 꺼야 한다. 토글이 낫다.
	 */
	FAutoConsoleCommandWithWorldAndArgs GRadioUICommand(
		TEXT("MOU.Voice.RadioUI"),
		TEXT("무전기 상태 위젯을 켜고 끈다(전원·배터리·반경 표시 + Z/X 조작). ")
		TEXT("인자 없으면 토글. 사용법: [0|1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				URadioStatusWidget* Existing = FindDebugRadioStatusWidget(World);

				const bool bWantOn = Args.IsValidIndex(0)
					? (FCString::Atoi(*Args[0]) != 0)
					: (Existing == nullptr);

				if (!bWantOn)
				{
					if (Existing != nullptr)
					{
						Existing->RemoveFromParent(); // NativeDestruct 가 송신을 끊고 키를 푼다
					}
					GDebugRadioStatusWidgets.Remove(World);
					return;
				}

				if (Existing != nullptr)
				{
					return; // 이 창에는 이미 떠 있다
				}

				APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
				if (PC == nullptr)
				{
					return;
				}

				if (URadioStatusWidget* Widget = CreateWidget<URadioStatusWidget>(PC, URadioStatusWidget::StaticClass()))
				{
					Widget->AddToViewport();
					GDebugRadioStatusWidgets.Add(World, Widget);

					UE_LOG(LogMOUVoice, Log,
						TEXT("무전기 UI 를 띄웠다. Z = 전원, X 홀드 = 송신. ")
						TEXT("무전기가 없으면 MOU.Voice.Radio.Spawn 으로 먼저 들 것."));
				}
			}));
}

// ---------------------------------------------------------------------------
// MOU.Voice.RadioDebug — "무전기를 주웠는데 UI 가 안 나온다" 전용 진단
//
// [왜 필요한가]
//   이 UI 가 안 뜨는 경로가 **두 갈래**인데 화면상 증상이 똑같다:
//
//     (가) 위젯이 아예 안 만들어졌다        -> 아무것도 안 보인다
//     (나) 위젯은 있는데 무전기를 못 찾았다 -> 내용이 접혀서 안 보인다
//
//   둘을 눈으로 구분할 방법이 없어서, 어느 쪽인지 모른 채 양쪽을 다 뒤지게 된다.
//   (나) 라면 다시 두 갈래다 - 무전기가 폰에 부착이 안 된 것인지, 부착은 됐는데
//   URadioComponent 가 없는 것(= 그 아이템이 ARadio 계열이 아님)인지.
//
//   MOU.Voice.HearTest 를 만든 것과 같은 이유다: 같은 증상에 원인이 여럿이면
//   **원인을 갈라주는 도구**부터 만드는 것이 결국 빠르다.
// ---------------------------------------------------------------------------

namespace
{
	const TCHAR* RadioStateName(ERadioIconState State)
	{
		switch (State)
		{
		case ERadioIconState::None:         return TEXT("None(무전기 없음)");
		case ERadioIconState::Off:          return TEXT("Off(전원 꺼짐)");
		case ERadioIconState::On:           return TEXT("On(전원 켜짐)");
		case ERadioIconState::Transmitting: return TEXT("Transmitting(송신 중)");
		case ERadioIconState::Receiving:    return TEXT("Receiving(수신 중)");
		default:                            return TEXT("(알 수 없음)");
		}
	}

	const TCHAR* VisibilityName(ESlateVisibility Visibility)
	{
		switch (Visibility)
		{
		case ESlateVisibility::Visible:               return TEXT("Visible");
		case ESlateVisibility::Collapsed:             return TEXT("Collapsed(접힘)");
		case ESlateVisibility::Hidden:                return TEXT("Hidden(숨김)");
		case ESlateVisibility::HitTestInvisible:      return TEXT("HitTestInvisible");
		case ESlateVisibility::SelfHitTestInvisible:  return TEXT("SelfHitTestInvisible");
		default:                                      return TEXT("(알 수 없음)");
		}
	}

	/** 부착 부모를 타고 올라가며 경로를 만든다. URadioComponent::GetHolder 와 같은 방향. */
	FString DescribeAttachChain(const AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return TEXT("(없음)");
		}

		FString Chain;
		int32 Depth = 0;

		for (const AActor* Current = Actor->GetAttachParentActor();
			Current != nullptr && Depth < 8;
			Current = Current->GetAttachParentActor(), ++Depth)
		{
			Chain += FString::Printf(TEXT(" -> %s"), *Current->GetName());
		}

		return Chain.IsEmpty() ? TEXT("(아무것에도 안 붙어 있다)") : Chain;
	}

	FAutoConsoleCommandWithWorldAndArgs GRadioDebugCommand(
		TEXT("MOU.Voice.RadioDebug"),
		TEXT("무전기 UI 가 안 뜨는 이유를 진단한다. 위젯 상태 / 폰에 부착된 액터 / ")
		TEXT("월드의 모든 무전기를 한 번에 덤프한다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				if (World == nullptr)
				{
					return;
				}

				UE_LOG(LogMOUVoice, Log, TEXT("=============== 무전기 UI 진단 ==============="));

				// --- 1. 위젯이 살아 있는가 ------------------------------------
				//
				// 여기가 "없음" 이면 (가) 다. 아래는 볼 것도 없다.
				URadioStatusWidget* Widget = FindDebugRadioStatusWidget(World);

				if (Widget == nullptr)
				{
					UE_LOG(LogMOUVoice, Warning,
						TEXT("[위젯] 화면에 없다. 이것이 원인이다.\n")
						TEXT("       확인할 것 3가지:\n")
						TEXT("        1) PlayerController 가 ATeamProject_MOUPlayerController 를 상속하는가\n")
						TEXT("        2) 그 BP 의 MOU|Voice 에서 bAutoShowVoiceWidgets 가 켜져 있는가\n")
						TEXT("        3) RadioStatusWidgetClass 에 넣은 WBP 가 URadioStatusWidget 을 상속하는가\n")
						TEXT("           (다른 부모를 쓰면 CreateWidget 이 null 을 돌려주고 조용히 넘어간다)\n")
						TEXT("       급하면 MOU.Voice.RadioUI 로 C++ 기본 위젯을 띄워 나머지를 먼저 확인할 수 있다."));
				}
				else
				{
					UE_LOG(LogMOUVoice, Log,
						TEXT("[위젯] 있음. 클래스=%s  상태=%s  가시성=%s"),
						*Widget->GetClass()->GetName(),
						RadioStateName(Widget->GetRadioState()),
						VisibilityName(Widget->GetVisibility()));
				}

				// --- 2. 로컬 폰 -----------------------------------------------
				APlayerController* PC   = World->GetFirstPlayerController();
				APawn*             Pawn = PC ? PC->GetPawn() : nullptr;

				if (Pawn == nullptr)
				{
					UE_LOG(LogMOUVoice, Warning,
						TEXT("[폰] 없다. 아직 빙의 전이면 정상이지만, 이 상태에서는 무전기를 찾을 수 없다."));
					UE_LOG(LogMOUVoice, Log, TEXT("=============================================="));
					return;
				}

				UE_LOG(LogMOUVoice, Log, TEXT("[폰] %s"), *Pawn->GetName());

				// --- 3. 폰에 부착된 액터 --------------------------------------
				//
				// ★ 이 UI 는 부착 관계로만 무전기를 찾는다(FindLocalRadioComponent).
				//   그래서 여기 안 보이면 UI 도 못 본다. 줍기가 부착까지 했는지,
				//   그 부착이 이 클라이언트까지 복제됐는지가 여기서 갈린다.
				TArray<AActor*> Attached;
				Pawn->GetAttachedActors(Attached, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/true);

				UE_LOG(LogMOUVoice, Log, TEXT("[폰에 부착된 액터] %d 개"), Attached.Num());

				for (const AActor* Actor : Attached)
				{
					if (!IsValid(Actor))
					{
						continue;
					}

					const bool bHasRadio = Actor->FindComponentByClass<URadioComponent>() != nullptr;

					UE_LOG(LogMOUVoice, Log, TEXT("   - %s  (%s)  RadioComponent=%s"),
						*Actor->GetName(),
						*Actor->GetClass()->GetName(),
						bHasRadio ? TEXT("있음  <-- 이것이 무전기다") : TEXT("없음"));
				}

				// --- 4. 월드의 모든 무전기 ------------------------------------
				//
				// 부착 목록에 없었다면, 무전기가 애초에 없는 것인지 아니면 있는데
				// 안 붙은 것인지를 여기서 가른다. 둘은 대응이 완전히 다르다.
				int32 RadioCount = 0;

				for (TObjectIterator<URadioComponent> It; It; ++It)
				{
					URadioComponent* Comp = *It;

					if (!IsValid(Comp) || Comp->GetWorld() != World)
					{
						continue;
					}

					AActor* Owner = Comp->GetOwner();

					if (!IsValid(Owner))
					{
						continue;
					}

					++RadioCount;

					UE_LOG(LogMOUVoice, Log,
						TEXT("   - %s (%s)  전원=%s  손에듦=%s\n")
						TEXT("     부착 경로: %s%s"),
						*Owner->GetName(),
						*Owner->GetClass()->GetName(),
						Comp->IsPoweredOn() ? TEXT("ON") : TEXT("OFF"),
						Comp->IsInHand()    ? TEXT("예") : TEXT("아니오"),
						*Owner->GetName(),
						*DescribeAttachChain(Owner));
				}

				UE_LOG(LogMOUVoice, Log, TEXT("[월드의 무전기] %d 개"), RadioCount);

				// --- 5. 결론 --------------------------------------------------
				//
				// 사람이 읽고 바로 다음 행동을 정할 수 있게 한 줄로 못 박는다.
				// "정보만 던지고 해석은 알아서" 는 진단 도구가 아니다.
				const bool bFoundOnPawn = [&Attached]()
				{
					for (const AActor* Actor : Attached)
					{
						if (IsValid(Actor) && Actor->FindComponentByClass<URadioComponent>() != nullptr)
						{
							return true;
						}
					}
					return false;
				}();

				if (bFoundOnPawn)
				{
					UE_LOG(LogMOUVoice, Log,
						TEXT("[결론] 무전기를 폰에서 찾았다. UI 가 여전히 안 보이면 위젯 쪽 문제다 - ")
						TEXT("WBP 의 RadioIcon 브러시(IconBrushes)가 비어 있거나, 아이콘이 화면 밖에 있거나, ")
						TEXT("ContentRoot 로 지정한 패널이 다른 곳에서 접혀 있는지 볼 것."));
				}
				else if (RadioCount > 0)
				{
					UE_LOG(LogMOUVoice, Warning,
						TEXT("[결론] ★ 월드에 무전기는 있는데 **폰에 부착돼 있지 않다.** 이것이 원인이다.\n")
						TEXT("       이 UI 는 부착 관계로만 무전기를 찾는다(ARadio::FindCarriedBy 와 같은 방식).\n")
						TEXT("       위 '부착 경로' 를 볼 것 - 폰 이름이 안 나오면 줍기가 부착을 안 했거나,\n")
						TEXT("       서버에서만 부착되고 이 클라이언트로 복제되지 않은 것이다."));
				}
				else
				{
					UE_LOG(LogMOUVoice, Warning,
						TEXT("[결론] ★ 월드에 URadioComponent 가 하나도 없다. **주운 그 아이템이 무전기가 아니다.**\n")
						TEXT("       배치한 액터가 ARadio 를 상속하는지 확인할 것. 이름이 무전기여도\n")
						TEXT("       ARadio 계열이 아니면 이 UI 는 영원히 안 뜬다.\n")
						TEXT("       테스트용 무전기는 MOU.Voice.Radio.Spawn 으로 바로 들 수 있다."));
				}

				UE_LOG(LogMOUVoice, Log, TEXT("=============================================="));
			}));
}
