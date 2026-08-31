// MOU 음성 - 음성 시스템 전체가 공유하는 타입과 상수.
//
// [이 파일이 시스템 어디에 있나]
//   Voice/ 폴더의 모든 파일이 이 헤더를 include 한다. 여기에는 로직이 없다.
//   설계 문서: 저장소 루트의 VOICE_INTEGRATION.md
//
// [현재 구현 단계]
//   V0 (FakeNoise 콘솔 명령) + V1 (로컬 루프백) + V2 (Opus 코덱) 까지.
//   네트워크 전송(V3)은 아직 없다. 그래서 EVoiceRoute 같은 라우팅 관련 타입은
//   아직 만들지 않았다 - 쓰는 곳이 없는 타입을 미리 만들면 설계가 바뀔 때
//   같이 바꿔야 하는 짐만 된다.
//
// [수정 시 같이 고쳐야 하는 파일]
//   SampleRate/FrameMs 를 바꾸면 VoiceCaptureSource.cpp 의 프레이밍과
//   VoiceSynthComponent 의 링버퍼 크기가 같이 영향을 받는다.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "VoiceTypes.generated.h"

/** 음성 시스템 전용 로그 카테고리. 에디터 출력 로그에서 'LogMOUVoice' 로 필터링한다. */
DECLARE_LOG_CATEGORY_EXTERN(LogMOUVoice, Log, All);

/**
 * 발화 모드. 목소리 크기에 따라 들리는 거리와 NPC 가 듣는 거리가 달라진다.
 * VOICE_INTEGRATION.md 13절의 표가 이 enum 에 대응한다.
 */
UENUM(BlueprintType)
enum class EVoiceMode : uint8
{
	Whisper UMETA(DisplayName = "속삭임"),
	Normal  UMETA(DisplayName = "보통"),
	Shout   UMETA(DisplayName = "외침"),

	/** 범위 검사용. 실제 모드가 아니다. 항상 마지막에 둘 것. */
	MAX UMETA(Hidden),
};


/**
* 마이크 상태 표시용도
* 마이크 없음 , 음소거, 대기상태, 발화상태,사망상태
*/
UENUM(BlueprintType)
enum class EMicIconState : uint8
{
	NoDevice UMETA(DisplayName = "마이크 없음"),
	Muted	 UMETA(DisplayName = "음소거"),
	Idle	 UMETA(DisplayName = "대기상태"),
	Speaking UMETA(DisplayName = "말하는 중"),
	Calibrating UMETA(DisplayName = "감도보정"),
	Dead	 UMETA(DisplayName = "사망상태")
};

/**
* 무전기 상태 표시용도. 아이콘 4장(전원 OFF / 전원 ON / 송신 / 수신)에 대응한다.
*
* ★ None 에는 아이콘이 없다. 무전기를 안 가진 것은 정상 상태라서 위젯 자체를
*   숨긴다 - 마이크와 다르다(마이크 없음은 설정이 잘못됐다는 경고라 항상 뜬다).
*
* ★ 송신과 수신이 동시에 성립하면 **송신이 이긴다.** 이 게임에서 송신은 곧
*   위치가 새는 것이라, 켜진 줄 모르는 쪽이 훨씬 위험하다.
*/
UENUM(BlueprintType)
enum class ERadioIconState : uint8
{
	None         UMETA(DisplayName = "무전기 없음"), // 위젯 Collapsed
	Off          UMETA(DisplayName = "전원 꺼짐"),
	On           UMETA(DisplayName = "전원 켜짐"),
	Transmitting UMETA(DisplayName = "송신 중"),
	Receiving    UMETA(DisplayName = "수신 중"),
};


/**
 * 이 목소리가 어느 경로로 전달되는가.
 *
 * ★ 이 값의 **최종 결정권은 서버에 있다.** 클라가 보내는 값은 "요청" 이고,
 *   서버가 라우팅하며 확정한 값이 재생에 쓰인다(7-3절의 라우트 우선순위).
 *   근접과 무전 양쪽 조건에 다 걸리는 사람에게 둘 다 보내면 같은 목소리가
 *   두 번 겹쳐 들려 에코가 되기 때문이다.
 */
UENUM(BlueprintType)
enum class EVoiceRoute : uint8
{
	/** 거리 기반. 발신자 폰 위치에서 3D 로 난다. */
	Proximity UMETA(DisplayName = "근접"),

	/** 거리 무관. 무전기 액터 위치에서 난다 (V6). */
	Radio     UMETA(DisplayName = "무전"),

	MAX UMETA(Hidden),
};

/**
 * 음성 파이프라인의 고정 상수.
 *
 * 지금은 컴파일 타임 상수다. VOICE_INTEGRATION.md 13절은 최종적으로 이 값들을
 * UDeveloperSettings 로 빼는 것을 계획하고 있지만, V1 에서는 파이프라인이
 * 도는 것을 먼저 확인하는 게 목적이라 상수로 둔다.
 */
namespace MOUVoice
{
	/**
	 * 16kHz 모노.
	 *
	 * 사람 목소리 명료도는 8kHz 대역이면 충분하고(전화가 그렇다), 무전기 톤은
	 * 오히려 대역이 좁을수록 그럴듯하다. 48kHz 스테레오로 올리면 대역폭만 6배가 된다.
	 *
	 * [확인됨] 엔진의 Windows 캡처 구현은 8000~48000 Hz 만 받는다
	 * (VoiceCaptureWindows.cpp 의 CreateCaptureBuffer). 16000 은 안전한 범위 안이다.
	 */
	inline constexpr int32 SampleRate  = 16000;
	inline constexpr int32 NumChannels = 1;

	/**
	 * 프레임 하나의 길이(ms)와 그에 해당하는 샘플 수.
	 *
	 * 20ms 인 이유: Opus 가 지원하는 프레임 길이(2.5/5/10/20/40/60ms) 중
	 * 지연과 오버헤드의 균형점이다.
	 *
	 * ★★ [V2 에서 확인됨] 20ms 는 우연이 아니라 **엔진이 강제하는 값이다.**
	 *
	 *   엔진의 Opus 래퍼는 프레임 길이를 인자로 받지 않는다. 하드코딩돼 있다:
	 *
	 *       // VoiceCodecOpus.cpp
	 *       #define NUM_OPUS_FRAMES_PER_SEC 50
	 *       FrameSize = SampleRate / NUM_OPUS_FRAMES_PER_SEC;   // 16000/50 = 320
	 *
	 *   즉 인코더는 **무조건 320샘플 단위로만** 인코딩한다. 우리 프레이밍이
	 *   마침 같아서 1프레임 = 1 Opus 프레임으로 딱 떨어진다.
	 *
	 *   **FrameMs 를 40 으로 바꾸면 "40ms Opus 프레임"이 되는 게 아니라
	 *   "한 패킷에 20ms 프레임 2개"가 된다.** 엔진 Encode() 가 들어온 바이트를
	 *   320샘플씩 쪼개 여러 번 opus_encode 를 부르고 헤더 하나에 묶기 때문이다.
	 *   대역폭이 줄어드는 것은 맞지만(헤더를 공유하므로) 이유가 13절 설명과 다르다.
	 *   그리고 320 의 배수가 아닌 값으로 바꾸면 나머지가 인코딩되지 않고 버려진다.
	 */
	inline constexpr int32 FrameMs          = 20;
	inline constexpr int32 SamplesPerFrame  = SampleRate * FrameMs / 1000;  // 320
	inline constexpr int32 BytesPerFrame    = SamplesPerFrame * sizeof(int16); // 640

