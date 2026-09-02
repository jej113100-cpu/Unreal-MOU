#include "Item/MapItem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

AMapItem::AMapItem()
{
	PrimaryActorTick.bCanEverTick = false;

	// 지도에 부착되는 top-down 캡처 카메라 (직교 투영, 아래를 향함)
	CaptureCamera = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("CaptureCamera"));
	CaptureCamera->SetupAttachment(RootComponent);
	CaptureCamera->ProjectionType = ECameraProjectionMode::Orthographic;
	CaptureCamera->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
	// 상시 캡처는 부담이 크므로 필요 시점에만 캡처하도록 기본 비활성화
	CaptureCamera->bCaptureEveryFrame = false;
	CaptureCamera->bCaptureOnMovement = false;

	// 캐릭터/적(스켈레탈 메시)을 지도 캡처에서 제외 — 지형만 보이게
	CaptureCamera->ShowFlags.SetSkeletalMeshes(false);

	// 지도 캡처에서 그림자 제거 (평면적인 지형만)
	CaptureCamera->ShowFlags.SetDynamicShadows(false);
}

void AMapItem::BeginPlay()
{
	Super::BeginPlay();

	// [MAP-014] 시작 시점의 메시 스케일(=BP에서 지정한 값)을 기억. 부착으로 튄 뒤 이 값으로 복원.
	if (MeshComponent)
	{
		InitialMeshScale = MeshComponent->GetRelativeScale3D();
	}

	// [MAP-007] 이 지도 인스턴스 전용 렌더타깃 3개 생성 (공유 애셋 대신 → 2인 깜빡임 해결)
	CreatePerInstanceRenderTargets();

	// 레벨 경계 볼륨(MapBounds 태그)에서 지도 범위 자동 설정
	ResolveMapBoundsFromVolume();

	// 캡처 카메라 초기 세팅 (직교 폭 / 인스턴스 렌더타깃 연결)
	if (CaptureCamera)
	{
		CaptureCamera->OrthoWidth = CaptureOrthoWidth;
		CaptureCamera->TextureTarget = CaptureRT;
	}

	// Fog 브러시 동적 머티리얼 인스턴스 생성 (밝힘 원을 그릴 때 사용)
	if (FogBrushMaterial)
	{
		FogBrushMID = UMaterialInstanceDynamic::Create(FogBrushMaterial, this);
	}

	if (UWorld* World = GetWorld())
	{
		// 지형이 로드될 시간을 준 뒤 1회 캡처 (World Partition 스트리밍 대비)
		World->GetTimerManager().SetTimer(
			CaptureDelayTimerHandle, this, &AMapItem::CaptureMapOnce,
			0.5f, false);   // false = 반복 안 함, 0.5초 후 딱 1번

		// [MAP-002] Fog 갱신은 손에 안 들어도(바닥에서도) 항상 가동
		World->GetTimerManager().SetTimer(
			FogUpdateTimerHandle, this, &AMapItem::UpdateFogMask,
			FogUpdateInterval, true);
	}
}

// [MAP-013] HoldingPlayer 복제 등록 (서버에서 세팅 → 클라 좌클릭 사용에 필요)
void AMapItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMapItem, HoldingPlayer);
}

