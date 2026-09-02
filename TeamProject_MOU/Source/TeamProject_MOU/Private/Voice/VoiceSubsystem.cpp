// MOU 음성 - 음성 시스템 진입점 구현 + 콘솔 명령.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7절, 11절, 14절(마일스톤)
//
// [스레드]
//   이 파일의 코드는 전부 게임 스레드다.
//   마이크 캡처도 게임 스레드다(VoiceCaptureSource.h 상단 주석에 이유가 있다).
//   유일한 다른 스레드는 오디오 렌더 스레드이고 VoiceSynthComponent.cpp 가 담당한다.

#include "Voice/VoiceSubsystem.h"

#include "Voice/VoiceCaptureSource.h"
#include "Voice/VoiceCodec.h"
#include "Voice/VoiceComponent.h"
#include "Voice/VoicePlaybackComponent.h"
#include "Voice/VoiceRouter.h"
#include "Voice/VoiceSynthComponent.h"

#include "AIController.h"
#include "DrawDebugHelpers.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "Modules/ModuleManager.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISense.h"
#include "Perception/AISenseConfig.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISense_Hearing.h"
#include "TimerManager.h"
#include "VoiceModule.h"

// ---------------------------------------------------------------------------
// 서브시스템 수명
// ---------------------------------------------------------------------------

namespace
{
	/**
	 * 이 프로세스에서 마이크를 잡고 있는 서브시스템.
	 *
	 * ★ PIE 다중 창 대비책이다(VOICE_INTEGRATION.md 6절).
	 *
	 *   PIE 로 창 두 개를 띄우면 한 프로세스 안에 로컬 플레이어가 둘 생기고,
	 *   서브시스템도 둘이 된다. 둘 다 **같은 물리 마이크를 열려고 해서** 장치
	 *   경합이 난다. 나쁜 것은 실패 방식이다 - 둘째 창이 조용히 실패하거나,
	 *   더 나쁘게는 첫 창의 캡처를 망가뜨려서 **"아까는 됐는데 왜 지금 안 되지"**
	 *   가 된다.
	 *
	 *   그래서 먼저 온 쪽만 마이크를 잡는다. 나머지 창은 **재생만** 한다 -
	 *   V3 의 검증 방법(PIE 2창에서 거리에 따라 들리고 안 들리는지)에는
	 *   말하는 쪽이 하나면 충분하다.
	 *
	 *   진짜 양방향 통화 테스트는 프로세스를 두 개 띄워서 한다.
	 *
	 * 약참조인 이유: 이 포인터를 역참조할 일은 없지만, 서브시스템이 예기치 않게
	 * 사라졌을 때 자리가 영영 안 풀리는 것을 막는다.
	 */
	TWeakObjectPtr<UVoiceSubsystem> GVoiceCaptureOwner;
}

void UVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// ★ Voice 모듈을 여기(게임 스레드)에서 명시적으로 로드한다.
	//
	//   Build.cs 에 의존성을 걸어도 그건 **링크**만 보장한다. 런타임에 모듈이
	//   실제로 로드돼 있는지는 별개다. FVoiceModule::IsAvailable() 은
	//   IsModuleLoaded("Voice") 라서, 아무도 안 불렀으면 false 를 돌려준다.
	//   그 상태로 캡처를 만들려 하면 "마이크가 없다" 로 오진하게 된다.
	//
	//   모듈 로드는 게임 스레드에서만 해야 하므로 워커가 아니라 여기서 한다.
	//   FVoiceModule::StartupModule() 이 ini 를 읽고 캡처 장치를 초기화한다.
	FModuleManager::Get().LoadModule(TEXT("Voice"));

	if (!FVoiceModule::IsAvailable())
	{
		UE_LOG(LogMOUVoice, Error,
			TEXT("Voice 모듈을 로드하지 못했다. 음성 기능을 사용할 수 없다."));
	}
	else if (!FVoiceModule::Get().IsVoiceEnabled())
	{
		// 여기 걸리면 십중팔구 ini 문제다. 진단 문구를 구체적으로 남긴다.
		UE_LOG(LogMOUVoice, Error,
			TEXT("Voice 모듈은 로드됐지만 비활성 상태다. ")
			TEXT("Config/DefaultEngine.ini 에 [Voice] bEnabled=true 가 있는지, ")
			TEXT("그리고 에디터를 재시작했는지 확인할 것(ini 는 시작 시 한 번만 읽는다)."));
	}
	else if (GVoiceCaptureOwner.IsValid())
	{
		// 이 프로세스에서 이미 다른 창이 마이크를 잡았다(위 GVoiceCaptureOwner 주석).
		// 재생은 정상 동작하므로 여기서 끝내도 된다 - 남의 목소리는 들린다.
		UE_LOG(LogMOUVoice, Log,
			TEXT("이 창은 마이크를 열지 않는다(같은 프로세스의 다른 창이 이미 사용 중). ")
			TEXT("듣기만 한다. 양방향 테스트는 프로세스를 두 개 띄울 것."));
	}
	else
	{
		GVoiceCaptureOwner = this;

		// 마이크 열기. 실패해도 게임은 정상 진행된다(재생만 동작).
		CaptureSource = MakeUnique<FVoiceCaptureSource>();
		CaptureSource->Start();

		// 코덱은 마이크와 독립적으로 만든다.
		//
		// ★ 마이크가 없어도 디코더는 필요하다. V3 에서 마이크 없는 팀원도
		//   남의 목소리는 들어야 하기 때문이다(15절: "마이크가 없거나 권한이
		//   없으면 전체 시스템이 죽지 않고 재생만 동작해야 한다").
		//   지금부터 그 구조로 만들어 두면 V3 에서 손댈 것이 없다.
		Encoder = MakeUnique<FMOUVoiceEncoder>();
		Encoder->Initialize();

		Decoder = MakeUnique<FMOUVoiceDecoder>();
		Decoder->Initialize();
	}

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UVoiceSubsystem::Tick));

	UE_LOG(LogMOUVoice, Log,
		TEXT("음성 서브시스템 초기화 완료 (마이크=%s). 루프백을 켜려면 MOU.Voice.Loopback 1"),
		IsCaptureReady() ? TEXT("준비됨") : TEXT("없음"));
}

