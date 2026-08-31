// MOU 음성 - 마이크 캡처 소스 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7-1절, 11절
//
// [스레드] 이 파일의 코드는 전부 게임 스레드다. 이유는 헤더 상단 주석 참고.

#include "Voice/VoiceCaptureSource.h"

#include "Interfaces/VoiceCapture.h"
#include "VoiceModule.h"

DEFINE_LOG_CATEGORY(LogMOUVoice);

FVoiceCaptureSource::FVoiceCaptureSource()
{
	// 엔진이 한 번에 주는 양이 들쭉날쭉하므로 1초치를 잡아둔다.
	// 매 틱 할당하지 않도록 멤버로 유지한다.
	ReadBuffer.SetNumUninitialized(MOUVoice::SampleRate * sizeof(int16));
}

FVoiceCaptureSource::~FVoiceCaptureSource()
{
	Shutdown();
}

bool FVoiceCaptureSource::Start()
{
	if (bReady)
	{
		return true;
	}

	// 호출자가 Voice 모듈을 이미 로드해 두었다는 전제다.
	// (모듈이 안 올라와 있으면 IsVoiceEnabled() 를 볼 수조차 없다)
	if (!FVoiceModule::Get().IsVoiceEnabled())
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("엔진 Voice 모듈이 비활성 상태다. DefaultEngine.ini 의 [Voice] bEnabled=true 를 확인할 것."));
		return false;
	}

	// DeviceName 을 비우면 시스템 기본 마이크를 쓴다.
	VoiceCapture = FVoiceModule::Get().CreateVoiceCapture(
		FString(), MOUVoice::SampleRate, MOUVoice::NumChannels);

	if (!VoiceCapture.IsValid())
	{
		// 마이크가 없거나 Windows 마이크 권한이 꺼져 있으면 여기로 온다.
		// 시스템 전체를 죽이지 않는다 - 마이크 없는 팀원도 게임은 해야 한다.
		UE_LOG(LogMOUVoice, Warning,
			TEXT("마이크를 열지 못했다. 장치가 없거나 Windows 마이크 권한(설정 > 개인 정보 > 마이크)이 ")
			TEXT("꺼져 있을 수 있다. 음성 없이 계속 진행한다."));
		return false;
	}

	if (!VoiceCapture->Start())
	{
		UE_LOG(LogMOUVoice, Warning, TEXT("마이크는 열렸지만 캡처 시작에 실패했다."));
		VoiceCapture->Shutdown();
		VoiceCapture.Reset();
		return false;
	}

	bReady = true;

	// 직전 세션의 마지막 음량이 남아 있으면 첫마디의 원이 엉뚱한 크기로 시작한다.
	LoudnessEnvelope = 0.f;
	CurrentLoudness  = 0.f;

	UE_LOG(LogMOUVoice, Log, TEXT("마이크 캡처 시작 (%d Hz, %d채널, %dms 프레임)."),
		MOUVoice::SampleRate, MOUVoice::NumChannels, MOUVoice::FrameMs);
	return true;
}

void FVoiceCaptureSource::Shutdown()
{
	if (VoiceCapture.IsValid())
	{
		VoiceCapture->Stop();
		VoiceCapture->Shutdown();
		VoiceCapture.Reset();
		UE_LOG(LogMOUVoice, Log, TEXT("마이크 캡처 종료."));
	}

	bReady = false;
	PendingBytes.Reset();

	LoudnessEnvelope = 0.f;
	CurrentLoudness  = 0.f;
}

void FVoiceCaptureSource::SetVadThreshold(float InThreshold)
{
	VadThreshold = FMath::Clamp(InThreshold, 0.f, 1.f);
}

