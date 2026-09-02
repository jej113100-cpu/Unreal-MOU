#pragma once

#include "CoreMinimal.h"
#include "Item/WeaponItemBase.h"
#include "GrabGun.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class USphereComponent;
class UPrimitiveComponent;
class ACharacterBase;
class UGrabFollowComponent;

/**
 * AGrabGun
 * 전투/유틸 아이템 - 그래버 건 (팬터그래프 집게총).
 * 좌클릭(OnUse) 시 카메라 조준 방향으로 피아식별 트레이스를 발사해 맞은 캐릭터를
 * 그 캐릭터에 붙어있는 UGrabFollowComponent를 통해 "집는다"(따라오게 만든다).
 *
 * 놓기 방식: 재발사 토글. (공용 입력 코드 수정 없이 OnUse 단발만으로 잡기/놓기)
 *   - 잡고 있지 않을 때 좌클릭 → 트레이스해서 잡음
 *   - 잡고 있을 때  좌클릭 → 잡은 대상을 놓음
 *
 * 연출(B층): body_shell(=루트 MeshComponent)에 팬터그래프 부품 메시들을 계층으로 붙이고,
 *   ExtendAlpha(0=접힘~1=최대뻗음)를 삼각함수로 풀어 링크가 촤르륵 뻗었다 접히게 한다.
 *   전기 이펙트류 VFX는 블루프린트에서 BlueprintImplementableEvent로 처리.
 */
UCLASS()
class TEAMPROJECT_MOU_API AGrabGun : public AWeaponItemBase
{
	GENERATED_BODY()

public:
	AGrabGun();

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

#pragma region [GRAB] 컴포넌트
	// 팬터그래프 링크가 매달리는 루트. body_shell(=MeshComponent) 앞쪽에 붙는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GrabGun")
	TObjectPtr<USceneComponent> LinkageRoot;

	// 총구/집게 끝 지점. VFX 시작 위치 및 트레이스 기준 폴백에 사용.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GrabGun")
	TObjectPtr<USceneComponent> MuzzlePoint;

	// 집게 끝 접촉 판정 콜라이더 (end_yoke 자식). 펴지는 동안 켜서 대상과 닿으면 잡는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GrabGun")
	TObjectPtr<USphereComponent> JawGrabCollider;

	// 집게 콜라이더 반경 (cm). BP에서 조정.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun")
	float JawColliderRadius = 8.0f;

	// 집게 콜라이더 로컬 위치 (end_yoke 기준, cm). 집게 물리는 지점으로 옮긴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun")
	FVector JawColliderOffset = FVector(6.0f, 0.0f, 0.0f);

	// 총 방향 보정 (deg). Details에서 이 값을 넣으면 루트(body_shell+링크 전부)가 그만큼 돌아간다.
	// 손에 쥐면 소켓 스냅이 루트 회전을 덮으므로, 매 Tick 루트에 다시 적용해 방향을 유지한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun")
	FRotator MeshOrientationFix = FRotator::ZeroRotator;
#pragma endregion

#pragma region [GRAB] 설정값
	// 트레이스 사거리 (cm) - 카메라 시점 기준 전방
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun")
	float GrabRange = 800.0f;

	// 집을 때 잡은 대상을 어느 소켓에 매달지 (GrabFollowComponent로 전달)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun")
	FName GrabSocketName = TEXT("GrabSocket");

	// 집은 대상과의 상대 오프셋 (소켓 기준). 잡은 대상을 앞쪽에 띄워두고 싶을 때 조정.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun")
	FVector GrabRelativeOffset = FVector::ZeroVector;
#pragma endregion

#pragma region [GRAB] 팬터그래프 연출 설정 (GLB matrix 실측 기반)
	// 셀(가위 링크) 개수. 임포트된 bar_up_0..8 / pin_*_0..8 기준 9칸.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	int32 CellCount = 12;

	// 링크 반팔 길이 R (cm). GLB 실측: bar가 pivot 자식으로 (R,0,±0.35)에 놓임 = 3.1.
	//   셀 간격 dx = 2*R*cos(a), 핀 반높이 hh = R*sin(a).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float LinkHalfLength = 3.1f;