void UVoiceSubsystem::Deinitialize()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}

	// ★ 순서가 중요하다: 재생을 먼저 멈추고 나서 캡처를 정리한다.
	//   반대로 하면 오디오 렌더 스레드가 아직 도는 동안 버퍼가 사라질 수 있다.
	//
	// IsValid() 로 검사하는 이유: PlayerController 가 먼저 파괴되면 컴포넌트도
	// 같이 파괴되지만, UPROPERTY 포인터는 GC 가 돌기 전까지 null 이 되지 않는다.
	// 그 사이에 Deinitialize 가 오면 이미 죽은 컴포넌트를 만지게 된다.
	if (IsValid(PlaybackComponent))
	{
		PlaybackComponent->Stop();
		PlaybackComponent->DestroyComponent();
	}
	PlaybackComponent = nullptr;

	// 마이크를 닫는다. 게임 스레드에서만 도는 객체라 스레드 종료 대기가 필요 없다.
	// (워커 스레드였을 때는 Kill(true) 순서가 필수였지만 이제 해당 없음)
	CaptureSource.Reset();

	// 코덱은 캡처 뒤에 정리한다. 캡처가 살아있는 동안 인코더가 사라지면
	// 남은 프레임을 처리하다 죽은 인코더를 만질 수 있다.
	Encoder.Reset();
	Decoder.Reset();

	// 마이크 자리를 놓아준다. 안 놓으면 PIE 를 껐다 켰을 때 **아무도 마이크를
	// 못 잡는다** - 죽은 서브시스템이 자리를 차지한 채로 남기 때문이다.
	// (약참조라 자동으로 풀리긴 하지만, 여기서 명시적으로 놓는 편이 읽기 쉽다)
	if (GVoiceCaptureOwner.Get() == this)
	{
		GVoiceCaptureOwner.Reset();
	}

	Super::Deinitialize();
}

