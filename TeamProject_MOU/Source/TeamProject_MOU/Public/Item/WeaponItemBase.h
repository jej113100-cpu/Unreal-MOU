#pragma once

#include "CoreMinimal.h"
#include "Base/ItemBase.h"
#include "WeaponItemBase.generated.h"

class UBoxComponent;
class UPrimitiveComponent;

/**
 * 무기 대상 판정 (피아식별용)
 * 콜리전 채널이 아니라 코드에서 대상 클래스로 판정한다.
 * (트레이스는 Visibility로 넓게 쏘고, 맞은 대상을 IsValidTarget으로 걸러냄)
 */
UENUM(BlueprintType)
enum class EWeaponTargetTeam : uint8
{
	// 본인(든 플레이어)만 빼고 모든 캐릭터를 맞힘 (테이저건 등)
	AllButSelf	UMETA(DisplayName = "All But Self"),
	// 적(NPC)만 맞힘
	Enemy		UMETA(DisplayName = "Enemy (NPC only)"),
	// 다른 플레이어만 맞힘
	Player		UMETA(DisplayName = "Player only")
};

/**
 * 무기 명중 방식
 */
UENUM(BlueprintType)
enum class EWeaponHitMode : uint8
{
	// 근접: 콜라이더 오버랩으로 명중
	Melee		UMETA(DisplayName = "Melee (Collider)"),
	// 원거리: 즉발 Line Trace로 명중 (발사체는 별도, 현재 주석)
	Ranged		UMETA(DisplayName = "Ranged (Trace)")
};

/**
 * AWeaponItemBase
 * 무기 아이템 공통 베이스. 주 목적 두 가지:
 *   1) 피아식별 - 커스텀 Trace Channel(NPC/Player)로 대상 진영 구분
 *   2) 콜라이더 - 근접용 오버랩 콜라이더 제공 (원거리 발사체는 주석 처리)
 * 자식(테이저건, 칼 등)은 ApplyWeaponHit만 override해서 실제 효과를 넣는다.
 */
UCLASS()
class TEAMPROJECT_MOU_API AWeaponItemBase : public AItemBase
{
	GENERATED_BODY()

public:
	AWeaponItemBase();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// [WEAPON] 내구도는 ItemBase의 CurrentDurability/MaxDurability를 사용한다.
	//   (CurrentDurability는 ItemBase에서 이미 복제되므로 무기용 별도 복제 변수는 두지 않는다)
	//   던질 때마다 서버에서 1씩 차감하고, 0 이하가 되면 더 발사할 수 없다.

#pragma region [WEAPON] 소유권 (Server RPC 전제)
	// [WEAPON-010] 집을 때 소유권 설정 → 클라가 ServerFire RPC 보낼 수 있게 함
	// (Owner 없으면 "No owning connection"으로 ServerRPC가 버려짐)
	virtual void PickUp_Implementation(AActor* Picker) override;

	// [WEAPON-011] 놓을 때 소유권 해제
	virtual void Drop_Implementation(FVector DropLocation, AActor* Dropper = nullptr) override;

	// [WEAPON-012] 던질 때 소유권 해제
	virtual void Throw_Implementation(FVector ThrowVelocity, AActor* Thrower = nullptr) override;
#pragma endregion

#pragma region [WEAPON] 사용/발사 흐름 (공통)
	// [WEAPON-000] 좌클릭: 잔여 횟수 체크 → (서버)차감 → Fire() 호출
	virtual void OnUse_Implementation() override;

	// [WEAPON-007] 실제 발사 로직. 자식이 override (테이저=트레이스, 칼=콜라이더 등)
	virtual void Fire();

	// [WEAPON-013] 이 발사에서 내구도(CurrentDurability)를 차감할지 여부.
	// 기본 true(소모). 발사 시점에 소모하지 않는 무기(예: 적중 시에만 닳는 부메랑)는 false로 override한다.
	virtual bool ShouldConsumeUseOnFire() const { return true; }

	// [WEAPON-014] 발사 1회당 깎일 내구도 양. 무기마다 다르게(테이저=25 등) override.
	// ShouldConsumeUseOnFire()가 true일 때만 TryFireOnServer에서 이 값만큼 차감한다.
	virtual float GetDurabilityCostPerUse() const { return 1.0f; }

	// [WEAPON-015] 이 무기가 발사 시 "사용 중" 상태를 유지하는지.
	//   기본 false(즉발 무기는 사용 중이 없음). 그래버처럼 동작이 이어지는 무기는 true로 override.
	//   true면 OnUse에서 bIsInUse=true가 되고, 자식이 FinishUse()로 끝을 알린다.
	virtual bool bUsesInUseState() const { return false; }

public:
	// [WEAPON-016] 지금 이 무기를 사용 중인지 (사용 중엔 슬롯 변경 차단용). 복제됨.
	//   MainCharacter가 슬롯 변경 전에 조회하므로 public.
	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsInUse() const { return bIsInUse; }

