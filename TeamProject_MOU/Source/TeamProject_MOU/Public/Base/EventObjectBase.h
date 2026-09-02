#pragma once

#include "CoreMinimal.h"
#include "Base/ItemBase.h"
#include "Interfaces/PushableInterface.h"
#include "EventObjectBase.generated.h"

/**
 * AEventObjectBase
 * 레벨에 배치되어 줍기(들기), 던지기, 상호작용 및 '밀기(Push)'가 모두 가능한 기믹용 베이스 클래스입니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API AEventObjectBase : public AItemBase, public IPushableInterface
{
	GENERATED_BODY()
	
public:
	AEventObjectBase();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	// 지면/터널관 감지 전용 박스 컴포넌트 (에디터 뷰포트에서 기즈모로 위치와 크기를 직접 조절 가능)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<class UBoxComponent> GroundDetectorBox;

	virtual void Tick(float DeltaTime) override;

	// --- IPushableInterface ---
	virtual float GetPushResistance_Implementation() const override;
	virtual void Push_Implementation(AActor* Pusher, FVector PushDirection) override;

	// 상호작용(F) 오버라이드
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;

	// 밀기 가능한 최대 거리 설정 (상호작용 진입 가능 거리)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push")
	float MaxPushDistance = 300.0f;

	// 밀기 중 상자와의 거리가 이 값(기본 110cm) 이상 벌어지면 밀기 모드 자동 해제
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push")
	float PushDetachDistance = 110.0f;

	// 밀기 가능 여부 토글
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push")
	bool bIsPushable = true;

	// 밀기/당기기에 필요한 최소 인원 수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push")
	int32 RequiredPushers = 1;

	// 디버그 라인으로 밀기 가능 거리 표시 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push|Debug")
	bool bShowDebugPushDistance = false;

	// 지면 감지 포인트 및 트레이스 디버그 라인 표시 여부 (에디터/인게임 실시간 토글)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push|Debug")
	bool bShowDebugGroundTrace = true;

	// 현재 이 상자를 잡고(밀고) 있는 캐릭터 목록 (서버에서 관리 및 모든 클라이언트에 복제)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Push")
	TArray<TObjectPtr<class AMainCharacter>> CurrentPushers;

	// 각 푸셔에게 분배되어 적용된 무게를 추적하는 맵
	UPROPERTY(Transient)
	TMap<TObjectPtr<class AMainCharacter>, float> AppliedWeightMap;

	// 각 푸셔의 상자 로컬 기준 손 접촉 앵커 위치 맵 (거리 이탈 감지용)
	UPROPERTY(Transient)
	TMap<TObjectPtr<class AMainCharacter>, FVector> PusherLocalAnchorMap;

	// 지면 지지율 검사 시 최소 요구 지지점 개수 (기본값: 최소 3개 이상 지지되어야 밀기 유지)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push|Ground")
	int32 MinRequiredGroundPoints = 3;

	// 지면 접촉 감지 허용 여유 거리 (cm) - 파이프 구멍/낭떠러지는 허공으로 걸러내고 실제 지지 접촉면만 감지
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push|Ground")
	float GroundTraceTolerance = 50.0f;

	// 평상시 물리 상태에서 폰과 충돌 시 날아가지 않도록 부여할 기본 질량(kg)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push|Physics")
	float PhysicalMassInKg = 1000.0f;

	// GroundDetectorBox 기준 다중 포인트 지면 지지율 검사 함수 (지지면 Z값 및 평균 지면 노멀 반환)
	bool CheckGroundSupport(FHitResult& OutFloorHit, int32& OutSupportedCount, float& OutSupportZ) const;




	// 물리 시뮬레이션 모드 안전 전환 (서버/클라이언트)
	void SetPhysicsSimulateEnabled(bool bEnablePhysics);



	// 밀기 모드 진입/해제 시 호출
	void AddPusher(class AMainCharacter* Pusher);
	void RemovePusher(class AMainCharacter* Pusher);

	// 푸셔들에게 상자 무게를 균등 분배하고 캐릭터 과적(속도)에 반영하는 함수
	void UpdatePushersWeight();

	// 현재 밀기/당기기가 가능한 상태(인원수 충족)인지 확인
	bool IsReadyToMove() const;

	// 낭떠러지에서 떨어질 때 호출 (로컬 전용)
	void FallOffLedge();

	// 서버가 추락을 감지했을 때 모든 클라이언트에게 추락(물리 켜기 및 밀기 해제)을 동기화
	UFUNCTION(NetMulticast, Reliable)
	void MulticastFallOffLedge();
};

