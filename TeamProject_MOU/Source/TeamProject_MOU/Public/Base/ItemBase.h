#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interfaces/InteractableInterface.h"
#include "Item/ItemSaveData.h"
#include "ItemBase.generated.h"

class UStaticMeshComponent;
class UTexture2D;

UCLASS()
class TEAMPROJECT_MOU_API AItemBase : public AActor, public IInteractableInterface
{
	GENERATED_BODY()
	
public:	
	AItemBase();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:	
	virtual void Tick(float DeltaTime) override;

	// ---------------------------------------------------------
	// [컴포넌트]
	// ---------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	// ---------------------------------------------------------
	// [기본 아이템 데이터]
	// ---------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Item|Data")
	FText ItemName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, ReplicatedUsing = OnRep_ItemIcon, Category = "Item|Data")
	TObjectPtr<UTexture2D> ItemIcon;

	UFUNCTION()
	void OnRep_ItemIcon();

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Item|Data")
	float ItemWeight = 1.0f;

	// 인벤토리 수납 가능 여부 (택배, 이벤트 오브젝트 등은 false)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Item|Inventory")
	bool bCanBeStoredInInventory = true;

	// ---------------------------------------------------------
	// [상태 관리 데이터]
	// ---------------------------------------------------------
	// 사용 횟수 (주로 일반 아이템용)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Item|Status")
	int32 MaxUseCount = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Item|Status")
	int32 CurrentUseCount = 1;

	// 내구도 (주로 파괴 가능한 아이템 및 택배용)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Item|Status")
	float MaxDurability = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Replicated, Category = "Item|Status")
	float CurrentDurability = 100.0f;

	// 마지막으로 이 아이템을 소유했던 액터 (평판 추적 등)
	UPROPERTY(BlueprintReadOnly, Category = "Item|Tracking")
	TObjectPtr<AActor> LastOwner;

	// 던져진 상태인지 여부 (충돌 시 피격 판정용)
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Item|Throw")
	bool bWasThrown = false;

	// 이 아이템을 마지막으로 던진 액터 (자폭 방지용)
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Item|Throw")
	TWeakObjectPtr<AActor> LastThrower;

protected:
	virtual void PostInitializeComponents() override;

	// 물리 충돌 감지 콜백 (플레이어 피격 등 공통 처리)
	UFUNCTION()
	virtual void OnItemHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	// 플레이어에게 부딪혔을 때 호출되는 가상 함수 (자식 클래스에서 속도별 분기 오버라이드)
	virtual void HandlePlayerHit(class AMainCharacter* HitPlayer, float ImpactSpeed);

public:
	// ---------------------------------------------------------
	// [상호작용 인터페이스 구현 (IInteractableInterface)]
	// ---------------------------------------------------------
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractPrompt_Implementation() const override;

	// 다른 캐릭터가 이 아이템을 집을 수 있는지 검사 (다른 사람이 들고 있는 아이템 뺏어가기 방지)
	UFUNCTION(BlueprintCallable, Category = "Item|Action")
	virtual bool CanBePickedUpBy(AActor* PotentialPicker) const;

	// 지금 손에서 내려놓기(Drop)/던지기(Throw)가 가능한 상태인지.
	// 기본 true. 사용 중이라 놓으면 안 되는 아이템(예: 비행 중 부메랑)은 false로 막는다.
	UFUNCTION(BlueprintCallable, Category = "Item|Action")
	virtual bool CanBeDropped() const { return true; }

	// 손 소켓에 붙인 후 바운딩박스 중심 보정을 적용할지 여부.
	// 손잡이 피벗을 그대로 소켓에 맞추는 아이템은 false를 반환한다.
	virtual bool ShouldCenterOnCarrySocket() const { return true; }
	virtual FName GetCarrySocketOverride() const { return NAME_None; }
	virtual FVector GetCarryLocationOffset() const { return FVector::ZeroVector; }
	virtual FRotator GetCarryRotationOffset() const { return FRotator::ZeroRotator; }

	// ---------------------------------------------------------
	// [핵심 행동 함수]
	// ---------------------------------------------------------
	
	// E키로 바닥에서 집어들 때 호출
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Action")
	void PickUp(AActor* Picker);
	virtual void PickUp_Implementation(AActor* Picker);

	// G키 등으로 손에서 놓을 때 호출
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Action")
	void Drop(FVector DropLocation, AActor* Dropper = nullptr);
	virtual void Drop_Implementation(FVector DropLocation, AActor* Dropper = nullptr);

	// 마우스 우클릭으로 던질 때 호출
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Action")
	void Throw(FVector ThrowVelocity, AActor* Thrower = nullptr);
	virtual void Throw_Implementation(FVector ThrowVelocity, AActor* Thrower = nullptr);

	// 물리 동기화를 위한 멀티캐스트 함수들
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPickUp(AActor* Picker);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastDrop(FVector DropLocation, AActor* Dropper = nullptr);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastThrow(FVector ThrowVelocity, AActor* Thrower = nullptr);

	// 좌클릭으로 아이템을 사용할 때 호출 (택배의 경우 기능을 비움)
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Action")
	void OnUse();
	virtual void OnUse_Implementation();

	// [ITEM-100] 사용 입력 해제를 자식 아이템에 전달한다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Action")
	void OnUseReleased();
	// [ITEM-100] 사용 입력 해제의 기본 동작은 비워 둔다.
	virtual void OnUseReleased_Implementation();

	// 손에 장착될 때 (인벤토리에서 활성화될 때)
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Action")
	void OnEquipped(AActor* Equipper);
	virtual void OnEquipped_Implementation(AActor* Equipper);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnEquipped(AActor* Equipper);

	// 손에서 해제될 때 (인벤토리로 들어갈 때)
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Action")
	void OnUnequipped(AActor* Equipper);
	virtual void OnUnequipped_Implementation(AActor* Equipper);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnUnequipped(AActor* Equipper);

	// ---------------------------------------------------------
	// [저장 / 복원]
	// ---------------------------------------------------------
	// ItemBase가 공통 상태를 저장하고, 자식 클래스는 오버라이드로 ExtraSaveData를 추가합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Save")
	void SaveItemToData(UPARAM(ref) FStoredItemInstanceData& OutData) const;
	virtual void SaveItemToData_Implementation(FStoredItemInstanceData& OutData) const;

	// 저장된 공통 상태를 복원합니다. 자식 클래스는 오버라이드로 자기 전용 ExtraSaveData를 읽습니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Save")
	void LoadItemFromData(const FStoredItemInstanceData& InData);
	virtual void LoadItemFromData_Implementation(const FStoredItemInstanceData& InData);
};