UVoiceSubsystem* UVoiceSubsystem::Get(const UObject* WorldContextObject)
{
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;

	return LocalPlayer ? LocalPlayer->GetSubsystem<UVoiceSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------
// 틱 - 워커 큐를 비워 재생으로 넘긴다
// ---------------------------------------------------------------------------

bool UVoiceSubsystem::Tick(float DeltaTime)
{
	if (!CaptureSource)
	{
		return true;
	}

	// 마이크를 폴링한다. 게임 스레드에서 부르는 것이 필수다(헤더 주석 참고).
	//
	// ★ 음소거 중에도 Poll() 자체는 부른다.
	//   설계 문서는 "캡처 자체를 중단한다" 고 적었지만, 여기서 아예 안 부르면
	//   엔진의 내부 캡처 버퍼(우리가 안 가져가는 동안 계속 쌓인다)가 얼마나
	//   쌓일지 우리가 통제할 수 없다 - 음소거를 오래 켜뒀다가 풀면 그동안
	//   쌓인 오디오가 한꺼번에 쏟아지는 것을 배제할 수 없다.
	//   그래서 Poll 은 계속 해서 엔진 버퍼를 비우되, **결과를 밑에서 버린다.**
	//   "말하는 상태가 밖으로 전혀 안 나간다" 는 결과는 동일하게 보장된다.
	PolledFrames.Reset();
	CaptureSource->Poll(PolledFrames);

	// 이번 틱에 쓸 창구를 한 번만 찾는다.
	// (사망 검사와 전송 양쪽이 필요한데, 매번 찾으면 캐시가 빈 첫 틱에 검사가
	//  건너뛰어져 죽은 상태로 한 프레임이 나갈 수 있다)
	UVoiceComponent* VoiceComp = GetVoiceComponent();

	// --- 사망자 차단 1겹: 클라이언트 캡처 중단 (V5) --------------------------
	//
	// ★ 이건 **방어가 아니라 알려주기다.** 진짜 차단은 서버가 한다(VoiceRouter).
	//   개조 클라이언트는 이 검사를 지우면 그만이므로 여기에 기대면 안 된다.
	//
	//   그래도 두는 이유 두 가지:
	//     · 죽은 사람이 계속 인코딩·전송을 하며 대역폭을 낭비하지 않는다
	//     · 화면 표시가 "말하는 중" 으로 남지 않는다 - 안 들리는 줄 모르고
	//       계속 말하는 답답한 상황을 막는다
	if (VoiceComp && VoiceComp->IsVoiceDead())
	{
		bIsSpeaking = false;
		return true;
	}

	if (bMuted)
	{
		// 밖에서 보기엔 마이크가 꺼진 것과 같다: 발화 상태 강제 OFF, 재생/전송 없음.
		bIsSpeaking = false;
		return true;
	}

	const bool bReceivedAnyFrame = PolledFrames.Num() > 0;

	for (const FMOUVoiceFrame& Frame : PolledFrames)
	{
		++FramesReceived;
		bIsSpeaking = Frame.bIsSpeaking;

		// --- 0. 감도 보정 중이면 측정만 한다 ---------------------------------
		//
		// 조용히 있으라고 해놓고 그 소리를 남에게 보내면 안 되므로, 보정 중에는
		// 인코딩도 전송도 재생도 하지 않는다. VAD 판정(bIsSpeaking)은 그대로
		// 돌게 두는데, 위젯에서 "지금 기준이 얼마나 안 맞는지" 가 보여야 하기 때문이다.
		if (bCalibrating)
		{
			CalibrationPeak = FMath::Max(CalibrationPeak, Frame.Loudness);
			++CalibrationSamples;
			continue;
		}

		// 무음 구간은 여기서 끝낸다. 인코딩도 하지 않는다.
		//
		// VAD 로 무음을 걸러내는 것은 대역폭 절감이기도 하지만, 더 중요하게는
		// **"지금 소리를 내고 있는가" 가 곧 게임 규칙**이기 때문이다(7-1절).
		// V8 의 NPC 소음 이벤트도 같은 판정을 입력으로 쓴다. 그래서 걸러내는
		// 지점이 여기 한 곳이어야 전송·소음·재생이 서로 어긋나지 않는다.
		if (!Frame.bIsSpeaking)
		{
			continue;
		}

		// --- 1. 인코딩 ------------------------------------------------------
		//
		// ★ 루프백 여부와 무관하게 인코딩한다.
		//   V3 에서 이 자리가 "서버로 전송" 이 되기 때문이다. 지금 루프백 분기
		//   안쪽에 넣어두면 V3 에서 다시 끄집어내야 한다.
		//   부수적으로, 루프백을 끈 채로도 MOU.Voice.Stat 의 압축률을 볼 수 있다.
		bool bEncoded = false;
		if (bCodecEnabled && Encoder.IsValid() && Encoder->IsReady())
		{
			bEncoded = Encoder->Encode(Frame.Samples.GetData(), Frame.Samples.Num(), EncodedScratch);
		}

		// --- 2. 서버로 전송 (V3) ---------------------------------------------
		//
		// 여기부터 남이 듣는다. 아래 루프백은 이제 **디버그 전용**이다.
		//
		// ★ 코덱이 꺼져 있으면 아무것도 못 보낸다. 네트워크로 나가는 형식이
		//   Opus 바이트라서 원본 PCM 을 실을 자리가 없기 때문이다.
		//   MOU.Voice.Codec 0 은 그래서 "루프백으로만 들어보는" 모드가 된다.
		if (bEncoded && VoiceComp)
		{
			// 무전 송신 중이면 무전으로 **요청**한다. 실제로 무전이 나갈지는
			// 서버가 정한다 - 무전기 소지·전원·채널 점유를 전부 다시 확인한다.
			//
			// 근접으로도 나가는 것은 서버가 알아서 한다(무전을 쳐도 육성은 난다).
			// 클라가 프레임을 두 번 보내지 않는다 - 같은 소리를 두 번 인코딩하고
			// 두 번 전송하는 낭비이고, 서버가 한 번 받아 두 갈래로 뿌리면 된다.
			const EVoiceRoute RequestedRoute = bRadioTransmitting
				? EVoiceRoute::Radio
				: EVoiceRoute::Proximity;

			// ★ 원본 RMS 가 아니라 **정규화된 발화 강도**를 보낸다.
			//
			//   서버는 이 클라의 마이크 보정값(VadThreshold)을 모르므로, 원본을
			//   받아서는 "크게 말한 것" 과 "마이크가 센 것" 을 구분할 수 없다.
			//   그대로 반경에 쓰면 부스트를 켠 마이크가 공짜로 큰 원을 얻는다 -
			//   숨는 것이 핵심인 게임에서 밸런스가 무너진다.
			//   그래서 보정값을 아는 이쪽에서 정규화해 보낸다(VoiceTypes.h).
			//
			//   엔벨로프를 통과한 값을 쓴다. 순간 RMS 를 보내면 받는 쪽 원이
			//   음절마다 펄럭인다.
			const float Intensity = MOUVoice::NormalizeLoudness(
				Frame.LoudnessEnvelope, GetMicSensitivity());

			VoiceComp->SendVoiceFrame(EncodedScratch, Intensity, VoiceMode, RequestedRoute);
			++FramesTransmitted;
		}

		if (!bLoopbackEnabled || !PlaybackComponent)
		{
			// 루프백이 꺼져 있으면 여기서 끝. 남에게는 위에서 이미 보냈다.
			continue;
		}

		// --- 3. 로컬 루프백 (디버그 전용) ------------------------------------
		//
		// 내 목소리를 내가 듣는 경로다. 서버는 발신자 본인에게 프레임을 보내지
		// 않으므로(에코 방지), 이 경로가 없으면 자기 소리를 확인할 방법이 없다.
		//
		// 코덱이 꺼져 있으면 원본 PCM 이 그대로 간다. 켜고 끄며 비교하는 것이
		// V2 의 검증 방법이다(MOU.Voice.Codec).
		const int16* PlaybackSamples = Frame.Samples.GetData();
		int32        PlaybackCount   = Frame.Samples.Num();

		if (bEncoded)
		{
			const bool bDecoded = Decoder.IsValid()
				&& Decoder->Decode(EncodedScratch.GetData(), EncodedScratch.Num(), DecodedScratch);

			if (!bDecoded)
			{
				// ★ 실패했을 때 원본 PCM 으로 대신 재생하지 않는다.
				//   그렇게 하면 코덱이 완전히 고장나 있어도 소리는 멀쩡히 나서
				//   V3 에 가서야 문제를 발견하게 된다. 지금은 조용한 편이 낫다 -
				//   실패 횟수는 MOU.Voice.Stat 에 남는다.
				continue;
			}

			PlaybackSamples = DecodedScratch.GetData();
			PlaybackCount   = DecodedScratch.Num();
		}

		// --- 4. 재생 --------------------------------------------------------
		//
		// 지연이 쌓이는 것을 막는다.
		//
		// 마이크 입력이 재생 소비보다 빠르면 버퍼가 계속 차서 "지금 말한 것이
		// 몇 초 뒤에 들리는" 상태가 된다. 임계치를 넘으면 버퍼를 비워
		// 현재 시점으로 되돌린다. 음성은 오래된 것을 버리는 게 맞다.
		const int32 BufferedFrames =
			PlaybackComponent->GetBufferedSampleCount() / MOUVoice::SamplesPerFrame;

		if (BufferedFrames >= MOUVoice::PlaybackDropThresholdFrames)
		{
			PlaybackComponent->RequestFlush();
			++FramesDropped;
		}

		PlaybackComponent->PushSamples(PlaybackSamples, PlaybackCount);
	}

	// ★ 프레임이 아예 안 오는 경우에도 발화 상태를 내려야 한다.
	//
	// 엔진 캡처는 무음 구간에 데이터를 아예 주지 않을 수 있다(내부 노이즈 게이트).
	// 그러면 위 루프가 한 번도 안 돌아 bIsSpeaking 이 마지막 값(true)에 그대로
	// 붙박인다. 그 상태로 V8 의 소음 이벤트가 붙으면 "말을 멈췄는데 NPC 가
	// 계속 쫓아오는" 버그가 된다. 마이크가 뽑히거나 캡처가 죽어도 마찬가지다.
	const double Now = FPlatformTime::Seconds();
	if (bReceivedAnyFrame)
	{
		LastFrameTime = Now;
	}
	else if (bIsSpeaking && LastFrameTime > 0.0
		&& (Now - LastFrameTime) > MOUVoice::VadHangoverSeconds)
	{
		bIsSpeaking = false;
	}

	// ★ 발화가 끝나는 순간 디코더 상태를 초기화한다.
	//
	// Opus 디코더는 직전 프레임을 참고해 다음 프레임을 복원한다. 발화 사이에
	// 몇 초의 공백이 있어도 디코더는 그것을 모르므로, 새 발화의 첫 프레임을
	// **공백 전의 소리를 참고해서** 푼다. 그러면 말 첫머리가 짧게 지직거린다.
	//
	// 여기서 잡는 이유: 이 전환(true -> false)은 위의 hangover 로직까지 다 돌고
	// 나야 확정된다. 루프 안에서 프레임 단위로 보면 hangover 로 아직 살아있는
	// 구간을 발화 종료로 오판한다.
	if (bWasSpeaking && !bIsSpeaking && Decoder.IsValid())
	{
		Decoder->Reset();
	}
	bWasSpeaking = bIsSpeaking;

	// 보정 시간이 다 됐는지는 프레임이 오든 안 오든 확인해야 한다.
	// 위 루프 안에서 보면 마이크가 조용해 프레임이 안 오는 동안 보정이
	// 영원히 안 끝난다 - 하필 **조용히 있으라고 한 상황**이라 확률이 높다.
	if (bCalibrating && FPlatformTime::Seconds() >= CalibrationEndTime)
	{
		FinishCalibration();
	}

	DrawRadiusDebug();

	return true; // 계속 틱
}

void UVoiceSubsystem::DrawRadiusDebug()
{
#if ENABLE_DRAW_DEBUG
	// 말하고 있을 때만 그린다. 조용할 때도 계속 그리면 "지금 소리가 나가는가" 를
	// 눈으로 구분할 수 없어서 시각화의 의미가 없어진다.
	if (!bShowRadiusDebug || !bIsSpeaking)
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
	APlayerController* PC = (LocalPlayer && World) ? LocalPlayer->GetPlayerController(World) : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	if (!Pawn)
	{
		return;
	}

	// 발 밑에서 살짝 띄운다. 바닥과 정확히 같은 높이면 지면에 파묻혀 잘 안 보인다.
	const FVector Center = Pawn->GetActorLocation() - FVector(0.f, 0.f, Pawn->GetSimpleCollisionHalfHeight() - 2.f);

	// XY 평면(바닥과 평행)에 그리기 위한 축. 기본값은 세로 원이라 바꿔줘야 한다.
	const FVector AxisX(1.f, 0.f, 0.f);
	const FVector AxisY(0.f, 1.f, 0.f);

	constexpr int32 Segments = 48;
	constexpr float LifeTime = -1.f;  // 한 프레임만. 매 틱 다시 그린다
	constexpr uint8 Depth    = 0;

	// ★★ 실제로 나가는 값과 **같은 함수**를 거쳐야 한다.
	//
	//   여기서 보이는 원이 곧 판정 범위여야 밸런싱이 가능하다. 정규화 인자도
	//   전송할 때와 똑같이 (엔벨로프, 내 감도) 조합이어야 한다 - 여기만
	//   원본 RMS 를 쓰면 화면의 원이 남에게 들리는 거리와 조용히 어긋난다
	//   (VoiceTypes.h 의 단일 진실 공급원 주석).
	const float Envelope  = GetLoudnessEnvelope();
	const float Intensity = GetCurrentIntensity();
	const float Scale     = MOUVoice::GetRadiusScaleFromNormalized(Intensity);

	const float HearRadius = MOUVoice::GetScaledHearRadius(VoiceMode, Intensity);
	const float NoiseRange = MOUVoice::GetScaledNoiseRange(VoiceMode, Intensity);

	// 모드가 정하는 상한. 얇은 회색으로 같이 그린다.
	//
	// ★ 이게 없으면 **지금 원이 작은 이유가 조용히 말해서인지, 모드가
	//   속삭임이라서인지 화면만 봐서는 구분할 수 없다.** 두 원의 간격이 곧
	//   "더 크게 말하면 얼마나 커지는가" 라서, 곡선을 튜닝할 때 이 여백을 본다.
	DrawDebugCircle(World, Center, MOUVoice::GetHearRadius(VoiceMode), Segments,
		FColor(90, 90, 90), false, LifeTime, Depth, 1.f, AxisX, AxisY, /*bDrawAxis=*/false);
	DrawDebugCircle(World, Center, MOUVoice::GetNoiseRange(VoiceMode), Segments,
		FColor(90, 60, 60), false, LifeTime, Depth, 1.f, AxisX, AxisY, /*bDrawAxis=*/false);

	// 초록 = 사람이 듣는 거리 (음량이 반영된 실제 값)
	DrawDebugCircle(World, Center, HearRadius, Segments, FColor::Green,
		false, LifeTime, Depth, 4.f, AxisX, AxisY, /*bDrawAxis=*/false);

	// 빨강 = NPC 가 듣는 거리. 일부러 더 넓다(13절).
	DrawDebugCircle(World, Center, NoiseRange, Segments, FColor::Red,
		false, LifeTime, Depth, 4.f, AxisX, AxisY, /*bDrawAxis=*/false);

	// 머리 위 숫자. 원만 보면 "왜 이 크기인지" 를 알 수 없어서, 곡선의 입력부터
	// 출력까지(순간 RMS -> 엔벨로프 -> 정규화 -> 배율 -> 거리) 순서대로 찍는다.
	// 튜닝할 때 어느 단계에서 값이 뭉개지는지 이 줄 하나로 좁힐 수 있다.
	DrawDebugString(World, Pawn->GetActorLocation() + FVector(0.f, 0.f, 100.f),
		FString::Printf(
			TEXT("%s  들림 %.1fm / NPC %.1fm  (상한 %.0fm / %.0fm)\n")
			TEXT("RMS %.3f -> 엔벨 %.3f -> 강도 %.2f -> 배율 %.2f  [감도 %.3f, 상한 %.2f]"),
			MOUVoice::GetVoiceModeName(VoiceMode),
			HearRadius / 100.f, NoiseRange / 100.f,
			MOUVoice::GetHearRadius(VoiceMode) / 100.f,
			MOUVoice::GetNoiseRange(VoiceMode) / 100.f,
			GetCurrentLoudness(), Envelope, Intensity, Scale,
			GetMicSensitivity(), MOUVoice::LoudnessCeiling),
		nullptr, FColor::White, 0.f /*이번 프레임만*/);
#endif
}

// ---------------------------------------------------------------------------
// 루프백
// ---------------------------------------------------------------------------

APlayerController* UVoiceSubsystem::GetOwningPlayerController() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;

	// ★ 인자 없는 ULocalPlayer::GetPlayerController() 는 존재하지 않는다.
	//   (그건 FLocalPlayerContext 의 것이다) UPlayer 의 월드 인자 버전을 쓴다.
	return (LocalPlayer && World) ? LocalPlayer->GetPlayerController(World) : nullptr;
}

