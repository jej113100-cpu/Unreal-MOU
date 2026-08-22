// MOU 음성 - Opus 인코더/디코더 래퍼.
//
// [이 파일이 시스템 어디에 있나]
//
//     FVoiceCaptureSource  (PCM16, 320샘플)
//       ▼
//   ★ FMOUVoiceEncoder  ← 이 파일. PCM -> 압축 바이트 (~50~70바이트)
//       ▼                  V3 에서는 여기 결과가 서버로 나간다
//     (V2 는 네트워크 없이 바로 아래로)
//       ▼
//   ★ FMOUVoiceDecoder  ← 이 파일. 압축 바이트 -> PCM
//       ▼
//     UVoiceSynthComponent
//
// [왜 인코더와 디코더를 한 클래스로 묶지 않았나]
//
//   V2 만 보면 둘은 항상 짝으로 쓰이니 "FVoiceCodec" 하나로 충분해 보인다.
//   하지만 V3 에서 개수가 갈라진다:
//
//     인코더: 내 마이크당 하나.        보내는 쪽에 1개.
//     디코더: **스트림(발신자 x 라우트)마다 하나.**  받는 쪽에 N개.
//
//   Opus 디코더는 **상태를 가진다**(직전 프레임을 참고해 예측한다). 그래서
//   여러 사람의 목소리를 디코더 하나로 돌려 쓰면 서로의 상태를 오염시켜
//   지직거리는 소리가 난다. 사람마다 따로 있어야 한다.
//
//   지금 묶어두면 V3 에서 다시 쪼개야 한다. 처음부터 나눠 두면 V3 은
//   "디코더를 VoicePlaybackComponent 로 **옮기는**" 일이 된다.
//
// [엔진 API 를 그대로 안 쓰고 감싸는 이유]
//   엔진 IVoiceEncoder::Encode 는 uint8* 와 in/out 크기 인자를 쓰는 C 스타일이라
//   호출부마다 버퍼 크기 계산과 캐스팅이 반복된다. 그 계산을 틀리면 조용히
//   실패하는 종류의 API 다(VoiceTypes.h 의 DecodeScratchSamples 주석 참고).
//   **한 곳에서만 틀릴 수 있도록** 여기 가둔다.
//
// [스레드]
//   게임 스레드 전용이다. 오디오 렌더 스레드에서 부르면 안 된다
//   (메모리 할당이 일어난다 - VoiceSynthComponent.h 상단 주석 참고).
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 4절(엔진 Voice 모듈을 쓰는 이유), 14절 V2

#pragma once

#include "CoreMinimal.h"
#include "Voice/VoiceTypes.h"

class IVoiceEncoder;
class IVoiceDecoder;

/**
 * PCM16 을 Opus 로 압축한다. **보내는 쪽에 하나만 있으면 된다.**
 *
 * 압축 결과는 순수 Opus 비트스트림이 아니라 **엔진이 정의한 컨테이너**다:
 *
 *     [0]        프레임 개수 (uint8)
 *     [1]        Generation - 패킷 순번. 디코더가 유실을 감지하는 데 쓴다 (uint8)
 *     [2..]      프레임마다 압축 끝 오프셋 (uint16 x 프레임개수)
 *     [그 뒤]    실제 Opus 데이터
 *
 * 그래서 **다른 Opus 구현으로는 못 푼다.** 반대쪽도 반드시 엔진 디코더여야 한다.
 * 우리는 양쪽 다 UE 라 문제가 되지 않지만, 나중에 외부 클라이언트를 붙일
 * 일이 생기면 이 사실이 발목을 잡는다는 것은 알고 있어야 한다.
 */
class FMOUVoiceEncoder
{
public:
	FMOUVoiceEncoder();
	~FMOUVoiceEncoder();

	/**
	 * 인코더를 만든다. Voice 모듈이 로드된 뒤에 부를 것.
	 * @return 실패하면 false. 실패해도 게임은 진행돼야 한다(코덱 없이 원본 PCM 을 쓴다).
	 */
	bool Initialize();

	/** 두 번 불러도 안전하다. */
	void Shutdown();

	bool IsReady() const { return Encoder.IsValid(); }

	/**
	 * 20ms 프레임 하나를 압축한다.
	 *
	 * @param Samples      PCM16. **MOUVoice::SamplesPerFrame(320) 개여야 한다.**
	 *                     엔진 인코더가 320샘플 단위로만 동작하기 때문이다
	 *                     (VoiceTypes.h 의 FrameMs 주석 참고). 모자라면 통째로 버려진다.
	 * @param OutEncoded   압축 결과. 호출자의 버퍼를 재사용하도록 참조로 받는다.
	 * @return 압축에 성공했으면 true.
	 */
	bool Encode(const int16* Samples, int32 NumSamples, TArray<uint8>& OutEncoded);

	/** 지금까지 인코딩한 프레임 수 / 압축 후 총 바이트. 통계용. */
	int32 GetFrameCount()   const { return FrameCount; }
	int64 GetTotalBytes()   const { return TotalEncodedBytes; }
	int32 GetMaxFrameBytes() const { return MaxFrameBytes; }

	/** 프레임당 평균 압축 크기(바이트). 12절의 대역폭 계산을 실측으로 검증하는 값이다. */
	float GetAverageFrameBytes() const
	{
		return FrameCount > 0 ? static_cast<float>(TotalEncodedBytes) / FrameCount : 0.f;
	}

private:
	TSharedPtr<IVoiceEncoder> Encoder;

	/** 매 프레임 할당하지 않으려고 멤버로 유지한다. */
	TArray<uint8> Scratch;

	int32 FrameCount        = 0;
	int64 TotalEncodedBytes = 0;
	int32 MaxFrameBytes     = 0;

	/** RPC 상한 초과 경고를 한 번만 남기기 위한 플래그. 매 프레임 로그하면 로그가 잠긴다. */
	bool bWarnedOversize = false;
};

/**
 * Opus 를 PCM16 으로 되돌린다.
 *
 * ★ **스트림 하나에 디코더 하나.** 위 헤더 주석의 이유대로 여러 발신자가
 *   공유하면 안 된다. V3 에서 UVoicePlaybackComponent 가 발신자마다
 *   이 객체를 하나씩 들고 있게 된다.
 */
class FMOUVoiceDecoder
{
public:
	FMOUVoiceDecoder();
	~FMOUVoiceDecoder();

	bool Initialize();
	void Shutdown();

	bool IsReady() const { return Decoder.IsValid(); }

	/**
	 * 압축 프레임 하나를 푼다.
	 *
	 * @param OutSamples  복원된 PCM16. 정상이면 320개가 나온다.
	 * @return 샘플이 하나라도 나왔으면 true.
	 */
	bool Decode(const uint8* Encoded, int32 NumBytes, TArray<int16>& OutSamples);

	/**
	 * 스트림이 끊겼을 때 부른다(발화가 끝났거나, V3 에서 패킷이 오래 안 왔을 때).
	 *
	 * 디코더의 예측 상태를 초기화한다. 이걸 안 하면 새 발화의 첫 프레임이
	 * **끊기기 전 소리를 참고해서** 복원되어 짧게 지직거린다.
	 */
	void Reset();

	int32 GetFrameCount() const { return FrameCount; }

	/** 디코딩이 0샘플을 돌려준 횟수. 0 이 아니면 버퍼 크기 문제를 의심한다. */
	int32 GetFailureCount() const { return FailureCount; }

private:
	TSharedPtr<IVoiceDecoder> Decoder;

	int32 FrameCount   = 0;
	int32 FailureCount = 0;

	bool bWarnedFailure = false;
};
