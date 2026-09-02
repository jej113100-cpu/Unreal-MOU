#include "NPC/NPCSpawner.h"

#include "Base/CharacterBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

namespace NPCSpawnerTags
{
	static const FName SpawnPoint(TEXT("NPCSpawnPoint"));
}

ANPCSpawner::ANPCSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void ANPCSpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildSpawnPoints();
}

void ANPCSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && bSpawnOnBeginPlay)
	{
		SpawnNPCsToLimit();
	}
}

void ANPCSpawner::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANPCSpawner, CurrentSpawnCount);
}

ACharacterBase* ANPCSpawner::SpawnOneNPC()
{
	if (!HasAuthority() || !GetWorld() || NPCSpawnDefinitions.IsEmpty())
	{
		return nullptr;
	}

	RemoveInvalidSpawnedNPCs();
	if (CurrentSpawnCount >= MaxSpawnCount)
	{
		return nullptr;
	}

	TArray<USceneComponent*> AvailablePoints;
	for (USceneComponent* SpawnPoint : SpawnPoints)
	{
		if (IsValid(SpawnPoint) && !IsSpawnPointOccupied(SpawnPoint))
		{
			AvailablePoints.Add(SpawnPoint);
		}
	}

	TArray<const FNPCSpawnDefinition*> ValidDefinitions;
	for (const FNPCSpawnDefinition& SpawnDefinition : NPCSpawnDefinitions)
	{
		if (SpawnDefinition.NPCClass)
		{
			ValidDefinitions.Add(&SpawnDefinition);
		}
	}

	if (AvailablePoints.IsEmpty() || ValidDefinitions.IsEmpty())
	{
		return nullptr;
	}

	USceneComponent* SelectedPoint = AvailablePoints[FMath::RandRange(0, AvailablePoints.Num() - 1)];
	const FNPCSpawnDefinition& SelectedDefinition =
		*ValidDefinitions[FMath::RandRange(0, ValidDefinitions.Num() - 1)];
	FTransform SpawnTransform = SelectedPoint->GetComponentTransform();

	// Spawn Point가 공중에 있더라도 가장 가까운 NavMesh 바닥을 기준으로 생성합니다.
	if (UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation ProjectedLocation;
		if (NavigationSystem->ProjectPointToNavigation(
			SpawnTransform.GetLocation(), ProjectedLocation, FVector(200.0f, 200.0f, 1000.0f)))
		{
			SpawnTransform.SetLocation(ProjectedLocation.Location);
		}
	}

	ACharacterBase* SpawnedNPC = GetWorld()->SpawnActorDeferred<ACharacterBase>(
		SelectedDefinition.NPCClass,
		SpawnTransform,
		this,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!SpawnedNPC)
	{
		return nullptr;
	}

	// NPC의 BeginPlay/Blackboard 초기화보다 먼저 정찰 액터를 지정합니다.
	AssignPatrolActors(SpawnedNPC, SelectedDefinition);
	if (UCapsuleComponent* CapsuleComponent = SpawnedNPC->GetCapsuleComponent())
	{
		FVector SpawnLocation = SpawnTransform.GetLocation();
		SpawnLocation.Z += CapsuleComponent->GetScaledCapsuleHalfHeight();
		SpawnTransform.SetLocation(SpawnLocation);
	}
	UGameplayStatics::FinishSpawningActor(SpawnedNPC, SpawnTransform);

	// BP의 Auto Possess AI 설정과 무관하게 런타임 Spawn NPC에 AIController를 붙입니다.
	if (!SpawnedNPC->GetController())
	{
		SpawnedNPC->SpawnDefaultController();
	}

	SpawnedNPCs.Add(SpawnedNPC);
	SpawnedNPC->OnDestroyed.AddDynamic(this, &ANPCSpawner::HandleSpawnedNPCDestroyed);
	CurrentSpawnCount = SpawnedNPCs.Num();
	ForceNetUpdate();
	return SpawnedNPC;
}

void ANPCSpawner::SpawnNPCsToLimit()
{
	if (!HasAuthority())
	{
		return;
	}

	const int32 TargetCount = FMath::Min(MaxSpawnCount, SpawnPoints.Num());
	while (CurrentSpawnCount < TargetCount)
	{
		if (!SpawnOneNPC())
		{
			break;
		}
	}
}

void ANPCSpawner::HandleSpawnedNPCDestroyed(AActor* DestroyedActor)
{
	if (!HasAuthority())
	{
		return;
	}

	SpawnedNPCs.Remove(Cast<ACharacterBase>(DestroyedActor));
	RemoveInvalidSpawnedNPCs();
	ForceNetUpdate();
	ScheduleRespawn();
}

