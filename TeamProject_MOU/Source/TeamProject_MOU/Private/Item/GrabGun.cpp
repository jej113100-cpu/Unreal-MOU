#include "Item/GrabGun.h"
#include "Base/CharacterBase.h"
#include "Components/GrabFollowComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h" // [DEBUG-GRAB] 확인용 임시

// 부품 메시들이 들어있는 폴더 (재임포트 위치)
static const TCHAR* GPartMeshDir =
	TEXT("/Game/04_JJO/Asset/StaticMeshes/GrabGun/StaticMeshes/");

// 재임포트 시 Interchange가 부품 이름을 버리고 mesh 인덱스로만 저장했다.
// (파일명: extending-arm-toy-gun2222_mesh_<idx>) 그래서 부품 이름 → mesh 인덱스 매핑이 필요하다.
// 매핑은 GLB(extending-arm-toy-gun2222.glb)의 node.mesh 참조 순서에서 추출한 실측값이다.
static const TCHAR* GMeshAssetPrefix = TEXT("extending-arm-toy-gun2222_mesh_");

static int32 PartNameToMeshIndex(const FString& PartName)
{
	static const TMap<FString, int32> Map = {
		{ TEXT("body_shell"), 0 },
		{ TEXT("side_plate_left"), 1 },
		{ TEXT("vent_slat_l0"), 2 }, { TEXT("vent_slat_l1"), 3 }, { TEXT("vent_slat_l2"), 4 },
		{ TEXT("vent_slat_l3"), 5 }, { TEXT("vent_slat_l4"), 6 },
		{ TEXT("screw_l0"), 7 }, { TEXT("screw_l1"), 8 }, { TEXT("screw_l2"), 9 },
		{ TEXT("screw_l3"), 10 }, { TEXT("screw_l4"), 11 },
		{ TEXT("side_plate_right"), 12 },
		{ TEXT("vent_slat_r0"), 13 }, { TEXT("vent_slat_r1"), 14 }, { TEXT("vent_slat_r2"), 15 },
		{ TEXT("vent_slat_r3"), 16 }, { TEXT("vent_slat_r4"), 17 },
		{ TEXT("screw_r0"), 18 }, { TEXT("screw_r1"), 19 }, { TEXT("screw_r2"), 20 },
		{ TEXT("screw_r3"), 21 }, { TEXT("screw_r4"), 22 },
		{ TEXT("trigger_guard"), 23 }, { TEXT("trigger"), 24 }, { TEXT("trigger_pivot_pin"), 25 },
		{ TEXT("muzzle_anchor"), 26 },
		{ TEXT("bar_up_0"), 27 }, { TEXT("bar_dn_0"), 28 }, { TEXT("pin_top_0"), 29 }, { TEXT("pin_bottom_0"), 30 }, { TEXT("pin_center_0"), 31 },
		{ TEXT("bar_up_1"), 32 }, { TEXT("bar_dn_1"), 33 }, { TEXT("pin_top_1"), 34 }, { TEXT("pin_bottom_1"), 35 }, { TEXT("pin_center_1"), 36 },
		{ TEXT("bar_up_2"), 37 }, { TEXT("bar_dn_2"), 38 }, { TEXT("pin_top_2"), 39 }, { TEXT("pin_bottom_2"), 40 }, { TEXT("pin_center_2"), 41 },
		{ TEXT("bar_up_3"), 42 }, { TEXT("bar_dn_3"), 43 }, { TEXT("pin_top_3"), 44 }, { TEXT("pin_bottom_3"), 45 }, { TEXT("pin_center_3"), 46 },
		{ TEXT("bar_up_4"), 47 }, { TEXT("bar_dn_4"), 48 }, { TEXT("pin_top_4"), 49 }, { TEXT("pin_bottom_4"), 50 }, { TEXT("pin_center_4"), 51 },
		{ TEXT("bar_up_5"), 52 }, { TEXT("bar_dn_5"), 53 }, { TEXT("pin_top_5"), 54 }, { TEXT("pin_bottom_5"), 55 }, { TEXT("pin_center_5"), 56 },
		{ TEXT("bar_up_6"), 57 }, { TEXT("bar_dn_6"), 58 }, { TEXT("pin_top_6"), 59 }, { TEXT("pin_bottom_6"), 60 }, { TEXT("pin_center_6"), 61 },
		{ TEXT("bar_up_7"), 62 }, { TEXT("bar_dn_7"), 63 }, { TEXT("pin_top_7"), 64 }, { TEXT("pin_bottom_7"), 65 }, { TEXT("pin_center_7"), 66 },
		{ TEXT("bar_up_8"), 67 }, { TEXT("bar_dn_8"), 68 }, { TEXT("pin_top_8"), 69 }, { TEXT("pin_bottom_8"), 70 }, { TEXT("pin_center_8"), 71 },
		{ TEXT("yoke_spine"), 72 },
		{ TEXT("jaw_blade_top"), 73 }, { TEXT("jaw_pad_top"), 74 }, { TEXT("jaw_pin_top"), 75 },
		{ TEXT("jaw_blade_bottom"), 76 }, { TEXT("jaw_pad_bottom"), 77 }, { TEXT("jaw_pin_bottom"), 78 },
	};
	const int32* Found = Map.Find(PartName);
	return Found ? *Found : INDEX_NONE;
}

