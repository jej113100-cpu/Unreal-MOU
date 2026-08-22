#include "Item/WeaponItemBase.h"
#include "Base/CharacterBase.h"
#include "Player/MainCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

AWeaponItemBase::AWeaponItemBase()
{
	// 근접 타격 콜라이더 (평소 꺼둠, BP에서 크기/위치 조정)
	MeleeCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("MeleeCollider"));
	MeleeCollider->SetupAttachment(MeshComponent);
	MeleeCollider->SetBoxExtent(FVector(20.0f, 20.0f, 20.0f));
	// 평소엔 충돌 없음 (휘두를 때 EnableMeleeCollision(true)로 켬)
	MeleeCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeleeCollider->SetGenerateOverlapEvents(false);
}

void AWeaponItemBase::BeginPlay()
{
	Super::BeginPlay();

	// 내구도를 최대치로 초기화 (ItemBase의 CurrentDurability/MaxDurability 사용)
	CurrentDurability = MaxDurability;

	// 근접 콜라이더 오버랩 콜백 바인딩
	if (MeleeCollider)
	{
		MeleeCollider->OnComponentBeginOverlap.AddDynamic(this, &AWeaponItemBase::OnMeleeOverlap);
	}
}

void AWeaponItemBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// 내구도(CurrentDurability)는 ItemBase에서 이미 복제하므로 여기서 별도 등록하지 않는다.
}

// [WEAPON-010] 집을 때 소유권 설정 (클라 → 서버 RPC 가능하게)
void AWeaponItemBase::PickUp_Implementation(AActor* Picker)
{
	Super::PickUp_Implementation(Picker);

	// PickUp은 서버에서 실행됨. Owner를 든 플레이어로 설정 → 그 플레이어 클라가 ServerRPC 전송 가능
	if (HasAuthority() && Picker)
	{
		SetOwner(Picker);
	}
}

// [WEAPON-011] 놓을 때 소유권 해제
void AWeaponItemBase::Drop_Implementation(FVector DropLocation, AActor* Dropper)
{
	if (HasAuthority())
	{
		SetOwner(nullptr);
	}

	Super::Drop_Implementation(DropLocation, Dropper);
}

// [WEAPON-012] 던질 때 소유권 해제
void AWeaponItemBase::Throw_Implementation(FVector ThrowVelocity, AActor* Thrower)
{
	if (HasAuthority())
	{
		SetOwner(nullptr);
	}

	Super::Throw_Implementation(ThrowVelocity, Thrower);
}

// [WEAPON-000] 좌클릭: 발사는 항상 서버에서 처리 (차감/판정/복제 신뢰성)
void AWeaponItemBase::OnUse_Implementation()
{
	// 차감·발사는 서버에서만. 클라에서 불리면 ServerFire로 넘겨 서버가 처리한다.
	// (Super::OnUse는 부르지 않음 - ItemBase의 CurrentUseCount 대신 복제되는 CurrentDurability 사용)
	if (HasAuthority())
	{
		TryFireOnServer();
	}
	else
	{
		ServerFire();
	}
}

// [WEAPON-009] 서버 공통 진입점: 잔여 체크 → 차감(복제) → Fire
void AWeaponItemBase::TryFireOnServer()
{
	// 서버에서만 실행되어야 함
	if (!HasAuthority())
	{
		return;
	}

	if (CurrentDurability <= 0.0f)
	{
		return;
	}

	// 소모형 무기만 차감(복제되어 모든 클라 화면에 동기화). false로 override한 무기는 차감 안 함. [WEAPON-013]
	// 발사 1회당 차감량은 무기별로 다르다(GetDurabilityCostPerUse). [WEAPON-014]
	if (ShouldConsumeUseOnFire())
	{
		CurrentDurability -= GetDurabilityCostPerUse();
	}

	Fire();
}

// [WEAPON-007] 실제 발사 로직 (기본 빈 구현, 자식이 override)
void AWeaponItemBase::Fire()
{
	// 자식 클래스에서 구현 (테이저=트레이스, 칼=콜라이더 등)
}

// [WEAPON-008] 클라이언트 → 서버 발사 위임 (서버에서 차감+발사)
void AWeaponItemBase::ServerFire_Implementation()
{
	TryFireOnServer();
}

// [WEAPON-005] 대상이 유효 타겟인지 코드로 판정 (피아식별)
bool AWeaponItemBase::IsValidTarget(AActor* HitActor) const
{
	// 캐릭터가 아니면 대상 아님 (벽/바닥/아이템 등)
	ACharacterBase* Character = Cast<ACharacterBase>(HitActor);
	if (!Character)
	{
		return false;
	}

	// 본인(든 플레이어)은 항상 제외
	if (HitActor == LastOwner)
	{
		return false;
	}

	switch (TargetTeam)
	{
	case EWeaponTargetTeam::Enemy:
		// NPC만: 플레이어(AMainCharacter)면 제외
		return Cast<AMainCharacter>(HitActor) == nullptr;

	case EWeaponTargetTeam::Player:
		// 다른 플레이어만: AMainCharacter여야 함
		return Cast<AMainCharacter>(HitActor) != nullptr;

	case EWeaponTargetTeam::AllButSelf:
	default:
		// 본인 뺀 모든 캐릭터 (위에서 이미 걸렀으므로 true)
		return true;
	}
}