	/**
	 * 프레임 하나의 길이(초). 시간 기반 계산에 쓴다.
	 *
	 * ★ 엔벨로프 진행에는 **실제 경과 시간이 아니라 이 값**을 써야 한다.
	 *   이유는 AdvanceLoudnessEnvelope 주석에 적어두었다.
	 */
	inline constexpr float FrameSeconds = FrameMs / 1000.f;

	// -----------------------------------------------------------------------
	// Opus 코덱 (V2)
	//
	// 엔진의 IVoiceEncoder / IVoiceDecoder 를 그대로 쓴다. 직접 Opus 를 붙이지 않는다.
	// 래퍼는 VoiceCodec.h 이고, 여기 있는 것은 그 래퍼가 쓰는 크기 상수뿐이다.
	// -----------------------------------------------------------------------

	/**
	 * 인코딩 비트레이트(bits/sec).
	 *
	 * 엔진은 Init() 에서 비트레이트를 명시하지 않아 Opus 자동값에 맡긴다.
	 * 우리는 명시적으로 박는다 - 12절의 대역폭 계산(프레임당 ~60바이트)이
	 * 이 숫자를 전제로 하기 때문이다. 자동값에 맡기면 계산의 근거가 사라진다.
	 */
	inline constexpr int32 OpusBitrate = 24000;

	/**
	 * 인코딩 결과를 받을 작업 버퍼 크기(바이트).
	 *
	 * 넉넉하게 잡는다. 이 크기가 곧 인코딩 결과 크기가 되는 것이 아니라
	 * **상한**일 뿐이다(실제 크기는 비트레이트가 정한다). 작게 잡아서 인코더가
	 * 공간 부족으로 실패하는 것이 훨씬 나쁘다.
	 */
	inline constexpr int32 EncodeScratchBytes = 1024;

	/**
	 * ★ 프레임 하나가 이 크기를 넘으면 V3 의 Unreliable RPC 가 조용히 버린다.
	 *
	 * 15절: "Unreliable RPC 는 한 패킷에 들어가야 한다. 페이로드가 커지면
	 * 조용히 버려진다." V3 에서 터지면 원인 찾기가 매우 어려운 종류의 버그라,
	 * **V2 단계에서 미리 감시해서 넘으면 경고를 남긴다.**
	 * 지금 넘는다면 비트레이트나 프레임 길이 설정이 잘못된 것이다.
	 */
	inline constexpr int32 MaxEncodedFrameBytes = 128;

	/**
	 * ★★ 디코딩 결과를 받을 버퍼 크기(샘플 수). **이 값을 줄이면 소리가 안 난다.**
	 *
	 * 20ms 프레임 하나를 디코딩하는데 왜 6프레임짜리 버퍼가 필요한가:
	 * 엔진 디코더가 프레임마다 아래 검사를 하기 때문이다.
	 *
	 *     // VoiceCodecOpus.cpp, FVoiceDecoderOpus::Decode()
	 *     #define MAX_OPUS_FRAMES 6
	 *     if (UncompressedBufferAvail >= (MAX_OPUS_FRAMES * BytesPerFrame))  // 6*640 = 3840
	 *     { ...디코딩... }
	 *     else
	 *     { UE_LOG(..., "Decompression buffer too small to decode voice"); break; }
	 *
	 * **남은 공간이 3840바이트 미만이면 디코딩을 통째로 건너뛴다.**
	 * 320샘플(640바이트)짜리 버퍼를 주면 - 즉 "한 프레임 넣었으니 한 프레임 나오겠지"
	 * 라고 생각한 크기를 주면 - 반환되는 샘플 수가 0 이고, 에러도 아니다.
	 * "인코딩은 되는데 소리만 안 난다" 로 보여서 원인을 엉뚱한 데서 찾게 된다.
	 */
	inline constexpr int32 DecodeScratchSamples = 6 * SamplesPerFrame;  // 1920

	/**
	 * VAD(음성 감지) 임계값. RMS 를 0~1 로 정규화한 값과 비교한다.
	 *
	 * 이 값 아래가 HangoverMs 동안 지속되면 "말이 끝났다" 로 본다.
	 * 마이크 환경에 따라 크게 다르므로 V9 에서 옵션 화면으로 뺄 예정이고,
	 * 그 전까지는 MOU.Voice.Sensitivity 콘솔 명령으로 바꾼다.
	 */
	inline constexpr float DefaultVadThreshold = 0.02f;

	/**
	 * 말끝이 잘리지 않도록 무음이 이만큼 지속돼야 발화 종료로 친다.
	 * 이게 없으면 단어 사이의 짧은 공백마다 발화가 끊겼다 이어진다.
	 */
	inline constexpr float VadHangoverSeconds = 0.2f;

	// -----------------------------------------------------------------------
	// 감도 자동 보정 (MOU.Voice.Calibrate)
	//
	// ★ 왜 필요한가: DefaultVadThreshold(0.02)는 **어떤 마이크에도 맞지 않는
	//   임의의 숫자다.** USB 헤드셋은 조용할 때 RMS 가 0.001 근처지만, 메인보드
	//   3.5mm 아날로그 입력은 전기 잡음과 마이크 부스트 때문에 0.03~0.08 이
	//   그냥 나온다. 그러면 **가만히 있어도 계속 "말하는 중"** 이 된다.
	//
	//   사람이 이 숫자를 감으로 맞추는 것은 불가능하다(9절). 그래서 조용한
	//   상태를 잠깐 측정해 그 위로 기준을 올리는 방식으로 자동화한다.
	// -----------------------------------------------------------------------

	/** 보정 측정 시간(초). 너무 짧으면 순간적인 잡음을 놓친다. */
	inline constexpr float DefaultCalibrationSeconds = 2.0f;

	/**
	 * 측정된 소음 최대치에 곱해 기준을 정한다.
	 *
	 * 1.0 이면 잡음 봉우리에 딱 붙어서 조금만 시끄러워도 오작동한다.
	 * 너무 크면 작게 말할 때 안 잡힌다. 2.0 은 "잡음보다 두 배는 커야 말로 친다".
	 */
	inline constexpr float CalibrationMargin = 2.0f;

	/** 보정 결과의 하한. 이보다 낮으면 어떤 잡음에도 반응하게 된다. */
	inline constexpr float MinVadThreshold = 0.005f;

	/**
	 * 보정 결과가 이보다 높으면 **하드웨어 설정을 의심해야 한다.**
	 *
	 * 잡음 바닥이 이 정도면 기준을 아무리 올려도 작은 목소리를 못 잡는다.
	 * 소프트웨어로 덮을 문제가 아니라 마이크 부스트를 꺼야 하는 상황이다.
	 */
	inline constexpr float NoisyMicWarnThreshold = 0.05f;

	/**
	 * 재생 링버퍼 용량(프레임 수).
	 *
	 * 게임 스레드가 push 하고 오디오 렌더 스레드가 pop 한다. 두 스레드의 주기가
	 * 다르므로 여유가 필요하다. 너무 작으면 push 가 잘리고(=끊김), 너무 크면
	 * 지연이 쌓인다. 8프레임 = 160ms 는 루프백 확인에 충분한 여유다.
	 */
	inline constexpr int32 PlaybackBufferFrames = 8;