UVoiceComponent* UVoiceSubsystem::GetVoiceComponent()
{
	// 캐시가 살아있으면 그대로 쓴다. 컨트롤러가 갈리면 약참조가 풀려 아래로 떨어진다.
	if (UVoiceComponent* Cached = CachedVoiceComponent.Get())
	{
		return Cached;
	}

	APlayerController* PC = GetOwningPlayerController();
	UVoiceComponent* Found = UVoiceComponent::Find(PC);

	if (!Found)
	{
		// ★ 여기 걸리면 원인은 거의 항상 하나다: 쓰고 있는 PlayerController 클래스가
		//   ATeamProject_MOUPlayerController 를 상속하지 않는다.
		//   증상이 "내 목소리가 아무에게도 안 들린다" 뿐이라 알아채기 어려워서,
		//   PC 가 있는데 컴포넌트만 없는 경우에만 한 번 크게 남긴다.
		//   (PC 자체가 아직 없는 것은 레벨 로드 중 정상 상황이라 조용히 넘어간다)
		static bool bWarnedMissingComponent = false;
		if (PC && !bWarnedMissingComponent)
		{
			bWarnedMissingComponent = true;
			UE_LOG(LogMOUVoice, Error,
				TEXT("★ PlayerController(%s)에 UVoiceComponent 가 없다. 음성이 서버로 나가지 않는다. ")
				TEXT("게임모드의 PlayerController 클래스가 ATeamProject_MOUPlayerController 를 ")
				TEXT("상속하는지 확인할 것."),
				*GetNameSafe(PC->GetClass()));
		}
		return nullptr;
	}

	CachedVoiceComponent = Found;
	return Found;
}

