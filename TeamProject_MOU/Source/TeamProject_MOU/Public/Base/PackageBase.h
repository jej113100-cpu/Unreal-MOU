#pragma once

#include "CoreMinimal.h"
#include "Base/ItemBase.h"
#include "Interfaces/PushableInterface.h"
#include "Traps/Interfaces/TrapTargetInterface.h"
#include "PackageBase.generated.h"

UENUM(BlueprintType)
enum class EPackageType : uint8
{
	Normal UMETA(DisplayName = "일반 물품"),
	Heavy UMETA(DisplayName = "무거운 물품"),
	Fragile UMETA(DisplayName = "깨지기 쉬운 물품"),
	Perishable UMETA(DisplayName = "상하기 쉬운 물품"),
	Dangerous UMETA(DisplayName = "위험 물품"),
	Quest UMETA(DisplayName = "퀘스트 물품")
};

UCLASS()
class TEAMPROJECT_MOU_API APackageBase : public AItemBase, public IPushableInterface, public ITrapTargetInterface
{
	GENERATED_BODY()
	
public:
	APackageBase();

protected:
	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	virtual void Tick(float DeltaTime) override;

	// ---------------------------------------------------------
	// [택배 기본 데이터]
	// ---------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Package|Data")
	int32 BaseValue = 100;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Package|Data")
	EPackageType PackageType = EPackageType::Normal;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Package|Data")
	float MaxSpoilTime = 0.0f; // 0이면 상하지 않음

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Package|Status")
	float CurrentSpoilTime = 0.0f;

	// 현재 내구도 및 신선도(상함)를 반영한 실제 가치 계산
	UFUNCTION(BlueprintCallable, Category = "Package|Data")
	int32 GetCurrentValue() const;

	// 던져진 택배에 맞아 넘어지는 최소 속도 기준
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Package|Data")
	float KnockdownThresholdSpeed = 500.0f;

	// ---------------------------------------------------------
	// [운반 부착 포인트 (핸들)]
	// ---------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<class USceneComponent> Handle_Front;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<class USceneComponent> Handle_Back;

	// [1인 운반 전용 회전 보정] 1인 들기 및 1인 드래그 시 메쉬 방향을 보정하는 회전값 (2인 협동 운반에는 영향을 주지 않음)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Package|Transform")
	FRotator SingleCarryRotationOffset = FRotator::ZeroRotator;

	// ---------------------------------------------------------
	// [협동 운반 데이터]
	// ---------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentCarriers, Category = "Package|Coop")
	TArray<TObjectPtr<AActor>> CurrentCarriers;

	UFUNCTION()
	void OnRep_CurrentCarriers();

	UFUNCTION(BlueprintCallable, Category = "Package|Coop")
	void AddCarrier(AActor* Carrier);

	UFUNCTION(BlueprintCallable, Category = "Package|Coop")
	void RemoveCarrier(AActor* Carrier);

	// 운반자들의 스피드 배율을 계산하고 적용하는 함수
	UFUNCTION(BlueprintCallable, Category = "Package|Coop")
	void UpdateCarriersSpeedModifier();

	// 각 운반자에게 분배되어 적용된 무게를 추적하는 맵
	UPROPERTY(Transient)
	TMap<AActor*, float> AppliedWeightMap;

	// 현재 패키지의 속도 비율 (끌림 판정용)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Package|Coop")
	float CurrentSpeedRatio = 1.0f;

	// 운반자들이 택배 손잡이/소켓 위치로부터 떨어질 수 있는 최대 허용 거리 (80cm 초과 시 자동 Drop 및 1인 들기 복귀)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Package|Coop")
	float MaxCarryDistance = 80.0f;

	// [2인 협동] 두 번째 운반자 손 소켓 이름 (공통 CarrySocket 사용 가능)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Package|Coop")
	FName CarrySocketName = TEXT("CarrySocket");

	// [1인 드래그] 혼자 무거운 택배를 끌 때 패키지가 붙는 위치 소켓 (등/어깨 쪽)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Package|Coop")
	FName DragSocketName = TEXT("DragSocket");

	// [무거운 택배 전용] Handle_Front와 Handle_Back 사이의 거리 (메시에 맞게 조절)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Package|Coop")
	float HandleLength = 100.0f;

	virtual bool CanBePickedUpBy(AActor* PotentialPicker) const override;


protected:
	// [무거운 택배 전용] Tick에서 서버가 패키지 위치/회전을 계산하고 이동시킴
	void UpdateHeavyPackagePosition(float DeltaTime);

	// [전환 유예] 2→1인 전환 직후 이탈 검사를 잠시 스킵하여 강제 드랍 연쇄 방지
	float TransitionGracePeriod = 0.0f;

public:

	// ---------------------------------------------------------
	// [내구도 및 파손 시스템]
	// ---------------------------------------------------------
	
	// 물리적 충돌 감지 콜백
	UFUNCTION()
	void OnPackageHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	// 패키지에 대미지를 가함
	UFUNCTION(BlueprintCallable, Category = "Package|Durability")
	void DamagePackage(float DamageAmount);

	// 패키지 파손 시 호출되는 이벤트 (시각적 찌그러짐 등 연출용) - 블루프린트에서 구현
	UFUNCTION(BlueprintImplementableEvent, Category = "Package|Durability")
	void OnPackageBroken();

	// [Replicated] 파손 상태 - 복제되면 클라이언트에서 OnRep이 연출을 자동 실행
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_bIsBroken, Category = "Package|Durability")
	bool bIsBroken = false;

protected:
	UFUNCTION()
	void OnRep_bIsBroken();

	// [RPC] 모든 클라이언트에서 파손 연출을 재생하기 위한 Multicast
	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnPackageBroken();

public:
	// ---------------------------------------------------------
	// [아이템 베이스 오버라이드]
	// ---------------------------------------------------------
	
	// 택배는 일반 아이템처럼 우클릭/좌클릭 사용이 불가능함
	virtual void OnUse_Implementation() override;

	// 택배는 F키 상호작용 대상이 아니며, 오직 E키로만 잡고/들고/던집니다.
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;

	// ItemBase 공통 저장 데이터에 택배 전용 상태를 추가합니다.
	virtual void SaveItemToData_Implementation(FStoredItemInstanceData& OutData) const override;
	virtual void LoadItemFromData_Implementation(const FStoredItemInstanceData& InData) override;

	// [클라이언트 동기화] 물리 콜리전 상태 동기화를 위해 오버라이드
	virtual void MulticastPickUp_Implementation(AActor* Picker) override;
	virtual void MulticastDrop_Implementation(FVector DropLocation, AActor* Dropper = nullptr) override;

	// ---------------------------------------------------------
	// [밀기 인터페이스 구현 (IPushableInterface)]
	// ---------------------------------------------------------
	virtual float GetPushResistance_Implementation() const override;
	virtual void Push_Implementation(AActor* Pusher, FVector PushDirection) override;

	// ---------------------------------------------------------
	// [함정 타겟 인터페이스 구현 (ITrapTargetInterface)]
	// ---------------------------------------------------------
	virtual void ApplyTrapDamage_Implementation(float DamageAmount, AActor* Causer) override;
	virtual void ApplyTrapStatusEffect_Implementation(TSubclassOf<UGameplayEffect> EffectClass, AActor* Causer) override;
	virtual void ApplyTrapImpulse_Implementation(FVector ImpulseVector) override;
	virtual void ForceDropCarriedItem_Implementation() override;
	virtual void DrainBattery_Implementation(float DrainAmount) override;
	virtual void OnTrapHazardEncountered_Implementation(ETrapHazardType HazardType, AActor* TrapActor) override;
};
