// MOU 음성 - 서버 라우팅 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7-2절(근접), 13절(반경), 14절 V3
//
// [스레드] 서버 게임 스레드 전용. 블로킹 I/O 를 하면 호스트 프레임이 떨어진다(11절).

#include "Voice/VoiceRouter.h"

#include "Voice/RadioComponent.h"
#include "Voice/VoiceComponent.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Perception/AISense_Hearing.h"

UVoiceRouter* UVoiceRouter::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UVoiceRouter>() : nullptr;
}

void UVoiceRouter::RouteFrame(APlayerController* SenderPC, const FVoiceFrame& Frame)
{
	UWorld* World = GetWorld();

	if (!World || !IsValid(SenderPC))
	{
		++FramesRejected;
		return;
	}

	// --- 1. 발신자 신원 확정 ------------------------------------------------
	//
	// ★ 클라가 보낸 값이 아니라 **RPC 를 받은 컨트롤러**에서 얻는다.
	//   이게 이 함수 전체에서 가장 중요한 줄이다 - 여기서 클라 값을 쓰면
	//   누구나 남을 사칭할 수 있게 된다.
	const APlayerState* SenderState = SenderPC->PlayerState;

	if (!SenderState)
	{
		// 접속 직후 PlayerState 가 아직 없는 순간. 곧 생긴다.
		++FramesRejected;
		return;
	}

	const int32 SpeakerId = SenderState->GetPlayerId();
	const double Now = FPlatformTime::Seconds();

	// --- 1-2. 사망자 송신 차단 (3중 방어의 2겹, ★ 진짜 방어선) ----------------
	//
	// 클라이언트도 죽으면 캡처를 멈추지만(1겹) 그건 **UX 이지 방어가 아니다.**
	// 개조 클라이언트는 그 검사를 지우고 계속 보낼 수 있다.
	// 서버가 여기서 버리는 것만이 실제로 막는 유일한 지점이다(8절).
	if (UVoiceComponent::IsPlayerVoiceDead(SenderPC))
	{
		++FramesFromDead;
		return;
	}

	// --- 2. 레이트 리밋 -----------------------------------------------------
	//
	// 크기 검사보다 **먼저** 한다. 홍수를 막는 것이 목적이므로, 버릴 프레임이라도
	// 개수는 세어야 한다. 뒤에 두면 규격 위반 프레임을 초당 수천 개 보내는
	// 공격이 리밋을 통째로 우회한다.
	if (!CheckRateLimit(SpeakerId, Now))
	{
		++FramesRateLimited;
		return;
	}

	++FramesRouted;

	// --- 3. 페이로드 검증 ---------------------------------------------------
	//
	// 여기서 걸리는 것은 접속을 끊을 만한 일은 아니다(VoiceComponent 의
	// GHardMaxOpusBytes 주석 참고). 조용히 버린다.
	if (Frame.Opus.Num() <= 0 || Frame.Opus.Num() > MOUVoice::MaxEncodedFrameBytes)
	{
		++FramesRejected;
		return;
	}

	// --- 4. 발신자 위치 -----------------------------------------------------
	const APawn* SenderPawn = SenderPC->GetPawn();

	if (!SenderPawn)
	{
		// 폰이 없으면 근접 음성을 낼 위치가 없다(관전 중 등).
		// 사망 검사는 이미 1-2 단계에서 끝났다.
		++FramesRejected;
		return;
	}

	const FVector SenderLocation = SenderPawn->GetActorLocation();
	const EVoiceMode Mode = MOUVoice::SanitizeMode(Frame.Mode);

	// --- 5. 내보낼 프레임 구성 ----------------------------------------------
	//
	// 여기 담기는 값은 전부 **서버가 확정한 것**이다. 그래서 받는 쪽은 이 값을
	// 그대로 믿고 재생해도 된다.
	FVoiceFrameOut Out;
	Out.SpeakerId = SpeakerId;
	Out.Seq       = Frame.Seq;
	Out.Mode      = Mode;
	Out.Loudness  = Frame.Loudness;
	Out.Opus      = Frame.Opus;

	// --- 7. 무전 자격 검사 (V6) ---------------------------------------------
	//
	// 클라가 무전을 요청했어도 **서버가 전부 다시 확인한다**(7-3절).
	// 클라 주장을 믿으면 무전기 없이도, 꺼진 채로도 무전을 칠 수 있게 된다.
	const bool bWantsRadio = (MOUVoice::SanitizeRoute(Frame.Route) == EVoiceRoute::Radio);
	bool bRadioAllowed = false;

	if (bWantsRadio)
	{
		// 들고 있고 켜져 있는 무전기가 있는가.
		if (FindUsableRadioFor(SenderState) == nullptr)
		{
			++FramesNoRadio;
		}
		// 반이중 - 지금 남이 송신 중이면 끼어들 수 없다.
		else if (!TryAcquireRadioChannel(SpeakerId, Now))
		{
			++FramesChannelBusy;
		}
		else
		{
			bRadioAllowed = true;
		}
	}

	// --- 8. 수신자 결정 -----------------------------------------------------
	//
	// ★ 순서가 중요하다: **근접을 먼저** 돌리고, 무전은 근접으로 이미 받은 사람을
	//   건너뛴다. 그게 "라우트 우선순위: 근접 > 무전" 의 실체다(7-3절).
	//
	//   무전을 치는 동안에도 **육성은 그대로 나간다**(2절). 그래서 가까이 있는
	//   사람은 두 조건에 다 걸리고, 둘 다 보내면 같은 목소리가 겹쳐 에코가 된다.
	//
	// 근접은 무전 자격과 무관하게 **항상** 돈다 - 무전기가 없어도, 채널이
	// 차 있어도 입으로 내는 소리는 나기 때문이다.
	TSet<APlayerController*> AlreadyRouted;

	Out.Route = EVoiceRoute::Proximity;
	Out.RadioActor = nullptr;
	FramesDelivered += RouteProximity(SenderPC, SenderLocation, Mode, Out, AlreadyRouted);

	if (bRadioAllowed)
	{
		Out.Route = EVoiceRoute::Radio;
		const int32 RadioDelivered = RouteRadio(SenderPC, Out, AlreadyRouted, Now);

		FramesDelivered += RadioDelivered;
		FramesRadio     += (RadioDelivered > 0) ? 1 : 0;
	}

	// --- 9. NPC 소음 발행 (V8) ----------------------------------------------
	//
	// ★ 여기가 "목소리가 게임플레이가 되는" 지점이다. 위의 라우팅은 사람에게
	//   들리게 하는 일이고, 이 한 줄이 **NPC 에게 들리게** 한다.
	//
	// ★ 사망자는 이 줄에 도달하지 못한다(1-2 단계에서 return). 죽은 사람의
	//   목소리로 NPC 를 끌어오는 일은 없다.
	//
	// 무전 수신 소음(무전기 스피커)은 여기가 아니라 RouteRadio 안에서 쏜다.
	// 위치가 발화자가 아니라 **무전기 액터**라서, 무전기를 순회하는 그쪽이
	// 자연스럽다. 바닥에 떨어진 무전기도 거기서 같이 처리된다.
	//
	// ★ 음량이 반경에 반영되는 지점이다. 클라가 보낸 정규화 강도를 그대로
	//   넘긴다 - 클라 감쇠와 **같은 값에서** 반경이 나와야 "화면의 원 = 사람이
	//   듣는 거리 = NPC 가 듣는 거리" 의 비율이 유지된다(VoiceTypes.h).
	ReportSpeakerNoise(SenderPawn, SpeakerId, Mode,
		MOUVoice::DequantizeLoudness(Frame.Loudness), bRadioAllowed, Now);
}

