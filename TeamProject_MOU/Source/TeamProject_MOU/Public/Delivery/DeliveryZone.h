#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DeliveryZone.generated.h"

class UBoxComponent;

/** 이 영역에 들어온 이번 배달 대상 패키지를 서버에서 납품 처리합니다. */
UCLASS(Blueprintable)
class TEAMPROJECT_MOU_API ADeliveryZone : public AActor
{
	GENERATED_BODY()

public:
	ADeliveryZone();

	/** BeginPlay 전에도 Manager가 맵 배치 Zone을 등록할 수 있도록 유효한 ID를 반환합니다. */
	FName GetEffectiveZoneID() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Delivery")
	FName ZoneID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery",
		meta = (ClampMin = "0.1", Units = "s"))
	float RequiredHoldSeconds = 3.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery")
	TObjectPtr<UBoxComponent> DeliveryBounds;

private:
	TMap<TWeakObjectPtr<class APackageBase>, FTimerHandle> ActiveDeliveryTimers;

	UFUNCTION()
	void HandleDeliveryOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleDeliveryEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex);

	void CompleteHeldDelivery(TWeakObjectPtr<class APackageBase> Package);
	void RegisterWithDeliveryManager();
};
