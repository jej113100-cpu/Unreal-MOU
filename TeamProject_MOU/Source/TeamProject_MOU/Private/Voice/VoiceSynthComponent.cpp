// MOU 음성 - 음성 재생 출구 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7-2절, 11절
//
// [★ OnGenerateAudio 는 오디오 렌더 스레드다. 헤더 주석의 금지 목록을 반드시 지킬 것.]

#include "Voice/VoiceSynthComponent.h"

#include "Components/AudioComponent.h"
#include "Engine/Attenuation.h"
#include "Sound/SoundAttenuation.h"

UVoiceSynthComponent::UVoiceSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 모노. 음성은 스테레오로 만들 이유가 없고, 공간화는 오디오 엔진이 한다.
	NumChannels = MOUVoice::NumChannels;

	// 기본은 2D 다. 루프백(내 목소리를 내가 듣기)은 월드 위치가 없으므로 그게 맞다.
	// 근접 재생은 SetProximityMode() 가 켠다.
	bAllowSpatialization = false;

	// 이 컴포넌트는 액터에 붙은 채로 계속 살아있고 필요할 때만 소리를 낸다.
	// 자동 활성화해두지 않으면 PushSamples 를 해도 아무 소리가 안 난다.
	bAutoActivate = true;
}

namespace
{
	/**
	 * 발화 모드에 맞는 근접 감쇠 설정을 만든다.
	 *
	 * 숫자는 전부 MOUVoice:: 함수에서 온다. 여기에 상수를 직접 적으면 안 된다 -
	 * 서버의 라우팅 반경, 디버그 링, V8 의 NPC 소음 반경이 같은 함수를 보고 있어서
	 * 여기만 다른 값을 쓰면 **화면에 보이는 원과 실제로 들리는 거리가 조용히 어긋난다.**
	 */
	FSoundAttenuationSettings MakeProximityAttenuation(EVoiceMode Mode, float RadiusScale)
	{
		FSoundAttenuationSettings Settings;

		Settings.bAttenuate  = true;
		Settings.bSpatialize = true;

		// 구형 감쇠. AttenuationShapeExtents.X 가 "음량 100% 를 유지하는 반경" 이고,
		// FalloffDistance 는 **그 바깥으로 추가되는** 감쇠 구간이다.
		// 총 가청 거리는 둘의 합이며, VoiceTypes.h 의 static_assert 가 이를 보증한다.
		//
		// ★ 음량 배율은 **두 값에 똑같이** 곱한다.
		//
		//   한쪽만 곱하면 합(=총 가청 거리)이 GetScaledHearRadius 와 어긋나서,
		//   디버그 링과 NPC 소음 반경이 가리키는 거리에서 소리가 실제로 0 이
		//   되지 않는다 - static_assert 가 컴파일 타임에 막아주는 그 어긋남을
		//   런타임에 다시 만드는 셈이다. 같은 배율이면 비율이 유지되므로
		//   "가까이서 100% 로 들리는 구간" 도 함께 줄어 자연스럽다.
		const float Scale = FMath::Clamp(RadiusScale, 0.f, 1.f);

		Settings.AttenuationShape = EAttenuationShape::Sphere;
		Settings.AttenuationShapeExtents =
			FVector(MOUVoice::GetAttenuationRadius(Mode) * Scale, 0.f, 0.f);
		Settings.FalloffDistance = MOUVoice::GetAttenuationFalloff(Mode) * Scale;

		// dB 기반이라 사람 귀에 가장 자연스럽다(7-2절).
		Settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;

		// ★★ 이 한 줄이 없으면 "경계에서 목소리가 뚝 끊긴다".
		//
		//   기본값은 Continues 다 - 엔진 주석 그대로 "가청 거리를 넘어가도 계속
		//   감쇠할 뿐 완전히 꺼지지는 않는다". 그런데 **서버는 반경 밖 사람에게
		//   프레임을 아예 안 보낸다.** 그러면 클라는 아직 들리려 하는데 데이터가
		//   끊겨서, 감쇠로 서서히 사라지는 대신 그 지점에서 잘려나간다.
		//
		//   Silent 로 두어야 클라 감쇠가 0 이 되는 거리와 서버가 끊는 거리가 맞는다.
		//   (서버의 1.2배 여유는 이 정렬을 위한 안전 마진이다 - 7-2절)
		Settings.FalloffMode = ENaturalSoundFalloffMode::Silent;

		// 공기 흡수. 멀수록 고음이 깎여 먹먹해진다 - 거리감이 확 산다. 싸다.
		Settings.bAttenuateWithLPF = true;

		// 벽 뒤 목소리가 막힌다. 숨바꼭질 게임에는 사실상 필수다.
		//
		// ★ 비용 주의: **동시 발화자 수 = 매 프레임 트레이스 수**다.
		//   프로파일링에서 걸리면 여기를 먼저 끈다(무전기 스피커부터, 7-2절).
		Settings.bEnableOcclusion = true;

		return Settings;
	}
}

