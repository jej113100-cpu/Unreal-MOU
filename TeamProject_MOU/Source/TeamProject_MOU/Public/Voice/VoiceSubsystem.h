// MOU 음성 - 음성 시스템의 진입점.
//
// [팀원이 알아야 할 것 - 요약]
//   음성을 쓰려면 이 서브시스템만 알면 된다. 스레드나 오디오 버퍼는 볼 필요 없다.
//   콘솔에서:  MOU.Voice.Loopback 1   -> 내 목소리가 내 헤드폰으로 돌아온다
//              MOU.Voice.Codec 0      -> Opus 우회. 켰다 껐다 하며 음질 비교
//              MOU.Voice.Stat         -> 프레임/버퍼/드랍 수 + 압축률
//              MOU.Voice.FakeNoise 1500  -> 마이크 없이 NPC 소음만 발생
//              MOU.Voice.Mute 1       -> 마이크 음소거 (C 키와 동일)
//              MOU.Voice.ShowUI       -> 상태 표시 위젯 띄우기 (VoiceStatusWidget.h)
//
// [이 파일이 시스템 어디에 있나]
//
//     FVoiceCaptureSource (게임 스레드에서 직접 폴링. VoiceCaptureSource.h 참고)
//       ↓ Poll()
//   ★ UVoiceSubsystem (게임 스레드)  ← 이 파일. Tick 에서 폴링해 재생 버퍼로 넘김
//       ↓ FMOUVoiceEncoder     (V3 에서 여기 결과가 서버로 나간다)
//       ↓ FMOUVoiceDecoder     (V3 에서는 받는 쪽이 한다)
//       ↓ PushSamples
//     UVoiceSynthComponent (오디오 렌더 스레드)
//
// [왜 LocalPlayerSubsystem 인가]
//   마이크는 로컬 플레이어당 하나다. 채팅이 쓰는 GameInstanceSubsystem 으로 하면
//   PIE 다중 창에서 창마다 마이크를 열려고 해서 장치 경합이 난다.
//   (VOICE_INTEGRATION.md 6절 참고)
//
// [현재 구현 단계 - V2]
//   캡처 -> Opus 인코딩 -> Opus 디코딩 -> 로컬 재생까지.
//   네트워크 전송도, 무전기도 아직 없다.
//   이 단계의 목적은 "코덱을 통과시켜도 알아들을 만한가" 를 귀로 확인하는 것이다.
//   MOU.Voice.Codec 0/1 로 코덱을 우회해 A/B 비교할 수 있다.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "Voice/VoiceTypes.h"
#include "Voice/VoiceCaptureSource.h"
#include "Voice/VoiceCodec.h"
#include "VoiceSubsystem.generated.h"

class FVoiceCaptureSource;
class UVoiceComponent;
class UVoiceSynthComponent;

/**
 * 마이크 캡처와 재생을 소유한다.
 *
 * 역할:
 *   1. 마이크 캡처 객체(FVoiceCaptureSource)의 생성과 파괴
 *   2. 게임 스레드 Tick 에서 마이크를 폴링해 재생 버퍼로 넘김
 *   3. 콘솔 명령 / 블루프린트가 쓸 API 제공
 */
