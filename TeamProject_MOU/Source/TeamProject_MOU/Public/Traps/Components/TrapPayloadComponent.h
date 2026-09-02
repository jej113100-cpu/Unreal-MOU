#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Traps/TrapEnums.h"
#include "TrapPayloadComponent.generated.h"

class UTrapDataAsset;

/**
 * UTrapPayloadComponent
 * 함정이 발동되었을 때 대상(플레이어, 몬스터, 패키지)에게
 * 대미지, GAS 상태이상, 물리 밀침, 배터리 방전, 아이템 드랍 효과를 인가하는 컴포넌트입니다.
 */
UCLASS(ClassGroup = (Traps), meta = (BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UTrapPayloadComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTrapPayloadComponent();

	/** 단일 대상 액터에게 함정 효과 실행 */
	UFUNCTION(BlueprintCallable, Category = "Trap|Payload")
	void ExecutePayloadOnActor(AActor* TargetActor, const UTrapDataAsset* InTrapData = nullptr);

	/** 다수 대상 액터들에게 함정 효과 일괄 실행 */
	UFUNCTION(BlueprintCallable, Category = "Trap|Payload")
	void ExecutePayloadOnActors(const TArray<AActor*>& TargetActors, const UTrapDataAsset* InTrapData = nullptr);

	/** 인가할 기본 위험 유형 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap|Payload")
	ETrapHazardType DefaultHazardType = ETrapHazardType::Damage;

	/** 기본 피해량 (TrapDataAsset이 없을 경우의 폴백) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap|Payload")
	float FallbackDamage = 20.0f;

	/** 물리 밀침 방향 및 강도 계산 시 기준 벡터 (기본: 상방 + Z) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap|Payload")
	FVector CustomImpulseDirection = FVector::UpVector;
};
