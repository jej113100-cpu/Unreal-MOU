// MOU 음성 - 수신 재생 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7-2절, 14절 V3
//
// [스레드] 전부 게임 스레드다. 오디오 렌더 스레드로 넘어가는 경계는
//          UVoiceSynthComponent 의 링버퍼 하나뿐이다(11절).

#include "Voice/VoicePlaybackComponent.h"

#include "Voice/RadioComponent.h"
#include "Voice/VoiceCodec.h"
#include "Voice/VoiceSynthComponent.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

UVoicePlaybackComponent::UVoicePlaybackComponent()
{
	// ★ V4 부터는 **매 프레임** 돌아야 한다.
	//
	//   지터버퍼가 생기기 전에는 프레임을 받는 즉시 재생해서 틱이 느려도 됐다.
	//   지금은 재생 링버퍼가 비어가는 것을 보고 채워 넣는 구조라, 틱이 느리면
	//   그만큼 버퍼가 마르고 **소리가 끊긴다.** 0.5초 간격이면 재생이 아예 안 된다.
	//
	//   대신 유휴 스트림 정리는 매 프레임 할 이유가 없으므로 TimeSinceCleanup 으로
	//   따로 늦춘다.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.f;
}

UVoicePlaybackComponent* UVoicePlaybackComponent::FindOrCreate(APlayerController* OwnerPC)
{
	if (!IsValid(OwnerPC))
	{
		return nullptr;
	}

	// ★ 로컬 컨트롤러가 아니면 만들지 않는다.
	//
	//   리슨서버에서 이걸 빼먹으면 호스트가 **접속한 모든 플레이어의 컨트롤러마다**
	//   재생 컴포넌트를 만들게 된다. 아무도 못 듣는 소리를 위해 사람 수만큼
	//   디코딩과 사운드를 돌리는 셈이라, 인원이 늘수록 호스트만 느려진다.
	if (!OwnerPC->IsLocalController())
	{
		return nullptr;
	}

	if (UVoicePlaybackComponent* Existing = OwnerPC->FindComponentByClass<UVoicePlaybackComponent>())
	{
		return Existing;
	}

	UVoicePlaybackComponent* Created = NewObject<UVoicePlaybackComponent>(OwnerPC, TEXT("MOUVoicePlaybackRemote"));
	Created->RegisterComponent();

	UE_LOG(LogMOUVoice, Log, TEXT("수신 재생 컴포넌트 생성 완료."));
	return Created;
}

void UVoicePlaybackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetAllStreams();
	Super::EndPlay(EndPlayReason);
}

void UVoicePlaybackComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 재생은 매 프레임. 늦으면 그만큼 소리가 끊긴다(생성자 주석).
	PumpAllStreams();

	// 정리는 느긋해도 된다.
	TimeSinceCleanup += DeltaTime;
	if (TimeSinceCleanup >= 0.5f)
	{
		TimeSinceCleanup = 0.f;
		CleanupIdleStreams();
	}
}

// ---------------------------------------------------------------------------
// 프레임 처리
// ---------------------------------------------------------------------------

