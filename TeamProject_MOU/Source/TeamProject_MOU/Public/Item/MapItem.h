#pragma once

#include "CoreMinimal.h"
#include "Base/ItemBase.h"
#include "MapItem.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UUserWidget;

UCLASS()
class TEAMPROJECT_MOU_API AMapItem : public AItemBase
{
	GENERATED_BODY()

public:
	AMapItem();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	// [MAP-013] HoldingPlayer 복제 등록 (클라 좌클릭 사용에 필요)
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

public:
#pragma region [CAP] Top-down 캡처 카메라
	// 지도에 부착된 하향 캡처 카메라 (소지 플레이어 위를 비춤)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Map|Capture")
	TObjectPtr<USceneCaptureComponent2D> CaptureCamera;

	// 캡처 카메라 높이 (소지 플레이어 위 cm)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Capture")
	float CaptureHeight = 3000.0f;

	// 직교 투영 폭 (한 번에 담는 가로 범위 cm)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Capture")
	float CaptureOrthoWidth = 4096.0f;

	// [MAP-009] RT 해상도 (인스턴스마다 런타임 생성 시 사용, 정사각 픽셀)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Capture")
	int32 RenderTargetSize = 1024;

	// 실시간 캡처 결과 렌더타깃 (BeginPlay에서 인스턴스별 생성 → 공유 애셋 아님).
	//   위젯(WBP_Map)은 이 필드를 Get해서 이미지 소스로 써야 한다(공유 애셋 직접참조 금지).
	UPROPERTY(BlueprintReadOnly, Category = "Map|Capture")
	TObjectPtr<UTextureRenderTarget2D> CaptureRT;
#pragma endregion

#pragma region [FOG] 지도별 누적 Fog 마스크
	// 이 지도의 밝힘 기록 (방문한 곳 회색으로 누적). BeginPlay에서 인스턴스별 생성.
	//   위젯은 이 필드를 Get해서 써야 함(공유 애셋 직접참조 금지).
	UPROPERTY(BlueprintReadOnly, Category = "Map|Fog")
	TObjectPtr<UTextureRenderTarget2D> FogMaskRT;

	// 현재 시야 마스크 (현재 위치만 밝힘, 매번 갱신). BeginPlay에서 인스턴스별 생성.
	//   위젯은 이 필드를 Get해서 써야 함.
	UPROPERTY(BlueprintReadOnly, Category = "Map|Fog")
	TObjectPtr<UTextureRenderTarget2D> FogRevealRT;

	// 방문한 곳 회색 밝기 (현재 위치는 항상 1.0)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Fog")
	float VisitedIntensity = 0.5f;

	// 마스크에 밝힘 원을 그리는 머티리얼 (에디터 지정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Fog")
	TObjectPtr<UMaterialInterface> FogBrushMaterial;

	// 소지 플레이어 주변 밝힘 반경 (cm)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Fog")
	float RevealRadius = 800.0f;

	// Fog 마스크 갱신 주기 (초). Tick 대신 타이머로 갱신
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Fog")
	float FogUpdateInterval = 0.15f;

	// 맵 월드 원점(좌하단 XY) — 좌표→UV 변환 기준
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Fog")
	FVector2D MapWorldOrigin = FVector2D::ZeroVector;

	// 맵 전체 크기 (cm) — 좌표→UV 변환 기준
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Fog")
	FVector2D MapWorldSize = FVector2D(20000.0f, 20000.0f);
#pragma endregion

#pragma region [MAP] 소지/갱신/토글
	// 이 지도를 현재 소지 중인 플레이어 (PickUp/OnEquipped 기준으로 세팅).
	//   서버에서 세팅되므로 클라의 좌클릭 사용(OnUse)에서도 쓰려면 복제 필수. [MAP-013]
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Map|State")
	TObjectPtr<AActor> HoldingPlayer;

	// 지도 오버레이 위젯 클래스 (에디터 지정, WBP)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|UI")
	TSubclassOf<UUserWidget> MapWidgetClass;

