#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "Interfaces/InteractableInterface.h"
#include "VehicleBase.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UBoxComponent;
class ACharacterBase;
class AController;
struct FInputActionValue;

/**
 * FVehicleSeat
 * 차량의 좌석 하나를 표현하는 구조체.
 * 좌석 인덱스 0 = 운전석(SeatIndex 0), 그 외 = 동승석.
 * 소켓 이름은 에디터에서 차량 스켈레탈 메시의 소켓에 맞춰 지정한다.
 */
USTRUCT(BlueprintType)
struct FVehicleSeat
{
	GENERATED_BODY()

	// 이 좌석에 캐릭터를 붙일 메시 소켓 이름 (에디터에서 지정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Seat")
	FName SeatSocketName = NAME_None;

	// 이 좌석이 운전석인지 여부 (true면 이 좌석 탑승자가 차량을 조종)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Seat")
	bool bIsDriverSeat = false;

	// 현재 이 좌석에 앉아 있는 캐릭터 (비어 있으면 nullptr) - 런타임 상태
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Seat")
	TObjectPtr<ACharacterBase> Occupant = nullptr;
};

/**
 * AVehicleBase
 * 탑승형 차량 아이템의 베이스 클래스 (Chaos Wheeled Vehicle 기반).
 *
 * - F키 상호작용(IInteractableInterface)으로 가장 가까운 빈 좌석에 탑승/하차한다.
 * - 운전석 탑승자가 컨트롤러로 이 Pawn 을 Possess 하여 W/S(전후진), A/D(조향)로 조종한다.
 * - 운전자 카메라는 차량 정면 고정(마우스 시점 이동 차단), 동승자는 자기 캐릭터 시점 유지.
 * - 탑승/하차 상태 변경은 모두 서버 권위로 처리하며 좌석 점유는 복제된다.
 *
 * 좌석/소켓/바퀴 등 물리 세부 셋업은 에디터(BP 파생)에서 지정한다.
 */
UCLASS()
class TEAMPROJECT_MOU_API AVehicleBase : public AWheeledVehiclePawn, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AVehicleBase();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

public:
	// ---------------------------------------------------------
	// [카메라 / 상호작용 컴포넌트]
	// ---------------------------------------------------------

	// 운전 시 사용할 카메라 붐 (차량 뒤/위에 위치, 차량 방향 따라감)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicle|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	// 운전 시 사용할 팔로우 카메라
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicle|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	// 탑승 상호작용 감지용 콜라이더 (F키 포커스 대상 범위)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicle|Interaction")
	TObjectPtr<UBoxComponent> InteractionVolume;

	// ---------------------------------------------------------
	// [좌석 구성] - 운전석(0) + 동승석
	// ---------------------------------------------------------

	// 차량의 좌석 목록. 인덱스 0을 운전석으로 두는 것을 권장. 에디터에서 소켓/운전석 여부 지정.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Seats, Category = "Vehicle|Seat")
	TArray<FVehicleSeat> Seats;

	UFUNCTION()
	void OnRep_Seats();

	// 좌석 점유 상태가 바뀌었을 때(탑승/하차) BP 연출 훅 (문/자세 등)
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|Seat")
	void OnSeatsChanged();

	// ---------------------------------------------------------
	// [탑승 / 하차]
	// ---------------------------------------------------------

	// 지정 캐릭터를 가장 가까운 빈 좌석에 태운다 (서버 권위). 성공 시 true.
	UFUNCTION(BlueprintCallable, Category = "Vehicle|Ride")
	bool EnterVehicle(ACharacterBase* NewOccupant);

	// 지정 캐릭터를 차량에서 내리게 한다 (서버 권위).
	UFUNCTION(BlueprintCallable, Category = "Vehicle|Ride")
	void ExitVehicle(ACharacterBase* Occupant);

	// 클라이언트 → 서버 탑승 요청 (F키 상호작용 시 호출)
	UFUNCTION(Server, Reliable)
	void ServerRequestEnter(ACharacterBase* Requestor);

	// 클라이언트 → 서버 하차 요청
	UFUNCTION(Server, Reliable)
	void ServerRequestExit(ACharacterBase* Requestor);

	// 해당 캐릭터가 앉아 있는 좌석 인덱스 반환 (없으면 INDEX_NONE)
	UFUNCTION(BlueprintPure, Category = "Vehicle|Ride")
	int32 GetSeatIndexOf(const ACharacterBase* Character) const;

	// 비어 있는 가장 가까운 좌석 인덱스 반환 (없으면 INDEX_NONE)
	UFUNCTION(BlueprintPure, Category = "Vehicle|Ride")
	int32 FindNearestFreeSeat(const AActor* ForActor) const;

	// 현재 운전석에 탑승한 캐릭터 반환 (없으면 nullptr)
	UFUNCTION(BlueprintPure, Category = "Vehicle|Ride")
	ACharacterBase* GetDriver() const;

	// ---------------------------------------------------------
	// [IInteractableInterface] - F키 탑승 트리거
	// ---------------------------------------------------------
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractPrompt_Implementation() const override;