	// linkage_root 로컬 위치 (cm). GLB 실측 (7.0, 1.1, 0).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	FVector LinkageRootOffset = FVector(7.0f, 1.1f, 0.0f);

	// bar 메시가 pivot 자식으로 갖는 Z 오프셋 (cm). GLB 실측 ±0.35.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float BarZOffset = 0.35f;

	// 펼침(최대 뻗음) 각 a (deg). GLB 기본 포즈 실측 = 35.98°.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float ExtendedAngleDeg = 35.98f;

	// 접힘 각 a (deg). 각이 클수록 셀이 촘촘히 접힘. 기본 70°(접었을 때 짧아짐).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float FoldedAngleDeg = 70.0f;

	// 링크가 뻗는/접히는 속도 (alpha/초). 1이면 1초에 완전히 뻗음.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float ExtendSpeed = 4.0f;

	// 집게턱 위치 (end_yoke 자식). GLB 실측 (0.6, ±2.62, 0), 기본 회전 ±10.03°.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	FVector JawPivotOffset = FVector(0.6f, 2.62f, 0.0f);

	// 집게턱 기본(펼침) 회전 (deg). 뷰포트 조정 확정값.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float JawBaseAngleDeg = 2.597f;

	// 집게턱 추가 벌림 각 (deg). alpha에 비례해 기본각(10.03°)에서 더 벌어진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float JawOpenAngle = 8.0f;

	// --- 집게 메시 방향 미세조정 (뷰포트 보며 Details에서 돌린다) ---
	// blade 메시의 pivot 로컬 위치. 뷰포트 조정 확정값.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Jaw")
	FVector JawBladeOffset = FVector(2.766f, 0.828f, 0.0f);

	// blade 메시 회전 (Pitch/Yaw/Roll). 뷰포트 조정 확정값. (Pitch≈87.5로 세워진 메시를 눕힘)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Jaw")
	FRotator JawBladeRot = FRotator(87.477f, -0.739f, 0.564f);

	// pad 메시의 pivot 로컬 위치. 뷰포트 조정 확정값.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Jaw")
	FVector JawPadOffset = FVector(4.993f, 2.682f, 0.0f);

	// pad 메시 회전 (Pitch/Yaw/Roll). 뷰포트 조정 확정값.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Jaw")
	FRotator JawPadRot = FRotator(90.0f, 0.0f, -4.219f);

	// yoke_spine(집게 연결부 세로 바) 메시 회전. 세로로 서 있으면 여기서 눕힌다. 뷰포트에서 맞춘다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Jaw")
	FRotator YokeSpineRot = FRotator::ZeroRotator;

	// --- 방아쇠(trigger) 위치/회전 미세조정 (뷰포트 보며 Details에서 맞춘다) ---
	// trigger 메시의 trigger_pivot 로컬 위치. GLB 실측 (0.013, -2.468, 0).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Trigger")
	FVector TriggerMeshOffset = FVector(0.013f, -2.468f, 0.0f);

	// trigger 메시 회전 (Pitch/Yaw/Roll). 방향이 틀어지면 여기서 돌린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Trigger")
	FRotator TriggerMeshRot = FRotator::ZeroRotator;

	// 트리거 피벗 위치 (body_shell 자식). GLB 실측 (-0.2, -0.8, 0).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	FVector TriggerPivotOffset = FVector(-0.2f, -0.8f, 0.0f);

	// 트리거 최대 당김 각 (deg).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float TriggerPullAngle = 24.0f;
#pragma endregion

#pragma region [GRAB] 사용/발사 (WeaponItemBase 훅)
	// [GRAB-001] 발사 override: 잡고 있으면 놓기, 아니면 트레이스해서 잡기 (재발사 토글)
	virtual void Fire() override;

	// [GRAB-002] 잡는 순간에만 내구도 1 소모. (놓기는 소모 안 함 → Fire 안에서 조건부 처리)
	//   토글이라 발사 시점 자동차감을 끄고, 실제 잡기 성공 시에만 서버에서 수동 차감한다.
	virtual bool ShouldConsumeUseOnFire() const override { return false; }

