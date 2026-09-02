#include "Traps/Actors/TrapElectricPanel.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "NiagaraComponent.h"
#include "Traps/Components/TrapTriggerComponent.h"
#include "Traps/Components/TrapPayloadComponent.h"
#include "Traps/Data/TrapDataAsset.h"

ATrapElectricPanel::ATrapElectricPanel()
{
	PanelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PanelMesh"));
	PanelMesh->SetupAttachment(RootScene);

	if (BaseTriggerBox)
	{
		BaseTriggerBox->SetupAttachment(PanelMesh);
		BaseTriggerBox->SetRelativeLocation(FVector(0.0f, 0.0f, 25.0f));
		BaseTriggerBox->InitBoxExtent(FVector(100.0f, 100.0f, 40.0f));
	}

	SparkFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("SparkFX"));
	SparkFX->SetupAttachment(PanelMesh);
	SparkFX->SetRelativeLocation(FVector(0.0f, 0.0f, 15.0f));
	SparkFX->bAutoActivate = false;

	if (PayloadComponent)
	{
		PayloadComponent->DefaultHazardType = ETrapHazardType::ElectricShock;
	}

	if (TriggerComponent)
	{
		TriggerComponent->TriggerType = ETrapTriggerType::PressurePlate;
		TriggerComponent->PeriodicInterval = 4.0f;
	}
}

void ATrapElectricPanel::BeginPlay()
{
	Super::BeginPlay();
}

void ATrapElectricPanel::OnStateEntered(ETrapState NewState)
{
	Super::OnStateEntered(NewState);

	if (SparkFX)
	{
		if (NewState == ETrapState::Warning || NewState == ETrapState::Active)
		{
			SparkFX->Activate(true);
		}
		else
		{
			SparkFX->Deactivate();
		}
	}
}

void ATrapElectricPanel::ExecuteTrapPayload()
{
	if (!HasAuthority() || !TriggerComponent || !PayloadComponent)
	{
		return;
	}

	const TArray<AActor*>& Overlapped = TriggerComponent->GetOverlappingActors();
	for (AActor* Target : Overlapped)
	{
		PayloadComponent->ExecutePayloadOnActor(Target, TrapData);
	}
}