AGrabGun::AGrabGun()
{
	PrimaryActorTick.bCanEverTick = true;

	MaxDurability = 100.0f;
	CurrentDurability = MaxDurability;

	// 루트(MeshComponent) = body_shell 총몸. ItemBase의 물리/줍기/충돌이 이 메시 기준이라 여기 둔다.
	// 링크·집게가 전부 이 자식이라, 루트를 돌리면 총 전체가 같이 돌아간다.
	if (MeshComponent)
	{
		if (UStaticMesh* Body = LoadPartMesh(TEXT("body_shell")))
		{
			MeshComponent->SetStaticMesh(Body);
		}
	}

	// 링크가 매달리는 루트 + 총구 지점 (GLB 실측 linkage_root = 7.0, 1.1, 0)
	LinkageRoot = CreateDefaultSubobject<USceneComponent>(TEXT("LinkageRoot"));
	LinkageRoot->SetupAttachment(MeshComponent);
	LinkageRoot->SetRelativeLocation(LinkageRootOffset);

	MuzzlePoint = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzlePoint"));
	MuzzlePoint->SetupAttachment(MeshComponent);

	// 고정 부품(집게·트리거·콜라이더)만 생성자에서 조립. 반복 셀은 RebuildCells가 담당.
	BuildLinkageComponents();
}

// BP에서 Linkage 설정값(CellCount/각도 등)을 바꿀 때마다 셀을 재생성하고 포즈를 다시 그린다.
void AGrabGun::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildCells();                       // CellCount 반영 (바뀐 경우에만 재생성)
	UpdateLinkagePose(CurrentExtendAlpha); // 접힘 포즈 적용
}

void AGrabGun::BeginPlay()
{
	Super::BeginPlay();

	// 셀 보장 (CellCount만큼) + 시작은 완전히 접힌 포즈
	RebuildCells();
	CurrentExtendAlpha = 0.0f;
	TargetExtendAlpha = 0.0f;
	UpdateLinkagePose(0.0f);

	// 집게 콜라이더 오버랩 콜백 바인딩
	if (JawGrabCollider)
	{
		JawGrabCollider->OnComponentBeginOverlap.AddDynamic(this, &AGrabGun::OnJawOverlap);
	}
}

void AGrabGun::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGrabGun, GrabbedTarget);
	DOREPLIFETIME(AGrabGun, TargetExtendAlpha);
	DOREPLIFETIME(AGrabGun, bOwnerInputLocked);
}

// =============================================================================
// [GRAB] 부품 메시 로드 / 축 헬퍼
// =============================================================================

UStaticMesh* AGrabGun::LoadPartMesh(const FString& PartName) const
{
	// 부품 이름 → mesh 인덱스 → 실제 에셋명(extending-arm-toy-gun2222_mesh_<idx>)
	const int32 Idx = PartNameToMeshIndex(PartName);
	if (Idx == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRAB] Unknown part name: %s"), *PartName);
		return nullptr;
	}
	const FString AssetName = FString(GMeshAssetPrefix) + FString::FromInt(Idx);
	// 예: /Game/.../GrabGun/StaticMeshes/extending-arm-toy-gun2222_mesh_27.extending-arm-toy-gun2222_mesh_27
	const FString Path = FString(GPartMeshDir) + AssetName + TEXT(".") + AssetName;
	UStaticMesh* Mesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *Path));
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRAB] Failed to load mesh for %s at %s"), *PartName, *Path);
	}
	return Mesh;
}

FVector AGrabGun::MakeLocal(float X, float Y, float Z) const
{
	// GLB matrix 실측값을 그대로 UE 로컬로 사용한다. (Interchange가 축 변환 처리)
	return FVector(X, Y, Z);
}

FRotator AGrabGun::MakeYawRot(float DegZ) const
{
	// GLB 노드 회전은 Z축(Yaw) 기준. 가위 링크가 XY 평면에서 접힌다.
	return FRotator(0.0f, DegZ, 0.0f);
}

// =============================================================================
// [GRAB-012] 팬터그래프 부품 계층 스폰
// =============================================================================