protected:
	// ---------------------------------------------------------
	// [운전 입력] - 운전석 탑승자가 이 Pawn 을 Possess 한 동안만 유효
	// ---------------------------------------------------------

	// W/S : 전진(+1)/후진(-1) 스로틀·브레이크 처리
	void OnThrottleInput(const FInputActionValue& Value);

	// A/D : 좌(-1)/우(+1) 조향. 누르는 동안 계속 조향값 유지.
	void OnSteerInput(const FInputActionValue& Value);

	// 하차 입력: 현재 운전자를 차량에서 내리게 한다.
	void OnExitInput();

	// 운전 입력 액션 (에디터에서 Enhanced Input Action 지정)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Input")
	TObjectPtr<class UInputAction> ThrottleAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Input")
	TObjectPtr<class UInputAction> SteerAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Input")
	TObjectPtr<class UInputAction> ExitAction;

	// 운전 시 적용할 입력 매핑 컨텍스트 (탑승 중에만 활성)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Input")
	TObjectPtr<class UInputMappingContext> DrivingMappingContext;

private:
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle|Drift", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float DriftRearGripScale = 0.45f;

	UPROPERTY(Replicated)
	bool bDrifting = false;

	UFUNCTION(Server, Reliable)
	void ServerSetDrifting(bool bEnabled);

	// Current vehicle layout: front wheels 0/1, rear wheels 2/3.
	TArray<float> DefaultWheelGrip;
	bool bLocalDriftRequested = false;

	// BP의 엔진 토크를 기준으로 전진 기어에서만 구동력을 높인다.
	float BaseEngineMaxTorque = 0.0f;

	// 탑승 처리 내부 구현: 좌석 배정 + Attach + 운전석이면 Possess 전환 (서버)
	void SeatCharacter(ACharacterBase* Character, int32 SeatIndex);

	// 하차 처리 내부 구현: 좌석 해제 + Detach + 원래 캐릭터 Possess 복귀 (서버)
	void UnseatCharacter(ACharacterBase* Character, int32 SeatIndex);

	// 탑승 시 캐릭터의 이동/충돌을 물리적으로 잠근다 (모든 머신에서 시각적 일관성)
	UFUNCTION(NetMulticast, Reliable)
	void MulticastAttachOccupant(ACharacterBase* Character, int32 SeatIndex);

	// 하차 시 캐릭터의 이동/충돌을 복구하고 안전 위치로 옮긴다
	UFUNCTION(NetMulticast, Reliable)
	void MulticastDetachOccupant(ACharacterBase* Character, FVector ExitLocation);

	// 운전자 입력 모드 전환. 각 머신에서 자기 로컬 컨트롤러일 때만 IMC를 전환한다
	// (입력 서브시스템은 로컬 플레이어에만 존재하므로 Multicast로 내려보낸다).
	// bEnterVehicle=true 면 차량 모드, false 면 캐릭터 모드로 복원.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastSwitchDriverInput(AController* DriverController, bool bEnterVehicle);

	// 운전석 탑승자가 원래 소유하던 컨트롤러 (하차 시 캐릭터 재빙의용). 서버 전용.
	UPROPERTY(Transient)
	TObjectPtr<AController> CachedDriverController;

	// 운전석 탑승자였던 캐릭터 (하차 시 Possess 복귀 대상). 서버 전용.
	UPROPERTY(Transient)
	TObjectPtr<ACharacterBase> CachedDriverCharacter;
};
