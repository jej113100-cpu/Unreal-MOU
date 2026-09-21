#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tickable.h"
#include "Components/Widget.h"
#include "MOU_HUDSwaySubsystem.generated.h"

class APlayerController;
class AMainCharacter;

/**
 * FHUDSwaySettings
 * HUD Sway & Parallax 물리 동작 관련 세부 설정값 구조체
 */
USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FHUDSwaySettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Sway")
	bool bEnableSway = true;

	// 마우스 회전(Yaw/Pitch)에 따른 흔들림 감도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Sway")
	FVector2D SwaySensitivity = FVector2D(2.0f, 2.0f);

	// 캐릭터 로컬 이동 속도에 따른 미세 관성 흔들림 감도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Sway")
	float MovementSwayIntensity = 3.0f;

	// 최대 허용 흔들림 거리 (픽셀 단위)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Sway")
	float MaxSwayOffset = 25.0f;

	// 목표 오프셋 추적 보간 속도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Sway")
	float SwayInterpSpeed = 10.0f;

	// 마우스 멈춤 시 원점 복귀 속도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Sway")
	float SwayReturnSpeed = 6.0f;

	// 좌우 흔들림 시 회전 기울기 계수 (Degree)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Sway")
	float TiltAngleMultiplier = 0.06f;
};

/**
 * FMOU_SwayTargetInfo
 * 서브시스템에 등록되어 매 프레임 RenderTransform이 자동 적용되는 위젯 정보
 */
USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FMOU_SwayTargetInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "UI|Sway")
	TWeakObjectPtr<UWidget> TargetWidget = nullptr;

	// 패럴랙스 배율 (1.0 = 표준, 1.25 = 앞쪽 돌출, 0.75 = 뒤쪽 원경)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Sway")
	float ParallaxMultiplier = 1.0f;

	// 좌우 흔들림 시 회전 기울기(Tilt) 적용 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Sway")
	bool bEnableTilt = true;
};

/**
 * UMOU_HUDSwaySubsystem
 * 카메라 회전 및 캐릭터 이동 관성을 매 프레임 1회 계산하여,
 * 등록된 모든 UI 위젯에 통일된 관성 흔들림(Sway) 및 입체 패럴랙스(Parallax)를 부여하는 로컬 플레이어 서브시스템
 */
UCLASS()
class TEAMPROJECT_MOU_API UMOU_HUDSwaySubsystem : public ULocalPlayerSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UMOU_HUDSwaySubsystem();

	// -- ULocalPlayerSubsystem Lifecycle --
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// -- FTickableGameObject Interface --
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return false; }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }

	// -- Blueprint Convenience Static Helper --
	UFUNCTION(BlueprintPure, Category = "UI|Sway", meta = (WorldContext = "WorldContextObject"))
	static UMOU_HUDSwaySubsystem* GetHUDSwaySubsystem(const UObject* WorldContextObject);

	// -- Target Management --
	/** 흔들림 대상 위젯 등록 (이미 등록된 경우 배율 및 틸트 설정 갱신) */
	UFUNCTION(BlueprintCallable, Category = "UI|Sway")
	void RegisterSwayTarget(UWidget* InWidget, float InParallaxMultiplier = 1.0f, bool bInEnableTilt = true);

	/** 등록된 흔들림 대상 위젯 해제 (위젯의 RenderTransform 원점 복원) */
	UFUNCTION(BlueprintCallable, Category = "UI|Sway")
	void UnregisterSwayTarget(UWidget* InWidget);

	/** 모든 등록된 위젯 해제 */
	UFUNCTION(BlueprintCallable, Category = "UI|Sway")
	void ClearAllSwayTargets();

	// -- Query Functions --
	UFUNCTION(BlueprintPure, Category = "UI|Sway")
	FVector2D GetSwayOffset() const { return CurrentSwayOffset; }

	UFUNCTION(BlueprintPure, Category = "UI|Sway")
	float GetTiltAngle() const { return CurrentTiltAngle; }

	UFUNCTION(BlueprintPure, Category = "UI|Sway")
	FVector2D GetParallaxSwayOffset(float InMultiplier) const { return CurrentSwayOffset * InMultiplier; }

	// -- Settings --
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Sway|Settings")
	FHUDSwaySettings Settings;

	/** 전체 Sway 설정 일괄 적용 */
	UFUNCTION(BlueprintCallable, Category = "UI|Sway")
	void ApplySwaySettings(const FHUDSwaySettings& InSettings) { Settings = InSettings; }

	/** 현재 Sway 설정 조회 */
	UFUNCTION(BlueprintPure, Category = "UI|Sway")
	FHUDSwaySettings GetSwaySettings() const { return Settings; }

private:
	// 등록된 대상 위젯 목록
	UPROPERTY()
	TArray<FMOU_SwayTargetInfo> RegisteredTargets;

	FVector2D CurrentSwayOffset = FVector2D::ZeroVector;
	FVector2D TargetSwayOffset = FVector2D::ZeroVector;
	float CurrentTiltAngle = 0.0f;

	FRotator PreviousControlRotation = FRotator::ZeroRotator;
	bool bHasPreviousRotation = false;

	void UpdatePhysics(float DeltaTime);
	void ApplyToTargets();
};