	// 그래버는 뻗기~당기기 시퀀스 동안 "사용 중"이라 슬롯 변경을 막는다. [WEAPON-015]
	virtual bool bUsesInUseState() const override { return true; }

	// [GRAB-003] 무기 공통 히트 처리 override: 맞은 캐릭터를 GrabFollowComponent로 집기
	virtual void ApplyWeaponHit_Implementation(AActor* HitActor, const FHitResult& Hit) override;
#pragma endregion

#pragma region [GRAB] 소유권/생명주기
	// [GRAB-004] 놓을 때(G키) 잡고 있던 대상도 자동 해제
	virtual void Drop_Implementation(FVector DropLocation, AActor* Dropper = nullptr) override;

	// [GRAB-005] 던질 때도 잡고 있던 대상 해제
	virtual void Throw_Implementation(FVector ThrowVelocity, AActor* Thrower = nullptr) override;
#pragma endregion

#pragma region [GRAB] 연출 훅 (Blueprint VFX)
	// [GRAB-006] 발사 이펙트 훅 (집게 발사/명중 등). 시작/끝 지점 전달, 모든 클라 재생
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayFireEffect(FVector Start, FVector End, bool bHit);

	// 블루프린트에서 실제 나이아가라/케이블 VFX를 붙이는 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "GrabGun|FX")
	void OnFireEffect(FVector Start, FVector End, bool bHit);
#pragma endregion

private:
#pragma region [GRAB] 잡기 상태
	// 현재 이 그래버가 집고 있는 대상. 서버 권한에서만 갱신, 재발사 토글 판단에 사용.
	UPROPERTY(Replicated)
	TObjectPtr<ACharacterBase> GrabbedTarget;

	// 발사(펴짐) 중이라 집게 콜라이더로 대상을 찾고 있는 상태. true인 동안 오버랩하면 잡는다.
	bool bGrabArmed = false;

	// 대상을 잡아 당기는 중(alpha 1→0 접히며 대상이 사용자 쪽으로 옴). 접힘 완료 시 detach.
	bool bPulling = false;

	// 그래버를 쓰는 동안(뻗기~완전히 당겨질 때까지) 사용자 입력 잠금 여부(복제).
	// 서버가 설정 → 소유 클라의 OnRep에서 로컬 PlayerController에 실제 Ignore 적용.
	UPROPERTY(ReplicatedUsing = OnRep_OwnerInputLocked)
	bool bOwnerInputLocked = false;

	UFUNCTION()
	void OnRep_OwnerInputLocked();

	// 마지막으로 로컬 컨트롤러에 적용한 잠금 상태 (중복 적용 방지, Ignore 카운터 짝 맞춤)
	bool bLocalInputLockApplied = false;

	// 실제 로컬 PlayerController에 Ignore 적용 (서버·클라 공통 진입점)
	void ApplyLocalInputLock(bool bLock);

	// [GRAB-002B] 집게 콜라이더 오버랩 콜백 - 펴짐 중(bGrabArmed) player/enemy 닿으면 잡기
	UFUNCTION()
	void OnJawOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	// [GRAB-010] 대상을 집는다 (서버). 집게에 attach + 이동정지 + 당기기 시작. 성공 시 내구도 1 소모.
	void GrabTarget(ACharacterBase* Target);

	// [GRAB-011] 잡고 있던 대상을 놓는다 (서버). detach + 이동복원 + 사용자 잠금해제.
	void ReleaseTarget();

	// 집게 콜라이더 on/off (펴짐 시작/접힘에서 호출)
	void SetJawColliderActive(bool bActive);

	// [GRAB-015] 사용자(그래버 든 플레이어)의 이동+카메라 입력 잠금/해제 (PlayerController Ignore).
	void SetOwnerInputLocked(bool bLock);

	// 잡힌 대상의 이동 모드 복원용 저장값
	uint8 GrabbedPrevMovementMode = 0;
#pragma endregion

protected:
#pragma region [GRAB] 내구도 0 분해 연출
	// 이미 분해됐는지 (중복 방지)
	bool bBrokenApart = false;

