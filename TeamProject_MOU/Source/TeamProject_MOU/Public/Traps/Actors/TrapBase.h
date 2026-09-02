#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Traps/TrapEnums.h"
#include "Traps/Interfaces/TrapTriggerableInterface.h"
#include "Components/BoxComponent.h"
#include "TrapBase.generated.h"

class UTrapDataAsset;
class UTrapTriggerComponent;
class UTrapPayloadComponent;
class USceneComponent;

/**
 * ATrapBase
 * 모든 함정의 최상위 베이스 액터 클래스입니다.
 * FSM 상태 머신 관리, 네트워크 상태 복제(RepNotify), 데이터 에셋 연동,
 * 그리고 모듈러 트리거/페이로드 컴포넌트의 라이프사이클을 총괄합니다.
 */
UCLASS(Abstract)
class TEAMPROJECT_MOU_API ATrapBase : public AActor, public ITrapTriggerableInterface
{
	GENERATED_BODY()
	
public:	
	ATrapBase();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---------------------------------------------------------
	// [컴포넌트]
	// ---------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> RootScene;

	/** 자체 밟힘/접촉 감지용 기본 트리거 박스 (단독 압력판 작동 시 활용) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> BaseTriggerBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTrapTriggerComponent> TriggerComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTrapPayloadComponent> PayloadComponent;

	// ---------------------------------------------------------
	// [데이터 에셋 및 기본 설정]
	// ---------------------------------------------------------

	/** 함정의 수치/타이머/이펙트 설정을 담은 데이터 에셋 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trap|Data")
	TObjectPtr<UTrapDataAsset> TrapData;

	/** 함정의 현재 복제된 상태 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_TrapState, Category = "Trap|State")
	ETrapState CurrentState = ETrapState::Idle;

	// ---------------------------------------------------------
	// [ITrapTriggerableInterface 구현]
	// ---------------------------------------------------------

	virtual void TriggerTrap_Implementation(AActor* InstigatorActor) override;
	virtual void ResetTrap_Implementation() override;
	virtual void DisarmTrap_Implementation(AActor* Disarmer) override;

	// ---------------------------------------------------------
	// [상태 머신 및 제어 함수]
	// ---------------------------------------------------------

	/** 상태 전환 요청 (서버 권한 전용) */
	UFUNCTION(BlueprintCallable, Category = "Trap|State")
	void SetTrapState(ETrapState NewState);

	/** 현재 상태 반환 */
	UFUNCTION(BlueprintPure, Category = "Trap|State")
	ETrapState GetTrapState() const { return CurrentState; }

	/** 함정 활성화 가능 여부 확인 */
	UFUNCTION(BlueprintPure, Category = "Trap|State")
	virtual bool CanActivate() const;

	/** TriggerType(압력판 vs 레이저연동 등)에 맞춰 BaseTriggerBox 콜리전을 동기화 */
	UFUNCTION(BlueprintCallable, Category = "Trap|Trigger")
	void UpdateTriggerBoxCollision();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	virtual void HandleTriggerActivated(AActor* InstigatorActor);

	UFUNCTION()
	virtual void HandleTriggerDeactivated();

	UFUNCTION()
	virtual void OnRep_TrapState(ETrapState PreviousState);

	/** 상태 변경 시 공통 핸들러 */
	virtual void OnStateEntered(ETrapState NewState);

	/** 상태별 내부 타이머 콜백 */
	virtual void OnWarningTimerExpired();
	virtual void OnActiveTimerExpired();
	virtual void OnCooldownTimerExpired();

	/** 실제 발동 로직 (파생 클래스에서 특화 구현 가능) */
	virtual void ExecuteTrapPayload();

	// ---------------------------------------------------------
	// [시각/음향 멀티캐스트 RPC]
	// ---------------------------------------------------------

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayStateFX(ETrapState StateToPlay);

	// ---------------------------------------------------------
	// [Blueprint 연출 이벤트]
	// ---------------------------------------------------------

	UFUNCTION(BlueprintImplementableEvent, Category = "Trap|Events")
	void OnTrapStateChanged_BP(ETrapState NewState, ETrapState OldState);

	UFUNCTION(BlueprintImplementableEvent, Category = "Trap|Events")
	void OnTrapWarning_BP();

	UFUNCTION(BlueprintImplementableEvent, Category = "Trap|Events")
	void OnTrapActivated_BP();

	UFUNCTION(BlueprintImplementableEvent, Category = "Trap|Events")
	void OnTrapCooldown_BP();

	UFUNCTION(BlueprintImplementableEvent, Category = "Trap|Events")
	void OnTrapDisarmed_BP();

protected:
	FTimerHandle StateTimerHandle;

	UPROPERTY(Transient)
	TObjectPtr<AActor> LastInstigator;
};