void UVoiceSubsystem::EnsurePlaybackComponent()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
	APlayerController* PC = (LocalPlayer && World) ? LocalPlayer->GetPlayerController(World) : nullptr;

	if (!World || !PC)
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("재생 컴포넌트를 만들 수 없다. 아직 PlayerController 가 없다(레벨 로드 중일 수 있다)."));
		return;
	}

	// 레벨을 이동하면 기존 컴포넌트는 죽은 월드에 남는다.
	// LocalPlayer 는 트래블을 넘어 살아있으므로 여기서 다시 만들어야 한다.
	if (PlaybackComponent && (!IsValid(PlaybackComponent) || PlaybackComponent->GetWorld() != World))
	{
		if (IsValid(PlaybackComponent))
		{
			PlaybackComponent->Stop();
			PlaybackComponent->DestroyComponent();
		}
		PlaybackComponent = nullptr;
	}

	if (PlaybackComponent)
	{
		return;
	}

	// PlayerController 를 소유자로 삼는다.
	// 폰에 붙이면 죽거나 리스폰할 때 같이 사라지는데, 음성은 그와 무관하게
	// 유지돼야 한다(관전 중 무전 수신 등, V6 이후).
	PlaybackComponent = NewObject<UVoiceSynthComponent>(PC, TEXT("MOUVoicePlayback"));
	PlaybackComponent->RegisterComponent();
	PlaybackComponent->Start();

	UE_LOG(LogMOUVoice, Log, TEXT("음성 재생 컴포넌트 생성 완료."));
}

void UVoiceSubsystem::SetLoopbackEnabled(bool bEnabled)
{
	if (bLoopbackEnabled == bEnabled)
	{
		return;
	}

	bLoopbackEnabled = bEnabled;

	if (bEnabled)
	{
		EnsurePlaybackComponent();

		if (!PlaybackComponent)
		{
			bLoopbackEnabled = false;
			return;
		}

		// 껐다 켜는 사이에 남은 옛 소리를 버린다.
		PlaybackComponent->RequestFlush();

		if (!IsCaptureReady())
		{
			UE_LOG(LogMOUVoice, Warning,
				TEXT("루프백을 켰지만 마이크가 준비되지 않았다. 소리가 나지 않는다."));
		}
		else
		{
			UE_LOG(LogMOUVoice, Log,
				TEXT("루프백 ON. ★헤드폰을 쓸 것 - 스피커로 들으면 하울링이 난다."));
		}
	}
	else
	{
		if (PlaybackComponent)
		{
			PlaybackComponent->RequestFlush();
		}
		UE_LOG(LogMOUVoice, Log, TEXT("루프백 OFF."));
	}
}

void UVoiceSubsystem::SetCodecEnabled(bool bEnabled)
{
	if (bCodecEnabled == bEnabled)
	{
		return;
	}

	bCodecEnabled = bEnabled;

	// 경로가 바뀌는 순간이므로 양쪽 다 정리한다.
	// 코덱을 켜고 끄는 사이에 남은 소리가 섞이면 A/B 비교의 의미가 없다.
	if (PlaybackComponent)
	{
		PlaybackComponent->RequestFlush();
	}
	if (Decoder.IsValid())
	{
		Decoder->Reset();
	}

	if (bEnabled && Encoder.IsValid() && !Encoder->IsReady())
	{
		// 시작할 때 실패했을 수 있다(마이크 권한과 무관하게 모듈 문제 등).
		// 여기서 한 번 더 시도해 본다 - 콘솔로 켜는 시점엔 상황이 달라졌을 수 있다.
		Encoder->Initialize();
	}

	UE_LOG(LogMOUVoice, Log, TEXT("Opus 코덱 %s. %s"),
		bEnabled ? TEXT("ON") : TEXT("OFF"),
		bEnabled ? TEXT("압축을 통과한 소리가 들린다.")
		         : TEXT("원본 PCM 이 그대로 들린다(비교용)."));
}

void UVoiceSubsystem::SetVoiceMode(EVoiceMode NewMode)
{
	if (VoiceMode == NewMode)
	{
		return;
	}

	VoiceMode = NewMode;

	UE_LOG(LogMOUVoice, Log, TEXT("발화 모드 = %s (들림 %.0fcm / NPC %.0fcm)"),
		MOUVoice::GetVoiceModeName(VoiceMode),
		MOUVoice::GetHearRadius(VoiceMode),
		MOUVoice::GetNoiseRange(VoiceMode));
}

void UVoiceSubsystem::SetShowRadiusDebug(bool bEnabled)
{
	bShowRadiusDebug = bEnabled;
	UE_LOG(LogMOUVoice, Log, TEXT("소리 범위 표시 %s. %s"),
		bEnabled ? TEXT("ON") : TEXT("OFF"),
		bEnabled ? TEXT("말하는 동안에만 링이 보인다(초록=사람, 빨강=NPC).") : TEXT(""));
}

void UVoiceSubsystem::SetMuted(bool bInMuted)
{
	if (bMuted == bInMuted)
	{
		return;
	}

	bMuted = bInMuted;

	if (bMuted)
	{
		// 즉시 조용해진다. 이미 재생 버퍼에 들어간 소리가 꼬리처럼 남는 것을 막는다.
		bIsSpeaking = false;
		if (PlaybackComponent)
		{
			PlaybackComponent->RequestFlush();
		}

		// 음소거는 Tick 의 발화 종료 감지를 우회한다(위쪽에서 early return 한다).
		// 그래서 여기서 직접 처리해야 음소거를 풀었을 때 첫마디가 지직거리지 않는다.
		if (Decoder.IsValid())
		{
			Decoder->Reset();
		}
		bWasSpeaking = false;
	}

	UE_LOG(LogMOUVoice, Log, TEXT("마이크 %s."), bMuted ? TEXT("음소거") : TEXT("음소거 해제"));
}