void AGrabGun::BuildLinkageComponents()
{
	// 헬퍼: SceneComponent(빈 피벗) 생성
	auto MakeScene = [this](const FName& Name, USceneComponent* Parent, const FVector& Loc, const FRotator& Rot) -> USceneComponent*
	{
		USceneComponent* Comp = CreateDefaultSubobject<USceneComponent>(Name);
		Comp->SetupAttachment(Parent);
		Comp->SetRelativeLocation(Loc);
		Comp->SetRelativeRotation(Rot);
		return Comp;
	};

	// 헬퍼: StaticMesh 부품 생성 (PartName으로 메시 로드 + 부착)
	auto MakeMesh = [this](const FName& CompName, const FString& PartName, USceneComponent* Parent, const FVector& Loc, const FRotator& Rot) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Comp = CreateDefaultSubobject<UStaticMeshComponent>(CompName);
		Comp->SetupAttachment(Parent);
		Comp->SetRelativeLocation(Loc);
		Comp->SetRelativeRotation(Rot);
		if (UStaticMesh* M = LoadPartMesh(PartName))
		{
			Comp->SetStaticMesh(M);
		}
		// 링크 부품은 순수 비주얼. 충돌/물리 끔 (루트 body_shell만 물리).
		Comp->SetSimulatePhysics(false);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Comp;
	};

	// 반복 셀(cell/bar/pin)은 여기서 만들지 않고 RebuildCells()가 CellCount만큼 런타임 생성한다.
	// (BuildLinkageComponents는 생성자에서 한 번만 불려 고정 부품만 만든다)

	// end_yoke: 셀 개수가 바뀌어도 부모가 안 바뀌게 LinkageRoot 직속으로 둔다.
	// 위치는 UpdateLinkagePose에서 (CellCount * dx)로 마지막 셀 끝에 맞춘다.
	// GLB 계층: end_yoke > yoke_spine_holder(identity) > yoke_spine(mesh)
	EndYoke = MakeScene(TEXT("end_yoke"), LinkageRoot, FVector::ZeroVector, FRotator::ZeroRotator);
	USceneComponent* YokeHolder = MakeScene(TEXT("yoke_spine_holder"), EndYoke, FVector::ZeroVector, FRotator::ZeroRotator);
	YokeSpine = MakeMesh(TEXT("yoke_spine"), TEXT("yoke_spine"), YokeHolder, FVector::ZeroVector, FRotator::ZeroRotator);

	// 집게 끝 접촉 판정 콜라이더 (end_yoke 자식). 평소 꺼두고 펴질 때만 켠다.
	JawGrabCollider = CreateDefaultSubobject<USphereComponent>(TEXT("JawGrabCollider"));
	JawGrabCollider->SetupAttachment(EndYoke);
	JawGrabCollider->SetRelativeLocation(JawColliderOffset);
	JawGrabCollider->SetSphereRadius(JawColliderRadius);
	JawGrabCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	JawGrabCollider->SetGenerateOverlapEvents(false);
	JawGrabCollider->SetCollisionResponseToAllChannels(ECR_Overlap);

	// 집게턱 (GLB 실측 계층 그대로):
	//   jaw_pivot_* (0.6, ±2.62, 회전 ±10.03°)  ← 벌림 회전은 여기(Update에서 갱신)
	//     jaw_reach_* (identity)
	//       jaw_blade_* (2.766, ∓0.965)
	//       jaw_pad_*   (4.600, ∓2.150, 회전 ±35.52°)
	//     jaw_pin_* (identity)
	// TOP
	JawPivotTop = MakeScene(TEXT("jaw_pivot_top"), EndYoke,
		MakeLocal(0.6f, +2.62f, 0.0f), MakeYawRot(+10.03f));
	{
		USceneComponent* Reach = MakeScene(TEXT("jaw_reach_top"), JawPivotTop, FVector::ZeroVector, FRotator::ZeroRotator);
		JawBladeTop = MakeMesh(TEXT("jaw_blade_top"), TEXT("jaw_blade_top"), Reach, FVector::ZeroVector, FRotator::ZeroRotator);
		JawPadTop   = MakeMesh(TEXT("jaw_pad_top"),   TEXT("jaw_pad_top"),   Reach, FVector::ZeroVector, FRotator::ZeroRotator);
		MakeMesh(TEXT("jaw_pin_top"),   TEXT("jaw_pin_top"),   JawPivotTop, FVector::ZeroVector, FRotator::ZeroRotator);
	}
	// BOTTOM (Y·회전 부호 반대)
	JawPivotBottom = MakeScene(TEXT("jaw_pivot_bottom"), EndYoke,
		MakeLocal(0.6f, -2.62f, 0.0f), MakeYawRot(-10.03f));
	{
		USceneComponent* Reach = MakeScene(TEXT("jaw_reach_bottom"), JawPivotBottom, FVector::ZeroVector, FRotator::ZeroRotator);
		JawBladeBottom = MakeMesh(TEXT("jaw_blade_bottom"), TEXT("jaw_blade_bottom"), Reach, FVector::ZeroVector, FRotator::ZeroRotator);
		JawPadBottom   = MakeMesh(TEXT("jaw_pad_bottom"),   TEXT("jaw_pad_bottom"),   Reach, FVector::ZeroVector, FRotator::ZeroRotator);
		MakeMesh(TEXT("jaw_pin_bottom"),   TEXT("jaw_pin_bottom"),   JawPivotBottom, FVector::ZeroVector, FRotator::ZeroRotator);
	}

	// 트리거 피벗 (body_shell=MeshComponent 직속). GLB 실측 trigger_pivot (-0.2, -0.8, 0).
	// trigger 메시 위치/회전은 Details(TriggerMeshOffset/Rot)로 조정. (Update에서 갱신)
	TriggerPivot = MakeScene(TEXT("trigger_pivot"), MeshComponent, MakeLocal(-0.2f, -0.8f, 0.0f), FRotator::ZeroRotator);
	TriggerMesh = MakeMesh(TEXT("trigger"), TEXT("trigger"), TriggerPivot, FVector::ZeroVector, FRotator::ZeroRotator);
}

