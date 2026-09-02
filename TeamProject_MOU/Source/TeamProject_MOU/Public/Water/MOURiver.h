// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WaterBodyRiverActor.h"
#include "MOURiver.generated.h"

/**
 * 런타임에 수위를 바꿀 수 있는 River 액터. AWaterBodyRiver와 완전히 동일하지만,
 * 내부 컴포넌트를 UMOUWaterBodyRiverComponent로 교체해 IsBodyDynamic()이 true가 되도록 한다.
 * 레벨에 이 클래스로 배치하거나, 기존 AWaterBodyRiver 액터를 에디터 아웃라이너에서
 * 우클릭 → Convert Actor(Replace Selected Actors With...)로 이 클래스로 바꿔주면 된다.
 */
UCLASS()
class TEAMPROJECT_MOU_API AMOURiver : public AWaterBodyRiver
{
	GENERATED_BODY()

public:
	AMOURiver(const FObjectInitializer& ObjectInitializer);
};
