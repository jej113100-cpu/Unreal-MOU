// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WaterBodyRiverComponent.h"
#include "MOUWaterBodyRiverComponent.generated.h"

/**
 * UWaterBodyRiverComponent는 기본적으로 IsBodyDynamic()이 false라서, 런타임(패키지 빌드)에서는
 * UpdateAll()/OnWaterBodyChanged()를 호출해도 아무 것도 재생성되지 않는다 (Ocean만 예외).
 * 이 컴포넌트는 IsBodyDynamic()을 true로 오버라이드해서 러버도 Ocean과 동일한 런타임 갱신 경로를
 * 타도록 만들고, 블루프린트에서 수위를 바꿀 수 있는 함수를 노출한다.
 */
UCLASS(ClassGroup = (Water), meta = (BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UMOUWaterBodyRiverComponent : public UWaterBodyRiverComponent
{
	GENERATED_BODY()

public:
	virtual bool IsBodyDynamic() const override { return true; }

	/**
	 * WaterMID는 RF_Transient/RF_NonPIEDuplicateTransient라서 Convert Actor 등으로 컴포넌트
	 * 클래스를 바꿔치기하면 살아남지 못하고, 그 재생성은 보통 에디터 편집 콜백(PostEditChangeProperty 등)에서만
	 * 걸린다. 여기서 등록될 때마다 한 번 더 보장해줘서, 클래스를 바꾼 직후에도 물 머티리얼이 None으로
	 * 남아 회색으로 렌더링되는 문제가 생기지 않게 한다.
	 */
	virtual void OnRegister() override;

	/**
	 * 강 전체를 주어진 월드 Z 높이로 옮긴다. SplineMeshComponent들은 어태치된 자식이라 위치만
	 * 바꿔도 같이 움직이므로, 매 프레임 호출해도(예: Timeline) 메쉬/물리를 재생성하지 않아 가볍다.
	 * 스플라인 포인트 개수/폭 등 "형태" 자체를 바꾼 경우에는 대신 RefreshRiverBody를 호출할 것.
	 */
	UFUNCTION(BlueprintCallable, Category = "Water|River")
	void SetRiverWaterLevelZ(float NewWorldZ, bool bUserTriggered = true);

	/** 현재 위치에서 DeltaZ 만큼 강 전체 수위를 올리거나 내린다. */
	UFUNCTION(BlueprintCallable, Category = "Water|River")
	void AdjustRiverWaterLevel(float DeltaZ, bool bUserTriggered = true);

	/**
	 * 스플라인 포인트를 직접 편집했거나(예: 한쪽 구간만 범람, 폭 변경) 형태 자체가 바뀐 뒤
	 * 메쉬/충돌 재생성을 강제로 트리거하고 싶을 때만 사용. 단순 Z 이동에는 필요 없고, 매 프레임
	 * 호출하면 메쉬/물리 스테이트가 계속 재생성되어 깜빡임이 생기니 필요할 때만 호출할 것.
	 */
	UFUNCTION(BlueprintCallable, Category = "Water|River")
	void RefreshRiverBody(bool bShapeOrPositionChanged = true, bool bUserTriggered = true);
};
