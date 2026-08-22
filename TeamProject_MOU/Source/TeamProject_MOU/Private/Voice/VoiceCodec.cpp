// MOU 음성 - Opus 인코더/디코더 래퍼 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 4절, 14절 V2
//
// [스레드] 전부 게임 스레드다. 헤더 상단 주석 참고.

#include "Voice/VoiceCodec.h"

#include "Interfaces/VoiceCodec.h"
#include "Net/VoiceConfig.h"
#include "VoiceModule.h"

// ---------------------------------------------------------------------------
// 인코더
// ---------------------------------------------------------------------------

FMOUVoiceEncoder::FMOUVoiceEncoder()
{
	Scratch.SetNumUninitialized(MOUVoice::EncodeScratchBytes);
}

FMOUVoiceEncoder::~FMOUVoiceEncoder()
{
	Shutdown();
}

bool FMOUVoiceEncoder::Initialize()
{
	if (Encoder.IsValid())
	{
		return true;
	}

	if (!FVoiceModule::IsAvailable() || !FVoiceModule::Get().IsVoiceEnabled())
	{
		// 캡처와 같은 조건에서 실패한다. 여기까지 왔다는 것은 호출자가 모듈 로드를
		// 건너뛴 것이므로, 캡처 쪽과 같은 문구로 안내한다.
		UE_LOG(LogMOUVoice, Warning,
			TEXT("Opus 인코더를 만들 수 없다. Voice 모듈이 비활성 상태다. MOU.Voice.Diag 로 진단할 것."));
		return false;
	}

	// VoiceEncode_Voice = 명료도 우선(VoIP). VoiceEncode_Audio 는 음악용이라
	// 같은 비트레이트에서 말소리가 더 뭉개진다. 무전기 톤에도 Voice 쪽이 맞다.
	Encoder = FVoiceModule::Get().CreateVoiceEncoder(
		MOUVoice::SampleRate, MOUVoice::NumChannels, EAudioEncodeHint::VoiceEncode_Voice);

	if (!Encoder.IsValid())
	{
		UE_LOG(LogMOUVoice, Warning, TEXT("Opus 인코더 생성에 실패했다. 압축 없이 원본 PCM 을 쓴다."));
		return false;
	}

	// ★ 비트레이트를 명시적으로 박는다.
	//   엔진 Init() 은 VBR 만 켜고 비트레이트는 Opus 자동값에 맡긴다. 그러면
	//   12절의 "프레임당 ~60바이트" 계산에 근거가 없어진다. 숫자를 우리가 정해야
	//   대역폭을 예측할 수 있고, 나중에 인터넷으로 확장할 때 내릴 여지도 생긴다.
	if (!Encoder->SetBitrate(MOUVoice::OpusBitrate))
	{
		// 치명적이지 않다. 자동값으로도 소리는 난다.
		UE_LOG(LogMOUVoice, Warning,
			TEXT("Opus 비트레이트를 %d bps 로 설정하지 못했다. 엔진 기본값으로 진행한다."),
			MOUVoice::OpusBitrate);
	}

	UE_LOG(LogMOUVoice, Log, TEXT("Opus 인코더 준비 완료 (%d Hz, %d채널, %d bps)."),
		MOUVoice::SampleRate, MOUVoice::NumChannels, MOUVoice::OpusBitrate);
	return true;
}

void FMOUVoiceEncoder::Shutdown()
{
	if (Encoder.IsValid())
	{
		Encoder->Destroy();
		Encoder.Reset();
	}
}

bool FMOUVoiceEncoder::Encode(const int16* Samples, int32 NumSamples, TArray<uint8>& OutEncoded)
{
	OutEncoded.Reset();

	if (!Encoder.IsValid() || !Samples || NumSamples <= 0)
	{
		return false;
	}

	// ★ 320샘플 미만은 통째로 버려진다.
	//   엔진 Encode() 는 (RawDataSize / BytesPerFrame) 개의 프레임만 인코딩하고
	//   나머지는 손대지 않은 채 그 크기를 반환한다. 320 미만이면 0개를 인코딩하고
	//   즉시 반환한다 - 에러가 아니라서 조용히 무음이 된다.
	//   캡처가 항상 320개씩 잘라주므로 정상 경로에서는 걸릴 일이 없지만,
	//   호출부가 바뀌었을 때 조용히 죽지 않도록 여기서 막는다.
	if (NumSamples < MOUVoice::SamplesPerFrame)
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("인코딩 입력이 %d샘플이다. 엔진 인코더는 %d샘플 단위로만 동작하므로 버려진다."),
			NumSamples, MOUVoice::SamplesPerFrame);
		return false;
	}

	// OutCompressedDataSize 는 **in/out** 이다.
	// 들어갈 때는 "버퍼에 쓸 수 있는 최대 크기", 나올 때는 "실제로 쓴 크기".
	// 이걸 0 으로 넣고 부르면 인코더가 쓸 공간이 없다고 판단한다.
	uint32 EncodedSize = static_cast<uint32>(Scratch.Num());

	const int32 Remainder = Encoder->Encode(
		reinterpret_cast<const uint8*>(Samples),
		static_cast<uint32>(NumSamples * sizeof(int16)),
		Scratch.GetData(),
		EncodedSize);

	if (EncodedSize == 0)
	{
		// 인코더가 실패를 이렇게 알린다(반환값이 아니라 크기 0 으로).
		return false;
	}

	if (Remainder > 0)
	{
		// 320 의 배수가 아닌 입력이 들어왔다는 뜻이다. 남은 꼬리는 사라진다.
		UE_LOG(LogMOUVoice, Verbose,
			TEXT("인코딩되지 않고 남은 바이트 %d개. 입력이 %d샘플의 배수가 아니다."),
			Remainder, MOUVoice::SamplesPerFrame);
	}

	OutEncoded.Append(Scratch.GetData(), static_cast<int32>(EncodedSize));

	// --- 통계 ---------------------------------------------------------------
	++FrameCount;
	TotalEncodedBytes += EncodedSize;
	MaxFrameBytes = FMath::Max(MaxFrameBytes, static_cast<int32>(EncodedSize));

	// ★ V3 에서 터질 문제를 V2 에서 미리 잡는다.
	//   프레임이 RPC 한 패킷에 안 들어가면 Unreliable RPC 는 조용히 버린다(15절).
	//   "V3 에서 어떤 사람 목소리만 안 들린다" 로 나타나면 원인 추적이 매우 어렵다.
	if (static_cast<int32>(EncodedSize) > MOUVoice::MaxEncodedFrameBytes && !bWarnedOversize)
	{
		bWarnedOversize = true;
		UE_LOG(LogMOUVoice, Warning,
			TEXT("★ 압축 프레임이 %u바이트로 상한(%d)을 넘었다. V3 의 Unreliable RPC 가 ")
			TEXT("이런 프레임을 조용히 버린다. 비트레이트(%d bps)를 낮추거나 상한을 재검토할 것. ")
			TEXT("(이 경고는 한 번만 남긴다)"),
			EncodedSize, MOUVoice::MaxEncodedFrameBytes, MOUVoice::OpusBitrate);
	}

	return true;
}

