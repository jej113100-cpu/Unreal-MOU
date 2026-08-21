// MOU 음성 - 마이크 상태 표시 위젯 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 15절(프라이버시), 7-1절(C 키)

#include "Voice/VoiceStatusWidget.h"

#include "Voice/VoiceSubsystem.h"
#include "Voice/VoiceTypes.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/InputComponent.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

UVoiceStatusWidget::UVoiceStatusWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 매 프레임 문자열을 새로 만들지 않으려고 RefreshInterval 로 직접 주기를
	// 조절한다. 그래도 NativeTick 자체는 켜져 있어야 그 타이머가 돈다.
	SetIsFocusable(false);
}

// ---------------------------------------------------------------------------
// 수명 주기
// ---------------------------------------------------------------------------

void UVoiceStatusWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
	}
}

void UVoiceStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// ChatWidgetBase 와 같은 패턴: 별도 입력 에셋 없이 소유 플레이어의
	// InputComponent 에 직접 건다. EnhancedInput 이 붙어 있어도 레거시
	// InputComponent 는 함께 살아있으므로 충돌하지 않는다.
	if (bBindMuteKeyToOwningPlayer)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			if (PC->InputComponent != nullptr)
			{
				PC->InputComponent->BindKey(MuteToggleKey, IE_Pressed, this, &UVoiceStatusWidget::HandleMuteKeyPressed);
			}
		}
	}

	TimeSinceLastRefresh = RefreshInterval; // 뜨자마자 한 번은 바로 갱신되게
	RefreshStatusText();
}

void UVoiceStatusWidget::NativeDestruct()
{
	// 위젯이 사라진 뒤에도 키 바인딩이 남아있으면 이미 파괴된 객체를 호출한다.
	// 이 위젯이 건 바인딩만 골라서 제거한다(ChatWidgetBase 와 동일한 이유).
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

void UVoiceStatusWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
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
// 입력 처리
// ---------------------------------------------------------------------------

void UVoiceStatusWidget::HandleMuteKeyPressed()
{
	if (UVoiceSubsystem* Voice = GetVoiceSubsystem())
	{
		Voice->ToggleMute();
		RefreshStatusText(); // 다음 주기까지 기다리지 않고 즉시 반영
	}
}

// ---------------------------------------------------------------------------
// 상태 표시
// ---------------------------------------------------------------------------

namespace
{
	/** 게이지 칸 수. 많을수록 세밀하지만 글자가 길어진다. */
	constexpr int32 GGaugeCells = 20;

	/**
	 * 게이지가 표현하는 음량 범위의 상한.
	 *
	 * 보통 목소리의 RMS 는 0.05~0.15 근처다. 상한을 1.0 으로 잡으면 말해도
	 * 막대가 한두 칸밖에 안 움직여서 **아무것도 읽을 수 없다.**
	 * 0.25 면 말할 때 막대가 절반 이상 차서 변화가 눈에 보인다.
	 */
	constexpr float GGaugeMaxLoudness = 0.25f;

	/**
	 * 입력 음량을 막대로 그린다.
	 *
	 * ★ 이 게이지가 있는 이유(VOICE_INTEGRATION.md 9절):
	 *   "감도 슬라이더 옆에 실시간 입력 게이지를 반드시 같이 둔다.
	 *    숫자만 있으면 아무도 못 맞춘다."
	 *
	 *   실제로 "가만히 있는데 계속 말하는 중" 문제를 만났을 때, 내 잡음 바닥이
	 *   얼마인지 볼 수 없으면 감도를 어디로 옮겨야 하는지 알 방법이 없다.
	 *   기준선(|)을 막대 안에 같이 그려서 **"지금 잡음이 기준을 넘고 있다"** 가
	 *   한눈에 보이게 한다.
	 */
	FString MakeLevelGauge(float Loudness, float Threshold)
	{
		const auto ToCell = [](float Value)
		{
			return FMath::Clamp(FMath::RoundToInt(Value / GGaugeMaxLoudness * GGaugeCells), 0, GGaugeCells);
		};

		const int32 FilledCells    = ToCell(Loudness);
		const int32 ThresholdCell  = FMath::Min(ToCell(Threshold), GGaugeCells - 1);

		FString Bar;
		Bar.Reserve(GGaugeCells + 2);

		for (int32 Cell = 0; Cell < GGaugeCells; ++Cell)
		{
			if (Cell == ThresholdCell)
			{
				// 기준선. 채워졌는지도 같이 알아야 하므로 글자를 나눈다.
				// (I = 기준선을 넘김, | = 아직 안 넘김)
				Bar.AppendChar(Cell < FilledCells ? TEXT('I') : TEXT('|'));
			}
			else
			{
				Bar.AppendChar(Cell < FilledCells ? TEXT('=') : TEXT('.'));
			}
		}

		return Bar;
	}
}

void UVoiceStatusWidget::RefreshStatusText()
{
	if (StatusText == nullptr)
	{
		return;
	}

	UVoiceSubsystem* Voice = GetVoiceSubsystem();

	// ★ 사망을 가장 먼저 본다. 마이크가 있든 없든, 음소거든 아니든 결과가 같기 때문이다.
	//   이걸 명시적으로 알려주지 않으면 "왜 아무도 내 말을 안 듣지" 로 한참 헤맨다.
	if (Voice && Voice->IsVoiceDead())
	{
		StatusText->SetText(FText::FromString(TEXT("[사망] 말할 수도, 들을 수도 없습니다")));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.25f, 0.25f)));
		return;
	}

	// 마이크가 아예 없는 경우 - 색으로 상태를 구분하되 텍스트로도 명확히 알린다.
	// 색만으로 구분하면 색각 이상이 있는 사용자가 상태를 못 읽는다.
	if (!Voice || !Voice->IsCaptureReady())
	{
		StatusText->SetText(FText::FromString(
			TEXT("[마이크 없음]\n에디터를 켠 뒤 꽂았다면 재시작 필요 (MOU.Voice.Diag)")));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)));
		return;
	}

	// 보정 중에는 다른 것을 보여줄 이유가 없다. "지금 조용히 해야 한다" 만 크게 알린다.
	if (Voice->IsCalibrating())
	{
		StatusText->SetText(FText::FromString(FString::Printf(
			TEXT("[감도 보정 중] 말하지 마세요... %.1f초\n%s  %.4f"),
			Voice->GetCalibrationRemainingSeconds(),
			*MakeLevelGauge(Voice->GetCurrentLoudness(), Voice->GetMicSensitivity()),
			Voice->GetCurrentLoudness())));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.85f, 0.3f)));
		return;
	}

	if (Voice->IsMuted())
	{
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("[음소거] %s 로 해제"), *MuteToggleKey.ToString())));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)));
		return;
	}

	const float Loudness  = Voice->GetCurrentLoudness();
	const float Threshold = Voice->GetMicSensitivity();

	// 둘째 줄은 항상 같은 모양으로 둔다. 상태에 따라 줄 수가 바뀌면 글자가
	// 위아래로 흔들려서 오히려 읽기 어렵다.
	const FString LevelLine = bShowLevelGauge
		? FString::Printf(TEXT("\n%s  %.4f / 기준 %.4f"), *MakeLevelGauge(Loudness, Threshold), Loudness, Threshold)
		: FString();

	if (Voice->IsSpeaking())
	{
		// 말하는 중에만 강조색을 준다 - "지금 내 목소리가 실제로 나가고 있다" 는
		// 순간적인 신호다. 이게 없으면 오픈 마이크인데 마이크가 실제로 잡고
		// 있는지 감이 안 온다.
		StatusText->SetText(FText::FromString(TEXT("[마이크 ON] 말하는 중") + LevelLine));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.3f, 1.f, 0.3f)));
	}
	else
	{
		StatusText->SetText(FText::FromString(TEXT("[마이크 ON]") + LevelLine));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f)));
	}
}

