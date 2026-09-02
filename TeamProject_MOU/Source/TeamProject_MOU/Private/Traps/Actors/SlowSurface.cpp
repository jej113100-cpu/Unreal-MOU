#include "Traps/Actors/SlowSurface.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "NiagaraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

ASlowSurface::ASlowSurface()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	SetRootComponent(RootScene);

	SurfaceMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SurfaceMesh"));
	SurfaceMesh->SetupAttachment(RootScene);

	SlowVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("SlowVolume"));
	SlowVolume->SetupAttachment(SurfaceMesh);
	SlowVolume->InitBoxExtent(FVector(200.0f, 200.0f, 30.0f));
	SlowVolume->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	SlowVolume->SetGenerateOverlapEvents(true);

	SplashFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("SplashFX"));
	SplashFX->SetupAttachment(SurfaceMesh);
	SplashFX->bAutoActivate = true;
}

void ASlowSurface::BeginPlay()
{
	Super::BeginPlay();

	if (SlowVolume)
	{
		SlowVolume->OnComponentBeginOverlap.AddDynamic(this, &ASlowSurface::HandleSurfaceBeginOverlap);
		SlowVolume->OnComponentEndOverlap.AddDynamic(this, &ASlowSurface::HandleSurfaceEndOverlap);
	}
}

void ASlowSurface::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 액터 파괴 시 감속 중이던 모든 캐릭터의 원래 무브먼트 복원
	for (auto& Pair : OriginalSpeedMap)
	{
		if (Pair.Key)
		{
			RestoreCharacterMovement(Pair.Key);
		}
	}
	OriginalSpeedMap.Empty();
	OriginalJumpMap.Empty();

	Super::EndPlay(EndPlayReason);
}

void ASlowSurface::HandleSurfaceBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor)
	{
		return;
	}

	if (ACharacter* Character = Cast<ACharacter>(OtherActor))
	{
		ApplySlowToCharacter(Character);
	}
}

void ASlowSurface::HandleSurfaceEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!OtherActor)
	{
		return;
	}

	if (ACharacter* Character = Cast<ACharacter>(OtherActor))
	{
		RestoreCharacterMovement(Character);
	}
}

void ASlowSurface::ApplySlowToCharacter(ACharacter* Character)
{
	if (!Character)
	{
		return;
	}

	UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();
	if (!MoveComp)
	{
		return;
	}

	// 1. 원래 속도 및 점프력 백업
	if (!OriginalSpeedMap.Contains(Character))
	{
		OriginalSpeedMap.Add(Character, MoveComp->MaxWalkSpeed);
	}
	if (!OriginalJumpMap.Contains(Character))
	{
		OriginalJumpMap.Add(Character, MoveComp->JumpZVelocity);
	}

	float BaseSpeed = OriginalSpeedMap[Character];
	float BaseJump = OriginalJumpMap[Character];

	// 2. 최종 감속 배율 적용
	MoveComp->MaxWalkSpeed = BaseSpeed * SlowSpeedMultiplier;

	// 3. 점프 제어
	if (bDisableJump)
	{
		MoveComp->JumpZVelocity = 0.0f;
	}
	else
	{
		MoveComp->JumpZVelocity = BaseJump * JumpVelocityMultiplier;
	}
}

void ASlowSurface::RestoreCharacterMovement(ACharacter* Character)
{
	if (!Character)
	{
		return;
	}

	UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();
	if (MoveComp)
	{
		if (float* OrigSpeed = OriginalSpeedMap.Find(Character))
		{
			MoveComp->MaxWalkSpeed = *OrigSpeed;
			OriginalSpeedMap.Remove(Character);
		}

		if (float* OrigJump = OriginalJumpMap.Find(Character))
		{
			MoveComp->JumpZVelocity = *OrigJump;
			OriginalJumpMap.Remove(Character);
		}
	}
}