bool UVoiceSubsystem::IsCaptureReady() const
{
	return CaptureSource.IsValid() && CaptureSource->IsReady();
}

bool UVoiceSubsystem::IsVoiceDead() const
{
	// const 함수라 캐시를 갱신할 수 없으므로 캐시된 것만 본다.
	// 캐시가 비어 있으면(아직 한 번도 안 찾음) 사망이 아닌 것으로 본다 -
	// 모르는 상태를 "죽음" 으로 잡으면 접속 직후 음성이 통째로 막힌다.
	const UVoiceComponent* Voice = CachedVoiceComponent.Get();
	return Voice ? Voice->IsVoiceDead() : false;
}

void UVoiceSubsystem::SetRadioTransmitting(bool bTransmitting)
{
	if (bRadioTransmitting == bTransmitting)
	{
		return;
	}

	bRadioTransmitting = bTransmitting;

	UE_LOG(LogMOUVoice, Log, TEXT("무전 송신 %s. %s"),
		bTransmitting ? TEXT("시작") : TEXT("중지"),
		bTransmitting
			? TEXT("(무전기를 들고 켜둬야 실제로 나간다 - 서버가 확인한다)")
			: TEXT(""));
}

bool UVoiceSubsystem::IsReceivingRadio() const
{
	// ★ 재생 컴포넌트를 캐시하지 않는다. 이건 PlayerController 에 붙어 있어서
	//   레벨을 옮기면 컨트롤러째로 새로 생긴다 - 캐시하면 죽은 포인터가 된다.
	//   0.1초에 한 번 FindComponentByClass 하는 비용은 없는 것과 같다.
	const APlayerController* PC = GetOwningPlayerController();

	if (!IsValid(PC))
	{
		return false;
	}

	const UVoicePlaybackComponent* Playback = PC->FindComponentByClass<UVoicePlaybackComponent>();

	// 없을 수 있다. 무전을 한 번도 못 받았으면 아직 안 만들어졌다
	// (UVoicePlaybackComponent::FindOrCreate 는 첫 프레임에서야 불린다).
	return Playback != nullptr && Playback->IsReceivingRadio();
}

void UVoiceSubsystem::RequestVoiceDead(bool bDead)
{
	UVoiceComponent* Voice = GetVoiceComponent();

	if (!Voice)
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("음성 컴포넌트를 찾지 못해 사망 상태를 바꿀 수 없다."));
		return;
	}

	// 서버가 확정하고 복제해 돌아온다. 여기서 직접 bVoiceDead 를 건드리지 않는 이유는
	// **클라가 스스로 정한 상태와 서버가 아는 상태가 어긋나면** 안 되기 때문이다.
	// (호스트에서는 이 호출이 그 자리에서 실행되므로 즉시 반영된다)
	Voice->ServerSetVoiceDead(bDead);

	UE_LOG(LogMOUVoice, Log, TEXT("서버에 음성 %s 상태를 요청했다."),
		bDead ? TEXT("사망") : TEXT("생존"));
}

bool UVoiceSubsystem::IsCodecReady() const
{
	return Encoder.IsValid() && Encoder->IsReady()
		&& Decoder.IsValid() && Decoder->IsReady();
}

float UVoiceSubsystem::GetCurrentLoudness() const
{
	return CaptureSource.IsValid() ? CaptureSource->GetCurrentLoudness() : 0.f;
}

float UVoiceSubsystem::GetLoudnessEnvelope() const
{
	return CaptureSource.IsValid() ? CaptureSource->GetLoudnessEnvelope() : 0.f;
}

float UVoiceSubsystem::GetCurrentIntensity() const
{
	// ★ 정규화가 일어나는 곳은 여기 하나다(전송 경로는 프레임별 엔벨로프를
	//   써야 해서 예외지만, 부르는 함수는 같다). 지름길을 만들면 화면의 원과
	//   남에게 들리는 거리가 어긋난다.
	return MOUVoice::NormalizeLoudness(GetLoudnessEnvelope(), GetMicSensitivity());
}

float UVoiceSubsystem::GetCurrentRadiusScale() const
{
	return MOUVoice::GetRadiusScaleFromNormalized(GetCurrentIntensity());
}

void UVoiceSubsystem::SetMicSensitivity(float InThreshold)
{
	if (!CaptureSource.IsValid())
	{
		return;
	}

	CaptureSource->SetVadThreshold(InThreshold);

	// ★ 감도를 바꾸면 음량->반경 조절 구간의 **시작점**이 같이 움직인다.
	//   상한이 그 아래로 내려가면 조절 구간이 사라져 모든 발화가 최대 반경이
	//   된다 - 조용히 죽는 종류의 버그라 여기서 막는다(VoiceTypes.h 의
	//   MinLoudnessSpan 주석). 감도를 바꾸는 경로는 자동 보정과 이 함수뿐이고,
	//   자동 보정도 결국 여기를 지나므로 한 곳에서 막으면 충분하다.
	const float Applied = CaptureSource->GetVadThreshold();

	if (MOUVoice::EnsureUsableLoudnessSpan(Applied))
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("★ 감도(%.4f)가 음량 상한에 너무 가까워 반경 조절 구간이 없어질 뻔했다. ")
			TEXT("상한을 %.4f 로 올렸다. 속삭임/외침의 반경 차이가 좁게 느껴지면 ")
			TEXT("MOU.Voice.LoudnessCurve 로 상한을 더 올릴 것."),
			Applied, MOUVoice::LoudnessCeiling);
	}

	UE_LOG(LogMOUVoice, Log,
		TEXT("마이크 감도(VAD 임계값) = %.4f (조절 구간 %.4f ~ %.4f)"),
		Applied, Applied, MOUVoice::LoudnessCeiling);
}

float UVoiceSubsystem::GetMicSensitivity() const
{
	return CaptureSource.IsValid() ? CaptureSource->GetVadThreshold() : MOUVoice::DefaultVadThreshold;
}

// ---------------------------------------------------------------------------
// 감도 자동 보정
//
// 왜 필요한지는 VoiceTypes.h 의 보정 상수 주석에 있다. 요약하면:
// 기본 임계값 0.02 는 **어떤 마이크에도 맞지 않는 임의의 숫자**이고,
// 아날로그 3.5mm 입력에서는 조용할 때도 그 값을 넘겨 "항상 말하는 중" 이 된다.
// ---------------------------------------------------------------------------

// 헤더의 기본 인자가 리터럴이라 상수와 조용히 갈라질 수 있다. 여기서 못박는다.
static_assert(MOUVoice::DefaultCalibrationSeconds == 2.0f,
	"BeginSensitivityCalibration 의 기본 인자(2.0f)와 DefaultCalibrationSeconds 가 어긋났다. "
	"VoiceSubsystem.h 의 기본값도 같이 고칠 것.");