// =============================================================================
// [GRAB-012B] 셀(cell/bar/pin) 런타임 재생성 - CellCount만큼
// =============================================================================

void AGrabGun::RebuildCells()
{
	if (!LinkageRoot)
	{
		return;
	}
	// 이미 같은 개수면 스킵 (매 OnConstruction마다 재생성 낭비 방지)
	if (BuiltCellCount == CellCount)
	{
		return;
	}

	// 기존 셀 컴포넌트 전부 제거
	auto DestroyArray = [](auto& Arr)
	{
		for (auto& C : Arr)
		{
			if (C) { C->DestroyComponent(); }
		}
		Arr.Reset();
	};
	DestroyArray(CellPivots);
	DestroyArray(PivotUp);
	DestroyArray(PivotDn);
	DestroyArray(PinTop);
	DestroyArray(PinBottom);
	DestroyArray(PinCenter);

	// 런타임 SceneComponent 생성 헬퍼 (NewObject + RegisterComponent)
	auto NewScene = [this](const FName& Name, USceneComponent* Parent) -> USceneComponent*
	{
		USceneComponent* Comp = NewObject<USceneComponent>(this, USceneComponent::StaticClass(), Name);
		Comp->SetupAttachment(Parent);
		Comp->RegisterComponent();
		return Comp;
	};
	// 런타임 StaticMesh 생성 헬퍼
	auto NewMesh = [this](const FName& Name, const FString& PartName, USceneComponent* Parent, const FVector& Loc) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this, UStaticMeshComponent::StaticClass(), Name);
		Comp->SetupAttachment(Parent);
		Comp->RegisterComponent();
		Comp->SetRelativeLocation(Loc);
		if (UStaticMesh* M = LoadPartMesh(PartName))
		{
			Comp->SetStaticMesh(M);
		}
		Comp->SetSimulatePhysics(false);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Comp;
	};

	// 임포트된 메시는 0~8(9칸)뿐 → CellCount가 9를 넘으면 메시를 순환 재사용(m = i % 9).
	const int32 MeshCells = 9;
	// 셀은 앞 셀의 자식으로 중첩(cell_i = cell_{i-1}의 자식). 9칸일 때 정상이던 구조 유지.
	// UpdateLinkagePose가 cell_1..은 상대 dx, cell_0은 0으로 배치 → X자가 연속으로 이어진다.
	USceneComponent* PrevParent = LinkageRoot;
	for (int32 i = 0; i < CellCount; ++i)
	{
		const int32 m = i % MeshCells;

		USceneComponent* Cell = NewScene(*FString::Printf(TEXT("cell_%02d"), i), PrevParent);
		CellPivots.Add(Cell);

		USceneComponent* PUp = NewScene(*FString::Printf(TEXT("pivot_up_%d"), i), Cell);
		USceneComponent* PDn = NewScene(*FString::Printf(TEXT("pivot_dn_%d"), i), Cell);
		PivotUp.Add(PUp);
		PivotDn.Add(PDn);

		NewMesh(*FString::Printf(TEXT("bar_up_%d"), i), FString::Printf(TEXT("bar_up_%d"), m), PUp,
			MakeLocal(LinkHalfLength, 0.0f, +BarZOffset));
		NewMesh(*FString::Printf(TEXT("bar_dn_%d"), i), FString::Printf(TEXT("bar_dn_%d"), m), PDn,
			MakeLocal(LinkHalfLength, 0.0f, -BarZOffset));

		PinTop.Add(NewMesh(*FString::Printf(TEXT("pin_top_%d"), i), FString::Printf(TEXT("pin_top_%d"), m), Cell, FVector::ZeroVector));
		PinBottom.Add(NewMesh(*FString::Printf(TEXT("pin_bottom_%d"), i), FString::Printf(TEXT("pin_bottom_%d"), m), Cell, FVector::ZeroVector));
		PinCenter.Add(NewMesh(*FString::Printf(TEXT("pin_center_%d"), i), FString::Printf(TEXT("pin_center_%d"), m), Cell, FVector::ZeroVector));

		PrevParent = Cell; // 다음 셀은 이 셀의 자식 (중첩)
	}

	// end_yoke를 마지막 셀의 자식으로 다시 붙인다 (셀 개수가 바뀌었으니 부모 갱신).
	if (EndYoke && CellPivots.Num() > 0 && CellPivots.Last())
	{
		EndYoke->AttachToComponent(CellPivots.Last(), FAttachmentTransformRules::KeepRelativeTransform);
	}

	BuiltCellCount = CellCount;
}

