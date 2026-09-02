// MOU 음성 - 마이크 캡처 소스.
//
// [이 파일이 시스템 어디에 있나]
//
//     마이크
//       │ IVoiceCapture (엔진 Voice 모듈)
//       ▼
//   ★ FVoiceCaptureSource  ← 이 파일. 20ms 프레이밍 + RMS + VAD
//       ▼ (직접 호출)
//     UVoiceSubsystem (게임 스레드 티커가 Poll 한다)
//       ▼ 링버퍼 (SPSC)
//     UVoiceSynthComponent (오디오 렌더 스레드)
//
// [★★ 왜 워커 스레드가 아닌가 - 설계가 바뀐 이유]
//
//   처음에는 기존 채팅(FServerClientRunnable)처럼 워커 스레드로 만들었다가 되돌렸다.
//   엔진 구현을 열어보니 그렇게 하면 안 되는 구조였다:
//
//     class FVoiceCaptureWindows : public IVoiceCapture, public FTSTickerObjectBase
//                                                        ^^^^^^^^^^^^^^^^^^^^^^^^^
//   엔진의 캡처 객체는 **게임 스레드 티커**로 자기 내부 버퍼를 채운다.
//   그리고 그 버퍼를 지키는 **뮤텍스가 없다**.
//   워커 스레드에서 GetVoiceData() 를 부르면 게임 스레드의 Tick() 이 쓰는 중인
//   버퍼를 동시에 읽게 된다 - 데이터 레이스다.
//
//   그래서 캡처 폴링은 게임 스레드에서 한다. 비용도 문제되지 않는다:
//   GetVoiceData 는 memcpy 이고, 320샘플 RMS 는 산술 320번이다.
//
//   **진짜 스레드 경계는 여기가 아니라 재생 쪽**(게임 스레드 -> 오디오 렌더 스레드)
//   이고, 그건 VoiceSynthComponent 의 락 프리 링버퍼가 담당한다.
//
//   [V2 결과] Opus 인코딩은 게임 스레드에 그대로 두었다. 24kbps / 320샘플
//   인코딩은 수십 마이크로초라 문제되지 않는다. 다만 인코딩은 이 클래스가
//   아니라 UVoiceSubsystem 이 한다(아래 FMOUVoiceFrame 주석 참고).
//   프로파일링에서 걸리면 그때 **인코딩만** 워커로 뺀다 - 캡처 폴링은 여전히
//   게임 스레드에 남아야 한다.
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 7-1절 (캡처), 11절 (스레드 경계)

#pragma once

#include "CoreMinimal.h"
#include "Voice/VoiceTypes.h"

class IVoiceCapture;

/**
 * 캡처된 20ms 조각 하나. **항상 원본 PCM 이다.**
 *
 * V2 를 하며 초안(“Opus 가 들어오면 PCM 대신 압축 바이트가 된다”)을 접었다.
 * 압축 결과를 이 구조체에 넣지 않고 **UVoiceSubsystem 이 따로 들고 있는** 이유:
 *
 *   - 캡처의 책임은 "마이크를 20ms 로 자르는 것" 하나다. 인코딩까지 넣으면
 *     이 클래스가 코덱 수명까지 관리하게 된다.
 *   - V3 에서 압축 바이트는 **RPC 인자로 바로 나간다.** 구조체에 담아둘 이유가 없다.
 *   - 코덱을 끄고 원본을 듣는 비교 경로(MOU.Voice.Codec)가 그대로 살아있다.
 *
 * V3 에서 네트워크로 나갈 때 Seq/Route 는 압축 바이트와 함께 RPC 인자로 붙는다.
 * 이 구조체는 캡처 결과 그대로 남는다.
 */
struct FMOUVoiceFrame
{
	/** 16kHz 모노 PCM16. 항상 MOUVoice::SamplesPerFrame 개다. */
	TArray<int16> Samples;

	/**
	 * 이 프레임의 RMS 를 0~1 로 정규화한 값. **순간값이다.**
	 *
	 * VAD 판정과 마이크 게이지가 쓴다 - 둘 다 반응이 빨라야 하는 쪽이다.
	 * **반경 계산에는 쓰지 않는다**(아래 LoudnessEnvelope 참고).
	 */
	float Loudness = 0.f;

