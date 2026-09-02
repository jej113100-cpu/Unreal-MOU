#pragma once

#include "CoreMinimal.h"
#include "Traps/Actors/TrapBase.h"
#include "TrapElectricPanel.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class UNiagaraComponent;

/**
 * ATrapElectricPanel
 * 바닥 패널에서 주기적으로 고전압 전기를 방전하는 테슬라 코일형 전기 함정입니다.
 * - 경고 단계: 미세한 스파크 및 파직거리는 전기음
 * - 발동 단계: 푸른 번개 방전으로 범위 내 캐릭터 감전(기절) + 체력 피해 + 손전등 배터리 30% 즉시 방전
 */
UCLASS()
class TEAMPROJECT_MOU_API ATrapElectricPanel : public ATrapBase
{
	GENERATED_BODY()

public:
	ATrapElectricPanel();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> PanelMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> SparkFX;

	// ---------------------------------------------------------
	// [전기 함정 고유 설정]
	// ---------------------------------------------------------

	/** 발동 시 손전등 배터리 방전량 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Electric|Config")
	float BatteryDrain = 30.0f;

	/** 비/물/오일 지형 연계 시 방전 범위 확장 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Electric|Config")
	float WetZoneRadiusMultiplier = 1.5f;

protected:
	virtual void BeginPlay() override;
	virtual void OnStateEntered(ETrapState NewState) override;
	virtual void ExecuteTrapPayload() override;
};
