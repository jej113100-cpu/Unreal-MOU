#include "Item/TaserGun.h"
#include "Base/CharacterBase.h"
#include "Components/StatusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "AbilitySystemBlueprintLibrary.h" // SendGameplayEventToActor
#include "DrawDebugHelpers.h" // [DEBUG-TASER] 확인용 임시

ATaserGun::ATaserGun()
{
	// 최대 내구도 100, 발사당 25 차감(GetDurabilityCostPerUse) → 4회 발사 가능.
	MaxDurability = 100.0f;
	CurrentDurability = MaxDurability;

	// 총구 지점 컴포넌트 (VFX 시작 위치용). BP에서 메시 총구로 이동시킴
	MuzzlePoint = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzlePoint"));
	MuzzlePoint->SetupAttachment(MeshComponent);
}

// [TASER-001] 발사 override: 카메라 조준 방향으로 피아식별 트레이스 + VFX
// (좌클릭→횟수차감→서버권한 분기는 부모 WeaponItemBase::OnUse에서 처리)
void ATaserGun::Fire()
{
	// 트레이스는 든 플레이어의 카메라 시점(화면 정중앙) 기준으로 발사한다.
	// 총구 방향이 아니라 조준 방향이므로 메시 회전과 무관하게 정확히 조준된다.
	FVector ViewLocation = GetActorLocation();
	FRotator ViewRotation = GetActorRotation();

	APawn* OwnerPawn = Cast<APawn>(LastOwner);
	if (OwnerPawn && OwnerPawn->GetController())
	{
		OwnerPawn->GetController()->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	else if (LastOwner)
	{
		// 컨트롤러가 없으면 액터 시점으로 폴백
		LastOwner->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	}

	const FVector TraceStart = ViewLocation;
	const FVector TraceEnd = TraceStart + ViewRotation.Vector() * TraceDistance;

	// 피아식별 트레이스는 부모(WeaponItemBase)에 위임.
	// TargetTeam 채널로만 트레이스하고, 맞으면 ApplyWeaponHit(=기절)을 내부에서 호출.
	FHitResult Hit;
	const bool bHit = FireHitscan(TraceStart, TraceEnd, Hit);

	// [DEBUG-TASER] 트레이스 선 시각화 (맞으면 초록/빨강, 히트 지점에 구) - 확인 후 제거
	DrawDebugLine(GetWorld(), TraceStart, TraceEnd, bHit ? FColor::Green : FColor::Red, false, 2.0f, 0, 1.5f);
	if (bHit)
	{
		DrawDebugSphere(GetWorld(), Hit.ImpactPoint, 15.0f, 12, FColor::Yellow, false, 2.0f);
		UE_LOG(LogTemp, Warning, TEXT("[TASER] Hit Actor: %s"), *GetNameSafe(Hit.GetActor()));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[TASER] Miss (nothing hit)"));
	}

	// VFX 시작점은 총구(MuzzlePoint), 끝점은 트레이스 도착지점(히트면 히트, 아니면 최대거리)
	const FVector FxStart = MuzzlePoint ? MuzzlePoint->GetComponentLocation() : TraceStart;
	const FVector FxEnd = bHit ? Hit.ImpactPoint : TraceEnd;
	MulticastPlayFireEffect(FxStart, FxEnd, bHit);
}

// [TASER-006] 무기 공통 히트 처리 override: 맞은 캐릭터에 감전 태그 부여 + 타이머로 해제 예약
void ATaserGun::ApplyWeaponHit_Implementation(AActor* HitActor, const FHitResult& Hit)
{
	ACharacterBase* TargetCharacter = Cast<ACharacterBase>(HitActor);
	if (!TargetCharacter)
	{
		// [DEBUG-TASER] 맞은 대상이 ACharacterBase가 아님 - 확인 후 제거
		UE_LOG(LogTemp, Warning, TEXT("[TASER] Hit but NOT ACharacterBase: %s"), *GetNameSafe(HitActor));
		return;
	}

	UStatusComponent* StatusComp = TargetCharacter->GetStatusComponent();
	if (!StatusComp)
	{
		// [DEBUG-TASER] StatusComponent 없음 - 확인 후 제거
		UE_LOG(LogTemp, Warning, TEXT("[TASER] Target has NO StatusComponent: %s"), *GetNameSafe(HitActor));
		return;
	}

	// 감전 발동 이벤트 태그 (GA_CC_Electric의 Trigger. 철자 'Eletric' 그대로여야 매칭됨)
	// 이 이벤트를 받으면 대상 ASC에 Grant된 GA_CC_Electric이 발동되어
	// GE 적용 + 감전 몽타주 재생 + 지속시간 후 자동 해제까지 모두 처리한다.
	static const FGameplayTag ElectricEventTag = FGameplayTag::RequestGameplayTag(FName("Event.Reaction.Eletric"), false);
	if (!ElectricEventTag.IsValid())
	{
		// [DEBUG-TASER] Event.Reaction.Eletric 태그가 프로젝트에 정의 안 됨 - 확인 후 제거
		UE_LOG(LogTemp, Warning, TEXT("[TASER] EventTag 'Event.Reaction.Eletric' is INVALID (not registered)"));
		return;
	}

	// 감전 GA 발동 (지속시간/해제/애니는 GA가 담당. 던진 주체를 Instigator로 전달)
	FGameplayEventData EventData;
	EventData.EventTag = ElectricEventTag;
	EventData.Instigator = LastOwner;       // 발사한 플레이어
	EventData.Target = TargetCharacter;     // 감전당하는 대상
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(TargetCharacter, ElectricEventTag, EventData);

	// [DEBUG-TASER] 이벤트 전송 로그 + 화면 메시지 - 확인 후 제거
	UE_LOG(LogTemp, Warning, TEXT("[TASER] ELECTRIC event sent to %s"), *GetNameSafe(TargetCharacter));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan,
			FString::Printf(TEXT("ELECTRIC event -> %s"), *GetNameSafe(TargetCharacter)));
	}
}

// [TASER-005] 발사 이펙트 훅 - 모든 클라이언트에서 BP VFX 이벤트 재생
void ATaserGun::MulticastPlayFireEffect_Implementation(FVector Start, FVector End, bool bHit)
{
	OnFireEffect(Start, End, bHit);
}