	// [WEAPON-018] 이 무기를 현재 든 Pawn을 반환.
	//   LastOwner가 유효하면 그걸, 아니면(레벨 이동 등으로 유실 시) attach된 부모 액터에서 찾는다.
	//   발사 시 조준 소스(컨트롤러 시점)를 안정적으로 얻기 위함.
	APawn* GetOwningPawn() const;

protected:
	// [WEAPON-017] 사용 완료 알림 (서버). 자식이 동작이 끝나는 시점에 호출 → bIsInUse=false.
	void FinishUse();

	// 사용 중 상태 (서버가 갱신, 모든 클라 복제). 슬롯 변경 차단 판정에 쓰인다.
	UPROPERTY(Replicated)
	bool bIsInUse = false;

private:
	// [WEAPON-008] 클라이언트에서 눌렀을 때 서버로 발사 위임 (서버에서 차감+발사)
	UFUNCTION(Server, Reliable)
	void ServerFire();

	// [WEAPON-009] 서버에서 잔여 체크 → 차감 → Fire (OnUse/ServerFire 공통 진입점)
	void TryFireOnServer();

protected:
#pragma endregion

#pragma region [WEAPON] 설정값
	// 이 무기가 노리는 대상 (피아식별). 기본은 본인 제외 모든 캐릭터.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon")
	EWeaponTargetTeam TargetTeam = EWeaponTargetTeam::AllButSelf;

	// 근접/원거리 명중 방식
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon")
	EWeaponHitMode HitMode = EWeaponHitMode::Ranged;
#pragma endregion

#pragma region [WEAPON] 근접 콜라이더
	// 근접 타격용 콜라이더. BP에서 크기/위치 조정 가능. 평소 꺼두고 휘두를 때만 켬.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Melee")
	TObjectPtr<UBoxComponent> MeleeCollider;

	// [WEAPON-001] 근접 콜라이더 오버랩 on/off (휘두르기 시작/끝에서 호출)
	UFUNCTION(BlueprintCallable, Category = "Weapon|Melee")
	void EnableMeleeCollision(bool bEnable);

	// [WEAPON-002] 근접 콜라이더 오버랩 콜백 - 진영 확인 후 ApplyWeaponHit
	UFUNCTION()
	void OnMeleeOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
#pragma endregion

#pragma region [WEAPON] 원거리 (즉발 트레이스)
	// [WEAPON-003] 즉발 트레이스 발사. TargetTeam 채널로 트레이스해 피아식별. 맞으면 ApplyWeaponHit.
	// 반환: 명중했으면 true, HitResult 채워짐
	UFUNCTION(BlueprintCallable, Category = "Weapon|Ranged")
	bool FireHitscan(const FVector& Start, const FVector& End, FHitResult& OutHit);
#pragma endregion

#pragma region [WEAPON] 공통 히트 처리
	// [WEAPON-004] 실제 무기 효과 (기절/데미지 등). 자식이 override.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Weapon")
	void ApplyWeaponHit(AActor* HitActor, const FHitResult& Hit);
	virtual void ApplyWeaponHit_Implementation(AActor* HitActor, const FHitResult& Hit);
#pragma endregion

protected:
	// [WEAPON-005] 대상이 이 무기의 유효 타겟인지 판정 (피아식별 코드 판정)
	// TargetTeam에 따라: AllButSelf=본인 뺀 모든 캐릭터, Enemy=NPC만, Player=다른 플레이어만
	// 자식이 필요하면 override 가능
	virtual bool IsValidTarget(AActor* HitActor) const;

// ---------------------------------------------------------------------------
// [WEAPON] 원거리 발사체 (Projectile) - 현재 미사용, 주석 처리.
//   나중에 날아가는 투사체가 필요하면 아래 주석을 풀고 사용.
//   (별도 Projectile 액터 클래스 AProjectileBase가 필요함)
// ---------------------------------------------------------------------------
// public:
//	// 발사할 투사체 클래스 (BP에서 지정)
//	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Projectile")
//	TSubclassOf<AActor> ProjectileClass;
//
//	// 투사체 발사 속도 (cm/s)
//	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Projectile")
//	float ProjectileSpeed = 3000.0f;
//
//	// [WEAPON-006] 투사체 스폰 후 발사 (서버 권한에서 호출 권장)
//	UFUNCTION(BlueprintCallable, Category = "Weapon|Projectile")
//	AActor* FireProjectile(const FVector& SpawnLocation, const FVector& Direction);
};
