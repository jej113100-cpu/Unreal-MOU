// MOU - 무전기 아이템 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 6절, 7-3절
//
// [스레드] 게임 스레드 전용.

#include "Voice/Radio.h"

#include "Voice/RadioComponent.h"
#include "Voice/VoiceSubsystem.h"
#include "Voice/VoiceTypes.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

namespace
{
	/** 배터리를 몇 초마다 깎을지. 1초면 UI 게이지가 충분히 부드럽고 서버도 한가하다. */
	constexpr float BatteryTickInterval = 1.0f;
}

ARadio::ARadio()
{
	// 무전 기능도 배터리도 틱이 필요 없다. 배터리는 타이머로 깎는다.
	PrimaryActorTick.bCanEverTick = false;

	// ★ 음성 시스템이 아이템에 요구하는 것은 이 한 줄이 전부다.
	//   (bReplicates / SetReplicateMovement 는 AItemBase 생성자가 이미 켜뒀다.
	//    복제가 안 되면 클라가 액터를 몰라서 **거기에 소리를 붙일 수 없다.**)
	RadioComponent = CreateDefaultSubobject<URadioComponent>(TEXT("RadioComponent"));

	ItemName = NSLOCTEXT("Item", "RadioName", "무전기");
}

void ARadio::BeginPlay()
{
	// AItemBase::BeginPlay 가 CurrentDurability = MaxDurability 로 배터리를 채운다.
	Super::BeginPlay();
}

void ARadio::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 타이머를 남기면 파괴된 무전기의 배터리가 계속 깎인다.
	// (URadioComponent 쪽 레지스트리 해제는 그쪽 EndPlay 가 알아서 한다.)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BatteryTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
// 찾기
// ---------------------------------------------------------------------------

ARadio* ARadio::FindCarriedBy(const APawn* Pawn)
{
	if (!IsValid(Pawn))
	{
		return nullptr;
	}

	// ★ 재귀로 훑는다. 손에 든 무전기는 캐릭터 메시의 소켓에 붙어 한 단계
	//   더 들어가 있을 수 있다 - 몇 단계인지 가정하지 않는다
	//   (URadioComponent::GetHolder 가 반대 방향으로 올라가는 것과 같은 이유다).
	TArray<AActor*> Attached;
	Pawn->GetAttachedActors(Attached, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/true);

	for (AActor* Actor : Attached)
	{
		if (ARadio* Radio = Cast<ARadio>(Actor))
		{
			return Radio;
		}
	}

	return nullptr;
}

// ---------------------------------------------------------------------------
// 조회
// ---------------------------------------------------------------------------

bool ARadio::IsPoweredOn() const
{
	return RadioComponent && RadioComponent->IsPoweredOn();
}

bool ARadio::IsInHand() const
{
	return RadioComponent && RadioComponent->IsInHand();
}

bool ARadio::IsLocalPlayerActor(const AActor* Actor)
{
	const APawn* Pawn = Cast<APawn>(Actor);
	return Pawn && Pawn->IsLocallyControlled();
}

// ---------------------------------------------------------------------------
// 배터리
// ---------------------------------------------------------------------------

float ARadio::GetBatteryPercent() const
{
	return (MaxDurability > 0.f) ? FMath::Clamp(CurrentDurability / MaxDurability, 0.f, 1.f) : 0.f;
}

bool ARadio::HasBattery() const
{
	return CurrentDurability > 0.f;
}

void ARadio::RechargeBattery(float Amount)
{
	if (!HasAuthority() || Amount <= 0.f)
	{
		return;
	}

	// CurrentDurability 는 복제되므로 서버에서 바꾸면 클라 UI 까지 따라온다.
	CurrentDurability = FMath::Clamp(CurrentDurability + Amount, 0.f, MaxDurability);
}

void ARadio::DrainBattery()
{
	if (!HasAuthority())
	{
		return;
	}

	CurrentDurability = FMath::Max(0.f, CurrentDurability - BatteryDrainPerSecond * BatteryTickInterval);

	if (CurrentDurability > 0.f)
	{
		return;
	}

	// ★ 방전도 결국 "라우터 레지스트리에서 빠지는 것" 이다. 음소거가 아니라
	//   기기 종료라서, 방전된 무전기에는 프레임이 아예 오지 않는다.
	UE_LOG(LogMOUVoice, Log, TEXT("무전기(%s) 배터리 방전. 전원이 꺼진다."), *GetName());

	ApplyPoweredOnServer(false);
}