void UVoiceSynthComponent::SetProximityMode(EVoiceMode Mode, float RadiusScale)
{
	const float ClampedScale = FMath::Clamp(RadiusScale, 0.f, 1.f);

	// 매 프레임 불려도 되도록 조기 반환한다.
	// 감쇠 갱신은 오디오 스레드로 명령을 보내는 일이라 공짜가 아니다.
	//
	// ★ 모드는 "같은가" 로, 배율은 "충분히 벌어졌는가" 로 비교한다 - 배율은
	//   음량을 따라 매 프레임 조금씩 움직이는 연속값이라 완전히 같아지는
	//   경우가 거의 없다. 여기가 정확히 같은지를 보면 사실상 매번 통과한다.
	if (bSpatialConfigured && CurrentMode == Mode
		&& FMath::Abs(CurrentRadiusScale - ClampedScale) < MOUVoice::RadiusScaleUpdateEpsilon)
	{
		return;
	}

	const FSoundAttenuationSettings Settings = MakeProximityAttenuation(Mode, ClampedScale);

	// ★ bAllowSpatialization 은 **사운드를 만들 때** 읽힌다.
	//   Start() 뒤에 켜도 그 사운드는 끝까지 2D 로 난다. 그래서 첫 호출은
	//   반드시 Start() 전이어야 한다(헤더 주석).
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	AttenuationOverrides = Settings;

	// 이미 소리가 나고 있으면 살아있는 사운드에 갈아끼운다.
	//
	// 위에서 멤버만 바꾸면 **이미 재생 중인 사운드에는 반영되지 않는다** -
	// 다음에 Start() 를 다시 부를 때까지 옛 반경이 유지된다. 말하는 도중
	// 속삭임에서 외침으로 바꾸면 반경이 안 늘어나는 버그가 된다.
	// Stop/Start 로 다시 만들면 반영되지만 그 순간 소리가 끊겨 딸깍거린다.
	if (UAudioComponent* AudioComp = GetAudioComponent())
	{
		AudioComp->AdjustAttenuation(Settings);
	}

	CurrentMode = Mode;
	CurrentRadiusScale = ClampedScale;
	bSpatialConfigured = true;
}

// ---------------------------------------------------------------------------
// 무전 톤 (V7)
// ---------------------------------------------------------------------------

namespace
{
	/**
	 * 무전 대역. 사람 목소리에서 명료도를 담당하는 구간만 남긴다.
	 *
	 * 전화가 300~3400Hz 를 쓰는 것과 같은 이유다. 저음(가슴 울림)과
	 * 고음(공기 소리)을 깎아내면 **대역이 좁아서 오히려 "기계를 통해 온 소리"** 로
	 * 들린다 - 무전기다움의 절반은 이 좁은 대역에서 나온다.
	 */
	constexpr float GRadioHighPassHz = 400.f;   // 이 아래를 깎는다
	constexpr float GRadioLowPassHz  = 2800.f;  // 이 위를 깎는다

	/** 찌그러짐 강도. 입력을 키워 소프트 클리핑에 밀어넣는 배율이다. */
	constexpr float GRadioDriveGain = 2.6f;

