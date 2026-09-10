#include "Item/VehicleBase.h"

#include "Base/CharacterBase.h"
#include "TeamProject_MOUPlayerController.h"
#include "ChaosVehicleMovementComponent.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "ChaosVehicleWheel.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputMappingContext.h"
#include "Net/UnrealNetwork.h"

// [VEHICLE-001] 생성자: 카메라/상호작용 볼륨 구성 및 기본값
AVehicleBase::AVehicleBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// 운전 카메라 붐: 차량 뒤 위쪽에서 차를 내려다보는 3인칭 시점.
	// 차량 방향을 그대로 따라가고(정면 고정), 마우스로는 못 돌린다.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetMesh());
	// 붐 시작점을 차체 위로 올린다(메시 원점이 바닥이라 안 올리면 카메라가 차 안에 묻힌다).
	CameraBoom->SetRelativeLocation(FVector(0.0f, 0.0f, 200.0f));
	// 붐을 아래로 15도 기울여 차를 위에서 내려다본다.
	CameraBoom->SetRelativeRotation(FRotator(-15.0f, 0.0f, 0.0f));
	CameraBoom->TargetArmLength = 800.0f;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bInheritYaw = true;
	// 붐이 벽/지면 콜리전에 말려 들어가 카메라가 튀는 것을 막는다.
	CameraBoom->bDoCollisionTest = false;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// F키 상호작용 감지 범위. 실제 크기는 차량마다 에디터에서 조정.
	InteractionVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionVolume"));
	InteractionVolume->SetupAttachment(GetMesh());
	InteractionVolume->SetBoxExtent(FVector(250.0f, 150.0f, 100.0f));
	InteractionVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionVolume->SetCollisionResponseToAllChannels(ECR_Overlap);

	// 차량은 네트워크에서 서버 권위로 위치가 복제되어야 한다.
	bReplicates = true;
	SetReplicateMovement(true);
}

void AVehicleBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 좌석 점유 상태를 모든 클라이언트에 복제한다.
	DOREPLIFETIME(AVehicleBase, Seats);
	DOREPLIFETIME(AVehicleBase, bDrifting);
}

void AVehicleBase::BeginPlay()
{
	Super::BeginPlay();

	if (const UChaosWheeledVehicleMovementComponent* Wheeled = Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent()))
	{
		BaseEngineMaxTorque = Wheeled->EngineSetup.MaxTorque;
		for (const FChaosWheelSetup& Setup : Wheeled->WheelSetups)
		{
			const UChaosVehicleWheel* Wheel = Setup.WheelClass ? Setup.WheelClass->GetDefaultObject<UChaosVehicleWheel>() : nullptr;
			DefaultWheelGrip.Add(Wheel ? Wheel->FrictionForceMultiplier : 1.0f);
		}
	}

	// [임시 진단] 무브먼트/메시 물리 연결 상태 확인.
	USkeletalMeshComponent* MeshComp = GetMesh();
	UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent();
	UE_LOG(LogTemp, Warning, TEXT("[VEHICLE] BeginPlay: Mesh=%s, SimPhysics=%d, Movement=%s, UpdatedComp=%s"),
		MeshComp ? *MeshComp->GetName() : TEXT("NULL"),
		(MeshComp && MeshComp->IsSimulatingPhysics()) ? 1 : 0,
		Movement ? *Movement->GetName() : TEXT("NULL"),
		(Movement && Movement->UpdatedComponent) ? *Movement->UpdatedComponent->GetName() : TEXT("NULL"));

	// [임시 진단] 비동기 물리 설정이 실제로 켜졌는지 런타임에 직접 확인.
	// bTickPhysicsAsync 가 0 이면 ini 가 반영 안 된 것(에디터 재시작 필요 or 다른 설정이 덮어씀).
	if (const UPhysicsSettings* PS = UPhysicsSettings::Get())
	{
		UE_LOG(LogTemp, Error, TEXT("[VEHICLE] bTickPhysicsAsync=%d, AsyncFixedTimeStepSize=%.5f"),
			PS->bTickPhysicsAsync ? 1 : 0,
			PS->AsyncFixedTimeStepSize);
	}
}

// [VEHICLE-002] 좌석 복제 콜백: 클라이언트에서 좌석 변화 연출 훅 호출
void AVehicleBase::OnRep_Seats()
{
	OnSeatsChanged();
}

