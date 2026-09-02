// MOU 음성 - 마이크 상태 표시 위젯 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 15절(프라이버시), 7-1절(C 키)

#include "Voice/VoiceStatusWidget.h"

#include "Voice/VoiceSubsystem.h"
#include "Voice/VoiceTypes.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/InputComponent.h"
#include "Components/ProgressBar.h"
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
	ApplyMicState(EvaluateMicState());
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

	// --- 이산 상태(아이콘/글자): 짧은 주기로 -------------------------------
	//
	// 매 프레임 문자열을 새로 만들거나 브러시를 갈아끼울 이유가 없다.
	TimeSinceLastRefresh += InDeltaTime;
	if (TimeSinceLastRefresh >= RefreshInterval)
	{
		TimeSinceLastRefresh = 0.f;
		ApplyMicState(EvaluateMicState());
		RefreshStatusText();
	}

	// --- 연속값(음량 바): 매 프레임 ----------------------------------------
	//
	// ★ 위 블록 안에 넣으면 안 된다. 0.1초마다 SetPercent 하면 바가 계단처럼
	//   튀어서 "지금 마이크가 내 목소리를 잡고 있다" 는 느낌이 사라진다 -
	//   그 느낌이 이 게이지의 존재 이유다(9절).
	UpdateLevelBar(InDeltaTime);
}

// ---------------------------------------------------------------------------
// 입력 처리
// ---------------------------------------------------------------------------