void AMapItem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 타이머 정리 (댕글링 방지)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FogUpdateTimerHandle);
		World->GetTimerManager().ClearTimer(CaptureDelayTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

// [MAP-001] 좌클릭: 전체 지도 오버레이 켜기/끄기 토글
void AMapItem::OnUse_Implementation()
{
	// 위젯 클래스 미지정 시 아무것도 하지 않음
	if (!MapWidgetClass)
	{
		return;
	}

	if (bMapOpen)
	{
		// 열려 있으면 닫기
		if (MapWidgetInstance)
		{
			MapWidgetInstance->RemoveFromParent();
			MapWidgetInstance = nullptr;
		}
		bMapOpen = false;
	}
	else
	{
		// 닫혀 있으면 소지 플레이어의 컨트롤러로 위젯 생성 후 표시
		APawn* OwnerPawn = Cast<APawn>(HoldingPlayer);
		APlayerController* PC = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
		if (PC)
		{
			MapWidgetInstance = CreateWidget<UUserWidget>(PC, MapWidgetClass);
			if (MapWidgetInstance)
			{
				// [MAP-010] 위젯에 이 지도 인스턴스를 전달 (인스턴스 RT를 참조하게 함, 검은 화면 방지)
				// WBP_Map에 입력 파라미터가 AMapItem* 1개인 커스텀 이벤트 "SetMapItem"을 구현해야 함
				if (UFunction* SetMapFunc = MapWidgetInstance->FindFunction(TEXT("SetMapItem")))
				{
					struct { AMapItem* Map; } Params{ this };
					MapWidgetInstance->ProcessEvent(SetMapFunc, &Params);
				}

				MapWidgetInstance->AddToViewport();
				bMapOpen = true;
			}
		}
	}
}

// [MAP-002] 손에 들었을 때: 소지자 세팅 (위젯 토글용).
//   Fog 갱신은 BeginPlay부터 항상 돌므로 여기서 타이머는 안 건드림.
void AMapItem::OnEquipped_Implementation(AActor* Equipper)
{
	Super::OnEquipped_Implementation(Equipper);

	HoldingPlayer = Equipper;

	// [MAP-014] 인벤토리에서 꺼내 손에 붙일 때도 스케일 튐 복원
	RestoreMeshScale();
}

// [MAP-003] 손에서 해제될 때: 소지자 해제. Fog 타이머는 계속 유지(바닥에서도 밝힘).
void AMapItem::OnUnequipped_Implementation(AActor* Equipper)
{
	HoldingPlayer = nullptr;

	Super::OnUnequipped_Implementation(Equipper);
}

// [MAP-011] 바닥에서 처음 집을 때(PickUp): OnEquipped가 안 불리므로 소지자를 여기서 세팅.
//   이게 없으면 주운 지도는 손에 들려 있어도 좌클릭 사용(OnUse)이 HoldingPlayer=null이라 실패한다.
void AMapItem::PickUp_Implementation(AActor* Picker)
{
	Super::PickUp_Implementation(Picker);

	// PickUp은 서버에서 실행되지만 HoldingPlayer는 위젯 생성(로컬)에도 쓰이므로 모두 세팅
	HoldingPlayer = Picker;

	// [MAP-014] 바닥에서 집어 손 소켓에 붙일 때 스케일이 튀는 것을 시작값으로 복원
	RestoreMeshScale();
}

// [MAP-012] 놓기: 소지자 해제
void AMapItem::Drop_Implementation(FVector DropLocation, AActor* Dropper)
{
	HoldingPlayer = nullptr;
	Super::Drop_Implementation(DropLocation, Dropper);
}

// [MAP-012] 던지기: 소지자 해제
void AMapItem::Throw_Implementation(FVector ThrowVelocity, AActor* Thrower)
{
	HoldingPlayer = nullptr;
	Super::Throw_Implementation(ThrowVelocity, Thrower);
}

// [MAP-004] 월드 좌표 → 지도 UV(0~1). 위젯 마커용
FVector2D AMapItem::WorldToMapUV(const FVector& WorldLocation) const
{
	// 하향 SceneCapture 이미지 축에 맞춰 정렬:
	// U(가로) = 월드 Y, V(세로) = 1 - 월드 X (위가 +X)
	const float U = (MapWorldSize.Y != 0.0f)
		? (WorldLocation.Y - MapWorldOrigin.Y) / MapWorldSize.Y : 0.0f;
	const float V = (MapWorldSize.X != 0.0f)
		? 1.0f - (WorldLocation.X - MapWorldOrigin.X) / MapWorldSize.X : 0.0f;

	return FVector2D(FMath::Clamp(U, 0.0f, 1.0f), FMath::Clamp(V, 0.0f, 1.0f));
}

// [MAP-005] 레벨의 MapBounds 태그 볼륨을 찾아 Origin/Size 자동 설정
void AMapItem::ResolveMapBoundsFromVolume()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// "MapBounds" 태그가 붙은 액터를 검색
	TArray<AActor*> BoundsActors;
	UGameplayStatics::GetAllActorsWithTag(World, TEXT("MapBounds"), BoundsActors);
	if (BoundsActors.Num() == 0)
	{
		// 볼륨이 없으면 에디터에서 수동 지정한 값을 그대로 사용 (폴백)
		return;
	}

	// 첫 번째 볼륨의 월드 경계 박스를 읽음
	FVector Origin, BoxExtent;
	BoundsActors[0]->GetActorBounds(false, Origin, BoxExtent);

	// 좌하단(XY) = 중심 - 절반크기, 전체크기 = 절반크기 * 2
	MapWorldOrigin = FVector2D(Origin.X - BoxExtent.X, Origin.Y - BoxExtent.Y);
	MapWorldSize = FVector2D(BoxExtent.X * 2.0f, BoxExtent.Y * 2.0f);

	// 캡처가 맵 전체를 담도록 직교 폭을 맵 크기에 맞춤 (가로 기준)
	CaptureOrthoWidth = MapWorldSize.X;
	if (CaptureCamera)
	{
		CaptureCamera->OrthoWidth = CaptureOrthoWidth;
	}
}

