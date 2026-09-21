#include "UI/CharacterCustomizationWidget.h"
#include "UI/ColorPickerWidget.h"
#include "Player/MainCharacter.h"
#include "Components/CharacterCustomizationComponent.h"
#include "Data/CustomizationTypes.h"
#include "GameFramework/PlayerController.h"

void UCharacterCustomizationWidget::NativeConstruct()
{
	Super::NativeConstruct();

	InitializeCustomization();
}

void UCharacterCustomizationWidget::InitializeCustomization()
{
	APlayerController* PC = GetOwningPlayer();
	if (PC)
	{
		CachedCharacter = Cast<AMainCharacter>(PC->GetPawn());
		if (CachedCharacter.IsValid())
		{
			CachedCustomizationComp = CachedCharacter->GetCustomizationComponent();
			if (CachedCustomizationComp.IsValid())
			{
				CurrentData = CachedCustomizationComp->GetCustomizationData();
				OriginalData = CurrentData;
				OnCustomizationDataInitialized(CurrentData);
			}
		}
	}
}

void UCharacterCustomizationWidget::NativeDestruct()
{
	CloseColorPicker();
	Super::NativeDestruct();
}

void UCharacterCustomizationWidget::SetBodyColor(FLinearColor InColor)
{
	CurrentData.BodyColor = InColor;
	UpdatePreview();
}

void UCharacterCustomizationWidget::SetMetallic(float InMetallic)
{
	CurrentData.Metallic = FMath::Clamp(InMetallic, 0.0f, 1.0f);
	UpdatePreview();
}

void UCharacterCustomizationWidget::SetRoughnessB(float InRoughnessB)
{
	CurrentData.RoughnessB = FMath::Clamp(InRoughnessB, 0.0f, 1.0f);
	UpdatePreview();
}

void UCharacterCustomizationWidget::SetRoughnessA(float InRoughnessA)
{
	CurrentData.RoughnessA = FMath::Clamp(InRoughnessA, 0.0f, 1.0f);
	UpdatePreview();
}

void UCharacterCustomizationWidget::SetDecalIndex(int32 InIndex)
{
	if (InIndex < 0 || InIndex >= GetAvailableDecalCount()) return;
	CurrentData.DecalIndex = InIndex;
	UpdatePreview();
}

void UCharacterCustomizationWidget::SetDecalsColor(FLinearColor InColor)
{
	CurrentData.DecalsColor = InColor;
	UpdatePreview();
}

void UCharacterCustomizationWidget::SetTilingX(float InTilingX)
{
	CurrentData.TilingX = FMath::Clamp(InTilingX, 0.01f, 20.0f);
	UpdatePreview();
}

void UCharacterCustomizationWidget::SetTilingY(float InTilingY)
{
	CurrentData.TilingY = FMath::Clamp(InTilingY, 0.01f, 20.0f);
	UpdatePreview();
}

void UCharacterCustomizationWidget::ConfirmAndSave()
{
	CloseColorPicker();

	if (CachedCustomizationComp.IsValid())
	{
		CachedCustomizationComp->ConfirmAndApplyCustomization(CurrentData);
	}

	if (CachedCharacter.IsValid())
	{
		CachedCharacter->EndCustomization();
	}

	RemoveFromParent();
}

void UCharacterCustomizationWidget::CancelAndExit()
{
	CloseColorPicker();

	if (CachedCustomizationComp.IsValid())
	{
		CachedCustomizationComp->ApplyPreview(OriginalData);
	}

	if (CachedCharacter.IsValid())
	{
		CachedCharacter->EndCustomization();
	}

	RemoveFromParent();
}

void UCharacterCustomizationWidget::ResetToDefault()
{
	const auto* Asset = GetEditingDataAsset();
	CurrentData = Asset && Asset->DefaultPresets.Num() > 0 ? Asset->DefaultPresets[0] : FCharacterCustomizationData();
	UpdatePreview();
	OnCustomizationDataInitialized(CurrentData);
}

void UCharacterCustomizationWidget::ApplyPreset(int32 PresetIndex)
{
	const auto* Asset = GetEditingDataAsset();
	if (!Asset || !Asset->DefaultPresets.IsValidIndex(PresetIndex)) return;
	CurrentData = Asset->DefaultPresets[PresetIndex];
	UpdatePreview();
	OnCustomizationDataInitialized(CurrentData);
}

void UCharacterCustomizationWidget::RotateCharacter(float DeltaX)
{
	if (CachedCharacter.IsValid())
	{
		CachedCharacter->AddCustomizationCharacterYaw(DeltaX * DragRotationSpeed);
	}
}

int32 UCharacterCustomizationWidget::GetAvailableDecalCount() const
{
	const auto* Asset = GetEditingDataAsset();
	return Asset ? Asset->AvailableDecals.Num() : 0;
}

