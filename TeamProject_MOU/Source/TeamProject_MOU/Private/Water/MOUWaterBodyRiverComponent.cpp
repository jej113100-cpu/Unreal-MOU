// Copyright Epic Games, Inc. All Rights Reserved.

#include "Water/MOUWaterBodyRiverComponent.h"

void UMOUWaterBodyRiverComponent::OnRegister()
{
	Super::OnRegister();

	// AWaterBody::InitializeBody()가 생성 직후 무조건 Static으로 되돌려놓기 때문에, 그보다
	// 나중인 여기서 다시 Movable로 세팅해야 게임 월드에서 SetActorLocation이 씹히지 않는다.
	SetMobility(EComponentMobility::Movable);

	// Convert Actor 등으로 컴포넌트 클래스가 바뀐 직후에도 WaterMID가 확실히 만들어지도록 보장한다.
	UpdateMaterialInstances();
}

void UMOUWaterBodyRiverComponent::SetRiverWaterLevelZ(float NewWorldZ, bool bUserTriggered)
{
	// SplineMeshComponent들의 시작/끝 위치는 로컬(액터 상대) 스페이스라서, 액터 전체를 Z로
	// 옮기는 것만으로 이미 부모-자식 어태치먼트를 통해 같이 움직인다. 여기서 RefreshRiverBody
	// (OnUpdateBody -> UpdateMesh + RecreatePhysicsState)를 매번 부르면, 필요도 없는 메쉬/물리
	// 스테이트 재생성이 매 틱 일어나서 렌더 스테이트가 잠깐씩 끊기며 깜빡이거나 순간적으로
	// 안 보이는 현상이 생긴다. 그래서 여기서는 위치만 옮기고 형태 재생성은 하지 않는다.
	if (AActor* Owner = GetOwner())
	{
		FVector Location = Owner->GetActorLocation();
		Location.Z = NewWorldZ;
		Owner->SetActorLocation(Location);
	}

	// 화면에 보이는 물 표면은 SplineMeshComponent가 아니라 WaterZone이 각 워터바디를 캡처해서
	// 만드는 "Water Info Texture"를 샘플링해서 그려진다. 위치만 옮기고 이 텍스처를 갱신하라고
	// 알려주지 않으면 액터는 움직여도 화면상 물은 예전 위치에 멈춰 보인다. 이 함수는 플래그만
	// 세팅하고(무거운 작업 없음), 실제 재캡처는 WaterSubsystem이 프레임당 한 번만 처리한다.
	MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::UpdateWaterInfoTexture);
}

void UMOUWaterBodyRiverComponent::AdjustRiverWaterLevel(float DeltaZ, bool bUserTriggered)
{
	if (AActor* Owner = GetOwner())
	{
		SetRiverWaterLevelZ(Owner->GetActorLocation().Z + DeltaZ, bUserTriggered);
	}
}

void UMOUWaterBodyRiverComponent::RefreshRiverBody(bool bShapeOrPositionChanged, bool bUserTriggered)
{
	FOnWaterBodyChangedParams Params;
	Params.bShapeOrPositionChanged = bShapeOrPositionChanged;
	Params.bUserTriggered = bUserTriggered;
	OnWaterBodyChanged(Params);
}
