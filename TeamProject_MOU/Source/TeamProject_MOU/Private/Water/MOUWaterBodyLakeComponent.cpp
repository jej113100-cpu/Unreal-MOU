// Copyright Epic Games, Inc. All Rights Reserved.

#include "Water/MOUWaterBodyLakeComponent.h"

void UMOUWaterBodyLakeComponent::OnRegister()
{
	Super::OnRegister();

	SetMobility(EComponentMobility::Movable);
	UpdateMaterialInstances();
}

void UMOUWaterBodyLakeComponent::SetLakeWaterLevelZ(float NewWorldZ, bool bUserTriggered)
{
	// LakeMeshComp/LakeCollision은 WaterBodyComponent에 어태치된 자식이라, 액터 전체를 Z로
	// 옮기면 부모-자식 어태치먼트를 통해 그냥 같이 움직인다. 여기서 RefreshLakeBody(OnUpdateBody)를
	// 매번 부르면 필요 없는 메쉬/콜리전 재생성이 매 틱 일어나서 깜빡이거나 순간적으로 안 보이는
	// 현상이 생기므로, 위치만 옮기고 형태 재생성은 하지 않는다.
	if (AActor* Owner = GetOwner())
	{
		FVector Location = Owner->GetActorLocation();
		Location.Z = NewWorldZ;
		Owner->SetActorLocation(Location);
	}

	// 화면에 보이는 물 표면은 WaterZone이 캡처하는 "Water Info Texture"를 샘플링해서 그려지므로,
	// 위치만 옮기고 이걸 갱신하라고 알려주지 않으면 화면상 물은 예전 위치에 멈춰 보인다. 플래그만
	// 세팅하고 실제 재캡처는 WaterSubsystem이 프레임당 한 번만 처리하므로 매 틱 불러도 가볍다.
	MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::UpdateWaterInfoTexture);
}

void UMOUWaterBodyLakeComponent::AdjustLakeWaterLevel(float DeltaZ, bool bUserTriggered)
{
	if (AActor* Owner = GetOwner())
	{
		SetLakeWaterLevelZ(Owner->GetActorLocation().Z + DeltaZ, bUserTriggered);
	}
}

void UMOUWaterBodyLakeComponent::RefreshLakeBody(bool bShapeOrPositionChanged, bool bUserTriggered)
{
	FOnWaterBodyChangedParams Params;
	Params.bShapeOrPositionChanged = bShapeOrPositionChanged;
	Params.bUserTriggered = bUserTriggered;
	OnWaterBodyChanged(Params);
}
