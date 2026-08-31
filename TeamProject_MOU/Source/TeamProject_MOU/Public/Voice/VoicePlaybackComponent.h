// MOU 음성 - 수신한 목소리를 재생하는 컴포넌트.
//
// [이 파일이 시스템 어디에 있나]
//
//     서버 (VoiceRouter)
//       │ ClientReceiveVoiceFrame  ← Client, Unreliable RPC
//       ▼
//     UVoiceComponent (내 PlayerController)
//       ▼ HandleFrame()
//   ★ UVoicePlaybackComponent  ← 이 파일. 발신자마다 스트림 하나
//       │  스트림 = { Opus 디코더 + UVoiceSynthComponent }
//       ▼
//     발신자의 폰에 붙은 UVoiceSynthComponent (3D)
//
// [왜 발신자마다 따로 관리하는가 - 두 가지 이유가 다르다]
//
//   1. **디코더**: Opus 는 직전 프레임을 참고해 다음을 복원한다(상태를 가진다).
//      여러 사람 목소리를 디코더 하나로 돌려 쓰면 서로 상태를 오염시켜 지직거린다.
//
//   2. **사운드**: 소리가 나야 하는 **위치가 사람마다 다르다.** 근접 음성의
//      핵심이 "그 사람 위치에서 들린다" 이므로 발신자 폰마다 사운드가 필요하다.
//
//   둘 다 "발신자마다 하나" 라서 한 스트림 구조체로 묶었다.
//
// [스트림 키가 (발신자, 라우트) 쌍인 이유]
//   V6 에서 한 사람이 근접과 무전 **양쪽으로 동시에** 들릴 수 있다 - 육성은
//   가까운 사람에게, 무전은 무전기에서. 두 소리는 위치도 음색도 다르므로
//   각각 별도 스트림이어야 한다. 지금은 근접뿐이지만 키를 미리 쌍으로 두면
//   V6 에서 이 파일을 다시 안 건드린다.
//
// [현재 구현 단계 - V3]
//   지터버퍼가 아직 없다(V4). 받은 프레임을 바로 디코딩해 링버퍼로 밀어넣는다.
//   UVoiceSynthComponent 의 링버퍼(8프레임 = 160ms)가 약간의 흔들림은 흡수한다.
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 6절(클래스), 7-2절(근접 재생), 14절 V3

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Voice/VoiceJitterBuffer.h"
#include "Voice/VoiceTypes.h"
#include "VoicePlaybackComponent.generated.h"

class APawn;
class APlayerController;
class FMOUVoiceDecoder;
class UVoiceSynthComponent;

/**
 * 스트림 하나를 가리키는 키. (발신자, 라우트) 쌍이다.
 *
 * USTRUCT 가 아닌 이유: 네트워크로 나가지도, 에디터에 보이지도 않는
 * 순수 내부 자료구조다. 리플렉션을 붙이면 얻는 것 없이 제약만 는다.
 */
struct FVoiceStreamKey
{
	int32       SpeakerId = 0;
	EVoiceRoute Route     = EVoiceRoute::Proximity;

	bool operator==(const FVoiceStreamKey& Other) const
	{
		return SpeakerId == Other.SpeakerId && Route == Other.Route;
	}

	friend uint32 GetTypeHash(const FVoiceStreamKey& Key)
	{
		return HashCombine(::GetTypeHash(Key.SpeakerId), ::GetTypeHash(static_cast<uint8>(Key.Route)));
	}
};

/**
 * 발신자 한 명(정확히는 한 라우트)의 재생 상태.
 *
 * ★ Synth 를 TWeakObjectPtr 로 들고 있고 UPROPERTY 를 걸지 않은 이유:
 *   RegisterComponent() 를 부르는 순간 그 컴포넌트는 **소유 액터(발신자 폰)의
 *   컴포넌트 목록에 들어가서** 액터가 GC 로부터 지켜준다. 우리가 또 붙들면
 *   폰이 죽었는데도 컴포넌트만 살아남는 상황을 만들 수 있다.
 *
 *   폰이 파괴되면(사망/리스폰) 컴포넌트도 같이 사라지고 이 약참조가 풀린다.
 *   그러면 다음 프레임에서 새 폰에 다시 만든다 - **리스폰 처리가 공짜로 된다.**
 */
struct FVoiceStream
{
	/** 이 발신자 전용 Opus 디코더. */
	TUniquePtr<FMOUVoiceDecoder> Decoder;

	/** 발신자 폰에 붙인 사운드. 위 ★ 참고. */
	TWeakObjectPtr<UVoiceSynthComponent> Synth;