UVoiceSubsystem* UVoiceStatusWidget::GetVoiceSubsystem() const
{
	const APlayerController* PC = GetOwningPlayer();
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UVoiceSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------
// 기본 레이아웃 (WBP 가 없을 때만)
//
//   CanvasPanel (화면 전체)
//     └ StatusText (우상단, ChatWidgetBase 의 좌하단 로그와 겹치지 않는 자리)
// ---------------------------------------------------------------------------

void UVoiceStatusWidget::BuildDefaultLayout()
{
	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("VoiceStatusRootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetVisibility(ESlateVisibility::SelfHitTestInvisible); // 마우스를 먹지 않는다
	StatusText->SetJustification(ETextJustify::Right);

	UCanvasPanelSlot* TextSlot = RootCanvas->AddChildToCanvas(StatusText);
	// 우상단(1,0)에 고정. 해상도가 바뀌어도 항상 같은 자리에 붙는다.
	TextSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
	TextSlot->SetAlignment(FVector2D(1.f, 0.f));
	TextSlot->SetAutoSize(true);
	TextSlot->SetPosition(FVector2D(-24.f, 24.f));
}

// ---------------------------------------------------------------------------
// 콘솔 명령 - ChatWidgetBase 의 MOU.Chat.ShowUI 와 정확히 같은 패턴.
//
// 실제 게임에서는 로그인 성공 시점 등에서
// CreateWidget<UVoiceStatusWidget>(PC, ...) -> AddToViewport() 하면 된다.
// 지금은 게임 플로우에 아직 안 엮었으므로 콘솔로 검증한다.
// ---------------------------------------------------------------------------

namespace
{
	/** 월드마다 따로 기억한다. PIE 다중 창에서 창별로 독립적으로 검증하기 위함(ChatWidgetBase 와 동일 이유). */
	TMap<TWeakObjectPtr<UWorld>, TWeakObjectPtr<UVoiceStatusWidget>> GDebugVoiceStatusWidgets;

	void PruneDebugVoiceStatusWidgets()
	{
		for (auto It = GDebugVoiceStatusWidgets.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid() || !It.Value().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	UVoiceStatusWidget* FindDebugVoiceStatusWidget(UWorld* World)
	{
		PruneDebugVoiceStatusWidgets();
		const TWeakObjectPtr<UVoiceStatusWidget>* Found = GDebugVoiceStatusWidgets.Find(World);
		return Found ? Found->Get() : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs GVoiceShowUICommand(
		TEXT("MOU.Voice.ShowUI"),
		TEXT("마이크 상태 표시 위젯을 화면에 띄운다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				if (FindDebugVoiceStatusWidget(World) != nullptr)
				{
					return; // 이 창에는 이미 떠 있다
				}

				APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
				if (PC == nullptr)
				{
					return;
				}

				if (UVoiceStatusWidget* Widget = CreateWidget<UVoiceStatusWidget>(PC, UVoiceStatusWidget::StaticClass()))
				{
					Widget->AddToViewport();
					GDebugVoiceStatusWidgets.Add(World, Widget);
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceHideUICommand(
		TEXT("MOU.Voice.HideUI"),
		TEXT("마이크 상태 표시 위젯을 화면에서 제거한다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				if (UVoiceStatusWidget* Widget = FindDebugVoiceStatusWidget(World))
				{
					Widget->RemoveFromParent();
				}
				GDebugVoiceStatusWidgets.Remove(World);
			}));
}