// ---------------------------------------------------------------------------
// 전원
// ---------------------------------------------------------------------------

void ARadio::SetPowered(bool bOn)
{
	if (!RadioComponent)
	{
		return;
	}

	// 인벤토리의 무전기는 수신 전용이다. UI를 거치지 않는 호출도 이 규칙을
	// 따라야 하므로, 전원 변경의 공용 진입점에서 막는다.
	if (!RadioComponent->IsInHand())
	{
		return;
	}

	// 전원 판정은 서버 권위다(RadioComponent.h 의 ★★).
	// 클라에서 불렸으면 서버로 넘기고, 결과는 복제로 돌아온다.
	if (HasAuthority())
	{
		ApplyPoweredOnServer(bOn);
	}
	else
	{
		ServerSetPowered(bOn);
	}
}

void ARadio::ServerSetPowered_Implementation(bool bOn)
{
	// 클라이언트 UI를 우회한 RPC도 서버에서 다시 검증한다.
	if (!RadioComponent || !RadioComponent->IsInHand())
	{
		return;
	}

	ApplyPoweredOnServer(bOn);
}

void ARadio::ApplyPoweredOnServer(bool bOn)
{
	if (!HasAuthority() || !RadioComponent)
	{
		return;
	}

	// 방전된 무전기는 켜지지 않는다. 이 검사가 없으면 켜자마자 다음 타이머
	// 틱에서 다시 꺼지는 깜빡임이 생긴다.
	if (bOn && !HasBattery())
	{
		UE_LOG(LogMOUVoice, Log, TEXT("무전기(%s) 배터리가 없어 켜지지 않는다."), *GetName());
		return;
	}

	RadioComponent->SetPowered(bOn);

	UWorld* World = GetWorld();

	if (!World)
	{
		return;
	}

	// 배터리는 **켜져 있는 동안에만** 닳는다. 손에 들었는지 인벤토리에 있는지는
	// 보지 않는다 - 넣어둬도 닳아야 "켜둘까 말까" 가 선택이 된다.
	if (bOn)
	{
		World->GetTimerManager().SetTimer(
			BatteryTimerHandle, this, &ARadio::DrainBattery, BatteryTickInterval, /*bLoop=*/true);
	}
	else
	{
		World->GetTimerManager().ClearTimer(BatteryTimerHandle);
	}
}

void ARadio::TogglePower()
{
	// 클라에서도 IsPoweredOn 은 복제된 값이라 읽어도 된다.
	// 어긋나더라도 서버가 최종 판정을 하므로 다음 복제 때 맞춰진다.
	SetPowered(!IsPoweredOn());
}

// ---------------------------------------------------------------------------
// 송신 (PTT)
// ---------------------------------------------------------------------------

void ARadio::StartTransmit()
{
	// 마이크는 각자 자기 컴퓨터에 있다. 서브시스템이 없으면(데디케이티드 서버 등)
	// 여기서 할 일이 없다.
	//
	// 인벤토리에서는 수신만 가능하다. 서버 라우터도 다시 확인하지만,
	// 로컬 송신 요청을 켜지 않아야 UI와 실제 동작이 일치한다.
	if (!RadioComponent || !RadioComponent->IsInHand() || !RadioComponent->IsPoweredOn())
	{
		StopTransmit();
		return;
	}
	if (UVoiceSubsystem* Voice = UVoiceSubsystem::Get(this))
	{
		Voice->SetRadioTransmitting(true);
	}
}

void ARadio::StopTransmit()
{
	if (UVoiceSubsystem* Voice = UVoiceSubsystem::Get(this))
	{
		Voice->SetRadioTransmitting(false);
	}
}

// ---------------------------------------------------------------------------
// AItemBase 오버라이드
// ---------------------------------------------------------------------------

void ARadio::OnUse_Implementation()
{
	// ★ 비어 있는 것이 맞다. 무전기는 좌클릭으로 조작하지 않는다(헤더 상단 ★).
	//
	//   전원은 Z, 송신은 X 다. 여기서 전원을 토글하면 Z 와 중복될 뿐 아니라
	//   무기를 쏘려다, 문을 열려다 무전기가 켜지는 오조작이 난다 - 이 게임에서
	//   무전기가 켜지는 것은 곧 위치가 새는 것이다.
	//
	//   Super 도 부르지 않는다. AItemBase::OnUse 는 CurrentUseCount 를 깎지만
	//   무전기가 닳는 것은 배터리이고, 그건 시간이 깎는다(DrainBattery).
}