// [WEAPON-001] 근접 콜라이더 오버랩 on/off
void AWeaponItemBase::EnableMeleeCollision(bool bEnable)
{
	if (!MeleeCollider)
	{
		return;
	}

	if (bEnable)
	{
		MeleeCollider->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeleeCollider->SetGenerateOverlapEvents(true);
	}
	else
	{
		MeleeCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeleeCollider->SetGenerateOverlapEvents(false);
	}
}

// [WEAPON-002] 근접 콜라이더 오버랩 콜백
void AWeaponItemBase::OnMeleeOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// 판정은 서버 권한에서만
	if (!HasAuthority())
	{
		return;
	}

	// 자기 자신은 무시 + 피아식별은 IsValidTarget 코드 판정
	if (!OtherActor || OtherActor == this || !IsValidTarget(OtherActor))
	{
		return;
	}

	ApplyWeaponHit(OtherActor, SweepResult);
}

// [WEAPON-003] 즉발 트레이스 발사 (피아식별은 IsValidTarget 코드 판정)
// Pawn 채널로 멀티 트레이스 → 벽/바닥(WorldStatic)이 앞을 막으면 거기서 멈추고,
// 캐릭터들 중 첫 유효 타겟(본인 제외 등)에게만 효과.
bool AWeaponItemBase::FireHitscan(const FVector& Start, const FVector& End, FHitResult& OutHit)
{
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	if (LastOwner)
	{
		Params.AddIgnoredActor(LastOwner);
	}

	// 벽/바닥 + 모든 Pawn을 함께 감지 (Visibility=지형, Pawn=캐릭터)
	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	TArray<FHitResult> Hits;
	GetWorld()->LineTraceMultiByObjectType(Hits, Start, End, ObjParams, Params);

	// 시작점에서 가까운 순서로 정렬 (막힘 판정을 위해 거리순 보장)
	Hits.Sort([](const FHitResult& A, const FHitResult& B)
	{
		return A.Distance < B.Distance;
	});

	// 가까운 순서대로: 벽을 먼저 만나면 중단, 캐릭터면 유효 타겟일 때 효과
	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor)
		{
			continue;
		}

		// 캐릭터가 아닌 지형(벽/바닥)을 먼저 만나면 탄이 막힘 → 종료
		if (!Cast<ACharacterBase>(HitActor))
		{
			OutHit = Hit;
			return false; // 지형에 막힘 (캐릭터 못 맞힘)
		}

		// 캐릭터인데 유효 타겟이면 명중 처리
		if (IsValidTarget(HitActor))
		{
			OutHit = Hit;
			ApplyWeaponHit(HitActor, Hit);
			return true;
		}
		// 유효 타겟 아닌 캐릭터(본인 등)는 통과해서 뒤의 대상 계속 검사
	}

	return false;
}

// [WEAPON-004] 공통 히트 처리 (기본은 빈 구현, 자식이 override)
void AWeaponItemBase::ApplyWeaponHit_Implementation(AActor* HitActor, const FHitResult& Hit)
{
	// 자식 클래스에서 구현 (테이저건=기절, 칼=데미지 등)
}

// ---------------------------------------------------------------------------
// [WEAPON] 원거리 발사체 (Projectile) - 현재 미사용, 주석 처리.
//   나중에 날아가는 투사체가 필요하면 헤더의 주석과 함께 아래 주석을 풀 것.
// ---------------------------------------------------------------------------
// AActor* AWeaponItemBase::FireProjectile(const FVector& SpawnLocation, const FVector& Direction)
// {
//	if (!ProjectileClass || !GetWorld())
//	{
//		return nullptr;
//	}
//
//	FActorSpawnParameters SpawnParams;
//	SpawnParams.Owner = this;
//	SpawnParams.Instigator = Cast<APawn>(LastOwner);
//	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
//
//	const FRotator SpawnRot = Direction.Rotation();
//	AActor* Projectile = GetWorld()->SpawnActor<AActor>(ProjectileClass, SpawnLocation, SpawnRot, SpawnParams);
//	// TODO: Projectile에 UProjectileMovementComponent를 붙이고 InitialSpeed=ProjectileSpeed 설정,
//	//       Projectile 쪽에서 TargetTeam 채널로 히트 판정 → ApplyWeaponHit 호출 연동.
//	return Projectile;
// }
