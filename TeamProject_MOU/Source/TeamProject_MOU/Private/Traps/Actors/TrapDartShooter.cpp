#include "Traps/Actors/TrapDartShooter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Traps/Projectiles/DartProjectile.h"
#include "Traps/Components/TrapTriggerComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"

ATrapDartShooter::ATrapDartShooter()
{
	ShooterMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShooterMesh"));
	ShooterMesh->SetupAttachment(RootScene);

	MuzzleDirection = CreateDefaultSubobject<UArrowComponent>(TEXT("MuzzleDirection"));
	MuzzleDirection->SetupAttachment(ShooterMesh);
	MuzzleDirection->bIsEditorOnly = false;

	ProjectileClass = ADartProjectile::StaticClass();

	if (BaseTriggerBox)
	{
		BaseTriggerBox->SetupAttachment(ShooterMesh);
		BaseTriggerBox->SetRelativeLocation(FVector(200.0f, 0.0f, -50.0f));
		BaseTriggerBox->InitBoxExtent(FVector(100.0f, 100.0f, 50.0f));
	}

	if (TriggerComponent)
	{
		TriggerComponent->TriggerType = ETrapTriggerType::RemoteSignal;
	}
}

void ATrapDartShooter::ExecuteTrapPayload()
{
	if (!HasAuthority())
	{
		return;
	}

	RemainingBurstCount = FMath::Max(1, BurstCount);
	SpawnSingleDart();

	if (RemainingBurstCount > 1 && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			BurstTimerHandle,
			this,
			&ATrapDartShooter::SpawnSingleDart,
			BurstInterval,
			true
		);
	}
}

void ATrapDartShooter::SpawnSingleDart()
{
	if (!HasAuthority() || !ProjectileClass || !GetWorld())
	{
		return;
	}

	FVector MuzzleLoc = MuzzleDirection ? MuzzleDirection->GetComponentLocation() : GetActorLocation();
	FRotator MuzzleRot = MuzzleDirection ? MuzzleDirection->GetComponentRotation() : GetActorRotation();
	FVector ForwardOffset = MuzzleRot.Vector() * 30.0f;
	FVector SpawnLocation = MuzzleLoc + ForwardOffset;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = GetInstigator();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ADartProjectile* SpawnedDart = GetWorld()->SpawnActor<ADartProjectile>(ProjectileClass, SpawnLocation, MuzzleRot, SpawnParams);
	if (SpawnedDart)
	{
		if (ShooterMesh)
		{
			ShooterMesh->IgnoreActorWhenMoving(SpawnedDart, true);
		}
	}

	RemainingBurstCount--;

	if (RemainingBurstCount <= 0 && GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(BurstTimerHandle);
	}
}