void UVoicePlaybackComponent::HandleFrame(const FVoiceFrameOut& Frame)
{
	++TotalFramesReceived;

	if (Frame.Opus.Num() <= 0)
	{
		return;
	}

	// 서버가 확정해 보낸 값이지만 한 번 더 자른다.
	// 서버가 우리 편이라는 가정은 맞지만, 범위 밖 enum 이 흘러들어오면
	// 나중에 이 값으로 배열을 인덱싱하는 코드가 생겼을 때 터진다.
	const EVoiceRoute Route = MOUVoice::SanitizeRoute(Frame.Route);
	const EVoiceMode  Mode = MOUVoice::SanitizeMode(Frame.Mode);

	const FVoiceStreamKey Key{ Frame.SpeakerId, Route };
	FVoiceStream& Stream = Streams.FindOrAdd(Key);

	const double Now = FPlatformTime::Seconds();

	// --- 디코더 준비 --------------------------------------------------------
	if (!Stream.Decoder.IsValid())
	{
		Stream.Decoder = MakeUnique<FMOUVoiceDecoder>();
		if (!Stream.Decoder->Initialize())
		{
			// 코덱을 못 만들면 이 발신자의 소리는 못 낸다. 스트림을 지워
			// 다음 프레임에서 다시 시도하게 둔다(일시적 문제일 수 있다).
			Streams.Remove(Key);
			return;
		}
	}
	// 오래 끊겼다 다시 오면 새 발화다. 디코더의 예측 상태와 버퍼를 함께 비운다.
	//
	// 지터버퍼도 같이 리셋하는 이유: 발화 사이의 공백은 유실이 아니다. 안 비우면
	// 새 발화의 첫 프레임이 옛 번호와 이어져 **그 사이를 전부 유실로 보고**
	// 은폐 프레임을 잔뜩 쏟아낸다.
	else if ((Now - Stream.LastFrameTime) > MOUVoice::VoiceUtteranceGapSeconds)
	{
		Stream.Decoder->Reset();
		Stream.Jitter.Reset();
		Stream.LastPcm.Reset();
		Stream.ConsecutiveConceals = 0;
	}

	Stream.LastFrameTime = Now;
	Stream.LastMode = Mode;
	Stream.Route = Route;

	// ★ 서버가 확정한 정규화 강도를 반경 배율로 바꿔 캐시해 둔다.
	//   PumpStream 은 이 값이 나온 것과 **같은 함수**(GetRadiusScaleFromNormalized)를
	//   거쳐야 화면(디버그 링)·NPC 반경과 어긋나지 않는다(VoiceTypes.h).
	Stream.LastRadiusScale = MOUVoice::GetRadiusScaleFromNormalized(
		MOUVoice::DequantizeLoudness(Frame.Loudness));

	// 무전이면 소리를 낼 무전기가 프레임에 실려 온다.
	// 매번 갱신한다 - 같은 사람이라도 무전기를 바꿔 들 수 있다.
	Stream.RadioActor = (Route == EVoiceRoute::Radio) ? Frame.RadioActor.Get() : nullptr;

	// --- 지터버퍼에 넣는다 --------------------------------------------------
	//
	// ★ 여기서 재생하지 않는다. 도착 간격은 들쭉날쭉하므로 그 박자에 맞춰
	//   재생하면 그대로 끊긴다. 재생은 PumpStream 이 일정한 속도로 한다.
	//   **이 분리가 V4 의 전부다.**
	if (!Stream.Jitter.Push(Frame.Seq, Frame.Opus))
	{
		// 너무 늦게 왔거나 중복이다. 자세한 사유는 지터버퍼 통계에 남는다.
		++TotalFramesDropped;
	}
}

// ---------------------------------------------------------------------------
// 재생 펌프 - 지터버퍼에서 꺼내 링버퍼를 채운다
// ---------------------------------------------------------------------------

void UVoicePlaybackComponent::PumpAllStreams()
{
	for (TPair<FVoiceStreamKey, FVoiceStream>& Pair : Streams)
	{
		PumpStream(Pair.Value, Pair.Key.SpeakerId);
	}
}

