#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "SlowSurface.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UBoxComponent;
class UNiagaraComponent;
class ACharacter;

/**
 * ASlowSurface
 * 진흙탕(Mud), 타르 웅덩이(Tar Pit), 거미줄(Spider Web), 모래늪(Quicksand) 등
 * 영역에 진입한 대상의 이동 속도를 대폭 감속시키고 점프를 억제/금지하는 상시 환경 지형 장애물입니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API ASlowSurface : public AActor
{
	GENERATED_BODY()
	
public:	
	ASlowSurface();

	// ---------------------------------------------------------
	// [컴포넌트]
	// ---------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> RootScene;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SurfaceMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> SlowVolume;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> SplashFX;

	// ---------------------------------------------------------
	// [감속 및 이동 제어 설정]
	// ---------------------------------------------------------

	/** 기본 이동 속도 감속 배율 (예: 0.4 -> 기존 속도의 40%로 감속) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slow|Movement")
	float SlowSpeedMultiplier = 0.4f;

	/** 점프 완전 금지 여부 (타르 웅덩이 / 모래늪용) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slow|Movement")
	bool bDisableJump = false;

	/** 점프 높이 감소 배율 (bDisableJump가 false일 때 적용, 예: 0.5 -> 점프력 50% 삭감) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slow|Movement")
	float JumpVelocityMultiplier = 0.5f;

	/** 선택적 GAS 슬로우 GameplayEffect 클래스 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slow|GAS")
	TSubclassOf<UGameplayEffect> SlowGameplayEffectClass;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleSurfaceBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleSurfaceEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void ApplySlowToCharacter(ACharacter* Character);
	void RestoreCharacterMovement(ACharacter* Character);

protected:
	UPROPERTY(Transient)
	TMap<TObjectPtr<ACharacter>, float> OriginalSpeedMap;

	UPROPERTY(Transient)
	TMap<TObjectPtr<ACharacter>, float> OriginalJumpMap;
};
