#include "UI/MOU_SwayBox.h"
#include "UI/MOU_HUDSwaySubsystem.h"
#include "Components/PanelSlot.h"
#include "Engine/World.h"

UMOU_SwayBox::UMOU_SwayBox(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsVariable = false;
	SetVisibilityInternal(ESlateVisibility::SelfHitTestInvisible);
}

void UMOU_SwayBox::ReleaseSlateResources(bool bReleaseChildren)
{
	UnregisterFromSubsystem();
	Super::ReleaseSlateResources(bReleaseChildren);
	MyBox.Reset();
}

TSharedRef<SWidget> UMOU_SwayBox::RebuildWidget()
{
	MyBox = SNew(SBox);

	if (GetChildrenCount() > 0)
	{
		if (UPanelSlot* ContentSlot = GetContentSlot())
		{
			if (ContentSlot->Content)
			{
				MyBox->SetContent(ContentSlot->Content->TakeWidget());
			}
		}
	}

	RegisterToSubsystem();
	return MyBox.ToSharedRef();
}

void UMOU_SwayBox::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	RegisterToSubsystem();
}

void UMOU_SwayBox::OnSlotAdded(UPanelSlot* InSlot)
{
	if (MyBox.IsValid() && InSlot && InSlot->Content)
	{
		MyBox->SetContent(InSlot->Content->TakeWidget());
	}
}

void UMOU_SwayBox::OnSlotRemoved(UPanelSlot* InSlot)
{
	if (MyBox.IsValid())
	{
		MyBox->SetContent(SNullWidget::NullWidget);
	}
}

void UMOU_SwayBox::SetParallaxMultiplier(float InMultiplier)
{
	ParallaxMultiplier = InMultiplier;
	RegisterToSubsystem();
}

void UMOU_SwayBox::SetEnableTilt(bool bInEnableTilt)
{
	bEnableTilt = bInEnableTilt;
	RegisterToSubsystem();
}

void UMOU_SwayBox::RegisterToSubsystem()
{
	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		if (UMOU_HUDSwaySubsystem* SwaySub = UMOU_HUDSwaySubsystem::GetHUDSwaySubsystem(this))
		{
			SwaySub->RegisterSwayTarget(this, ParallaxMultiplier, bEnableTilt);
		}
	}
}

void UMOU_SwayBox::UnregisterFromSubsystem()
{
	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		if (UMOU_HUDSwaySubsystem* SwaySub = UMOU_HUDSwaySubsystem::GetHUDSwaySubsystem(this))
		{
			SwaySub->UnregisterSwayTarget(this);
		}
	}
}