// [VEHICLE-010] F키 상호작용 가능 여부: 빈 좌석이 하나라도 있으면 탑승 가능
bool AVehicleBase::CanInteract_Implementation(AActor* Interactor) const
{
	const ACharacterBase* Character = Cast<ACharacterBase>(Interactor);
	if (!Character)
	{
		return false;
	}

	// 이미 이 차량에 타고 있으면(하차는 별도 입력) 상호작용 대상에서 제외
	if (GetSeatIndexOf(Character) != INDEX_NONE)
	{
		return false;
	}

	return FindNearestFreeSeat(Interactor) != INDEX_NONE;
}

// [VEHICLE-011] F키 상호작용 실행: 서버에 탑승 요청 (드론과 동일하게 서버 권위 처리)
void AVehicleBase::Interact_Implementation(AActor* Interactor)
{
	ACharacterBase* Character = Cast<ACharacterBase>(Interactor);
	if (!Character)
	{
		return;
	}

	// 상호작용은 로컬 클라에서도 호출되므로, 상태 변경은 서버 RPC로 위임한다.
	ServerRequestEnter(Character);
}

FText AVehicleBase::GetInteractPrompt_Implementation() const
{
	return FText::FromString(TEXT("탑승"));
}

// [VEHICLE-020] 가장 가까운 빈 좌석 찾기 (운전석 우선 없음 - 순수 거리 기준)
int32 AVehicleBase::FindNearestFreeSeat(const AActor* ForActor) const
{
	if (!ForActor)
	{
		return INDEX_NONE;
	}

	const FVector FromLoc = ForActor->GetActorLocation();
	int32 BestIndex = INDEX_NONE;
	float BestDistSq = TNumericLimits<float>::Max();

	const USkeletalMeshComponent* MeshComp = GetMesh();

	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		if (Seats[i].Occupant != nullptr)
		{
			continue; // 이미 점유된 좌석
		}

		// 좌석 소켓 위치 기준 거리. 소켓이 없으면 차량 위치로 대체.
		FVector SeatLoc = GetActorLocation();
		if (MeshComp && Seats[i].SeatSocketName != NAME_None && MeshComp->DoesSocketExist(Seats[i].SeatSocketName))
		{
			SeatLoc = MeshComp->GetSocketLocation(Seats[i].SeatSocketName);
		}

		const float DistSq = FVector::DistSquared(FromLoc, SeatLoc);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestIndex = i;
		}
	}

	return BestIndex;
}

