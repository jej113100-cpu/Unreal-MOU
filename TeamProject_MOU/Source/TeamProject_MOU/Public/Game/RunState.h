#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Game/RunTypes.h"
#include "RunState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnThreatLevelChanged, float, ThreatLevel);

UCLASS(BlueprintType)
class TEAMPROJECT_MOU_API ARunState : public AInfo
{
	GENERATED_BODY()

public:
	ARunState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RunPhase, Category = "Run")
	ERunPhase RunPhase = ERunPhase::Playing;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RunEndReason, Category = "Run")
	ERunEndReason RunEndReason = ERunEndReason::None;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ThreatLevel, Category = "Run|Threat")
	float ThreatLevel = 0.0f;

	// NPCSpawner 등 위험도에 반응할 시스템이 직접 구독합니다.
	UPROPERTY(BlueprintAssignable, Category = "Run|Threat")
	FOnThreatLevelChanged OnThreatLevelChanged;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Run")
	void SetRunState(ERunPhase NewPhase, ERunEndReason NewReason);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Run|Threat")
	void SetThreatLevel(float NewThreatLevel);

	UFUNCTION(BlueprintPure, Category = "Run", meta = (WorldContext = "WorldContextObject"))
	static ARunState* GetRunState(const UObject* WorldContextObject);

	UFUNCTION(BlueprintImplementableEvent, Category = "Run")
	void OnRunStateUpdated(ERunPhase NewPhase, ERunEndReason NewReason);

	UFUNCTION(BlueprintImplementableEvent, Category = "Run|Threat")
	void OnThreatLevelUpdated(float NewThreatLevel);

private:
	UFUNCTION()
	void OnRep_RunPhase();

	UFUNCTION()
	void OnRep_RunEndReason();

	UFUNCTION()
	void OnRep_ThreatLevel();
};