	/**
	 * 재생 버퍼가 이만큼 쌓이면 오래된 것부터 버린다. **루프백 전용이다.**
	 *
	 * 루프백에서 마이크 입력이 재생 소비보다 빠르면 지연이 무한히 누적된다.
	 * "지금 말한 것이 3초 뒤에 들리는" 상태를 막는 안전장치다.
	 *
	 * ★ 네트워크로 받은 소리에는 쓰지 않는다. 거기는 지터버퍼(V4)가 링버퍼를
	 *   목표 깊이까지만 채우므로 애초에 넘칠 수가 없다 - 넘친 뒤에 버리는 것보다
	 *   처음부터 안 넣는 편이 낫다.
	 */
	inline constexpr int32 PlaybackDropThresholdFrames = 6;

	// -----------------------------------------------------------------------
	// 발화 모드별 거리 — ★ 단일 진실 공급원 (single source of truth)
	//
	// VOICE_INTEGRATION.md 13절 표의 숫자를 여기 한 곳에만 둔다.
	//
	// **이 값을 읽는 곳이 앞으로 세 군데가 된다:**
	//   1. 디버그 시각화 (MOU.Voice.ShowRadius)          ← 지금
	//   2. 서버 근접 라우팅: 이 반경 * 1.2 안의 사람에게만 전송  (V3)
	//   3. NPC 소음: UAISense_Hearing::ReportNoiseEvent 의 MaxRange (V8)
	//
	// ★ 셋이 반드시 같은 함수를 불러야 한다.
	//   각자 숫자를 따로 들고 있으면 "화면에 보이는 원"과 "NPC 가 실제로 듣는 거리"가
	//   조용히 어긋난다. 그러면 디버그 표시가 거짓말을 하게 되고, 밸런싱이 불가능해진다.
	//   숫자를 바꿀 일이 있으면 **반드시 여기만 고친다.**
	// -----------------------------------------------------------------------

	/**
	 * 사람이 들을 수 있는 총 거리(cm).
	 *
	 * 이것은 감쇠 에셋의 `Radius + FalloffDistance` **합계**다.
	 * 에셋에 이 숫자를 그대로 넣으면 사거리가 두 배가 된다 - 7-2절 참고.
	 */
	inline constexpr float GetHearRadius(EVoiceMode Mode)
	{
		switch (Mode)
		{
		case EVoiceMode::Whisper: return 500.f;   //  5m
		case EVoiceMode::Shout:   return 3000.f;  // 30m
		default:                  return 1500.f;  // 15m (보통)
		}
	}

	/**
	 * NPC 가 들을 수 있는 거리(cm). `ReportNoiseEvent` 의 MaxRange 로 그대로 들어간다.
	 *
	 * ★ 사람이 듣는 거리보다 **일부러 넓다.**
	 *   "나한텐 안 들렸는데 NPC 는 들었다" 가 있어야 긴장이 생긴다.
	 *   같게 만들면 안전 거리를 학습하기가 너무 쉬워진다(13절).
	 */
	inline constexpr float GetNoiseRange(EVoiceMode Mode)
	{
		switch (Mode)
		{
		case EVoiceMode::Whisper: return 700.f;
		case EVoiceMode::Shout:   return 4000.f;
		default:                  return 1800.f;
		}
	}

	/**
	 * 소음 이벤트의 Loudness 배율. 측정된 RMS 에 곱해서 쓴다.
	 *
	 * ★★ 주의: 이 값은 "크기" 가 아니라 **반경 배율로도 동작한다.** ★★
	 *
	 * `UAISense_Hearing::Update` 는 거리 비교를 이렇게 한다(엔진 소스):
	 *
	 *     DistSq > HearingRangeSq * Square(Loudness)          -> 안 들림
	 *     DistSq > Square(Event.MaxRange * Loudness)          -> 안 들림
	 *
	 * 즉 **NPC 가 실제로 듣는 거리 = GetNoiseRange() x GetLoudnessScale()** 이다.
	 * 위의 GetNoiseRange 표를 그대로 믿으면 안 된다:
	 *
	 *     속삭임  700 x 0.35 =  245 cm   (표의 700 이 아니다)
	 *     보통   1800 x 1.00 = 1800 cm
	 *     외침   4000 x 1.60 = 6400 cm   (표의 4000 이 아니다)
	 *
	 * ★ V8 의 결정: **소음 발행에는 이 값을 쓰지 않는다.**
	 *   Loudness 는 NoiseEventLoudness(1.0)로 고정하고 거리는 GetNoiseRange 만으로
	 *   표현한다. 이유는 그 상수의 주석에 적어두었다.
	 *   이 함수는 이제 MOU.Voice.FakeNoise 의 기본 인자에서만 쓴다.
	 */
	inline constexpr float GetLoudnessScale(EVoiceMode Mode)
	{
		switch (Mode)
		{
		case EVoiceMode::Whisper: return 0.35f;
		case EVoiceMode::Shout:   return 1.6f;
		default:                  return 1.0f;
		}
	}

	/** 로그/UI 표시용 이름. */
	inline const TCHAR* GetVoiceModeName(EVoiceMode Mode)
	{
		switch (Mode)
		{
		case EVoiceMode::Whisper: return TEXT("속삭임");
		case EVoiceMode::Shout:   return TEXT("외침");
		default:                  return TEXT("보통");
		}
	}

	inline const TCHAR* GetVoiceRouteName(EVoiceRoute Route)
	{
		return Route == EVoiceRoute::Radio ? TEXT("무전") : TEXT("근접");
	}

	// -----------------------------------------------------------------------
	// 감쇠 분해 — ★ 하나의 거리를 두 숫자로 나눠 넣어야 한다 (7-2절)
	//
	//         │←─ Radius ─→│←──── Falloff ────→│
	//    발화자 ●━━━━━━━━━━━━┿━━━━━━━━━━━━━━━━━━━┫  그 너머 = 무음
	//         │   음량 100%  │  100% -> 0% 감소   │
	//         │←──────── GetHearRadius() ───────→│
	//
	// 언리얼 감쇠는 "100% 음량 구간(Radius)" 과 "감쇠 구간(FalloffDistance)" 을
	// 따로 받는다. **총 가청 거리는 그 합이다.**
	// GetHearRadius() 값을 Radius 에 그대로 넣으면 사거리가 두 배가 된다.
	// -----------------------------------------------------------------------

	/** 감쇠가 시작되기 전, 음량 100% 를 유지하는 거리(cm). */
	inline constexpr float GetAttenuationRadius(EVoiceMode Mode)
	{
		switch (Mode)
		{
		case EVoiceMode::Whisper: return 150.f;
		case EVoiceMode::Shout:   return 500.f;
		default:                  return 300.f;
		}
	}

	/** Radius 바깥으로 음량이 100% -> 0% 로 줄어드는 구간의 길이(cm). */
	inline constexpr float GetAttenuationFalloff(EVoiceMode Mode)
	{
		switch (Mode)
		{
		case EVoiceMode::Whisper: return 350.f;
		case EVoiceMode::Shout:   return 2500.f;
		default:                  return 1200.f;
		}
	}