void UVoiceSubsystem::BeginSensitivityCalibration(float DurationSeconds)
{
	if (!IsCaptureReady())
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("마이크가 없어 보정할 수 없다. MOU.Voice.Reopen 으로 다시 열어보거나 ")
			TEXT("MOU.Voice.Diag 로 진단할 것."));
		return;
	}

	if (bMuted)
	{
		// 음소거 중에는 Tick 이 프레임을 버리므로 측정값이 0 만 나온다.
		// 그대로 두면 "기준이 최소값으로 떨어져서 모든 잡음에 반응하는" 상태가 된다.
		UE_LOG(LogMOUVoice, Warning,
			TEXT("음소거 상태에서는 보정할 수 없다. %s 로 음소거를 풀고 다시 시도할 것."),
			TEXT("C"));
		return;
	}

	const float Duration = FMath::Max(0.5f, DurationSeconds);

	bCalibrating       = true;
	CalibrationEndTime = FPlatformTime::Seconds() + Duration;
	CalibrationPeak    = 0.f;
	CalibrationSamples = 0;

	UE_LOG(LogMOUVoice, Log,
		TEXT("감도 보정 시작 (%.1f초). ★ 지금부터 **말하지 마세요** - 평소 환경의 잡음만 재는 중입니다."),
		Duration);
}

float UVoiceSubsystem::GetCalibrationRemainingSeconds() const
{
	if (!bCalibrating)
	{
		return 0.f;
	}

	return FMath::Max(0.f, static_cast<float>(CalibrationEndTime - FPlatformTime::Seconds()));
}

void UVoiceSubsystem::FinishCalibration()
{
	bCalibrating = false;

	if (CalibrationSamples <= 0)
	{
		// 측정 구간에 프레임이 한 개도 안 왔다. 마이크가 중간에 빠졌거나
		// 엔진 캡처가 죽은 것이다. 기준을 건드리지 않는 편이 안전하다 -
		// 0 을 기준으로 잡으면 모든 잡음이 발화가 된다.
		UE_LOG(LogMOUVoice, Warning,
			TEXT("보정 실패: 측정 구간에 마이크 데이터가 오지 않았다. 감도를 바꾸지 않는다."));
		return;
	}

	const float NewThreshold = FMath::Clamp(
		CalibrationPeak * MOUVoice::CalibrationMargin,
		MOUVoice::MinVadThreshold,
		1.f);

	const float OldThreshold = GetMicSensitivity();
	SetMicSensitivity(NewThreshold);

	UE_LOG(LogMOUVoice, Log,
		TEXT("감도 보정 완료. 잡음 최대=%.4f (%d프레임) -> 기준 %.4f (이전 %.4f)"),
		CalibrationPeak, CalibrationSamples, NewThreshold, OldThreshold);

	// ★ 소프트웨어로 덮을 수 없는 상황은 그렇다고 말해준다.
	//   기준을 올려서 조용할 때 조용하게는 만들 수 있지만, 잡음 바닥이 이 정도면
	//   **작게 말하는 소리가 잡음에 묻혀서** 속삭임 모드가 사실상 못 쓰게 된다.
	if (CalibrationPeak >= MOUVoice::NoisyMicWarnThreshold)
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("★ 잡음 바닥이 높다(%.4f). 기준을 올려두긴 했지만 작게 말하면 안 잡힐 수 있다. ")
			TEXT("Windows 소리 설정 > 입력 장치 > 속성에서 **마이크 부스트(+20/+30dB)를 끄면** ")
			TEXT("훨씬 나아진다. 3.5mm 아날로그 입력에서 흔한 문제다."),
			CalibrationPeak);
	}
}

// ---------------------------------------------------------------------------
// 마이크 다시 열기
// ---------------------------------------------------------------------------

bool UVoiceSubsystem::ReopenCapture()
{
	// 다른 창이 마이크를 잡고 있으면 뺏지 않는다. 뺏으면 그 창이 조용히 죽는다.
	if (GVoiceCaptureOwner.IsValid() && GVoiceCaptureOwner.Get() != this)
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("같은 프로세스의 다른 창이 마이크를 쓰고 있어 다시 열 수 없다."));
		return false;
	}

	if (!FVoiceModule::IsAvailable() || !FVoiceModule::Get().IsVoiceEnabled())
	{
		UE_LOG(LogMOUVoice, Error,
			TEXT("Voice 모듈이 비활성 상태다. MOU.Voice.Diag 로 진단할 것."));
		return false;
	}

	if (!CaptureSource)
	{
		// 이 창은 시작할 때 캡처를 안 잡았다(다른 창이 먼저였거나 모듈이 늦게 올라옴).
		// 지금은 비었으므로 이 창이 가져간다.
		CaptureSource = MakeUnique<FVoiceCaptureSource>();
	}
	else
	{
		// ★ 반드시 먼저 닫는다. 안 닫고 Start() 를 부르면 이미 열려 있다고 판단해
		//   **그대로 반환한다**(FVoiceCaptureSource::Start 의 bReady 조기 반환).
		//   그러면 "다시 열었는데 왜 그대로지" 가 된다 - 새로 꽂은 장치를 잡으려면
		//   엔진 캡처 객체를 버리고 새로 만들어야 한다.
		CaptureSource->Shutdown();
	}

	GVoiceCaptureOwner = this;

	const bool bOpened = CaptureSource->Start();

	// 코덱이 아직 없으면 지금 만든다(이 창이 처음 캡처를 잡는 경우).
	if (!Encoder)
	{
		Encoder = MakeUnique<FMOUVoiceEncoder>();
		Encoder->Initialize();
	}
	if (!Decoder)
	{
		Decoder = MakeUnique<FMOUVoiceDecoder>();
		Decoder->Initialize();
	}

	if (bOpened)
	{
		UE_LOG(LogMOUVoice, Log,
			TEXT("마이크를 다시 열었다. 이어서 MOU.Voice.Calibrate 로 감도를 맞출 것 - ")
			TEXT("장치마다 잡음 바닥이 크게 달라서 기본값이 맞는 경우가 오히려 드물다."));
	}
	else
	{
		// ★ 여기서 가장 흔한 오해를 미리 잡아준다(헤더 주석의 장치 목록 캐시 문제).
		UE_LOG(LogMOUVoice, Warning,
			TEXT("마이크를 다시 열지 못했다.\n")
			TEXT("  · **에디터를 켠 뒤에 헤드셋을 꽂았다면 이 명령으로는 안 된다.** ")
			TEXT("엔진이 장치 목록을 시작할 때 한 번만 읽어서 캐시하기 때문에 ")
			TEXT("에디터를 재시작해야 한다.\n")
			TEXT("  · 장치가 원래 꽂혀 있었다면 Windows 소리 설정에서 입력 장치가 ")
			TEXT("선택돼 있는지 확인하고 MOU.Voice.Diag 를 볼 것."));
	}

	return bOpened;
}