void ARadio::PickUp_Implementation(AActor* Picker)
{
	Super::PickUp_Implementation(Picker);

	// PickUp 은 서버에서 실행된다. Owner 를 든 사람으로 잡아야 그 클라가
	// ServerSetPowered 를 보낼 수 있다.
	if (HasAuthority() && Picker)
	{
		SetOwner(Picker);

		// ★ 줍기도 "손에 듦" 이다. OnEquipped 만 믿으면 안 된다.
		//
		//   OnEquipped 는 **인벤토리 슬롯에서 꺼낼 때만** 불린다
		//   (UInventoryComponent::HandleSlotAction). 바닥에서 E 로 줍는 경로는
		//   UCarryingComponent::GrabOrDrop 이 PickUp 을 부른 뒤 곧바로
		//   CarrySocket 에 붙이는데, 그 경로에는 OnEquipped 가 없다.
		//
		//   이 한 줄이 없으면 **주운 무전기는 손에 들려 있는데도 송신이 안 된다** -
		//   라우터가 IsInHand() 를 보고 거부하기 때문이다(FindUsableRadioFor).
		//   증상은 "X 를 눌러도 아무 일도 안 일어난다" 뿐이라 원인을 찾기가 아주 어렵다.
		//   인벤토리에 한 번 넣었다 빼야 고쳐지는데, 그걸 알아낼 방법이 없다.
		if (RadioComponent)
		{
			RadioComponent->SetInHand(true);
		}
	}
}

void ARadio::Drop_Implementation(FVector DropLocation, AActor* Dropper)
{
	// ★ 떨어뜨리면 무조건 꺼진다. 자발적(G키)이든 비자발적(사망·기절)이든
	//   구분하지 않는다 - 주인 없는 무전기가 소리를 내는 일은 없다.
	if (HasAuthority())
	{
		ApplyPoweredOnServer(false);

		if (RadioComponent)
		{
			RadioComponent->SetInHand(false);
		}

		SetOwner(nullptr);
	}

	// 내가 들고 있던 무전기를 놓는 것이면 송신도 끊는다.
	if (IsLocalPlayerActor(Dropper) || IsLocalPlayerActor(LastOwner))
	{
		StopTransmit();
	}

	Super::Drop_Implementation(DropLocation, Dropper);
}

void ARadio::Throw_Implementation(FVector ThrowVelocity, AActor* Thrower)
{
	// Drop 과 같은 규칙이다.
	if (HasAuthority())
	{
		ApplyPoweredOnServer(false);

		if (RadioComponent)
		{
			RadioComponent->SetInHand(false);
		}

		SetOwner(nullptr);
	}

	if (IsLocalPlayerActor(Thrower) || IsLocalPlayerActor(LastOwner))
	{
		StopTransmit();
	}

	Super::Throw_Implementation(ThrowVelocity, Thrower);
}

void ARadio::OnEquipped_Implementation(AActor* Equipper)
{
	Super::OnEquipped_Implementation(Equipper);

	// 손에 들었다 = 송신 자격이 생긴다. **전원은 건드리지 않는다** -
	// 켜는 것은 언제나 플레이어가 직접 하는 조작이다.
	if (HasAuthority() && RadioComponent)
	{
		RadioComponent->SetInHand(true);
	}
}

void ARadio::OnUnequipped_Implementation(AActor* Equipper)
{
	// 인벤토리로 들어간다 = 송신 자격이 사라진다. 수신은 계속되므로
	// **전원은 그대로 둔다**(배터리도 계속 닳는다).
	if (HasAuthority() && RadioComponent)
	{
		RadioComponent->SetInHand(false);
	}

	// ★ 이 함수는 멀티캐스트로 모든 클라에서 불린다. 검사 없이 송신을 끊으면
	//   남이 무전기를 집어넣었는데 내 송신이 끊긴다.
	if (IsLocalPlayerActor(Equipper))
	{
		StopTransmit();
	}

	Super::OnUnequipped_Implementation(Equipper);
}