void UVoicePlaybackComponent::PumpStream(FVoiceStream& Stream, int32 SpeakerId)
{
	if (!Stream.Decoder.IsValid())
	{
		return;
	}

	UVoiceSynthComponent* Synth = EnsureSynthForStream(Stream, SpeakerId, Stream.LastMode);

	if (!Synth)
	{
		// 발신자 폰이 아직 이 클라에 없다. 다음 틱에 다시 시도한다.
		return;
	}

	// 링버퍼를 목표 깊이까지만 채운다.
	//
	// ★ 이 상한이 곧 지연 상한이다. 가득 채우면 그만큼 늦게 들리고, 적게 채우면
	//   오디오 스레드가 먼저 비워서 끊긴다. 목표 깊이(3프레임=60ms)는 그 균형점이다.
	const int32 TargetSamples = MOUVoice::TargetJitterFrames * MOUVoice::SamplesPerFrame;

	// 한 틱에 무한정 돌지 않도록 상한을 둔다. 링버퍼가 어떤 이유로든 안 비면
	// 조건이 계속 참이 되어 게임이 멈출 수 있다.
	int32 SafetyCounter = MOUVoice::MaxJitterFrames + MOUVoice::TargetJitterFrames;

	while (Synth->GetBufferedSampleCount() < TargetSamples && SafetyCounter-- > 0)
	{
		const EVoiceJitterResult Result = Stream.Jitter.Pop(Stream.PopScratch);

		if (Result == EVoiceJitterResult::Starved)
		{
			// 아직 안 왔을 뿐이다. 여기서 은폐하면 멀쩡히 오고 있는 소리를 밀어낸다.
			break;
		}

		if (Result == EVoiceJitterResult::Frame)
		{
			if (!Stream.Decoder->Decode(Stream.PopScratch.GetData(), Stream.PopScratch.Num(), Stream.DecodedScratch))
			{
				++TotalFramesDropped;
				continue;
			}

			// 다음 유실 때 메울 재료로 남겨둔다.
			Stream.LastPcm = Stream.DecodedScratch;
			Stream.ConsecutiveConceals = 0;
			++Stream.FramesPlayed;
		}
		else // Conceal - 유실이 확정됐다
		{
			BuildConcealmentFrame(Stream);
			++Stream.FramesConcealed;
		}

		Synth->PushSamples(Stream.DecodedScratch.GetData(), Stream.DecodedScratch.Num());
	}
}

void UVoicePlaybackComponent::BuildConcealmentFrame(FVoiceStream& Stream)
{
	++Stream.ConsecutiveConceals;

	// 메울 재료가 없거나(발화 첫 프레임이 유실) 너무 오래 끌었으면 무음을 낸다.
	//
	// ★ 같은 소리를 계속 반복하면 사람 목소리가 아니라 기계음처럼 웅웅거린다.
	//   짧게 덮을 때만 도움이 되고, 길어지면 조용한 편이 덜 거슬린다.
	if (Stream.LastPcm.Num() == 0 || Stream.ConsecutiveConceals > MOUVoice::MaxConcealFrames)
	{
		Stream.DecodedScratch.Reset();
		Stream.DecodedScratch.SetNumZeroed(MOUVoice::SamplesPerFrame);
		return;
	}

	// 반복할수록 빠르게 잦아들게 한다. 갑자기 뚝 끊기는 것보다 자연스럽다.
	const float Gain = FMath::Pow(MOUVoice::ConcealFadePerFrame, static_cast<float>(Stream.ConsecutiveConceals));

	Stream.DecodedScratch.SetNumUninitialized(Stream.LastPcm.Num(), EAllowShrinking::No);

	for (int32 Index = 0; Index < Stream.LastPcm.Num(); ++Index)
	{
		Stream.DecodedScratch[Index] = static_cast<int16>(Stream.LastPcm[Index] * Gain);
	}
}

// ---------------------------------------------------------------------------
// 스트림 관리
// ---------------------------------------------------------------------------

APawn* UVoicePlaybackComponent::FindSpeakerPawn(int32 SpeakerId) const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;

	if (!GameState)
	{
		return nullptr;
	}

	// PlayerArray 는 리플리케이트되므로 클라에서도 채워져 있다.
	// 인원이 한 방에 몇 명뿐이라 선형 탐색으로 충분하다 - 20ms 마다 도는 것이
	// 부담이 되면 그때 TMap 캐시를 두면 되지만, 지금 넣으면 플레이어 입퇴장마다
	// 캐시를 무효화하는 코드가 더 늘어난다.
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (PlayerState && PlayerState->GetPlayerId() == SpeakerId)
		{
			return PlayerState->GetPawn();
		}
	}

	return nullptr;
}

AActor* UVoicePlaybackComponent::ResolveSpeakerActor(const FVoiceStream& Stream, int32 SpeakerId) const
{
	if (Stream.Route == EVoiceRoute::Radio)
	{
		// ★ 무전은 **발신자가 아니라 무전기**에서 소리가 난다.
		//   그래서 발신자 폰을 찾지 않는다 - 발신자는 맵 반대편에 있을 수도 있고,
		//   그 사람 폰은 이 클라에 리플리케이트조차 안 돼 있을 수 있다.
		return Stream.RadioActor.Get();
	}

	return FindSpeakerPawn(SpeakerId);
}