// =============================================================================
// [GRAB-013] 링크 포즈 갱신 (삼각함수)
// =============================================================================

void AGrabGun::UpdateLinkagePose(float Alpha)
{
	Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

	// alpha: 0=접힘(FoldedAngleDeg, 큰 각) ~ 1=최대뻗음(ExtendedAngleDeg, 작은 각)
	// GLB 실측 관계: dx = 2*R*cos(a), hh = R*sin(a). (R=LinkHalfLength)
	const float aDeg = FMath::Lerp(FoldedAngleDeg, ExtendedAngleDeg, Alpha);
	const float aRad = FMath::DegreesToRadians(aDeg);
	const float R = LinkHalfLength;
	const float dx = 2.0f * R * FMath::Cos(aRad);   // cm, 셀 간 X 간격
	const float hh = R * FMath::Sin(aRad);          // cm, 핀 상하 반높이

	// 셀은 앞 셀의 자식(중첩)이므로 각 셀에 상대 dx를 준다 (누적되어 i*dx가 됨).
	//   cell_0은 0, cell_1..은 각각 dx. end_yoke도 마지막 셀 기준 상대 dx.
	for (int32 i = 1; i < CellPivots.Num(); ++i)
	{
		if (CellPivots[i])
		{
			CellPivots[i]->SetRelativeLocation(MakeLocal(dx, 0.0f, 0.0f));
		}
	}
	if (EndYoke)
	{
		EndYoke->SetRelativeLocation(MakeLocal(dx, 0.0f, 0.0f));
	}

	// 각 셀 내부: pivot_up/dn 위치+회전, 핀 위치 (GLB 실측 부호: pivot_up Y=-hh, 회전=+a)
	for (int32 i = 0; i < CellPivots.Num(); ++i)
	{
		if (PivotUp.IsValidIndex(i) && PivotUp[i])
		{
			PivotUp[i]->SetRelativeLocation(MakeLocal(0.0f, -hh, 0.0f));
			PivotUp[i]->SetRelativeRotation(MakeYawRot(+aDeg));
		}
		if (PivotDn.IsValidIndex(i) && PivotDn[i])
		{
			PivotDn[i]->SetRelativeLocation(MakeLocal(0.0f, +hh, 0.0f));
			PivotDn[i]->SetRelativeRotation(MakeYawRot(-aDeg));
		}
		if (PinTop.IsValidIndex(i) && PinTop[i])
		{
			PinTop[i]->SetRelativeLocation(MakeLocal(0.0f, +hh, 0.0f));
		}
		if (PinBottom.IsValidIndex(i) && PinBottom[i])
		{
			PinBottom[i]->SetRelativeLocation(MakeLocal(0.0f, -hh, 0.0f));
		}
		if (PinCenter.IsValidIndex(i) && PinCenter[i])
		{
			PinCenter[i]->SetRelativeLocation(MakeLocal(dx / 2.0f, 0.0f, 0.0f));
		}
	}

	// 집게턱: 기본각(JawBaseAngleDeg)에서 alpha만큼 더 벌어짐. top=+, bottom=-.
	const float JawDeg = JawBaseAngleDeg + JawOpenAngle * Alpha;
	if (JawPivotTop)
	{
		JawPivotTop->SetRelativeRotation(MakeYawRot(+JawDeg));
	}
	if (JawPivotBottom)
	{
		JawPivotBottom->SetRelativeRotation(MakeYawRot(-JawDeg));
	}

	// 집게 blade/pad 메시 위치·회전 (Details에서 조정 가능).
	// top=+Y, bottom=-Y. 회전은 top=그대로, bottom=Yaw만 반전(좌우 대칭).
	auto MirrorYaw = [](const FRotator& R) { return FRotator(R.Pitch, -R.Yaw, R.Roll); };
	if (JawBladeTop)
	{
		JawBladeTop->SetRelativeLocation(MakeLocal(JawBladeOffset.X, +JawBladeOffset.Y, JawBladeOffset.Z));
		JawBladeTop->SetRelativeRotation(JawBladeRot);
	}
	if (JawBladeBottom)
	{
		JawBladeBottom->SetRelativeLocation(MakeLocal(JawBladeOffset.X, -JawBladeOffset.Y, JawBladeOffset.Z));
		JawBladeBottom->SetRelativeRotation(MirrorYaw(JawBladeRot));
	}
	if (JawPadTop)
	{
		JawPadTop->SetRelativeLocation(MakeLocal(JawPadOffset.X, +JawPadOffset.Y, JawPadOffset.Z));
		JawPadTop->SetRelativeRotation(JawPadRot);
	}
	if (JawPadBottom)
	{
		JawPadBottom->SetRelativeLocation(MakeLocal(JawPadOffset.X, -JawPadOffset.Y, JawPadOffset.Z));
		JawPadBottom->SetRelativeRotation(MirrorYaw(JawPadRot));
	}
	// yoke_spine(집게 연결부 세로 바): 세로로 서 있으면 눕힌다.
	if (YokeSpine)
	{
		YokeSpine->SetRelativeRotation(YokeSpineRot);
	}
	// trigger(방아쇠) 메시 위치·회전 (Details에서 조정).
	if (TriggerMesh)
	{
		TriggerMesh->SetRelativeLocation(TriggerMeshOffset);
		TriggerMesh->SetRelativeRotation(TriggerMeshRot);
	}
	// 트리거 당김: t=alpha
	if (TriggerPivot)
	{
		TriggerPivot->SetRelativeRotation(MakeYawRot(TriggerPullAngle * Alpha));
	}
}

