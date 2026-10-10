#pragma once

#include "CoreMinimal.h"
#include "Base/ItemBase.h"
#include "ItemDrone.generated.h"

class USceneComponent;
class UAudioComponent;
class ACharacter;

// ---------------------------------------------------------
// [아이템 드론]
// AItemBase를 상속한 "집을 수 있는" 드론 아이템.
// - 줍기 : 바닥에 있을 때 F키로 집는다 (AItemBase 기본 PickUp 흐름).
// - 사용 : 손에 들고 좌클릭(OnUse) -> 손에서 배치되어 플레이어를 따라감(팔로우 시작).
// - 맡기기: 배치(팔로우) 상태에서 F키 -> 손 아이템을 ItemHoldPoint에 거치. [DRONE-001]
// - 회수 : 배치 상태 + 빈손으로 F키 -> 거치 아이템을 손으로 되돌림. [DRONE-002]
// - 종료 : 드론과 플레이어가 모두 빈 상태에서 배치자가 F키 -> 팔로우 종료 후 드론을 손으로 회수. [DRONE-014]
// - 배치 중 아이템을 보관하면 내구도(보관된 아이템)가 5분에 걸쳐 0이 되고 파괴된다. [DRONE-006]
// ---------------------------------------------------------
UCLASS()
class TEAMPROJECT_MOU_API AItemDrone : public AItemBase
{
	GENERATED_BODY()

public:
	AItemDrone();

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	virtual void Tick(float DeltaTime) override;

	// ---------------------------------------------------------
	// [컴포넌트] (본체 메시는 AItemBase의 MeshComponent 재사용)
	// ---------------------------------------------------------
	// 맡긴 일반 아이템이 거치될 위치 (요청하신 SceneComponent)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> ItemHoldPoint;

	// 맡긴 택배(APackageBase)가 거치될 위치 (드론 머리 위)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> PackageHoldPoint;

	// 드론이 배치된 동안 위치를 따라다니며 루프 사운드를 재생한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UAudioComponent> DroneLoopAudio;

	// ---------------------------------------------------------
	// [배치 / 팔로우 상태]
	// ---------------------------------------------------------
	// 드론이 배치되어(손에서 나가) 팔로우 중인지 여부. 좌클릭 사용으로 true가 된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_IsDeployed, Category = "Drone|State")
	bool bIsDeployed = false;

	UFUNCTION()
	void OnRep_IsDeployed();

	// 따라다닐 대상 플레이어 (좌클릭으로 사용한 플레이어가 지정됨)
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Drone|Follow")
	TObjectPtr<ACharacter> FollowTarget;

	// 플레이어 기준 목표 위치 오프셋 (로컬 공간). 기본값: 오른쪽 어깨 옆 + 약간 위.
	// X=뒤쪽(-), Y=오른쪽(+), Z=위(+)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Follow")
	FVector FollowOffset = FVector(-40.0f, 70.0f, 60.0f);

	// 목표 위치까지 따라가는 부드러움 (클수록 빠르게 붙음)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Follow")
	float FollowInterpSpeed = 6.0f;

	// 위아래로 살짝 떠다니는 보빙 진폭 (0이면 비활성)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Follow")
	float BobbingAmplitude = 5.0f;

	// 보빙 속도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Follow")
	float BobbingSpeed = 2.0f;

	// 이 속도(cm/s) 이하이면 플레이어가 "정지"한 것으로 보고 드론 목표 위치를 갱신하지 않는다.
	// (정지 중 카메라만 돌려도 드론이 따라 돌지 않게 해서, 정면에서 상호작용 가능하도록)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Follow")
	float MoveThreshold = 10.0f;

	// 플레이어와 이 거리 이상 벌어지면 보간 이동 대신 플레이어 주변 목표 위치로 즉시 이동한다. 0이면 비활성.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Follow", meta = (ClampMin = "0.0", Units = "cm"))
	float TeleportDistance = 1500.0f;

	// ---------------------------------------------------------
	// [보관 상태]
	// 일반 아이템과 택배는 슬롯이 분리되어 있어 동시에 하나씩 보관할 수 있다.
	// ---------------------------------------------------------
	// 드론 아래(ItemHoldPoint)에 보관 중인 일반 아이템 (없으면 nullptr)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Drone|State")
	TObjectPtr<AItemBase> StoredItem;

	// 드론 머리 위(PackageHoldPoint)에 보관 중인 택배 (없으면 nullptr)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Drone|State")
	TObjectPtr<AItemBase> StoredPackage;

