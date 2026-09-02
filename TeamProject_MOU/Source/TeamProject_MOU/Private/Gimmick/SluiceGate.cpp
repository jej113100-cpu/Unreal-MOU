#include "Gimmick/SluiceGate.h"
#include "Components/SceneComponent.h"
#include "Components/ChildActorComponent.h"
#include "Water/MOUWaterBodyLakeComponent.h"
#include "Water/MOUWaterBodyRiverComponent.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Net/UnrealNetwork.h"

ASluiceGate::ASluiceGate()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	DefaultRootScene = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultRootScene"));
	SetRootComponent(DefaultRootScene);

	bStartFullyOpen = true;
	CurrentOpenRatio = 1.0f;
	TargetOpenRatio = 1.0f;
	VisualOpenRatio = 1.0f;

	GateTravelDistance = 1000.0f;
	WaterThresholdRatio = 0.7f;

	LakeInitialZ = 100.0f;
	LakeMaxZ = 300.0f;

	RiverInitialZ = 15.0f;
	RiverMinZ = -200.0f;

	InterpSpeed = 4.0f;
}

void ASluiceGate::BeginPlay()
{
	Super::BeginPlay();

	InitialGateLocation = GetActorLocation();

	// TargetDoorComponent가 지정되지 않은 경우, 자식 컴포넌트 중 'Gate' 또는 'Door' 이름 포함된 컴포넌트(BP_Gate 등)를 자동 탐색
	if (!TargetDoorComponent && GetRootComponent())
	{
		TArray<USceneComponent*> ChildComponents;
		GetRootComponent()->GetChildrenComponents(true, ChildComponents);
		for (USceneComponent* Child : ChildComponents)
		{
			if (Child && (Child->GetName().Contains(TEXT("Gate")) || Child->GetName().Contains(TEXT("Door"))))
			{
				TargetDoorComponent = Child;
				break;
			}
		}
	}

	if (TargetDoorComponent)
	{
		InitialDoorRelativeLocation = TargetDoorComponent->GetRelativeLocation();
		TargetDoorComponent->SetVisibility(true, true);
	}

	CurrentOpenRatio = bStartFullyOpen ? 1.0f : 0.0f;
	TargetOpenRatio = CurrentOpenRatio;
	VisualOpenRatio = CurrentOpenRatio;

	// Water Body 액터에서 커스텀 MOU 컴포넌트 캐싱 및 초기 수위 설정
	if (LakeWaterActor)
	{
		CachedLakeComp = LakeWaterActor->FindComponentByClass<UMOUWaterBodyLakeComponent>();
		if (CachedLakeComp.IsValid())
		{
			CachedLakeComp->SetLakeWaterLevelZ(LakeInitialZ);
		}
		else
		{
			FVector LakeLoc = LakeWaterActor->GetActorLocation();
			LakeLoc.Z = LakeInitialZ;
			LakeWaterActor->SetActorLocation(LakeLoc, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	if (RiverWaterActor)
	{
		CachedRiverComp = RiverWaterActor->FindComponentByClass<UMOUWaterBodyRiverComponent>();
		if (CachedRiverComp.IsValid())
		{
			CachedRiverComp->SetRiverWaterLevelZ(RiverInitialZ);
		}
		else
		{
			FVector RiverLoc = RiverWaterActor->GetActorLocation();
			RiverLoc.Z = RiverInitialZ;
			RiverWaterActor->SetActorLocation(RiverLoc, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	UpdateGateMovement(0.0f);
	UpdateWaterActors(0.0f);
}

void ASluiceGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASluiceGate, CurrentOpenRatio);
}

void ASluiceGate::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (HasAuthority())
	{
		float OldRatio = CurrentOpenRatio;
		CurrentOpenRatio = FMath::FInterpTo(CurrentOpenRatio, TargetOpenRatio, DeltaTime, InterpSpeed);

		if (!FMath::IsNearlyEqual(OldRatio, CurrentOpenRatio, 0.0001f))
		{
			OnGateRatioChanged.Broadcast(CurrentOpenRatio, CurrentOpenRatio - OldRatio);
			OnGateStateUpdated(CurrentOpenRatio);
		}
	}

	UpdateGateMovement(DeltaTime);
	UpdateWaterActors(DeltaTime);
}

void ASluiceGate::SetTargetOpenRatio(float NewRatio)
{
	TargetOpenRatio = FMath::Clamp(NewRatio, 0.0f, 1.0f);
	if (!HasAuthority())
	{
		ServerSetTargetOpenRatio(NewRatio);
	}
}

void ASluiceGate::ServerSetTargetOpenRatio_Implementation(float NewRatio)
{
	SetTargetOpenRatio(NewRatio);
}

void ASluiceGate::OnRep_CurrentOpenRatio()
{
	TargetOpenRatio = CurrentOpenRatio;
	OnGateRatioChanged.Broadcast(CurrentOpenRatio, 0.0f);
	OnGateStateUpdated(CurrentOpenRatio);
}

void ASluiceGate::UpdateGateMovement(float DeltaTime)
{
	VisualOpenRatio = (DeltaTime > 0.0f)
		? FMath::FInterpTo(VisualOpenRatio, CurrentOpenRatio, DeltaTime, InterpSpeed)
		: CurrentOpenRatio;

	// 레벨에 배치된 위치가 완전 열림(VisualOpenRatio=1.0) 위치이며, 닫힐수록(1.0 -> 0.0) 아래로 내려옵니다
	float DropAmount = (1.0f - VisualOpenRatio) * GateTravelDistance;

	if (TargetDoorComponent)
	{
		FVector NewRelLoc = InitialDoorRelativeLocation;
		NewRelLoc.Z = InitialDoorRelativeLocation.Z - DropAmount;
		TargetDoorComponent->SetRelativeLocation(NewRelLoc);
	}
	else
	{
		FVector TargetLocation = InitialGateLocation;
		TargetLocation.Z = InitialGateLocation.Z - DropAmount;
		SetActorLocation(TargetLocation);
	}
}

void ASluiceGate::UpdateWaterActors(float DeltaTime)
{
	float TargetLakeZ = LakeInitialZ;
	float TargetRiverZ = RiverInitialZ;

	// 수문이 70% 이하로 닫히기 시작할 때부터 수위가 점차 변화
	if (VisualOpenRatio < WaterThresholdRatio)
	{
		TargetRiverZ = FMath::GetMappedRangeValueClamped(
			FVector2D(0.0f, WaterThresholdRatio),
			FVector2D(RiverMinZ, RiverInitialZ),
			VisualOpenRatio
		);

		TargetLakeZ = FMath::GetMappedRangeValueClamped(
			FVector2D(0.0f, WaterThresholdRatio),
			FVector2D(LakeMaxZ, LakeInitialZ),
			VisualOpenRatio
		);
	}

	// 1. 머티리얼 파라미터 컬렉션(MPC) 갱신 (선택 사항)
	if (WaterMaterialParameterCollection)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), WaterMaterialParameterCollection, LakeZParameterName, TargetLakeZ);
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), WaterMaterialParameterCollection, RiverZParameterName, TargetRiverZ);
	}

	// 2. 호수 수위 갱신 (MOUWaterBodyLakeComponent 활용)
	if (LakeWaterActor)
	{
		float CurrentLakeZ = (DeltaTime > 0.0f)
			? FMath::FInterpTo(LakeWaterActor->GetActorLocation().Z, TargetLakeZ, DeltaTime, InterpSpeed)
			: TargetLakeZ;

		if (CachedLakeComp.IsValid())
		{
			CachedLakeComp->SetLakeWaterLevelZ(CurrentLakeZ);
		}
		else
		{
			FVector LakeLoc = LakeWaterActor->GetActorLocation();
			LakeLoc.Z = CurrentLakeZ;
			LakeWaterActor->SetActorLocation(LakeLoc, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	// 3. 강 수위 갱신 (MOUWaterBodyRiverComponent 활용)
	if (RiverWaterActor)
	{
		float CurrentRiverZ = (DeltaTime > 0.0f)
			? FMath::FInterpTo(RiverWaterActor->GetActorLocation().Z, TargetRiverZ, DeltaTime, InterpSpeed)
			: TargetRiverZ;

		if (CachedRiverComp.IsValid())
		{
			CachedRiverComp->SetRiverWaterLevelZ(CurrentRiverZ);
		}
		else
		{
			FVector RiverLoc = RiverWaterActor->GetActorLocation();
			RiverLoc.Z = CurrentRiverZ;
			RiverWaterActor->SetActorLocation(RiverLoc, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
}
