#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SluiceGate.generated.h"

class UMaterialParameterCollection;
class UMOUWaterBodyLakeComponent;
class UMOUWaterBodyRiverComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSluiceGateRatioChanged, float, CurrentRatio, float, Delta);

UCLASS()
class TEAMPROJECT_MOU_API ASluiceGate : public AActor
{
	GENERATED_BODY()
	
public:
	ASluiceGate();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> DefaultRootScene;

	// --- Gate Movement Configuration ---
	// Z축으로 오르내릴 대상 컴포넌트 (지정하지 않으면 자식 컴포넌트 중 'Gate'/'Door' 자동 검색)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceGate|Movement")
	TObjectPtr<USceneComponent> TargetDoorComponent;

	// 수문이 완전히 닫힐 때 아래로 내려가는 총 이동 거리 (기본값: 1000.0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceGate|Movement")
	float GateTravelDistance = 1000.0f;

	// 시작 시 완전 열림 상태로 시작할지 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceGate|Movement")
	bool bStartFullyOpen = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceGate|Movement")
	float InterpSpeed = 4.0f;

	// --- Water Bodies Integration ---
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "SluiceGate|Water")
	TObjectPtr<AActor> LakeWaterActor;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "SluiceGate|Water")
	TObjectPtr<AActor> RiverWaterActor;

	// --- Water Material Parameter Collection Integration (Shader Level Control) ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceGate|Water|Material")
	TObjectPtr<UMaterialParameterCollection> WaterMaterialParameterCollection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceGate|Water|Material")
	FName LakeZParameterName = FName("LakeWaterZ");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceGate|Water|Material")
	FName RiverZParameterName = FName("RiverWaterZ");

	// 수문이 닫히며(내려오며) 수위가 변하기 시작하는 수문 닫힘 진행률 임계치 (0.0~1.0, 기본 0.7 = 70% 열림 시점부터 수위 변화)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceGate|Water")
	float WaterThresholdRatio = 0.7f;

	// 호수(Lake) 시작/기본 수위 (기본값: 100.0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceGate|Water")
	float LakeInitialZ = 100.0f;

	// 수문 완전 닫힘 시 호수(Lake) 최대 수위 (기본값: 300.0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceGate|Water")
	float LakeMaxZ = 300.0f;

	// 강(River) 시작/기본 수위 (기본값: 15.0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceGate|Water")
	float RiverInitialZ = 15.0f;

	// 수문 완전 닫힘 시 강(River) 최저 수위 (기본값: -200.0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SluiceGate|Water")
	float RiverMinZ = -200.0f;

	// --- State & Replication ---
	UPROPERTY(ReplicatedUsing = OnRep_CurrentOpenRatio, BlueprintReadOnly, Category = "SluiceGate|State")
	float CurrentOpenRatio = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "SluiceGate|State")
	float TargetOpenRatio = 1.0f;

	UFUNCTION()
	void OnRep_CurrentOpenRatio();

	UFUNCTION(BlueprintCallable, Category = "SluiceGate")
	void SetTargetOpenRatio(float NewRatio);

	UFUNCTION(Server, Reliable)
	void ServerSetTargetOpenRatio(float NewRatio);

	UFUNCTION(BlueprintPure, Category = "SluiceGate")
	float GetCurrentOpenRatio() const { return CurrentOpenRatio; }

	UPROPERTY(BlueprintAssignable, Category = "SluiceGate|Events")
	FOnSluiceGateRatioChanged OnGateRatioChanged;

	UFUNCTION(BlueprintImplementableEvent, Category = "SluiceGate|Events")
	void OnGateStateUpdated(float Ratio);

protected:
	UPROPERTY(Transient)
	FVector InitialDoorRelativeLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector InitialGateLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	float VisualOpenRatio = 1.0f;

	UPROPERTY(Transient)
	TWeakObjectPtr<UMOUWaterBodyLakeComponent> CachedLakeComp;

	UPROPERTY(Transient)
	TWeakObjectPtr<UMOUWaterBodyRiverComponent> CachedRiverComp;

	void UpdateGateMovement(float DeltaTime);
	void UpdateWaterActors(float DeltaTime);
};
