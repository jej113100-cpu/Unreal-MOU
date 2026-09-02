// Copyright Epic Games, Inc. All Rights Reserved.

#include "Water/MOULake.h"
#include "Water/MOUWaterBodyLakeComponent.h"

AMOULake::AMOULake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WaterBodyLakeComponentClass = UMOUWaterBodyLakeComponent::StaticClass();
}
