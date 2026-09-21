#pragma once

#include "CoreMinimal.h"
#include "Components/ContentWidget.h"
#include "Widgets/Layout/SBox.h"
#include "MOU_SwayBox.generated.h"

/**
 * UMOU_SwayBox
 * UMG 디자이너 팔레트에서 자식 위젯을 감싸면(Wrap With),
 * UMOU_HUDSwaySubsystem과 연동되어 자식 위젯들을 자동으로 흔들어주는 UMG 컨테이너 위젯
 */
UCLASS()
class TEAMPROJECT_MOU_API UMOU_SwayBox : public UContentWidget
{
	GENERATED_BODY()

public:
	UMOU_SwayBox(const FObjectInitializer& ObjectInitializer);

	// 패럴랙스 깊이감 배율 (1.0 = 표준, 1.25 = 앞쪽 돌출, 0.75 = 뒤쪽 원경)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway")
	float ParallaxMultiplier = 1.0f;

	// 좌우 흔들림 시 회전 기울기(Tilt) 적용 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway")
	bool bEnableTilt = true;

	// 런타임에 패럴랙스 배율 동적 변경
	UFUNCTION(BlueprintCallable, Category = "Sway")
	void SetParallaxMultiplier(float InMultiplier);

	// 런타임에 틸트 활성화 동적 변경
	UFUNCTION(BlueprintCallable, Category = "Sway")
	void SetEnableTilt(bool bInEnableTilt);

	// -- UVisual Interface --
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	// -- UWidget Interface --
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;

	// -- UPanelWidget Interface --
	virtual void OnSlotAdded(UPanelSlot* InSlot) override;
	virtual void OnSlotRemoved(UPanelSlot* InSlot) override;

private:
	TSharedPtr<SBox> MyBox;

	void RegisterToSubsystem();
	void UnregisterFromSubsystem();
};
