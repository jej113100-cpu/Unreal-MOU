// MOU 음성 - 음성 재생 출구 (USynthComponent).
//
// [이 파일이 시스템 어디에 있나]
//
//     UVoiceSubsystem (게임 스레드)
//       │  PushSamples()   ← 게임 스레드가 넣는다
//       ▼ Audio::TCircularAudioBuffer<float>  (SPSC, 락 프리)
//   ★ UVoiceSynthComponent::OnGenerateAudio()  ← 오디오 렌더 스레드가 꺼낸다
//       ▼
//     스피커
//
// [★★ 최우선 주의사항 - OnGenerateAudio 는 게임 스레드가 아니다]
//   오디오 렌더 스레드에서 불린다. 이 함수 안에서 다음을 하면 오디오가 끊기고
//   결국 크래시한다:
//     - UObject 접근 (this 의 UPROPERTY 포함)
//     - 락 (FCriticalSection 등)
//     - 메모리 할당 (TArray::Add, NewObject 등)
//     - UE_LOG
//   허용되는 것은 링버퍼에서 pop 하기와 float 산술뿐이다.
//
//   디버깅이 필요하면 카운터를 올리고 게임 스레드에서 읽는다.
//   (기존 채팅의 "워커 스레드에서 UObject 금지" 와 정확히 같은 규칙이다)
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 7-2절 (재생/감쇠), 11절 (스레드 경계)
//
// [현재 구현 단계]
//   V3. 기본은 2D(루프백용)이고, SetProximityMode() 를 부르면 3D 로 바뀐다.
//   근접 재생은 발신자 폰에 이 컴포넌트를 붙이고 그 함수를 부르는 방식이다.
//   무전 재생(무전기 액터 + 필터)은 V6·V7 에서 붙인다.

#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "DSP/Dsp.h"
#include "Voice/VoiceTypes.h"
#include "VoiceSynthComponent.generated.h"

/**
 * 링버퍼에 쌓인 PCM 을 오디오 엔진에 흘려보내는 컴포넌트.
 *
 * 이 클래스는 "어디서 온 소리인지" 를 모른다. 그냥 주는 대로 낸다.
 * 근접/무전 구분과 3D 부착은 이 컴포넌트를 쓰는 쪽(V3 이후)이 정한다.
 */
