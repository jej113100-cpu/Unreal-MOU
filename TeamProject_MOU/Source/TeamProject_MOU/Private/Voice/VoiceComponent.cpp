// MOU 음성 - 네트워크 창구 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 5절, 10절, 14절 V3
//
// [스레드] 전부 게임 스레드다.

#include "Voice/VoiceComponent.h"

#include "Voice/RadioComponent.h"
#include "Voice/VoiceDebugRadio.h"
#include "Voice/VoicePlaybackComponent.h"
#include "Voice/VoiceRouter.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

namespace
{
	/**
	 * _Validate 에서 쓰는 **하드 상한.** 정상 상한(MaxEncodedFrameBytes)보다 훨씬 크다.
	 *
	 * ★ 왜 두 단계로 나눴는가 - `_Validate` 가 false 를 돌려주면 언리얼이
	 *   **그 클라이언트를 접속 종료시킨다.** 정상 상한(128)으로 여기서 끊으면
	 *   비트레이트 설정이 조금 어긋난 팀원이 게임에서 튕겨나간다.
	 *
	 *   그래서 역할을 나눴다:
	 *     _Validate     "이건 정상 클라이언트가 만들 수 있는 값이 아니다" → 끊는다
	 *     RouteFrame    "규격을 벗어났다" → 프레임만 조용히 버린다
	 */
	constexpr int32 GHardMaxOpusBytes = 512;
}

UVoiceComponent::UVoiceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// ★ 컴포넌트가 RPC 를 주고받으려면 리플리케이트 표시가 있어야 한다.
	//   빼먹으면 컴파일도 되고 호출도 되는데 **아무 일도 일어나지 않는다** -
	//   에러 없이 조용히 사라지는 종류라 원인 찾기가 고약하다.
	SetIsReplicatedByDefault(true);
}

UVoiceComponent* UVoiceComponent::Find(APlayerController* PC)
{
	return IsValid(PC) ? PC->FindComponentByClass<UVoiceComponent>() : nullptr;
}

APlayerController* UVoiceComponent::GetOwningPlayerController() const
{
	return Cast<APlayerController>(GetOwner());
}

void UVoiceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 소유자에게만 보내도 될 것 같지만 **모두에게 보낸다.**
	// 나중에 "누가 죽었는지" 를 남의 화면(관전 UI, 팀 표시)에서도 알아야 하고,
	// bool 하나라 대역폭이 문제되지 않는다.
	DOREPLIFETIME(UVoiceComponent, bVoiceDead);
}

// ---------------------------------------------------------------------------
// 사망 상태 (V5)
// ---------------------------------------------------------------------------

bool UVoiceComponent::IsPlayerVoiceDead(APlayerController* PC)
{
	const UVoiceComponent* Voice = Find(PC);

	// 음성 컴포넌트가 없으면 사망 여부를 알 수 없다. **살아있는 것으로 본다.**
	//
	// 반대로(없으면 죽은 것으로) 잡으면, 컴포넌트가 없는 순간 - 접속 직후나
	// 다른 PlayerController 클래스를 쓰는 경우 - 그 사람의 음성이 통째로
	// 조용히 사라진다. 원인을 찾기 매우 어려운 종류다.
	return Voice ? Voice->IsVoiceDead() : false;
}

void UVoiceComponent::SetVoiceDeadAuthoritative(bool bDead)
{
	// 서버 전용이다. 클라에서 바꿔봐야 다음 복제 때 덮어써지고,
	// 그 사이에 "나만 죽은 줄 아는" 어긋난 상태가 생긴다.
	if (GetOwnerRole() != ROLE_Authority)
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("SetVoiceDeadAuthoritative 는 서버에서만 부를 수 있다. ")
			TEXT("클라에서는 ServerSetVoiceDead 를 쓸 것."));
		return;
	}

	if (bVoiceDead == bDead)
	{
		return;
	}

	bVoiceDead = bDead;

	UE_LOG(LogMOUVoice, Log, TEXT("[서버] 플레이어 음성 상태 = %s"),
		bDead ? TEXT("사망(말하기·듣기 모두 차단)") : TEXT("생존"));
}

void UVoiceComponent::ServerSetVoiceDead_Implementation(bool bDead)
{
	SetVoiceDeadAuthoritative(bDead);
}

// ---------------------------------------------------------------------------
// 무전기 테스트용 (V6) - 아이템 파트가 끝나면 지운다
// ---------------------------------------------------------------------------