// ---------------------------------------------------------------------------
// 소음 이벤트 (V8)
// ---------------------------------------------------------------------------

void UVoiceRouter::ReportSpeakerNoise(const APawn* SenderPawn, int32 SpeakerId,
	EVoiceMode Mode, float Intensity01, bool bOnRadio, double Now)
{
	UWorld* World = GetWorld();

	if (!World || !SenderPawn)
	{
		return;
	}

	// --- 집계 창 ------------------------------------------------------------
	//
	// ★ 이 검사가 없으면 20ms 마다, 즉 초당 50회 x 말하는 사람 수만큼
	//   perception 갱신이 돈다. 몇 명만 동시에 떠들어도 서버가 주저앉는다(11절).
	if (const double* LastTime = LastSpeakerNoiseTime.Find(SpeakerId))
	{
		if (Now - *LastTime < MOUVoice::NoiseWindowSec)
		{
			return;
		}
	}

	LastSpeakerNoiseTime.Add(SpeakerId, Now);

	// --- 반경 --------------------------------------------------------------
	//
	// ★ 숫자를 여기 적으면 안 된다. MOUVoice:: 함수 하나에서만 나와야
	//   디버그 링(MOU.Voice.ShowRadius)과 실제 판정이 어긋나지 않는다.
	//
	// ★ 모드가 상한을 정하고, **실제로 얼마나 크게 말했는지가 그 아래로
	//   반경을 줄인다**(아날로그). 조용히 말하면 NPC 도 가까이서만 듣는다.
	//   클라 감쇠가 같은 함수·같은 강도를 쓰므로 사람 반경과 비율이 유지된다.
	float NoiseRange = MOUVoice::GetScaledNoiseRange(Mode, Intensity01);

	// 무전을 치는 중이면 무전기에 대고 낮게 말하므로 육성이 덜 퍼진다(7-5절).
	if (bOnRadio)
	{
		NoiseRange *= MOUVoice::RadioSpeakingNoiseScale;
	}

	// ★ Instigator 는 "소리가 난 위치에 있는 대상" 이다. NPC 는 이 값으로
	//   누구를 쫓을지 정한다 - 잘못 넣으면 엉뚱한 사람을 쫓는다(7-5절).
	UAISense_Hearing::ReportNoiseEvent(
		World,
		SenderPawn->GetActorLocation(),
		MOUVoice::NoiseEventLoudness,
		const_cast<APawn*>(SenderPawn),
		NoiseRange,
		bOnRadio ? MOUVoice::GetRadioNoiseTag() : MOUVoice::GetProximityNoiseTag());

	++NoiseEventsReported;
}

