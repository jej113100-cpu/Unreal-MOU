#pragma once

#include "CoreMinimal.h"
#include "Traps/Actors/TrapBase.h"
#include "TrapDartShooter.generated.h"

class UStaticMeshComponent;
class UArrowComponent;
class ADartProjectile;

/**
 * ATrapDartShooter
 * 벽면이나 천장에 부착되어 발동 신호(TriggerTrap)를 수신하면
 * 전방으로 고속 다트/화살 투사체(ADartProjectile)를 발사하는 함정 액터입니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API ATrapDartShooter : public ATrapBase
{
	GENERATED_BODY()

public:
	ATrapDartShooter();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShooterMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> MuzzleDirection;

	/** 발사할 다트 투사체 클래스 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DartShooter|Config")
	TSubclassOf<ADartProjectile> ProjectileClass;

	/** 1회 발동 시 연속 발사할 다트 수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DartShooter|Config")
	int32 BurstCount = 1;

	/** 다트 연사 간격 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DartShooter|Config")
	float BurstInterval = 0.15f;

protected:
	virtual void ExecuteTrapPayload() override;

	void SpawnSingleDart();

protected:
	int32 RemainingBurstCount = 0;
	FTimerHandle BurstTimerHandle;
};
