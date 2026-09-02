// MOU 음성 - 콘솔 명령 등록.
//
// [왜 VoiceSubsystem.cpp 에서 떼어냈나 (2026-08-26)]
//
//   VoiceSubsystem.cpp 가 2105 줄이었는데 그중 1012 줄(48%)이 콘솔 명령이었다.
//   즉 파일 절반이 "시스템이 무엇을 하는가" 가 아니라 "그것을 어떻게 손으로
//   찔러보는가" 였다. 캡처·코덱·라우팅을 고치러 열었다가 검증 도구 사이를
//   스크롤하며 헤매게 된다.
//
//   떼어낼 수 있었던 이유는 **이 명령들이 public API 만 쓰기 때문**이다.
//   전부 FindVoiceSubsystem() 으로 서브시스템을 찾아 공개 함수를 부를 뿐,
//   private 멤버를 건드리지 않는다. 그래서 friend 선언도, 헤더 변경도,
//   접근자 추가도 없이 파일만 옮기면 끝났다.
//
//   이 성질은 앞으로도 지켜야 한다. 콘솔 명령이 private 를 필요로 한다면
//   그건 "명령이 지저분해서" 가 아니라 **그 기능이 아직 API 로 정리되지
//   않았다는 신호**다. 그때는 여기를 늘리지 말고 서브시스템에 공개 함수를
//   먼저 만든다.
//
// [같은 이유로 봐야 할 곳]
//   Server/ServerSubsystem.cpp 도 같은 문제를 갖고 있다(콘솔 명령 16개).
//   CodeRefactoring.md 의 R-2 항목 참고.
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md, MOU_Server/CodeRefactoring.md

#include "Voice/VoiceSubsystem.h"

#include "Voice/VoiceComponent.h"

#include "AIController.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "HAL/IConsoleManager.h"
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
// 콘솔 명령
//
// 사용 예:
//   MOU.Voice.Loopback 1          내 목소리를 내 헤드폰으로 (V1 검증)
//   MOU.Voice.Codec 0             Opus 우회 - 원본과 압축을 A/B 비교 (V2 검증)
//   MOU.Voice.Stat                통계 출력 (압축률과 프레임 크기 포함)
//   MOU.Voice.Sensitivity 0.01    마이크 감도
//   MOU.Voice.FakeNoise 1500      마이크 없이 NPC 소음만 발생 (V0, NPC 팀원용)
//
// 기존 채팅의 MOU.Chat.* 와 같은 등록 방식을 쓴다.
// ---------------------------------------------------------------------------