	// ★★ 분해한 두 값의 합이 총 가청 거리와 어긋나지 않도록 **컴파일 타임에** 못박는다.
	//
	//   이게 어긋나면 증상이 고약하다: 서버는 GetHearRadius 기준으로 전송을 끊는데
	//   클라 감쇠는 다른 거리에서 0 이 되므로, **경계에서 목소리가 뚝 끊기거나**
	//   (감쇠가 더 길 때) **들려야 할 거리인데 소리가 없다**(감쇠가 더 짧을 때).
	//   둘 다 "가끔 이상하다" 로만 보여서 원인을 찾기 매우 어렵다.
	//
	//   숫자를 고칠 일이 생기면 세 함수를 같이 고쳐야 하고, 안 그러면 여기서 빌드가 깨진다.
	static_assert(GetAttenuationRadius(EVoiceMode::Whisper) + GetAttenuationFalloff(EVoiceMode::Whisper)
		== GetHearRadius(EVoiceMode::Whisper), "속삭임: Radius + Falloff 가 총 가청 거리와 다르다");
	static_assert(GetAttenuationRadius(EVoiceMode::Normal) + GetAttenuationFalloff(EVoiceMode::Normal)
		== GetHearRadius(EVoiceMode::Normal), "보통: Radius + Falloff 가 총 가청 거리와 다르다");
	static_assert(GetAttenuationRadius(EVoiceMode::Shout) + GetAttenuationFalloff(EVoiceMode::Shout)
		== GetHearRadius(EVoiceMode::Shout), "외침: Radius + Falloff 가 총 가청 거리와 다르다");

	// -----------------------------------------------------------------------
	// 음량에 따른 반경 조절 — ★ 아날로그 (연속)
	//
	// [무엇을 하는가]
	//   위의 GetHearRadius / GetNoiseRange 는 발화 모드가 정하는 **상한**이다.
	//   여기서부터는 **실제로 얼마나 크게 말했는지**에 따라 그 상한 아래로
	//   반경이 연속적으로 줄어든다. 조용히 말하면 원이 작아지고, 크게 말하면
	//   모드 상한까지 커진다.
	//
	// [★ 왜 아날로그(연속)인가 - 디지털(단계)을 버린 이유]
	//
	//   RMS 는 한 문장 안에서도 크게 출렁인다(음절 사이 공백, 파열음).
	//   임계값을 두고 "넘으면 큰 원" 으로 만들면 **같은 톤으로 말하는 동안에도
	//   원이 초당 몇 번씩 왕복한다.** 플레이어는 그것을 규칙이 아니라 버그로
	//   읽는다 - 규칙을 배울 수가 없기 때문이다.
	//
	//   연속 함수는 단조증가라서 숫자를 몰라도 **"조용히 말하면 안전하다" 가
	//   예외 없이 성립한다.** 이 게임이 요구하는 학습 가능성은 그쪽에서 나온다.
	//   그리고 단계 축은 이미 발화 모드(속삭임/보통/외침)가 담당하고 있으므로,
	//   여기에 단계를 또 얹으면 조합이 9개로 늘어 밸런싱만 어려워진다.
	//
	// [★★ 어디에 적용하고 어디에 적용하지 않는가 - 이 구분이 핵심이다]
	//
	//   적용한다:        · 클라 감쇠 (실제로 들리는 거리)
	//                    · NPC 소음 MaxRange
	//                    · 디버그 링 (MOU.Voice.ShowRadius)
	//
	//   적용하지 않는다: · **서버 근접 라우팅 컷 거리** (ProximityRoutingMargin 쪽)
	//
	//   라우팅 컷을 음량에 따라 움직이면 경계 근처 청취자가 수신자 목록에
	//   들락날락해서 **프레임이 끊겼다 붙었다 한다.** ProximityRoutingMargin(1.2)
	//   과 FalloffMode=Silent 가 애초에 막으려고 있는 그 지직거림이다.
	//
	//   그래서 **라우팅은 모드 상한으로 넉넉히 보내고, 조용한 소리가 멀리
	//   안 들리게 만드는 일은 클라 감쇠에 맡긴다.** 대역폭 상한은 어차피
	//   모드가 이미 정하고 있으므로 잃는 것이 없다.
	//
	// [★★ 정규화는 반드시 클라이언트에서 한다 - 서버는 할 수 없다]
	//
	//   서버는 **각 플레이어의 마이크 보정값(VadThreshold)을 모른다.** 그래서
	//   원본 RMS 를 받아서는 그것이 "크게 말한 것" 인지 "마이크가 센 것" 인지
	//   구분할 수 없다. 그대로 쓰면 **부스트를 켠 마이크가 공짜로 큰 원을
	//   얻는다** - 숨는 것이 핵심인 게임에서 이건 밸런스 붕괴다.
	//
	//   그래서 클라가 NormalizeLoudness() 로 0~1 강도를 만들어 보내고,
	//   서버와 다른 클라는 GetRadiusScaleFromNormalized() 만 쓴다.
	//   FVoiceFrame::Loudness 에 실려 오는 값이 **원본 RMS 가 아니라 이
	//   정규화된 강도**인 이유다(그 필드 주석 참고).
	// -----------------------------------------------------------------------

	// ★ 아래 세 개는 **런타임에 바뀔 수 있는 튜닝 값**이다(MOU.Voice.LoudnessCurve).
	//   constexpr 이 아닌 이유는 마이크마다 맞춰야 하는 값이라서다 - 감도
	//   보정(MOU.Voice.Calibrate)과 같은 성격이다.
	//
	//   ★ PIE 처럼 한 프로세스 안에 서버와 클라가 같이 있을 때만 자동으로
	//     일치한다. 데디케이티드 서버로 가면 이 값이 양쪽에서 갈라질 수 있고,
	//     그러면 "화면에 보이는 원" 과 "NPC 가 듣는 거리" 가 어긋난다.
	//     그때는 설정(UDeveloperSettings)으로 빼서 서버 값을 권위로 삼아야 한다.

	/**
	 * 정규화 상한. 음량이 이 값에 닿으면 반경이 모드 상한(배율 1.0)에 도달한다.
	 *
	 * VadThreshold 에서 이 값까지가 곧 **조절 구간**이다.
	 * 너무 높으면 어지간히 소리쳐도 원이 안 커지고, 너무 낮으면 평상시 말소리가
	 * 이미 상한에 붙어서 조절이 아예 안 된다.
	 */
	inline float LoudnessCeiling = 0.25f;

	/**
	 * 가장 조용하게 말했을 때의 반경 배율(하한).
	 *
	 * ★ 0 으로 두지 않는다. VAD 를 통과했다는 것은 **소리를 냈다는 뜻**이고,
	 *   배율이 0 이면 말했는데 아무에게도 안 들리는 구간이 생겨 "마이크가
	 *   고장났나" 로 보인다. 들리기는 해야 하고, 아주 가까이서만 들려야 한다.
	 */
	inline float MinRadiusScale = 0.4f;

	/**
	 * 조절 곡선의 지수. 1.0 이면 직선, 클수록 조용한 구간이 넓게 펴진다.
	 *
	 * 1.5 인 이유: 이 게임에서 플레이어가 실제로 하는 판단은 "얼마나 크게
	 * 말할까" 가 아니라 **"얼마나 조용해야 안전한가"** 다. 그 구간의 해상도를
	 * 넓게 주는 편이 맞다.
	 */
	inline float RadiusCurveExponent = 1.5f;

	/**
	 * 엔벨로프 상승 시정수(초). **거의 즉시**에 가깝게 짧다.
	 *
	 * ★ 왜 이렇게 짧은가: NPC 소음은 집계 창(NoiseWindowSec 0.3초)의 **첫
	 *   프레임에 한 번** 쏘고 나머지를 억제한다(7-5절). 상승이 느리면 그
	 *   첫 프레임에서 엔벨로프가 아직 절반밖에 안 올라와 있어서, **소리치며
	 *   시작한 발화가 작은 반경으로 보고된다.** 특히 0.3초보다 짧은 발화
	 *   ("헉!")는 그 한 번이 전부라 통째로 과소평가된다.
	 *
	 *   20ms 프레임에서 계수가 0.87 이라 사실상 한 프레임에 따라잡는다.
	 *   0 이 아닌 이유는 단일 프레임 튐(파열음 하나)을 걸러내기 위해서다.
	 *
	 * 떨림을 막는 일은 상승이 아니라 **하강**이 한다(아래).
	 */
	inline constexpr float LoudnessAttackSeconds = 0.01f;