	/**
	 * 지금 Synth 가 붙어 있는 액터.
	 *
	 * 근접이면 발신자 폰, 무전이면 무전기 액터다. 이 값이 바뀌면(리스폰,
	 * 무전기를 떨어뜨림) 사운드를 새 액터에 다시 만든다.
	 */
	TWeakObjectPtr<AActor> AttachedActor;

	/**
	 * 무전 스트림일 때 소리를 낼 무전기 액터. 서버가 프레임마다 알려준다.
	 *
	 * ★ 매번 갱신하는 이유: **같은 스트림이라도 무전기가 바뀔 수 있다.**
	 *   들고 있던 무전기를 떨어뜨리고 다른 것을 주우면 소리가 나는 위치가 바뀐다.
	 */
	TWeakObjectPtr<AActor> RadioActor;

	/**
	 * 도착한 프레임을 번호 순서로 다시 세우는 버퍼(V4).
	 *
	 * 이게 생기면서 **"받으면 바로 재생" 이 "받으면 넣고, 따로 꺼내 재생" 으로**
	 * 바뀌었다. 도착(HandleFrame)과 재생(PumpStream)이 분리된 이유다.
	 */
	FVoiceJitterBuffer Jitter;

	/** 지터버퍼에서 꺼낸 압축 바이트를 받는 버퍼. */
	TArray<uint8> PopScratch;

	/** 디코딩 결과를 받는 버퍼. 스트림마다 들고 있어 매 프레임 할당하지 않는다. */
	TArray<int16> DecodedScratch;

	/**
	 * 마지막으로 정상 디코딩한 PCM. **유실 은폐(PLC)의 재료다.**
	 *
	 * 엔진 Opus 래퍼가 진짜 PLC 를 안 열어주기 때문에(VoiceTypes.h 참고)
	 * 직전 소리를 감쇠시켜 반복하는 방식으로 메운다. 그러려면 직전 프레임을
	 * 들고 있어야 한다.
	 */
	TArray<int16> LastPcm;

	/** 연속 은폐 횟수. 너무 길어지면 무음으로 전환한다. */
	int32 ConsecutiveConceals = 0;

	/** 마지막으로 받은 발화 모드. 재생 시점에 감쇠 반경을 정하는 데 쓴다. */
	EVoiceMode LastMode = EVoiceMode::Normal;

	/**
	 * 마지막으로 받은 반경 배율(MinRadiusScale~1.0). ★ 근접에서만 쓴다.
	 *
	 * 서버가 실어 보낸 Frame.Loudness(정규화된 발화 강도)를
	 * MOUVoice::GetRadiusScaleFromNormalized 에 넣은 값을 그대로 저장해 둔다 -
	 * 재생 시점(PumpStream)마다 다시 계산할 필요가 없다. 무전 라우트는
	 * 반경이 무전기 속성에서 오므로(SetRadioMode) 이 값을 쓰지 않는다.
	 */
	float LastRadiusScale = 1.f;

	/** 이 스트림의 라우트. 소리를 어디에 붙일지와 어떤 톤으로 낼지를 정한다. */
	EVoiceRoute Route = EVoiceRoute::Proximity;

	/** 마지막으로 프레임이 온 시각. 오래 조용하면 스트림을 정리한다. */
	double LastFrameTime = 0.0;

	/** 진단용. */
	int32 FramesPlayed    = 0;
	int32 FramesConcealed = 0;
};

/**
 * 남의 목소리를 재생한다. **내 PlayerController 에만 있으면 된다.**
 *
 * UVoiceComponent 가 RPC 를 받아 이리로 넘긴다. 이 컴포넌트는 네트워크를
 * 전혀 모른다 - "누가, 어떤 소리를 냈다" 만 받아서 들려주는 것이 전부다.
 */