UCLASS()
class TEAMPROJECT_MOU_API UVoiceSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	// --- ULocalPlayerSubsystem --------------------------------------------
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 아무 데서나 서브시스템을 얻는 헬퍼. WorldContextObject 는 위젯/액터의 self. */
	UFUNCTION(BlueprintPure, Category = "MOU|Voice", meta = (WorldContext = "WorldContextObject"))
	static UVoiceSubsystem* Get(const UObject* WorldContextObject);

	/**
	 * 로컬 루프백을 켜고 끈다. 내 목소리가 내 헤드폰으로 즉시 돌아온다.
	 *
	 * V1 검증 전용이다. 네트워크 전송(V3)이 붙으면 이 경로는 디버깅용으로만 남는다.
	 * **헤드폰 없이 켜면 하울링이 난다.**
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void SetLoopbackEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsLoopbackEnabled() const { return bLoopbackEnabled; }

	/**
	 * Opus 코덱을 켜고 끈다(V2 검증용).
	 *
	 * 끄면 원본 PCM 이 그대로 재생 경로로 간다. 켜고 끄며 들어보면 **압축이
	 * 음질을 얼마나 깎는지**를 바로 비교할 수 있다 - "음질이 전화 수준인가" 라는
	 * V2 의 합격 기준을 귀로 확인하는 유일한 방법이다.
	 *
	 * V3 이후에는 네트워크로 나가는 이상 코덱이 필수이므로 이 스위치는
	 * 디버깅 전용으로만 남는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void SetCodecEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsCodecEnabled() const { return bCodecEnabled; }

	/**
	 * 인코더와 디코더가 둘 다 살아있는지.
	 *
	 * 마이크와 **별개로** 실패할 수 있다(모듈은 올라왔는데 코덱 생성만 실패 등).
	 * 그래서 MOU.Voice.Diag 가 마이크와 따로 찍어준다 - "마이크는 잡히는데
	 * 소리가 안 난다" 의 원인이 여기일 수 있기 때문이다.
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsCodecReady() const;

	/**
	 * 마이크 음소거를 켜고 끈다. `C` 키에 연결된다(VOICE_INTEGRATION.md 7-1절).
	 *
	 * 켜면 **캡처 자체를 중단한다**(마이크를 놓는다. VAD 도 안 돈다) - 설계 문서의
	 * 표현 그대로다. "볼륨을 낮추는" 것이 아니라 "말하지 않는" 상태로 만드는 것이다.
	 * 이렇게 해야 나중에 네트워크 전송(V3)이 붙었을 때도 음소거 중엔 아무것도
	 * 나가지 않는다는 게 자연스럽게 보장된다 - 별도로 "전송 안 함" 분기를 안 둬도 된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void SetMuted(bool bInMuted);

	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void ToggleMute() { SetMuted(!bMuted); }

	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsMuted() const { return bMuted; }

	/**
	 * 발화 모드(속삭임/보통/외침)를 바꾼다.
	 *
	 * 이 값이 들리는 거리와 NPC 가 듣는 거리를 동시에 결정한다.
	 * 실제 숫자는 MOUVoice::GetHearRadius / GetNoiseRange 한 곳에만 있다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void SetVoiceMode(EVoiceMode NewMode);

	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	EVoiceMode GetVoiceMode() const { return VoiceMode; }

	/**
	 * 말할 때 내 주위에 소리 도달 범위를 링으로 그린다(디버그 전용).
	 *
	 * 초록 = 사람이 듣는 거리, 빨강 = NPC 가 듣는 거리.
	 * 두 값 모두 V8 에서 실제 소음 이벤트가 쓸 값과 **같은 함수**에서 나오므로,
	 * 여기 보이는 원이 곧 실제 판정 범위다(어긋날 수 없다).
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void SetShowRadiusDebug(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsShowingRadiusDebug() const { return bShowRadiusDebug; }

	/** 마이크가 실제로 열렸는지. 없어도 게임은 정상 진행된다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsCaptureReady() const;

	/**
	 * 내가 지금 음성 사망 상태인가(말하기·듣기 모두 차단).
	 *
	 * 판정은 UVoiceComponent 가 들고 있다 - 서버가 정하고 복제해 오는 값이다.
	 * 여기서는 그걸 그대로 물어본다.
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsVoiceDead() const;

	/**
	 * 무전 송신(PTT)을 켜고 끈다. 설계상 `X` 키 홀드에 대응한다(V6).
	 *
	 * 켜져 있는 동안 내 목소리가 **근접과 무전 양쪽으로** 나간다 - 무전을 쳐도
	 * 입으로는 실제로 소리를 내고 있기 때문이다(2절). 근처 사람은 육성으로 듣고,
	 * 멀리 있는 사람은 무전기로 듣는다. 겹치지 않게 하는 것은 서버 몫이다.
	 *
	 * ★ 실제로 무전이 나가는지는 서버가 정한다 - 무전기를 들고 있는지,
	 *   켜져 있는지, 채널이 비었는지를 전부 서버가 다시 확인한다.
	 *   여기서 켰다고 나가는 것이 아니다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void SetRadioTransmitting(bool bTransmitting);

	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsRadioTransmitting() const { return bRadioTransmitting; }

	/**
	 * 사망 상태를 서버에 요청한다(테스트용, `MOU.Voice.Die` / `MOU.Voice.Revive`).
	 *
	 * ★ 나중에 실제 게임 로직(체력 0 -> 사망)과 엮이면 이 함수는 사라진다.
	 *   사망 판정은 서버가 하는 것이지 클라가 요청할 일이 아니다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void RequestVoiceDead(bool bDead);

	/**
	 * 지금 말하고 있는지 (VAD 판정). NPC 소음 발생 조건이자 UI 표시용이다.
	 * V8 에서 소음 이벤트의 입력이 된다.
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsSpeaking() const { return bIsSpeaking; }

	/**
	 * 마지막 프레임의 음량(0~1).
	 * 옵션 화면의 입력 게이지가 이 값을 그린다(V9).
	 */
	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	float GetCurrentLoudness() const;

	/**
	 * 마이크 감도(VAD 임계값)를 바꾼다. 낮출수록 작은 소리도 발화로 친다.
	 * 마이크 환경이 사람마다 달라 반드시 조절 수단이 필요하다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void SetMicSensitivity(float InThreshold);

	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	float GetMicSensitivity() const;

	/**
	 * 주변 소음을 측정해 마이크 감도를 자동으로 맞춘다.
	 *
	 * **측정하는 동안 말하지 말아야 한다.** 조용한 상태의 최대 음량을 재서
	 * 그보다 확실히 위에 기준을 놓는 방식이라, 말소리가 섞이면 기준이 너무
	 * 높아져서 이번엔 아무것도 안 잡히게 된다.
	 *
	 * 측정 중에는 **아무것도 전송하지 않는다** - 조용히 있으라고 해놓고 그
	 * 소리를 남에게 보내면 곤란하다.
	 *
	 * ★ 기본값이 리터럴인 이유: UHT 가 UFUNCTION 의 기본 인자로 네임스페이스
	 *   상수를 읽지 못한다("C++ Default parameter not parsed"). 대신 구현부에
	 *   static_assert 를 두어 MOUVoice::DefaultCalibrationSeconds 와 어긋나면
	 *   빌드가 깨지게 해 두었다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	void BeginSensitivityCalibration(float DurationSeconds = 2.0f);

	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	bool IsCalibrating() const { return bCalibrating; }

	/** 보정이 끝나기까지 남은 시간(초). 보정 중이 아니면 0. */
	UFUNCTION(BlueprintPure, Category = "MOU|Voice")
	float GetCalibrationRemainingSeconds() const;

	/**
	 * 캡처 객체를 버리고 다시 연다.
	 *
	 * ★★ **새로 꽂은 장치는 이걸로 못 잡는다.** 엔진이 오디오 장치 목록을
	 *    `Voice` 모듈이 로드될 때 **한 번만** 열거하고 그 결과를 싱글턴
	 *    (`FVoiceCaptureDeviceWindows`)에 캐시하기 때문이다. 캡처를 새로
	 *    만들어도 그 캐시된 목록에서 이름을 찾아 열 뿐이다.
	 *
	 *    즉 **에디터를 켠 뒤에 헤드셋을 꽂았다면 에디터를 재시작해야 한다.**
	 *    (Windows 기본 입력 장치를 바꾼 경우도 마찬가지다 - 기본 장치 이름이
	 *    시작 시점 값으로 캐시돼 있다)
	 *
	 *    이 함수가 쓸모 있는 경우는 **장치는 시작할 때부터 있었는데 캡처만
	 *    실패한** 상황이다 - 다른 프로그램이 마이크를 잡고 있었다거나, 권한을
	 *    나중에 켰다거나. 그때는 에디터를 안 껐다 켜도 된다.
	 *
	 *    장치 목록까지 새로 읽으려면 `Voice` 모듈을 통째로 언로드했다 다시
	 *    로드해야 한다(그때 싱글턴이 파괴되고 재열거된다). 그러려면 살아있는
	 *    인코더·디코더·재생 스트림을 **전부 먼저 놓아야** 해서 위험하다.
	 *    자동 감지는 V9 에서 그 방식으로 다룬다.
	 *
	 * @return 마이크가 실제로 열렸으면 true.
	 */
	UFUNCTION(BlueprintCallable, Category = "MOU|Voice")
	bool ReopenCapture();

	/** 진단용 통계를 한 줄 문자열로. MOU.Voice.Stat 이 쓴다. */
	FString GetStatsString() const;

	/**
	 * 내 PlayerController 의 음성 창구를 얻는다. 없으면 null.
	 *
	 * 매 틱 FindComponentByClass 를 부르지 않도록 캐시한다. 컨트롤러가 바뀌면
	 * (레벨 이동, 재접속) 캐시가 자동으로 풀리도록 약참조로 들고 있는다.
	 */
	UVoiceComponent* GetVoiceComponent();