	// ---------------------------------------------------------
	// [내구도 소모]
	// 드론이 아이템(또는 택배)을 하나라도 보관하는 동안 "드론 자신"의 내구도(CurrentDurability)가
	// 이 시간(초)에 걸쳐 0이 되도록 깎인다. 0이 되면 보관 중인 것들을 바닥에 떨어뜨리고 드론이 파괴된다.
	// 기본 300초 = 5분(1판 플레이 타임).
	// ---------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Durability", meta = (ClampMin = "0.1"))
	float DurabilityDrainDuration = 300.0f;

	// ---------------------------------------------------------
	// [AItemBase 오버라이드]
	// ---------------------------------------------------------
	// 좌클릭 사용: 손에서 배치되어 팔로우 시작
	virtual void OnUse_Implementation() override;

	// 집을 때 소유권 설정 (클라가 ServerDeploy RPC를 보낼 수 있게 함). WeaponItemBase [WEAPON-010]과 동일.
	virtual void PickUp_Implementation(AActor* Picker) override;

	// 배치(팔로우) 상태에서는 E키 줍기 대상에서 제외한다. (허공에서 E로 드론이 잡히는 것 방지)
	virtual bool CanBePickedUpBy(AActor* PotentialPicker) const override;

	// F키 상호작용: 배치 상태면 아이템 맡기기/회수/빈 드론 종료, 아니면 기본(줍기)
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractPrompt_Implementation() const override;

private:
	// [DRONE-010] 서버에서 맡기기/회수를 실제 처리.
	// 클라의 상호작용은 InteractionComponent::ServerRunInteract가 서버로 위임 후 Interact를 재실행하므로,
	// 드론 자체 Server RPC는 두지 않는다(드론 소유권=배치자에 묶여 다른 클라가 못 쓰던 문제 회피).
	void HandleInteractOnServer(ACharacter* Character);

	// [DRONE-006] 뭔가를 보관하는 동안 드론 자신의 내구도를 깎고, 0이 되면 내용물 Drop 후 드론 파괴 (서버 전용)
	void TickDurabilityDrain(float DeltaTime);

	// [DRONE-011] 드론 내구도 소진 시: 보관 중인 슬롯 아이템들을 바닥에 떨어뜨리고 드론을 파괴 (서버 전용)
	void BreakDrone();

	// [DRONE-007] 손에서 배치되어 팔로우를 시작한다 (서버 전용 처리)
	void DeployAndFollow(ACharacter* User);

	// [DRONE-008] 클라이언트 -> 서버 배치 위임 (WeaponItemBase의 ServerFire와 동일 패턴)
	UFUNCTION(Server, Reliable)
	void ServerDeploy();

	// [DRONE-009] 이 아이템을 드론에 맡길 수 있는지 판정.
	// 택배(APackageBase)는 Heavy/Quest 타입 거부, 그 외 허용. 일반 아이템은 인벤토리 수납 가능해야 함.
	bool CanStoreItem(const AItemBase* Item) const;

	// [DRONE-001] 손에 든 아이템을 드론에 맡긴다 (서버 전용 처리 흐름).
	// 택배면 StoredPackage 슬롯, 일반 아이템이면 StoredItem 슬롯에 넣는다.
	void StoreItemFromHand(ACharacter* Interactor, AItemBase* HandItem);

	// [DRONE-002] 드론이 보관 중인 아이템(슬롯)을 플레이어 손으로 되돌려준다 (서버 전용 처리 흐름).
	void RetrieveItemToHand(ACharacter* Interactor, bool bRetrievePackage);

	// [DRONE-014] 배치자가 빈 드론 앞에서 빈손으로 상호작용하면 팔로우를 종료하고 손으로 회수한다.
	void RecallToHand(ACharacter* Interactor);

	// [DRONE-003] 아이템을 드론 거치 지점에 부착 (모든 클라 동기화). 택배면 머리 위, 아니면 아래.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastAttachToDrone(AItemBase* Item);

	// [DRONE-004] 팔로우 목표 위치 계산. 차량 등 부모 액터에 부착된 플레이어는 부모 속도까지 반영한다.
	FVector CalcTargetLocation();

	// [DRONE-012] 플레이어에서 각 후보 오프셋 지점까지 경로가 뚫려 있는지 검사해, 따라갈 오프셋을 고른다.
	// 우선순위: 오른쪽뒤(기본) -> 왼쪽뒤 -> 정뒤 -> 오른쪽옆 -> 왼쪽옆. 다 막히면 기본값 반환.
	FVector ChooseFollowOffset() const;

	// [DRONE-013] 배치 상태에 맞춰 드론 루프 사운드를 재생하거나 정지한다.
	void UpdateDroneLoopSound();

	// 보빙 위상 누적용
	float BobbingPhase = 0.0f;

	// 정지 중 드론을 고정하기 위한, 마지막으로 갱신한 목표 위치(보빙 제외 베이스). 서버에서만 사용.
	FVector CachedFollowBaseLocation = FVector::ZeroVector;
	bool bHasCachedFollowBase = false;
};