	/**
	 * 엔벨로프 하강 시정수(초). ★ 상승보다 훨씬 길다.
	 *
	 * 이게 짧으면 음절 사이 공백마다 엔벨로프가 바닥까지 떨어져 **원이
	 * 펄럭인다** - 아날로그로 만든 이유가 통째로 사라진다. 반대로 말이 끝나고
	 * 원이 줄어드는 데 0.25초가 걸리는 것은 눈에 거슬리지 않는다.
	 */
	inline constexpr float LoudnessReleaseSeconds = 0.25f;

	/**
	 * 엔벨로프 한 스텝. 상승은 빠르게, 하강은 느리게 따라간다.
	 *
	 * @param Envelope       직전 엔벨로프 값(호출자가 들고 있는 상태)
	 * @param Rms            이번 프레임의 순간 RMS
	 * @param DeltaSeconds   ★ **FrameSeconds 를 넣는다.** 실제 경과 시간이 아니다.
	 *
	 * ★ 왜 실제 경과 시간이 아닌가: 캡처는 누적 버퍼를 20ms 씩 잘라내는 루프라
	 *   한 틱에 프레임 여러 개가 한꺼번에 나온다. 거기서 실제 경과 시간을 쓰면
	 *   **첫 프레임이 시간을 다 먹고 나머지는 0 이 되어** 엔벨로프가 계단이 된다.
	 *   프레임은 마이크가 만든 시간 순서대로 20ms 씩 흘러야 한다.
	 */
	inline float AdvanceLoudnessEnvelope(float Envelope, float Rms, float DeltaSeconds)
	{
		const float Tau = (Rms > Envelope) ? LoudnessAttackSeconds : LoudnessReleaseSeconds;

		// 표준 일차 저역통과. 시정수로 두면 프레임 간격이 바뀌어도 반응 속도가 같다.
		const float Coefficient =
			1.f - FMath::Exp(-DeltaSeconds / FMath::Max(Tau, UE_KINDA_SMALL_NUMBER));

		return Envelope + (Rms - Envelope) * FMath::Clamp(Coefficient, 0.f, 1.f);
	}

	/**
	 * VadThreshold 와 LoudnessCeiling 사이에 최소한 이만큼의 폭이 있어야 한다.
	 *
	 * ★★ 이것이 없으면 아날로그 반경이 **조용히 죽는다.**
	 *
	 *   감도 보정은 잡음 바닥 x CalibrationMargin(2.0) 을 기준으로 잡는다.
	 *   3.5mm 아날로그 입력처럼 잡음이 큰 마이크(피크 0.15)에서는 기준이
	 *   0.30 이 되는데, 이는 LoudnessCeiling(0.25)보다 **높다.**
	 *
	 *   그러면 NormalizeLoudness 의 Span 이 0 이하가 되어 KINDA_SMALL_NUMBER 로
	 *   잘리고, **모든 발화가 강도 1.0 으로 clamp 된다** - 속삭이든 소리치든
	 *   항상 최대 반경이다. 예외도 로그도 없이 기능만 사라지므로 원인을
	 *   찾기가 매우 고약하다.
	 *
	 *   0.08 은 "조절 구간이 이 정도는 돼야 속삭임과 외침이 구분된다" 는 값이다.
	 */
	inline constexpr float MinLoudnessSpan = 0.08f;

	/**
	 * 감도가 바뀌었을 때 조절 구간이 무너지지 않도록 상한을 밀어올린다.
	 *
	 * **감도를 바꾸는 모든 경로에서 불러야 한다**(자동 보정, 콘솔 수동 설정).
	 * 한 곳이라도 빠뜨리면 그 경로로 들어온 사용자만 아날로그가 안 먹는다.
	 *
	 * @return 상한을 실제로 올렸으면 true (호출자가 로그를 남길 수 있게)
	 */
	inline bool EnsureUsableLoudnessSpan(float VadThreshold)
	{
		const float Required = FMath::Max(VadThreshold, 0.f) + MinLoudnessSpan;

		if (LoudnessCeiling >= Required)
		{
			return false;
		}

		LoudnessCeiling = Required;
		return true;
	}

	/**
	 * 엔벨로프를 통과한 RMS 를 0~1 **발화 강도**로 정규화한다. **클라 전용.**
	 *
	 * ★ 조절 구간의 시작을 VadThreshold 에 맞추는 것이 요점이다.
	 *   0 에서 시작하면, 잡음 바닥이 높은 마이크(메인보드 3.5mm 아날로그는
	 *   가만히 있어도 RMS 0.03~0.08 이 나온다)에서는 **그 잡음이 이미 구간의
	 *   절반을 차지해 속삭여도 원이 중간 크기부터 시작한다.** 감도 보정이
	 *   해결한 문제와 정확히 같은 문제다(9절).
	 *
	 * @param LoudnessEnvelope  AdvanceLoudnessEnvelope 를 통과한 값. 순간 RMS 를
	 *                          그대로 넣으면 원이 떨린다.
	 * @param VadThreshold      이 클라의 VAD 기준. 조절 구간의 시작점.
	 */
	inline float NormalizeLoudness(float LoudnessEnvelope, float VadThreshold)
	{
		const float Floor = FMath::Max(VadThreshold, 0.f);
		const float Span  = FMath::Max(LoudnessCeiling - Floor, UE_KINDA_SMALL_NUMBER);

		return FMath::Clamp((LoudnessEnvelope - Floor) / Span, 0.f, 1.f);
	}

	/**
	 * 정규화된 발화 강도(0~1)를 반경 배율(MinRadiusScale ~ 1.0)로 바꾼다.
	 *
	 * ★★ 반경에 곱하는 배율이 나오는 곳은 **이 함수 하나뿐이어야 한다.**
	 *   클라 감쇠, NPC 소음, 디버그 링이 전부 여기를 거쳐야 "화면에 보이는 원"
	 *   과 "실제로 들리는 거리" 가 어긋나지 않는다. 위 단일 진실 공급원 주석과
	 *   같은 이유이고, 어긋나면 증상이 똑같이 고약하다 - 디버그 표시가
	 *   거짓말을 하기 시작하므로 밸런싱이 불가능해진다.
	 */
	inline float GetRadiusScaleFromNormalized(float Normalized01)
	{
		const float T = FMath::Clamp(Normalized01, 0.f, 1.f);

		// ★ 지수를 먼저 먹인 뒤 하한과 보간한다. 순서를 바꾸면 하한까지 곡선에
		//   눌려서 "가장 조용할 때의 배율" 이 MinRadiusScale 이 아니게 된다.
		const float Curved = FMath::Pow(T, FMath::Max(RadiusCurveExponent, UE_KINDA_SMALL_NUMBER));

		return FMath::Lerp(FMath::Clamp(MinRadiusScale, 0.f, 1.f), 1.f, Curved);
	}

