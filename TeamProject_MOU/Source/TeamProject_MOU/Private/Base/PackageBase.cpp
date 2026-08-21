#include "Base/PackageBase.h"
#include "Base/CharacterBase.h"
#include "Base/BaseAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/CarryingComponent.h"
#include "Components/CapsuleComponent.h"
#include "Player/MainCharacter.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Ability/GA_CoopCarry.h"

APackageBase::APackageBase()
{
	PrimaryActorTick.bCanEverTick = true;
	bCanBeStoredInInventory = false;

	// 운반을 위한 전방/후방 손잡이 생성 (일반/무거운 물품 공통 기반)
	Handle_Front = CreateDefaultSubobject<USceneComponent>(TEXT("Handle_Front"));
	Handle_Front->SetupAttachment(RootComponent);

	Handle_Back = CreateDefaultSubobject<USceneComponent>(TEXT("Handle_Back"));
	Handle_Back->SetupAttachment(RootComponent);
}

void APackageBase::BeginPlay()
{
	Super::BeginPlay();
	
	CurrentSpoilTime = MaxSpoilTime;
}

void APackageBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// 메시에 물리 충돌 이벤트 바인딩
	if (MeshComponent)
	{
		MeshComponent->SetNotifyRigidBodyCollision(true); // Hit 이벤트 발생 허용
		MeshComponent->OnComponentHit.AddDynamic(this, &APackageBase::OnPackageHit);
	}
}

void APackageBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 상하기 쉬운 물품의 경우 유통기한 감소
	if (PackageType == EPackageType::Perishable && CurrentSpoilTime > 0.0f)
	{
		CurrentSpoilTime -= DeltaTime;
		if (CurrentSpoilTime <= 0.0f)
		{
			// 시간 만료: 가치는 GetCurrentValue()에서 동적으로 0 처리되므로
			// 여기서 BaseValue를 강제로 깎을 필요 없음. (단, UI 갱신을 위해 파손 연출이나 OnRep 활용 가능)
		}
	}

	// 서버에서만 운반 로직 수행
	if (HasAuthority())
	{
		// [전환 유예 타이머] 매 프레임 감산
		if (TransitionGracePeriod > 0.0f)
		{
			TransitionGracePeriod = FMath::Max(0.0f, TransitionGracePeriod - DeltaTime);
		}

		// [Heavy 전용] 운반 중 패키지 위치/회전 업데이트
		if (PackageType == EPackageType::Heavy && CurrentCarriers.Num() > 0)
		{
			UpdateHeavyPackagePosition(DeltaTime);
		}

		// [협동 이탈 검사] 전환 유예 중에는 검사 스킵 (강제 드랍 연쇄 방지)
		if (CurrentCarriers.Num() > 0 && TransitionGracePeriod <= 0.0f)
		{
			TArray<AActor*> CarriersToDrop;
			for (AActor* Carrier : CurrentCarriers)
			{
				if (FVector::DistXY(Carrier->GetActorLocation(), GetActorLocation()) > MaxCarryDistance)
				{
					CarriersToDrop.Add(Carrier);
				}
			}

			for (AActor* Carrier : CarriersToDrop)
			{
				if (UCarryingComponent* CarryComp = Carrier->FindComponentByClass<UCarryingComponent>())
				{
					CarryComp->GrabOrDrop();
				}
				else
				{
					RemoveCarrier(Carrier);
				}
				UE_LOG(LogTemp, Warning, TEXT("[%s] 운반자가 너무 멀어져 택배를 놓쳓습니다!"), *Carrier->GetName());
			}
		}

		// [내구도 시스템] 끌림 상태 지속 대미지
		if (!bIsBroken && CurrentCarriers.Num() > 0 && CurrentSpeedRatio < 0.5f)
		{
			DamagePackage(5.0f * DeltaTime);
		}
	}
}

