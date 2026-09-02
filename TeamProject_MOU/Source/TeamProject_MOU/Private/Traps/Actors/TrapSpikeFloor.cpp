#include "Traps/Actors/TrapSpikeFloor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Traps/Components/TrapTriggerComponent.h"
#include "Traps/Components/TrapPayloadComponent.h"
#include "Traps/Data/TrapDataAsset.h"
#include "Traps/Interfaces/TrapTargetInterface.h"

ATrapSpikeFloor::ATrapSpikeFloor()
{
	PrimaryActorTick.bCanEverTick = true;

	BaseFrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseFrameMesh"));
	BaseFrameMesh->SetupAttachment(RootScene);
	BaseFrameMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));

	SpikeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpikeMesh"));
	SpikeMesh->SetupAttachment(BaseFrameMesh);
	SpikeMesh->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	SpikeMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	SpikeMesh->CanCharacterStepUpOn = ECB_No;
	SpikeMesh->SetCanEverAffectNavigation(false);

	if (BaseTriggerBox)
	{
		BaseTriggerBox->SetupAttachment(BaseFrameMesh);
		BaseTriggerBox->InitBoxExtent(FVector(100.0f, 100.0f, 30.0f));
	}
}

void ATrapSpikeFloor::BeginPlay()
{
	Super::BeginPlay();

	if (SpikeMesh)
	{
		SpikeMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		SpikeMesh->CanCharacterStepUpOn = ECB_No;

		RetractedRelativeLocation = SpikeMesh->GetRelativeLocation();
		ExtendedRelativeLocation = RetractedRelativeLocation + FVector(0.0f, 0.0f, SpikeExtendHeight);
		TargetSpikeLocation = RetractedRelativeLocation;
	}

	if (TriggerComponent)
	{
		switch (OperationMode)
		{
		case ESpikeOperationMode::AlwaysActive:
			TriggerComponent->SetTriggerEnabled(false);
			TargetSpikeLocation = ExtendedRelativeLocation;
			if (SpikeMesh)
			{
				SpikeMesh->SetRelativeLocation(ExtendedRelativeLocation);
			}
			break;

		case ESpikeOperationMode::PeriodicPopup:
			TriggerComponent->TriggerType = ETrapTriggerType::PeriodicTimer;
			break;

		case ESpikeOperationMode::PressureTriggered:
			TriggerComponent->TriggerType = ETrapTriggerType::PressurePlate;
			break;

		case ESpikeOperationMode::RemoteLinked:
			TriggerComponent->TriggerType = ETrapTriggerType::RemoteSignal;
			break;
		}

		UpdateTriggerBoxCollision();
	}
}

void ATrapSpikeFloor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateSpikeMovement(DeltaTime);
}

void ATrapSpikeFloor::OnStateEntered(ETrapState NewState)
{
	Super::OnStateEntered(NewState);

	if (OperationMode == ESpikeOperationMode::AlwaysActive)
	{
		return;
	}

	if (NewState == ETrapState::Active)
	{
		TargetSpikeLocation = ExtendedRelativeLocation;
	}
	else if (NewState == ETrapState::Cooldown || NewState == ETrapState::Idle)
	{
		TargetSpikeLocation = RetractedRelativeLocation;
	}
}

void ATrapSpikeFloor::UpdateSpikeMovement(float DeltaTime)
{
	if (!SpikeMesh || OperationMode == ESpikeOperationMode::AlwaysActive)
	{
		return;
	}

	FVector CurrentLoc = SpikeMesh->GetRelativeLocation();
	if (CurrentLoc != TargetSpikeLocation)
	{
		float Speed = (TargetSpikeLocation == ExtendedRelativeLocation) ? SpikeExtendSpeed : SpikeRetractSpeed;
		FVector NewLoc = FMath::VInterpConstantTo(CurrentLoc, TargetSpikeLocation, DeltaTime, Speed);
		SpikeMesh->SetRelativeLocation(NewLoc);
	}
}

void ATrapSpikeFloor::ExecuteTrapPayload()
{
	if (!HasAuthority() || !PayloadComponent || !TriggerComponent)
	{
		return;
	}

	const TArray<AActor*>& Overlapped = TriggerComponent->GetOverlappingActors();
	for (AActor* Target : Overlapped)
	{
		if (Target && Target->GetClass()->ImplementsInterface(UTrapTargetInterface::StaticClass()))
		{
			PayloadComponent->ExecutePayloadOnActor(Target, TrapData);
		}
	}
}
