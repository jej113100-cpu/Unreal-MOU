#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Traps/TrapEnums.h"
#include "GameplayEffect.h"
#include "TrapDataAsset.generated.h"

class UNiagaraSystem;
class USoundBase;

/**
 * UTrapDataAsset
 * 함정의 수치(피해량, 타이머, 쿨다운) 및 이펙트/사운드/GAS 클래스를 정의하는 데이터 에셋입니다.
 */
UCLASS(BlueprintType)
class TEAMPROJECT_MOU_API UTrapDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------
	// [기본 타이머 및 수치 설정]
	// ---------------------------------------------------------

	/** 감지 후 실제 발동까지의 전조/경고 지연 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing")
	float ActivationDelay = 0.5f;

	/** 발동(Active) 상태가 유지되는 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing")
	float ActiveDuration = 1.0f;

	/** 발동 종료 후 다음 감지가 가능해질 때까지의 쿨다운 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing")
	float CooldownDuration = 3.0f;

	/** 주기적 발동 모드일 때의 발동 간격 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing")
	float PeriodicInterval = 4.0f;

	// ---------------------------------------------------------
	// [위험 효과 및 수치 (Payload)]
	// ---------------------------------------------------------

	/** 함정 기본 피해량 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hazard")
	float BaseDamage = 30.0f;

	/** 손전등 배터리 방전량 (전기 함정 등) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hazard")
	float BatteryDrainAmount = 30.0f;

	/** 물리 충격/넉백 힘 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hazard")
	float ImpulseStrength = 800.0f;

	/** 적용할 GAS Gameplay Effect 클래스 (스턴, 출혈, 둔화, 감전 등) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hazard|GAS")
	TSubclassOf<UGameplayEffect> StatusEffectClass;

	/** 소지 물품 강제 드랍 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hazard")
	bool bForceDropItem = false;

	// ---------------------------------------------------------
	// [시각 및 음향 연출 에셋]
	// ---------------------------------------------------------

	/** 경고/전조 단계 나이아가라 이펙트 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FX|Niagara")
	TObjectPtr<UNiagaraSystem> WarningFX;

	/** 발동 단계 나이아가라 이펙트 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FX|Niagara")
	TObjectPtr<UNiagaraSystem> ActivateFX;

	/** 경고/전조 사운드 (치익-, 삐빅 등) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FX|Sound")
	TObjectPtr<USoundBase> WarningSound;

	/** 발동 사운드 (쾅!, 찌릿!, 팟! 등) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FX|Sound")
	TObjectPtr<USoundBase> ActivateSound;

	/** 해제/무력화 사운드 (찰칵, 틱 등) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FX|Sound")
	TObjectPtr<USoundBase> DisarmSound;
};