void ANPCSpawner::ScheduleRespawn()
{
	const int32 TargetCount = FMath::Min(MaxSpawnCount, SpawnPoints.Num());
	if (!HasAuthority() || !bAutoRespawn || CurrentSpawnCount >= TargetCount
		|| GetWorldTimerManager().IsTimerActive(RespawnTimerHandle))
	{
		return;
	}

	// 파괴 처리 중 즉시 Spawn하지 않도록 최소 한 프레임 이후에 실행합니다.
	const float SafeRespawnDelay = FMath::Max(RespawnDelay, KINDA_SMALL_NUMBER);
	GetWorldTimerManager().SetTimer(
		RespawnTimerHandle, this, &ANPCSpawner::RespawnMissingNPCs, SafeRespawnDelay, false);
}

void ANPCSpawner::RespawnMissingNPCs()
{
	if (!HasAuthority())
	{
		return;
	}

	RemoveInvalidSpawnedNPCs();
	SpawnNPCsToLimit();

	// 일시적으로 모든 Spawn Point가 막혀 있었다면 다음 주기에 다시 시도합니다.
	const int32 TargetCount = FMath::Min(MaxSpawnCount, SpawnPoints.Num());
	if (CurrentSpawnCount < TargetCount)
	{
		ScheduleRespawn();
	}
}

void ANPCSpawner::RebuildSpawnPoints()
{
	SpawnPointCount = FMath::Max(0, SpawnPointCount);

	TArray<USceneComponent*> ExistingPoints;
	GetComponents<USceneComponent>(ExistingPoints);
	ExistingPoints.RemoveAll([](const USceneComponent* Component)
	{
		return !Component || !Component->ComponentHasTag(NPCSpawnerTags::SpawnPoint);
	});

	ExistingPoints.Sort([](const USceneComponent& Left, const USceneComponent& Right)
	{
		return Left.GetName() < Right.GetName();
	});

	while (ExistingPoints.Num() > SpawnPointCount)
	{
		USceneComponent* PointToRemove = ExistingPoints.Pop();
		RemoveInstanceComponent(PointToRemove);
		PointToRemove->DestroyComponent();
	}

	while (ExistingPoints.Num() < SpawnPointCount)
	{
		const int32 NewIndex = ExistingPoints.Num();
		const FName ComponentName = MakeUniqueObjectName(
			this, USceneComponent::StaticClass(), *FString::Printf(TEXT("SpawnPoint_%02d"), NewIndex));
		USceneComponent* NewPoint = NewObject<USceneComponent>(this, ComponentName, RF_Transactional);
		NewPoint->ComponentTags.Add(NPCSpawnerTags::SpawnPoint);
		NewPoint->SetupAttachment(SceneRoot);
		NewPoint->SetRelativeLocation(FVector(NewIndex * 150.0f, 0.0f, 0.0f));
		AddInstanceComponent(NewPoint);
		NewPoint->RegisterComponent();
		ExistingPoints.Add(NewPoint);
	}

	SpawnPoints.Reset(ExistingPoints.Num());
	for (USceneComponent* ExistingPoint : ExistingPoints)
	{
		SpawnPoints.Add(ExistingPoint);
	}
}

void ANPCSpawner::RemoveInvalidSpawnedNPCs()
{
	SpawnedNPCs.RemoveAll([](const TObjectPtr<ACharacterBase>& NPC)
	{
		return !IsValid(NPC);
	});
	CurrentSpawnCount = SpawnedNPCs.Num();
}

bool ANPCSpawner::IsSpawnPointOccupied(const USceneComponent* SpawnPoint) const
{
	if (!SpawnPoint)
	{
		return true;
	}

	constexpr float OccupiedDistanceSquared = 100.0f * 100.0f;
	const FVector SpawnLocation = SpawnPoint->GetComponentLocation();
	for (const ACharacterBase* SpawnedNPC : SpawnedNPCs)
	{
		if (IsValid(SpawnedNPC)
			&& FVector::DistSquared2D(SpawnedNPC->GetActorLocation(), SpawnLocation) <= OccupiedDistanceSquared)
		{
			return true;
		}
	}

	return false;
}

void ANPCSpawner::AssignPatrolActors(
	ACharacterBase* SpawnedNPC, const FNPCSpawnDefinition& SpawnDefinition) const
{
	if (!SpawnedNPC)
	{
		return;
	}

	// 현재 BP_Base_NPC에 정의된 정찰 변수에 BeginPlay 전에 값을 주입합니다.
	if (FObjectPropertyBase* SplineProperty = FindFProperty<FObjectPropertyBase>(
		SpawnedNPC->GetClass(), TEXT("PatrolSplineActor")))
	{
		SplineProperty->SetObjectPropertyValue_InContainer(
			SpawnedNPC, SpawnDefinition.PatrolSplineActor.Get());
	}

	if (FObjectPropertyBase* BoundProperty = FindFProperty<FObjectPropertyBase>(
		SpawnedNPC->GetClass(), TEXT("PatrolBoundActor")))
	{
		BoundProperty->SetObjectPropertyValue_InContainer(
			SpawnedNPC, SpawnDefinition.PatrolBoundActor.Get());
	}
}

