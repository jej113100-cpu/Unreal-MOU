#include "Item/TerminalShopWidget.h"

#include "Item/TerminalShop.h"
#include "TeamProject_MOUPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/Widget.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

// [TSHOP-011] 키보드 입력을 받을 수 있도록 상점 위젯을 설정한다.
UTerminalShopWidget::UTerminalShopWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

// [TSHOP-005] UI를 연 상점 액터를 기록한다.
void UTerminalShopWidget::InitializeShop(ATerminalShop* InShop)
{
	OwningShop = InShop;
}

// [TSHOP-006] 상점 UI에 입력 초점과 마우스 커서를 준다.
void UTerminalShopWidget::ActivateShopInput()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		// 이동 입력을 누른 채 상점을 열면 UI 전환 뒤 Key Up을 받지 못해 이동이 계속될 수 있다.
		// 현재 속도와 눌린 키 상태를 함께 비워 상점이 열리는 즉시 플레이어를 정지시킨다.
		if (APawn* Pawn = PC->GetPawn())
		{
			if (UPawnMovementComponent* MovementComponent = Pawn->GetMovementComponent())
			{
				MovementComponent->StopMovementImmediately();
			}
		}
		PC->FlushPressedKeys();

		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
		PC->bShowMouseCursor = true;
		SetKeyboardFocus();
	}
}

// [TSHOP-007] 상점 UI를 닫고 게임 입력으로 돌린다.
void UTerminalShopWidget::CloseShop()
{
	if (bIsClosing)
	{
		return;
	}

	bIsOpening = false;
	bIsClosing = true;
	PanelAnimationTime = 0.0f;

	if (AnimatedPanel)
	{
		AnimatedPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
		return;
	}

	FinishCloseShop();
}

void UTerminalShopWidget::ForceCloseShopImmediately()
{
	bIsOpening = false;
	bIsClosing = true;
	FinishCloseShop();
}

// [TSHOP-016] BP 장바구니 맵을 서버 구매 요청 구조체 배열로 변환해 소유 컨트롤러로 전달합니다.
void UTerminalShopWidget::SubmitTerminalPurchase(const TMap<FName, int32>& CartItems)
{
	ATeamProject_MOUPlayerController* PC = Cast<ATeamProject_MOUPlayerController>(GetOwningPlayer());
	if (!PC || CartItems.IsEmpty())
	{
		return;
	}

	TArray<FTerminalCartItem> PurchaseItems;
	PurchaseItems.Reserve(CartItems.Num());
	for (const TPair<FName, int32>& CartItem : CartItems)
	{
		if (CartItem.Key.IsNone() || CartItem.Value <= 0)
		{
			continue;
		}

		FTerminalCartItem PurchaseItem;
		PurchaseItem.RowName = CartItem.Key;
		PurchaseItem.Quantity = CartItem.Value;
		PurchaseItems.Add(PurchaseItem);
	}

	if (!PurchaseItems.IsEmpty())
	{
		PC->ServerRequestTerminalPurchase(PurchaseItems);
	}
}

void UTerminalShopWidget::HandlePreLoadMap(const FString& MapName)
{
	ForceCloseShopImmediately();
}

// [TSHOP-014] 닫기 애니메이션 종료 후 입력 모드와 위젯 참조를 정리한다.
void UTerminalShopWidget::FinishCloseShop()
{
	if (ATerminalShop* Shop = OwningShop.Get())
	{
		Shop->NotifyWidgetClosed(this);
	}
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
	RemoveFromParent();
}

void UTerminalShopWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (!PreLoadMapHandle.IsValid())
	{
		PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(
			this,
			&UTerminalShopWidget::HandlePreLoadMap);
	}
}

void UTerminalShopWidget::NativeDestruct()
{
	if (PreLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
		PreLoadMapHandle.Reset();
	}

	if (ATerminalShop* Shop = OwningShop.Get())
	{
		Shop->NotifyWidgetClosed(this);
	}
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}

	Super::NativeDestruct();
}

// [TSHOP-008] Widget BP 콘텐츠를 기준 해상도에 맞춰 화면에 배치한다.
void UTerminalShopWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree)
	{
		return;
	}

	UWidget* BlueprintContent = WidgetTree->RootWidget;
	UOverlay* Layers = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ShopLayers"));

	UScaleBox* Scale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("ShopScale"));
	Scale->SetStretch(EStretch::ScaleToFit);
	UOverlaySlot* ScaleSlot = Layers->AddChildToOverlay(Scale);
	ScaleSlot->SetHorizontalAlignment(HAlign_Fill);
	ScaleSlot->SetVerticalAlignment(VAlign_Fill);
	USizeBox* Fixed = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ShopSize"));
	Fixed->SetWidthOverride(1920.0f);
	Fixed->SetHeightOverride(1080.0f);
	Scale->AddChild(Fixed);
	if (BlueprintContent)
	{
		Fixed->AddChild(BlueprintContent);
		AnimatedPanel = BlueprintContent;
		PanelAnimationTime = 0.0f;
		bIsOpening = true;
		bIsClosing = false;
		ApplyPanelAnimation(0.0f, false);
	}
	WidgetTree->RootWidget = Layers;
}

// [TSHOP-009] Esc 입력을 받아 상점 UI를 닫는다.
FReply UTerminalShopWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		CloseShop();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// [TSHOP-012] 상점 패널의 열기·닫기 렌더 애니메이션을 프레임마다 갱신한다.
void UTerminalShopWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bIsClosing)
	{
		ATerminalShop* Shop = OwningShop.Get();
		APlayerController* PC = GetOwningPlayer();
		if (!Shop || !PC)
		{
			ForceCloseShopImmediately();
			return;
		}

		APawn* Pawn = PC->GetPawn();
		if (!Pawn || FVector::DistSquared(Pawn->GetActorLocation(), Shop->GetActorLocation()) > FMath::Square(Shop->GetInteractionRadius() * 1.5f))
		{
			CloseShop();
			return;
		}
	}

	if (!AnimatedPanel || (!bIsOpening && !bIsClosing))
	{
		return;
	}

	PanelAnimationTime += InDeltaTime;
	const float Duration = bIsClosing ? CloseAnimationDuration : OpenAnimationDuration;
	const float Progress = FMath::Clamp(PanelAnimationTime / Duration, 0.0f, 1.0f);
	ApplyPanelAnimation(Progress, bIsClosing);

	if (Progress < 1.0f)
	{
		return;
	}

	if (bIsClosing)
	{
		FinishCloseShop();
		return;
	}

	bIsOpening = false;
}

// [TSHOP-013] 기존 크기와 위치를 유지하며 진행률에 맞춰 패널의 투명도만 적용한다.
void UTerminalShopWidget::ApplyPanelAnimation(float Progress, bool bClosing) const
{
	if (!AnimatedPanel)
	{
		return;
	}

	const float ClampedProgress = FMath::Clamp(Progress, 0.0f, 1.0f);

	if (bClosing)
	{
		AnimatedPanel->SetRenderOpacity(1.0f - ClampedProgress);
		return;
	}

	AnimatedPanel->SetRenderOpacity(FMath::Clamp(ClampedProgress * 1.8f, 0.0f, 1.0f));
}