	/**
	 * 스무딩을 통과한 음량. **반경 계산에는 이 값을 쓴다.**
	 *
	 * ★ 순간 RMS 를 반경에 그대로 넣으면 음절 사이 공백마다 원이 펄럭인다 -
	 *   아날로그로 만든 이유가 사라진다(VoiceTypes.h 의 LoudnessReleaseSeconds
	 *   주석). 게이지와 VAD 는 위의 Loudness 를, 반경은 이쪽을 쓴다.
	 */
	float LoudnessEnvelope = 0.f;

	/** VAD 판정 결과. false 면 무음 구간이다. */
	bool bIsSpeaking = false;
};

/**
 * 마이크를 열고 20ms 프레임으로 잘라주는 객체.
 *
 * **모든 멤버 함수는 게임 스레드에서만 호출한다.** (위 주석 참고)
 * 그래서 원자적 타입이나 큐가 필요 없다 - 평범한 멤버로 충분하다.
 */
class FVoiceCaptureSource
{
public:
	FVoiceCaptureSource();
	~FVoiceCaptureSource();

	/**
	 * 마이크를 연다.
	 *
	 * [중요] 이 함수를 부르기 전에 **Voice 모듈이 로드돼 있어야 한다.**
	 * 호출자(UVoiceSubsystem::Initialize)가 게임 스레드에서 보장한다.
	 *
	 * @return 마이크가 실제로 열렸으면 true. false 여도 게임은 정상 진행돼야 한다.
	 */
	bool Start();

	/** 마이크를 닫는다. 두 번 불러도 안전하다. */
	void Shutdown();

	/**
	 * 엔진에서 받은 데이터를 20ms 프레임으로 잘라 OutFrames 에 **덧붙인다.**
	 * 매 게임 스레드 틱마다 부른다.
	 */
	void Poll(TArray<FMOUVoiceFrame>& OutFrames);

	/** 마이크가 열려 있는지. */
	bool IsReady() const { return bReady; }

	/** VAD 임계값(마이크 감도). */
	void  SetVadThreshold(float InThreshold);
	float GetVadThreshold() const { return VadThreshold; }

	/** 마지막으로 계산한 순간 음량. UI 게이지용. */
	float GetCurrentLoudness() const { return CurrentLoudness; }

	/**
	 * 스무딩을 통과한 마지막 음량. **반경 계산과 디버그 링이 쓴다.**
	 *
	 * 정규화까지 거친 값이 필요하면 MOUVoice::NormalizeLoudness 에 이 값과
	 * GetVadThreshold() 를 넣는다 - 그 조합이 네트워크로 나가는 값이다.
	 */
	float GetLoudnessEnvelope() const { return LoudnessEnvelope; }

private:
	/** 누적 버퍼에서 20ms 씩 잘라낸다. */
	void DrainPendingSamples(TArray<FMOUVoiceFrame>& OutFrames);

	/** 샘플 배열의 RMS 를 0~1 로 정규화해 돌려준다. */
	static float ComputeRms(const int16* Samples, int32 NumSamples);

	TSharedPtr<IVoiceCapture> VoiceCapture;

	/** 엔진이 임의 길이로 주는 바이트를 20ms 경계까지 모아두는 버퍼. */
	TArray<uint8> PendingBytes;

	/** GetVoiceData 로 받아올 임시 버퍼. 매번 할당하지 않으려고 멤버로 둔다. */
	TArray<uint8> ReadBuffer;

	bool  bReady          = false;
	float VadThreshold    = MOUVoice::DefaultVadThreshold;
	float CurrentLoudness = 0.f;

	/**
	 * 음량 엔벨로프 상태. 프레임마다 한 스텝씩 진행한다.
	 *
	 * 마이크를 열고 닫을 때 0 으로 되돌린다 - 안 그러면 마이크를 다시 열었을 때
	 * **직전 세션의 마지막 음량에서 시작해 첫마디의 원이 엉뚱한 크기로 나온다.**
	 */
	float LoudnessEnvelope = 0.f;

	/** VAD hangover 상태. */
	double SilenceStartedAt = 0.0;
	bool   bSpeaking        = false;
};