FString UVoiceSubsystem::GetStatsString() const
{
	const int32 Buffered = PlaybackComponent ? PlaybackComponent->GetBufferedSampleCount() : 0;
	const int32 Underrun = PlaybackComponent ? PlaybackComponent->GetUnderrunCount() : 0;
	const int32 Overflow = PlaybackComponent ? PlaybackComponent->GetOverflowCount() : 0;

	// --- 코덱 통계 ----------------------------------------------------------
	//
	// 평균 프레임 크기가 12절의 대역폭 계산을 실측으로 검증하는 값이다.
	// 설계는 프레임당 ~60바이트(= 초당 50프레임 x 60 = 3KB/s)를 전제로 했다.
	// 여기 숫자가 그와 크게 다르면 12절의 대역폭 표를 다시 계산해야 한다.
	FString CodecStats;
	if (Encoder.IsValid() && Encoder->GetFrameCount() > 0)
	{
		const float AvgBytes = Encoder->GetAverageFrameBytes();

		// 초당 프레임 수는 고정(1000/FrameMs = 50)이므로 초당 바이트를 바로 낸다.
		const float BytesPerSec = AvgBytes * (1000.f / MOUVoice::FrameMs);

		CodecStats = FString::Printf(
			TEXT(" | 코덱=%s 인코딩=%d프레임 평균%.1f바이트(최대%d) 압축률%.1f:1 %.1fKB/s 디코딩=%d실패=%d"),
			bCodecEnabled ? TEXT("ON") : TEXT("OFF"),
			Encoder->GetFrameCount(),
			AvgBytes,
			Encoder->GetMaxFrameBytes(),
			AvgBytes > 0.f ? MOUVoice::BytesPerFrame / AvgBytes : 0.f,
			BytesPerSec / 1024.f,
			Decoder.IsValid() ? Decoder->GetFrameCount() : 0,
			Decoder.IsValid() ? Decoder->GetFailureCount() : 0);
	}
	else
	{
		CodecStats = FString::Printf(TEXT(" | 코덱=%s (아직 인코딩한 프레임 없음)"),
			bCodecEnabled ? TEXT("ON") : TEXT("OFF"));
	}

	// --- V3 네트워크 통계 ---------------------------------------------------
	//
	// 세 줄로 나눠 보여주는 이유: "안 들린다" 의 원인이 송신/서버/수신 중
	// 어디인지가 이 숫자들의 조합으로 바로 갈린다.
	//   전송=0        -> 마이크나 코덱 문제 (또는 VoiceComponent 가 없다)
	//   전송>0 라우팅=0 -> 서버까지 안 갔다 (RPC 문제)
	//   라우팅>0 전달=0 -> 서버가 수신자를 못 찾았다 (거리, 폰 없음)
	//   전달>0 재생=0   -> 받는 쪽 문제 (디코딩, 발신자 폰 못 찾음)
	FString NetStats = FString::Printf(TEXT(" | 전송=%d"), FramesTransmitted);

	const UWorld* World = GetLocalPlayer() ? GetLocalPlayer()->GetWorld() : nullptr;

	// ★ 라우터 통계는 **서버일 때만** 보여준다.
	//
	//   월드 서브시스템이라 클라이언트에도 객체는 생긴다. 그런데 클라의 라우터는
	//   한 번도 안 불리므로 항상 0 이다. 그걸 그대로 찍으면 "라우팅=0" 을 보고
	//   **서버가 일을 안 한다고 오진하게 된다** - 실제로는 내 화면에 남의 서버
	//   상태가 보일 리 없는 것뿐인데. 아예 안 보여주는 편이 덜 헷갈린다.
	if (World && World->GetNetMode() != NM_Client)
	{
		if (const UVoiceRouter* Router = UVoiceRouter::Get(World))
		{
			NetStats += FString::Printf(TEXT(" | [서버] %s"), *Router->GetStatsString());
		}
	}

	if (const APlayerController* PC = GetOwningPlayerController())
	{
		if (const UVoicePlaybackComponent* Playback = PC->FindComponentByClass<UVoicePlaybackComponent>())
		{
			NetStats += FString::Printf(TEXT(" | [수신] %s"), *Playback->GetStatsString());
		}
	}

	// ★ 반경은 이제 모드 상한이 아니라 **음량이 곱해진 실제 값**을 찍는다.
	//   상한만 보여주면 "표에는 15m 인데 실제로는 8m 에서 안 들린다" 를
	//   설명할 수 없어서, 아날로그 반경을 튜닝하는 동안 이 줄이 거짓말을 한다.
	//   괄호 안이 모드 상한, 앞이 지금 실제로 나가는 거리다.
	const float Intensity = GetCurrentIntensity();

	return FString::Printf(
		TEXT("마이크=%s 음소거=%s 루프백=%s 발화=%s 모드=%s ")
		TEXT("| 들림%.0f/NPC%.0f(상한%.0f/%.0f) 배율=%.2f 강도=%.2f ")
		TEXT("| 감도=%.4f 음량=%.4f 엔벨=%.4f ")
		TEXT("| 수신프레임=%d 버림=%d ")
		TEXT("| 재생버퍼=%d샘플(%.0fms) 언더런=%d 오버플로=%d%s%s"),
		IsCaptureReady() ? TEXT("준비됨") : TEXT("없음"),
		bMuted ? TEXT("ON") : TEXT("OFF"),
		bLoopbackEnabled ? TEXT("ON") : TEXT("OFF"),
		bIsSpeaking ? TEXT("O") : TEXT("X"),
		MOUVoice::GetVoiceModeName(VoiceMode),
		MOUVoice::GetScaledHearRadius(VoiceMode, Intensity),
		MOUVoice::GetScaledNoiseRange(VoiceMode, Intensity),
		MOUVoice::GetHearRadius(VoiceMode),
		MOUVoice::GetNoiseRange(VoiceMode),
		MOUVoice::GetRadiusScaleFromNormalized(Intensity),
		Intensity,
		GetMicSensitivity(),
		GetCurrentLoudness(),
		GetLoudnessEnvelope(),
		FramesReceived,
		FramesDropped,
		Buffered,
		Buffered * 1000.f / MOUVoice::SampleRate,
		Underrun,
		Overflow,
		*CodecStats,
		*NetStats);
}

