// MOU 음성 - 무전기 상태 표시 위젯.
//
// [이 위젯이 왜 있는가]
//
//   무전기 설계(Radio.h 의 확정된 규칙표)는 **화면에 아무것도 안 나온다.**
//   전원이 켜졌는지, 배터리가 얼마나 남았는지, 지금 손에 들었는지 인벤토리에
//   있는지, 송신이 실제로 나가고 있는지 - 전부 서버 로그나 콘솔로만 확인할 수
//   있었다. 그래서는 규칙표의 각 칸이 맞는지 **눈으로 검증할 수가 없다.**
//
//   UVoiceStatusWidget 이 마이크에 대해 하는 일을 무전기에 대해 한다.
//
// [이 위젯이 시스템 어디에 있나]
//
//   ARadio / AVoiceDebugRadio 에 붙은 URadioComponent
//     ↓ IsPoweredOn() / IsInHand() / GetBatteryPercent()   (짧은 주기로 폴링)
//   ★ URadioStatusWidget  ← 이 파일
//     ↓ Z 키 -> TogglePower()
//     ↓ X 홀드 -> StartTransmit() / StopTransmit()
//
//   상태가 매 프레임 바뀔 수 있어 델리게이트를 쏘면 낭비다. UVoiceStatusWidget
//   과 같은 이유로 NativeTick 에서 폴링해 텍스트만 갱신한다.
//
// [★ 왜 ARadio 가 아니라 URadioComponent 를 찾는가]
//
//   진짜 아이템은 ARadio 지만, 지금 `MOU.Voice.Radio.Spawn` 이 만드는 것은
//   AVoiceDebugRadio 다. ARadio 만 찾으면 **이 UI 가 정작 지금 테스트에 못 쓰인다.**
//   두 클래스의 공통분모는 URadioComponent 하나뿐이고, 그게 "음성 시스템이
//   아이템에게 요구하는 전부" 라는 원칙과도 맞는다(RadioComponent.h 상단).
//
//   배터리만은 예외다 - CurrentDurability 는 AItemBase 의 것이라 ARadio 에만
//   있다. 그래서 배터리 줄만 ARadio 로 캐스팅해서 그린다.
//
// [★ 이 위젯이 Z/X 를 바인딩한다]
//
//   설계상 Z=전원, X=PTT 인데 **지금까지 어디에도 바인딩돼 있지 않았다**
//   (VoiceSubsystem.cpp 의 무전기 콘솔 명령 주석: "키 바인딩은 V9 몫이라
//   지금은 콘솔로만 조작한다"). 조작과 표시는 세트라서 - 눌렀는데 화면이
//   안 바뀌면 눌린 건지 알 수 없다 - 여기서 같이 건다.
//
//   ChatWidgetBase / UVoiceStatusWidget 과 같은 방식으로 소유 플레이어의
//   레거시 InputComponent 에 직접 건다. 게임 쪽에서 EnhancedInput 액션으로
//   따로 만들 것이면 bBindKeysToOwningPlayer 를 끄면 된다.
//
// [WBP 없이도 동작한다]
//   WidgetTree->RootWidget 이 비어 있으면 BuildDefaultLayout() 이 화면 우하단에
//   텍스트 하나를 C++ 로 조립한다(우상단은 마이크 상태, 좌하단은 채팅이 쓴다).
//   WBP 를 만들려면 이름이 같은 TextBlock(StatusText) 을 배치하면 된다.
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 7-3절(전원/송신), 14절 V6

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "RadioStatusWidget.generated.h"

class APawn;
class ARadio;
class UTextBlock;
class URadioComponent;
class UVoiceSubsystem;

/**
 * 화면 한쪽에 떠 있는 무전기 상태 표시 + Z/X 조작.
 *
 * Radio.h 의 규칙표(손에 듦 / 인벤토리 / 드롭)를 그대로 화면에 옮긴 것이다.
 * 각 줄이 규칙표의 한 열에 대응하므로, 규칙이 지켜지는지 눈으로 검증할 수 있다.
 */