	/**
	 * 반경 배율이 이만큼 벌어져야 재생 중인 사운드의 감쇠를 실제로 갱신한다.
	 *
	 * ★ 배율은 음량을 따라 매 프레임 조금씩 움직이는 연속값이다. 갱신을
	 *   막지 않으면 재생 중인 스트림마다 매 프레임 오디오 스레드로 명령이
	 *   나간다(UVoiceSynthComponent::SetProximityMode 헤더 주석). 이 정도
	 *   문턱이면 사람 귀에는 여전히 매끄럽게 이어지면서 갱신 빈도만 줄어든다.
	 */
	inline constexpr float RadiusScaleUpdateEpsilon = 0.03f;

	/**
	 * 이 발화가 실제로 사람에게 들리는 총 거리(cm).
	 *
	 * 모드 상한에 음량 배율을 곱한 것이다. 클라 감쇠와 디버그 링이 쓴다.
	 */
	inline float GetScaledHearRadius(EVoiceMode Mode, float Normalized01)
	{
		return GetHearRadius(Mode) * GetRadiusScaleFromNormalized(Normalized01);
	}

	/**
	 * 이 발화를 NPC 가 듣는 거리(cm). `ReportNoiseEvent` 의 MaxRange 로 들어간다.
	 *
	 * ★ 사람이 듣는 거리보다 넓다는 성질은 배율을 곱해도 유지된다 - 양쪽에
	 *   **같은 배율**이 곱해지기 때문이다. 한쪽만 곱하면 "나한테는 조용한데
	 *   NPC 는 똑같이 다 듣는" 상태가 되어 13절의 의도가 깨진다.
	 */
	inline float GetScaledNoiseRange(EVoiceMode Mode, float Normalized01)
	{
		return GetNoiseRange(Mode) * GetRadiusScaleFromNormalized(Normalized01);
	}

	// -----------------------------------------------------------------------
	// 서버 라우팅 (V3)
	// -----------------------------------------------------------------------

	/**
	 * 근접 라우팅 반경에 곱하는 여유값.
	 *
	 * 서버는 `GetHearRadius(Mode) * 이 값` 안에 있는 사람에게만 프레임을 보낸다.
	 * 1.0 이 아니라 1.2 인 이유: 경계에 서 있는 사람은 **서버가 전송을 끊는 지점과
	 * 클라 감쇠가 0 이 되는 지점이 정확히 겹친다.** 거기서 조금만 움직여도
	 * 소리가 붙었다 끊겼다 해서 지직거린다. 감쇠로 이미 들리지 않는 구간까지
	 * 조금 더 보내두면 그 경계가 감쇠 안쪽에 묻힌다.
	 */
	inline constexpr float ProximityRoutingMargin = 1.2f;

	/**
	 * 한 플레이어가 초당 보낼 수 있는 최대 프레임 수(서버 강제).
	 *
	 * 정상값은 50(20ms x 50 = 1초)이다. 60 은 틱 흔들림을 감안한 여유.
	 * ★ 이것은 최적화가 아니라 **서버 보호**다. 개조 클라이언트가 초당 수천 개를
	 *   보내면 서버는 그걸 그대로 N명에게 복사한다 - 한 명이 방 전체를 마비시킬 수 있다.
	 *   V3 이 클라 데이터를 서버가 처음 받는 지점이라 여기서부터 막는다.
	 */
	inline constexpr int32 MaxFramesPerSecPerPlayer = 60;

	// -----------------------------------------------------------------------
	// 지터버퍼 (V4)
	//
	// ★ 왜 필요한가: UDP 프레임은 **일정한 간격으로 도착하지 않는다.** 20ms마다
	//   보내도 받는 쪽에서는 5ms, 40ms, 3ms... 로 흔들리고 순서도 뒤집힌다.
	//   받는 즉시 재생하면 그 흔들림이 그대로 소리의 끊김이 된다.
	//
	//   그래서 **일부러 조금 늦게** 재생한다. 몇 프레임을 모아두고 그 뒤에서
	//   일정한 속도로 꺼내 쓰면, 늦게 온 프레임도 제시간에 자리를 찾는다.
	//   지연과 안정성을 맞바꾸는 것이고, 그 교환 비율이 아래 두 숫자다.
	// -----------------------------------------------------------------------

	/**
	 * 재생을 시작하기 전에 모아둘 프레임 수. 이 값이 곧 **추가 지연**이다.
	 *
	 * 3프레임 = 60ms. 낮추면 반응이 빨라지지만 작은 흔들림에도 끊기고,
	 * 높이면 안정적이지만 대화가 굼떠진다. 근접 음성은 얼굴을 보며 하는
	 * 대화라 지연에 민감하므로 낮은 쪽에 둔다.
	 */
	inline constexpr int32 TargetJitterFrames = 3;

	/**
	 * 버퍼가 담을 수 있는 최대 프레임 수(= 재정렬 가능한 범위).
	 *
	 * 이보다 앞선 프레임이 오면 재생이 뒤처진 것이므로 따라잡기(재동기화)를 한다.
	 * 무한정 쌓게 두면 **지연이 계속 누적되어** "몇 초 전 목소리" 를 듣게 된다.
	 */
	inline constexpr int32 MaxJitterFrames = 8;

	// -----------------------------------------------------------------------
	// 유실 은폐 (PLC, Packet Loss Concealment)
	//
	// ★★ [V4 에서 확인됨] **엔진 Opus 래퍼로는 진짜 PLC 를 쓸 수 없다.**
	//
	//   Opus 자체는 `opus_decode(dec, NULL, 0, ...)` 로 유실 구간을 그럴듯하게
	//   합성해주는 기능이 있다. 그런데 엔진 래퍼가 그 경로를 막아놨다:
	//
	//       // VoiceCodecOpus.cpp, FVoiceDecoderOpus::Decode()
	//       if (!InCompressedData || (CompressedDataSize < HeaderSize))
	//       { OutRawDataSize = 0; return; }   // <- null 을 넘기면 그냥 0샘플
	//
	//   IVoiceDecoder 인터페이스에 PLC 를 요청할 방법이 아예 없다.
	//   그래서 **PCM 단계에서 직접 메운다** - 직전 프레임을 감쇠시켜 반복한다.
	//   Opus 내부 PLC 보다 거칠지만, 아무것도 안 하는 것(무음)보다는 훨씬 낫다.
	//   짧은 유실에서 "뚝" 끊기는 대신 자연스럽게 잦아든다.
	// -----------------------------------------------------------------------

	/**
	 * 연속으로 이만큼까지만 은폐하고 그 뒤로는 무음을 낸다.
	 *
	 * 같은 소리를 계속 반복하면 사람 목소리가 아니라 **기계음처럼 웅웅거린다.**
	 * 3프레임(60ms)이면 짧은 유실을 덮기에 충분하고, 그보다 길게 끊겼다면
	 * 조용한 편이 덜 거슬린다.
	 */
	inline constexpr int32 MaxConcealFrames = 3;

	/**
	 * 은폐 프레임마다 곱하는 감쇠. 반복할수록 빠르게 잦아들게 한다.
	 * 1.0 이면 원래 크기로 계속 반복해서 웅웅거림이 그대로 들린다.
	 */
	inline constexpr float ConcealFadePerFrame = 0.5f;

	/**
	 * 이만큼 조용하면 그 발신자의 재생 스트림을 정리한다(초).
	 *
	 * 스트림 하나는 오디오 엔진의 **사운드 슬롯 하나를 계속 점유한다** -
	 * 무음을 재생하는 중이어도 마찬가지다. 방을 나간 사람의 스트림이 남아있으면
	 * 그 슬롯이 영영 안 돌아온다.
	 *
	 * 너무 짧으면 말 사이의 공백마다 스트림을 만들었다 부쉈다 한다. 3초는
	 * "대화가 끝났다" 로 볼 만하면서 문장 사이 호흡보다는 충분히 길다.
	 */
	inline constexpr double VoiceStreamIdleTimeoutSeconds = 3.0;

