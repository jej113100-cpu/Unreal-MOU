#include "UI/SpectatorOverlayWidget.h"
#include "Player/MainCharacter.h"
#include "GameFramework/PlayerState.h"

void USpectatorOverlayWidget::SetSpectatorInfo(AMainCharacter* TargetCharacter, int32 InAliveCount)
{
	AliveCount = InAliveCount;

	if (TargetCharacter)
	{
		if (APlayerState* PS = TargetCharacter->GetPlayerState())
		{
			CurrentTargetName = PS->GetPlayerName();
		}
		if (CurrentTargetName.IsEmpty())
		{
			CurrentTargetName = TargetCharacter->GetName();
		}
	}
	else
	{
		CurrentTargetName = TEXT("None");
	}

	OnSpectatorInfoUpdated(CurrentTargetName, AliveCount);
}