void UVoiceRouter::ReportRadioSpeakerNoise(URadioComponent* Radio, double Now)
{
	UWorld* World = GetWorld();
	AActor* RadioActor = Radio ? Radio->GetOwner() : nullptr;

	if (!World || !RadioActor)
	{
		return;
	}

	if (const double* LastTime = LastRadioNoiseTime.Find(Radio))
	{
		if (Now - *LastTime < MOUVoice::NoiseWindowSec)
		{
			return;
		}
	}

	LastRadioNoiseTime.Add(Radio, Now);

	// ★ 여기서 소리를 내는 것은 발화자가 아니라 **무전기**다.
	//   그래서 위치도 Instigator 도 무전기 쪽이다 - NPC 는 무전기를 들고 있는
	//   사람 쪽으로 온다(주인 없는 무전기는 애초에 여기까지 오지 않는다).
	//
	// ★ 위치는 GetSpeakerLocation() 을 쓴다. 사람이 듣는 기준점과 NPC 가 듣는
	//   기준점이 다르면 둘이 어긋난다.
	//
	// ★ 반경은 GetEffectiveNoiseRadius() 다. 인벤토리에 넣은 무전기는 NPC 에게도
	//   덜 들려야 한다 - 사람 반경만 줄이고 여기를 안 줄이면 "나한테는 조용한데
	//   NPC 는 똑같이 다 듣는" 이상한 상태가 된다.
	UAISense_Hearing::ReportNoiseEvent(
		World,
		Radio->GetSpeakerLocation(),
		MOUVoice::NoiseEventLoudness,
		RadioActor,
		Radio->GetEffectiveNoiseRadius() * Radio->SpeakerVolume * MOUVoice::RadioSpeakerNoiseScale,
		MOUVoice::GetRadioSpeakerNoiseTag());

	++NoiseEventsReported;
}

// ---------------------------------------------------------------------------
// 근접 라우팅
// ---------------------------------------------------------------------------