	/** 최종 출력 보정. 필터와 클리핑으로 줄어든 음량을 되돌린다. */
	constexpr float GRadioOutputGain = 1.5f;

	/** 항상 깔리는 잡음(hiss)의 크기. 너무 크면 말소리를 덮는다. */
	constexpr float GRadioNoiseLevel = 0.006f;

	/** 일차 필터 계수를 차단 주파수에서 구한다. */
	float MakeOnePoleCoefficient(float CutoffHz, float SampleRate)
	{
		// 표준 일차 RC 필터의 이산 근사. 정확한 응답보다 **싸고 안정적인 것**이
		// 중요하다 - 이 계산은 설정할 때 한 번만 하지만, 필터 자체는
		// 샘플마다 도는 자리이기 때문이다.
		const float RC = 1.f / (2.f * PI * FMath::Max(CutoffHz, 1.f));
		const float DT = 1.f / FMath::Max(SampleRate, 1.f);
		return DT / (RC + DT);
	}

	/**
	 * 소프트 클리핑. 하드 클리핑(단순 Clamp)보다 훨씬 자연스럽다.
	 *
	 * 하드 클리핑은 파형을 각지게 잘라 **날카로운 고조파**를 만든다 - 무전기가
	 * 아니라 망가진 스피커처럼 들린다. 부드럽게 눌러야 "찌그러진 무전" 이 된다.
	 */
	float SoftClip(float Sample)
	{
		if (Sample > 1.f)  return 1.f;
		if (Sample < -1.f) return -1.f;

		// x - x^3/3 은 tanh 의 값싼 근사다. 렌더 스레드라 초월함수를 피한다.
		return Sample - (Sample * Sample * Sample) / 3.f;
	}
}

void UVoiceSynthComponent::SetRadioMode(float HearRadius)
{
	// 무전기 스피커의 감쇠는 근접과 성격이 다르다.
	//
	// ★ Radius(100% 구간)를 거의 0 으로 둔다 - **가까이 가야만 또렷하게** 들리고
	//   조금만 떨어져도 웅얼거리게 만들기 위해서다(7-2절). 근접 음성은 반대로
	//   가까운 거리에서 또렷한 구간이 넓다.
	FSoundAttenuationSettings Settings;

	Settings.bAttenuate  = true;
	Settings.bSpatialize = true;
	Settings.AttenuationShape = EAttenuationShape::Sphere;
	Settings.AttenuationShapeExtents = FVector(0.f, 0.f, 0.f);
	Settings.FalloffDistance = FMath::Max(HearRadius, 1.f);
	Settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;

	// 근접과 같은 이유로 Silent 다 - 서버가 이 거리 밖으로 프레임을 안 보내므로,
	// 클라 감쇠도 같은 지점에서 0 이 돼야 경계에서 뚝 끊기지 않는다.
	Settings.FalloffMode = ENaturalSoundFalloffMode::Silent;

	Settings.bAttenuateWithLPF = true;

	// ★ 무전기 스피커는 차폐를 끄는 쪽을 먼저 고려한다(7-2절).
	//   동시 발화자 수 = 트레이스 수인데, 무전은 여러 무전기가 동시에 울릴 수 있어
	//   근접보다 소스가 많아지기 쉽다. 지금은 켜두고 프로파일링에서 걸리면 끈다.
	Settings.bEnableOcclusion = true;

	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	AttenuationOverrides = Settings;

	if (UAudioComponent* AudioComp = GetAudioComponent())
	{
		AudioComp->AdjustAttenuation(Settings);
	}

	bSpatialConfigured  = true;
	bRadioFilterEnabled = true;
}

