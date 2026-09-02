// Copyright Epic Games, Inc. All Rights Reserved.

#include "Water/MOURiver.h"
#include "Water/MOUWaterBodyRiverComponent.h"

AMOURiver::AMOURiver(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WaterBodyRiverComponentClass = UMOUWaterBodyRiverComponent::StaticClass();
}
