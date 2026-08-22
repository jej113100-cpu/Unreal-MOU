// MOU 음성 - 무전기 컴포넌트 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7-3절, 7-4절, 10절
//
// [스레드] 게임 스레드 전용.

#include "Voice/RadioComponent.h"

#include "Voice/VoiceRouter.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

URadioComponent::URadioComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// 전원 상태가 클라에 복제돼야 한다. 이게 없으면 클라는 무전기가 켜졌는지
	// 알 수 없어서 UI 도, V7 의 잡음도 못 낸다.
	SetIsReplicatedByDefault(true);
}

void URadioComponent::BeginPlay()
{
	Super::BeginPlay();

	// 에디터에서 bPoweredOn=true 로 배치해둔 무전기를 위해 한 번 맞춰준다.
	// (기본값은 꺼짐이라 대부분 아무 일도 하지 않는다)
	UpdateRegistration();
}

void URadioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// ★ 반드시 레지스트리에서 빠진다.
	//   안 빼면 파괴된 무전기가 목록에 남아 라우터가 매번 죽은 포인터를 만난다.
	//   TWeakObjectPtr 라 크래시는 안 나지만 목록이 계속 자라고, 무엇보다
	//   **없어진 무전기에서 소리가 나려고 시도한다.**
	if (UVoiceRouter* Router = UVoiceRouter::Get(GetWorld()))
	{
		Router->UnregisterRadio(this);
	}
	bRegistered = false;

	Super::EndPlay(EndPlayReason);
}

void URadioComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(URadioComponent, bPoweredOn);
	DOREPLIFETIME(URadioComponent, bInHand);
}

// ---------------------------------------------------------------------------
// 전원
// ---------------------------------------------------------------------------

void URadioComponent::SetPowered(bool bOn)
{
	AActor* Owner = GetOwner();

	// 서버 권위다. 클라가 켜봐야 다음 복제 때 덮어써지고, 그 사이에
	// "나만 켜진 줄 아는" 어긋난 상태가 생긴다.
	if (!Owner || !Owner->HasAuthority())
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("URadioComponent::SetPowered 는 서버에서만 부를 수 있다."));
		return;
	}

	if (bPoweredOn == bOn)
	{
		return;
	}

	bPoweredOn = bOn;

	// 서버에서는 OnRep 이 안 불리므로 직접 부른다(연출을 호스트도 봐야 한다).
	OnRep_Power();

	UpdateRegistration();

	UE_LOG(LogMOUVoice, Log, TEXT("무전기(%s) 전원 %s."),
		*GetNameSafe(Owner), bOn ? TEXT("ON") : TEXT("OFF (무전망에서 완전히 빠짐)"));
}

void URadioComponent::OnRep_Power()
{
	// 지금은 로그만. V7 에서 여기가 "켜지면 노이즈 베드 재생 / 꺼지면 정지" 가 된다.
	UE_LOG(LogMOUVoice, Verbose, TEXT("무전기 전원 표시 갱신: %s"),
		bPoweredOn ? TEXT("ON") : TEXT("OFF"));
}

void URadioComponent::UpdateRegistration()
{
	AActor* Owner = GetOwner();

	// 레지스트리는 **서버의 것**이다. 클라가 등록하면 아무 의미가 없다
	// (클라의 라우터는 라우팅을 하지 않는다).
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	UVoiceRouter* Router = UVoiceRouter::Get(GetWorld());

	if (!Router)
	{
		return;
	}

	if (bPoweredOn && !bRegistered)
	{
		Router->RegisterRadio(this);
		bRegistered = true;
	}
	else if (!bPoweredOn && bRegistered)
	{
		Router->UnregisterRadio(this);
		bRegistered = false;
	}
}

// ---------------------------------------------------------------------------
// 손에 듦 / 인벤토리
// ---------------------------------------------------------------------------

void URadioComponent::SetInHand(bool bNowInHand)
{
	AActor* Owner = GetOwner();

	// 전원과 같은 이유로 서버 권위다. 클라가 "나 손에 들었어" 라고 주장해서
	// 송신 자격이 생기면 인벤토리에 넣은 채로 무전을 칠 수 있게 된다.
	if (!Owner || !Owner->HasAuthority())
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("URadioComponent::SetInHand 는 서버에서만 부를 수 있다."));
		return;
	}

	if (bInHand == bNowInHand)
	{
		return;
	}

	bInHand = bNowInHand;

	UE_LOG(LogMOUVoice, Verbose, TEXT("무전기(%s) %s."),
		*GetNameSafe(Owner),
		bNowInHand ? TEXT("손에 듦 (송신 가능)") : TEXT("인벤토리 (수신만)"));
}

float URadioComponent::GetEffectiveHearRadius() const
{
	// 인벤토리에 있으면 가방 안에서 웅얼거리는 소리라 덜 퍼진다.
	return bInHand ? SpeakerHearRadius : SpeakerHearRadius * StowedRadiusScale;
}

float URadioComponent::GetEffectiveNoiseRadius() const
{
	return bInHand ? SpeakerNoiseRadius : SpeakerNoiseRadius * StowedRadiusScale;
}

// ---------------------------------------------------------------------------
// 조회
// ---------------------------------------------------------------------------

APlayerState* URadioComponent::GetHolder() const
{
	const AActor* Owner = GetOwner();

	if (!Owner)
	{
		return nullptr;
	}

	// 부착된 액터를 타고 올라가며 폰을 찾는다.
	//
	// 아이템이 캐릭터 메시의 소켓에 붙는 구조라 한 단계일 수도, 여러 단계일 수도
	// 있다. 몇 단계인지 가정하지 않고 올라간다 - 아이템 파트의 부착 방식이
	// 바뀌어도 여기가 안 깨지게 하기 위해서다.
	for (const AActor* Current = Owner->GetAttachParentActor();
		Current != nullptr;
		Current = Current->GetAttachParentActor())
	{
		if (const APawn* Pawn = Cast<APawn>(Current))
		{
			return Pawn->GetPlayerState();
		}
	}

	// 아무 폰에도 안 붙어 있다 = 바닥에 떨어져 있다.
	return nullptr;
}

FVector URadioComponent::GetSpeakerLocation() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}