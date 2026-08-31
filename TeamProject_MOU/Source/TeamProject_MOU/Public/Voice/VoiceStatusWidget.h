// MOU 음성 - 마이크 상태 표시 위젯.
//
// [이 위젯이 왜 있는가 - 프라이버시 요구사항]
//   VOICE_INTEGRATION.md 15절: "마이크가 열려 있는 동안 화면에 항상 표시한다."
//   근접 음성은 기본이 오픈 마이크(7-1절)라서, 이 위젯 없이 `C` 토글만 만들면
//   "지금 내 목소리가 나가고 있는지" 를 화면에서 확인할 방법이 없다.
//   그래서 토글 로직(UVoiceSubsystem::SetMuted)과 이 위젯은 세트로 취급한다.
//
// [이 위젯이 시스템 어디에 있나]
//
//   UVoiceSubsystem (게임 스레드)
//     ↓ IsMuted() / IsSpeaking() / IsCaptureReady()   (매 틱 폴링)
//   ★ UVoiceStatusWidget  ← 이 파일
//     ↓ C 키 -> UVoiceSubsystem::ToggleMute()
//
//   ChatWidgetBase 와 같은 원칙: 델리게이트로 밀어주는 대신(음성 상태는 매 프레임
//   바뀔 수 있어 델리게이트를 매 프레임 쏘면 낭비다) NativeTick 에서 짧은 주기로
//   상태를 읽어와 텍스트만 갱신한다.
//
// [WBP 없이도 동작한다]
//   ChatWidgetBase 와 동일한 방식. WidgetTree->RootWidget 이 비어 있으면
//   BuildDefaultLayout() 이 화면 우하단에 작은 텍스트 하나를 C++ 로 조립한다.
//   WBP 를 만들려면 이름이 같은 TextBlock(StatusText) 을 배치하면 된다.
//
// [★ WBP 아이콘을 쓸 때의 구조]
//
//   C++ 는 상태를 **EMicIconState 하나로 축약해서** 넘길 뿐이고, 그 상태를
//   어떤 그림과 색으로 그릴지는 전부 WBP 가 정한다. 여기에 텍스처 경로를
//   적어두면 아트가 바뀔 때마다 C++ 를 고쳐 다시 빌드해야 한다.
//
//   WBP 에 아래 이름으로 배치하면 자동 연결된다(전부 선택이다 - 없으면
//   그 부분만 건너뛰고 텍스트 폴백이 그대로 동작한다):
//
//     Image        MicIcon    ← 상태별 그림 (IconBrushes / IconTints 로 지정)
//     ProgressBar  LevelBar   ← 입력 음량
//     TextBlock    StatusText ← 진단용 글자
//
//   ★ 아이콘은 3장이면 된다 - 기본 / 음소거(/) / 없음(X).
//     말하는 중·보정 중·사망은 **기본 아이콘에 색만 입힌다.** 상태마다
//     텍스처를 따로 두면 아이콘을 손볼 때 6장을 고쳐야 하고, 그중 하나를
//     빠뜨리면 특정 상태에서만 모양이 다른 버그가 된다.
//
//   ★ 갱신 주기가 둘로 나뉜다. 아이콘은 **바뀔 때만**, 음량 바는 **매 프레임**.
//     아이콘을 매 프레임 갈아끼우는 것은 낭비고, 음량 바를 0.1초마다 갱신하면
//     눈에 띄게 끊겨서 게이지의 존재 이유가 사라진다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "Styling/SlateBrush.h"
#include "Voice/VoiceTypes.h"
#include "VoiceStatusWidget.generated.h"

class UImage;
class UProgressBar;
class UTextBlock;
class UVoiceSubsystem;

/**
 * 화면 한쪽에 항상 떠 있는 마이크 상태 표시.
 *
 * 상태는 EMicIconState 하나로 축약된다: 마이크 없음 / 음소거 / 대기 / 말하는 중
 * / 감도 보정 / 사망. 아이콘 3장에 색을 얹어 이 여섯을 구분한다.
 *
 * 그 이상(감도 슬라이더 등)은 V9 옵션 화면 몫이다 - 이건 "지금 내가 들리는가" 를
 * 한눈에 보여주는 상시 인디케이터일 뿐이다.
 */