UTexture2D* UCharacterCustomizationWidget::GetDecalTexture(int32 Index) const
{
	const auto* Asset = GetEditingDataAsset();
	return Asset ? Asset->GetDecalTexture(Index) : nullptr;
}

void UCharacterCustomizationWidget::UpdatePreview()
{
	if (CachedCustomizationComp.IsValid())
	{
		CachedCustomizationComp->ApplyPreview(CurrentData);
	}
}

void UCharacterCustomizationWidget::CloseColorPicker()
{
	if (ActiveColorPicker)
	{
		ActiveColorPicker->RemoveFromParent();
		ActiveColorPicker = nullptr;
	}
	ActiveColorPickerType = 0;
}

bool UCharacterCustomizationWidget::IsColorPickerOpen() const
{
	return ActiveColorPicker != nullptr && ActiveColorPicker->IsInViewport();
}

UColorPickerWidget* UCharacterCustomizationWidget::OpenBodyColorPicker()
{
	// 1. 이미 바디 컬러 피커가 열려있는 경우: 닫고 nullptr 반환 (토글 OFF)
	if (ActiveColorPicker && ActiveColorPicker->IsInViewport() && ActiveColorPickerType == 1)
	{
		CloseColorPicker();
		return nullptr;
	}

	// 2. 다른 피커(예: 데칼)가 열려있다면 기존 피커 먼저 닫기
	CloseColorPicker();

	if (!ColorPickerWidgetClass)
	{
		return nullptr;
	}

	// 3. 새로 생성하여 화면에 띄우기 (토글 ON)
	ActiveColorPicker = CreateWidget<UColorPickerWidget>(GetOwningPlayer(), ColorPickerWidgetClass);
	if (ActiveColorPicker)
	{
		CloseColorPickers();
		ActiveColorPicker->InitializeColor(CurrentData.BodyColor);
		ActiveColorPicker->OnColorChanged.AddDynamic(this, &UCharacterCustomizationWidget::SetBodyColor);
		ActiveColorPicker->OnColorConfirmed.AddDynamic(this, &UCharacterCustomizationWidget::SetBodyColor);
		ActiveColorPicker->OnColorCancelled.AddDynamic(this, &UCharacterCustomizationWidget::SetBodyColor);
		ActiveColorPicker->AddToViewport(100);
		OpenColorPickers.Add(ActiveColorPicker);
	}
	return ActiveColorPicker;
}

UColorPickerWidget* UCharacterCustomizationWidget::OpenDecalColorPicker()
{
	// 1. 이미 데칼 컬러 피커가 열려있는 경우: 닫고 nullptr 반환 (토글 OFF)
	if (ActiveColorPicker && ActiveColorPicker->IsInViewport() && ActiveColorPickerType == 2)
	{
		CloseColorPicker();
		return nullptr;
	}

	// 2. 다른 피커(예: 바디)가 열려있다면 기존 피커 먼저 닫기
	CloseColorPicker();

	if (!ColorPickerWidgetClass)
	{
		return nullptr;
	}

	// 3. 새로 생성하여 화면에 띄우기 (토글 ON)
	ActiveColorPicker = CreateWidget<UColorPickerWidget>(GetOwningPlayer(), ColorPickerWidgetClass);
	if (ActiveColorPicker)
	{
		CloseColorPickers();
		ActiveColorPicker->InitializeColor(CurrentData.DecalsColor);
		ActiveColorPicker->OnColorChanged.AddDynamic(this, &UCharacterCustomizationWidget::SetDecalsColor);
		ActiveColorPicker->OnColorConfirmed.AddDynamic(this, &UCharacterCustomizationWidget::SetDecalsColor);
		ActiveColorPicker->OnColorCancelled.AddDynamic(this, &UCharacterCustomizationWidget::SetDecalsColor);
		ActiveColorPicker->AddToViewport(100);
		OpenColorPickers.Add(ActiveColorPicker);
	}
	return ActiveColorPicker;
}


UCustomizationDataAsset* UCharacterCustomizationWidget::GetEditingDataAsset() const
{
	if (CachedCustomizationComp.IsValid()) return CachedCustomizationComp->GetCustomizationDataAsset();
	return EditingDataAsset;
}

int32 UCharacterCustomizationWidget::GetAvailablePresetCount() const
{
	const auto* Asset = GetEditingDataAsset();
	return Asset ? Asset->DefaultPresets.Num() : 0;
}

void UCharacterCustomizationWidget::CloseColorPickers()
{
	for (UColorPickerWidget* Picker : OpenColorPickers)
	{
		if (!Picker) continue;
		Picker->OnColorChanged.RemoveAll(this);
		Picker->OnColorConfirmed.RemoveAll(this);
		Picker->OnColorCancelled.RemoveAll(this);
		Picker->RemoveFromParent();
	}
	OpenColorPickers.Reset();
}