// [VEHICLE-021] 특정 캐릭터가 앉은 좌석 인덱스 조회
int32 AVehicleBase::GetSeatIndexOf(const ACharacterBase* Character) const
{
	if (!Character)
	{
		return INDEX_NONE;
	}

	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		if (Seats[i].Occupant == Character)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

// [VEHICLE-022] 현재 운전자(운전석 탑승자) 반환
ACharacterBase* AVehicleBase::GetDriver() const
{
	for (const FVehicleSeat& Seat : Seats)
	{
		if (Seat.bIsDriverSeat && Seat.Occupant != nullptr)
		{
			return Seat.Occupant;
		}
	}
	return nullptr;
}

// [VEHICLE-030] 서버: 탑승 요청 처리
void AVehicleBase::ServerRequestEnter_Implementation(ACharacterBase* Requestor)
{
	if (!Requestor)
	{
		return;
	}
	EnterVehicle(Requestor);
}

// [VEHICLE-031] 서버: 하차 요청 처리
void AVehicleBase::ServerRequestExit_Implementation(ACharacterBase* Requestor)
{
	if (!Requestor)
	{
		return;
	}
	ExitVehicle(Requestor);
}

// [VEHICLE-032] 탑승 실제 처리 (서버 권위)
bool AVehicleBase::EnterVehicle(ACharacterBase* NewOccupant)
{
	if (!HasAuthority() || !NewOccupant)
	{
		return false;
	}

	// 이미 탑승 중이면 무시
	if (GetSeatIndexOf(NewOccupant) != INDEX_NONE)
	{
		return false;
	}

	const int32 SeatIndex = FindNearestFreeSeat(NewOccupant);
	if (SeatIndex == INDEX_NONE)
	{
		return false; // 빈 좌석 없음
	}

	SeatCharacter(NewOccupant, SeatIndex);
	return true;
}

// [VEHICLE-033] 하차 실제 처리 (서버 권위)
void AVehicleBase::ExitVehicle(ACharacterBase* Occupant)
{
	if (!HasAuthority() || !Occupant)
	{
		return;
	}

	const int32 SeatIndex = GetSeatIndexOf(Occupant);
	if (SeatIndex == INDEX_NONE)
	{
		return;
	}

	UnseatCharacter(Occupant, SeatIndex);
}

// [VEHICLE-040] 좌석 배정 + Attach + (운전석이면) 차량 Possess (서버 전용)
void AVehicleBase::SeatCharacter(ACharacterBase* Character, int32 SeatIndex)
{
	if (!Seats.IsValidIndex(SeatIndex) || !Character)
	{
		return;
	}

	FVehicleSeat& Seat = Seats[SeatIndex];
	Seat.Occupant = Character;

	// 모든 머신에서 캐릭터를 좌석 소켓에 붙이고 이동/충돌을 잠근다.
	MulticastAttachOccupant(Character, SeatIndex);

	// 운전석이면 컨트롤러를 차량으로 옮겨(Possess) 운전 입력을 넘겨받는다.
	if (Seat.bIsDriverSeat)
	{
		if (AController* DriverController = Character->GetController())
		{
			// 하차 시 원래 캐릭터로 되돌리기 위해 짝을 기억한다.
			CachedDriverController = DriverController;
			CachedDriverCharacter = Character;

			DriverController->Possess(this);

			// 운전자 로컬 컨트롤러의 입력을 차량 모드로 전환한다.
			// (캐릭터 IMC가 남아 W/A/S/D를 먼저 소비하면 차량 액션에 도달하지 못한다.)
			MulticastSwitchDriverInput(DriverController, true);
		}
	}

	// 서버에서도 좌석 변화 연출 훅 호출 (OnRep 은 클라 전용이므로)
	OnSeatsChanged();
}

// [VEHICLE-041] 좌석 해제 + Detach + (운전자면) 원래 캐릭터 Possess 복귀 (서버 전용)
void AVehicleBase::UnseatCharacter(ACharacterBase* Character, int32 SeatIndex)
{
	if (!Seats.IsValidIndex(SeatIndex) || !Character)
	{
		return;
	}

	FVehicleSeat& Seat = Seats[SeatIndex];
	const bool bWasDriver = Seat.bIsDriverSeat;
	if (bWasDriver)
	{
		bDrifting = false;
	}

	// 운전자였다면 조종 입력을 0으로 정리한 뒤 컨트롤러를 캐릭터로 되돌린다.
	if (bWasDriver && CachedDriverController && CachedDriverCharacter == Character)
	{
		if (UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent())
		{
			Movement->SetThrottleInput(0.0f);
			Movement->SetBrakeInput(0.0f);
			Movement->SetSteeringInput(0.0f);
			Movement->SetHandbrakeInput(true);
		}

		AController* DriverController = CachedDriverController;
		DriverController->Possess(Character);

		// 운전자 입력을 캐릭터 모드로 복원한다 (각 머신 로컬에서 처리).
		MulticastSwitchDriverInput(DriverController, false);

		CachedDriverController = nullptr;
		CachedDriverCharacter = nullptr;
	}

	Seat.Occupant = nullptr;

	// 하차 위치: 차량 오른쪽 옆(운전석 반대편이 아니라 일단 차량 옆 안전 위치).
	const FVector ExitLocation = GetActorLocation()
		+ GetActorRightVector() * 200.0f
		+ FVector(0.0f, 0.0f, 50.0f);

	MulticastDetachOccupant(Character, ExitLocation);

	OnSeatsChanged();
}

// [VEHICLE-050] 모든 머신: 캐릭터를 좌석 소켓에 붙이고 이동/충돌 잠금
void AVehicleBase::MulticastAttachOccupant_Implementation(ACharacterBase* Character, int32 SeatIndex)
{
	if (!Character || !Seats.IsValidIndex(SeatIndex))
	{
		return;
	}

	USkeletalMeshComponent* MeshComp = GetMesh();
	const FName SocketName = Seats[SeatIndex].SeatSocketName;

	// [임시 진단] 캐릭터 Attach 가 차량 물리를 방해하는지 배제하기 위해,
	// 지금은 Attach 하지 않고 숨기기만 한다. 이래도 차가 안 굴러가면 캐릭터는 원인이 아니다.
	if (UCharacterMovementComponent* CharMove = Character->GetCharacterMovement())
	{
		CharMove->DisableMovement();
	}
	if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	Character->SetActorHiddenInGame(true);
}

// [VEHICLE-051] 모든 머신: 캐릭터 Detach + 이동/충돌 복구 + 안전 위치 이동
void AVehicleBase::MulticastDetachOccupant_Implementation(ACharacterBase* Character, FVector ExitLocation)
{
	if (!Character)
	{
		return;
	}

	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	// [임시 진단] 탑승 시 숨겼던 캐릭터를 다시 보이게 한다.
	Character->SetActorHiddenInGame(false);

	if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	if (UCharacterMovementComponent* CharMove = Character->GetCharacterMovement())
	{
		CharMove->SetMovementMode(MOVE_Walking);
	}

	// 서버 권위 머신에서만 실제 위치를 옮긴다(복제로 클라 동기화).
	if (Character->HasAuthority())
	{
		Character->SetActorLocation(ExitLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

// [VEHICLE-060] 운전 입력 바인딩 (컨트롤러가 이 Pawn 을 Possess 한 동안만 유효)
// IMC 추가/제거는 컨트롤러(SwitchToVehicleInput)가 담당하고, 여기서는 액션 바인딩만 한다.
void AVehicleBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (ThrottleAction)
		{
			EIC->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &AVehicleBase::OnThrottleInput);
			EIC->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &AVehicleBase::OnThrottleInput);
		}
		if (SteerAction)
		{
			EIC->BindAction(SteerAction, ETriggerEvent::Triggered, this, &AVehicleBase::OnSteerInput);
			EIC->BindAction(SteerAction, ETriggerEvent::Completed, this, &AVehicleBase::OnSteerInput);
		}
		if (ExitAction)
		{
			// 하차: 서버에 요청. Possess 로 이 Pawn 이 조종 중이므로 GetDriver 로 운전자를 찾는다.
			EIC->BindAction(ExitAction, ETriggerEvent::Started, this, &AVehicleBase::OnExitInput);
		}
	}
}