UCLASS()
class TEAMPROJECT_MOU_API UVoiceStatusWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UVoiceStatusWidget(const FObjectInitializer& ObjectInitializer);

	// --- UUserWidget --------------------------------------------------------
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// --- 설정 -----------------------------------------------------------------

	/**
	 * 소유 플레이어의 InputComponent 에 음소거 토글 키를 직접 바인딩할지.
	 * ChatWidgetBase::bBindToggleKeyToOwningPlayer 와 같은 이유로 기본 true.
	 * 게임 쪽에서 EnhancedInput 액션으로 따로 만들 것이면 false 로 끄고
	 * 그 액션에서 UVoiceSubsystem::ToggleMute() 를 직접 부르면 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Voice")
	bool bBindMuteKeyToOwningPlayer = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Voice", meta = (EditCondition = "bBindMuteKeyToOwningPlayer"))
	FKey MuteToggleKey = EKeys::C;

	/** 상태 텍스트를 갱신하는 주기(초). 매 프레임 문자열을 새로 만들 필요는 없다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Voice")
	float RefreshInterval = 0.1f;

	/**
	 * 입력 음량 게이지를 같이 보여줄지.
	 *
	 * ★ 켜 두는 것이 기본이다. 9절: "감도 슬라이더 옆에 실시간 입력 게이지를
	 *   반드시 같이 둔다 - 숫자만 있으면 아무도 못 맞춘다."
	 *   마이크 환경이 팀원마다 달라서, 이게 없으면 **감도가 안 맞을 때 원인을
	 *   짐작할 방법이 전혀 없다**(가만히 있는데 계속 말하는 중으로 나오는 등).
	 *
	 *   최종 빌드에서 화면을 깔끔하게 하고 싶으면 끄면 된다. 그 대신
	 *   V9 옵션 화면에는 같은 게이지가 반드시 있어야 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Voice")
	bool bShowLevelGauge = true;

	// --- WBP 아이콘 설정 ------------------------------------------------------

	/**
	 * 상태별 마이크 아이콘. WBP 의 클래스 디폴트에서 한 번에 지정한다.
	 *
	 * ★ 이미지는 3장이면 된다 - 기본 / 음소거(/) / 없음(X).
	 *   Speaking·Calibrating·Dead 는 **기본 아이콘에 색만 입힌다**(IconTints).
	 *   상태마다 텍스처를 따로 만들면 아이콘을 손볼 때마다 6장을 고쳐야 하고,
	 *   그중 하나만 안 고치면 특정 상태에서만 모양이 다른 버그가 된다.
	 *
	 * 비워두면 그 상태에서 아이콘을 건드리지 않는다(WBP 에서 찍어둔 것이 남는다).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MOU|Voice|UI")
	TMap<EMicIconState, FSlateBrush> IconBrushes;

	/**
	 * 상태별 아이콘 틴트.
	 *
	 * ★ **색만으로 상태를 구분시키지 말 것.** 색각 이상이 있으면 초록/회색을
	 *   구분 못 한다. 아이콘 모양(/ 와 X)이 항상 같은 정보를 말하고 있어야 하고,
	 *   색은 "지금 말하는 중" 처럼 **순간적인 강조**에만 쓴다.
	 *   (기존 텍스트 표시가 지키던 원칙 그대로다)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MOU|Voice|UI")
	TMap<EMicIconState, FLinearColor> IconTints;

	/**
	 * 음량 바의 상한을 감도의 몇 배로 잡을지.
	 *
	 * ★ 고정 상수로 잡으면 안 된다. 마이크마다 출력이 몇 배씩 달라서, 어떤
	 *   팀원 화면에서는 바가 항상 꽉 차고 어떤 화면에서는 항상 0 이 된다.
	 *   감도(VadThreshold)는 그 사람 마이크에 맞춰 보정된 값이라 기준으로 쓸 수 있다.
	 *   3배면 "겨우 넘김" 이 1/3 쯤, "크게 말함" 이 꽉 차는 정도가 된다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MOU|Voice|UI")
	float LevelBarHeadroom = 3.f;

	// --- 조회 / 블루프린트 훅 -------------------------------------------------

	/** 지금 아이콘이 나타내는 상태. WBP 애니메이션 분기에 쓴다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Voice|UI")
	EMicIconState GetMicState() const { return CachedState; }

	/**
	 * 상태가 **바뀐 순간에만** 불린다. 깜빡임 같은 연출은 여기서 시작한다.
	 *
	 * ★ 매 틱이 아니라 변경 시에만인 것이 중요하다. 매 틱 애니메이션을 다시
	 *   재생시키면 첫 프레임에서 멈춘 것처럼 보인다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MOU|Voice|UI")
	void OnMicStateChanged(EMicIconState NewState, EMicIconState OldState);

protected:

	/**
	 * WBP 에 같은 이름의 Image 가 있으면 자동으로 연결된다.
	 *
	 * ★ StatusText 와 달리 BuildDefaultLayout 이 만들어주지 않는다. UImage 는
	 *   텍스처가 있어야 의미가 있는데 그 애셋은 에디터에서만 지정할 수 있어서다.
	 *   WBP 를 안 쓰면 이 값은 null 이고, 아이콘 갱신은 통째로 건너뛴다.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Voice")
	TObjectPtr<UImage> MicIcon;

	/** 입력 음량 바. 위와 같은 이유로 WBP 에만 있다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Voice")
	TObjectPtr<UProgressBar> LevelBar;

	/** WBP 에 같은 이름의 TextBlock 이 있으면 자동으로 연결된다. 없으면 BuildDefaultLayout 이 만든다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Voice")
	TObjectPtr<UTextBlock> StatusText;

private:
	void BuildDefaultLayout();
	void RefreshStatusText();
	UVoiceSubsystem* GetVoiceSubsystem() const;

	/**
	 * 원본 상태를 아이콘 상태 하나로 축약한다. **UI 를 전혀 모른다.**
	 *
	 * ★ 텍스트 표시와 아이콘 표시가 이 함수 하나를 같이 쓴다. 판정을 두 벌
	 *   두면 상태를 하나 추가할 때 한쪽만 고치게 되고, 그러면 글자와 아이콘이
	 *   서로 다른 말을 한다.
	 */
	EMicIconState EvaluateMicState() const;

	/** 축약된 상태를 실제 브러시/색으로 옮긴다. MicIcon 이 없으면 아무것도 안 한다. */
	void ApplyMicState(EMicIconState NewState);

	/**
	 * 음량 바를 갱신한다. **RefreshInterval 과 무관하게 매 프레임 부른다.**
	 *
	 * ★ 0.1초마다 갱신하면 눈에 띄게 끊긴다. 음량은 이산 상태가 아니라
	 *   연속값이라 아이콘과 갱신 주기를 따로 가져가야 한다.
	 */
	void UpdateLevelBar(float InDeltaTime);

	/** BindKey 델리게이트는 UFUNCTION 이 아니어도 되지만, 다른 위젯 콜백들과 통일해 둔다. */
	void HandleMuteKeyPressed();

	float TimeSinceLastRefresh = 0.f;

	/** 마지막으로 적용한 상태. 바뀔 때만 브러시를 갈아끼우려고 들고 있다. */
	EMicIconState CachedState = EMicIconState::NoDevice;

	/** 아직 한 번도 적용한 적이 없다. 첫 갱신은 CachedState 와 같아도 반영해야 한다. */
	bool bMicStateApplied = false;

	/** 화면에 그려지는 음량(0~1). 원본을 보간한 값이다. */
	float DisplayLevel = 0.f;
};