UCLASS(ClassGroup = (MOU), meta = (BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UVoiceSynthComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	// USynthComponent 는 FObjectInitializer 생성자만 제공한다(기본 생성자가 없다).
	UVoiceSynthComponent(const FObjectInitializer& ObjectInitializer);

	/**
	 * 재생할 PCM16 샘플을 밀어 넣는다. **게임 스레드에서만 호출한다.**
	 *
	 * 내부에서 float 로 변환해 링버퍼에 push 한다.
	 * 버퍼가 가득 차면 넘치는 만큼은 조용히 버려진다(음성의 정상 동작이다 -
	 * 오래된 소리를 억지로 재생하면 지연만 쌓인다).
	 */
	void PushSamples(const int16* Samples, int32 NumSamples);

	/** 버퍼에 쌓인 샘플 수. 지연 진단용. 게임 스레드에서 호출한다. */
	int32 GetBufferedSampleCount() const;

	/**
	 * 쌓인 소리를 버리라고 요청한다. **게임 스레드에서 호출한다.**
	 *
	 * ★ 여기서 직접 버퍼를 비우지 않는 이유:
	 *   TCircularAudioBuffer::SetCapacity() 는 내부 TArray 를 재할당한다.
	 *   오디오 렌더 스레드가 Pop() 안에서 그 메모리를 읽는 중이면
	 *   해제된 메모리를 읽게 된다(use-after-free).
	 *
	 *   그래서 플래그만 세우고, 실제 비우기는 **소비자인 오디오 렌더 스레드가**
	 *   OnGenerateAudio 안에서 한다. SPSC 버퍼는 소비자만 Pop 할 수 있다.
	 */
	void RequestFlush();

	/**
	 * 오디오 렌더 스레드가 버퍼를 비웠는데도 채워지지 않은 횟수(언더런).
	 * 소리가 끊긴다면 이 값이 오르는지 본다. 게임 스레드에서 읽는다.
	 */
	int32 GetUnderrunCount() const { return UnderrunCounter.GetValue(); }

	/** 버퍼가 넘쳐 버린 샘플 수. 지연이 쌓이는 중이라는 신호다. */
	int32 GetOverflowCount() const { return OverflowCounter.GetValue(); }

	/**
	 * 근접 3D 재생으로 설정한다(발화 모드에 맞는 거리 감쇠 포함).
	 *
	 * **처음 한 번은 반드시 `Start()` 전에 불러야 한다.** 공간화 여부
	 * (`bAllowSpatialization`)는 사운드를 만드는 시점에 읽히기 때문에,
	 * Start() 뒤에 켜면 그 사운드는 끝까지 2D 로 난다.
	 *
	 * 그 뒤로는 매 프레임 불러도 싸다 - 모드가 실제로 바뀔 때만 일한다.
	 * 재생 중 모드가 바뀌면 사운드를 다시 만들지 않고 **감쇠만 갈아끼운다.**
	 * 다시 만들면 말하는 도중에 소리가 끊겼다 이어져 딸깍거린다.
	 */
	void SetProximityMode(EVoiceMode Mode);

	/** 지금 3D 로 설정돼 있는지. 루프백(2D)과 구분하는 데 쓴다. */
	bool IsSpatialized() const { return bSpatialConfigured; }

	/**
	 * 무전기 스피커로 재생하도록 설정한다(V7). **`Start()` 전에 부를 것.**
	 *
	 * 근접과 다른 점 두 가지:
	 *   1. 감쇠 반경이 발화 모드가 아니라 **무전기 속성**에서 온다
	 *      (무전기 스피커 크기는 말하는 사람이 속삭이든 소리치든 그대로다)
	 *   2. **무전 톤 필터**가 걸린다 - 대역을 좁히고 찌그러뜨리고 잡음을 얹는다
	 *
	 * @param HearRadius  사람이 들을 수 있는 총 거리(cm). URadioComponent 의 값.
	 */
	void SetRadioMode(float HearRadius);

	/** 지금 무전 톤이 걸려 있는지. */
	bool IsRadioFiltered() const { return bRadioFilterEnabled; }

protected:
	// --- USynthComponent ---------------------------------------------------

	/** 오디오 엔진에 샘플레이트를 알린다. 게임 스레드에서 불린다. */
	virtual bool Init(int32& SampleRate) override;

	/**
	 * ★ 오디오 렌더 스레드에서 불린다. 위 주의사항을 반드시 지킬 것.
	 * @return 실제로 채운 샘플 수
	 */
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	/**
	 * 게임 스레드 -> 오디오 렌더 스레드 단방향 버퍼.
	 *
	 * Audio::TCircularAudioBuffer 는 SPSC 로 스레드 세이프하다고 엔진 주석에
	 * 명시돼 있다. 반대 방향으로 쓰거나 두 스레드가 같은 쪽을 만지면 안 된다.
	 */
	Audio::TCircularAudioBuffer<float> RingBuffer;

	/**
	 * 진단 카운터.
	 *
	 * OnGenerateAudio 안에서 UE_LOG 를 할 수 없으므로, 문제가 생기면 여기에
	 * 세어두고 게임 스레드가 MOU.Voice.Stat 으로 읽어간다.
	 */
	mutable FThreadSafeCounter UnderrunCounter;
	mutable FThreadSafeCounter OverflowCounter;

	/**
	 * 게임 스레드가 올리고 오디오 렌더 스레드가 확인하는 "비우기 요청" 카운터.
	 *
	 * bool 이 아니라 카운터인 이유: 렌더 스레드가 플래그를 내리는 사이에
	 * 게임 스레드가 다시 요청하면 그 요청이 사라진다. 값이 달라졌는지만 보면
	 * 요청을 놓치지 않는다.
	 */
	FThreadSafeCounter FlushRequestCounter;

	/** 렌더 스레드가 마지막으로 처리한 비우기 요청 번호. **렌더 스레드 전용.** */
	int32 LastHandledFlushRequest = 0;

	/**
	 * 지금 적용돼 있는 발화 모드.
	 *
	 * SetProximityMode 가 매 프레임 불려도 실제 작업을 건너뛰기 위한 캐시다.
	 * 감쇠 갱신은 오디오 스레드로 명령을 보내는 일이라 공짜가 아니다.
	 */
	EVoiceMode CurrentMode = EVoiceMode::Normal;

	/** SetProximityMode 가 한 번이라도 불렸는지. 2D(루프백)와 구분한다. */
	bool bSpatialConfigured = false;

	// -----------------------------------------------------------------------
	// 무전 톤 필터 (V7)
	//
	// ★★ 아래 상태 변수는 **오디오 렌더 스레드 전용**이다.
	//    OnGenerateAudio 안에서만 읽고 쓴다. 게임 스레드에서 만지면 지직거린다.
	//
	// [왜 소스 이펙트 체인 에셋이 아니라 코드인가]
	//   설계 문서 7-4절은 USoundEffectSourcePresetChain 에셋을 쓰라고 했다.
	//   디자이너가 에디터에서 톤을 굴릴 수 있어야 한다는 이유인데, 맞는 말이다.
	//   다만 **지금 그 에셋이 없다.** 에셋을 만들 때까지 무전이 그냥 맑은
	//   목소리로 들리면 "무전기 같은가" 를 판단할 수가 없다.
	//
	//   그래서 코드로 기본 톤을 넣어 지금 들어볼 수 있게 하고, 나중에 에셋이
	//   생기면 SourceEffectChain 프로퍼티(USynthComponent 가 이미 갖고 있다)에
	//   지정하고 이 내장 필터를 끄면 된다. 둘은 배타적이지 않다.
	// -----------------------------------------------------------------------

	/** 무전 톤을 적용할지. 게임 스레드가 켜고 렌더 스레드가 읽는다(bool 이라 원자적). */
	bool bRadioFilterEnabled = false;

	/** 하이패스 상태 - 저음을 깎아 "전화기" 대역으로 만든다. */
	float HighPassState = 0.f;

	/** 로우패스 상태 - 고음을 깎는다. 두 개를 겹쳐 기울기를 키운다. */
	float LowPassState1 = 0.f;
	float LowPassState2 = 0.f;

	/** 잡음 생성용 난수 상태. FMath::Rand 를 렌더 스레드에서 쓰면 안 되므로 직접 돌린다. */
	uint32 NoiseSeed = 0x1234567u;

	/** 무전 톤을 입힌다. **오디오 렌더 스레드에서만 호출한다.** */
	void ApplyRadioFilter(float* Audio, int32 NumSamples);
};
