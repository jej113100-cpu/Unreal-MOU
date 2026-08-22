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

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "VoiceStatusWidget.generated.h"

class UTextBlock;
class UVoiceSubsystem;

/**
 * 화면 한쪽에 항상 떠 있는 마이크 상태 표시.
 *
 * 상태 세 가지만 구분한다: 마이크 없음 / 음소거 / 켜짐(말하는 중이면 강조).
 * 그 이상(감도 게이지 등)은 V9 옵션 화면 몫이다 - 이건 "지금 내가 들리는가" 를
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

protected:
	/** WBP 에 같은 이름의 TextBlock 이 있으면 자동으로 연결된다. 없으면 BuildDefaultLayout 이 만든다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Voice")
	TObjectPtr<UTextBlock> StatusText;

private:
	void BuildDefaultLayout();
	void RefreshStatusText();
	UVoiceSubsystem* GetVoiceSubsystem() const;

	/** BindKey 델리게이트는 UFUNCTION 이 아니어도 되지만, 다른 위젯 콜백들과 통일해 둔다. */
	void HandleMuteKeyPressed();

	float TimeSinceLastRefresh = 0.f;
};