void APackageBase::UpdateHeavyPackagePosition(float DeltaTime)
{
	if (CurrentCarriers.Num() == 0) return;

	AActor* FrontCarrier = CurrentCarriers[0];
	if (!FrontCarrier) return;

	ACharacter* FrontChar = Cast<ACharacter>(FrontCarrier);
	if (!FrontChar || !FrontChar->GetMesh()) return;

	// 소켓이 없을 경우 오른손 본(hand_r)을 직접 사용
	FVector FrontHandPos;
	if (FrontChar->GetMesh()->DoesSocketExist(CarrySocketName))
	{
		FrontHandPos = FrontChar->GetMesh()->GetSocketLocation(CarrySocketName);
	}
	else
	{
		// 소켓이 없으면 캐릭터 위치에서 앞쪽 60cm를 기본값으로 사용
		FrontHandPos = FrontCarrier->GetActorLocation()
			+ FrontCarrier->GetActorForwardVector() * 60.0f
			+ FVector(0, 0, 30.0f);
	}

	// Handle_Front의 로컬 오프셋 (이 값만큼 패키지 원점을 역방향으로 밀어야 함)
	FVector HFLocal = Handle_Front ? Handle_Front->GetRelativeLocation() : FVector::ZeroVector;

	if (CurrentCarriers.Num() == 1)
	{
		// [1인 드래그 운반] DragSocket(등/어깨) 기준으로 패키지 부착
		FVector DragPos;
		if (FrontChar->GetMesh()->DoesSocketExist(DragSocketName))
		{
			DragPos = FrontChar->GetMesh()->GetSocketLocation(DragSocketName);
		}
		else
		{
			// DragSocket 없으면 캐릭터 뒤쪽 어깨 높이 위치로 폴백
			DragPos = FrontCarrier->GetActorLocation()
				- FrontCarrier->GetActorForwardVector() * 30.0f  // 뒤쪽
				+ FVector(0, 0, 80.0f);  // 어깨 높이
		}

		// [오프셋 제거] 아까 추가했던 좌우 보정(SideOffset)이 오히려 중앙 정렬을 방해했으므로 완전히 제거합니다.
		// DragPos는 캐릭터의 소켓 위치를 그대로 사용합니다.

		// 핵심 수정: 택배가 캐릭터 뒤쪽으로 뻗도록 Yaw를 180도 뒤집습니다. (그림 2 방향 복구)
		FRotator DragRot = FrontCarrier->GetActorRotation();
		DragRot.Yaw += 180.0f;  // 캐릭터 등 뒤로 끌리도록 회전 복구!
		DragRot.Pitch = -20.0f; // 손잡이(앞)는 캐릭터에, 꼬리(뒤)는 바닥으로 처지는 각도

		// DragSocket 위치에서 Handle_Front 오프셋만큼 역방향으로 밀어 패키지 원점 계산
		FQuat PackageQuat = DragRot.Quaternion();
		FVector RotatedHF = PackageQuat.RotateVector(HFLocal);
		FVector NewPackageLoc = DragPos - RotatedHF;

		SetActorLocationAndRotation(NewPackageLoc, DragRot, false, nullptr, ETeleportType::None);
	}
	else if (CurrentCarriers.Num() >= 2)
	{
		// [2인 운반] Handle_Front = 앞사람 손, Handle_Back = 뒷사람 손
		AActor* BackCarrier = CurrentCarriers[1];
		if (!BackCarrier) return;

		ACharacter* BackChar = Cast<ACharacter>(BackCarrier);
		if (!BackChar || !BackChar->GetMesh()) return;

		FVector BackHandPos;
		if (BackChar->GetMesh()->DoesSocketExist(CarrySocketName))
		{
			BackHandPos = BackChar->GetMesh()->GetSocketLocation(CarrySocketName);
		}
		else
		{
			BackHandPos = BackCarrier->GetActorLocation()
				+ BackCarrier->GetActorForwardVector() * 60.0f
				+ FVector(0, 0, 30.0f);
		}

		// 패키지 회전: 앞사람 손 → 뒷사람 손 방향
		FVector Dir = BackHandPos - FrontHandPos;
		if (Dir.IsNearlyZero()) return;

		// 핸들 간의 로컬 방향 벡터 (Front -> Back)
		FVector HBLocal = Handle_Back ? Handle_Back->GetRelativeLocation() : FVector(-100, 0, 0);
		// HFLocal은 이미 함수 상단에 선언되었으므로 재선언하지 않고 사용하거나, 
		// 상단의 값과 동일하므로 그대로 유지하면서 지역변수 재선언을 방지합니다.
		FVector LocalDir = HBLocal - HFLocal;
		if (LocalDir.IsNearlyZero()) LocalDir = FVector(-1, 0, 0); // 폴백

		// 핵심: 택배의 로컬 Handle방향(LocalDir)이 월드의 손 방향(Dir)과 일치하도록 회전값 계산!
		FQuat PackageQuat = FQuat::FindBetweenVectors(LocalDir, Dir);
		FRotator PackageRot = PackageQuat.Rotator();

		// Handle_Front 월드 위치 = FrontHandPos 가 되도록 패키지 원점 계산
		FVector RotatedHF = PackageQuat.RotateVector(HFLocal);
		FVector NewPackageOrigin = FrontHandPos - RotatedHF;

		SetActorLocationAndRotation(NewPackageOrigin, PackageRot, false, nullptr, ETeleportType::None);
	}
}

void APackageBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APackageBase, CurrentCarriers);
	DOREPLIFETIME(APackageBase, bIsBroken);  // 파손 상태 복제: 클라이언트 접속 시에도 동기화
}

void APackageBase::OnRep_CurrentCarriers()
{
	// 클라이언트에서 운반자 목록이 변경될 때 수행할 로직 (필요시 추가)
}

void APackageBase::OnRep_bIsBroken()
{
	// 서버에서 bIsBroken이 true로 바뀌면 클라이언트에서 이 함수가 자동 호출됨
	if (bIsBroken)
	{
		// 블루프린트에서 구현한 파손 메시 교체 / 파티클 등 연출 실행
		OnPackageBroken();
	}
}

void APackageBase::OnUse_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[%s] 택배는 사용할 수 없습니다! (옮기기만 가능)"), *GetName());
}

void APackageBase::MulticastPickUp_Implementation(AActor* Picker)
{
	Super::MulticastPickUp_Implementation(Picker);

	// 무거운 택배는 서버와 모든 클라이언트에서 상호작용은 유지하되 물리 및 캐릭터 충돌을 끕니다. (동기화 불일치/떨림 방지)
	if (PackageType == EPackageType::Heavy)
	{
		MeshComponent->SetSimulatePhysics(false);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
}

void APackageBase::MulticastDrop_Implementation(FVector DropLocation, AActor* Dropper)
{
	Super::MulticastDrop_Implementation(DropLocation, Dropper);

	// 내려놓을 때는 서버와 모든 클라이언트에서 충돌을 정상 복구합니다.
	if (PackageType == EPackageType::Heavy)
	{
		MeshComponent->SetSimulatePhysics(true);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}
}

void APackageBase::AddCarrier(AActor* Carrier)
{
	if (!Carrier || CurrentCarriers.Contains(Carrier)) return;

	if (PackageType == EPackageType::Heavy && CurrentCarriers.Num() >= 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] 무거운 택배는 최대 2명까지 운반 가능합니다."), *GetName());
		return;
	}

	if (CurrentCarriers.Num() == 0)
	{
		// 1인 픽업 시점의 물리 처리는 MulticastPickUp_Implementation에서 클라이언트 동기화와 함께 처리됩니다.
	}
	else if (CurrentCarriers.Num() == 1 && PackageType == EPackageType::Heavy)
	{
		AActor* FrontCarrier = CurrentCarriers[0];

		// [수정] Handle_Back의 월드 위치는 패키지 Pitch(-20도) 기울기의 영향을 받아 하늘/땅으로 오차 발생.
		// 따라서 현재 기울어진 Handle_Back 위치를 직접 사용하지 않고,
		// 첫 번째 운반자(FrontCarrier) → 패키지 중심(XY) → 반대편 연장선 상에서 위치를 계산합니다.
		const FVector PackageCenterXY = FVector(GetActorLocation().X, GetActorLocation().Y, 0.0f);
		const FVector FrontXY = FVector(FrontCarrier->GetActorLocation().X, FrontCarrier->GetActorLocation().Y, 0.0f);

		const FVector FrontToCenter = PackageCenterXY - FrontXY;
		if (FrontToCenter.IsNearlyZero()) return;

		// 첫 번째 운반자로부터 패키지 중심까지의 거리만큼 반대편으로 연장 + 캐릭터 서있을 여유거리
		const float HandleDist = FrontToCenter.Size();
		const FVector OppositeDir = FrontToCenter.GetSafeNormal();
		constexpr float StandOffDistance = 60.0f;

		// 최종 텔레포트 위치: XY는 반대편 연장선, Z는 첫 번째 운반자와 동일한 높이 유지
		FVector FinalLocation;
		FinalLocation.X = PackageCenterXY.X + OppositeDir.X * (HandleDist + StandOffDistance);
		FinalLocation.Y = PackageCenterXY.Y + OppositeDir.Y * (HandleDist + StandOffDistance);
		FinalLocation.Z = FrontCarrier->GetActorLocation().Z;

		// 첫 번째 운반자를 바라보는 방향으로 Yaw 회전
		FVector DirToFront = FrontCarrier->GetActorLocation() - FinalLocation;
		DirToFront.Z = 0.0f;
		const FRotator FaceRotation = DirToFront.IsNearlyZero()
			? Carrier->GetActorRotation()
			: DirToFront.Rotation();

		Carrier->SetActorLocationAndRotation(FinalLocation, FaceRotation, false, nullptr, ETeleportType::TeleportPhysics);

		// 첫 번째 운반자도 즉시 2번째 운반자 방향을 바라보게 회전
		FVector DirToBack = FinalLocation - FrontCarrier->GetActorLocation();
		DirToBack.Z = 0.0f;
		if (!DirToBack.IsNearlyZero())
		{
			FrontCarrier->SetActorRotation(DirToBack.Rotation());
		}
	}

	CurrentCarriers.Add(Carrier);
	UpdateCarriersSpeedModifier();
}