	// 이번 당기기가 끝나면(내구도 0) 분해할 예정인지. GrabTarget에서 세우고 ReleaseTarget에서 처리.
	bool bBreakAfterPull = false;

	// 떨어진 부품이 사라지기까지 시간 (초). BP에서 조정.
	UPROPERTY(EditDefaultsOnly, Category = "GrabGun|Break")
	float BrokenPartLifetime = 4.0f;

	// 부품이 튀어오르는 임펄스 세기. BP에서 조정.
	UPROPERTY(EditDefaultsOnly, Category = "GrabGun|Break")
	float BreakImpulseStrength = 150.0f;

	// [GRAB-016] 내구도 0 도달 시 빨간 부품(bar/pin/jaw/yoke)을 물리로 분해 (모든 클라 재생)
	UFUNCTION(NetMulticast, Reliable)
	void MulticastBreakApart();

	// 실제 분해 처리 (부모 detach + 물리/콜라이더 ON + 임펄스 + 수명 타이머)
	void BreakApartLinkage();
#pragma endregion

private:

#pragma region [GRAB] 팬터그래프 부품/구동
	// 링크 뻗음 정도 목표치 (0=접힘, 1=최대뻗음). 잡으면 1, 놓으면 0으로 보간된다.
	UPROPERTY(Replicated)
	float TargetExtendAlpha = 0.0f;

	// 현재 뻗음 정도 (매 Tick TargetExtendAlpha로 보간). 시각 표현용이라 각 클라에서 로컬 계산.
	float CurrentExtendAlpha = 0.0f;

	// 셀별 회전 피벗(빈 SceneComponent)과 그 아래 bar 메시. 인덱스=셀 번호.
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> CellPivots;       // cell_i 기준점 (linkage_root 자식으로 체인)
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> PivotUp;           // pivot_up_i
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> PivotDn;           // pivot_dn_i
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PinTop;       // pin_top_i
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PinBottom;    // pin_bottom_i
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PinCenter;    // pin_center_i

	// 끝단 요크 + 집게턱 + 트리거
	UPROPERTY()
	TObjectPtr<USceneComponent> EndYoke;
	UPROPERTY()
	TObjectPtr<USceneComponent> JawPivotTop;
	UPROPERTY()
	TObjectPtr<USceneComponent> JawPivotBottom;
	UPROPERTY()
	TObjectPtr<USceneComponent> TriggerPivot;

	// 집게 blade/pad 메시 (Details 값으로 위치·회전 갱신하려고 포인터 보관)
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> JawBladeTop;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> JawBladeBottom;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> JawPadTop;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> JawPadBottom;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> YokeSpine;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> TriggerMesh;

	// [GRAB-012] 생성자에서 고정 부품(집게·트리거·콜라이더)을 스폰
	void BuildLinkageComponents();

	// [GRAB-012B] CellCount만큼 셀(cell/bar/pin)을 런타임 재생성.
	//   CreateDefaultSubobject(생성자 전용) 대신 NewObject+RegisterComponent를 써서
	//   OnConstruction/BeginPlay에서 CellCount가 바뀌면 다시 만든다.
	void RebuildCells();

	// 마지막으로 셀을 지은 개수 (같으면 재생성 스킵)
	int32 BuiltCellCount = -1;

	// [GRAB-013] CurrentExtendAlpha에 맞춰 링크/집게/트리거 트랜스폼 갱신 (Tick에서 호출)
	void UpdateLinkagePose(float Alpha);

	// [GRAB-014] 부품 메시 하나 로드 헬퍼 (extending-arm-toy-gun 폴더 기준)
	UStaticMesh* LoadPartMesh(const FString& PartName) const;

	// GLB matrix 실측 좌표를 UE 로컬 위치 벡터로 만든다.
	FVector MakeLocal(float X, float Y, float Z) const;
	// Z축(Yaw) 회전을 UE 로컬 회전으로 만든다.
	FRotator MakeYawRot(float DegZ) const;
#pragma endregion
};
