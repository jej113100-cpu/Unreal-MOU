#include "Item/Boomerang.h"
#include "Base/CharacterBase.h"
#include "Base/BaseAttributeSet.h"
#include "Components/StatusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffect.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

ABoomerang::ABoomerang()
{
	// 비행 궤적을 매 프레임 직접 제어해야 하므로 Tick 필수 (AItemBase가 이미 bCanEverTick=true).
	PrimaryActorTick.bCanEverTick = true;

	// 부메랑은 근접 콜라이더 오버랩으로 비행 중 타격을 판정한다.
	HitMode = EWeaponHitMode::Melee;

	// 최대 내구도 100. 부메랑은 던질 때가 아니라 "적중할 때마다" DurabilityCostPerHit(기본 10)씩
	// 깎이고(ApplyWeaponHit), 0이 되면 돌아온 뒤 파괴된다. BP에서 MaxDurability로 조정.
	MaxDurability = 100.0f;
	CurrentDurability = MaxDurability;

	// 기본 타격 상태이상 = 넘어짐(State.CC.FallDown). DefaultGameplayTags.ini에 등록된 실제 태그.
	// (미등록이면 Invalid → ApplyWeaponHit에서 부여를 건너뛴다)
	HitStatusTag = FGameplayTag::RequestGameplayTag(FName("State.CC.FallDown"), false);
}

void ABoomerang::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABoomerang, FlightState);
}

// 복제된 상태 도착 시 훅 (클라 연출용). 현재 비어있음.
void ABoomerang::OnRep_FlightState()
{
}

// [BOOMERANG-000] 발사: 비행 시작. (부모 OnUse→TryFireOnServer가 서버 권한 확인 후 호출)
void ABoomerang::Fire()
{
	// 이미 비행 중이면 재발사 무시.
	// (부메랑은 발사 시 차감이 없으므로 - ShouldConsumeUseOnFire=false - 환불 처리 불필요)
	if (FlightState != EBoomerangState::Idle)
	{
		return;
	}

	StartFlight();
}

// [BOOMERANG-002] 비행 시작 (서버). 손에서 분리 → 물리 끄고 운동학 이동 준비 → 콜라이더 켜기.
void ABoomerang::StartFlight()
{
	if (!HasAuthority())
	{
		return;
	}

	// 발사 방향은 든 플레이어의 조준(카메라) 방향을 수평면에 투영해서 사용한다.
	// (부메랑은 위/아래로 꽂히기보다 수평으로 도는 게 자연스럽다)
	FVector ViewLocation = GetActorLocation();
	FRotator ViewRotation = GetActorRotation();

	// LastOwner 유실(레벨 이동 등) 대비: attach 부모까지 찾아 현재 든 Pawn을 얻는다. [WEAPON-018]
	if (APawn* OwnerPawn = GetOwningPawn())
	{
		if (OwnerPawn->GetController())
		{
			OwnerPawn->GetController()->GetPlayerViewPoint(ViewLocation, ViewRotation);
		}
		else
		{
			OwnerPawn->GetActorEyesViewPoint(ViewLocation, ViewRotation);
		}
	}

	FVector Dir = ViewRotation.Vector();
	Dir.Z = 0.0f; // 수평면 투영
	FlightDirection = Dir.GetSafeNormal();
	if (FlightDirection.IsNearlyZero())
	{
		// 폴백: 액터 전방(수평)
		FlightDirection = FVector(GetActorForwardVector().X, GetActorForwardVector().Y, 0.0f).GetSafeNormal();
	}

	// 손에서 분리 (월드 트랜스폼 유지)
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	// 운동학 이동: 물리 시뮬레이션 끄고, 충돌은 QueryOnly로 (벽에 튕기지 않고 오버랩만 감지)
	if (MeshComponent)
	{
		MeshComponent->SetSimulatePhysics(false);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		// 던진 본인과는 충돌/오버랩 무시 (손 앞에서 자기 자신에 걸리는 것 방지)
		if (LastOwner)
		{
			MeshComponent->IgnoreActorWhenMoving(LastOwner, true);
		}
	}

	// 비행 중 근접 콜라이더로 타격 판정 (부모 OnMeleeOverlap → IsValidTarget → ApplyWeaponHit)
	EnableMeleeCollision(true);

	FlightStartLocation = GetActorLocation();
	ElapsedFlightTime = 0.0f;
	FlightState = EBoomerangState::Outbound; // 복제됨

	// 던짐 연출 (전 클라)
	MulticastThrown();
}