void AGrabGun::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 총 방향 보정: 손에 쥐면(물리 off) 소켓 스냅이 루트 회전을 덮으므로 매 Tick 다시 적용.
	// 바닥에 떨어진 상태(물리 on)에서는 건드리지 않는다(물리 회전과 충돌 방지).
	if (MeshComponent && !MeshOrientationFix.IsNearlyZero() && !MeshComponent->IsSimulatingPhysics())
	{
		MeshComponent->SetRelativeRotation(MeshOrientationFix);
	}

	// 목표치로 부드럽게 보간 (시각 표현이라 서버/클라 각자 로컬 계산)
	const bool bWasMoving = !FMath::IsNearlyEqual(CurrentExtendAlpha, TargetExtendAlpha, 0.001f);
	if (bWasMoving)
	{
		CurrentExtendAlpha = FMath::FInterpConstantTo(CurrentExtendAlpha, TargetExtendAlpha, DeltaTime, ExtendSpeed);
		UpdateLinkagePose(CurrentExtendAlpha);
	}

	// 시퀀스 완료 판정은 서버에서만 (상태 변경/detach는 서버 권한)
	if (HasAuthority())
	{
		// 당기는 중이었고 링크가 완전히 접혔으면(alpha≈0) → 대상 놓기 + 사용자 잠금해제
		if (bPulling && CurrentExtendAlpha <= 0.02f)
		{
			ReleaseTarget();
		}
		// 헛방(못 잡고 완전히 뻗기만 함): alpha=1 도달하면 자동으로 접기 시작
		else if (bGrabArmed && !GrabbedTarget && CurrentExtendAlpha >= 0.98f)
		{
			bGrabArmed = false;
			SetJawColliderActive(false);
			TargetExtendAlpha = 0.0f; // 다시 접힘
			bPulling = true;          // 접힘 완료 시 잠금해제 되도록 pulling 처리(대상 없음)
		}
	}
}

// =============================================================================
// [GRAB-001] 발사: 재발사 토글 (잡고 있으면 놓기, 아니면 잡기)
// =============================================================================

void AGrabGun::Fire()
{
	// Fire는 서버 권한에서 호출됨 (WeaponItemBase::TryFireOnServer 경로)
	// 이미 뻗는 중(bGrabArmed)/당기는 중(bPulling)이면 새 발사 무시 (시퀀스가 끝날 때까지).
	if (bGrabArmed || bPulling || GrabbedTarget)
	{
		return;
	}

	// 뻗기 시작: 링크가 쫙 펴지고, 집게 콜라이더 ON. 뻗는 동안 대상과 닿으면 잡는다.
	TargetExtendAlpha = 1.0f;
	bGrabArmed = true;
	SetJawColliderActive(true);

	// 사용자 이동+카메라 잠금 (아이템 사용~완전히 당겨질 때까지)
	SetOwnerInputLocked(true);

	// VFX: 총구 → 집게 끝 (연출)
	const FVector FxStart = MuzzlePoint ? MuzzlePoint->GetComponentLocation() : GetActorLocation();
	const FVector FxEnd = JawGrabCollider ? JawGrabCollider->GetComponentLocation() : FxStart;
	MulticastPlayFireEffect(FxStart, FxEnd, false);
}

// [GRAB-002B] 집게 콜라이더 오버랩: 펴짐 중(bGrabArmed) player/enemy 닿으면 잡기
void AGrabGun::OnJawOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// 서버 권한 + 아직 안 잡았고 + 펴는 중일 때만
	if (!HasAuthority() || GrabbedTarget || !bGrabArmed)
	{
		return;
	}
	// 든 사람 자신은 무시
	if (OtherActor == LastOwner || OtherActor == this)
	{
		return;
	}
	// 피아식별: WeaponItemBase의 IsValidTarget(TargetTeam) 사용
	if (!IsValidTarget(OtherActor))
	{
		return;
	}
	ACharacterBase* Target = Cast<ACharacterBase>(OtherActor);
	if (Target)
	{
		GrabTarget(Target);
	}
}

// [GRAB-003] 무기 공통 히트 처리 override: (콜라이더 방식이라 트레이스 히트는 안 쓴다. 호환용으로 남김)
void AGrabGun::ApplyWeaponHit_Implementation(AActor* HitActor, const FHitResult& Hit)
{
	// 집게 콜라이더 오버랩(OnJawOverlap)에서 처리하므로 여기서는 아무것도 하지 않는다.
}