	// [MAP-001] 좌클릭: 전체 지도 오버레이 켜기/끄기 토글
	virtual void OnUse_Implementation() override;

	// [MAP-002] 손에 들었을 때: 소지자 세팅 + Fog 갱신 타이머 시작
	virtual void OnEquipped_Implementation(AActor* Equipper) override;

	// [MAP-003] 손에서 해제될 때: 소지자 해제 + 타이머 정지
	virtual void OnUnequipped_Implementation(AActor* Equipper) override;

	// [MAP-011] 바닥에서 처음 집는 경로(PickUp)에는 OnEquipped가 안 불린다.
	//   그래서 여기서 HoldingPlayer를 직접 세팅해야 좌클릭 사용(OnUse)이 동작한다.
	//   (참고: Radio.cpp가 같은 이유로 PickUp을 오버라이드함)
	virtual void PickUp_Implementation(AActor* Picker) override;

	// [MAP-012] 놓기/던지기: 소지자 해제 (HoldingPlayer null)
	virtual void Drop_Implementation(FVector DropLocation, AActor* Dropper) override;
	virtual void Throw_Implementation(FVector ThrowVelocity, AActor* Thrower) override;

	// [MAP-004] 월드 좌표 → 지도 UV(0~1). 위젯 마커용
	UFUNCTION(BlueprintCallable, Category = "Map")
	FVector2D WorldToMapUV(const FVector& WorldLocation) const;

	// [MAP-008] 레벨 이동/재시작 시 Fog·지형 초기화 (RT 클리어 + 지형 재캡처).
	//   일반 레벨 이동(액터 재생성)이면 BeginPlay가 자동 초기화하므로 불필요.
	//   Seamless Travel 등 아이템이 유지되는 경우 BP에서 이 함수를 호출한다.
	UFUNCTION(BlueprintCallable, Category = "Map")
	void ResetMapForNewLevel();
#pragma endregion

private:
	// [MAP-014] 부착 시 스케일·위치가 튀지 않도록, 시작 시점의 메시 스케일을 기억했다가 복원.
	//   SnapToTargetNotIncludingScale 부착이 RelativeScale을 재계산하고,
	//   CarryingComponent 위치보정이 캡처카메라까지 박스에 넣어 지도를 밀어내는 것을 되돌린다.
	FVector InitialMeshScale = FVector(1.0f);
	void RestoreMeshScale();

	// 손(캐릭터)에 붙어 있는지 (바닥에 놓인 지도 위치는 건드리지 않기 위함)
	bool IsAttachedToPlayer() const;

	// [MAP-007] 이 지도 인스턴스 전용 렌더타깃 3개를 런타임 생성 (공유 애셋 깜빡임 방지)
	void CreatePerInstanceRenderTargets();

	// [MAP-005] 레벨의 MapBounds 태그 볼륨을 찾아 Origin/Size 자동 설정
	void ResolveMapBoundsFromVolume();

	// [MAP-006] 레벨 로딩 시 지형을 1회만 캡처 (이후 고정)
	void CaptureMapOnce();

	// [CAP-001] 캡처 카메라를 소지 플레이어 위로 이동/정렬
	void UpdateCaptureTransform();

	// [FOG-001] 소지 플레이어 위치를 Fog 마스크에 누적으로 밝힘
	void UpdateFogMask();

	// 지도 오버레이 위젯 인스턴스 (토글 상태 추적)
	UPROPERTY()
	TObjectPtr<UUserWidget> MapWidgetInstance;

	// Fog 브러시 동적 머티리얼 인스턴스
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FogBrushMID;

	// Fog 주기 갱신 타이머 핸들
	FTimerHandle FogUpdateTimerHandle;

	// 지형 1회 캡처 완료 여부 (중복 캡처 방지)
	bool bMapCaptured = false;

	// 지형 캡처 지연 타이머 (레벨 로드 대기용)
	FTimerHandle CaptureDelayTimerHandle;

	// 오버레이 열림 상태
	bool bMapOpen = false;
};