void ABoomerang::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 비행 로직은 서버 권한에서만. 위치/회전은 SetReplicateMovement(true)로 클라에 복제된다.
	if (!HasAuthority() || FlightState == EBoomerangState::Idle)
	{
		return;
	}

	// 빙글빙글 회전 (SpinAxis 기준 매 프레임 회전)
	const FQuat SpinDelta(SpinAxis.GetSafeNormal(), FMath::DegreesToRadians(SpinSpeed * DeltaTime));
	AddActorLocalRotation(SpinDelta);

	ElapsedFlightTime += DeltaTime;

	const FVector CurrentLoc = GetActorLocation();

	if (FlightState == EBoomerangState::Outbound)
	{
		// 앞으로 직진
		SetActorLocation(CurrentLoc + FlightDirection * FlightSpeed * DeltaTime);

		// 최대 사거리 or 시간 초과 → 되돌아오기
		const float DistFromStart = FVector::Dist(GetActorLocation(), FlightStartLocation);
		if (DistFromStart >= MaxRange || ElapsedFlightTime >= MaxFlightTime)
		{
			BeginReturn();
		}
	}
	else // Returning
	{
		// 소유자가 사라졌으면(로그아웃 등) 그냥 소멸
		if (!LastOwner)
		{
			Destroy();
			return;
		}

		const FVector OwnerLoc = LastOwner->GetActorLocation();
		const FVector ToOwner = OwnerLoc - CurrentLoc;

		// 충분히 가까우면 잡힘
		if (ToOwner.Size() <= CatchRadius)
		{
			CatchByOwner();
			return;
		}

		// 소유자 쪽으로 호밍
		SetActorLocation(CurrentLoc + ToOwner.GetSafeNormal() * ReturnSpeed * DeltaTime);

		// 안전장치: 너무 오래 못 잡으면 강제 회수
		if (ElapsedFlightTime >= MaxFlightTime * 2.0f)
		{
			CatchByOwner();
		}
	}
}

// [BOOMERANG-003] 되돌아오기 전환 (서버). Outbound에서만.
void ABoomerang::BeginReturn()
{
	if (!HasAuthority() || FlightState != EBoomerangState::Outbound)
	{
		return;
	}
	FlightState = EBoomerangState::Returning; // 복제됨
}

