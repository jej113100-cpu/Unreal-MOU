#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DartProjectile.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;
class UGameplayEffect;

/**
 * ADartProjectile
 * 함정(TrapDartShooter)에서 발사되는 고속 다트/화살 투사체입니다.
 * 타겟 액터 충돌 시 ITrapTargetInterface를 통해 대미지와 상태이상을 적용합니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API ADartProjectile : public AActor
{
	GENERATED_BODY()
	
public:	
	ADartProjectile();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	/** 투사체 피해량 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage")
	float Damage = 25.0f;

	/** 적중 시 부여할 GAS GameplayEffect (출혈/둔화 등) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage|GAS")
	TSubclassOf<UGameplayEffect> HitStatusEffectClass;

	/** 적중 시 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FX")
	TObjectPtr<class USoundBase> HitSound;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
};
