#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MOU_CharacterStatusHUD.generated.h"

class UImage;
class UTexture2D;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class ECharacterStatusState : uint8
{
	Happy,
	OK,
	Warning,
	Critical,
	Offline
};

/**
 * Character Status HUD
 * Handles HP, Stamina circular bars and dynamically changes center portrait
 */
UCLASS()
class TEAMPROJECT_MOU_API UMOU_CharacterStatusHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Set Target HP (0.0 ~ 1.0) for interpolation
	UFUNCTION(BlueprintCallable, Category = "UI|Status")
	void SetTargetHP(float NewHPPercent);

	// Set Target Stamina (0.0 ~ 1.0) for interpolation
	UFUNCTION(BlueprintCallable, Category = "UI|Status")
	void SetTargetStamina(float NewStaminaPercent);

	// Immediately set values without interpolation
	UFUNCTION(BlueprintCallable, Category = "UI|Status")
	void ForceUpdateStatus(float NewHPPercent, float NewStaminaPercent);

protected:
	// -- UI Components --

	UPROPERTY(meta = (BindWidget))
	UImage* Image_HPBar;

	UPROPERTY(meta = (BindWidget))
	UImage* Image_StaminaBar;

	UPROPERTY(meta = (BindWidget))
	UImage* Image_CenterPortrait;

	UPROPERTY(meta = (BindWidget))
	UImage* Image_Background;

	// -- Portrait Textures --

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Portrait")
	UTexture2D* Tex_Happy;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Portrait")
	UTexture2D* Tex_OK;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Portrait")
	UTexture2D* Tex_Warning;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Portrait")
	UTexture2D* Tex_Critical;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Portrait")
	UTexture2D* Tex_Offline;

	// -- Background Textures --

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Background")
	UTexture2D* Tex_Bg_Happy;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Background")
	UTexture2D* Tex_Bg_OK;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Background")
	UTexture2D* Tex_Bg_Warning;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Background")
	UTexture2D* Tex_Bg_Critical;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Background")
	UTexture2D* Tex_Bg_Offline;

	// -- Interpolation Setting --

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Interpolation")
	float CatchUpInterpSpeed = 5.0f;

private:
	float TargetHPPercent = 1.0f;
	float CurrentHPPercent = 1.0f;

	float TargetStaminaPercent = 1.0f;
	float CurrentStaminaPercent = 1.0f;

	ECharacterStatusState CurrentState = ECharacterStatusState::Happy;

	UPROPERTY()
	UMaterialInstanceDynamic* MID_HPBar;

	UPROPERTY()
	UMaterialInstanceDynamic* MID_StaminaBar;

	void UpdatePortraitState(float InCurrentHP);
	void SetPortraitTextureByState(ECharacterStatusState NewState);
};