// ---------------------------------------------------------------------------
// 디코더
// ---------------------------------------------------------------------------

FMOUVoiceDecoder::FMOUVoiceDecoder()
{
}

FMOUVoiceDecoder::~FMOUVoiceDecoder()
{
	Shutdown();
}

bool FMOUVoiceDecoder::Initialize()
{
	if (Decoder.IsValid())
	{
		return true;
	}

	if (!FVoiceModule::IsAvailable() || !FVoiceModule::Get().IsVoiceEnabled())
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("Opus 디코더를 만들 수 없다. Voice 모듈이 비활성 상태다. MOU.Voice.Diag 로 진단할 것."));
		return false;
	}

	Decoder = FVoiceModule::Get().CreateVoiceDecoder(MOUVoice::SampleRate, MOUVoice::NumChannels);

	if (!Decoder.IsValid())
	{
		UE_LOG(LogMOUVoice, Warning, TEXT("Opus 디코더 생성에 실패했다."));
		return false;
	}

	UE_LOG(LogMOUVoice, Log, TEXT("Opus 디코더 준비 완료 (%d Hz, %d채널)."),
		MOUVoice::SampleRate, MOUVoice::NumChannels);
	return true;
}

void FMOUVoiceDecoder::Shutdown()
{
	if (Decoder.IsValid())
	{
		Decoder->Destroy();
		Decoder.Reset();
	}
}

void FMOUVoiceDecoder::Reset()
{
	if (Decoder.IsValid())
	{
		Decoder->Reset();
	}
}

bool FMOUVoiceDecoder::Decode(const uint8* Encoded, int32 NumBytes, TArray<int16>& OutSamples)
{
	if (!Decoder.IsValid() || !Encoded || NumBytes <= 0)
	{
		OutSamples.Reset();
		return false;
	}

	// ★★ 버퍼를 넉넉히 잡는 것이 **필수**다. 320개만 잡으면 소리가 안 난다.
	//   이유는 VoiceTypes.h 의 DecodeScratchSamples 주석에 있다(엔진이 남은 공간이
	//   6프레임분 미만이면 디코딩을 건너뛴다). 여기서 크기를 줄이는 "최적화" 를
	//   하면 증상이 "무음" 으로만 나타나 원인을 찾기 어렵다.
	OutSamples.SetNumUninitialized(MOUVoice::DecodeScratchSamples, EAllowShrinking::No);

	// 인코더와 마찬가지로 in/out 이다. 단위가 **샘플이 아니라 바이트**인 것에 주의.
	uint32 DecodedBytes = static_cast<uint32>(OutSamples.Num() * sizeof(int16));

	Decoder->Decode(
		Encoded,
		static_cast<uint32>(NumBytes),
		reinterpret_cast<uint8*>(OutSamples.GetData()),
		DecodedBytes);

	const int32 DecodedSamples = static_cast<int32>(DecodedBytes / sizeof(int16));

	if (DecodedSamples <= 0)
	{
		++FailureCount;

		if (!bWarnedFailure)
		{
			bWarnedFailure = true;
			UE_LOG(LogMOUVoice, Warning,
				TEXT("★ Opus 디코딩이 0샘플을 돌려줬다(입력 %d바이트). 출력 버퍼가 작으면 ")
				TEXT("엔진이 에러 없이 건너뛴다 - VoiceTypes.h 의 DecodeScratchSamples 주석 참고. ")
				TEXT("(이 경고는 한 번만 남긴다. 누적 횟수는 MOU.Voice.Stat 으로 볼 것)"),
				NumBytes);
		}

		OutSamples.Reset();
		return false;
	}

	// 실제로 나온 만큼만 남긴다. 위에서 1920개로 잡아뒀으므로 줄여야 한다 -
	// 안 줄이면 프레임 하나를 재생할 때마다 1600샘플(100ms)의 쓰레기가 같이 나간다.
	OutSamples.SetNum(DecodedSamples, EAllowShrinking::No);
	++FrameCount;

	return true;
}
