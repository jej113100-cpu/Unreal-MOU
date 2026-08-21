#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "CarryingComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCarriedStateChanged, AActor*, CarriedActor);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UCarryingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCarryingComponent();

protected:
	virtual void BeginPlay() override;

public:
	UFUNCTION(BlueprintCallable, Category = "Carrying")
	void GrabOrDrop();

	UFUNCTION(BlueprintCallable, Category = "Carrying")
	void Throw();

	UFUNCTION(Server, Reliable)
	void ServerGrabOrDrop();

	UFUNCTION(Server, Reliable)
	void ServerThrow();

	UFUNCTION(BlueprintCallable, Category = "Carrying")
	bool IsCarrying() const { return CarriedActor != nullptr; }

	UFUNCTION(BlueprintCallable, Category = "Carrying")
	bool IsCarryingCharacter() const;

	UFUNCTION(BlueprintCallable, Category = "Carrying")
	AActor* GetCarriedActor() const { return CarriedActor; }

	// 인벤토리 등에서 특정 아이템을 손에 강제로 쥐어줄 때 사용
	UFUNCTION(BlueprintCallable, Category = "Carrying")
	void EquipItem(AActor* ItemToEquip);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastEquipItem(AActor* ItemToEquip);

	// 손에 든 물건을 인벤토리에 넣거나 파괴할 때 손을 비우기 위해 사용
	UFUNCTION(BlueprintCallable, Category = "Carrying")
	void ClearCarriedItem();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastClearCarriedItem();

	// 손에 든 물건과 인벤토리 슬롯 아이템의 총 무게를 계산하여 AttributeSet에 동기화
	UFUNCTION(BlueprintCallable, Category = "Carrying")
	void UpdateCharacterTotalWeight();

	UPROPERTY(BlueprintAssignable, Category = "Carrying")
	FOnCarriedStateChanged OnCarriedStateChanged;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrying")
	float DefaultThrowForce = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrying")
	FName CarrySocketName = TEXT("CarrySocket");

private:
	UPROPERTY(ReplicatedUsing = OnRep_CarriedActor)
	TObjectPtr<AActor> CarriedActor;

	UFUNCTION()
	void OnRep_CarriedActor(AActor* OldCarriedActor);

	UPROPERTY(Transient)
	struct FGameplayAbilitySpecHandle ActiveCarryAbilitySpecHandle;
};