UCLASS(ClassGroup = (MOU), meta = (BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UVoicePlaybackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoicePlaybackComponent();

	/**
	 * 주어진 PlayerController 의 재생 컴포넌트를 얻는다. 없으면 만든다.
	 *
	 * **로컬 컨트롤러가 아니면 null 을 돌려준다** - 남의 컨트롤러에 재생
	 * 컴포넌트를 만들면 서버에서 들리지도 않는 소리를 위해 디코딩을 하게 된다.
	 */
	static UVoicePlaybackComponent* FindOrCreate(APlayerController* OwnerPC);

	/** 서버가 보낸 프레임 하나를 재생한다. 게임 스레드에서만 부른다. */
	void HandleFrame(const FVoiceFrameOut& Frame);

	/** 전부 정리한다(레벨 이동 등). */
	void ResetAllStreams();

	/** 지금 살아있는 스트림 수. */
	int32 GetStreamCount() const { return Streams.Num(); }

	/**
	 * 지금 무전을 받고 있는가. **UI 의 "수신 중" 표시가 이걸 쓴다.**
	 *
	 * [왜 여기에 있는가]
	 *   "무전이 들어오는 중" 을 아는 곳은 이 컴포넌트뿐이다. 서버는 프레임을
	 *   쏘고 잊고, UVoiceSubsystem 은 **내가 보내는 것**(IsRadioTransmitting)만
	 *   알지 받는 것은 모른다. 그래서 이 함수가 없으면 무전기 아이콘 4개 중
	 *   수신 하나를 그릴 방법이 아예 없었다.
	 *
	 * [판정 기준]
	 *   Route == Radio 인 스트림에 **최근 프레임이 도착했는가**. 재생 여부가
	 *   아니라 도착 여부인 것이 의도적이다 - 지터버퍼 지연만큼 늦게 켜지면
	 *   소리가 이미 나는데 아이콘은 아직 꺼져 있게 된다.
	 *
	 *   스트림이 살아있는 것만으로는 안 된다. 스트림은 말이 끝나도
	 *   VoiceStreamIdleTimeoutSeconds(3초)까지 남아 있어서, 그걸로 판정하면
	 *   **아무도 말하지 않는데 3초 동안 수신 중이라고 뜬다.**
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsReceivingRadio() const;

	/** 진단용 한 줄 요약. MOU.Voice.Stat 이 쓴다. */
	FString GetStatsString() const;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** SpeakerId 로 발신자의 폰을 찾는다. 못 찾으면 null(아직 리플리케이트 전일 수 있다). */
	APawn* FindSpeakerPawn(int32 SpeakerId) const;

	/**
	 * 이 스트림의 소리가 나야 할 액터를 정한다.
	 *
	 *   근접 → 발신자의 폰
	 *   무전 → **듣는 쪽 근처의 무전기**(서버가 프레임에 실어 보낸 액터)
	 *
	 * 무전 소리가 발신자가 아니라 무전기에서 나는 것이 이 설계의 핵심이다(7-4절).
	 */
	AActor* ResolveSpeakerActor(const FVoiceStream& Stream, int32 SpeakerId) const;

	/**
	 * 스트림의 사운드가 올바른 액터에 붙어 있도록 보장한다.
	 * 없거나 대상이 바뀌었으면(리스폰, 무전기 교체) 새로 만든다.
	 *
	 * @return 쓸 수 있는 사운드. 대상을 못 찾으면 null.
	 */
	UVoiceSynthComponent* EnsureSynthForStream(FVoiceStream& Stream, int32 SpeakerId, EVoiceMode Mode);

	/**
	 * 지터버퍼에서 꺼내 재생 링버퍼를 목표 깊이까지 채운다.
	 *
	 * ★ **재생 속도를 정하는 것은 이 함수가 아니라 오디오 스레드다.**
	 *   오디오 스레드가 링버퍼를 실시간 속도로 비우므로, 우리는 "덜 찼으면
	 *   채운다" 만 하면 자연히 20ms 간격이 맞는다. 여기서 시간을 재서 직접
	 *   박자를 맞추려 하면 게임 틱 주기(가변)와 오디오 주기(고정)가 어긋나
	 *   조금씩 밀리거나 쌓인다.
	 */
	void PumpStream(FVoiceStream& Stream, int32 SpeakerId);

	/** 모든 스트림을 펌프한다. 매 틱 호출된다. */
	void PumpAllStreams();

	/**
	 * 유실 구간을 메울 PCM 을 만든다(PLC).
	 *
	 * 직전 프레임을 감쇠시켜 반복한다. 엔진 Opus 래퍼가 진짜 PLC 를 안 열어주기
	 * 때문이다 - 자세한 것은 VoiceTypes.h 의 MaxConcealFrames 주석.
	 */
	void BuildConcealmentFrame(FVoiceStream& Stream);

	/** 오래 조용한 스트림을 정리한다. */
	void CleanupIdleStreams();

	/** 발신자(+라우트)마다 하나. */
	TMap<FVoiceStreamKey, FVoiceStream> Streams;

	/**
	 * 정리 작업 주기 조절용 누적 시간.
	 *
	 * 펌프는 매 틱 돌아야 하지만(안 그러면 재생이 끊긴다) 유휴 스트림 정리는
	 * 그럴 이유가 없다. 틱 간격을 늦추는 대신 여기서 따로 세어 분리한다.
	 */
	float TimeSinceCleanup = 0.f;

	/** 진단 카운터. */
	int32 TotalFramesReceived = 0;
	int32 TotalFramesDropped  = 0;
	int32 TotalPawnMisses     = 0;
};