// [VEHICLE-065] 운전자 입력 모드 전환. 각 머신에서 자기 로컬 컨트롤러일 때만 IMC를 바꾼다.
void AVehicleBase::MulticastSwitchDriverInput_Implementation(AController* DriverController, bool bEnterVehicle)
{
	// 입력 서브시스템은 로컬 플레이어에만 있으므로, 이 머신에서 로컬로 조종하는
	// 컨트롤러가 아니면 무시한다.
	ATeamProject_MOUPlayerController* PC = Cast<ATeamProject_MOUPlayerController>(DriverController);
	UE_LOG(LogTemp, Warning, TEXT("[VEHICLE] MulticastSwitchDriverInput: bEnter=%d, DriverController=%s, castOK=%d, IsLocal=%d"),
		bEnterVehicle ? 1 : 0,
		DriverController ? *DriverController->GetName() : TEXT("NULL"),
		PC ? 1 : 0,
		(PC && PC->IsLocalController()) ? 1 : 0);

	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	if (bEnterVehicle)
	{
		UE_LOG(LogTemp, Warning, TEXT("[VEHICLE] -> SwitchToVehicleInput 호출, DrivingMappingContext=%s"),
			DrivingMappingContext ? *DrivingMappingContext->GetName() : TEXT("NULL"));
		PC->SwitchToVehicleInput(DrivingMappingContext);
	}
	else
	{
		PC->RestoreCharacterInput();
	}
}