int32 UVoiceRouter::RouteProximity(APlayerController* SenderPC, const FVector& SenderLocation,
	EVoiceMode Mode, FVoiceFrameOut& Out, TSet<APlayerController*>& OutRouted)
{
	UWorld* World = GetWorld();

	if (!World)
	{
		return 0;
	}

	// ★ 반경 숫자를 여기 적으면 안 된다. MOUVoice:: 함수 하나에서만 나와야
	//   디버그 링(MOU.Voice.ShowRadius), 클라 감쇠, V8 의 NPC 소음 반경이
	//   서로 어긋나지 않는다(VoiceTypes.h 의 단일 진실 공급원 주석).
	const float Radius = MOUVoice::GetHearRadius(Mode) * MOUVoice::ProximityRoutingMargin;
	const float RadiusSquared = Radius * Radius;

	int32 Delivered = 0;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* ListenerPC = It->Get();

		if (!IsValid(ListenerPC) || ListenerPC == SenderPC)
		{
			// 자기 목소리는 자기에게 안 보낸다.
			// 자기 목소리를 듣고 싶으면 MOU.Voice.Loopback 을 쓴다(디버그 전용).
			continue;
		}

		// --- 사망자 수신 차단 (3중 방어의 3겹) ------------------------------
		//
		// ★ 송신만 막으면 **죽은 사람이 산 사람 대화를 계속 엿듣는다.**
		//   공포 게임에서 이건 단순한 버그가 아니라 게임을 망가뜨린다 -
		//   죽은 사람이 숨은 위치를 다 듣고 있게 된다(8절).
		if (UVoiceComponent::IsPlayerVoiceDead(ListenerPC))
		{
			continue;
		}

		const APawn* ListenerPawn = ListenerPC->GetPawn();

		if (!ListenerPawn)
		{
			continue;
		}

		// 제곱 거리로 비교한다. 매 프레임 x 인원수만큼 도는 자리라
		// 제곱근을 뽑을 이유가 없다.
		const float DistanceSquared =
			FVector::DistSquared(ListenerPawn->GetActorLocation(), SenderLocation);

		if (DistanceSquared > RadiusSquared)
		{
			continue;
		}

		if (UVoiceComponent* ListenerVoice = UVoiceComponent::Find(ListenerPC))
		{
			ListenerVoice->DeliverToOwner(Out);
			OutRouted.Add(ListenerPC);
			++Delivered;
		}
	}

	return Delivered;
}

// ---------------------------------------------------------------------------
// 무전 라우팅
// ---------------------------------------------------------------------------

