#pragma once

#include "CoreMinimal.h"
#include "Base/ItemBase.h"
#include "GameplayTagContainer.h"
#include "ConsumableItemBase.generated.h"

class UAnimMontage;

/**
 * 소비 아이템 효과 대상
 * 효과가 자기 자신에게 적용되는지(포션 등), 앞의 대상에게 적용되는지(치료/수리 등) 구분.
 * (무기의 EWeaponTargetTeam에 대응하는 역할)
 */
UENUM(BlueprintType)
enum class EConsumeTarget : uint8
{
	// 자기 자신(든 플레이어)에게 효과 적용 (포션 등)
	SelfOnly		UMETA(DisplayName = "Self Only"),
	// 앞의 포커스된 대상에게 효과 적용 (치료 도구, 수리 도구 등) - 탐색 로직은 추후 구현
	FocusedTarget	UMETA(DisplayName = "Focused Target")
};

/**
 * AConsumableItemBase
 * 소비 아이템 공통 베이스. 포션/수리 도구/치료 도구 등 "쓰면 닳는" 아이템의 부모.
 * 무기 베이스(AWeaponItemBase)와 대칭 구조:
 *   - 복제되는 사용 횟수(ConsumeUseCount) - 멀티에서 동기화
 *   - 서버 권한 사용 흐름(OnUse → ServerConsume → 차감 → ApplyEffect)
 *   - 다 쓰면 자동 소멸(Destroy)
 * 자식(포션 등)은 ApplyEffect만 override해서 실제 효과를 넣는다.
 */
UCLASS()
class TEAMPROJECT_MOU_API AConsumableItemBase : public AItemBase
{
	GENERATED_BODY()

public:
	AConsumableItemBase();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

#pragma region [CONSUME] 사용 횟수 (복제)
	// 현재 남은 사용 횟수. 서버에서만 차감하고 복제되어 모든 클라에 동기화된다.
	// (ItemBase의 CurrentUseCount는 복제 안 돼서 별도 복제 변수 사용)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_ConsumeUseCount, Category = "Consumable")
	int32 ConsumeUseCount = 0;

	// 복제값 도착 시 호출 (UI 갱신 훅). 현재 비어있음
	UFUNCTION()
	void OnRep_ConsumeUseCount();
#pragma endregion

#pragma region [CONSUME] 소유권 (Server RPC 전제)
	// [CONSUME-010] 집을 때 소유권 설정 → 클라가 ServerConsume RPC 보낼 수 있게 함
	// (Owner 없으면 "No owning connection"으로 ServerRPC가 버려짐)
	virtual void PickUp_Implementation(AActor* Picker) override;

	// [CONSUME-011] 놓을 때 소유권 해제
	virtual void Drop_Implementation(FVector DropLocation, AActor* Dropper = nullptr) override;

	// [CONSUME-012] 던질 때 소유권 해제
	virtual void Throw_Implementation(FVector ThrowVelocity, AActor* Thrower = nullptr) override;
#pragma endregion

#pragma region [CONSUME] 사용 흐름 (공통)
	// [CONSUME-000] 좌클릭: 잔여 횟수 체크 → (서버)차감 → ApplyEffect() → 다 쓰면 Destroy
	virtual void OnUse_Implementation() override;

	// [CONSUME-007] 실제 소비 효과. 자식이 override (포션=회복, 수리=수리 등)
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Consumable")
	void ApplyEffect();
	virtual void ApplyEffect_Implementation();

	// [CONSUME-005] 효과 연출(VFX/사운드) 멀티캐스트 → 모든 클라에서 재생
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayUseEffect();

	// [CONSUME-006] 효과 연출 BP 훅 (나이아가라/사운드 등을 BP에서 연결)
	UFUNCTION(BlueprintImplementableEvent, Category = "Consumable")
	void OnUseEffect();

	// [CONSUME-015] 사용 시 소유 캐릭터가 재생할 애니메이션 몽타주 (마시기 등).
	// GA에 payload(Optional Object)로 전달됨. GA_UsePotion이 이 몽타주를 재생.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Consumable")
	TObjectPtr<UAnimMontage> UseMontage;

	// [CONSUME-016] 사용 시 발동할 GA를 트리거하는 GameplayEvent 태그 (Event.Consumable.Use).
	// 비워두면 이벤트를 안 쏨. BP에서 지정.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Consumable")
	FGameplayTag UseAbilityEventTag;

private:
	// [CONSUME-008] 클라이언트에서 눌렀을 때 서버로 소비 위임 (서버에서 차감+효과)
	UFUNCTION(Server, Reliable)
	void ServerConsume();

	// [CONSUME-009] 서버 공통 진입점: 잔여 체크 → 차감(복제) → ApplyEffect → 소진 시 Destroy
	void TryConsumeOnServer();

	// [CONSUME-013] 다 쓴 아이템을 소유 캐릭터의 손(CarryingComponent)에서 비운다.
	// (안 하면 CarriedActor가 남아 Carry 포즈가 안 풀리고 무게도 안 빠짐) 서버 전용.
	void ReleaseFromHolderHand();

	// [CONSUME-016] 소유 캐릭터에게 사용 GA 트리거 GameplayEvent 전송 (몽타주는 payload로 전달)
	void SendUseAbilityEvent();
#pragma endregion

protected:
#pragma region [CONSUME] 설정값
	// 이 소비 아이템의 효과 대상 (자기 자신 / 앞의 대상). 기본은 자기 자신.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Consumable")
	EConsumeTarget TargetMode = EConsumeTarget::SelfOnly;
#pragma endregion

#pragma region [CONSUME] 효과 대상 조회
	// [CONSUME-004] 효과를 적용할 대상 액터를 반환.
	// SelfOnly면 든 플레이어(LastOwner), FocusedTarget이면 nullptr(탐색 로직 추후 구현).
	// 자식/치료 도구에서 override해 앞의 대상을 채우면 됨.
	UFUNCTION(BlueprintCallable, Category = "Consumable")
	virtual AActor* ResolveEffectTarget() const;
#pragma endregion
};