void UVoiceSynthComponent::ApplyRadioFilter(float* Audio, int32 NumSamples)
{
	// ★★ 오디오 렌더 스레드다. 할당·락·UObject·UE_LOG 전부 금지(헤더 상단).
	//     아래는 float 산술과 멤버 상태 갱신뿐이다.

	const float SampleRate = static_cast<float>(MOUVoice::SampleRate);

	// 계수는 상수에서 나오므로 매번 같지만, 렌더 스레드에서 계산해도
	// 나눗셈 두 번이라 무시할 수 있다. 미리 캐시하면 샘플레이트가 바뀔 때
	// 갱신을 잊는 위험이 생겨서 오히려 손해다.
	const float HighPassAlpha = MakeOnePoleCoefficient(GRadioHighPassHz, SampleRate);
	const float LowPassAlpha  = MakeOnePoleCoefficient(GRadioLowPassHz, SampleRate);

	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		float Sample = Audio[Index];

		// 1) 하이패스 - 저음을 뺀다.
		//    일차 로우패스로 저음 성분을 뽑아낸 뒤 원본에서 빼는 방식이다.
		HighPassState += HighPassAlpha * (Sample - HighPassState);
		Sample -= HighPassState;

		// 2) 찌그러뜨린다. 대역을 좁힌 **뒤에** 해야 한다 -
		//    먼저 찌그러뜨리면 거기서 생긴 고조파를 다음 필터가 도로 깎아낸다.
		Sample = SoftClip(Sample * GRadioDriveGain);

		// 3) 로우패스 두 단 - 고음을 깎는다.
		//    한 단으로는 기울기가 완만해서 여전히 맑게 들린다. 두 번 겹쳐야
		//    "작은 스피커에서 나오는" 느낌이 난다.
		LowPassState1 += LowPassAlpha * (Sample - LowPassState1);
		LowPassState2 += LowPassAlpha * (LowPassState1 - LowPassState2);
		Sample = LowPassState2;

		// 4) 잡음을 얹는다.
		//    ★ 이게 무전기다움의 큰 부분이다 - 완전히 깨끗한 무음은
		//      "기계를 통해 온 소리" 로 안 들린다.
		//    난수는 직접 돌린다. FMath::Rand 는 전역 상태를 건드려
		//    렌더 스레드에서 부르면 안 된다.
		NoiseSeed = NoiseSeed * 1664525u + 1013904223u;
		const float Noise = (static_cast<float>(NoiseSeed >> 8) / 8388608.f) - 1.f; // -1~1
		Sample += Noise * GRadioNoiseLevel;

		// 5) 음량을 되돌리고 최종 안전 클램프.
		Audio[Index] = FMath::Clamp(Sample * GRadioOutputGain, -1.f, 1.f);
	}
}

bool UVoiceSynthComponent::Init(int32& SampleRate)
{
	// 오디오 엔진에게 "나는 16kHz 로 낸다" 고 알린다.
	// 엔진 출력이 48kHz 여도 오디오 믹서가 리샘플링해준다.
	SampleRate = MOUVoice::SampleRate;

	// 링버퍼를 미리 잡는다. OnGenerateAudio 안에서는 할당을 할 수 없으므로
	// 반드시 여기(게임 스레드)에서 최종 크기를 확보해야 한다.
	RingBuffer.SetCapacity(MOUVoice::SamplesPerFrame * MOUVoice::PlaybackBufferFrames);

	return true;
}

