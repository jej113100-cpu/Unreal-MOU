#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TrapTriggerableInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UTrapTriggerableInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 외부 트리거(레이저 센서, 스위치, 레버, 타이머)로부터 신호를 수신하여
 * 작동, 리셋, 무력화를 수행하는 함정 액터용 인터페이스
 */
class TEAMPROJECT_MOU_API ITrapTriggerableInterface
{
	GENERATED_BODY()

public:
	/** 함정을 발동시킵니다. (InstigatorActor: 발동 원인 제공자) */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Trap|Trigger")
	void TriggerTrap(AActor* InstigatorActor);

	/** 함정을 초기 대기 상태로 리셋합니다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Trap|Trigger")
	void ResetTrap();

	/** 함정을 일시적 또는 영구적으로 해제/무력화합니다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Trap|Trigger")
	void DisarmTrap(AActor* Disarmer);
};