int32 UVoiceRouter::RouteRadio(APlayerController* SenderPC, FVoiceFrameOut& Out,
	const TSet<APlayerController*>& AlreadyRouted, double Now)
{
	UWorld* World = GetWorld();

	if (!World)
	{
		return 0;
	}

	int32 Delivered = 0;

	// ★ 소지자당 무전기 한 대만 소리를 낸다.
	//
	//   두 대를 가지고 있으면 같은 무전이 두 번 재생돼 에코가 된다. 소지 개수
	//   제한을 아이템 쪽에 맡기지 않고 **여기서 보장한다** - 어떤 경로로 두 대를
	//   갖게 되든 소리가 겹치지 않는다. 어느 쪽이 울릴지는 정하지 않는다
	//   (어차피 같은 사람이 가진 무전기라 위치가 사실상 같다).
	TSet<const APlayerState*> HandledHolders;

	// ★ **무전기마다** 돌면서 그 무전기 주변 사람에게 보낸다.
	//
	//   "무전을 켠 사람에게 보낸다" 가 아니라 "켜진 무전기에서 소리가 나고,
	//   그 소리가 닿는 사람이 듣는다" 이다. 이 차이가 설계의 핵심이다(7-4절):
	//   무전기는 스피커라서 주인만이 아니라 **주변 사람도 듣는다.**
	for (int32 Index = PoweredRadios.Num() - 1; Index >= 0; --Index)
	{
		URadioComponent* Radio = PoweredRadios[Index].Get();

		if (!Radio || !Radio->IsPoweredOn())
		{
			// 파괴됐거나 꺼졌다. 순회하는 김에 정리한다.
			// 소음 타임스탬프도 같이 지운다 - 안 지우면 껐다 켤 때마다 항목이
			// 하나씩 쌓인다(무전기가 파괴된 경우엔 키가 이미 죽어서 못 지우는데,
			// 그건 죽은 약참조 하나라 무해하다).
			LastRadioNoiseTime.Remove(PoweredRadios[Index]);
			PoweredRadios.RemoveAtSwap(Index);
			continue;
		}

		const APlayerState* Holder = Radio->GetHolder();

		// ★ 주인이 없는 무전기는 소리를 내지 않는다 (2026-08-20 결정).
		//
		//   이전 설계는 바닥에 떨어진 무전기가 계속 울려 NPC 를 유인하는 것이었으나
		//   폐기됐다 - NPC 는 무전기가 아니라 **그것을 들고 있는 사람**을 쫓는다.
		//   드롭 시 ARadio 가 전원을 끄지만, 다른 경로로 주인만 사라진 경우
		//   (사망 처리가 Drop 을 안 태운 경우 등)에도 소리가 나지 않도록 여기서
		//   한 번 더 막는다.
		if (!Holder)
		{
			continue;
		}

		// 발신자 본인의 무전기에서는 소리가 나지 않는다.
		// 자기가 말한 것이 자기 무전기로 되돌아오면 그냥 에코다.
		if (SenderPC && Holder == SenderPC->PlayerState)
		{
			continue;
		}

		// 이 사람의 무전기는 이미 한 대 울렸다. 두 번 재생하지 않는다.
		if (HandledHolders.Contains(Holder))
		{
			continue;
		}

		HandledHolders.Add(Holder);

		const FVector SpeakerLocation = Radio->GetSpeakerLocation();

		// ★ SpeakerHearRadius 를 직접 읽지 않는다. 인벤토리에 들어간 무전기는
		//   반경이 줄어야 하고, 그 계산은 GetEffectiveHearRadius() 안에만 있다.
		const float HearRadius   = Radio->GetEffectiveHearRadius();
		const float HearRadiusSq = HearRadius * HearRadius;

		// 이 무전기에서 나는 소리라고 표시한다. 받는 쪽은 이 액터 위치에서 재생한다.
		Out.RadioActor = Radio->GetOwner();

		// --- NPC 소음 (V8) --------------------------------------------------
		//
		// ★ 사람에게 보내는 루프 **밖**, 무전기당 한 번이다. 안에 넣으면
		//   듣는 사람 수만큼 같은 소음이 중복 발행된다.
		//
		// ★ 듣는 사람이 하나도 없어도 쏜다. 무전기는 스피커라서 주변에 사람이
		//   없어도 NPC 에게는 들린다 - 무전을 받는 순간이 곧 위치가 새는 순간이다.
		//   (주인 없는 무전기는 위에서 이미 걸러졌으므로 여기 도달하지 않는다.)
		ReportRadioSpeakerNoise(Radio, Now);

		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* ListenerPC = It->Get();

			if (!IsValid(ListenerPC) || ListenerPC == SenderPC)
			{
				continue;
			}

			// ★ 근접으로 이미 받은 사람은 건너뛴다 - 라우트 우선순위(7-3절).
			//   안 그러면 발신자 근처에 있는 사람은 육성과 무전을 겹쳐 듣는다.
			if (AlreadyRouted.Contains(ListenerPC))
			{
				continue;
			}

			// 사망자는 못 듣는다(3겹). 무전기는 계속 소리를 내지만
			// 그 소리를 듣는 것은 **살아있는 사람과 NPC** 이지 죽은 주인이 아니다(8절).
			if (UVoiceComponent::IsPlayerVoiceDead(ListenerPC))
			{
				continue;
			}

			const APawn* ListenerPawn = ListenerPC->GetPawn();

			if (!ListenerPawn)
			{
				continue;
			}

			if (FVector::DistSquared(ListenerPawn->GetActorLocation(), SpeakerLocation) > HearRadiusSq)
			{
				continue;
			}

			if (UVoiceComponent* ListenerVoice = UVoiceComponent::Find(ListenerPC))
			{
				ListenerVoice->DeliverToOwner(Out);
				++Delivered;
			}
		}
	}

	return Delivered;
}

// ---------------------------------------------------------------------------
// 무전기 레지스트리
// ---------------------------------------------------------------------------

void UVoiceRouter::RegisterRadio(URadioComponent* Radio)
{
	if (!Radio)
	{
		return;
	}

	PoweredRadios.AddUnique(Radio);

	UE_LOG(LogMOUVoice, Verbose, TEXT("무전기 등록. 켜진 무전기 %d대."), PoweredRadios.Num());
}

void UVoiceRouter::UnregisterRadio(URadioComponent* Radio)
{
	if (!Radio)
	{
		return;
	}

	PoweredRadios.RemoveAll([Radio](const TWeakObjectPtr<URadioComponent>& Entry)
	{
		// 죽은 참조도 같이 치운다. 어차피 훑는 김에.
		return !Entry.IsValid() || Entry.Get() == Radio;
	});

	UE_LOG(LogMOUVoice, Verbose, TEXT("무전기 해제. 켜진 무전기 %d대."), PoweredRadios.Num());
}

