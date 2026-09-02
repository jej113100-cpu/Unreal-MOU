#pragma once

#include "CoreMinimal.h"
#include "Base/EventObjectBase.h"
#include "Gimmick/SluiceGate.h"
#include "SluiceWheel.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSluiceWheelTurned, float, CurrentProgress, float, DeltaAngle);

UCLASS()
class TEAMPROJECT_MOU_API ASluiceWheel : public AEventObjectBase
{
	GENERATED_BODY()

public:
	ASluiceWheel();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BasePlatformMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> RotatingWheelMesh;

	// --- Target Sluice Gate ---
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "SluiceWheel|Target")
	TObjectPtr<ASluiceGate> TargetGate;

	// --- Wheel Rotation Settings ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceWheel|Settings")
	float FullTurnsForFullOpen = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceWheel|Settings")
	float HandleRadius = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceWheel|Settings")
	float TurnSpeedDegreesPerSec = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceWheel|Settings")
	bool bStartFullyOpen = true;

	// --- State & Replication ---
	UPROPERTY(ReplicatedUsing = OnRep_CurrentTurnProgress, BlueprintReadOnly, Category = "SluiceWheel|State")
	float CurrentTurnProgress = 1.0f;

	UFUNCTION()
	void OnRep_CurrentTurnProgress();

	UFUNCTION(BlueprintCallable, Category = "SluiceWheel")
	void SetTurnProgress(float NewProgress);

	UFUNCTION(Server, Reliable)
	void ServerSetTurnProgress(float NewProgress);

	UFUNCTION(BlueprintPure, Category = "SluiceWheel")
	float GetTurnProgress() const { return CurrentTurnProgress; }

	UPROPERTY(BlueprintAssignable, Category = "SluiceWheel|Events")
	FOnSluiceWheelTurned OnWheelTurned;

	UFUNCTION(BlueprintImplementableEvent, Category = "SluiceWheel|Events")
	void OnWheelRotated(float Progress, float CurrentYaw);

	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;

protected:
	UPROPERTY(Transient)
	float VisualWheelYaw = 0.0f;

	void UpdateWheelVisuals(float DeltaTime);
};