UVoiceSynthComponent* UVoicePlaybackComponent::EnsureSynthForStream(
	FVoiceStream& Stream, int32 SpeakerId, EVoiceMode Mode)
{
	AActor* SpeakerActor = ResolveSpeakerActor(Stream, SpeakerId);

	if (!SpeakerActor)
	{
		// 근접이면 발신자 폰이, 무전이면 무전기가 아직 이 클라에 없다.
		// 접속 직후처럼 아직 안 온 순간에만 잠깐 걸린다. 계속 오르면 문제다.
		++TotalPawnMisses;
		return nullptr;
	}

	UVoiceSynthComponent* Synth = Stream.Synth.Get();

	// 붙어 있던 액터가 바뀌었으면(사망 후 리스폰, 무전기 교체) 옛 사운드는
	// 엉뚱한 액터에 붙어 있다. 그대로 두면 **시체가 있던 자리나 놓고 온
	// 무전기에서 목소리가 난다.**
	if (Synth && Stream.AttachedActor.Get() != SpeakerActor)
	{
		Synth->Stop();
		Synth->DestroyComponent();
		Synth = nullptr;
		Stream.Synth.Reset();
	}

	if (!Synth)
	{
		USceneComponent* AttachTarget = SpeakerActor->GetRootComponent();

		if (!AttachTarget)
		{
			// 붙일 곳이 없으면 만들지 않는다.
			// 그냥 만들면 컴포넌트가 월드 원점에 놓여 **목소리가 맵 한가운데서 난다** -
			// "소리는 나는데 엉뚱한 데서 들린다" 는 원인 찾기 고약한 증상이다.
			++TotalPawnMisses;
			return nullptr;
		}

		Synth = NewObject<UVoiceSynthComponent>(SpeakerActor);

		// ★★ 순서가 중요하다. 공간화 설정을 Start() **전에** 해야 한다.
		//    공간화 여부는 사운드를 만드는 시점에 읽히기 때문에, 나중에 켜면
		//    그 사운드는 끝까지 2D 로 난다 - "소리는 나는데 방향이 없다" 가 된다.
		if (Stream.Route == EVoiceRoute::Radio)
		{
			// ★ 상수가 아니라 **그 무전기의 실효 반경**을 쓴다.
			//   인벤토리에 넣은 무전기는 반경이 줄어야 하는데(StowedRadiusScale),
			//   여기서 상수를 쓰면 서버는 덜 보내는데 클라 감쇠는 그대로라
			//   "닿는 사람은 줄었는데 들리는 크기는 똑같은" 어긋난 상태가 된다.
			//   bInHand 가 복제되므로 클라도 같은 값을 계산할 수 있다.
			//
			//   ※ 공간화 설정은 생성 시점에 한 번만 읽힌다(위 ★★). 송신 도중에
			//     무전기를 집어넣으면 그 스트림은 옛 반경으로 끝난다 - 한 번의
			//     송신 안에서 일어나는 일이라 무시할 수 있는 차이다.
			float RadioRadius = MOUVoice::DefaultSpeakerHearRadius;

			if (const URadioComponent* Radio = SpeakerActor->FindComponentByClass<URadioComponent>())
			{
				RadioRadius = Radio->GetEffectiveHearRadius();
			}

			Synth->SetRadioMode(RadioRadius);
		}
		else
		{
			Synth->SetProximityMode(Mode, Stream.LastRadiusScale);
		}

		// 등록 전에는 SetupAttachment, 등록 후에는 AttachToComponent 다.
		// 여기서는 아직 등록 전이다.
		Synth->SetupAttachment(AttachTarget);
		Synth->RegisterComponent();
		Synth->Start();

		Stream.Synth = Synth;
		Stream.AttachedActor = SpeakerActor;
	}
	else if (Stream.Route == EVoiceRoute::Proximity)
	{
		// 모드나 음량 배율이 바뀌었으면 감쇠만 갈아끼운다(둘 다 그대로면
		// SetProximityMode 안에서 즉시 반환한다 - RadiusScaleUpdateEpsilon 참고).
		// 무전은 반경이 무전기 속성이라 발화 모드/음량과 무관하다.
		Synth->SetProximityMode(Mode, Stream.LastRadiusScale);
	}

	return Synth;
}