	/**
	 * 이만큼 프레임이 끊겼다가 다시 오면 **새 발화**로 보고 디코더를 리셋한다(초).
	 *
	 * 리셋하지 않으면 새 발화의 첫 프레임을 공백 이전 소리를 참고해 복원해서
	 * 말 첫머리가 짧게 지직거린다(V2 에서 같은 이유로 로컬 디코더도 리셋한다).
	 * VAD hangover(0.2초)보다 커야 한 문장 안에서 리셋되지 않는다.
	 */
	inline constexpr double VoiceUtteranceGapSeconds = 0.5;

	/**
	 * "무전 수신 중" 표시를 프레임 하나마다 몇 초씩 붙잡아 둘지(초).
	 *
	 * ★ 이게 없으면 아이콘이 발작한다. 프레임은 20ms 간격으로 오는데, 한 프레임
	 *   늦거나 빠지기만 해도 "수신 중" 이 켜졌다 꺼졌다 하기 때문이다. 사람 눈에
	 *   깜빡임으로 안 보이려면 프레임 간격보다 한참 커야 한다.
	 *
	 * VoiceUtteranceGapSeconds(0.5)보다는 **작아야 한다.** 말이 끝났는데도
	 * 수신 아이콘이 남아 있으면 "누가 아직 무전을 잡고 있다" 로 잘못 읽힌다 -
	 * 그건 숨을지 말지를 가르는 정보라 틀리면 안 된다.
	 */
	inline constexpr double RadioReceiveHoldSeconds = 0.2;

	/** 음량(0~1)을 패킷에 실을 uint8 로 양자화한다. */
	inline uint8 QuantizeLoudness(float Loudness01)
	{
		return static_cast<uint8>(FMath::Clamp(Loudness01, 0.f, 1.f) * 255.f + 0.5f);
	}

	/** 양자화된 음량을 0~1 로 되돌린다. */
	inline float DequantizeLoudness(uint8 Quantized)
	{
		return static_cast<float>(Quantized) / 255.f;
	}

	// -----------------------------------------------------------------------
	// 무전 (V6)
	// -----------------------------------------------------------------------

	/**
	 * 무전 반이중 - 마지막 프레임 이후 이만큼 지나면 채널 점유가 풀린다(초).
	 *
	 * ★ 동시에 한 명만 송신하게 만드는 이유는 "무전기다움" 보다 **대역폭 상한**이다.
	 *   반이중이 없으면 8명 전원 송신 시 8x7 = 56 스트림이 되어 280 KB/s 로 뛴다(12절).
	 *   근접에는 적용하지 않는다 - 실제로 여러 명이 동시에 말할 수 있어야 한다.
	 *
	 * 0.3초인 이유: 말 사이의 짧은 숨에 채널이 풀려 남에게 뺏기면 안 되고,
	 * 그렇다고 길면 놓은 뒤에도 한참 잠겨 있다.
	 */
	inline constexpr double RadioChannelHoldSeconds = 0.3;

	/** 무전기 스피커 기본 반경(cm). 컴포넌트에서 아이템마다 조절한다. */
	inline constexpr float DefaultSpeakerHearRadius  = 1000.f;  // 10m - 사람이 듣는 거리

	/**
	 * ★ NPC 가 듣는 거리는 사람보다 **일부러 넓다**(12m vs 10m).
	 *   "나한텐 안 들렸는데 NPC 는 들었다" 가 있어야 긴장이 생긴다. 같으면
	 *   안전 거리를 학습하기가 너무 쉬워진다(7-4절). 이 비율이 난이도의 주 손잡이다.
	 */
	inline constexpr float DefaultSpeakerNoiseRadius = 1200.f;  // 12m - NPC 가 듣는 거리

	// -----------------------------------------------------------------------
	// 소음 이벤트 (V8) — NPC 팀원에게 넘기는 계약 (7-5절)
	// -----------------------------------------------------------------------

	/**
	 * 소음 보고 집계 창(초). 한 발화자/무전기당 이 간격보다 자주 쏘지 않는다.
	 *
	 * ★ 프레임마다(20ms) 쏘면 초당 50회 x 인원수의 perception 갱신이 돌아
	 *   서버가 죽는다. 0.3초면 초당 3.3회로 떨어진다(7-5절).
	 */
	inline constexpr float NoiseWindowSec = 0.3f;

	/**
	 * 소음 이벤트에 넣는 Loudness. **1.0 고정이다.**
	 *
	 * ★ 왜 발화 모드별로 다르게 주지 않는가:
	 *   UAISense_Hearing::Update 는 Loudness 를 **반경 배율로 쓴다**(위 GetLoudnessScale
	 *   주석 참고). 모드별 값을 그대로 넣으면 NPC 가 듣는 거리가
	 *   GetNoiseRange x GetLoudnessScale 로 바뀌어 아래 두 가지가 동시에 깨진다:
	 *
	 *     · MOU.Voice.ShowRadius 의 빨간 원이 실제 판정과 어긋난다
	 *     · NPC 에디터의 `듣기 범위` 도 Loudness 만큼 축소·확대된다
	 *       (첫 번째 비교식이 리스너의 HearingRangeSq 에도 곱하기 때문)
	 *
	 *   1.0 으로 고정하면 판정이 이렇게 단순해진다:
	 *
	 *       실제로 들리는 거리 = min(NPC 의 듣기 범위, GetNoiseRange(모드))
	 *
	 *   양쪽이 각자 뚜렷한 의미를 갖는다 - NPC 는 "이 개체가 들을 수 있는 최대치",
	 *   우리는 "이 발화가 퍼지는 거리". 속삭임과 외침의 차이는 **반경으로만**
	 *   표현한다(700 vs 4000). NPC 입장에서 "가까이서만 들리는 소리 = 작은 소리" 다.
	 */
	inline constexpr float NoiseEventLoudness = 1.0f;

	/**
	 * 무전 송신 중 **육성** 소음의 반경 배율.
	 *
	 * 무전을 치는 동안에도 입으로는 소리가 난다(2절). 다만 무전기에 대고
	 * 낮게 말하므로 평소보다 좁게 퍼진다.
	 */
	inline constexpr float RadioSpeakingNoiseScale = 0.35f;

	/** 무전기 스피커에서 나는 소음의 반경 배율. SpeakerNoiseRadius 에 곱한다. */
	inline constexpr float RadioSpeakerNoiseScale = 0.8f;

	/**
	 * 소음 태그. **NPC 담당자가 이 값으로 반응을 분기한다 - 함부로 바꾸면 안 된다.**
	 *
	 * ★ 전역 FName 상수로 두지 않고 함수로 감싼 이유: FName 은 정적 초기화 순서에
	 *   의존하는데, 전역 객체로 두면 FName 테이블이 준비되기 전에 생성될 수 있다.
	 *   호출은 0.3초에 한 번뿐이라 매번 만들어도 비용이 문제되지 않는다.
	 */
	inline FName GetProximityNoiseTag()    { return FName(TEXT("Voice.Proximity")); }
	inline FName GetRadioNoiseTag()        { return FName(TEXT("Voice.Radio")); }
	inline FName GetRadioSpeakerNoiseTag() { return FName(TEXT("Voice.RadioSpeaker")); }