void UVoiceComponent::ServerDebugSpawnRadio_Implementation()
{
	APlayerController* PC = GetOwningPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	if (!Pawn)
	{
		UE_LOG(LogMOUVoice, Warning, TEXT("폰이 없어 테스트 무전기를 만들 수 없다."));
		return;
	}

	AVoiceDebugRadio::SpawnAttachedTo(Pawn);
}

void UVoiceComponent::ServerDebugDropRadio_Implementation()
{
	APlayerController* PC = GetOwningPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	if (AVoiceDebugRadio* Radio = AVoiceDebugRadio::FindHeldBy(Pawn))
	{
		Radio->DropHere();
	}
	else
	{
		UE_LOG(LogMOUVoice, Warning, TEXT("들고 있는 테스트 무전기가 없다."));
	}
}

void UVoiceComponent::ServerDebugSetRadioPower_Implementation(bool bOn)
{
	APlayerController* PC = GetOwningPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	AVoiceDebugRadio* Radio = AVoiceDebugRadio::FindHeldBy(Pawn);

	if (!Radio || !Radio->RadioComponent)
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("무전기가 없다. MOU.Voice.Radio.Spawn 으로 먼저 만들 것."));
		return;
	}

	Radio->RadioComponent->SetPowered(bOn);
}

// ---------------------------------------------------------------------------
// 보내는 쪽
// ---------------------------------------------------------------------------

void UVoiceComponent::SendVoiceFrame(const TArray<uint8>& Opus, float Loudness01,
	EVoiceMode Mode, EVoiceRoute Route)
{
	if (Opus.Num() <= 0)
	{
		return;
	}

	FVoiceFrame Frame;
	Frame.Seq      = NextSeq++;   // uint16 순환은 받는 쪽이 견딘다(헤더 주석)
	Frame.Route    = Route;
	Frame.Mode     = Mode;
	Frame.Loudness = MOUVoice::QuantizeLoudness(Loudness01);
	Frame.Opus     = Opus;

	// 호스트에서는 이 호출이 네트워크를 타지 않고 그 자리에서 실행된다.
	// 그래도 코드는 똑같다 - 헤더 상단의 리슨서버 주석 참고.
	ServerSendVoiceFrame(Frame);

	++FramesSent;
}

// ---------------------------------------------------------------------------
// RPC: 클라 -> 서버
// ---------------------------------------------------------------------------

bool UVoiceComponent::ServerSendVoiceFrame_Validate(const FVoiceFrame& Frame)
{
	// 여기서 false 를 돌려주면 **클라이언트가 튕긴다.** 그래서 "정상 클라이언트라면
	// 절대 만들 수 없는 값" 만 걸러낸다. 규격 위반 정도는 라우터가 조용히 버린다.
	return Frame.Opus.Num() <= GHardMaxOpusBytes;
}

void UVoiceComponent::ServerSendVoiceFrame_Implementation(const FVoiceFrame& Frame)
{
	// 여기는 서버다(호스트에서는 직접 호출, 원격 클라에서는 RPC 수신).
	UVoiceRouter* Router = UVoiceRouter::Get(GetWorld());

	if (!Router)
	{
		++FramesRejected;
		return;
	}

	// ★ 발신자를 인자로 넘기지 않고 **이 컴포넌트의 소유자**를 넘긴다.
	//   클라가 자기 신원을 주장할 여지를 아예 만들지 않는 것이 요점이다.
	Router->RouteFrame(GetOwningPlayerController(), Frame);
}

// ---------------------------------------------------------------------------
// RPC: 서버 -> 클라
// ---------------------------------------------------------------------------

void UVoiceComponent::DeliverToOwner(const FVoiceFrameOut& Frame)
{
	ClientReceiveVoiceFrame(Frame);
	++FramesDelivered;
}

void UVoiceComponent::ClientReceiveVoiceFrame_Implementation(const FVoiceFrameOut& Frame)
{
	// 여기는 이 컨트롤러를 소유한 클라이언트다.
	// (호스트 자신에게 보낸 경우에도 여기로 온다 - 헤더 상단 리슨서버 주석)
	APlayerController* OwnerPC = GetOwningPlayerController();

	UVoicePlaybackComponent* Playback = UVoicePlaybackComponent::FindOrCreate(OwnerPC);

	if (!Playback)
	{
		// FindOrCreate 는 로컬 컨트롤러가 아니면 null 을 준다.
		// 정상 경로에서는 올 수 없다 - Client RPC 는 소유 클라에서만 실행되므로.
		return;
	}

	Playback->HandleFrame(Frame);
}