void UVoiceStatusWidget::HandleMuteKeyPressed()
{
	if (UVoiceSubsystem* Voice = GetVoiceSubsystem())
	{
		Voice->ToggleMute();

		// 다음 주기까지 기다리지 않고 즉시 반영. 키를 눌렀는데 최대 0.1초 동안
		// 화면이 그대로면 "안 눌렸나" 하고 한 번 더 누르게 된다.
		ApplyMicState(EvaluateMicState());
		RefreshStatusText();
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

	/**
	 * 음량 바가 **올라갈** 때의 보간 속도. 사실상 즉시다.
	 *
	 * ★ 올라가는 것을 늦추면 말을 시작하고 나서야 바가 따라온다 - 그러면
	 *   "내가 낸 소리" 와 "화면" 이 어긋나 보여서 게이지를 못 믿게 된다.
	 */
	constexpr float GLevelAttackSpeed = 30.f;

	/**
	 * 음량 바가 **내려갈** 때의 보간 속도. 일부러 느리다.
	 *
	 * ★ 사람 목소리는 음절 사이에서 순간순간 0 에 가까워진다. 내려가는 것도
	 *   빠르게 두면 말하는 내내 바가 발작하듯 떨려서 최고 음량을 읽을 수 없다.
	 *   천천히 내려오게 두면 방금 얼마나 크게 말했는지가 눈에 남는다.
	 */
	constexpr float GLevelReleaseSpeed = 8.f;

	/**
	 * 상태별 기본 틴트. WBP 의 IconTints 가 비어 있을 때 쓴다.
	 *
	 * ★ 값은 기존 텍스트 표시가 쓰던 색을 그대로 옮긴 것이다. 이미 화면에서
	 *   검증된 색이라 새로 정할 이유가 없고, 글자와 아이콘 색이 어긋나지도 않는다.
	 */
	FLinearColor GetDefaultMicTint(EMicIconState State)
	{
		switch (State)
		{
		case EMicIconState::Dead:        return FLinearColor(0.9f,  0.25f, 0.25f);
		case EMicIconState::NoDevice:    return FLinearColor(0.5f,  0.5f,  0.5f);
		case EMicIconState::Calibrating: return FLinearColor(1.f,   0.85f, 0.3f);
		case EMicIconState::Muted:       return FLinearColor(0.6f,  0.6f,  0.6f);
		case EMicIconState::Speaking:    return FLinearColor(0.3f,  1.f,   0.3f);
		case EMicIconState::Idle:
		default:                         return FLinearColor(0.8f,  0.8f,  0.8f);
		}
	}
}

// ---------------------------------------------------------------------------
// 상태 판정
//
// ★ 여기서 UI 를 만지지 않는다. 판정과 표현을 갈라놓아야 글자 표시와 아이콘
//   표시가 **같은 상태 머신**을 쓴다 - 나중에 상태를 하나 더해도 고칠 곳이
//   이 함수 하나다.
// ---------------------------------------------------------------------------

EMicIconState UVoiceStatusWidget::EvaluateMicState() const
{
	const UVoiceSubsystem* Voice = GetVoiceSubsystem();

	if (Voice == nullptr)
	{
		return EMicIconState::NoDevice;
	}

	// ★ 아래 순서가 곧 우선순위다. 위에 있을수록 "그 아래를 봐도 소용없는" 상태다.

	// 사망이 가장 먼저다. 마이크가 있든 없든, 음소거든 아니든 결과가 같다.
	// 이걸 먼저 보지 않으면 "왜 아무도 내 말을 안 듣지" 로 한참 헤맨다.
	if (Voice->IsVoiceDead())
	{
		return EMicIconState::Dead;
	}

	if (!Voice->IsCaptureReady())
	{
		return EMicIconState::NoDevice;
	}

	// 보정 중에는 다른 것을 보여줄 이유가 없다. "지금 조용히 해야 한다" 가 전부다.
	if (Voice->IsCalibrating())
	{
		return EMicIconState::Calibrating;
	}

	if (Voice->IsMuted())
	{
		return EMicIconState::Muted;
	}

	return Voice->IsSpeaking() ? EMicIconState::Speaking : EMicIconState::Idle;
}

// ---------------------------------------------------------------------------
// 상태 -> 아이콘
// ---------------------------------------------------------------------------

void UVoiceStatusWidget::ApplyMicState(EMicIconState NewState)
{
	// ★ 안 바뀌었으면 아무것도 안 한다. 브러시를 매번 다시 넣으면 Slate 가
	//   매번 무효화되고, 무엇보다 OnMicStateChanged 가 매 주기 불려서 WBP
	//   애니메이션이 첫 프레임에서 계속 되감긴다.
	if (bMicStateApplied && NewState == CachedState)
	{
		return;
	}

	const EMicIconState OldState = CachedState;

	CachedState      = NewState;
	bMicStateApplied = true;

	// WBP 없이 텍스트 폴백만 쓰는 경우다. 아이콘은 건너뛰고 이벤트만 보낸다.
	if (MicIcon != nullptr)
	{
		// 비워둔 상태는 브러시를 안 건드린다 - WBP 에서 찍어둔 그림이 남는다.
		// (아이콘 3장 중 그 상태에 해당하는 것이 없을 수 있다)
		if (const FSlateBrush* Brush = IconBrushes.Find(NewState))
		{
			MicIcon->SetBrush(*Brush);
		}

		const FLinearColor* Tint = IconTints.Find(NewState);
		MicIcon->SetColorAndOpacity(Tint ? *Tint : GetDefaultMicTint(NewState));
	}

	OnMicStateChanged(NewState, OldState);
}

// ---------------------------------------------------------------------------
// 음량 바
// ---------------------------------------------------------------------------

void UVoiceStatusWidget::UpdateLevelBar(float InDeltaTime)
{
	if (LevelBar == nullptr)
	{
		return; // WBP 를 안 쓰면 텍스트 게이지(MakeLevelGauge)가 대신한다.
	}

	if (!bShowLevelGauge)
	{
		LevelBar->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	const UVoiceSubsystem* Voice = GetVoiceSubsystem();

	float Target = 0.f;

	// ★ 음소거·사망·마이크 없음에서는 0 으로 내린다.
	//   음소거 중에도 캡처는 돌고 있어서 그대로 그리면 **바가 움직인다** -
	//   "음소거인데 뭔가 나가고 있나" 로 읽히면 프라이버시 표시로서 최악이다.
	const bool bLive = (CachedState == EMicIconState::Idle)
		|| (CachedState == EMicIconState::Speaking)
		|| (CachedState == EMicIconState::Calibrating);

	if (Voice != nullptr && bLive)
	{
		// 상한을 감도의 배수로 잡는다 - 고정 상수면 마이크마다 바가 항상 꽉
		// 차거나 항상 0 이 된다(LevelBarHeadroom 주석).
		const float Ceiling = FMath::Max(Voice->GetMicSensitivity() * LevelBarHeadroom, KINDA_SMALL_NUMBER);

		// 순간 RMS(GetCurrentLoudness)가 아니라 엔벨로프를 쓴다. 순간값은
		// 음절마다 튀어서 바가 읽히지 않는다.
		Target = FMath::Clamp(Voice->GetLoudnessEnvelope() / Ceiling, 0.f, 1.f);
	}

	// 비대칭 보간: 올라갈 땐 즉시, 내려올 땐 천천히.
	const float Speed = (Target > DisplayLevel) ? GLevelAttackSpeed : GLevelReleaseSpeed;

	DisplayLevel = FMath::FInterpTo(DisplayLevel, Target, InDeltaTime, Speed);

	LevelBar->SetVisibility(ESlateVisibility::HitTestInvisible);
	LevelBar->SetPercent(DisplayLevel);
}

void UVoiceStatusWidget::RefreshStatusText()
{
	if (StatusText == nullptr)
	{
		return;
	}

	const UVoiceSubsystem* Voice = GetVoiceSubsystem();

	// ★ 상태 판정을 여기서 다시 하지 않는다. EvaluateMicState 하나가 아이콘과
	//   글자 양쪽의 근거다 - 판정이 두 벌이면 상태를 하나 추가할 때 한쪽만
	//   고치게 되고, 그러면 아이콘과 글자가 서로 다른 말을 한다.
	const EMicIconState State = EvaluateMicState();

	// Voice 가 null 이면 EvaluateMicState 는 반드시 NoDevice 를 준다(그 분기만
	// Voice 를 안 쓴다). 아래 분기들이 Voice 를 그냥 쓰는 근거가 이것이다.
	//
	// ★ check 가 아니라 ensure 인 이유: 이건 상태 표시일 뿐이라 틀렸다고
	//   게임을 죽일 일이 아니다. 에디터에서는 눈에 띄되 빌드에서는 조용히 넘어간다.
	if (!ensure(Voice != nullptr || State == EMicIconState::NoDevice))
	{
		return;
	}

	// 색은 아이콘과 같은 표를 쓴다. 글자만 다른 색이면 같은 상태가 두 가지
	// 색으로 보인다.
	FLinearColor Color = GetDefaultMicTint(State);
	FString      Line;

	switch (State)
	{
	case EMicIconState::Dead:
		Line = TEXT("[사망] 말할 수도, 들을 수도 없습니다");
		break;

	case EMicIconState::NoDevice:
		// 색으로 상태를 구분하되 텍스트로도 명확히 알린다.
		// 색만으로 구분하면 색각 이상이 있는 사용자가 상태를 못 읽는다.
		Line = TEXT("[마이크 없음]\n에디터를 켠 뒤 꽂았다면 재시작 필요 (MOU.Voice.Diag)");
		break;

	case EMicIconState::Calibrating:
		// 보정 중에는 다른 것을 보여줄 이유가 없다. "지금 조용히 해야 한다" 만 크게 알린다.
		Line = FString::Printf(
			TEXT("[감도 보정 중] 말하지 마세요... %.1f초\n%s  %.4f"),
			Voice->GetCalibrationRemainingSeconds(),
			*MakeLevelGauge(Voice->GetCurrentLoudness(), Voice->GetMicSensitivity()),
			Voice->GetCurrentLoudness());
		break;

	case EMicIconState::Muted:
		Line = FString::Printf(TEXT("[음소거] %s 로 해제"), *MuteToggleKey.ToString());
		break;

	case EMicIconState::Speaking:
	case EMicIconState::Idle:
	default:
	{
		const float Loudness  = Voice->GetCurrentLoudness();
		const float Threshold = Voice->GetMicSensitivity();

		// 둘째 줄은 항상 같은 모양으로 둔다. 상태에 따라 줄 수가 바뀌면 글자가
		// 위아래로 흔들려서 오히려 읽기 어렵다.
		//
		// ★ WBP 의 LevelBar 가 붙어 있으면 이 글자 게이지는 접는다. 같은 정보를
		//   두 벌 그리면 화면만 시끄럽다.
		const FString LevelLine = (bShowLevelGauge && LevelBar == nullptr)
			? FString::Printf(TEXT("\n%s  %.4f / 기준 %.4f"), *MakeLevelGauge(Loudness, Threshold), Loudness, Threshold)
			: FString();

		// 말하는 중에만 강조한다 - "지금 내 목소리가 실제로 나가고 있다" 는
		// 순간적인 신호다. 이게 없으면 오픈 마이크인데 마이크가 실제로 잡고
		// 있는지 감이 안 온다.
		Line = (State == EMicIconState::Speaking)
			? TEXT("[마이크 ON] 말하는 중") + LevelLine
			: TEXT("[마이크 ON]") + LevelLine;
		break;
	}
	}

	StatusText->SetText(FText::FromString(Line));
	StatusText->SetColorAndOpacity(FSlateColor(Color));
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

		if (const TWeakObjectPtr<UVoiceStatusWidget>* Found = GDebugVoiceStatusWidgets.Find(World))
		{
			if (UVoiceStatusWidget* Widget = Found->Get())
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
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, InViewport, UVoiceStatusWidget::StaticClass(), /*TopLevelOnly=*/false);

		return InViewport.Num() > 0 ? Cast<UVoiceStatusWidget>(InViewport[0]) : nullptr;
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
