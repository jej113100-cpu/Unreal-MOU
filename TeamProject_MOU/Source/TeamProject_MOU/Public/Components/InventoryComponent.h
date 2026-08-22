#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InventoryComponent.generated.h"

class AItemBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInventorySlotChanged, int32, SlotIndex, AItemBase*, ItemInSlot);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEquipRequested, AItemBase*, ItemToEquip);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UInventoryComponent();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	// 슬롯 개수 (기본 3칸)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	int32 MaxSlots = 3;

	// 인벤토리 슬롯
	UPROPERTY(ReplicatedUsing = OnRep_InventorySlots, VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	TArray<TObjectPtr<AItemBase>> InventorySlots;

	UFUNCTION()
	void OnRep_InventorySlots();

	// 특정 슬롯 키를 눌렀을 때의 동작 요청
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestSlotAction(int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerRequestSlotAction(int32 SlotIndex);

	// UI 갱신용 델리게이트
	UPROPERTY(BlueprintAssignable, Category = "Inventory|UI")
	FOnInventorySlotChanged OnInventorySlotChanged;

	// 캐릭터에게 특정 아이템 장착을 요청하는 델리게이트
	UPROPERTY(BlueprintAssignable, Category = "Inventory|Action")
	FOnEquipRequested OnEquipRequested;
};