// 집게 콜라이더 on/off
void AGrabGun::SetJawColliderActive(bool bActive)
{
	if (!JawGrabCollider)
	{
		return;
	}
	if (bActive)
	{
		JawGrabCollider->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		JawGrabCollider->SetGenerateOverlapEvents(true);
	}
	else
	{
		JawGrabCollider->SetGenerateOverlapEvents(false);
		JawGrabCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

// [GRAB-010] 대상을 집는다 (서버): 집게 컴포넌트에 직접 attach + 이동정지
void AGrabGun::GrabTarget(ACharacterBase* Target)
{
	if (!HasAuthority() || !Target)
	{
		return;
	}

	// 더 이상 새 대상을 찾지 않는다 (콜라이더 끔)
	bGrabArmed = false;
	SetJawColliderActive(false);

	// 대상을 집게 끝(EndYoke)에 attach → 집게 위치에 붙는다.
	// 위치는 집게에 스냅하되, 회전은 대상 자신의 것을 유지한다(총 방향 90도 회전에 안 딸려가게).
	USceneComponent* AttachTo = EndYoke ? (USceneComponent*)EndYoke : (USceneComponent*)MeshComponent;
	const FAttachmentTransformRules GrabAttachRules(
		EAttachmentRule::SnapToTarget,   // Location: 집게에 스냅
		EAttachmentRule::KeepWorld,      // Rotation: 대상의 월드 회전 유지
		EAttachmentRule::KeepWorld,      // Scale: 유지
		true);
	Target->AttachToComponent(AttachTo, GrabAttachRules);

	// 잡힌 대상 이동 정지 (서버가 위치를 attach로 강제하므로 클라 예측 이동 끔)
	if (UCharacterMovementComponent* Move = Target->GetCharacterMovement())
	{
		GrabbedPrevMovementMode = static_cast<uint8>(Move->MovementMode);
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}

	GrabbedTarget = Target;

	// 잡는 즉시 당기기 시작: 뻗던 걸 멈추고 현재 지점부터 접힌다(alpha 1→0).
	// 대상은 집게에 attach돼 있어 링크가 접히면 사용자 쪽으로 딸려온다.
	bPulling = true;
	TargetExtendAlpha = 0.0f;

	// 잡기 성공 시에만 내구도 소모 (ShouldConsumeUseOnFire=false라 여기서 수동 차감)
	if (CurrentDurability > 0.0f)
	{
		CurrentDurability -= 50.0f;
		// 내구도가 0이 되면 이번 당기기를 끝낸 뒤 분해한다 (당기는 도중엔 안 부서짐).
		if (CurrentDurability <= 0.0f)
		{
			CurrentDurability = 0.0f;
			bBreakAfterPull = true;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[GRAB] Grabbed & pulling %s"), *GetNameSafe(Target));
}

// [GRAB-016] 내구도 0 도달 시 빨간 부품 분해 (모든 클라)
void AGrabGun::MulticastBreakApart_Implementation()
{
	BreakApartLinkage();
}

// 빨간 부품(bar/pin/jaw/yoke) 물리 분해: 부모 detach + 물리·콜라이더 ON + 임펄스 + 수명 타이머
void AGrabGun::BreakApartLinkage()
{
	if (bBrokenApart)
	{
		return;
	}
	bBrokenApart = true;

	// 진행 중인 시퀀스 정리 (링크 갱신 멈춤)
	bGrabArmed = false;
	bPulling = false;
	SetJawColliderActive(false);
	SetOwnerInputLocked(false); // 분해 시에도 사용자 잠금 해제
	FinishUse();                // 사용 중 상태 해제 (슬롯 변경 허용)

	// 모든 StaticMesh 컴포넌트 중 루트(body_shell=MeshComponent)와 트리거만 남기고
	// 나머지 빨간 부품을 떨어뜨린다.
	TArray<UStaticMeshComponent*> Meshes;
	GetComponents<UStaticMeshComponent>(Meshes);

	for (UStaticMeshComponent* Comp : Meshes)
	{
		if (!Comp || Comp == MeshComponent || Comp == TriggerMesh)
		{
			continue; // 총몸(루트)·방아쇠는 손에 남긴다
		}

		// 부모에서 떼어 월드에 독립 (Tick 링크 갱신이 위치를 덮지 않도록)
		Comp->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

		// 물리 + 콜라이더 ON → 중력으로 떨어짐
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Comp->SetCollisionProfileName(TEXT("PhysicsActor"));
		Comp->SetSimulatePhysics(true);

		// 살짝 랜덤 임펄스로 우수수 흩어지게
		const FVector Impulse = FVector(
			FMath::FRandRange(-1.0f, 1.0f),
			FMath::FRandRange(-1.0f, 1.0f),
			FMath::FRandRange(0.5f, 1.0f)).GetSafeNormal() * BreakImpulseStrength;
		Comp->AddImpulse(Impulse, NAME_None, true);
	}

	// 팬터그래프 갱신 배열 비우기 (UpdateLinkagePose가 더 이상 만지지 않게)
	CellPivots.Reset();
	PivotUp.Reset();
	PivotDn.Reset();
	PinTop.Reset();
	PinBottom.Reset();
	PinCenter.Reset();
	EndYoke = nullptr;
	JawPivotTop = nullptr;
	JawPivotBottom = nullptr;
	JawBladeTop = nullptr;
	JawBladeBottom = nullptr;
	JawPadTop = nullptr;
	JawPadBottom = nullptr;
	YokeSpine = nullptr;

	// 몇 초 뒤 떨어진 부품들을 파괴 (수명)
	FTimerHandle DummyHandle;
	GetWorldTimerManager().SetTimer(DummyHandle, [this, Meshes]()
	{
		for (UStaticMeshComponent* Comp : Meshes)
		{
			if (Comp && Comp != MeshComponent && Comp != TriggerMesh)
			{
				Comp->DestroyComponent();
			}
		}
	}, BrokenPartLifetime, false);

	UE_LOG(LogTemp, Warning, TEXT("[GRAB] Broke apart (durability 0)"));
}

// [GRAB-011] 시퀀스 종료 (서버): detach + 대상 이동복원 + 사용자 잠금해제 + 상태 리셋
void AGrabGun::ReleaseTarget()
{
	if (!HasAuthority())
	{
		return;
	}

	bGrabArmed = false;
	bPulling = false;
	SetJawColliderActive(false);

	if (GrabbedTarget)
	{
		// 집게에서 떼어내 월드에 남긴다 (당겨진 위치에 그대로)
		GrabbedTarget->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

		// 이동 복원
		if (UCharacterMovementComponent* Move = GrabbedTarget->GetCharacterMovement())
		{
			Move->SetMovementMode(static_cast<EMovementMode>(GrabbedPrevMovementMode));
		}
		UE_LOG(LogTemp, Warning, TEXT("[GRAB] Released (fully pulled) %s"), *GetNameSafe(GrabbedTarget));
	}

	GrabbedTarget = nullptr;
	TargetExtendAlpha = 0.0f; // 링크 접힘 유지

	// 사용자 이동+카메라 잠금 해제 (시퀀스 종료)
	SetOwnerInputLocked(false);

	// 사용 중 상태 해제 → 슬롯 변경 다시 허용 [WEAPON-017]
	FinishUse();

	// 내구도 0으로 이번 당기기가 마지막이었으면, 당김 완료 후 지금 분해한다.
	if (bBreakAfterPull)
	{
		bBreakAfterPull = false;
		MulticastBreakApart();
	}
}

// [GRAB-015] 사용자 입력 잠금/해제 (서버 진입점).
// 복제 변수 bOwnerInputLocked만 세팅 → 소유 클라는 OnRep에서, 서버(리슨호스트)는 여기서 직접 로컬 적용.
void AGrabGun::SetOwnerInputLocked(bool bLock)
{
	if (!HasAuthority() || bOwnerInputLocked == bLock)
	{
		return;
	}
	bOwnerInputLocked = bLock;
	ApplyLocalInputLock(bLock); // 리슨서버 호스트 자신이 사용자인 경우 즉시 적용
}

// 복제 도착 시 소유 클라에서 로컬 컨트롤러에 실제 Ignore 적용
void AGrabGun::OnRep_OwnerInputLocked()
{
	ApplyLocalInputLock(bOwnerInputLocked);
}

// 실제 로컬 PlayerController에 Ignore 적용 (로컬 소유일 때만). Ignore는 카운터라 짝을 맞춘다.
void AGrabGun::ApplyLocalInputLock(bool bLock)
{
	if (bLocalInputLockApplied == bLock)
	{
		return;
	}
	APawn* OwnerPawn = Cast<APawn>(LastOwner);
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return; // 이 머신이 사용자를 로컬 제어하지 않으면 입력 무시 대상 아님
	}
	APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
	if (!PC)
	{
		return;
	}
	PC->SetIgnoreMoveInput(bLock);
	PC->SetIgnoreLookInput(bLock);
	bLocalInputLockApplied = bLock;
}

// [GRAB-004] 그래버 자체를 내려놓을 때 잡고 있던 대상도 해제
void AGrabGun::Drop_Implementation(FVector DropLocation, AActor* Dropper)
{
	if (HasAuthority())
	{
		ReleaseTarget();
	}
	Super::Drop_Implementation(DropLocation, Dropper);
}

// [GRAB-005] 던질 때도 해제
void AGrabGun::Throw_Implementation(FVector ThrowVelocity, AActor* Thrower)
{
	if (HasAuthority())
	{
		ReleaseTarget();
	}
	Super::Throw_Implementation(ThrowVelocity, Thrower);
}

// [GRAB-006] 발사 이펙트 훅
void AGrabGun::MulticastPlayFireEffect_Implementation(FVector Start, FVector End, bool bHit)
{
	OnFireEffect(Start, End, bHit);
}
