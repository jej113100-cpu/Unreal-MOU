#include "Traps/Components/TrapTriggerComponent.h"
#include "Components/ShapeComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"

UTrapTriggerComponent::UTrapTriggerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void UTrapTriggerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (TriggerType == ETrapTriggerType::PeriodicTimer)
		{
			StartPeriodicTimer();
		}
	}
}

void UTrapTriggerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopPeriodicTimer();
	SetTriggerEnabled(false);

	Super::EndPlay(EndPlayReason);
}

void UTrapTriggerComponent::RegisterDetectionVolume(UShapeComponent* InDetectionVolume)
{
	if (DetectionVolume)
	{
		DetectionVolume->OnComponentBeginOverlap.RemoveDynamic(this, &UTrapTriggerComponent::HandleDetectionBeginOverlap);
		DetectionVolume->OnComponentEndOverlap.RemoveDynamic(this, &UTrapTriggerComponent::HandleDetectionEndOverlap);
	}

	DetectionVolume = InDetectionVolume;

	if (DetectionVolume && bTriggerEnabled)
	{
		DetectionVolume->OnComponentBeginOverlap.AddDynamic(this, &UTrapTriggerComponent::HandleDetectionBeginOverlap);
		DetectionVolume->OnComponentEndOverlap.AddDynamic(this, &UTrapTriggerComponent::HandleDetectionEndOverlap);
	}
}

void UTrapTriggerComponent::StartPeriodicTimer()
{
	if (!GetWorld() || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	StopPeriodicTimer();

	if (PeriodicInterval > 0.0f)
	{
		GetWorld()->GetTimerManager().SetTimer(
			PeriodicTimerHandle,
			this,
			&UTrapTriggerComponent::HandlePeriodicTimerTick,
			PeriodicInterval,
			true,
			PeriodicInterval
		);
	}
}

void UTrapTriggerComponent::StopPeriodicTimer()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(PeriodicTimerHandle);
	}
}

void UTrapTriggerComponent::TriggerManually(AActor* InstigatorActor)
{
	if (!bTriggerEnabled)
	{
		return;
	}

	OnTriggerActivated.Broadcast(InstigatorActor);
}

void UTrapTriggerComponent::SetTriggerEnabled(bool bEnabled)
{
	bTriggerEnabled = bEnabled;

	if (!bTriggerEnabled)
	{
		StopPeriodicTimer();
		OverlappingActors.Empty();
	}
	else if (TriggerType == ETrapTriggerType::PeriodicTimer && GetOwner() && GetOwner()->HasAuthority())
	{
		StartPeriodicTimer();
	}
}

void UTrapTriggerComponent::HandleDetectionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bTriggerEnabled || !OtherActor || OtherActor == GetOwner())
	{
		return;
	}

	OverlappingActors.AddUnique(OtherActor);

	// 압력판이나 근접 감지 방식일 경우 오버랩 즉시 발동 알림
	if (TriggerType == ETrapTriggerType::PressurePlate || TriggerType == ETrapTriggerType::ProximitySensor)
	{
		OnTriggerActivated.Broadcast(OtherActor);
	}
}

void UTrapTriggerComponent::HandleDetectionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!OtherActor)
	{
		return;
	}

	OverlappingActors.Remove(OtherActor);

	if (OverlappingActors.Num() == 0)
	{
		OnTriggerDeactivated.Broadcast();
	}
}

void UTrapTriggerComponent::HandlePeriodicTimerTick()
{
	if (!bTriggerEnabled)
	{
		return;
	}

	OnTriggerActivated.Broadcast(GetOwner());
}
