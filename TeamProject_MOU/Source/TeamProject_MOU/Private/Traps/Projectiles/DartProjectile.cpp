#include "Traps/Projectiles/DartProjectile.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Traps/Interfaces/TrapTargetInterface.h"
#include "GameplayEffect.h"
#include "Kismet/GameplayStatics.h"

ADartProjectile::ADartProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
	CollisionComponent->InitSphereRadius(12.0f);
	CollisionComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	CollisionComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	CollisionComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	CollisionComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	CollisionComponent->SetGenerateOverlapEvents(true);
	CollisionComponent->bReturnMaterialOnMove = true;
	SetRootComponent(CollisionComponent);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(CollisionComponent);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionComponent;
	ProjectileMovement->InitialSpeed = 2500.0f;
	ProjectileMovement->MaxSpeed = 2500.0f;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;
	ProjectileMovement->ProjectileGravityScale = 0.0f;
	ProjectileMovement->bAutoActivate = true;

	InitialLifeSpan = 5.0f;
}

void ADartProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner())
	{
		CollisionComponent->IgnoreActorWhenMoving(GetOwner(), true);
	}
	if (GetInstigator())
	{
		CollisionComponent->IgnoreActorWhenMoving(GetInstigator(), true);
	}

	if (ProjectileMovement)
	{
		ProjectileMovement->Velocity = GetActorForwardVector() * ProjectileMovement->InitialSpeed;
	}

	if (CollisionComponent)
	{
		CollisionComponent->OnComponentHit.AddDynamic(this, &ADartProjectile::HandleHit);
	}
}

void ADartProjectile::HandleHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!OtherActor || OtherActor == this || OtherActor == GetOwner())
	{
		return;
	}

	if (HasAuthority())
	{
		if (OtherActor->GetClass()->ImplementsInterface(UTrapTargetInterface::StaticClass()))
		{
			ITrapTargetInterface::Execute_ApplyTrapDamage(OtherActor, Damage, GetOwner());

			if (HitStatusEffectClass)
			{
				ITrapTargetInterface::Execute_ApplyTrapStatusEffect(OtherActor, HitStatusEffectClass, GetOwner());
			}
		}

		if (HitSound && GetWorld())
		{
			UGameplayStatics::PlaySoundAtLocation(this, HitSound, Hit.ImpactPoint);
		}

		Destroy();
	}
}