void APackageBase::RemoveCarrier(AActor* Carrier)
{
	if (!Carrier || !CurrentCarriers.Contains(Carrier)) return;

	// 무게 원상복구
	if (AppliedWeightMap.Contains(Carrier))
	{
		if (ACharacterBase* BaseChar = Cast<ACharacterBase>(Carrier))
		{
			if (UBaseAttributeSet* AttrSet = BaseChar->BaseAttribute)
			{
				AttrSet->SetCurrentWeight(FMath::Max(0.0f, AttrSet->GetCurrentWeight() - AppliedWeightMap[Carrier]));
			}
		}
		AppliedWeightMap.Remove(Carrier);
	}

	// 기존 개별 무시(IgnoreActorWhenMoving) 해제 로직은 제거 (이제 채널 단위 무시를 사용하므로)

	CurrentCarriers.Remove(Carrier);
	UpdateCarriersSpeedModifier();

	// [2→1 전환] 다음 Tick 이전에 즉시 1인 드래그 위치로 선점 배치 (떨림 방지)
	// UpdateHeavyPackagePosition()의 1인 로직과 동일한 계산을 RemoveCarrier 시점에 미리 수행합니다.
	if (CurrentCarriers.Num() == 1 && PackageType == EPackageType::Heavy)
	{
		// [제안 B] 0.2초 유예 기간 설정: 전환 직후 이탈 검사 스킵 (강제 드랍 연쇄 방지)
		TransitionGracePeriod = 0.2f;

		AActor* RemainingCarrier = CurrentCarriers[0];
		if (ACharacter* RemainingChar = Cast<ACharacter>(RemainingCarrier))
		{
			FVector DragPos;
			if (RemainingChar->GetMesh()->DoesSocketExist(DragSocketName))
			{
				DragPos = RemainingChar->GetMesh()->GetSocketLocation(DragSocketName);
			}
			else
			{
				DragPos = RemainingCarrier->GetActorLocation()
					- RemainingCarrier->GetActorForwardVector() * 30.0f
					+ FVector(0, 0, 80.0f);
			}

			FRotator DragRot = RemainingCarrier->GetActorRotation();
			DragRot.Yaw += 180.0f;
			DragRot.Pitch = -20.0f;

			const FVector HFLocal = Handle_Front ? Handle_Front->GetRelativeLocation() : FVector::ZeroVector;
			const FVector NewPackageLoc = DragPos - DragRot.Quaternion().RotateVector(HFLocal);

			SetActorLocationAndRotation(NewPackageLoc, DragRot, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	// 아무도 들고 있지 않게 되면 물리 및 충돌 재활성 (클라이언트 동기화를 위해 MulticastDrop이 담당함)
	if (CurrentCarriers.Num() == 0)
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
}

void APackageBase::UpdateCarriersSpeedModifier()
{
	if (ItemWeight <= 0.0f)
	{
		return;
	}

	float WeightPerCarrier = CurrentCarriers.Num() > 0 ? (ItemWeight / CurrentCarriers.Num()) : 0.0f;

	// 참여한 모든 플레이어에게 무게 균등 분배
	for (AActor* Carrier : CurrentCarriers)
	{
		if (ACharacterBase* BaseChar = Cast<ACharacterBase>(Carrier))
		{
			if (UBaseAttributeSet* AttrSet = BaseChar->BaseAttribute)
			{
				// 기존에 분배된 무게가 있다면 일단 차감 (재분배를 위함)
				if (AppliedWeightMap.Contains(Carrier))
				{
					AttrSet->SetCurrentWeight(FMath::Max(0.0f, AttrSet->GetCurrentWeight() - AppliedWeightMap[Carrier]));
				}

				// 새로운 N분의 1 무게 부여
				AttrSet->SetCurrentWeight(AttrSet->GetCurrentWeight() + WeightPerCarrier);
				AppliedWeightMap.Add(Carrier, WeightPerCarrier);
			}
		}
	}

	CurrentSpeedRatio = 1.0f;
}

void APackageBase::OnPackageHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// 캐릭터가 밟거나 가볍게 부딪히는 것은 무시 (물리적 낙하/충돌만 취급)
	if (bIsBroken || OtherActor == this)
	{
		return;
	}

	// 충돌 순간의 물리 속도(충격량)를 구함
	float ImpactSpeed = HitComponent->GetComponentVelocity().Size();

	// 플레이어(MainCharacter)와 부딪혔을 경우 넉다운 처리 검사
	if (AMainCharacter* HitPlayer = Cast<AMainCharacter>(OtherActor))
	{
		// 임팩트 속도가 설정된 넉다운 기준치 이상일 때만 기절(Knockdown) 처리
		if (ImpactSpeed >= KnockdownThresholdSpeed)
		{
			HitPlayer->Knockdown();
			UE_LOG(LogTemp, Warning, TEXT("[%s] 플레이어를 세게 타격하여 기절시켰습니다! 속도: %f"), *GetName(), ImpactSpeed);
		}
		
		// 플레이어와의 충돌에서는 패키지 자체의 내구도 감소는 진행하지 않음(기획에 따라 변경 가능)
		return;
	}

	// 일반 ACharacterBase와의 충돌 무시 (위에서 MainCharacter를 걸러냈으므로 남은 건 NPC 등)
	if (Cast<ACharacterBase>(OtherActor))
	{
		return;
	}

	// 500 이상의 속도로 부딪혔을 때만 대미지 처리 (살짝 내려놓는 것은 100~300 내외)
	if (ImpactSpeed > 500.0f)
	{
		// 충격 속도에 비례하여 대미지 계산 (예: 1000 속도면 10 대미지)
		float CalculatedDamage = (ImpactSpeed - 500.0f) * 0.02f;
		if (CalculatedDamage > 1.0f)
		{
			DamagePackage(CalculatedDamage);
			UE_LOG(LogTemp, Warning, TEXT("[%s] 강한 충돌 감지! 속도: %f -> 대미지: %f"), *GetName(), ImpactSpeed, CalculatedDamage);
		}
	}
}

void APackageBase::DamagePackage(float DamageAmount)
{
	if (bIsBroken || DamageAmount <= 0.0f)
	{
		return;
	}

	// [중요] DamagePackage는 서버에서만 실행해야 함.
	// 클라이언트 물리 Hit 이벤트로 실수로 호출되는 것을 방지.
	if (!HasAuthority())
	{
		return;
	}

	CurrentDurability -= DamageAmount;
	
	// 서버 자신(로컬)의 UI도 즉시 갱신되도록 수동으로 OnRep 호출
	OnRep_CurrentDurability();
	
	// 파손 처리
	if (CurrentDurability <= 0.0f)
	{
		CurrentDurability = 0.0f;
		bIsBroken = true;  // [Replicated] 이 값이 바뀌면 클라이언트의 OnRep_bIsBroken이 자동 호출됨
		
		UE_LOG(LogTemp, Error, TEXT("[%s] 택배 파손! 가치가 0원이 되었습니다."), *GetName());
		
		// 서버 자신도 연출 실행 (OnRep은 서버에서는 호출 안 되므로 직접 호출)
		OnPackageBroken();
	}
}

int32 APackageBase::GetCurrentValue() const
{
	// 1. 상하기 쉬운 음식이고, 유통기한이 다 지났다면 가치 0원
	if (PackageType == EPackageType::Perishable && CurrentSpoilTime <= 0.0f)
	{
		return 0;
	}

	// 2. 파손된 경우 0원
	if (bIsBroken || CurrentDurability <= 0.0f)
	{
		return 0;
	}

	// 3. 내구도 비례 가치 계산
	if (MaxDurability > 0.0f)
	{
		float DurabilityRatio = FMath::Clamp(CurrentDurability / MaxDurability, 0.0f, 1.0f);
		return FMath::RoundToInt(BaseValue * DurabilityRatio);
	}

	return BaseValue;
}

void APackageBase::MulticastOnPackageBroken_Implementation()
{
	// 블루프린트 이벤트를 모든 클라이언트에서 실행 (메시 교체, 파티클 등)
	// OnRep_bIsBroken에서도 호출되므로 혹시 모를 이중 호출에 주의
	OnPackageBroken();
}



float APackageBase::GetPushResistance_Implementation() const
{
	return ItemWeight;
}

void APackageBase::Push_Implementation(AActor* Pusher, FVector PushDirection)
{
	// 바닥에 놓여있을 때만 밀기 가능
	if (CurrentCarriers.Num() == 0)
	{
		// 블루프린트나 C++에서 Lerp 또는 RootMotion 기반 이동 로직을 추가
		// 물리 기반 임펄스 대신 정해진 애니메이션과 거리만큼 이동하게 할 예정
		UE_LOG(LogTemp, Log, TEXT("택배가 %s 방향으로 밀렸습니다! 저항: %f"), *PushDirection.ToString(), ItemWeight);
	}
}