void UVoiceSynthComponent::PushSamples(const int16* Samples, int32 NumSamples)
{
	if (!Samples || NumSamples <= 0)
	{
		return;
	}

	// PCM16 -> float(-1~1). 오디오 엔진은 float 로 다룬다.
	//
	// 스택 배열을 쓰는 이유: 매 프레임 TArray 를 할당하면 20ms 마다 힙을 때려
	// GC 압력과 프레임 스파이크가 생긴다.
	//
	// ★ 320샘플씩 끊어서 도는 이유 - 예전에는 `Min(NumSamples, 320)` 으로
	//   **넘치는 만큼을 그냥 버렸다.** 지금은 항상 320개만 들어오니 티가 안 나지만,
	//   V4 의 지터버퍼가 여러 프레임을 한 번에 밀어넣거나 디코더가 한 패킷에서
	//   여러 프레임을 풀어내면 **뒷부분이 조용히 사라진다.** 소리가 미묘하게
	//   끊기는데 카운터에도 안 잡혀서 원인을 찾을 실마리가 없는 종류의 버그다.
	//   버퍼 크기는 스택 상한일 뿐 처리량 상한이어서는 안 된다.
	float Converted[MOUVoice::SamplesPerFrame];

	const int16* Cursor = Samples;
	int32 Remaining = NumSamples;

	while (Remaining > 0)
	{
		const int32 ChunkSize = FMath::Min(Remaining, MOUVoice::SamplesPerFrame);

		for (int32 Index = 0; Index < ChunkSize; ++Index)
		{
			Converted[Index] = static_cast<float>(Cursor[Index]) / 32768.f;
		}

		const int32 Pushed = RingBuffer.Push(Converted, static_cast<uint32>(ChunkSize));

		// 넘쳐서 못 넣은 만큼을 센다. 이 값이 계속 오르면 입력이 재생보다
		// 빨라 지연이 쌓이고 있다는 뜻이다(= 버퍼를 비워야 한다).
		if (Pushed < ChunkSize)
		{
			// 한 번 가득 찼으면 남은 것도 못 들어간다. 세어두고 그만둔다.
			OverflowCounter.Add(Remaining - Pushed);
			return;
		}

		Cursor    += ChunkSize;
		Remaining -= ChunkSize;
	}
}

int32 UVoiceSynthComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	// ★★ 여기는 오디오 렌더 스레드다.
	//    UObject 접근 / 락 / 메모리 할당 / UE_LOG 전부 금지.
	//    아래 코드는 링버퍼 pop 과 float 산술만 한다.

	// 게임 스레드가 "비워달라" 고 했으면 여기서 비운다.
	// Pop 은 읽기 커서만 앞으로 미는 것이라 소비자 스레드에서 안전하다.
	// (게임 스레드가 직접 비우면 버퍼 메모리를 재할당하게 되어 위험하다)
	const int32 FlushRequest = FlushRequestCounter.GetValue();
	if (FlushRequest != LastHandledFlushRequest)
	{
		LastHandledFlushRequest = FlushRequest;
		RingBuffer.Pop(RingBuffer.Num());
	}

	const int32 Popped = RingBuffer.Pop(OutAudio, static_cast<uint32>(NumSamples));

	if (Popped < NumSamples)
	{
		// 데이터가 모자란 만큼은 무음으로 채운다.
		// 채우지 않으면 이전 버퍼 내용이 그대로 남아 지직거린다.
		FMemory::Memzero(OutAudio + Popped, (NumSamples - Popped) * sizeof(float));

		// 언더런 카운트. 말하고 있지 않을 때는 정상적으로 계속 오르므로,
		// "발화 중인데 오르는지" 로 판단해야 한다.
		UnderrunCounter.Increment();
	}

	// 무전 톤은 **무음 구간에도** 걸어야 한다.
	//
	// 잡음(hiss)이 말할 때만 나면 오히려 어색하다 - 실제 무전기는 켜져 있는 동안
	// 계속 지직거린다. 그래서 Popped 가 아니라 NumSamples 전체에 건다.
	if (bRadioFilterEnabled)
	{
		ApplyRadioFilter(OutAudio, NumSamples);
	}

	// 모자란 부분도 무음으로 채웠으므로 요청받은 만큼 전부 채운 것이다.
	return NumSamples;
}

int32 UVoiceSynthComponent::GetBufferedSampleCount() const
{
	return static_cast<int32>(RingBuffer.Num());
}

void UVoiceSynthComponent::RequestFlush()
{
	// ★ 여기서 RingBuffer 를 직접 건드리면 안 된다.
	//   SetCapacity() 는 내부 TArray 를 재할당하는데, 그 순간 오디오 렌더
	//   스레드가 Pop() 안에서 옛 메모리를 읽고 있으면 use-after-free 다.
	//   플래그만 올리고 실제 비우기는 소비자(렌더 스레드)에게 맡긴다.
	FlushRequestCounter.Increment();

	// 진단 카운터는 게임 스레드에서 지워도 안전하다(원자적이고, 버퍼와 무관하다).
	UnderrunCounter.Reset();
	OverflowCounter.Reset();
}