void UVoicePlaybackComponent::CleanupIdleStreams()
{
	const double Now = FPlatformTime::Seconds();

	for (auto It = Streams.CreateIterator(); It; ++It)
	{
		FVoiceStream& Stream = It.Value();

		if ((Now - Stream.LastFrameTime) <= MOUVoice::VoiceStreamIdleTimeoutSeconds)
		{
			continue;
		}

		if (UVoiceSynthComponent* Synth = Stream.Synth.Get())
		{
			Synth->Stop();
			Synth->DestroyComponent();
		}

		It.RemoveCurrent();
	}
}

void UVoicePlaybackComponent::ResetAllStreams()
{
	for (TPair<FVoiceStreamKey, FVoiceStream>& Pair : Streams)
	{
		if (UVoiceSynthComponent* Synth = Pair.Value.Synth.Get())
		{
			Synth->Stop();
			Synth->DestroyComponent();
		}
	}

	Streams.Empty();
}

// ---------------------------------------------------------------------------
// 수신 여부 (UI 전용)
//
// ★ 여기서 상태를 따로 들고 있지 않는다. 스트림 맵이 이미 진실을 갖고 있어서,
//   플래그를 하나 더 두면 켜는 곳과 끄는 곳이 갈라져 어긋난다(스트림이 정리될
//   때 끄는 것을 빠뜨리면 수신 아이콘이 영영 켜진 채로 남는다).
//   스트림은 많아야 몇 개라 매번 훑어도 비용이 없다.
// ---------------------------------------------------------------------------

bool UVoicePlaybackComponent::IsReceivingRadio() const
{
	const double Now = FPlatformTime::Seconds();

	for (const TPair<FVoiceStreamKey, FVoiceStream>& Pair : Streams)
	{
		if (Pair.Key.Route != EVoiceRoute::Radio)
		{
			continue;
		}

		if ((Now - Pair.Value.LastFrameTime) <= MOUVoice::RadioReceiveHoldSeconds)
		{
			return true;
		}
	}

	return false;
}

FString UVoicePlaybackComponent::GetStatsString() const
{
	int32 Played = 0;
	int32 Concealed = 0;
	int32 Late = 0;
	int32 Duplicate = 0;
	int32 Resync = 0;
	int32 Starve = 0;
	int32 Pending = 0;

	for (const TPair<FVoiceStreamKey, FVoiceStream>& Pair : Streams)
	{
		const FVoiceStream& Stream = Pair.Value;

		Played += Stream.FramesPlayed;
		Concealed += Stream.FramesConcealed;

		Late += Stream.Jitter.GetLateCount();
		Duplicate += Stream.Jitter.GetDuplicateCount();
		Resync += Stream.Jitter.GetResyncCount();
		Starve += Stream.Jitter.GetStarveCount();
		Pending += Stream.Jitter.GetPendingCount();
	}

	// 지터 깊이를 ms 로도 보여준다. 프레임 수보다 "지금 몇 ms 늦게 듣고 있는지" 가
	// 직관적이고, 목표값(60ms)과 바로 비교된다.
	const float PendingMs = Pending * static_cast<float>(MOUVoice::FrameMs);

	return FString::Printf(
		TEXT("스트림=%d 수신=%d 재생=%d 버림=%d 폰없음=%d ")
		TEXT("| 지터깊이=%d(%.0fms) 은폐=%d 지각=%d 중복=%d 재동기=%d 끊김=%d"),
		Streams.Num(), TotalFramesReceived, Played, TotalFramesDropped, TotalPawnMisses,
		Pending, PendingMs, Concealed, Late, Duplicate, Resync, Starve);
}