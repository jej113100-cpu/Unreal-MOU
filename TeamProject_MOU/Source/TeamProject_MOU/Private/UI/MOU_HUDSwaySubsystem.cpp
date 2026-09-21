#include "UI/MOU_HUDSwaySubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

UMOU_HUDSwaySubsystem::UMOU_HUDSwaySubsystem()
{
}

void UMOU_HUDSwaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisteredTargets.Empty();
	CurrentSwayOffset = FVector2D::ZeroVector;
	TargetSwayOffset = FVector2D::ZeroVector;
	CurrentTiltAngle = 0.0f;
	bHasPreviousRotation = false;
}

void UMOU_HUDSwaySubsystem::Deinitialize()
{
	ClearAllSwayTargets();
	Super::Deinitialize();
}

bool UMOU_HUDSwaySubsystem::IsTickable() const
{
	if (IsTemplate())
	{
		return false;
	}
	const ULocalPlayer* LP = GetLocalPlayer();
	if (!LP)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld();
}

TStatId UMOU_HUDSwaySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMOU_HUDSwaySubsystem, STATGROUP_Tickables);
}

UMOU_HUDSwaySubsystem* UMOU_HUDSwaySubsystem::GetHUDSwaySubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return nullptr;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return nullptr;
	}
	ULocalPlayer* LP = PC->GetLocalPlayer();
	if (!LP)
	{
		return nullptr;
	}
	return LP->GetSubsystem<UMOU_HUDSwaySubsystem>();
}

void UMOU_HUDSwaySubsystem::RegisterSwayTarget(UWidget* InWidget, float InParallaxMultiplier, bool bInEnableTilt)
{
	if (!InWidget)
	{
		return;
	}

	for (FMOU_SwayTargetInfo& Info : RegisteredTargets)
	{
		if (Info.TargetWidget.Get() == InWidget)
		{
			Info.ParallaxMultiplier = InParallaxMultiplier;
			Info.bEnableTilt = bInEnableTilt;
			return;
		}
	}

	FMOU_SwayTargetInfo NewInfo;
	NewInfo.TargetWidget = InWidget;
	NewInfo.ParallaxMultiplier = InParallaxMultiplier;
	NewInfo.bEnableTilt = bInEnableTilt;
	RegisteredTargets.Add(NewInfo);
}

void UMOU_HUDSwaySubsystem::UnregisterSwayTarget(UWidget* InWidget)
{
	if (!InWidget)
	{
		return;
	}

	for (int32 i = RegisteredTargets.Num() - 1; i >= 0; --i)
	{
		if (RegisteredTargets[i].TargetWidget.Get() == InWidget)
		{
			InWidget->SetRenderTranslation(FVector2D::ZeroVector);
			InWidget->SetRenderTransformAngle(0.0f);
			RegisteredTargets.RemoveAt(i);
			break;
		}
	}
}

void UMOU_HUDSwaySubsystem::ClearAllSwayTargets()
{
	for (FMOU_SwayTargetInfo& Info : RegisteredTargets)
	{
		if (UWidget* Widget = Info.TargetWidget.Get())
		{
			Widget->SetRenderTranslation(FVector2D::ZeroVector);
			Widget->SetRenderTransformAngle(0.0f);
		}
	}
	RegisteredTargets.Empty();
}

void UMOU_HUDSwaySubsystem::Tick(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	UpdatePhysics(DeltaTime);
	ApplyToTargets();
}

void UMOU_HUDSwaySubsystem::UpdatePhysics(float DeltaTime)
{
	if (!Settings.bEnableSway)
	{
		CurrentSwayOffset = FMath::Vector2DInterpTo(CurrentSwayOffset, FVector2D::ZeroVector, DeltaTime, Settings.SwayReturnSpeed);
		TargetSwayOffset = FVector2D::ZeroVector;
		CurrentTiltAngle = FMath::FInterpTo(CurrentTiltAngle, 0.0f, DeltaTime, Settings.SwayReturnSpeed);
		return;
	}

	const ULocalPlayer* LP = GetLocalPlayer();
	if (!LP)
	{
		return;
	}

	APlayerController* PC = LP->GetPlayerController(GetWorld());
	if (PC)
	{
		const FRotator CurrentControlRot = PC->GetControlRotation();
		if (bHasPreviousRotation)
		{
			const FRotator DeltaRot = (CurrentControlRot - PreviousControlRotation).GetNormalized();

			FVector2D AddedSway;
			AddedSway.X = -DeltaRot.Yaw * Settings.SwaySensitivity.X;
			AddedSway.Y = DeltaRot.Pitch * Settings.SwaySensitivity.Y;

			if (APawn* Pawn = PC->GetPawn())
			{
				if (Settings.MovementSwayIntensity > 0.0f)
				{
					const FVector Velocity = Pawn->GetVelocity();
					const FVector LocalVel = Pawn->GetActorTransform().InverseTransformVector(Velocity);
					AddedSway.X -= (LocalVel.Y / 500.0f) * Settings.MovementSwayIntensity;
					AddedSway.Y += (LocalVel.X / 500.0f) * (Settings.MovementSwayIntensity * 0.5f);
				}
			}

			TargetSwayOffset += AddedSway;
			TargetSwayOffset.X = FMath::Clamp(TargetSwayOffset.X, -Settings.MaxSwayOffset, Settings.MaxSwayOffset);
			TargetSwayOffset.Y = FMath::Clamp(TargetSwayOffset.Y, -Settings.MaxSwayOffset, Settings.MaxSwayOffset);
		}
		else
		{
			bHasPreviousRotation = true;
		}
		PreviousControlRotation = CurrentControlRot;
	}

	// 1. 현재 오프셋을 목표치로 보간
	CurrentSwayOffset = FMath::Vector2DInterpTo(CurrentSwayOffset, TargetSwayOffset, DeltaTime, Settings.SwayInterpSpeed);

	// 2. 목표치를 원점으로 서서히 복귀 감쇠
	TargetSwayOffset = FMath::Vector2DInterpTo(TargetSwayOffset, FVector2D::ZeroVector, DeltaTime, Settings.SwayReturnSpeed);

	// 3. 틸트 각도 산출
	CurrentTiltAngle = CurrentSwayOffset.X * Settings.TiltAngleMultiplier;
}

void UMOU_HUDSwaySubsystem::ApplyToTargets()
{
	for (int32 i = RegisteredTargets.Num() - 1; i >= 0; --i)
	{
		FMOU_SwayTargetInfo& Info = RegisteredTargets[i];
		UWidget* Widget = Info.TargetWidget.Get();
		if (!Widget)
		{
			RegisteredTargets.RemoveAt(i);
			continue;
		}

		Widget->SetRenderTranslation(CurrentSwayOffset * Info.ParallaxMultiplier);
		if (Info.bEnableTilt && Settings.TiltAngleMultiplier != 0.0f)
		{
			Widget->SetRenderTransformAngle(CurrentTiltAngle * Info.ParallaxMultiplier);
		}
	}
}
