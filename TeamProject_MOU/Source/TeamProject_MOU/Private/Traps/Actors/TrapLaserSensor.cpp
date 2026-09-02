#include "Traps/Actors/TrapLaserSensor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ArrowComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

ATrapLaserSensor::ATrapLaserSensor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	SetRootComponent(RootScene);

	EmitterMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EmitterMesh"));
	EmitterMesh->SetupAttachment(RootScene);

	LaserDirectionArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("LaserDirectionArrow"));
	LaserDirectionArrow->SetupAttachment(EmitterMesh);
	LaserDirectionArrow->ArrowColor = FColor::Red;
	LaserDirectionArrow->ArrowSize = 1.5f;
	LaserDirectionArrow->bIsEditorOnly = false;

	LaserMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LaserMesh"));
	LaserMesh->SetupAttachment(LaserDirectionArrow);
	LaserMesh->SetCollisionProfileName(TEXT("NoCollision"));
	LaserMesh->SetCastShadow(false);
	LaserMesh->SetCanEverAffectNavigation(false);
}

void ATrapLaserSensor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATrapLaserSensor, bIsSensorActive);
}

void ATrapLaserSensor::BeginPlay()
{
	Super::BeginPlay();

	if (LaserMesh)
	{
		LaserMesh->SetVisibility(bIsSensorActive);
	}

	if (HasAuthority() && bIsSensorActive && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			ScanTimerHandle,
			this,
			&ATrapLaserSensor::PerformLaserScan,
			ScanInterval,
			true,
			ScanInterval
		);
	}
}

void ATrapLaserSensor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(ScanTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void ATrapLaserSensor::TriggerTrap_Implementation(AActor* InstigatorActor)
{
	if (HasAuthority())
	{
		bIsSensorActive = true;
		if (GetWorld())
		{
			GetWorld()->GetTimerManager().SetTimer(ScanTimerHandle, this, &ATrapLaserSensor::PerformLaserScan, ScanInterval, true);
		}
		if (LaserMesh)
		{
			LaserMesh->SetVisibility(true);
		}
		OnSensorStateChanged_BP(true);
	}
}

void ATrapLaserSensor::ResetTrap_Implementation()
{
	if (HasAuthority())
	{
		bIsSensorActive = true;
		bWasObstructed = false;
		LastDetectedActor = nullptr;
		if (LaserMesh)
		{
			LaserMesh->SetVisibility(true);
		}
		OnSensorStateChanged_BP(true);
	}
}

void ATrapLaserSensor::DisarmTrap_Implementation(AActor* Disarmer)
{
	if (HasAuthority())
	{
		bIsSensorActive = false;
		if (GetWorld())
		{
			GetWorld()->GetTimerManager().ClearTimer(ScanTimerHandle);
		}
		if (LaserMesh)
		{
			LaserMesh->SetVisibility(false);
		}
		OnSensorStateChanged_BP(false);
	}
}

void ATrapLaserSensor::OnRep_bIsSensorActive()
{
	if (LaserMesh)
	{
		LaserMesh->SetVisibility(bIsSensorActive);
	}

	OnSensorStateChanged_BP(bIsSensorActive);
}

void ATrapLaserSensor::PerformLaserScan()
{
	if (!HasAuthority() || !bIsSensorActive || !GetWorld())
	{
		return;
	}

	FVector StartLoc = LaserDirectionArrow ? LaserDirectionArrow->GetComponentLocation() : (EmitterMesh ? EmitterMesh->GetComponentLocation() : GetActorLocation());
	FVector ForwardVec = LaserDirectionArrow ? LaserDirectionArrow->GetForwardVector() : (EmitterMesh ? EmitterMesh->GetForwardVector() : GetActorForwardVector());
	FVector EndLoc = StartLoc + (ForwardVec * LaserMaxDistance);

	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, StartLoc, EndLoc, ECC_Visibility, QueryParams);

	if (bShowDebugLaser)
	{
		FColor LineColor = bHit ? FColor::Red : FColor::Green;
		DrawDebugLine(GetWorld(), StartLoc, bHit ? HitResult.ImpactPoint : EndLoc, LineColor, false, ScanInterval * 1.5f, 0, 1.5f);
	}

	if (bHit && HitResult.GetActor())
	{
		AActor* HitActor = HitResult.GetActor();

		// 새롭게 감지되었거나 액터가 변경되었을 때 브로드캐스트
		if (!bWasObstructed || LastDetectedActor != HitActor)
		{
			bWasObstructed = true;
			LastDetectedActor = HitActor;

			MulticastOnLaserBeamObstructed(HitResult.ImpactPoint, HitActor);

			// 연결된 모든 함정 액터들에게 발동 신호 브로드캐스트
			for (AActor* TargetTrap : LinkedTraps)
			{
				if (TargetTrap && TargetTrap->GetClass()->ImplementsInterface(UTrapTriggerableInterface::StaticClass()))
				{
					ITrapTriggerableInterface::Execute_TriggerTrap(TargetTrap, HitActor);
				}
			}
		}
	}
	else
	{
		bWasObstructed = false;
		LastDetectedActor = nullptr;
	}
}

void ATrapLaserSensor::MulticastOnLaserBeamObstructed_Implementation(FVector ImpactPoint, AActor* ObstructingActor)
{
	OnLaserObstructed_BP(ImpactPoint, ObstructingActor);
}