URadioComponent* UVoiceRouter::FindUsableRadioFor(const APlayerState* Holder) const
{
	if (!Holder)
	{
		return nullptr;
	}

	for (const TWeakObjectPtr<URadioComponent>& Entry : PoweredRadios)
	{
		URadioComponent* Radio = Entry.Get();

		// ★ IsInHand() 가 송신 자격의 핵심이다.
		//   인벤토리에 넣은 무전기는 **수신만 된다** - 무전을 치려면 손에 들어야
		//   한다. 이 검사를 빼면 가방에 넣은 채로 무전을 칠 수 있게 된다.
		if (Radio && Radio->IsPoweredOn() && Radio->IsInHand() && Radio->GetHolder() == Holder)
		{
			// ★ 두 대 이상 들고 있으면 **가장 먼저 찾은 것만** 쓴다(15절).
			//   전부 쓰면 같은 소리가 두 번 난다.
			return Radio;
		}
	}

	return nullptr;
}

// ---------------------------------------------------------------------------
// 반이중 채널
// ---------------------------------------------------------------------------

bool UVoiceRouter::TryAcquireRadioChannel(int32 SpeakerId, double Now)
{
	const bool bChannelFree =
		(RadioChannelOwnerId == INDEX_NONE) ||
		(RadioChannelOwnerId == SpeakerId) ||
		((Now - RadioChannelLastFrameTime) > MOUVoice::RadioChannelHoldSeconds);

	if (!bChannelFree)
	{
		// >>> 여기서 요청자에게 "채널 사용 중" 삑 소리를 한 번 보내면 좋다(7-3절).
		//     지금은 조용히 버린다 - 소리를 보내려면 별도 RPC 와 사운드 에셋이
		//     필요한데, 그건 V7 의 스퀄치 작업과 같이 하는 편이 자연스럽다.
		return false;
	}

	if (RadioChannelOwnerId != SpeakerId)
	{
		UE_LOG(LogMOUVoice, Verbose, TEXT("무전 채널 점유: 플레이어 %d"), SpeakerId);
	}

	RadioChannelOwnerId = SpeakerId;
	RadioChannelLastFrameTime = Now;

	return true;
}

// ---------------------------------------------------------------------------
// 레이트 리밋
// ---------------------------------------------------------------------------

bool UVoiceRouter::CheckRateLimit(int32 SpeakerId, double Now)
{
	FRateWindow& Window = RateWindows.FindOrAdd(SpeakerId);

	// 새 창 시작. 처음 만들어진 항목은 WindowStartTime 이 0 이라 여기서 초기화된다.
	if ((Now - Window.WindowStartTime) >= 1.0)
	{
		Window.WindowStartTime    = Now;
		Window.FrameCount         = 0;
		Window.bWarnedThisWindow  = false;
	}

	++Window.FrameCount;

	if (Window.FrameCount <= MOUVoice::MaxFramesPerSecPerPlayer)
	{
		return true;
	}

	// 창마다 한 번만 경고한다. 매 프레임 남기면 초당 수천 줄이 찍혀
	// **로그를 보느라 서버가 더 느려진다** - 공격을 도와주는 꼴이 된다.
	if (!Window.bWarnedThisWindow)
	{
		Window.bWarnedThisWindow = true;
		UE_LOG(LogMOUVoice, Warning,
			TEXT("플레이어 %d 의 음성 전송이 초당 %d 프레임을 넘었다(정상값 50). ")
			TEXT("초과분은 버린다."),
			SpeakerId, MOUVoice::MaxFramesPerSecPerPlayer);
	}

	return false;
}

FString UVoiceRouter::GetStatsString() const
{
	return FString::Printf(
		TEXT("라우팅=%d 전달=%d 리밋=%d 거부=%d 사망차단=%d 발신자=%d명 ")
		TEXT("| 무전=%d 켜진무전기=%d 채널점유중=%d 무전기없음=%d ")
		TEXT("| NPC소음=%d"),
		FramesRouted, FramesDelivered, FramesRateLimited, FramesRejected,
		FramesFromDead, RateWindows.Num(),
		FramesRadio, PoweredRadios.Num(), FramesChannelBusy, FramesNoRadio,
		NoiseEventsReported);
}
