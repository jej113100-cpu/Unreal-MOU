#pragma once

#include "CoreMinimal.h"
#include "Traps/Actors/TrapBase.h"
#include "TrapSpikeFloor.generated.h"

class UStaticMeshComponent;
class UBoxComponent;

/**
 * ATrapSpikeFloor
 * 바닥에서 날카로운 강철 가시가 솟구쳐 대상에게 물리 피해와 출혈(Bleed)/둔화 디버프를 부여하는 함정입니다.
 * 4가지 동작 모드(상시 돌출, 주기적 팝업, 압력판 감지, 외부 신호/레이저 연동)를 지원합니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API ATrapSpikeFloor : public ATrapBase
{
	GENERATED_BODY()

public:
	ATrapSpikeFloor();

	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BaseFrameMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SpikeMesh;

	// ---------------------------------------------------------
	// [가시 바닥 설정]
	// ---------------------------------------------------------

	/** 동작 모드 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spike|Config")
	ESpikeOperationMode OperationMode = ESpikeOperationMode::PressureTriggered;

	/** 가시 돌출 높이 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spike|Config")
	float SpikeExtendHeight = 60.0f;

	/** 가시 돌출 속도 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spike|Config")
	float SpikeExtendSpeed = 600.0f;

	/** 가시 수납 속도 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spike|Config")
	float SpikeRetractSpeed = 200.0f;

protected:
	virtual void BeginPlay() override;
	virtual void OnStateEntered(ETrapState NewState) override;
	virtual void ExecuteTrapPayload() override;

	void UpdateSpikeMovement(float DeltaTime);

protected:
	FVector RetractedRelativeLocation;
	FVector ExtendedRelativeLocation;
	FVector TargetSpikeLocation;
};
