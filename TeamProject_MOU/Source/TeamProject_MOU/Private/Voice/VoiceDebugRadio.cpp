// MOU 음성 - 테스트용 무전기 액터 구현.
// ★ 임시 클래스다. 아이템 파트가 끝나면 지운다(헤더 상단 주석).

#include "Voice/VoiceDebugRadio.h"

#include "Voice/RadioComponent.h"
#include "Voice/VoiceTypes.h"

#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

AVoiceDebugRadio::AVoiceDebugRadio()
{
	// 디버그 표시용으로만 틱한다. 무전 기능 자체는 틱이 필요 없다.
	PrimaryActorTick.bCanEverTick = true;

	// ★ 반드시 복제돼야 한다.
	//   클라가 이 액터를 알아야 **거기에 사운드를 붙일 수 있다.**
	//   복제가 안 되면 서버는 무전을 보내는데 클라는 "무전기를 못 찾음" 으로
	//   조용히 재생을 건너뛴다.
	bReplicates = true;
	SetReplicateMovement(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	RadioComponent = CreateDefaultSubobject<URadioComponent>(TEXT("RadioComponent"));
}

AVoiceDebugRadio* AVoiceDebugRadio::FindHeldBy(APawn* OwnerPawn)
{
	if (!IsValid(OwnerPawn))
	{
		return nullptr;
	}

	TArray<AActor*> Attached;
	OwnerPawn->GetAttachedActors(Attached);

	for (AActor* Actor : Attached)
	{
		if (AVoiceDebugRadio* Radio = Cast<AVoiceDebugRadio>(Actor))
		{
			return Radio;
		}
	}

	return nullptr;
}

AVoiceDebugRadio* AVoiceDebugRadio::SpawnAttachedTo(APawn* OwnerPawn)
{
	if (!IsValid(OwnerPawn) || !OwnerPawn->HasAuthority())
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("테스트 무전기는 서버에서만 만들 수 있다."));
		return nullptr;
	}

	// ★ 이미 들고 있으면 새로 만들지 않는다.
	//   두 대를 들면 같은 무전이 두 번 재생된다(15절). 아이템 파트가 소지 개수를
	//   제한하겠지만, 테스트 도구가 그 규칙을 먼저 깨뜨리면 곤란하다.
	if (AVoiceDebugRadio* Existing = FindHeldBy(OwnerPawn))
	{
		UE_LOG(LogMOUVoice, Log, TEXT("이미 무전기를 들고 있다."));
		return Existing;
	}

	UWorld* World = OwnerPawn->GetWorld();

	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Owner = OwnerPawn;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoiceDebugRadio* Radio = World->SpawnActor<AVoiceDebugRadio>(
		AVoiceDebugRadio::StaticClass(), OwnerPawn->GetActorTransform(), Params);

	if (!Radio)
	{
		return nullptr;
	}

	// 폰에 붙인다. 이 부착 관계가 곧 "누가 들고 있는가" 의 근거가 된다
	// (URadioComponent::GetHolder 가 부착을 타고 올라가며 찾는다).
	Radio->AttachToActor(OwnerPawn, FAttachmentTransformRules::SnapToTargetNotIncludingScale);

	// ★ 손에 들었다고 알려줘야 송신 자격이 생긴다(FindUsableRadioFor 가 IsInHand
	//   를 본다). 테스트 무전기는 인벤토리 개념이 없으므로 스폰 = 손에 듦이다.
	if (Radio->RadioComponent)
	{
		Radio->RadioComponent->SetInHand(true);
	}

	UE_LOG(LogMOUVoice, Log,
		TEXT("테스트 무전기를 손에 들었다. MOU.Voice.Radio.Power 1 로 켤 것."));

	return Radio;
}

void AVoiceDebugRadio::DropHere()
{
	// 부착만 푼다. 위치는 그대로 두어 "그 자리에 놓았다" 가 된다.
	//
	// ★ 떨어뜨리면 꺼진다 (2026-08-20 결정, RadioComponent.h 상단 참고).
	//   "바닥에 떨어진 무전기가 계속 울려 NPC 를 유인한다" 는 설계는 폐기됐다 -
	//   NPC 는 무전기가 아니라 그것을 들고 있는 사람을 쫓는다.
	//   진짜 아이템(ARadio)은 Drop_Implementation 에서 같은 일을 한다.
	if (RadioComponent)
	{
		RadioComponent->SetInHand(false);
		RadioComponent->SetPowered(false);
	}

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	UE_LOG(LogMOUVoice, Log,
		TEXT("무전기를 바닥에 놓았다. **전원이 꺼져 더 이상 소리가 나지 않는다.**"));
}

void AVoiceDebugRadio::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

#if ENABLE_DRAW_DEBUG
	// 메시가 없어서 어디 있는지 안 보인다. 최소한의 표시만 그린다.
	// 켜져 있으면 초록, 꺼져 있으면 회색 - 전원 상태가 한눈에 보여야
	// "왜 무전이 안 가지" 를 바로 판단할 수 있다.
	if (!RadioComponent)
	{
		return;
	}

	const bool bOn = RadioComponent->IsPoweredOn();
	const FColor Color = bOn ? FColor::Green : FColor(90, 90, 90);

	DrawDebugSphere(GetWorld(), GetActorLocation(), 20.f, 12, Color, false, -1.f, 0, 2.f);

	if (bOn)
	{
		// 사람이 들을 수 있는 거리를 링으로 그린다.
		// V8 에서 NPC 반경(빨강)도 여기 같이 그리면 두 값의 차이가 눈에 보인다.
		DrawDebugCircle(GetWorld(), GetActorLocation(), RadioComponent->GetEffectiveHearRadius(),
			32, FColor::Green, false, -1.f, 0, 2.f,
			FVector(1, 0, 0), FVector(0, 1, 0), /*bDrawAxis=*/false);
	}
#endif
}