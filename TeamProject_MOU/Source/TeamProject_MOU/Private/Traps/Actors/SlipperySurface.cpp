#include "Traps/Actors/SlipperySurface.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "NiagaraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Traps/Interfaces/TrapTargetInterface.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

ASlipperySurface::ASlipperySurface()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	SetRootComponent(RootScene);

	SurfaceMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SurfaceMesh"));
	SurfaceMesh->SetupAttachment(RootScene);

	SlipperyVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("SlipperyVolume"));
	SlipperyVolume->SetupAttachment(SurfaceMesh);
	SlipperyVolume->InitBoxExtent(FVector(200.0f, 200.0f, 30.0f));
	SlipperyVolume->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	SlipperyVolume->SetGenerateOverlapEvents(true);

	FireFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FireFX"));
	FireFX->SetupAttachment(SurfaceMesh);
	FireFX->bAutoActivate = false;
}

void ASlipperySurface::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASlipperySurface, bIsIgnited);
}

void ASlipperySurface::BeginPlay()
{
	Super::BeginPlay();

	if (SlipperyVolume)
	{
		SlipperyVolume->OnComponentBeginOverlap.AddDynamic(this, &ASlipperySurface::HandleSurfaceBeginOverlap);
		SlipperyVolume->OnComponentEndOverlap.AddDynamic(this, &ASlipperySurface::HandleSurfaceEndOverlap);
	}
}

void ASlipperySurface::TriggerTrap_Implementation(AActor* InstigatorActor)
{
	if (bIsFlammable && !bIsIgnited)
	{
		IgniteSurface();
	}
}

void ASlipperySurface::ResetTrap_Implementation()
{
	if (HasAuthority())
	{
		bIsIgnited = false;
		if (GetWorld())
		{
			GetWorld()->GetTimerManager().ClearTimer(FireTimerHandle);
		}
		if (FireFX)
		{
			FireFX->Deactivate();
		}
	}
}

void ASlipperySurface::DisarmTrap_Implementation(AActor* Disarmer)
{
	ResetTrap_Implementation();
}

void ASlipperySurface::IgniteSurface()
{
	if (!HasAuthority() || !bIsFlammable || bIsIgnited)
	{
		return;
	}

	bIsIgnited = true;
	OnRep_bIsIgnited();

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			FireTimerHandle,
			this,
			&ASlipperySurface::ApplyFireDamageTick,
			1.0f,
			true
		);
	}
}

void ASlipperySurface::OnRep_bIsIgnited()
{
	if (FireFX)
	{
		if (bIsIgnited)
		{
			FireFX->Activate(true);
		}
		else
		{
			FireFX->Deactivate();
		}
	}
}

void ASlipperySurface::HandleSurfaceBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor)
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(OtherActor);
	if (Character && Character->GetCharacterMovement())
	{
		UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();
		OriginalFrictionMap.Add(Character, MoveComp->GroundFriction);
		OriginalBrakingMap.Add(Character, MoveComp->BrakingDecelerationWalking);

		MoveComp->GroundFriction = SlipperyGroundFriction;
		MoveComp->BrakingDecelerationWalking = SlipperyBrakingDeceleration;
	}
}

void ASlipperySurface::HandleSurfaceEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!OtherActor)
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(OtherActor);
	if (Character && Character->GetCharacterMovement())
	{
		UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();

		if (float* OrigFriction = OriginalFrictionMap.Find(Character))
		{
			MoveComp->GroundFriction = *OrigFriction;
			OriginalFrictionMap.Remove(Character);
		}
		if (float* OrigBraking = OriginalBrakingMap.Find(Character))
		{
			MoveComp->BrakingDecelerationWalking = *OrigBraking;
			OriginalBrakingMap.Remove(Character);
		}
	}
}

void ASlipperySurface::ApplyFireDamageTick()
{
	if (!HasAuthority() || !bIsIgnited || !SlipperyVolume)
	{
		return;
	}

	TArray<AActor*> OverlappingActors;
	SlipperyVolume->GetOverlappingActors(OverlappingActors);

	for (AActor* Target : OverlappingActors)
	{
		if (Target && Target->GetClass()->ImplementsInterface(UTrapTargetInterface::StaticClass()))
		{
			ITrapTargetInterface::Execute_ApplyTrapDamage(Target, FireDamagePerSecond, this);
		}
	}
}