// [MAP-006] 레벨 로딩 시 지형을 1회만 캡처 (이후 고정)
void AMapItem::CaptureMapOnce()
{
	// 이미 캡처했으면 다시 안 함 (확실한 1회 보장)
	if (bMapCaptured || !CaptureCamera)
	{
		return;
	}

	// 맵 중앙 상공에 카메라 정렬 후 지형을 1회 캡처
	UpdateCaptureTransform();
	CaptureCamera->TextureTarget = CaptureRT;
	CaptureCamera->CaptureScene();

	bMapCaptured = true;
}

// [CAP-001] 캡처 카메라를 맵 중앙 상공에 고정 (전체 지도 방식)
void AMapItem::UpdateCaptureTransform()
{
	if (!CaptureCamera)
	{
		return;
	}

	// 맵 중앙 XY = 원점 + 크기/2
	const float CenterX = MapWorldOrigin.X + MapWorldSize.X * 0.5f;
	const float CenterY = MapWorldOrigin.Y + MapWorldSize.Y * 0.5f;

	// 맵 중앙 상공에서 수직 하향 (플레이어 위치와 무관하게 고정)
	const FVector CamLoc(CenterX, CenterY, CaptureHeight);
	CaptureCamera->SetWorldLocation(CamLoc);
	CaptureCamera->SetWorldRotation(FRotator(-90.0f, 0.0f, 0.0f));
	// 메시(루트) 스케일을 물려받지 않도록 캡처는 항상 1배로 고정 (아이템 크기와 캡처 분리)
	CaptureCamera->SetWorldScale3D(FVector(1.0f));
}

// [FOG-001] 현재 위치=밝음(1.0), 지나간 곳=회색(0.5) 누적
//   시야 기준: 손에 들면 플레이어 몸 중심(회전 튐 방지), 바닥이면 지도 자신 위치.
void AMapItem::UpdateFogMask()
{
	// (지형 캡처는 CaptureMapOnce에서 1회만 하므로 여기선 캡처하지 않음)

	if (!FogBrushMID)
	{
		return;
	}

	// 시야 기준 위치 → UV, 반경 계산 (두 마스크 공통)
	const FVector RevealLoc = HoldingPlayer ? HoldingPlayer->GetActorLocation() : GetActorLocation();
	const FVector2D MapUV = WorldToMapUV(RevealLoc);
	const float RadiusUV = (MapWorldSize.X != 0.0f) ? (RevealRadius / MapWorldSize.X) : 0.0f;
	FogBrushMID->SetVectorParameterValue(
		TEXT("BrushCenter"), FLinearColor(MapUV.X, MapUV.Y, 0.0f, 0.0f));
	FogBrushMID->SetScalarParameterValue(TEXT("BrushRadius"), RadiusUV);

	// 1) 방문 마스크: 회색(VisitedIntensity)으로 누적 (지우지 않음)
	if (FogMaskRT)
	{
		FogBrushMID->SetScalarParameterValue(TEXT("BrushIntensity"), VisitedIntensity);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, FogMaskRT, FogBrushMID);
	}

	// 2) 현재 마스크: 매번 지우고 현재 위치만 밝게(1.0)
	if (FogRevealRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FogRevealRT, FLinearColor::Black);
		FogBrushMID->SetScalarParameterValue(TEXT("BrushIntensity"), 1.0f);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, FogRevealRT, FogBrushMID);
	}
}

