#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Traps/TrapEnums.h"
#include "TrapTriggerComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTrapTriggerActivated, AActor*, InstigatorActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTrapTriggerDeactivated);

class UShapeComponent;

/**
 * UTrapTriggerComponent
 * 압력판(오버랩), 주기적 타이머, 원격 신호 등의 발동 조건을 관리하고
 * 조건 충족 시 부모 함정에 발동 신호를 전달하는 컴포넌트입니다.
 */
UCLASS(ClassGroup = (Traps), meta = (BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UTrapTriggerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTrapTriggerComponent();

	/** 트리거 조건이 충족되어 발동될 때 발생하는 델리게이트 */
	UPROPERTY(BlueprintAssignable, Category = "Trap|Trigger")
	FOnTrapTriggerActivated OnTriggerActivated;

	/** 트리거 영역에서 모든 액터가 벗어났을 때 발생하는 델리게이트 */
	UPROPERTY(BlueprintAssignable, Category = "Trap|Trigger")
	FOnTrapTriggerDeactivated OnTriggerDeactivated;

	/** 트리거 감지 방식 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap|Trigger")
	ETrapTriggerType TriggerType = ETrapTriggerType::PressurePlate;

	/** 주기적 타이머 발동 간격 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap|Trigger", meta = (EditCondition = "TriggerType == ETrapTriggerType::PeriodicTimer"))
	float PeriodicInterval = 3.0f;

	/** 감지할 콜리전 컴포넌트 등록 */
	UFUNCTION(BlueprintCallable, Category = "Trap|Trigger")
	void RegisterDetectionVolume(UShapeComponent* InDetectionVolume);

	/** 주기적 타이머 시작 */
	UFUNCTION(BlueprintCallable, Category = "Trap|Trigger")
	void StartPeriodicTimer();

	/** 주기적 타이머 정지 */
	UFUNCTION(BlueprintCallable, Category = "Trap|Trigger")
	void StopPeriodicTimer();

	/** 원격/수동 발동 요청 */
	UFUNCTION(BlueprintCallable, Category = "Trap|Trigger")
	void TriggerManually(AActor* InstigatorActor);

	/** 현재 트리거 영역 내에 있는 액터 목록 */
	UFUNCTION(BlueprintPure, Category = "Trap|Trigger")
	const TArray<AActor*>& GetOverlappingActors() const { return OverlappingActors; }

	/** 트리거 활성화 여부 토글 */
	UFUNCTION(BlueprintCallable, Category = "Trap|Trigger")
	void SetTriggerEnabled(bool bEnabled);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleDetectionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleDetectionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void HandlePeriodicTimerTick();

protected:
	UPROPERTY(Transient)
	TObjectPtr<UShapeComponent> DetectionVolume;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> OverlappingActors;

	FTimerHandle PeriodicTimerHandle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trap|Trigger")
	bool bTriggerEnabled = true;
};
