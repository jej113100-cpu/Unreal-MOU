#pragma once

#include "CoreMinimal.h"
#include "Traps/Actors/TrapBase.h"
#include "TrapBearTrap.generated.h"

class UStaticMeshComponent;
class UBoxComponent;

/**
 * ATrapBearTrap
 * 밟았을 때 강력한 턱이 닫히며 발목을 붙잡는 곰덫 함정입니다.
 * - 밟은 대상의 이동을 속박(Hold)하고 소지 물품을 강제 드랍시킵니다.
 * - 1인 탈출(F키 5회 연타) 및 2인 협동 구출(팀원이 F키를 누르면 1초 만에 즉시 해제)을 지원합니다.
 * - 몬스터가 밟을 경우 장시간 이동 불가 상태가 되어 도주 기회를 제공합니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API ATrapBearTrap : public ATrapBase
{
	GENERATED_BODY()

public:
	ATrapBearTrap();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BaseMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> LeftJawMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> RightJawMesh;

	// ---------------------------------------------------------
	// [곰덫 고유 설정]
	// ---------------------------------------------------------

	/** 1인 자력 탈출 시 필요한 키 입력(연타) 횟수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BearTrap|Escape")
	int32 RequiredEscapeInputs = 5;

	/** 플레이어 최대 속박 지속 시간 (초, 시간 경과 시 자동 풀림) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BearTrap|Escape")
	float MaxHoldDuration = 6.0f;

	/** 몬스터(NPC) 속박 지속 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BearTrap|Escape")
	float MonsterHoldDuration = 8.0f;

	/** 현재 붙잡힌 액터 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_TrappedActor, Category = "BearTrap|Status")
	TObjectPtr<AActor> TrappedActor;

	/** 1인 자력 탈출 시도 (F키 입력) */
	UFUNCTION(BlueprintCallable, Category = "BearTrap|Interaction")
	void RequestSelfEscape(AActor* EscapingActor);

	/** 팀원 협동 해제 지원 (다른 플레이어가 F키 입력 시 즉시 해제) */
	UFUNCTION(BlueprintCallable, Category = "BearTrap|Interaction")
	void AssistDisarm(AActor* HelperActor);

	virtual bool CanActivate() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void OnStateEntered(ETrapState NewState) override;
	virtual void ExecuteTrapPayload() override;

	UFUNCTION()
	void OnRep_TrappedActor();

	void ReleaseTrappedActor();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnJawSnap(bool bSnapped);

	UFUNCTION(BlueprintImplementableEvent, Category = "BearTrap|Events")
	void OnJawSnap_BP(bool bSnapped);

protected:
	int32 CurrentEscapeInputCount = 0;
	FTimerHandle AutoReleaseTimerHandle;
};