// [BOOMERANG-001] 히트 override: 상태이상 부여 + 즉시 되돌아오기 전환
void ABoomerang::ApplyWeaponHit_Implementation(AActor* HitActor, const FHitResult& Hit)
{
	// 판정은 서버에서만 불림 (부모 OnMeleeOverlap이 HasAuthority 가드)
	ACharacterBase* TargetCharacter = Cast<ACharacterBase>(HitActor);
	if (TargetCharacter && HitStatusTag.IsValid() && HitStatusDuration > 0.0f)
	{
		if (UStatusComponent* StatusComp = TargetCharacter->GetStatusComponent())
		{
			// 테이저와 동일하게 Loose 태그로 부여 + 대상 타이머로 해제 예약
			StatusComp->AddStatusTag(HitStatusTag);

			const FGameplayTag TagCopy = HitStatusTag;
			TWeakObjectPtr<UStatusComponent> WeakStatus = StatusComp;
			FTimerDelegate ClearDelegate = FTimerDelegate::CreateLambda([WeakStatus, TagCopy]()
			{
				if (WeakStatus.IsValid())
				{
					WeakStatus->RemoveStatusTag(TagCopy);
				}
			});
			FTimerHandle TmpHandle;
			TargetCharacter->GetWorldTimerManager().SetTimer(TmpHandle, ClearDelegate, HitStatusDuration, false);
		}
	}

	// 적중당 데미지/내구도 차감은 "이번 비행의 첫 적중"에만 적용한다.
	// (같은 프레임 다중 오버랩으로 ApplyWeaponHit이 여러 번 불려도 Outbound일 때만 1회 처리)
	if (FlightState == EBoomerangState::Outbound)
	{
		// 데미지: 팀 정석 방식 - Instant GE로 대상 Health를 Additive 차감.
		// (TestDamageActor와 동일한 파이프라인이라 BaseAttributeSet::PostGameplayEffectExecute 사망처리가 정상 동작)
		if (HitDamage > 0.0f)
		{
			if (UAbilitySystemComponent* TargetASC =
				UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor))
			{
				UGameplayEffect* DamageEffect =
					NewObject<UGameplayEffect>(GetTransientPackage(), FName(TEXT("BoomerangDamage")));
				DamageEffect->DurationPolicy = EGameplayEffectDurationType::Instant;
				DamageEffect->Modifiers.SetNum(1);

				FGameplayModifierInfo& HealthModifier = DamageEffect->Modifiers[0];
				HealthModifier.Attribute = UBaseAttributeSet::GetHealthAttribute();
				HealthModifier.ModifierOp = EGameplayModOp::Additive;
				HealthModifier.ModifierMagnitude = FScalableFloat(-HitDamage);

				FGameplayEffectContextHandle Context = TargetASC->MakeEffectContext();
				Context.AddSourceObject(this);
				TargetASC->ApplyGameplayEffectToSelf(DamageEffect, 1.0f, Context);
			}
		}

		CurrentDurability -= DurabilityCostPerHit;

		// 내구도가 다 닳으면 지금 파괴하지 않고, 손에 돌아온 순간(CatchByOwner) 파괴하도록 예약.
		if (CurrentDurability <= 0.0f)
		{
			CurrentDurability = 0.0f;
			bPendingDestroyOnCatch = true;
		}
	}

	// 첫 유효 타겟을 맞히면 즉시 되돌아온다 (Outbound일 때만 전환).
	BeginReturn();
}

// [BOOMERANG-004] 잡힘 처리 (서버). 상태 복귀 + 손 소켓 재부착 연출.
void ABoomerang::CatchByOwner()
{
	if (!HasAuthority())
	{
		return;
	}

	FlightState = EBoomerangState::Idle; // 복제됨
	ElapsedFlightTime = 0.0f;

	// 손에 돌아왔으니 사용 중 상태 해제 → 슬롯 변경 다시 허용 [WEAPON-017]
	FinishUse();

	// 비행 콜라이더 끄기
	EnableMeleeCollision(false);

	// 내구도가 다 닳아 파괴 예약된 경우: 손에 돌아온 지금 파괴한다.
	// (비행 중 파괴하면 궤적이 끊겨 어색하므로 회수 후 파괴로 처리)
	if (bPendingDestroyOnCatch)
	{
		Destroy();
		return;
	}

	// 전 클라에서 손 소켓 재부착 + 장착 상태 복원
	MulticastCatch();
}

// [BOOMERANG-005] 잡힘 재부착/연출 (모든 클라).
//   주의: CarryingComponent::EquipItem은 무게를 다시 더하므로(좌클릭 발사라 손에서 논리적으로
//   내려놓은 적이 없음) 사용하지 않는다. 여기서는 소켓 부착 + 장착 상태만 복원한다.
void ABoomerang::MulticastCatch_Implementation()
{
	if (MeshComponent)
	{
		// 장착 상태: 물리 off, 충돌 QueryOnly
		MeshComponent->SetSimulatePhysics(false);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeshComponent->ClearMoveIgnoreActors();
	}

	// 소유자 캐릭터 손 소켓에 부착
	if (ACharacter* OwnerCharacter = Cast<ACharacter>(LastOwner))
	{
		if (USkeletalMeshComponent* OwnerMesh = OwnerCharacter->GetMesh())
		{
			AttachToComponent(OwnerMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, CatchSocketName);
		}
	}

	// 잡힘 연출 훅 (VFX/사운드)
	OnCaught();
}

// [BOOMERANG-006] 던짐 연출 (모든 클라).
void ABoomerang::MulticastThrown_Implementation()
{
	OnThrown();
}
