#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Traps/Interfaces/TrapTriggerableInterface.h"
#include "SlipperySurface.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class UNiagaraComponent;
class ACharacter;

/**
 * ASlipperySurface
 * 빙판(Ice) 또는 기름(Oil Spill) 바닥을 나타내는 액터입니다.
 * - 영역에 진입한 캐릭터의 지면 마찰력(Ground Friction)을 극감시켜 미끄러짐/썰매 관성을 부여합니다.
 * - 패키지를 던졌을 때 미끄러지며 멀리 나아갑니다.
 * - 기름 바닥(bIsFlammable=true)일 경우 화염이 닿으면 불바다(DoT 화염 구역)로 전환됩니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API ASlipperySurface : public AActor, public ITrapTriggerableInterface
{
	GENERATED_BODY()
	
public:	
	ASlipperySurface();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> RootScene;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SurfaceMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> SlipperyVolume;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> FireFX;

	// ---------------------------------------------------------
	// [지면 마찰 설정]
	// ---------------------------------------------------------

	/** 미끄러운 지면 마찰력 (기본 8.0 -> 0.1로 감소) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slippery|Physics")
	float SlipperyGroundFriction = 0.1f;

	/** 제동 감속도 (Braking Deceleration, 기본 2048 -> 100으로 감소) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slippery|Physics")
	float SlipperyBrakingDeceleration = 100.0f;

	/** 인화성 기름 바닥 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slippery|Fire")
	bool bIsFlammable = false;

	/** 현재 불이 붙었는지 여부 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_bIsIgnited, Category = "Slippery|Fire")
	bool bIsIgnited = false;

	/** 화염 지속 피해량 (초당) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slippery|Fire")
	float FireDamagePerSecond = 15.0f;

	/** 바닥에 불 붙이기 */
	UFUNCTION(BlueprintCallable, Category = "Slippery|Fire")
	void IgniteSurface();

	// ---------------------------------------------------------
	// [ITrapTriggerableInterface 구현]
	// ---------------------------------------------------------

	virtual void TriggerTrap_Implementation(AActor* InstigatorActor) override;
	virtual void ResetTrap_Implementation() override;
	virtual void DisarmTrap_Implementation(AActor* Disarmer) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleSurfaceBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleSurfaceEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	UFUNCTION()
	void OnRep_bIsIgnited();

	void ApplyFireDamageTick();

protected:
	UPROPERTY(Transient)
	TMap<TObjectPtr<ACharacter>, float> OriginalFrictionMap;

	UPROPERTY(Transient)
	TMap<TObjectPtr<ACharacter>, float> OriginalBrakingMap;

	FTimerHandle FireTimerHandle;
};