private:
	/** 게임 스레드 틱. 마이크를 폴링해 재생 버퍼로 넘긴다. */
	bool Tick(float DeltaTime);

	/** 재생용 신스 컴포넌트를 만든다(루프백을 처음 켤 때). */
	void EnsurePlaybackComponent();

	/** 지금 로컬 플레이어의 PlayerController. 없으면 null. */
	APlayerController* GetOwningPlayerController() const;

	/** 말하는 동안 소리 도달 범위를 링으로 그린다. 디버그 빌드에서만 의미가 있다. */
	void DrawRadiusDebug();

	/**
	 * 마이크 캡처.
	 *
	 * 워커 스레드가 아니라 **게임 스레드에서 직접 폴링한다.**
	 * 엔진의 IVoiceCapture 구현이 게임 스레드 티커로 자기 버퍼를 채우는데
	 * 그 버퍼에 뮤텍스가 없기 때문이다(FVoiceCaptureSource.h 상단 주석 참고).
	 */
	TUniquePtr<FVoiceCaptureSource> CaptureSource;

	/**
	 * Opus 인코더. **보내는 쪽에 하나면 된다.**
	 * V3 에서도 여기 남는다 - 바뀌는 것은 결과를 재생이 아니라 RPC 로 보내는 것뿐이다.
	 */
	TUniquePtr<FMOUVoiceEncoder> Encoder;

	/**
	 * Opus 디코더.
	 *
	 * ★ V2 라서 여기 있는 것이다. V3 에서는 **UVoicePlaybackComponent 로 옮겨가고,
	 *   발신자마다 하나씩** 생긴다(Opus 디코더는 상태를 가져서 공유할 수 없다 -
	 *   VoiceCodec.h 상단 주석 참고). 지금은 발신자가 나 하나뿐이라 하나로 족하다.
	 */
	TUniquePtr<FMOUVoiceDecoder> Decoder;

	/** 매 틱 재사용하는 프레임 버퍼. 틱마다 할당하지 않으려고 멤버로 둔다. */
	TArray<FMOUVoiceFrame> PolledFrames;

	/** 인코딩/디코딩 결과를 받는 작업 버퍼. 역시 틱마다 할당하지 않으려고 멤버다. */
	TArray<uint8> EncodedScratch;
	TArray<int16> DecodedScratch;

	/**
	 * 내 PlayerController 의 음성 창구(캐시).
	 *
	 * 약참조인 이유: 컨트롤러는 레벨 이동이나 재접속으로 갈릴 수 있다.
	 * 강참조로 들면 죽은 컨트롤러의 컴포넌트에 계속 프레임을 보내게 된다.
	 */
	TWeakObjectPtr<UVoiceComponent> CachedVoiceComponent;

	/** 서버로 보낸 프레임 수. 루프백 통계와 구분해서 센다. */
	int32 FramesTransmitted = 0;

	/** 측정한 값으로 감도를 확정한다. */
	void FinishCalibration();

	// --- 감도 자동 보정 상태 -------------------------------------------------
	bool   bCalibrating        = false;
	double CalibrationEndTime  = 0.0;

	/**
	 * 측정 구간에서 관찰한 **최대** 음량.
	 *
	 * 평균이 아니라 최대를 쓰는 이유: 평균으로 기준을 잡으면 키보드 소리나
	 * 에어컨이 잠깐 커지는 순간마다 발화로 오인식된다. 잡음의 봉우리를
	 * 넘겨야 조용할 때 확실히 조용하다.
	 */
	float  CalibrationPeak     = 0.f;
	int32  CalibrationSamples  = 0;

	/**
	 * 재생 컴포넌트.
	 *
	 * UPROPERTY 로 잡아두지 않으면 GC 가 수거해간다 - 이 서브시스템만 참조를
	 * 들고 있기 때문이다.
	 */
	UPROPERTY()
	TObjectPtr<UVoiceSynthComponent> PlaybackComponent;

	FTSTicker::FDelegateHandle TickHandle;

	bool bLoopbackEnabled = false;
	bool bIsSpeaking      = false;
	bool bMuted           = false;
	bool bShowRadiusDebug = false;

	/**
	 * 코덱 사용 여부. 기본 ON.
	 *
	 * 기본값이 ON 인 이유: V3 부터는 코덱이 필수 경로라, 평소에 코덱을 통과한
	 * 소리를 듣고 있어야 음질 문제를 일찍 발견한다. 끄는 것은 비교할 때뿐이다.
	 */
	bool bCodecEnabled = true;

	/**
	 * 직전 프레임에서 말하고 있었는지. **디코더 리셋 시점을 잡는 데 쓴다.**
	 *
	 * 발화가 끝나면(true -> false) 디코더 상태를 초기화해야 한다. 안 하면 다음
	 * 발화의 첫 프레임이 몇 초 전에 끊긴 소리를 참고해 복원되어 짧게 지직거린다.
	 * bIsSpeaking 하나만으로는 "지금 상태" 만 알 수 있어 전환 시점을 못 잡는다.
	 */
	bool bWasSpeaking = false;

	/** 무전 송신 중인지(설계상 `X` 홀드). 서버가 자격을 다시 확인한다. */
	bool bRadioTransmitting = false;

	/** 지금 발화 모드. V9 에서 키/UI 로 바꾸게 되고, 지금은 콘솔로만 바꾼다. */
	EVoiceMode VoiceMode = EVoiceMode::Normal;

	/**
	 * 마지막으로 프레임을 받은 시각.
	 *
	 * 프레임이 끊겼을 때 발화 상태를 내리는 데 쓴다. 엔진 캡처가 무음 구간에
	 * 데이터를 주지 않으면 bIsSpeaking 이 true 로 붙박이는 것을 막는다.
	 */
	double LastFrameTime = 0.0;

	/** 진단 카운터. 게임 스레드 전용이라 원자적일 필요가 없다. */
	int32 FramesReceived = 0;
	int32 FramesDropped  = 0;
};
