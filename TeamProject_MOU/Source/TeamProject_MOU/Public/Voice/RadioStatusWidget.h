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
// [★ WBP 아이콘을 쓸 때의 구조]
//
//   UVoiceStatusWidget 과 완전히 같은 원칙이다. C++ 는 상태를 ERadioIconState
//   하나로 축약해 넘기고, 그림은 WBP 가 정한다.
//
//     Image        RadioIcon  ← 상태별 그림 (IconBrushes 로 지정)
//     ProgressBar  BatteryBar ← 배터리 잔량
//     TextBlock    StatusText ← 진단용 글자
//
//   아이콘 4장이 그대로 대응한다 - 전원 OFF / 전원 ON / 송신 / 수신.
//   무전기가 없을 때는 아이콘이 없다. 위젯째로 접는다(bHideWhenNoRadio) -
//   무전기를 안 가진 것은 정상 상태라 화면을 차지할 이유가 없기 때문이다.
//   마이크와 다른 점이다(마이크 없음은 설정이 잘못됐다는 경고다).
//
//   ★ 배터리 바는 보간하지 않는다. 마이크의 음량 바와 달리 배터리는 튀는
//     값이 아니라서, 보간을 걸면 실제 잔량보다 늦게 따라와 "곧 꺼진다" 를
//     늦게 알리게 된다.
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 7-3절(전원/송신), 14절 V6

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "Styling/SlateBrush.h"
#include "Voice/VoiceTypes.h"
#include "RadioStatusWidget.generated.h"

class APawn;
class ARadio;
class UImage;
class UWidget;
class UProgressBar;
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

	// --- WBP 아이콘 설정 ------------------------------------------------------

	/**
	 * 상태별 무전기 아이콘. WBP 의 클래스 디폴트에서 지정한다.
	 *
	 * 아이콘 4장 - 전원 OFF / 전원 ON / 송신 / 수신 - 이 그대로 대응한다.
	 * None 은 넣지 않는다(아래 bHideWhenNoRadio 로 위젯째 숨긴다).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MOU|Radio|UI")
	TMap<ERadioIconState, FSlateBrush> IconBrushes;

	/**
	 * 무전기가 없을 때 위젯을 통째로 숨길지.
	 *
	 * ★ 마이크와 다르다. 무전기를 안 가진 것은 **정상 상태**라서 화면을 차지할
	 *   이유가 없다(마이크 없음은 설정이 잘못됐다는 경고라 항상 떠야 한다).
	 *
	 *   테스트 중에는 꺼 두는 것이 낫다 - 숨겨버리면 "무전기를 못 찾은 것" 과
	 *   "위젯이 아예 안 뜬 것" 을 구분할 수 없다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOU|Radio|UI")
	bool bHideWhenNoRadio = true;

	// --- 조회 / 블루프린트 훅 -------------------------------------------------

	/** 지금 아이콘이 나타내는 상태. WBP 애니메이션 분기에 쓴다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Radio|UI")
	ERadioIconState GetRadioState() const { return CachedState; }

	/** 배터리가 곧 바닥나는가. 깜빡임 같은 연출은 WBP 에서 이 값으로 건다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Radio|UI")
	bool IsBatteryLow() const { return bBatteryLow; }

	/**
	 * 상태가 **바뀐 순간에만** 불린다.
	 *
	 * ★ 매 틱이 아니라 변경 시에만인 것이 중요하다. 매 틱 애니메이션을 다시
	 *   재생시키면 첫 프레임에서 멈춘 것처럼 보인다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MOU|Radio|UI")
	void OnRadioStateChanged(ERadioIconState NewState, ERadioIconState OldState);

protected:
	/**
	 * WBP 에 같은 이름의 Image 가 있으면 자동 연결된다.
	 *
	 * ★ StatusText 와 달리 BuildDefaultLayout 이 만들어주지 않는다. UImage 는
	 *   텍스처가 있어야 의미가 있는데 그 애셋은 에디터에서만 지정할 수 있어서다.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Radio")
	TObjectPtr<UImage> RadioIcon;

	/** 배터리 잔량 바. 위와 같은 이유로 WBP 에만 있다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Radio")
	TObjectPtr<UProgressBar> BatteryBar;

	/**
	 * 무전기가 없을 때 접을 대상. WBP 의 최상위 패널에 이 이름을 주면 된다.
	 *
	 * ★ 왜 위젯 자신(this)을 접지 않는가 - **틱을 잃을 위험이 있어서다.**
	 *   이 위젯은 NativeTick 에서 폴링해 "무전기가 생겼는지" 를 스스로 알아낸다.
	 *   그런데 자기 자신을 Collapsed 로 만들면, 그 이후 틱이 도는지가 위젯이
	 *   어느 패널에 담겼는지에 달리게 된다. 한 번이라도 안 돌면 **다시 켤 사람이
	 *   아무도 없다** - 무전기를 주워도 영영 안 나타난다.
	 *
	 *   그래서 뿌리는 항상 살려두고 **내용물만** 접는다. 빈 위젯이 틱만 도는
	 *   비용은 0.1초에 한 번 무전기를 찾는 것뿐이다.
	 *
	 *   지정하지 않으면 아래 개별 위젯(RadioIcon/BatteryBar/StatusText)을 각각
	 *   접는다. WBP 에 배경 이미지처럼 따로 둔 장식이 있으면 그것까지 접히도록
	 *   패널 하나를 ContentRoot 로 지정하는 편이 낫다.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Radio")
	TObjectPtr<UWidget> ContentRoot;

	/** WBP 에 같은 이름의 TextBlock 이 있으면 자동 연결된다. 없으면 BuildDefaultLayout 이 만든다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MOU|Radio")
	TObjectPtr<UTextBlock> StatusText;

private:
	void BuildDefaultLayout();
	void RefreshStatusText();

	/**
	 * 무전기의 원본 상태를 아이콘 상태 하나로 축약한다. **UI 를 전혀 모른다.**
	 *
	 * ★ 텍스트 표시와 아이콘 표시가 이 함수 하나를 같이 쓴다(UVoiceStatusWidget
	 *   과 같은 이유). 판정이 두 벌이면 글자와 아이콘이 서로 다른 말을 한다.
	 */
	ERadioIconState EvaluateRadioState() const;

	/** 축약된 상태를 브러시/색/가시성으로 옮긴다. */
	void ApplyRadioState(ERadioIconState NewState);

	/**
	 * 내용물을 보이거나 접는다. **위젯 자신은 건드리지 않는다**(ContentRoot 주석의 ★).
	 *
	 * ContentRoot 가 지정돼 있으면 그것만, 없으면 RadioIcon/BatteryBar/StatusText 를
	 * 각각 접는다.
	 */
	void SetContentVisible(bool bVisible);

	/** 배터리 바를 갱신한다. 보간하지 않는다 - 배터리는 튀는 값이 아니다. */
	void UpdateBatteryBar();

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

	/** 마지막으로 적용한 상태. 바뀔 때만 브러시를 갈아끼우려고 들고 있다. */
	ERadioIconState CachedState = ERadioIconState::None;

	/** 아직 한 번도 적용한 적이 없다. 첫 갱신은 CachedState 와 같아도 반영해야 한다. */
	bool bRadioStateApplied = false;

	/** 배터리 경고 구간인가. WBP 가 IsBatteryLow 로 읽어간다. */
	bool bBatteryLow = false;
};