	/**
	 * 네트워크로 받은 enum 을 신뢰 가능한 범위로 자른다.
	 *
	 * ★ 개조 클라이언트는 enum 에 아무 값이나 넣을 수 있다. 범위 밖 값이
	 *   그대로 switch 에 들어가면 default 로 떨어져 당장은 안 죽지만,
	 *   나중에 이 값으로 배열을 인덱싱하는 코드가 생기면 그때 터진다.
	 *   **서버 경계에서 한 번 자르고 들어가는 것이 규칙이다.**
	 */
	inline EVoiceMode SanitizeMode(EVoiceMode Mode)
	{
		return (Mode < EVoiceMode::MAX) ? Mode : EVoiceMode::Normal;
	}

	inline EVoiceRoute SanitizeRoute(EVoiceRoute Route)
	{
		return (Route < EVoiceRoute::MAX) ? Route : EVoiceRoute::Proximity;
	}
}

// ---------------------------------------------------------------------------
// 패킷 (V3)
//
// VOICE_INTEGRATION.md 10절. 두 구조체가 **따로 있는 이유**가 이 설계의 핵심이다:
//
//   FVoiceFrame     클라가 서버에 보내는 것. **주장(claim)** 이다. 못 믿는다.
//   FVoiceFrameOut  서버가 클라에 보내는 것. **사실(fact)** 이다. 서버가 확정했다.
//
// 하나로 합치면 "이 필드를 믿어도 되는가" 가 코드에서 사라진다. 특히 SpeakerId 를
// 클라가 보내게 두면 **남을 사칭할 수 있다.** 그래서 SpeakerId 는 서버가 채우는
// FVoiceFrameOut 에만 있고, FVoiceFrame 에는 아예 자리가 없다.
// ---------------------------------------------------------------------------

/**
 * 클라 -> 서버. 20ms 한 조각.
 *
 * **발신자가 누구인지는 들어있지 않다.** 서버가 RPC 를 받은 컴포넌트의 소유자로
 * 알아내기 때문이다(그게 위조 불가능한 유일한 출처다).
 */
USTRUCT()
struct FVoiceFrame
{
	GENERATED_BODY()

	/** 지터버퍼 정렬과 유실 감지용. 순환(65535 -> 0)해도 무방하다. */
	UPROPERTY() uint16 Seq = 0;

	/** 클라가 **요청하는** 라우트. 최종 결정은 서버가 한다(EVoiceRoute 주석). */
	UPROPERTY() EVoiceRoute Route = EVoiceRoute::Proximity;

	/**
	 * 발화 모드. 이 값이 **서버의 라우팅 반경을 결정한다.**
	 *
	 * ★ 알려진 신뢰 문제: 개조 클라이언트가 항상 Shout 를 보내면 30m 밖까지
	 *   목소리가 전달된다. 지금은 서버가 범위 검사만 한다.
	 *   제대로 막으려면 발화 모드를 PlayerState 에 리플리케이트해서 서버가
	 *   자기가 아는 값을 쓰면 된다 - 키 입력이 이미 서버를 거치기 때문이다.
	 *   V9(옵션/키 바인딩)에서 같이 정리한다.
	 */
	UPROPERTY() EVoiceMode Mode = EVoiceMode::Normal;

	/**
	 * **정규화된 발화 강도**(0~1)를 0~255 로 양자화한 값. 원본 RMS 가 아니다.
	 *
	 * ★ 왜 원본 RMS 가 아닌가: 서버는 이 클라의 마이크 보정값을 모르므로
	 *   원본을 받아서는 "크게 말한 것" 과 "마이크가 센 것" 을 구분할 수 없다.
	 *   그대로 반경에 쓰면 부스트를 켠 마이크가 공짜로 큰 원을 얻는다.
	 *   그래서 클라가 NormalizeLoudness() 를 거친 값을 실어 보낸다
	 *   (위 "정규화는 반드시 클라이언트에서" 주석).
	 *
	 * 서버는 이 값을 GetScaledNoiseRange 에, 받는 클라는 감쇠 배율에 쓴다.
	 * 로컬 마이크 게이지는 이 값이 아니라 원본 RMS 를 쓴다
	 * (FVoiceCaptureSource::GetCurrentLoudness).
	 */
	UPROPERTY() uint8 Loudness = 0;

	/** Opus 압축 바이트. 정상값은 50~70바이트. 상한은 MOUVoice::MaxEncodedFrameBytes. */
	UPROPERTY() TArray<uint8> Opus;
};

/**
 * 서버 -> 클라. **여기 있는 값은 전부 서버가 확정한 것이다.**
 */
USTRUCT()
struct FVoiceFrameOut
{
	GENERATED_BODY()

	/**
	 * 누가 말했는가. `APlayerState::GetPlayerId()`.
	 *
	 * 받는 쪽은 이 값으로 발신자의 폰을 찾아 거기서 소리를 낸다.
	 * **서버만 채운다** - 클라가 보내는 FVoiceFrame 에는 이 필드가 없다.
	 */
	UPROPERTY() int32 SpeakerId = 0;

	UPROPERTY() uint16 Seq = 0;

	/** 서버가 우선순위까지 적용해 확정한 라우트. 재생 방식이 이 값으로 갈린다. */
	UPROPERTY() EVoiceRoute Route = EVoiceRoute::Proximity;

	/**
	 * 서버가 확정한 발화 모드.
	 *
	 * 받는 쪽이 **감쇠 반경을 이 값으로 정한다.** 서버의 라우팅 반경과 같은
	 * 값에서 나와야 "서버가 끊는 거리" 와 "클라에서 안 들리는 거리" 가 맞는다.
	 * 그래서 클라가 스스로 추측하지 않고 서버가 실어 보낸다.
	 */
	UPROPERTY() EVoiceMode Mode = EVoiceMode::Normal;

	/**
	 * 서버가 확정해 넘기는 **정규화된 발화 강도**(FVoiceFrame::Loudness 와 같은 값).
	 *
	 * 받는 클라는 이 값으로 감쇠 반경 배율을 정한다 - 조용히 말한 목소리는
	 * 가까이서만 들려야 하고, 그 판정이 서버의 NPC 소음 반경과 **같은 값에서
	 * 나와야** 화면의 원과 실제로 들리는 거리가 맞는다.
	 * 말하는 사람 표시(아이콘/게이지)에도 그대로 쓴다.
	 */
	UPROPERTY() uint8 Loudness = 0;

	/**
	 * ★ 무전 라우트일 때만 채워진다 - **소리가 나야 할 무전기 액터**다.
	 *
	 *   무전을 받으면 소리는 발신자가 아니라 **듣는 쪽 근처의 무전기**에서 난다.
	 *   그게 이 설계의 핵심 재미다(7-4절): 무전을 수신하면 내 무전기가 소리를 내고,
	 *   그 소리를 주변 사람도 NPC 도 듣는다. 숨어 있는데 팀원이 무전을 치면 들킨다.
	 *
	 *   그래서 위치의 출처가 근접과 완전히 다르다:
	 *     근접 → SpeakerId 로 **발신자 폰**을 찾아 거기서 재생
	 *     무전 → 이 액터(**듣는 쪽 근처의 무전기**) 위치에서 재생
	 *
	 *   바닥에 떨어진 무전기도 그냥 액터이므로 코드가 두 경우를 구분할 필요가 없다.
	 *   시체 옆에 켜진 채 남은 무전기가 계속 소리를 내는 연출이 공짜로 나온다.
	 */
	UPROPERTY() TObjectPtr<AActor> RadioActor = nullptr;

	UPROPERTY() TArray<uint8> Opus;
};
