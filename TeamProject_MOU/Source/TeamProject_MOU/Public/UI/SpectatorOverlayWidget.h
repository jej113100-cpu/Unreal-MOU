#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SpectatorOverlayWidget.generated.h"

class AMainCharacter;

/**
 * Spectator Overlay Widget Base Class
 */
UCLASS()
class TEAMPROJECT_MOU_API USpectatorOverlayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Spectator")
	void SetSpectatorInfo(AMainCharacter* TargetCharacter, int32 InAliveCount);

	UFUNCTION(BlueprintImplementableEvent, Category = "Spectator")
	void OnSpectatorInfoUpdated(const FString& TargetName, int32 InAliveCount);

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Spectator")
	FString CurrentTargetName;

	UPROPERTY(BlueprintReadOnly, Category = "Spectator")
	int32 AliveCount = 0;
};