// [VEHICLE-061] W/S : 전진/후진. Axis1D 입력값을 꺼내 스로틀/브레이크로 변환.
void AVehicleBase::OnThrottleInput(const FInputActionValue& Value)
{
	UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent();
	if (!Movement)
	{
		UE_LOG(LogTemp, Error, TEXT("[VEHICLE] OnThrottleInput: Movement NULL"));
		return;
	}

	// Chaos Vehicle 은 입력이 없으면 Sleep/Park 상태로 시뮬레이션을 멈춘다.
	// 스폰 직후 Sleep 로 시작하면 스로틀을 줘도 안 깨어나 EngineRPM 이 0에서 안 오른다.
	// 그래서 입력이 들어올 때마다 명시적으로 깨우고 주차를 해제한다.
	Movement->SetSleeping(false);
	Movement->SetParked(false);
	Movement->SetHandbrakeInput(bLocalDriftRequested);

	const float Axis = Value.Get<float>();

	// Axis > 0 : 전진(스로틀), Axis < 0 : 후진(브레이크/리버스)
	if (Axis >= 0.0f)
	{
		Movement->SetThrottleInput(Axis);
		Movement->SetBrakeInput(0.0f);
	}
	else
	{
		Movement->SetThrottleInput(0.0f);
		Movement->SetBrakeInput(-Axis);
	}

	// [임시 진단] 스로틀 설정 후 실제 물리 상태 확인.
	// - HasAuthority / IsLocallyControlled: 이 머신이 물리 시뮬레이션 권한이 있는지
	// - EngineRPM: 엔진이 실제로 도는지 (안 오르면 스로틀이 엔진에 전달 안 됨)
	// - ForwardSpeed: 차가 실제로 나아가는지
	float EngineRPM = -1.0f;
	if (UChaosWheeledVehicleMovementComponent* Wheeled = Cast<UChaosWheeledVehicleMovementComponent>(Movement))
	{
		EngineRPM = Wheeled->GetEngineRotationSpeed();
	}
	// 차량의 실제 Forward/Up 방향. Chaos 는 ActorForward(X축)를 앞으로 보고 미는데,
	// 차가 물리적으로 누워있으면 UpZ 가 1 이 아니고, 그러면 접지/구동이 깨진다.
	const FVector Fwd = GetActorForwardVector();
	const FVector Up = GetActorUpVector();
	UE_LOG(LogTemp, Warning, TEXT("[VEHICLE] Throttle=%.2f | EngineRPM=%.1f | ForwardSpeed=%.1f | Fwd=(%.2f,%.2f,%.2f) Up=(%.2f,%.2f,%.2f)"),
		Axis,
		EngineRPM,
		Movement->GetForwardSpeed(),
		Fwd.X, Fwd.Y, Fwd.Z,
		Up.X, Up.Y, Up.Z);
}

// [VEHICLE-062] A/D : 조향. 누르는 동안 Triggered 로 값이 계속 들어온다.
void AVehicleBase::OnSteerInput(const FInputActionValue& Value)
{
	if (UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent())
	{
		Movement->SetSteeringInput(Value.Get<float>());
	}
}

// [VEHICLE-063] 하차 입력: 운전석 탑승자를 찾아 서버에 하차 요청
void AVehicleBase::OnExitInput()
{
	// 이 Pawn 을 Possess 한 운전자가 하차 대상이다.
	if (ACharacterBase* Driver = GetDriver())
	{
		ServerRequestExit(Driver);
	}
}

void AVehicleBase::ServerSetDrifting_Implementation(bool bEnabled)
{
	bDrifting = bEnabled && GetDriver() != nullptr;
}

void AVehicleBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (UChaosWheeledVehicleMovementComponent* Wheeled = Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent()))
	{
		if (IsLocallyControlled())
		{
			const APlayerController* PC = Cast<APlayerController>(GetController());
			const bool bRequested = PC && PC->IsInputKeyDown(EKeys::SpaceBar)
				&& GetDriver() && Wheeled->GetForwardSpeed() > 300.0f;
			if (bRequested != bLocalDriftRequested)
			{
				bLocalDriftRequested = bRequested;
				ServerSetDrifting(bRequested);
			}
			Wheeled->SetHandbrakeInput(bRequested);
		}
		else
		{
			bLocalDriftRequested = false;
		}

		const bool bApplyDrift = IsLocallyControlled() ? bLocalDriftRequested : bDrifting;
		for (int32 Index = 2; Index < FMath::Min(4, Wheeled->Wheels.Num()); ++Index)
		{
			if (DefaultWheelGrip.IsValidIndex(Index))
			{
				Wheeled->SetWheelFrictionMultiplier(Index, DefaultWheelGrip[Index] * (bApplyDrift ? DriftRearGripScale : 1.0f));
			}
		}
		const float TorqueMultiplier = Wheeled->GetCurrentGear() > 0 ? 1.8f : 1.0f;
		Wheeled->SetMaxEngineTorque(BaseEngineMaxTorque * TorqueMultiplier);
	}
}
