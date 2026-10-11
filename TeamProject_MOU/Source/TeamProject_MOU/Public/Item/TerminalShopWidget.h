#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TerminalShopWidget.generated.h"

class ATerminalShop;
class UWidget;

/** 상점 배경 위에 Widget BP 콘텐츠를 표시한다. */
UCLASS()
class TEAMPROJECT_MOU_API UTerminalShopWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// [TSHOP-011] 키보드 입력을 받을 수 있도록 상점 위젯을 설정한다.
	UTerminalShopWidget(const FObjectInitializer& ObjectInitializer);

	// [TSHOP-005] UI를 연 상점 액터를 기록한다.
	void InitializeShop(ATerminalShop* InShop);

	// [TSHOP-006] 상점 UI에 입력 초점과 마우스 커서를 준다.
	void ActivateShopInput();

	// [TSHOP-007] 상점 UI를 닫고 게임 입력으로 돌린다.
	UFUNCTION(BlueprintCallable, Category="Terminal Shop")
	void CloseShop();

	UFUNCTION(BlueprintCallable, Category="Terminal Shop")
	void ForceCloseShopImmediately();

	// [TSHOP-016] BP 장바구니 맵을 서버 구매 요청 구조체 배열로 변환해 소유 컨트롤러로 전달합니다.
	UFUNCTION(BlueprintCallable, Category="Terminal Shop")
	void SubmitTerminalPurchase(const TMap<FName, int32>& CartItems);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// [TSHOP-008] Widget BP 콘텐츠를 기준 해상도에 맞춰 화면에 배치한다.
	virtual void NativeOnInitialized() override;

	// [TSHOP-009] Esc 입력을 받아 상점 UI를 닫는다.
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	// [TSHOP-012] 상점 패널의 열기·닫기 렌더 애니메이션을 프레임마다 갱신한다.
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	// [TSHOP-013] 기존 크기와 위치를 유지하며 진행률에 맞춰 패널의 투명도만 적용한다.
	void ApplyPanelAnimation(float Progress, bool bIsClosing) const;

	// [TSHOP-014] 닫기 애니메이션 종료 후 입력 모드와 위젯 참조를 정리한다.
	void FinishCloseShop();

	void HandlePreLoadMap(const FString& MapName);
	FDelegateHandle PreLoadMapHandle;

	TWeakObjectPtr<ATerminalShop> OwningShop;

	// BP에서 제작한 실제 상점 패널 루트. 배경 레이아웃에는 영향을 주지 않고 이 위젯만 움직인다.
	UPROPERTY(Transient)
	TObjectPtr<UWidget> AnimatedPanel;

	// 현재 열기 또는 닫기 애니메이션의 누적 시간이다.
	float PanelAnimationTime = 0.0f;

	// 닫기 요청 중복과 종료 시점을 구분한다.
	bool bIsClosing = false;
	bool bIsOpening = true;

	// 빠르고 경쾌한 터미널 UI 모션을 위한 재생 시간이다.
	UPROPERTY(EditDefaultsOnly, Category="Terminal Shop|Animation", meta=(ClampMin="0.01"))
	float OpenAnimationDuration = 0.24f;

	UPROPERTY(EditDefaultsOnly, Category="Terminal Shop|Animation", meta=(ClampMin="0.01"))
	float CloseAnimationDuration = 0.16f;
};
