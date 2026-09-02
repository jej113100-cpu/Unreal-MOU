#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Traps/Interfaces/TrapTriggerableInterface.h"
#include "TrapLaserSensor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UArrowComponent;

/**
 * ATrapLaserSensor
 * 전방으로 레이저 광선을 방출하며, 플레이어/NPC/패키지에 의해 광선이 차단되면
 * 연결된 모든 타겟 함정(LinkedTraps)에 발동 신호를 브로드캐스트하는 독립 센서 액터입니다.
 * - LaserMesh의 트랜스폼(위치, 회전, 길이, 굵기)과 메시/머티리얼은 블루프린트에서 100% 자유롭게 편집 가능합니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API ATrapLaserSensor : public AActor, public ITrapTriggerableInterface
{
	GENERATED_BODY()
	
public:	
	ATrapLaserSensor();

	// ---------------------------------------------------------
	// [컴포넌트]
	// ---------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> RootScene;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> EmitterMesh;

	/** 에디터 뷰포트에서 레이저 방출 방향을 자유롭게 회전/지정할 수 있는 방향 화살표 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> LaserDirectionArrow;

	/** 레이저 광선 스태틱 메시 (블루프린트 디테일 패널에서 트랜스폼/메시/머티리얼 자유 편집) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> LaserMesh;

	// ---------------------------------------------------------
	// [연동 설정 (N:M Event Linkage)]
	// ---------------------------------------------------------

	/** 레이저 차단 시 일제히 격발시킬 함정 목록 (화살발사기, 압살기, 가시바닥, 경보 사이렌 등) */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Laser|LinkedTraps")
	TArray<TObjectPtr<AActor>> LinkedTraps;

	/** 레이저 감지 사거리 (LineTrace 스캔 거리, cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Config")
	float LaserMaxDistance = 1500.0f;

	/** 레이저 스캔 주기 (초 단위, 0.05초 = 20Hz로 CPU 부하 최소화) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Config")
	float ScanInterval = 0.05f;

	/** 센서 활성화 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_bIsSensorActive, Category = "Laser|Config")
	bool bIsSensorActive = true;

	/** 디버그 레이저 라인 렌더링 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Debug")
	bool bShowDebugLaser = false;

	// ---------------------------------------------------------
	// [ITrapTriggerableInterface 구현]
	// ---------------------------------------------------------

	virtual void TriggerTrap_Implementation(AActor* InstigatorActor) override;
	virtual void ResetTrap_Implementation() override;
	virtual void DisarmTrap_Implementation(AActor* Disarmer) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void PerformLaserScan();

	UFUNCTION()
	void OnRep_bIsSensorActive();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnLaserBeamObstructed(FVector ImpactPoint, AActor* ObstructingActor);

	UFUNCTION(BlueprintImplementableEvent, Category = "Laser|Events")
	void OnLaserObstructed_BP(FVector ImpactPoint, AActor* ObstructingActor);

	UFUNCTION(BlueprintImplementableEvent, Category = "Laser|Events")
	void OnSensorStateChanged_BP(bool bActive);

protected:
	FTimerHandle ScanTimerHandle;

	UPROPERTY(Transient)
	TObjectPtr<AActor> LastDetectedActor;

	bool bWasObstructed = false;
};
