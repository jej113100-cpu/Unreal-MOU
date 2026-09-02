#pragma once

#include "CoreMinimal.h"
#include "Traps/Actors/TrapBase.h"
#include "TrapCrusher.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class UArrowComponent;

/**
 * ATrapCrusher
 * 천장이나 벽면에 설치되어 굉음 전조 후 급강하하여 하단의 대상(플레이어, 몬스터, 패키지)을
 * 압살하는 대형 유압식 프레스/낙하 압살 함정입니다.
 * - 단일 CrusherMesh가 이동 방향(CrushDirectionArrow)으로 하강하고 복귀합니다.
 * - 하단에 상자(AEventObjectBase)가 끼어 있으면 하강이 저지되는 물리 퍼즐을 지원합니다.
 * - 타격 시 MulticastOnCrushImpact를 통해 모든 클라이언트에서 즉시 멈추도록 동기화합니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API ATrapCrusher : public ATrapBase
{
	GENERATED_BODY()

public:
	ATrapCrusher();

	virtual void Tick(float DeltaTime) override;

	// ---------------------------------------------------------
	// [컴포넌트]
	// ---------------------------------------------------------

	/** 압살 헤드가 이동할 방향을 에디터 뷰포트에서 자유롭게 회전/지정할 수 있는 방향 화살표 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> CrushDirectionArrow;

	/** 실제로 내려찍는 단일 압살기 메시 (돌 블록, 큐브, 가시 천장 등) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CrusherMesh;

	/** 압살 헤드 하단의 타격/대미지 감지 영역 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> CrushDamageBox;

	// ---------------------------------------------------------
	// [압살기 이동 설정]
	// ---------------------------------------------------------

	/** 압살기 최대 하강 이동 거리 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crusher|Movement")
	float MaxDropDistance = 400.0f;

	/** 하강(압살) 속도 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crusher|Movement")
	float DropSpeed = 1600.0f;

	/** 상승(복귀) 속도 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crusher|Movement")
	float RetractSpeed = 300.0f;

	/** 압살 헤드가 완전히 내려왔을 때 머무르는 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crusher|Movement")
	float BottomHoldDuration = 0.5f;

	/** 바닥 상자에 의해 저지되었는지 여부 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crusher|Status")
	bool bIsBlocked = false;

protected:
	virtual void BeginPlay() override;
	virtual void OnStateEntered(ETrapState NewState) override;
	virtual void ExecuteTrapPayload() override;

	UFUNCTION()
	void HandleCrushOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void UpdateCrusherMovement(float DeltaTime);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnCrushImpact(FVector ImpactRelativeLocation);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnCrushBlocked();

protected:
	FVector InitialCrusherRelativeLocation;
	FVector TargetCrusherRelativeLocation;
	bool bIsDescending = false;
	bool bIsAscending = false;
	float BottomHoldTimer = 0.0f;
};