// [MAP-014] 부착으로 튄 메시 스케일·위치를 손 소켓 기준으로 복원.
//   ① SnapToTargetNotIncludingScale 부착이 RelativeScale을 재계산해 크기가 튀는 것 복원.
//   ② CarryingComponent의 GetComponentsBoundingBox 위치보정이 캡처카메라(맵 상공 3000)까지
//      박스에 포함해 지도를 손 밖 공중으로 밀어내는 것을, RelativeLocation=0으로 되돌려 복원.
//   부착·보정이 같은 프레임 뒤에 일어날 수 있어, 즉시 + 다음 틱에 한 번 더 복원한다.
void AMapItem::RestoreMeshScale()
{
	// PickUp 시점엔 아직 부착 전이다(부착·위치보정은 CarryingComponent가 PickUp 이후에 함).
	// 따라서 즉시 복원은 부착돼 있을 때만 하고, 다음 틱 복원은 부착 여부와 무관하게 반드시 예약한다.
	if (IsAttachedToPlayer())
	{
		SetActorRelativeLocation(FVector::ZeroVector);
		if (MeshComponent)
		{
			MeshComponent->SetRelativeScale3D(InitialMeshScale);
		}
	}

	// 다음 틱: 이때는 부착·위치보정이 끝난 상태 → 손 소켓 원점으로 확실히 복원
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick([this]()
		{
			if (IsValid(this) && IsAttachedToPlayer())
			{
				SetActorRelativeLocation(FVector::ZeroVector);
				if (MeshComponent)
				{
					MeshComponent->SetRelativeScale3D(InitialMeshScale);
				}
			}
		});
	}
}

// 손(캐릭터 메시)에 붙어 있는지 확인 (바닥에 놓인 지도의 위치를 건드리지 않기 위함)
bool AMapItem::IsAttachedToPlayer() const
{
	return GetAttachParentActor() != nullptr;
}

// [MAP-007] 이 지도 인스턴스 전용 렌더타깃 3개 생성 (공유 애셋 대신 → 2인 깜빡임 해결)
//   ⚠️ 위젯(WBP_Map)은 이 인스턴스의 CaptureRT/FogMaskRT/FogRevealRT 필드를 Get해서 써야 한다.
//      공유 RT 애셋을 직접 참조하면 검은 화면이 된다.
void AMapItem::CreatePerInstanceRenderTargets()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const int32 Size = FMath::Max(64, RenderTargetSize);

	// 지형 캡처: 컬러(RGBA8), Fog 마스크: 단일 채널(R8)이면 충분
	CaptureRT   = UKismetRenderingLibrary::CreateRenderTarget2D(World, Size, Size, RTF_RGBA8);
	FogMaskRT   = UKismetRenderingLibrary::CreateRenderTarget2D(World, Size, Size, RTF_R8);
	FogRevealRT = UKismetRenderingLibrary::CreateRenderTarget2D(World, Size, Size, RTF_R8);

	// 마스크는 검정(안 밝힘)에서 시작
	if (FogMaskRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FogMaskRT, FLinearColor::Black);
	}
	if (FogRevealRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FogRevealRT, FLinearColor::Black);
	}

	// 캡처 카메라 출력 대상을 이 인스턴스 RT로 연결
	if (CaptureCamera)
	{
		CaptureCamera->TextureTarget = CaptureRT;
	}
}

// [MAP-008] 레벨 이동/재시작 시 Fog·지형 초기화 (RT 클리어 + 지형 재캡처).
//   일반 레벨 이동(액터 재생성)이면 BeginPlay가 자동 초기화하므로 불필요.
//   Seamless Travel 등 아이템이 유지되는 경우 BP에서 이 함수를 호출한다.
void AMapItem::ResetMapForNewLevel()
{
	// Fog 기록 리셋 (탐험 처음부터)
	if (FogMaskRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FogMaskRT, FLinearColor::Black);
	}
	if (FogRevealRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FogRevealRT, FLinearColor::Black);
	}

	// 새 레벨 경계 재인식
	ResolveMapBoundsFromVolume();

	// 지형을 다시 1회 캡처하도록 예약 (새 레벨 지형 로드 대기)
	bMapCaptured = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			CaptureDelayTimerHandle, this, &AMapItem::CaptureMapOnce,
			0.5f, false);
	}
}
