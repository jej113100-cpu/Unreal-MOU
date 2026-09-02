// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WaterBodyLakeComponent.h"
#include "MOUWaterBodyLakeComponent.generated.h"

/**
 * UWaterBodyLakeComponent도 River와 마찬가지로 IsBodyDynamic()이 false라서 런타임(패키지 빌드)에서는
 * UpdateAll()/OnWaterBodyChanged()가 아무 것도 재생성하지 않는다. 이 컴포넌트는 IsBodyDynamic()을
 * true로 오버라이드해서 런타임 갱신 경로를 타게 하고, 수위를 바꿀 수 있는 함수를 노출한다.
 *
 * Lake는 IsFlatSurface()가 true라서, OnWaterBodyChanged() 안에서 호출되는 UpdateWaterHeight()가
 * 스플라인 포인트 전부를 액터의 Z에 맞춰 자동으로 재배치해준다 (River처럼 개별 포인트를 신경 쓸 필요 없음).
 */
UCLASS(ClassGroup = (Water), meta = (BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UMOUWaterBodyLakeComponent : public UWaterBodyLakeComponent
{
	GENERATED_BODY()

public:
	virtual bool IsBodyDynamic() const override { return true; }

	/**
	 * AWaterBody::InitializeBody()가 생성 직후 무조건 Static으로 되돌려놓기 때문에, 그보다 나중인
	 * 여기서 다시 Movable로 세팅해야 게임 월드에서 SetActorLocation이 씹히지 않는다. WaterMID도
	 * Convert Actor 등으로 클래스가 바뀐 직후엔 살아남지 못하므로 여기서 재생성을 보장한다.
	 */
	virtual void OnRegister() override;

	/**
	 * 호수 전체를 주어진 월드 Z 높이로 옮긴다. LakeMeshComp/LakeCollision은 어태치된 자식이라
	 * 위치만 바꿔도 같이 움직이므로, 매 프레임 호출해도(예: Timeline) 메쉬/충돌을 재생성하지
	 * 않아 가볍다. 형태 자체를 바꾼 경우에는 대신 RefreshLakeBody를 호출할 것.
	 */
	UFUNCTION(BlueprintCallable, Category = "Water|Lake")
	void SetLakeWaterLevelZ(float NewWorldZ, bool bUserTriggered = true);

	/** 현재 위치에서 DeltaZ 만큼 호수 전체 수위를 올리거나 내린다. */
	UFUNCTION(BlueprintCallable, Category = "Water|Lake")
	void AdjustLakeWaterLevel(float DeltaZ, bool bUserTriggered = true);

	/**
	 * 형태 자체가 바뀐 뒤 메쉬/충돌 재생성을 강제로 트리거하고 싶을 때만 사용. 단순 Z 이동에는
	 * 필요 없고, 매 프레임 호출하면 메쉬/충돌이 계속 재생성되어 깜빡임이 생기니 필요할 때만 호출할 것.
	 */
	UFUNCTION(BlueprintCallable, Category = "Water|Lake")
	void RefreshLakeBody(bool bShapeOrPositionChanged = true, bool bUserTriggered = true);
};