UCLASS()
class TEAMPROJECT_MOU_API URadioStatusWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	URadioStatusWidget(const FObjectInitializer& ObjectInitializer);

	// --- UUserWidget --------------------------------------------------------
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// --- 설정 ---------------------------------------------------------------

	/**
	 * 소유 플레이어의 InputComponent 에 Z/X 를 직접 바인딩할지.
	 *
	 * ★ 기본 true 인 이유: 지금 프로젝트 어디에도 Z/X 바인딩이 없어서, 이걸
	 *   끄면 **무전기를 조작할 방법이 콘솔밖에 없다.** 게임 쪽에서 EnhancedInput
	 *   액션을 만들면 그때 끄고, 그 액션에서 ARadio::TogglePower() /
	 *   StartTransmit() / StopTransmit() 을 직접 부르면 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Radio")
	bool bBindKeysToOwningPlayer = true;

	/** 전원 토글 키. 설계상 Z(Radio.h 상단 ★). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Radio", meta = (EditCondition = "bBindKeysToOwningPlayer"))
	FKey PowerToggleKey = EKeys::Z;

	/**
	 * PTT 키. 설계상 X **홀드**다.
	 *
	 * 누름과 뗌이 둘 다 필요해서 좌클릭(OnUse)으로는 안 되는 것이기도 하다
	 * (Radio.h 상단 ★ 2번).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Radio", meta = (EditCondition = "bBindKeysToOwningPlayer"))
	FKey TransmitKey = EKeys::X;

	/** 상태 텍스트 갱신 주기(초). 매 프레임 문자열을 새로 만들 필요는 없다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Radio")
	float RefreshInterval = 0.1f;

	/**
	 * 스피커 반경(사람/NPC)을 숫자로 같이 보여줄지.
	 *
	 * 인벤토리에 넣으면 StowedRadiusScale 이 곱해지는데, 그게 실제로 적용됐는지
	 * **숫자로 보지 않으면 확인할 방법이 없다.** 밸런스를 만질 때 켜 둔다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Radio")
	bool bShowRadiusInfo = true;

protected:
	/** WBP 에 같은 이름의 TextBlock 이 있으면 자동 연결된다. 없으면 BuildDefaultLayout 이 만든다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Radio")
	TObjectPtr<UTextBlock> StatusText;

private:
	void BuildDefaultLayout();
	void RefreshStatusText();

	/**
	 * 지금 로컬 플레이어가 가지고 있는 무전기의 컴포넌트. 없으면 null.
	 *
	 * ARadio 든 AVoiceDebugRadio 든 상관없이 찾는다(헤더 상단 ★).
	 * 부착 관계를 재귀로 훑으므로 손에 든 것과 인벤토리에 있는 것을 모두 잡는다
	 * (ARadio::FindCarriedBy 와 같은 근거).
	 */
	URadioComponent* FindLocalRadioComponent() const;

	/** 위에서 찾은 것이 진짜 아이템이면 그것을. 테스트 무전기면 null. */
	ARadio* FindLocalRadioItem() const;

	APawn* GetLocalPawn() const;
	UVoiceSubsystem* GetVoiceSubsystem() const;

	// --- 입력 콜백 ----------------------------------------------------------

	/**
	 * Z. 전원을 토글한다.
	 *
	 * ★ 진짜 아이템(ARadio)이면 TogglePower() 가 알아서 서버로 넘긴다.
	 *   테스트 무전기는 그 경로가 없어서 UVoiceComponent 의 디버그 RPC 로 돌린다.
	 */
	void HandlePowerKeyPressed();

	/** X 누름. */
	void HandleTransmitKeyPressed();

	/**
	 * X 뗌.
	 *
	 * ★ 무전기를 못 찾아도 **무조건 송신을 끊는다.** 누른 사이에 무전기를
	 *   떨어뜨렸거나 뺏겼을 때 송신이 켜진 채로 남으면, 본인은 모르는데 계속
	 *   무전이 나간다 - 이 게임에서 그건 위치가 새는 것이다.
	 */
	void HandleTransmitKeyReleased();

	float TimeSinceLastRefresh = 0.f;
};
