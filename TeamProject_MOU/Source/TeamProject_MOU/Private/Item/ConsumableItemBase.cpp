#include "Item/ConsumableItemBase.h"
#include "Net/UnrealNetwork.h"
#include "Components/CarryingComponent.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "AbilitySystemBlueprintLibrary.h"

AConsumableItemBase::AConsumableItemBase()
{
}

void AConsumableItemBase::BeginPlay()
{
	Super::BeginPlay();

	// 복제 카운트를 MaxUseCount로 초기화 (ItemBase가 CurrentUseCount=MaxUseCount 한 뒤)
	ConsumeUseCount = MaxUseCount;
}

void AConsumableItemBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AConsumableItemBase, ConsumeUseCount);
}

// [CONSUME-010] 집을 때 소유권 설정 (클라 → 서버 RPC 가능하게)
void AConsumableItemBase::PickUp_Implementation(AActor* Picker)
{
	Super::PickUp_Implementation(Picker);

	// PickUp은 서버에서 실행됨. Owner를 든 플레이어로 설정 → 그 플레이어 클라가 ServerRPC 전송 가능
	if (HasAuthority() && Picker)
	{
		SetOwner(Picker);
	}
}

// [CONSUME-011] 놓을 때 소유권 해제
void AConsumableItemBase::Drop_Implementation(FVector DropLocation, AActor* Dropper)
{
	if (HasAuthority())
	{
		SetOwner(nullptr);
	}

	Super::Drop_Implementation(DropLocation, Dropper);
}

// [CONSUME-012] 던질 때 소유권 해제
void AConsumableItemBase::Throw_Implementation(FVector ThrowVelocity, AActor* Thrower)
{
	if (HasAuthority())
	{
		SetOwner(nullptr);
	}

	Super::Throw_Implementation(ThrowVelocity, Thrower);
}

// [CONSUME-000] 좌클릭: 소비는 항상 서버에서 처리 (차감/판정/복제 신뢰성)
void AConsumableItemBase::OnUse_Implementation()
{
	// 차감·효과는 서버에서만. 클라에서 불리면 ServerConsume로 넘겨 서버가 처리한다.
	// (Super::OnUse는 부르지 않음 - ItemBase의 CurrentUseCount 대신 복제되는 ConsumeUseCount 사용)
	if (HasAuthority())
	{
		TryConsumeOnServer();
	}
	else
	{
		ServerConsume();
	}
}

// [CONSUME-009] 서버 공통 진입점: 잔여 체크 → 차감(복제) → ApplyEffect → 소진 시 Destroy
void AConsumableItemBase::TryConsumeOnServer()
{
	// 서버에서만 실행되어야 함
	if (!HasAuthority())
	{
		return;
	}

	if (ConsumeUseCount <= 0)
	{
		return;
	}

	// 서버에서 차감 → ConsumeUseCount 복제되어 모든 클라 화면에 동기화
	ConsumeUseCount--;

	// 실제 효과 적용 (자식 override)
	ApplyEffect();

	// 사용 애니메이션 GA 트리거 (소유 캐릭터에게 GameplayEvent → GA_UsePotion이 몽타주 재생)
	SendUseAbilityEvent();

	// 효과 연출(VFX/사운드)을 모든 클라에서 재생
	MulticastPlayUseEffect();

	// 다 쓰면 손을 비운 뒤 아이템 소멸
	if (ConsumeUseCount <= 0)
	{
		// [CONSUME-013] 손(CarriedActor)을 명시적으로 비워야 Carry 포즈가 풀리고 무게가 빠짐.
		// (Destroy만 하면 다음 GC 전까지 CarriedActor가 남아 애니메이션이 안 돌아옴)
		ReleaseFromHolderHand();
		Destroy();
	}
}

// [CONSUME-013] 소유 캐릭터의 CarryingComponent에서 이 아이템을 비운다.
void AConsumableItemBase::ReleaseFromHolderHand()
{
	if (!HasAuthority())
	{
		return;
	}

	// 포션은 소유 캐릭터 메시에 Attach돼 있음 → 부착 부모로 소유자를 얻는다.
	// (Owner가 비어있는 경우를 대비해 GetOwner()로 폴백)
	AActor* Holder = GetAttachParentActor();
	if (!Holder)
	{
		Holder = GetOwner();
	}
	if (!Holder)
	{
		return;
	}

	if (UCarryingComponent* Carry = Holder->FindComponentByClass<UCarryingComponent>())
	{
		// 내가 들려 있는 게 맞을 때만 비운다 (다른 걸 들고 있으면 건드리지 않음)
		if (Carry->GetCarriedActor() == this)
		{
			Carry->ClearCarriedItem();
		}
	}
}

// [CONSUME-016] 사용 GA 트리거: 소유 캐릭터에게 GameplayEvent 전송 (몽타주는 payload로 전달)
void AConsumableItemBase::SendUseAbilityEvent()
{
	if (!UseAbilityEventTag.IsValid())
	{
		return;
	}

	// 소유 캐릭터 (아이템이 캐릭터 메시에 Attach돼 있으므로 부착 부모 → 폴백 Owner)
	AActor* Holder = GetAttachParentActor();
	if (!Holder)
	{
		Holder = GetOwner();
	}
	if (!Holder)
	{
		return;
	}

	FGameplayEventData Payload;
	Payload.EventTag = UseAbilityEventTag;
	Payload.Instigator = this;
	Payload.OptionalObject = UseMontage; // GA_UsePotion이 이 몽타주를 재생

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Holder, UseAbilityEventTag, Payload);
}

// [CONSUME-008] 클라이언트 → 서버 소비 위임 (서버에서 차감+효과)
void AConsumableItemBase::ServerConsume_Implementation()
{
	TryConsumeOnServer();
}

// 복제된 ConsumeUseCount가 각 클라에 도착했을 때 호출 (UI 갱신 등 훅으로 활용 가능)
void AConsumableItemBase::OnRep_ConsumeUseCount()
{
}

// [CONSUME-007] 실제 소비 효과 (기본 빈 구현, 자식이 override)
void AConsumableItemBase::ApplyEffect_Implementation()
{
	// 자식 클래스에서 구현 (포션=회복, 수리=수리 등)
	// ResolveEffectTarget()으로 효과 대상을 얻어 처리하면 됨.
}

// [CONSUME-005] 효과 연출 멀티캐스트 → VFX/사운드 BP 훅 호출
// (몽타주는 GA_UsePotion이 재생 - SendUseAbilityEvent 참고)
void AConsumableItemBase::MulticastPlayUseEffect_Implementation()
{
	OnUseEffect();
}

// [CONSUME-004] 효과 대상 조회
AActor* AConsumableItemBase::ResolveEffectTarget() const
{
	switch (TargetMode)
	{
	case EConsumeTarget::SelfOnly:
		// 자기 자신 = 마지막으로 이 아이템을 든 플레이어
		return LastOwner;

	case EConsumeTarget::FocusedTarget:
	default:
		// 앞의 대상 탐색은 추후 구현 (치료/수리 도구 만들 때 override)
		return nullptr;
	}
}