namespace
{
	/**
	 * 콘솔 명령이 실행된 월드에서 음성 서브시스템을 찾는다.
	 * PIE 창이 여러 개면 "지금 콘솔을 연 창" 의 것이 잡힌다.
	 */
	UVoiceSubsystem* FindVoiceSubsystem(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
		return LocalPlayer ? LocalPlayer->GetSubsystem<UVoiceSubsystem>() : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs GVoiceLoopbackCommand(
		TEXT("MOU.Voice.Loopback"),
		TEXT("로컬 루프백을 켜고 끈다(내 목소리가 내 헤드폰으로). 사용법: MOU.Voice.Loopback <0|1>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					const bool bEnable = Args.IsValidIndex(0) ? (FCString::Atoi(*Args[0]) != 0) : true;
					Voice->SetLoopbackEnabled(bEnable);
				}
				else
				{
					UE_LOG(LogMOUVoice, Warning, TEXT("음성 서브시스템을 찾지 못했다."));
				}
			}));

	/**
	 * V2 검증의 핵심 도구.
	 *
	 * 루프백을 켠 채로 이 명령을 0/1 로 왕복하며 같은 말을 반복하면
	 * **압축이 음질을 얼마나 깎는지** 직접 비교할 수 있다.
	 * "전화 수준이면 합격" 이라는 기준은 이렇게만 판정할 수 있다 - 숫자로는 안 된다.
	 */
	FAutoConsoleCommandWithWorldAndArgs GVoiceCodecCommand(
		TEXT("MOU.Voice.Codec"),
		TEXT("Opus 코덱을 켜고 끈다(끄면 원본 PCM 이 그대로 재생된다. 음질 비교용). ")
		TEXT("인자 없이 부르면 토글. 사용법: MOU.Voice.Codec [0|1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					const bool bEnable = Args.IsValidIndex(0)
						? (FCString::Atoi(*Args[0]) != 0)
						: !Voice->IsCodecEnabled();

					Voice->SetCodecEnabled(bEnable);
				}
				else
				{
					UE_LOG(LogMOUVoice, Warning, TEXT("음성 서브시스템을 찾지 못했다."));
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceMuteCommand(
		TEXT("MOU.Voice.Mute"),
		TEXT("마이크 음소거를 켜고 끈다(C 키와 동일). 인자 없이 부르면 토글. 사용법: MOU.Voice.Mute [0|1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					if (Args.IsValidIndex(0))
					{
						Voice->SetMuted(FCString::Atoi(*Args[0]) != 0);
					}
					else
					{
						Voice->ToggleMute();
					}
				}
				else
				{
					UE_LOG(LogMOUVoice, Warning, TEXT("음성 서브시스템을 찾지 못했다."));
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceShowRadiusCommand(
		TEXT("MOU.Voice.ShowRadius"),
		TEXT("말할 때 소리 도달 범위를 링으로 그린다(초록=사람, 빨강=NPC). 사용법: MOU.Voice.ShowRadius <0|1>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					const bool bEnable = Args.IsValidIndex(0) ? (FCString::Atoi(*Args[0]) != 0) : true;
					Voice->SetShowRadiusDebug(bEnable);
				}
			}));

	/**
	 * 음량 -> 반경 곡선을 실시간으로 튜닝한다.
	 *
	 * ★ 값이 MOUVoice 네임스페이스의 전역이라 **서버·클라 양쪽에서 따로
	 *   실행해야 한다.** PIE 처럼 한 프로세스에 서버·클라가 같이 있을 때만
	 *   한 번으로 양쪽에 반영된다 - 데디케이티드 서버로 가면 서버 쪽에서도
	 *   실행하지 않으면 NPC 반경과 화면의 원이 어긋난다(VoiceTypes.h 주석).
	 *
	 * 인자 없이 부르면 현재 값을 보여준다. 인자 순서: Ceiling, MinScale, Exponent.
	 * 하나만 바꾸고 싶으면 나머지에 현재 값을 그대로 넣는다.
	 */
	FAutoConsoleCommandWithWorldAndArgs GVoiceLoudnessCurveCommand(
		TEXT("MOU.Voice.LoudnessCurve"),
		TEXT("음량->반경 곡선을 조절한다. 사용법: MOU.Voice.LoudnessCurve [Ceiling] [MinScale] [Exponent]. ")
		TEXT("인자 없이 부르면 현재 값 표시. 서버·클라 양쪽에서 따로 실행해야 한다(★ 위 주석)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (!Args.IsValidIndex(0))
				{
					UE_LOG(LogMOUVoice, Log,
						TEXT("Loudness Ceiling=%.3f  MinRadiusScale=%.2f  CurveExponent=%.2f"),
						MOUVoice::LoudnessCeiling, MOUVoice::MinRadiusScale, MOUVoice::RadiusCurveExponent);
					return;
				}

				MOUVoice::LoudnessCeiling = FMath::Max(FCString::Atof(*Args[0]), UE_KINDA_SMALL_NUMBER);

				if (Args.IsValidIndex(1))
				{
					MOUVoice::MinRadiusScale = FMath::Clamp(FCString::Atof(*Args[1]), 0.f, 1.f);
				}
				if (Args.IsValidIndex(2))
				{
					MOUVoice::RadiusCurveExponent = FMath::Max(FCString::Atof(*Args[2]), UE_KINDA_SMALL_NUMBER);
				}

				// ★ 감도보다 낮은 상한을 넣으면 조절 구간이 사라진다(모든 발화가
				//   최대 반경). 손으로 넣는 값이라 더 쉽게 일어난다 - 되돌리지 않고
				//   밀어올린 뒤 왜 그랬는지 말해준다. 조용히 고치면 다음에 또 넣는다.
				if (const UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					const float Threshold = Voice->GetMicSensitivity();

					if (MOUVoice::EnsureUsableLoudnessSpan(Threshold))
					{
						UE_LOG(LogMOUVoice, Warning,
							TEXT("★ 상한이 현재 감도(%.4f)보다 낮거나 너무 가까웠다. ")
							TEXT("%.4f 로 올렸다 - 그대로 두면 속삭여도 최대 반경이 된다."),
							Threshold, MOUVoice::LoudnessCeiling);
					}
				}

				UE_LOG(LogMOUVoice, Log,
					TEXT("Loudness Ceiling=%.3f  MinRadiusScale=%.2f  CurveExponent=%.2f 로 설정."),
					MOUVoice::LoudnessCeiling, MOUVoice::MinRadiusScale, MOUVoice::RadiusCurveExponent);
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceModeCommand(
		TEXT("MOU.Voice.Mode"),
		TEXT("발화 모드를 바꾼다. 사용법: MOU.Voice.Mode <0=속삭임|1=보통|2=외침>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				UVoiceSubsystem* Voice = FindVoiceSubsystem(World);
				if (!Voice)
				{
					return;
				}

				if (!Args.IsValidIndex(0))
				{
					UE_LOG(LogMOUVoice, Log, TEXT("현재 발화 모드 = %s"),
						MOUVoice::GetVoiceModeName(Voice->GetVoiceMode()));
					return;
				}

				// 범위를 벗어난 값은 조용히 뭉개지 말고 알려준다.
				// 조용히 '보통'으로 떨어뜨리면 왜 안 바뀌는지 알 수 없다.
				const int32 Raw = FCString::Atoi(*Args[0]);
				if (Raw < 0 || Raw > 2)
				{
					UE_LOG(LogMOUVoice, Warning,
						TEXT("모드는 0(속삭임) / 1(보통) / 2(외침) 중 하나여야 한다. 받은 값: %d"), Raw);
					return;
				}

				Voice->SetVoiceMode(static_cast<EVoiceMode>(Raw));
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceStatCommand(
		TEXT("MOU.Voice.Stat"),
		TEXT("음성 파이프라인 통계를 출력한다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					UE_LOG(LogMOUVoice, Log, TEXT("%s"), *Voice->GetStatsString());
				}
			}));

	/**
	 * 마이크가 안 잡힐 때 원인을 단계별로 짚어주는 명령.
	 *
	 * "마이크가 준비되지 않았다" 는 원인이 네 가지나 되는데 증상이 똑같아서
	 * 어디서 막혔는지 알기 어렵다. 각 단계를 따로 찍어준다.
	 */
	FAutoConsoleCommandWithWorldAndArgs GVoiceDiagCommand(
		TEXT("MOU.Voice.Diag"),
		TEXT("마이크가 안 잡힐 때 원인을 단계별로 진단한다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				UE_LOG(LogMOUVoice, Log, TEXT("===== 음성 진단 ====="));

				const bool bModuleLoaded = FVoiceModule::IsAvailable();
				UE_LOG(LogMOUVoice, Log, TEXT("1) Voice 모듈 로드   : %s"),
					bModuleLoaded ? TEXT("O") : TEXT("X  <- 모듈이 안 올라왔다"));

				if (!bModuleLoaded)
				{
					UE_LOG(LogMOUVoice, Log, TEXT("   -> Build.cs 의 \"Voice\" 의존성을 확인할 것."));
					return;
				}

				const bool bVoiceEnabled = FVoiceModule::Get().IsVoiceEnabled();
				UE_LOG(LogMOUVoice, Log, TEXT("2) [Voice] bEnabled  : %s"),
					bVoiceEnabled ? TEXT("true") : TEXT("false <- ini 설정 문제"));

				if (!bVoiceEnabled)
				{
					UE_LOG(LogMOUVoice, Log,
						TEXT("   -> Config/DefaultEngine.ini 에 [Voice] bEnabled=true 를 넣고 ")
						TEXT("**에디터를 재시작**할 것. ini 는 시작 시 한 번만 읽는다."));
					return;
				}

				UVoiceSubsystem* Voice = FindVoiceSubsystem(World);
				UE_LOG(LogMOUVoice, Log, TEXT("3) 음성 서브시스템   : %s"),
					Voice ? TEXT("O") : TEXT("X"));

				if (!Voice)
				{
					return;
				}

				UE_LOG(LogMOUVoice, Log, TEXT("4) 마이크 열기       : %s"),
					Voice->IsCaptureReady() ? TEXT("O") : TEXT("X  <- 장치 또는 권한 문제"));

				if (!Voice->IsCaptureReady())
				{
					UE_LOG(LogMOUVoice, Log,
						TEXT("   -> Windows 설정 > 개인 정보 및 보안 > 마이크 에서 ")
						TEXT("'데스크톱 앱이 마이크에 액세스하도록 허용'이 켜져 있는지 확인할 것."));
					UE_LOG(LogMOUVoice, Log,
						TEXT("   -> 헤드셋을 꽂은 뒤 에디터를 켰다면, 장치 목록이 갱신되도록 ")
						TEXT("에디터를 재시작해볼 것."));
					return;
				}

				// 5단계: 코덱. 마이크와 별개로 실패할 수 있으므로 따로 찍는다.
				// "마이크는 잡히는데 소리가 안 난다" 의 원인이 여기일 수 있다.
				UE_LOG(LogMOUVoice, Log, TEXT("5) Opus 코덱         : %s (%s)"),
					Voice->IsCodecEnabled() ? TEXT("사용") : TEXT("우회 중"),
					Voice->IsCodecReady() ? TEXT("인코더/디코더 준비됨") : TEXT("X  <- 생성 실패"));

				if (Voice->IsCodecEnabled() && !Voice->IsCodecReady())
				{
					UE_LOG(LogMOUVoice, Log,
						TEXT("   -> 코덱 없이 들어보려면 MOU.Voice.Codec 0 으로 우회할 수 있다."));
				}

				UE_LOG(LogMOUVoice, Log, TEXT("모두 정상. %s"), *Voice->GetStatsString());
			}));

	/**
	 * ★ "가만히 있는데 계속 말하는 중으로 나온다" 의 해결책.
	 *
	 * 기본 임계값(0.02)은 어떤 마이크에도 맞지 않는 임의의 숫자다. 특히 3.5mm
	 * 아날로그 입력은 잡음 바닥이 그보다 높아서 조용해도 발화로 잡힌다.
	 * 사람이 이 숫자를 감으로 맞추는 것은 불가능하므로 측정해서 정한다(9절).
	 */
	FAutoConsoleCommandWithWorldAndArgs GVoiceCalibrateCommand(
		TEXT("MOU.Voice.Calibrate"),
		TEXT("조용한 상태의 잡음을 재서 마이크 감도를 자동으로 맞춘다. ")
		TEXT("★ 측정 동안 말하지 말 것. 사용법: MOU.Voice.Calibrate [초=2]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					const float Duration = Args.IsValidIndex(0)
						? FCString::Atof(*Args[0])
						: MOUVoice::DefaultCalibrationSeconds;

					Voice->BeginSensitivityCalibration(Duration);
				}
				else
				{
					UE_LOG(LogMOUVoice, Warning, TEXT("음성 서브시스템을 찾지 못했다."));
				}
			}));

	/**
	 * V5 사망자 차단 테스트용.
	 *
	 * ★ 실제 사망(체력 0)과는 아직 연결돼 있지 않다. 캐릭터/PlayerState 와 엮는 것은
	 *   다음 단계이고, 지금은 **차단 구조 3겹이 실제로 동작하는지**를 확인하는 것이
	 *   목적이라 상태를 직접 세울 수단만 만들어 두었다.
	 */
	FAutoConsoleCommandWithWorldAndArgs GVoiceDieCommand(
		TEXT("MOU.Voice.Die"),
		TEXT("음성 사망 상태를 토글한다(말하기·듣기 모두 차단). ")
		TEXT("사용법: MOU.Voice.Die [0|1] (인자 없으면 토글)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				UVoiceSubsystem* Voice = FindVoiceSubsystem(World);
				if (!Voice)
				{
					UE_LOG(LogMOUVoice, Warning, TEXT("음성 서브시스템을 찾지 못했다."));
					return;
				}

				const bool bDead = Args.IsValidIndex(0)
					? (FCString::Atoi(*Args[0]) != 0)
					: !Voice->IsVoiceDead();

				Voice->RequestVoiceDead(bDead);
			}));

	/**
	 * 위 Die 의 반대. `MOU.Voice.Die 0` 과 같은 일을 하지만 이름을 따로 둔다 -
	 * 테스트 중에 죽여놓은 것을 되돌리려고 인자까지 기억해 붙이는 건 실수하기 쉽고,
	 * 인자를 빼먹으면(토글이 되어) 오히려 산 사람을 죽인다.
	 *
	 * 사망은 bVoiceDead 하나로만 걸리므로 그것만 내리면 말하기·듣기가 함께 돌아온다
	 * (송신 1겹·수신 3겹이 모두 같은 값을 본다). 다만 음소거는 별개의 상태라
	 * 여기서 건드리지 않는다 - 사용자가 직접 켠 설정을 부활이 몰래 되돌리면
	 * "왜 음소거가 풀렸지" 가 된다. 대신 아직 안 들릴 이유가 남아 있으면 알려준다.
	 */
	FAutoConsoleCommandWithWorldAndArgs GVoiceReviveCommand(
		TEXT("MOU.Voice.Revive"),
		TEXT("음성 사망 상태를 푼다(말하기·듣기 모두 복구). 인자 없음. MOU.Voice.Die 의 반대."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				UVoiceSubsystem* Voice = FindVoiceSubsystem(World);
				if (!Voice)
				{
					UE_LOG(LogMOUVoice, Warning, TEXT("음성 서브시스템을 찾지 못했다."));
					return;
				}

				if (!Voice->IsVoiceDead())
				{
					UE_LOG(LogMOUVoice, Log, TEXT("이미 생존 상태다."));
				}

				// 서버가 확정하고 복제해 돌아온다(Die 와 같은 경로).
				Voice->RequestVoiceDead(false);

				// 부활했는데도 조용하면 원인은 거의 항상 이 둘 중 하나다.
				// 여기서 짚어주지 않으면 "부활이 안 먹는다" 로 오해하게 된다.
				if (Voice->IsMuted())
				{
					UE_LOG(LogMOUVoice, Warning,
						TEXT("부활했지만 마이크가 음소거 상태다. MOU.Voice.Mute 0 으로 풀 것."));
				}

				if (!Voice->IsCaptureReady())
				{
					UE_LOG(LogMOUVoice, Warning,
						TEXT("부활했지만 마이크 캡처가 준비되지 않았다. MOU.Voice.Diag 로 확인할 것."));
				}
			}));

	// -----------------------------------------------------------------------
	// 무전기 (V6)
	//
	// ★ Spawn/Drop 은 테스트 도구다. 진짜 무전기는 아이템으로 줍고 버린다.
	//   Power 는 설계상 `Z` 키, PTT 는 `X` 키에 대응한다 - 키 바인딩은 V9 몫이라
	//   지금은 콘솔로만 조작한다.
	// -----------------------------------------------------------------------

	FAutoConsoleCommandWithWorldAndArgs GVoiceRadioSpawnCommand(
		TEXT("MOU.Voice.Radio.Spawn"),
		TEXT("테스트용 무전기를 손에 든다(임시 - 아이템 파트 완성 전까지)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					if (UVoiceComponent* Comp = Voice->GetVoiceComponent())
					{
						Comp->ServerDebugSpawnRadio();
					}
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceRadioDropCommand(
		TEXT("MOU.Voice.Radio.Drop"),
		TEXT("들고 있던 무전기를 그 자리에 놓는다. 켜져 있으면 거기서 계속 소리가 난다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					if (UVoiceComponent* Comp = Voice->GetVoiceComponent())
					{
						Comp->ServerDebugDropRadio();
					}
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceRadioPowerCommand(
		TEXT("MOU.Voice.Radio.Power"),
		TEXT("무전기 전원을 켜고 끈다(설계상 Z 키). ")
		TEXT("★ 끄면 음소거가 아니라 **무전망에서 완전히 빠진다.** 사용법: <0|1>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					if (UVoiceComponent* Comp = Voice->GetVoiceComponent())
					{
						Comp->ServerDebugSetRadioPower(Args.IsValidIndex(0) ? (FCString::Atoi(*Args[0]) != 0) : true);
					}
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceRadioPttCommand(
		TEXT("MOU.Voice.Radio.PTT"),
		TEXT("무전 송신을 켜고 끈다(설계상 X 키 홀드). 인자 없으면 토글. 사용법: [0|1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					const bool bOn = Args.IsValidIndex(0)
						? (FCString::Atoi(*Args[0]) != 0)
						: !Voice->IsRadioTransmitting();

					Voice->SetRadioTransmitting(bOn);
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceReopenCommand(
		TEXT("MOU.Voice.Reopen"),
		TEXT("마이크 캡처를 닫았다 다시 연다. ")
		TEXT("★ 에디터를 켠 뒤 새로 꽂은 장치는 이걸로 못 잡는다(엔진이 장치 목록을 캐시한다)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					Voice->ReopenCapture();
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceSensitivityCommand(
		TEXT("MOU.Voice.Sensitivity"),
		TEXT("마이크 감도(VAD 임계값)를 바꾼다. 사용법: MOU.Voice.Sensitivity <0~1>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					if (!Args.IsValidIndex(0))
					{
						UE_LOG(LogMOUVoice, Log, TEXT("현재 감도 = %.4f"), Voice->GetMicSensitivity());
						return;
					}
					Voice->SetMicSensitivity(FCString::Atof(*Args[0]));
				}
			}));

	// -----------------------------------------------------------------------
	// V0/V8 - NPC 담당 팀원용 소음 도구
	//
	// ★ 이 명령들은 음성 파이프라인과 완전히 독립적이다.
	//   마이크가 없어도, 캡처가 실패해도, 음성 코드가 한 줄도 안 돌아도 동작한다.
	//   그래서 NPC 담당자는 음성 시스템 완성을 기다리지 않고 청각 반응을
	//   지금 바로 작업할 수 있다.
	//
	// V8 에서 진짜 음성이 붙으면 같은 ReportNoiseEvent 를 부르므로,
	// 이 명령으로 맞춰둔 NPC 동작이 그대로 유효하다.
	// -----------------------------------------------------------------------

	/**
	 * 플레이어 위치에 소음 이벤트를 하나 쏜다. 성공하면 true.
	 *
	 * 반경/음량을 0 으로 주면 현재 발화 모드의 실제 값을 쓴다. 기본값을 여기
	 * 하드코딩하지 않는 이유는, NPC 팀원이 이 명령으로 맞춰둔 반응이 V8 의 진짜
	 * 음성에도 그대로 유효해야 하기 때문이다(숫자가 다르면 다시 튜닝해야 한다).
	 */
	bool ReportFakeNoise(UWorld* World, float MaxRange, float Loudness, FName Tag)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;

		if (!Pawn)
		{
			UE_LOG(LogMOUVoice, Warning,
				TEXT("소음을 낼 폰이 없다. 게임에 스폰된 뒤에 사용할 것."));
			return false;
		}

		const UVoiceSubsystem* Voice = FindVoiceSubsystem(World);
		const EVoiceMode Mode = Voice ? Voice->GetVoiceMode() : EVoiceMode::Normal;

		if (MaxRange <= 0.f)
		{
			MaxRange = MOUVoice::GetNoiseRange(Mode);
		}
		if (Loudness <= 0.f)
		{
			Loudness = MOUVoice::GetLoudnessScale(Mode);
		}

		const FVector NoiseLocation = Pawn->GetActorLocation();

		// 소음의 책임자(Instigator)는 "소리가 난 위치에 있는 대상" 이다.
		// NPC 는 이 값으로 누구를 쫓을지 판단한다.
		UAISense_Hearing::ReportNoiseEvent(
			World, NoiseLocation, Loudness, Pawn, MaxRange, Tag);

		UE_LOG(LogMOUVoice, Log,
			TEXT("[가짜 소음] 위치=%s 반경=%.0f 음량=%.2f 태그=%s"),
			*NoiseLocation.ToCompactString(), MaxRange, Loudness, *Tag.ToString());

		return true;
	}

	/**
	 * 퍼셉션 컴포넌트 하나의 청각 관련 상태를 찍는다.
	 *
	 * DumpHearingState 가 컨트롤러마다 **모든** 퍼셉션 컴포넌트에 대해 부른다.
	 * bIsPrimary 는 AAIController::GetPerceptionComponent() 가 돌려주는 그 하나인지다 -
	 * 설정은 A 에 넣었는데 BP/BT 는 B 를 쓰는 상황을 잡으려고 표시한다.
	 */
	void DumpPerceptionComponent(UAIPerceptionComponent* Perception, APawn* PlayerPawn,
		float ModeLoudness, bool bIsPrimary)
	{
		if (!Perception)
		{
			return;
		}

		UE_LOG(LogMOUVoice, Log, TEXT("    [컴포넌트] %s  (소유자=%s%s)"),
			*Perception->GetName(),
			*GetNameSafe(Perception->GetOwner()),
			bIsPrimary ? TEXT(", GetPerceptionComponent 가 쓰는 것") : TEXT(""));

		// ★ 셋이 전부 다른 이야기다. 하나로 뭉뚱그리면 오진한다.
		//   · 컴포넌트 등록 = UActorComponent::IsRegistered(). **월드에 붙었다는
		//     뜻일 뿐 퍼셉션과 무관하다.**
		//   · 리스너 ID    = 퍼셉션 시스템이 실제로 리스너로 받아들였는가.
		//                    무효면 자극이 올 길이 아예 없다.
		//   · 청각 채널    = PerceptionFilter 에 청각이 있는가. 없으면
		//                    UAISense_Hearing::Update 의 HasSense() 에서 건너뛴다.
		UE_LOG(LogMOUVoice, Log,
			TEXT("      컴포넌트 등록: %s, 리스너 ID: %s, 청각 채널 활성: %s"),
			Perception->IsRegistered() ? TEXT("O") : TEXT("X"),
			Perception->GetListenerId().IsValid() ? TEXT("O") : TEXT("X <- 리스너가 아니다"),
			Perception->IsSenseEnabled(UAISense_Hearing::StaticClass()) ? TEXT("O") : TEXT("X"));

		// ★ 거리 판정은 폰 위치가 아니라 **리스너가 캐시해둔 위치**로 한다
		//   (UAISense_Hearing::Update 의 Listener.CachedLocation).
		//   위의 "플레이어와 N cm" 는 폰 기준이라, 둘이 크게 다르면 눈에 보이는
		//   거리와 실제 판정 거리가 어긋나고 있다는 뜻이다.
		FVector ListenerLocation(ForceInitToZero);
		FVector ListenerDirection(ForceInitToZero);
		Perception->GetLocationAndDirection(ListenerLocation, ListenerDirection);

		UE_LOG(LogMOUVoice, Log, TEXT("      리스너 기준 위치: %s"),
			*ListenerLocation.ToCompactString());

		UAISenseConfig* SenseConfig =
			Perception->GetSenseConfig(UAISense::GetSenseID<UAISense_Hearing>());
		UAISenseConfig_Hearing* Hearing = Cast<UAISenseConfig_Hearing>(SenseConfig);

		if (Hearing)
		{
			// MaxAge 0 은 엔진이 FLT_MAX 로 바꿔 들고 있다. 그대로 찍으면
			// 3.4e38 이 나와 설정이 깨진 것처럼 보인다.
			const float MaxAge = Hearing->GetMaxAge();
			const FString MaxAgeText = (MaxAge >= FLT_MAX)
				? FString(TEXT("만료 없음"))
				: FString::Printf(TEXT("%.1f s"), MaxAge);

			UE_LOG(LogMOUVoice, Log,
				TEXT("      청각: 범위=%.0f cm (현재 모드 실효 %.0f cm), 최대수명=%s, ")
				TEXT("적=%s 중립=%s 아군=%s"),
				Hearing->HearingRange,
				Hearing->HearingRange * ModeLoudness,
				*MaxAgeText,
				Hearing->DetectionByAffiliation.bDetectEnemies    ? TEXT("O") : TEXT("X"),
				Hearing->DetectionByAffiliation.bDetectNeutrals   ? TEXT("O") : TEXT("X"),
				Hearing->DetectionByAffiliation.bDetectFriendlies ? TEXT("O") : TEXT("X"));
		}
		else
		{
			// ★ 여기 걸리면 이 컴포넌트엔 청각 설정이 없는 것이다.
			//   에디터에서 배열에 추가했는데도 뜨면 다른 컴포넌트에 넣은 것이다.
			UE_LOG(LogMOUVoice, Warning,
				TEXT("      청각 설정(AISenseConfig_Hearing)이 없다."));
		}

		// ★ 이게 이 진단의 핵심 한 줄이다.
		//   여기에 플레이어가 나오면 소음은 확실히 도달했고, 문제는 BP/BT 쪽이다.
		TArray<AActor*> HeardActors;
		Perception->GetCurrentlyPerceivedActors(UAISense_Hearing::StaticClass(), HeardActors);

		if (HeardActors.Num() == 0)
		{
			UE_LOG(LogMOUVoice, Warning, TEXT("      >> 청각으로 감지 중인 액터가 없다."));
		}
		else
		{
			for (const AActor* Heard : HeardActors)
			{
				UE_LOG(LogMOUVoice, Log,
					TEXT("      >> 청각 감지 중: %s  (여기 나오면 퍼셉션은 정상. BP/BT 를 볼 것)"),
					*GetNameSafe(Heard));
			}
		}

		// ★ "지금 감지 중" 은 여러 조건의 **결과**라, 비어 있다는 것만으로는
		//   자극이 안 온 건지 왔다가 죽은 건지 알 수 없다. 그래서 원시 기록도 본다.
		//
		// ★★ 다만 이 프로젝트에서 아래 숫자는 "한 번이라도" 의 뜻이 **아니다.** ★★
		//   DefaultEngine.ini 에 [/Script/AIModule.AISystem] bForgetStaleActors=True
		//   가 있다. 그러면 UAIPerceptionComponent::ProcessStimuli 가
		//
		//       if (bForgetStaleActors && !PerceptualInfo->HasAnyCurrentStimulus())
		//           ActorsToForget.Add(ActorToForget);
		//
		//   로 **기록 자체를 지운다.** 시각(최대수명 1초)은 1초 뒤 만료되면서
		//   액터 항목이 통째로 사라지므로, 방금 봤어도 여기엔 0 으로 나온다.
		//   즉 이 값이 0 이라고 해서 자극이 온 적 없다고 단정하면 안 된다.
		//   반면 청각은 최대수명이 0(만료 없음)이라 한 번 들어오면 남는다.
		TArray<AActor*> EverHeard;
		Perception->GetKnownPerceivedActors(UAISense_Hearing::StaticClass(), EverHeard);

		// 빈 TSubclassOf 를 넘기면 "감각 무관" 이 된다(엔진이 SenseToUse == nullptr
		// 로 분기한다). nullptr 리터럴은 오버로드가 모호해질 수 있어 변수로 둔다.
		const TSubclassOf<UAISense> AnySense;

		TArray<AActor*> EverPerceived;
		Perception->GetKnownPerceivedActors(AnySense, EverPerceived);

		UE_LOG(LogMOUVoice, Log,
			TEXT("      한 번이라도 감지된 액터: 청각 %d 개 / 전체 감각 %d 개"),
			EverHeard.Num(), EverPerceived.Num());

		for (const AActor* Known : EverPerceived)
		{
			UE_LOG(LogMOUVoice, Log, TEXT("        · %s"), *GetNameSafe(Known));
		}

		if (!PlayerPawn)
		{
			return;
		}

		FActorPerceptionBlueprintInfo Info;

		if (!Perception->GetActorsPerception(PlayerPawn, Info))
		{
			UE_LOG(LogMOUVoice, Warning,
				TEXT("      플레이어에 대한 지각 기록 자체가 없다 ")
				TEXT("(어떤 감각으로도 인지된 적이 없음)."));
			return;
		}

		for (int32 Index = 0; Index < Info.LastSensedStimuli.Num(); ++Index)
		{
			const FAIStimulus& Stimulus = Info.LastSensedStimuli[Index];

			// 한 번도 안 온 감각은 기본값 그대로라 찍어봐야 노이즈다.
			if (!Stimulus.IsValid())
			{
				continue;
			}

			UE_LOG(LogMOUVoice, Log,
				TEXT("      [자극 %d] 세기=%.2f 나이=%.2f 성공=%s 만료=%s 태그=%s 위치=%s"),
				Index,
				Stimulus.Strength,
				Stimulus.GetAge(),
				Stimulus.WasSuccessfullySensed() ? TEXT("O") : TEXT("X"),
				Stimulus.IsExpired() ? TEXT("O") : TEXT("X"),
				*Stimulus.Tag.ToString(),
				*Stimulus.StimulusLocation.ToCompactString());
		}
	}

	/**
	 * 월드의 모든 AI 를 돌며 청각 설정과 현재 감지 목록을 찍는다.
	 *
	 * ★ 이 진단이 필요한 이유:
	 *   NPC 가 반응하지 않을 때 원인이 두 갈래인데 겉으로는 구분이 안 된다.
	 *     (a) 소음이 퍼셉션 컴포넌트까지 아예 도달하지 못했다 (반경·소속·등록 문제)
	 *     (b) 도달은 했는데 BP 그래프가 그 자극을 쓰지 않는다 (BT 분기 없음)
	 *   BP 에 Print String 을 심어 확인하면 (b) 쪽 코드를 건드리게 되고,
	 *   그 과정에서 뭘 바꿨는지 헷갈려 원인이 더 흐려진다.
	 *   그래서 여기서는 퍼셉션 컴포넌트의 상태를 직접 읽는다. BP 는 손대지 않는다.
	 */
	void DumpHearingState(UWorld* World)
	{
		if (!World)
		{
			return;
		}

		APlayerController* PC = World->GetFirstPlayerController();
		APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;

		UE_LOG(LogMOUVoice, Log, TEXT("===== 청각 진단 ====="));
		UE_LOG(LogMOUVoice, Log, TEXT("플레이어 폰: %s (팀 %d)"),
			PlayerPawn ? *PlayerPawn->GetName() : TEXT("없음"),
			PlayerPawn ? FGenericTeamId::GetTeamIdentifier(PlayerPawn).GetId() : 255);

		const FAISenseID HearingSenseID = UAISense::GetSenseID<UAISense_Hearing>();

		// ★★ 소음이 조용히 사라지는 자리 ★★
		//
		// UAIPerceptionSystem::OnEvent 는 청각 센스가 인스턴스화돼 있지 않으면
		// 이벤트를 **아무 로그도 없이 버린다**(엔진 주석: "there's no one
		// interested in this event, skip it"). ReportNoiseEvent 는 성공한 것처럼
		// 보이고 우리 로그도 정상으로 찍히는데 아무도 못 듣는다.
		// 월드 단위 상태라 AI 루프 밖에서 한 번만 본다.
		//
		// ※ 판정 루프의 진짜 입력은 퍼셉션 시스템이 따로 들고 있는 리스너 사본
		//   (FPerceptionListener 의 Filter/CachedLocation)이지만, 그 맵을 주는
		//   GetListenersMap 은 protected 라 여기서 못 읽는다. 대신 사본이 복사해
		//   가는 원본 - 컴포넌트의 필터와 GetLocationAndDirection - 을 아래 AI
		//   루프에서 찍는다.
		if (const UAIPerceptionSystem* PerceptionSys = UAIPerceptionSystem::GetCurrent(World))
		{
			// UE_LOG 의 Verbosity 는 컴파일 타임 상수여야 해서 분기로 나눈다.
			if (PerceptionSys->IsSenseInstantiated(HearingSenseID))
			{
				UE_LOG(LogMOUVoice, Log, TEXT("퍼셉션 시스템의 청각 센스: 살아있음"));
			}
			else
			{
				UE_LOG(LogMOUVoice, Warning,
					TEXT("퍼셉션 시스템의 청각 센스가 없다. ")
					TEXT("소음 이벤트가 여기서 통째로 버려진다."));
			}
		}
		else
		{
			UE_LOG(LogMOUVoice, Warning, TEXT("이 월드에 퍼셉션 시스템이 없다."));
		}

		// ★★ 퍼셉션 시스템 전체를 한 방에 죽이는 플래그 ★★
		//
		// UAIPerceptionSystem::Tick 의 첫 줄이 `if (World->bPlayersOnly == false)` 다.
		// 이게 켜져 있으면 센스 갱신도, 리스너 위치 캐싱도, 자극 배달도 전부 멈춘다.
		// 콘솔 `PlayersOnly` 로 켜지고, **한 번 켜면 화면상으로는 플레이어만
		// 멀쩡히 움직여서** 원인으로 의심하기가 매우 어렵다.
		if (World->bPlayersOnly || World->bPlayersOnlyPending)
		{
			UE_LOG(LogMOUVoice, Warning,
				TEXT("월드가 PlayersOnly 상태다(%s). 퍼셉션 Tick 이 통째로 멈춰 있다. ")
				TEXT("콘솔에 PlayersOnly 를 다시 쳐서 끌 것."),
				World->bPlayersOnly ? TEXT("적용됨") : TEXT("다음 프레임 적용 예정"));
		}

		// ★ 실효 반경 = 설정 범위 x 소음의 Loudness 다.
		//   UAISense_Hearing::Update 가 HearingRangeSq 에 Loudness 제곱을 곱해
		//   비교한다(엔진 소스). Loudness 를 "음량" 으로만 생각하면 놓치는 부분이라
		//   설정값과 나란히 찍는다 - 속삭임(0.35)은 설정 범위의 35% 까지만 들린다.
		const UVoiceSubsystem* Voice = FindVoiceSubsystem(World);
		const EVoiceMode Mode = Voice ? Voice->GetVoiceMode() : EVoiceMode::Normal;
		const float ModeLoudness = MOUVoice::GetLoudnessScale(Mode);

		int32 ControllerCount = 0;

		for (TActorIterator<AAIController> It(World); It; ++It)
		{
			AAIController* AI = *It;

			if (!IsValid(AI))
			{
				continue;
			}

			++ControllerCount;

			APawn* AIPawn = AI->GetPawn();

			const float Distance = (PlayerPawn && AIPawn)
				? FVector::Dist(AIPawn->GetActorLocation(), PlayerPawn->GetActorLocation())
				: -1.f;

			UE_LOG(LogMOUVoice, Log, TEXT("  --- %s (폰=%s, 팀 %d, 플레이어와 %.0f cm)"),
				*AI->GetName(),
				AIPawn ? *AIPawn->GetName() : TEXT("없음"),
				FGenericTeamId::GetTeamIdentifier(AI).GetId(),
				Distance);

			if (PlayerPawn)
			{
				// 소속 판정은 거리 검사보다 **먼저** 걸린다. 여기서 걸러지면
				// 반경을 아무리 키워도 소용이 없다. 컴포넌트가 아니라 컨트롤러
				// 단위 값이라 컴포넌트 루프 밖에서 한 번만 찍는다.
				//
				// ★★ 반드시 **TeamId 끼리 비교하는 오버로드**를 써야 한다 ★★
				//
				//   액터를 받는 FGenericTeamId::GetAttitude(A, B) 를 쓰면 안 된다.
				//   그쪽은 상대가 IGenericTeamAgentInterface 를 구현하지 않으면
				//   내용을 보지도 않고 Neutral 을 돌려준다:
				//
				//       return OtherTeamAgent ? GetAttitude(내 팀, 상대 팀)
				//                             : ETeamAttitude::Neutral;
				//
				//   플레이어 폰은 그 인터페이스를 구현하지 않으므로 **항상 "중립"**
				//   으로 나온다. 그런데 UAISense_Hearing 은 TeamId 오버로드를 쓴다:
				//
				//       DefaultTeamAttitudeSolver(A, B)
				//           = (A != B) ? Hostile : Friendly
				//
				//   팀을 아무도 설정하지 않았으면 양쪽 다 NoTeam(255) 이라 **우호**다.
				//   즉 진단은 "중립" 이라고 하는데 퍼셉션은 "우호" 로 판정한다.
				//   이 불일치 때문에 "중립 탐지" 를 켜도 안 들리고 "아군 탐지" 를
				//   켜야 들리는, 원인을 짚기 매우 어려운 상황이 생긴다.
				const FGenericTeamId ListenerTeam = FGenericTeamId::GetTeamIdentifier(AI);
				const FGenericTeamId NoiseTeam    = FGenericTeamId::GetTeamIdentifier(PlayerPawn);

				const ETeamAttitude::Type Attitude =
					FGenericTeamId::GetAttitude(ListenerTeam, NoiseTeam);

				const TCHAR* AttitudeName =
					(Attitude == ETeamAttitude::Hostile)  ? TEXT("적대") :
					(Attitude == ETeamAttitude::Friendly) ? TEXT("우호") : TEXT("중립");

				UE_LOG(LogMOUVoice, Log,
					TEXT("      플레이어에 대한 태도: %s (NPC 팀 %d vs 플레이어 팀 %d) ")
					TEXT("- 퍼셉션이 실제로 쓰는 판정이다"),
					AttitudeName, ListenerTeam.GetId(), NoiseTeam.GetId());
			}

			// ★★ 퍼셉션 컴포넌트는 하나라는 보장이 없다 ★★
			//
			// AAIController::GetPerceptionComponent() 는 PostRegisterAllComponents 에서
			// FindComponentByClass 로 **처음 찾은 하나**를 캐시한 것이다. 부모 BP 가
			// 이미 하나 갖고 있는데 자식에서 또 추가하면 두 개가 되고, 그러면
			// "설정을 넣은 컴포넌트" 와 "실제로 동작하는 컴포넌트" 가 갈린다.
			// 폰 쪽에 붙어 있을 수도 있다. 그래서 전부 훑는다.
			TArray<UAIPerceptionComponent*> PerceptionComps;
			AI->GetComponents(PerceptionComps);

			if (AIPawn)
			{
				TArray<UAIPerceptionComponent*> PawnComps;
				AIPawn->GetComponents(PawnComps);
				PerceptionComps.Append(PawnComps);
			}

			if (PerceptionComps.Num() == 0)
			{
				UE_LOG(LogMOUVoice, Warning,
					TEXT("      퍼셉션 컴포넌트가 하나도 없다."));
				continue;
			}

			if (PerceptionComps.Num() > 1)
			{
				UE_LOG(LogMOUVoice, Warning,
					TEXT("      ★ 퍼셉션 컴포넌트가 %d 개다. ")
					TEXT("설정을 넣은 것과 실제로 쓰이는 것이 다를 수 있다."),
					PerceptionComps.Num());
			}

			const UAIPerceptionComponent* Primary = AI->GetPerceptionComponent();

			for (UAIPerceptionComponent* Perception : PerceptionComps)
			{
				if (!Perception)
				{
					continue;
				}

				DumpPerceptionComponent(Perception, PlayerPawn, ModeLoudness,
					/*bIsPrimary=*/Perception == Primary);
			}
		}

		if (ControllerCount == 0)
		{
			UE_LOG(LogMOUVoice, Warning,
				TEXT("월드에 AAIController 가 하나도 없다. NPC 가 스폰됐는지 확인할 것."));
		}

		UE_LOG(LogMOUVoice, Log, TEXT("===================="));
	}

	FAutoConsoleCommandWithWorldAndArgs GVoiceFakeNoiseCommand(
		TEXT("MOU.Voice.FakeNoise"),
		TEXT("마이크 없이 내 위치에 소음 이벤트를 발생시킨다(NPC 청각 테스트용). ")
		TEXT("사용법: MOU.Voice.FakeNoise [반경] [음량] [태그=Voice.Proximity] ")
		TEXT("(생략하면 현재 발화 모드의 실제 값을 쓴다)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				const float MaxRange = Args.IsValidIndex(0) ? FCString::Atof(*Args[0]) : 0.f;
				const float Loudness = Args.IsValidIndex(1) ? FCString::Atof(*Args[1]) : 0.f;
				const FName Tag = Args.IsValidIndex(2) ? FName(*Args[2]) : FName(TEXT("Voice.Proximity"));

				ReportFakeNoise(World, MaxRange, Loudness, Tag);
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceHearDebugCommand(
		TEXT("MOU.Voice.HearDebug"),
		TEXT("월드의 모든 AI 의 청각 설정과 현재 감지 목록을 즉시 출력한다. ")
		TEXT("소음을 낸 직후를 보고 싶으면 MOU.Voice.HearTest 를 쓸 것."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				DumpHearingState(World);
			}));

	/** HearTest 의 지연 호출용. 명령이 전역이라 핸들도 전역으로 둔다. */
	FTimerHandle GHearTestTimerHandle;

	/** 남은 샘플 횟수. 0 이 되면 타이머를 끈다. */
	int32 GHearTestSamplesLeft = 0;

	/**
	 * 소음을 내고 **잠시 기다린 뒤** 진단을 찍는다.
	 *
	 * ★ FakeNoise 와 HearDebug 를 손으로 연달아 치면 거의 항상 "감지 없음" 이 나온다.
	 *   ReportNoiseEvent 는 이벤트를 큐에 넣을 뿐이고 실제 판정은 퍼셉션 시스템이
	 *   다음에 틱할 때 돈다. 그 사이에 목록을 보면 아직 비어 있는 게 정상이다.
	 *   이 함정 때문에 "설정은 다 맞는데 안 들린다" 로 오진하기 매우 쉬워서
	 *   두 동작을 한 명령으로 묶었다.
	 */
	FAutoConsoleCommandWithWorldAndArgs GVoiceHearTestCommand(
		TEXT("MOU.Voice.HearTest"),
		TEXT("소음을 내고 잠시 뒤 청각 진단을 자동으로 찍는다(권장). ")
		TEXT("사용법: MOU.Voice.HearTest [지연초=1.5] [반경] [음량]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (!World)
				{
					return;
				}

				const float Delay = Args.IsValidIndex(0)
					? FMath::Max(FCString::Atof(*Args[0]), 0.1f) : 1.5f;
				const float MaxRange = Args.IsValidIndex(1) ? FCString::Atof(*Args[1]) : 0.f;
				const float Loudness = Args.IsValidIndex(2) ? FCString::Atof(*Args[2]) : 0.f;

				if (!ReportFakeNoise(World, MaxRange, Loudness, FName(TEXT("Voice.Proximity"))))
				{
					return;
				}

				// ★ 한 번만 찍으면 **왔다가 사라지는 자극을 놓친다.**
				//   bForgetStaleActors=True 라서 자극이 만료되는 순간 액터 기록이
				//   통째로 지워진다. 한 번 찍은 결과가 비어 있다는 것만으로는
				//   "안 왔다" 인지 "왔다가 지워졌다" 인지 구분되지 않는다.
				//   그래서 소음 직후부터 여러 번 나눠 찍는다.
				constexpr int32 SampleCount = 4;
				const float Interval = Delay / SampleCount;

				GHearTestSamplesLeft = SampleCount;

				UE_LOG(LogMOUVoice, Log,
					TEXT("%.2f 초 간격으로 %d 번 청각 진단을 찍는다..."),
					Interval, SampleCount);

				// 약참조로 잡는다. 진단이 돌기 전에 PIE 가 끝나면 월드가 사라진다.
				TWeakObjectPtr<UWorld> WeakWorld(World);

				World->GetTimerManager().SetTimer(
					GHearTestTimerHandle,
					FTimerDelegate::CreateLambda([WeakWorld]()
					{
						UWorld* TimerWorld = WeakWorld.Get();

						if (!TimerWorld)
						{
							return;
						}

						UE_LOG(LogMOUVoice, Log, TEXT("--- 샘플 %d 회차 남음 ---"),
							GHearTestSamplesLeft);

						DumpHearingState(TimerWorld);

						if (--GHearTestSamplesLeft <= 0)
						{
							TimerWorld->GetTimerManager().ClearTimer(GHearTestTimerHandle);
						}
					}),
					Interval, /*bLoop=*/true);
			}));
}
