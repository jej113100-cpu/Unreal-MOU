// MOU 음성 - 무전기 상태 표시 위젯 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7-3절, 14절 V6 / Radio.h 의 규칙표

#include "Voice/RadioStatusWidget.h"

#include "Voice/Radio.h"
#include "Voice/RadioComponent.h"
#include "Voice/VoiceComponent.h"
#include "Voice/VoiceSubsystem.h"
#include "Voice/VoiceTypes.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/InputComponent.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

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
				PC->InputComponent->BindKey(PowerToggleKey, IE_Pressed, this, &URadioStatusWidget::HandlePowerKeyPressed);

				// ★ PTT 는 누름과 뗌이 **둘 다** 필요하다. 뗌을 안 걸면 한 번
				//   누른 순간부터 영원히 송신 상태로 남는다.
				PC->InputComponent->BindKey(TransmitKey, IE_Pressed, this, &URadioStatusWidget::HandleTransmitKeyPressed);
				PC->InputComponent->BindKey(TransmitKey, IE_Released, this, &URadioStatusWidget::HandleTransmitKeyReleased);
			}
		}
	}

	TimeSinceLastRefresh = RefreshInterval; // 뜨자마자 한 번은 바로 갱신되게
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

	// 진짜 아이템은 자기 힘으로 서버까지 간다(ARadio::SetPowered 가 Server RPC 를 탄다).
	if (ARadio* Item = Cast<ARadio>(Comp->GetOwner()))
	{
		Item->TogglePower();
		RefreshStatusText(); // 다음 주기까지 기다리지 않고 즉시 반영
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

	RefreshStatusText();
}

void URadioStatusWidget::HandleTransmitKeyPressed()
{
	// 손에 들었는지 / 켜져 있는지는 **여기서 막지 않는다.** 서버가
	// UVoiceRouter::FindUsableRadioFor 에서 다시 확인하므로, 클라에서 미리
	// 거르면 판정이 두 군데로 갈라져 어긋나기만 한다(ARadio::StartTransmit 주석).
	//
	// 대신 화면에는 왜 안 나가는지를 표시해 준다(RefreshStatusText 의 경고 줄).
	if (ARadio* Item = FindLocalRadioItem())
	{
		Item->StartTransmit();
	}
	else if (FindLocalRadioComponent() != nullptr)
	{
		// 테스트 무전기. 송신은 어차피 로컬 상태라 서브시스템을 직접 켠다
		// (ARadio::StartTransmit 이 하는 일과 똑같다).
		if (UVoiceSubsystem* Voice = GetVoiceSubsystem())
		{
			Voice->SetRadioTransmitting(true);
		}
	}
	else
	{
		return; // 무전기가 아예 없다
	}

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
}

void URadioStatusWidget::RefreshStatusText()
{
	if (StatusText == nullptr)
	{
		return;
	}

	URadioComponent* Comp = FindLocalRadioComponent();

	// --- 무전기가 없다 ------------------------------------------------------
	//
	// 이 경우를 명시적으로 알려주지 않으면 "Z 를 눌러도 아무 일도 안 일어난다" 로
	// 헤매게 된다. 어떻게 하나 만드는지까지 같이 알려준다.
	if (Comp == nullptr)
	{
		StatusText->SetText(FText::FromString(
			TEXT("[무전기 없음]\nMOU.Voice.Radio.Spawn 으로 테스트용을 손에 들 수 있다")));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)));
		return;
	}

	const ARadio* Item = Cast<ARadio>(Comp->GetOwner());
	const UVoiceSubsystem* Voice = GetVoiceSubsystem();

	const bool bPowered      = Comp->IsPoweredOn();
	const bool bInHand       = Comp->IsInHand();
	const bool bTransmitting = Voice && Voice->IsRadioTransmitting();

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

	if (Item != nullptr)
	{
		const float Percent = Item->GetBatteryPercent();
		BatteryLine = FString::Printf(TEXT("\n배터리 [%s] %3.0f%%"), *MakeBatteryGauge(Percent), Percent * 100.f);
	}
	else
	{
		BatteryLine = TEXT("\n배터리 (테스트 무전기 - 없음)");
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
	FString ActionLine;
	FLinearColor Color;

	if (!bPowered)
	{
		ActionLine = FString::Printf(TEXT("\n%s 로 켜기"), *PowerToggleKey.ToString());
		Color = FLinearColor(0.55f, 0.55f, 0.55f);
	}
	else if (bTransmitting)
	{
		// 지금 실제로 무전이 나가고 있다. 가장 강한 신호를 준다 - 이 게임에서
		// 송신은 곧 위치가 새는 것이라, 켜진 줄 모르고 있으면 안 된다.
		ActionLine = FString::Printf(TEXT("\n[송신 중] %s"), *TransmitKey.ToString());
		Color = FLinearColor(1.f, 0.55f, 0.2f);
	}
	else if (!bInHand)
	{
		// 규칙표의 "인벤토리 = 송신 불가" 칸. 수신은 되고 배터리도 닳는다는 것을
		// 같이 알려야 "꺼둘까" 가 선택이 된다.
		ActionLine = TEXT("\n수신만 (송신하려면 손에 들 것) · 배터리는 닳는 중");
		Color = FLinearColor(1.f, 0.85f, 0.3f);
	}
	else
	{
		ActionLine = FString::Printf(TEXT("\n%s 홀드로 송신"), *TransmitKey.ToString());
		Color = FLinearColor(0.3f, 1.f, 0.3f);
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
		const TWeakObjectPtr<URadioStatusWidget>* Found = GDebugRadioStatusWidgets.Find(World);
		return Found ? Found->Get() : nullptr;
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