void FVoiceCaptureSource::Poll(TArray<FMOUVoiceFrame>& OutFrames)
{
	if (!bReady || !VoiceCapture.IsValid())
	{
		return;
	}

	uint32 AvailableBytes = 0;
	const EVoiceCaptureState::Type State = VoiceCapture->GetCaptureState(AvailableBytes);

	if (State == EVoiceCaptureState::Ok && AvailableBytes > 0)
	{
		uint32 ReadBytes = 0;
		const EVoiceCaptureState::Type ReadState = VoiceCapture->GetVoiceData(
			ReadBuffer.GetData(), static_cast<uint32>(ReadBuffer.Num()), ReadBytes);

		if (ReadState == EVoiceCaptureState::Ok && ReadBytes > 0)
		{
			PendingBytes.Append(ReadBuffer.GetData(), static_cast<int32>(ReadBytes));
		}
	}

	DrainPendingSamples(OutFrames);
}

void FVoiceCaptureSource::DrainPendingSamples(TArray<FMOUVoiceFrame>& OutFrames)
{
	// 20ms(=640바이트) 단위로 정확히 잘라낸다. 남는 꼬리는 다음 회차로 넘긴다.
	while (PendingBytes.Num() >= MOUVoice::BytesPerFrame)
	{
		FMOUVoiceFrame Frame;
		Frame.Samples.SetNumUninitialized(MOUVoice::SamplesPerFrame);
		FMemory::Memcpy(Frame.Samples.GetData(), PendingBytes.GetData(), MOUVoice::BytesPerFrame);

		Frame.Loudness = ComputeRms(Frame.Samples.GetData(), MOUVoice::SamplesPerFrame);
		CurrentLoudness = Frame.Loudness;

		// --- 음량 엔벨로프 (반경 조절용) -------------------------------------
		//
		// 반경을 순간 RMS 로 정하면 음절 사이마다 원이 펄럭인다. 상승은 빠르게,
		// 하강은 느리게 따라가서 "말하는 동안 유지되는 크기" 를 만든다.
		//
		// ★ 진행 폭은 실제 경과 시간이 아니라 **FrameSeconds** 다. 이 루프는
		//   누적 버퍼를 20ms 씩 잘라내므로 한 틱에 프레임 여러 개가 나오는데,
		//   거기서 경과 시간을 쓰면 첫 프레임이 시간을 다 먹고 나머지는 0 이
		//   되어 엔벨로프가 계단이 된다(VoiceTypes.h 의 함수 주석).
		LoudnessEnvelope = MOUVoice::AdvanceLoudnessEnvelope(
			LoudnessEnvelope, Frame.Loudness, MOUVoice::FrameSeconds);
		Frame.LoudnessEnvelope = LoudnessEnvelope;

		// --- VAD: 임계값 + hangover -----------------------------------------
		// hangover 가 없으면 단어 사이 공백마다 발화가 끊겼다 이어져서
		// 말끝이 잘리고 NPC 소음 이벤트도 잘게 쪼개진다.
		const double Now = FPlatformTime::Seconds();

		if (Frame.Loudness >= VadThreshold)
		{
			bSpeaking = true;
			SilenceStartedAt = 0.0;
		}
		else if (bSpeaking)
		{
			if (SilenceStartedAt == 0.0)
			{
				SilenceStartedAt = Now;
			}
			else if (Now - SilenceStartedAt >= MOUVoice::VadHangoverSeconds)
			{
				bSpeaking = false;
				SilenceStartedAt = 0.0;
			}
		}

		Frame.bIsSpeaking = bSpeaking;
		OutFrames.Add(MoveTemp(Frame));

		PendingBytes.RemoveAt(0, MOUVoice::BytesPerFrame, EAllowShrinking::No);
	}
}

float FVoiceCaptureSource::ComputeRms(const int16* Samples, int32 NumSamples)
{
	if (!Samples || NumSamples <= 0)
	{
		return 0.f;
	}

	// double 로 누적한다. float 로 하면 320개를 더하는 동안 정밀도가 눈에 띄게 깎인다.
	double SumOfSquares = 0.0;
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const double Normalized = static_cast<double>(Samples[Index]) / 32768.0;
		SumOfSquares += Normalized * Normalized;
	}

	return static_cast<float>(FMath::Sqrt(SumOfSquares / static_cast<double>(NumSamples)));
}
