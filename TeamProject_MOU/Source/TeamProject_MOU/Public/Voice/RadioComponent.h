// MOU 음성 - 무전기 컴포넌트.
//
// [이 파일이 시스템 어디에 있나]
//
//   무전기 아이템 액터 (AItemBase 를 상속한 클래스)
//     └ ★ URadioComponent  ← 이 파일
//          │  · 전원 ON/OFF (서버 권위)
//          │  · 소리 반경 (에디터에서 조절)
//          ▼ 켜질 때 등록 / 꺼질 때 해제
//        UVoiceRouter 의 무전기 레지스트리 (서버)
//          ▼ 무전이 오면 이 무전기 반경 안의 사람에게 전달
//        UVoicePlaybackComponent 가 **이 액터 위치에서** 소리를 낸다
//
// [★ 왜 플레이어가 아니라 아이템에 붙는가 - 이 설계의 핵심]
//
//   **무전기는 떨어뜨릴 수 있는 물건이기 때문이다.**
//   플레이어에 붙이면 "죽어서 떨군 무전기가 계속 소리를 내는 것" 을 표현할 수 없다.
//   아이템 액터에 두면:
//
//     손에 들고 있을 때  → 액터가 플레이어에 붙어 있으니 소리도 플레이어 위치에서
//     바닥에 떨어졌을 때 → 액터가 바닥에 있으니 소리도 거기서
//
//   **코드가 두 경우를 구분할 필요가 전혀 없다.** 항상 "이 액터 위치" 하나만 쓰면
//   되고, 시체 주변이 위험지대가 되는 연출이 규칙 하나에서 자동으로 나온다.
//
// [★★ 전원 OFF 는 "음소거" 가 아니라 "기기 종료" 다]
//
//   이 구분을 흐리면 구현이 통째로 틀어진다(7-3절).
//
//   | | 음소거 (❌) | 기기 종료 (✅ 이 설계) |
//   |---|---|---|
//   | 서버 | 프레임을 계속 보냄 | **레지스트리에서 빠져 아예 안 감** |
//   | 클라 | 받아놓고 재생만 안 함 | 받을 것이 없다 |
//   | 개조 클라 | **들린다** (프레임이 손에 있으므로) | 못 듣는다 |
//
//   그래서 SetPowered(false) 는 볼륨을 0 으로 만드는 것이 아니라
//   **라우터 레지스트리에서 자기를 제거하는 것**이다.
//
// [팀원에게 - 아이템 파트가 할 일]
//   무전기 아이템 클래스(AItemBase 상속)에 이 컴포넌트를 붙이기만 하면 된다.
//   음성 시스템이 아이템에 요구하는 것은 이 컴포넌트 하나뿐이고, 반대로
//   이 컴포넌트는 AItemBase 를 전혀 모른다 - 어떤 액터에 붙여도 동작한다.
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 6절(클래스), 7-3절(전원/송신), 7-4절(스피커 소리), 10절

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Voice/VoiceTypes.h"
#include "RadioComponent.generated.h"

class APlayerState;

/**
 * 무전기 한 대. **아이템 액터에 붙인다** (플레이어가 아니라).
 */
UCLASS(ClassGroup = (MOU), meta = (BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API URadioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URadioComponent();

	// --- 전원 ---------------------------------------------------------------

	/**
	 * 전원을 켜고 끈다. **서버에서만 호출한다.**
	 *
	 * 켜면 라우터 레지스트리에 등록되어 무전을 받기 시작하고,
	 * 끄면 제거되어 **프레임이 아예 오지 않는다**(위 ★★ 참고).
	 */
	UFUNCTION(BlueprintCallable, Category = "Radio")
	void SetPowered(bool bOn);

	UFUNCTION(BlueprintPure, Category = "Radio")
	bool IsPoweredOn() const { return bPoweredOn; }

	// --- 조회 ---------------------------------------------------------------

	/**
	 * 이 무전기를 지금 들고 있는 사람. **없으면 바닥에 떨어진 것이다.**
	 *
	 * 부착 관계로 판단한다 - 아이템이 캐릭터에 붙어 있으면 그 사람이 주인이다.
	 * AItemBase 의 소유권 필드를 보지 않는 이유는 **이 컴포넌트가 특정 아이템
	 * 클래스에 묶이지 않게** 하기 위해서다. 어떤 액터에 붙여도 동작해야 한다.
	 */
	UFUNCTION(BlueprintPure, Category = "Radio")
	APlayerState* GetHolder() const;

	/**
	 * 소리가 나야 하는 위치.
	 *
	 * 손에 있든 바닥에 있든 **언제나 이 액터의 위치**다 - 그게 이 설계가
	 * 두 경우를 구분하지 않아도 되는 이유다(위 ★).
	 */
	UFUNCTION(BlueprintPure, Category = "Radio")
	FVector GetSpeakerLocation() const;

	// --- 튜닝 (에디터에서 아이템마다 조절) -----------------------------------

	/** 사람이 이 무전기 소리를 들을 수 있는 거리(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio|Sound")
	float SpeakerHearRadius = MOUVoice::DefaultSpeakerHearRadius;

	/**
	 * ★ NPC 가 이 무전기 소리를 들을 수 있는 거리(cm). **밸런스의 주 손잡이다.**
	 *
	 * 사람이 듣는 거리보다 일부러 넓게 잡는다 - "나한텐 안 들렸는데 NPC 는
	 * 들었다" 가 있어야 무전을 켤지 말지가 진짜 선택이 된다(7-4절).
	 * V8 에서 소음 이벤트의 MaxRange 로 그대로 들어간다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio|Sound")
	float SpeakerNoiseRadius = MOUVoice::DefaultSpeakerNoiseRadius;

	/** 스피커 출력 음량 배율. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio|Sound")
	float SpeakerVolume = 1.0f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * 전원 상태가 복제돼 왔을 때. **클라에서만 불린다.**
	 *
	 * 클라는 레지스트리와 무관하다(그건 서버 것이다). 여기서는 표시 갱신 같은
	 * 연출만 한다 - V7 의 노이즈 베드를 켜고 끄는 자리이기도 하다.
	 */
	UFUNCTION()
	void OnRep_Power();

private:
	/**
	 * 전원. 서버가 정하고 클라에 복제된다.
	 *
	 * 클라가 이 값을 알아야 하는 이유: 무전기를 켰는지 껐는지 화면에 보여줘야 하고,
	 * 켜져 있는 동안 나는 잡음(V7)을 재생해야 한다. **판정은 서버가 한다.**
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Power)
	bool bPoweredOn = false;

	/** 지금 라우터 레지스트리에 등록돼 있는지(서버 전용). 중복 등록을 막는다. */
	bool bRegistered = false;

	/** 라우터에 등록/해제한다. 서버 전용. */
	void UpdateRegistration();
};
