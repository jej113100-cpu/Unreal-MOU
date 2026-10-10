#include "Item/PotionItem.h"
#include "Base/CharacterBase.h"
#include "Player/MainCharacter.h"
#include "Components/StatusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CarryingComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemBlueprintLibrary.h" // SendGameplayEventToActor
#include "GameplayEffect.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"
#include "TeamProject_MOU.h" // LogTeamProject_MOU
#include "Engine/Engine.h"   // GEngine->AddOnScreenDebugMessage
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

// [POTION-DEBUG] 화면 + 로그 동시 출력용 임시 매크로 (원인 파악 후 제거)
#define POTION_DEBUG(Color, Fmt, ...) \
	do { \
		UE_LOG(LogTeamProject_MOU, Warning, TEXT("[Potion] " Fmt), ##__VA_ARGS__); \
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 6.0f, Color, FString::Printf(TEXT("[Potion] " Fmt), ##__VA_ARGS__)); } \
	} while (0)

// [POTION-000] 포션 공통 설정과 작은 메시의 바닥 관통 방지용 CCD를 초기화한다.
APotionItem::APotionItem()
{
	PrimaryActorTick.bCanEverTick = true;

	// 작은 포션 메시가 중력이나 겹친 물리 바디의 반발로 빠르게 이동할 때도
	// 바닥 충돌을 건너뛰지 않도록 연속 충돌 감지를 사용한다.
	if (MeshComponent)
	{
		MeshComponent->SetUseCCD(true);
	}

	// 포션은 자기 자신에게 효과를 준다 (치료 도구 등은 FocusedTarget으로 override)
	TargetMode = EConsumeTarget::SelfOnly;

	RollingAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("RollingAudioComponent"));
	RollingAudioComponent->SetupAttachment(MeshComponent);
	RollingAudioComponent->bAutoActivate = false;

	static ConstructorHelpers::FObjectFinder<USoundBase> BreakSoundFinder(
		TEXT("/Game/04_JJO/Sound/SFX_BreakGlass.SFX_BreakGlass"));
	if (BreakSoundFinder.Succeeded())
	{
		BreakGlassSound = BreakSoundFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<USoundBase> RollSoundFinder(
		TEXT("/Game/04_JJO/Sound/SFX_RollGlass.SFX_RollGlass"));
	if (RollSoundFinder.Succeeded())
	{
		RollGlassSound = RollSoundFinder.Object;
		RollingAudioComponent->SetSound(RollGlassSound);
	}
}

// [POTION-016] Q 투척으로 바닥에 닿은 포션의 이동 속도에 따라 굴림음 볼륨과 재생 상태를 갱신한다.
void APotionItem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bRollingSoundEnabled || !RollingAudioComponent || !MeshComponent)
	{
		return;
	}

	const float Speed = MeshComponent->GetPhysicsLinearVelocity().Size();
	if (!MeshComponent->IsSimulatingPhysics() || Speed < RollSoundMinSpeed)
	{
		RollingBelowSpeedTime += DeltaTime;
		if (RollingBelowSpeedTime >= 0.2f)
		{
			RollingAudioComponent->Stop();
			bRollingSoundEnabled = false;
		}
		return;
	}

	RollingBelowSpeedTime = 0.0f;
	const float SpeedAlpha = FMath::GetMappedRangeValueClamped(
		FVector2D(RollSoundMinSpeed, FMath::Max(RollSoundMinSpeed + 1.0f, RollSoundFullSpeed)),
		FVector2D(0.2f, 1.0f), Speed);
	RollingAudioComponent->SetVolumeMultiplier(RollGlassVolume * SpeedAlpha);
	RollingAudioComponent->SetPitchMultiplier(FMath::Lerp(0.85f, 1.1f, SpeedAlpha));

	// 원본 SoundWave가 루프 설정이 아니어도 계속 구르는 동안 다시 재생한다.
	if (!RollingAudioComponent->IsPlaying() && RollGlassSound)
	{
		RollingAudioComponent->Play();
	}
}

// [POTION-012] 메시 피벗과 BP 보정값을 그대로 오른손 소켓에 맞춘다.
bool APotionItem::ShouldCenterOnCarrySocket() const
{
	return false;
}

// [POTION-013] 포션 블루프린트에 설정된 오른손 소켓 이름을 반환한다.
FName APotionItem::GetCarrySocketOverride() const
{
	return HandSocketName;
}

// [POTION-014] 포션 블루프린트의 손 장착 위치 보정값을 반환한다.
FVector APotionItem::GetCarryLocationOffset() const
{
	return HandLocationOffset;
}

// [POTION-015] 포션 블루프린트의 손 장착 회전 보정값을 반환한다.
FRotator APotionItem::GetCarryRotationOffset() const
{
	return HandRotationOffset;
}

// [POTION-001] 소비 효과: 자기 사용 시 대상 하나(자신)에게 적용
// (부모 TryConsumeOnServer에서 서버 권한으로만 호출됨)
void APotionItem::ApplyEffect_Implementation()
{
	// 효과 대상 (SelfOnly면 든 플레이어 = LastOwner)
	ApplyPotionEffectToTarget(ResolveEffectTarget());
}

// [POTION-004] 좌클릭: bApplyOnImpact 값으로 "던지기(사용) vs 제자리 마시기" 분기
void APotionItem::OnUse_Implementation()
{
	// 투척형 포션(Apply on Impact 켜짐): 좌클릭 = 손에서 던지기 (충돌 시 터져서 발동)
	if (bApplyOnImpact)
	{
		// 클라이언트에서만 플래그를 세우면 서버의 포션에는 전달되지 않으므로
		// 서버 RPC로 넘겨 서버 권한 인스턴스에서 사용 투척을 시작한다.
		if (!HasAuthority())
		{
			ServerThrowAsUse();
			return;
		}

		AActor* OwnerActor = GetOwner();
		if (!OwnerActor)
		{
			OwnerActor = GetAttachParentActor();
		}

		UCarryingComponent* Carrying = OwnerActor
			? OwnerActor->FindComponentByClass<UCarryingComponent>()
			: nullptr;
		if (!Carrying || Carrying->GetCarriedActor() != this)
		{
			POTION_DEBUG(FColor::Red, "사용 투척 실패: 소유자의 손에 든 포션을 찾지 못함");
			return;
		}

		// 연타로 몽타주가 다시 시작되거나 투척 요청이 중복되는 것을 막는다.
		if (bWaitingForThrowNotify)
		{
			return;
		}

		bWaitingForThrowNotify = true;
		if (UseMontage)
		{
			// 실제 투척은 몽타주의 UAnimNotify_PotionThrow가 호출할 때 실행한다.
			MulticastPlayThrowMontage(Cast<ACharacterBase>(OwnerActor));
		}
		else
		{
			// 몽타주가 지정되지 않은 포션은 사용 불능 상태가 되지 않도록 즉시 투척한다.
			ExecutePendingImpactThrow();
		}
		return;
	}

	// 일반 포션(Apply on Impact 꺼짐): 기존대로 제자리에서 마신다
	Super::OnUse_Implementation();
}

// [POTION-005] 클라이언트 좌클릭을 서버 권한의 사용 투척으로 다시 실행한다.
void APotionItem::ServerThrowAsUse_Implementation()
{
	OnUse_Implementation();
}

// [POTION-006] BP에 지정된 투척 몽타주를 사용 캐릭터에게 네트워크 동기화해 재생한다.
void APotionItem::MulticastPlayThrowMontage_Implementation(ACharacterBase* ThrowerCharacter)
{
	if (ThrowerCharacter && UseMontage)
	{
		ThrowerCharacter->PlayAnimMontage(UseMontage);
	}
}

// [POTION-007] 로컬 소유 캐릭터의 Anim Notify를 서버 권한 투척으로 연결한다.
void APotionItem::HandleThrowAnimNotify(AActor* NotifyOwner)
{
	if (!NotifyOwner || (GetOwner() != NotifyOwner && GetAttachParentActor() != NotifyOwner))
	{
		return;
	}

	if (HasAuthority())
	{
		ExecutePendingImpactThrow();
	}
	else
	{
		ServerConfirmThrowNotify();
	}
}

// [POTION-008] 소유 클라이언트의 투척 Notify를 서버에서 처리한다.
void APotionItem::ServerConfirmThrowNotify_Implementation()
{
	ExecutePendingImpactThrow();
}

// [POTION-009] 서버가 Notify 대기 상태를 소비하고 손에 든 포션을 실제로 던진다.
void APotionItem::ExecutePendingImpactThrow()
{
	if (!HasAuthority() || !bWaitingForThrowNotify)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		OwnerActor = GetAttachParentActor();
	}

	UCarryingComponent* Carrying = OwnerActor
		? OwnerActor->FindComponentByClass<UCarryingComponent>()
		: nullptr;
	if (!Carrying || Carrying->GetCarriedActor() != this)
	{
		bWaitingForThrowNotify = false;
		POTION_DEBUG(FColor::Red, "Notify 투척 실패: 소유자의 손에 든 포션을 찾지 못함");
		return;
	}

	bWaitingForThrowNotify = false;
	bThrowAsUse = true; // 서버에서 설정해야 Throw_Implementation의 충돌 등록 조건이 성립한다.
	Carrying->Throw();

	// Throw가 CanBeDropped 등의 이유로 거부되면 Throw_Implementation이 플래그를 지우지 못하므로 정리한다.
	bThrowAsUse = false;
}

// 실제 GE 적용 + 상태이상 태그 제거를 한 대상에게 수행 (자기 사용 / 광역 공용)
void APotionItem::ApplyPotionEffectToTarget(AActor* Target)
{
	if (!Target)
	{
		return;
	}

	// 대상의 AbilitySystemComponent 획득 (ACharacterBase는 IAbilitySystemInterface 구현)
	UAbilitySystemComponent* TargetASC = nullptr;
	if (IAbilitySystemInterface* AscInterface = Cast<IAbilitySystemInterface>(Target))
	{
		TargetASC = AscInterface->GetAbilitySystemComponent();
	}

	// GameplayEffect 목록 적용 (지속시간/원복/복제는 GAS가 처리)
	if (TargetASC)
	{
		FGameplayEffectContextHandle Context = TargetASC->MakeEffectContext();
		Context.AddSourceObject(this);

		for (const TSubclassOf<UGameplayEffect>& EffectClass : EffectsToApply)
		{
			if (!EffectClass)
			{
				continue;
			}

			FGameplayEffectSpecHandle SpecHandle = TargetASC->MakeOutgoingSpec(EffectClass, EffectLevel, Context);
			if (SpecHandle.IsValid())
			{
				TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			}
		}
	}

	// 이동속도 가감 (신속 +/슬로우 -). GE로 MoveSpeed를 바꾸면 UpdateCharacterSpeed가 덮어쓰므로
	// CharacterBase의 SpeedBuffFlat에 직접 가감하고 지속시간 후 도로 뺀다.
	if (SpeedFlatDelta != 0.0f)
	{
		if (ACharacterBase* TargetCharacter = Cast<ACharacterBase>(Target))
		{
			TargetCharacter->SpeedBuffFlat += SpeedFlatDelta;
			TargetCharacter->UpdateCharacterSpeed(); // 즉시 반영

			// 지속시간 후 원복 (더한 만큼 도로 빼기). 대상 캐릭터 타이머라 포션이 소멸돼도 유지된다.
			if (SpeedBuffDuration > 0.0f)
			{
				TWeakObjectPtr<ACharacterBase> WeakTarget = TargetCharacter;
				const float DeltaToRevert = SpeedFlatDelta;
				FTimerDelegate RevertDelegate = FTimerDelegate::CreateLambda([WeakTarget, DeltaToRevert]()
				{
					if (WeakTarget.IsValid())
					{
						WeakTarget->SpeedBuffFlat -= DeltaToRevert;
						WeakTarget->UpdateCharacterSpeed();
					}
				});
				FTimerHandle TmpHandle;
				TargetCharacter->GetWorldTimerManager().SetTimer(TmpHandle, RevertDelegate, SpeedBuffDuration, false);
			}
		}
	}

	// 상태이상 제거:
	// GA/GE 방식 상태이상은 활성 GameplayEffect가 태그를 계속 부여하므로 GE부터 제거한다.
	// 이후 기존 Loose Tag 방식 상태이상도 StatusComponent를 통해 함께 정리한다.
	if (!TagsToRemove.IsEmpty())
	{
		if (TargetASC)
		{
			TargetASC->RemoveActiveEffectsWithGrantedTags(TagsToRemove);
		}

		if (ACharacterBase* TargetCharacter = Cast<ACharacterBase>(Target))
		{
			if (UStatusComponent* Status = TargetCharacter->GetStatusComponent())
			{
				TArray<FGameplayTag> TagArray;
				TagsToRemove.GetGameplayTagArray(TagArray);
				for (const FGameplayTag& Tag : TagArray)
				{
					Status->RemoveStatusTag(Tag);
				}
			}
		}
	}

	// 상태이상 태그 부여 (감전 등) - 테이저와 동일하게 Loose 태그, 대상 타이머로 해제
	if (!TagsToApply.IsEmpty())
	{
		if (ACharacterBase* TargetCharacter = Cast<ACharacterBase>(Target))
		{
			if (UStatusComponent* Status = TargetCharacter->GetStatusComponent())
			{
				TArray<FGameplayTag> ApplyArray;
				TagsToApply.GetGameplayTagArray(ApplyArray);
				for (const FGameplayTag& Tag : ApplyArray)
				{
					Status->AddStatusTag(Tag);
				}

				// 지속시간 후 해제 예약 (대상 캐릭터 타이머 - 포션이 소멸돼도 유지)
				if (AppliedTagDuration > 0.0f)
				{
					TWeakObjectPtr<UStatusComponent> WeakStatus = Status;
					FGameplayTagContainer TagsCopy = TagsToApply;
					FTimerDelegate ClearDelegate = FTimerDelegate::CreateLambda([WeakStatus, TagsCopy]()
					{
						if (WeakStatus.IsValid())
						{
							TArray<FGameplayTag> ClearArray;
							TagsCopy.GetGameplayTagArray(ClearArray);
							for (const FGameplayTag& T : ClearArray)
							{
								WeakStatus->RemoveStatusTag(T);
							}
						}
					});
					FTimerHandle TmpHandle;
					TargetCharacter->GetWorldTimerManager().SetTimer(TmpHandle, ClearDelegate, AppliedTagDuration, false);
				}
			}
		}
	}

	// 상태이상 발동 이벤트 전송 (정석 방식) - 대상 ASC의 상태이상 GA를 발동시켜
	// GE적용 + 몽타주 재생 + 자동 해제를 GA에 위임한다. (테이저와 동일한 흐름)
	// StatusEventTag가 비어있으면(None) 아무 것도 보내지 않는다.
	if (StatusEventTag.IsValid())
	{
		if (ACharacterBase* TargetCharacter = Cast<ACharacterBase>(Target))
		{
			FGameplayEventData EventData;
			EventData.EventTag = StatusEventTag;
			EventData.Instigator = LastOwner;      // 포션을 사용/투척한 주체
			EventData.Target = TargetCharacter;    // 효과를 받는 대상
			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(TargetCharacter, StatusEventTag, EventData);
		}
	}
}

// [POTION-002] 투척: 사용 투척이면 물리 투척 전에 충돌 감지를 등록한 뒤 부모 투척을 실행한다.
void APotionItem::Throw_Implementation(FVector ThrowVelocity, AActor* Thrower)
{
	const bool bShouldApplyOnImpact = bThrowAsUse && bApplyOnImpact && HasAuthority() && MeshComponent;
	const bool bIsSimpleThrow = !bThrowAsUse;
	if (HasAuthority())
	{
		bSimpleThrowRollingEligible = bIsSimpleThrow;
		if (!bIsSimpleThrow)
		{
			MulticastSetRollingSoundEnabled(false);
		}
	}

	// [POTION-DEBUG] 던지기 진입 시 충돌 발동 조건 상태 출력
	POTION_DEBUG(FColor::Cyan, "Throw 호출됨: bThrowAsUse=%d, bApplyOnImpact=%d, HasAuthority=%d, MeshComponent=%d",
		bThrowAsUse ? 1 : 0, bApplyOnImpact ? 1 : 0, HasAuthority() ? 1 : 0, MeshComponent ? 1 : 0);

	// 좌클릭 "사용" 던지기(bThrowAsUse)일 때만 충돌 발동. Q(단순 투척)는 어떤 포션이든 절대 안 터진다.
	if (bShouldApplyOnImpact)
	{
		bHasImpacted = false;
		MeshComponent->SetNotifyRigidBodyCollision(true); // OnComponentHit 활성화
		MeshComponent->OnComponentHit.AddUniqueDynamic(this, &APotionItem::OnImpact);
		POTION_DEBUG(FColor::Green, "충돌 감지 바인딩 완료 (OnImpact 대기)");
	}
	else
	{
		// bThrowAsUse가 false(Q 투척)이거나 조건 미충족 → 던져도 절대 안 터짐
		POTION_DEBUG(FColor::Red, "충돌 감지 미설정 (Q 투척이거나 조건 미충족)");
	}

	// 부모의 충돌/물리 활성화보다 먼저 위 델리게이트가 등록되어 첫 충돌도 놓치지 않는다.
	Super::Throw_Implementation(ThrowVelocity, Thrower);

	// 다음 던지기를 위해 "사용 발" 플래그 리셋 (Q 투척이 이전 좌클릭 상태를 물려받지 않도록)
	bThrowAsUse = false;
}

// [POTION-017] 포션을 다시 집었을 때 모든 클라이언트에서 굴림음을 즉시 정지한다.
void APotionItem::PickUp_Implementation(AActor* Picker)
{
	if (HasAuthority())
	{
		bSimpleThrowRollingEligible = false;
		MulticastSetRollingSoundEnabled(false);
	}

	Super::PickUp_Implementation(Picker);
}

// [POTION-018] 단순 내려놓기는 Q 투척이 아니므로 굴림음 추적 상태를 해제한다.
void APotionItem::Drop_Implementation(FVector DropLocation, AActor* Dropper)
{
	if (HasAuthority())
	{
		bSimpleThrowRollingEligible = false;
		MulticastSetRollingSoundEnabled(false);
	}

	Super::Drop_Implementation(DropLocation, Dropper);
}

// [POTION-019] Q로 던진 포션이 실제 표면에 처음 닿았을 때부터 굴림음을 활성화한다.
void APotionItem::OnItemHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::OnItemHit(HitComponent, OtherActor, OtherComp, NormalImpulse, Hit);

	if (HasAuthority() && bSimpleThrowRollingEligible && OtherActor != LastThrower.Get())
	{
		MulticastSetRollingSoundEnabled(true);
	}
}

// [POTION-003] 첫 충돌 → 반경 내 플레이어 전원에게 적용 → 깨짐
void APotionItem::OnImpact(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	// [POTION-DEBUG] 충돌 이벤트 자체는 도착했음
	POTION_DEBUG(FColor::Yellow, "OnImpact 진입: Other=%s, HasAuthority=%d, bHasImpacted=%d",
		*GetNameSafe(OtherActor), HasAuthority() ? 1 : 0, bHasImpacted ? 1 : 0);

	if (!HasAuthority() || bHasImpacted)
	{
		POTION_DEBUG(FColor::Red, "OnImpact 조기 반환 (권한 없음 or 이미 발동)");
		return;
	}

	// 던진 본인과 손에서 나가자마자 충돌한 경우 자폭 방지
	if (OtherActor && OtherActor == LastOwner)
	{
		POTION_DEBUG(FColor::Orange, "던진 본인과 충돌 → 무시 (아직 안 터짐, 다음 충돌 대기)");
		return;
	}

	bHasImpacted = true;
	const FVector BreakLocation = Hit.ImpactPoint.IsNearlyZero()
		? GetActorLocation()
		: FVector(Hit.ImpactPoint);
	MulticastPlayBreakGlassSound(BreakLocation);

	// 반경 내 모든 캐릭터(플레이어 + NPC = ACharacterBase 파생)에게 효과 적용
	// ACharacterBase로 필터링하면 AMainCharacter(플레이어)와 NPC 모두 포함된다.
	TArray<AActor*> Overlapped;
	TArray<AActor*> IgnoreActors;
	UKismetSystemLibrary::SphereOverlapActors(
		this, GetActorLocation(), ImpactRadius,
		{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
		ACharacterBase::StaticClass(), IgnoreActors, Overlapped);

	// [POTION-DEBUG] 반경 내 대상 수 (0이면 AMainCharacter가 반경 밖이거나 타입 불일치)
	POTION_DEBUG(FColor::Yellow, "SphereOverlap 결과: %d명 (Radius=%.0f). 0이면 대상 없음",
		Overlapped.Num(), ImpactRadius);

	for (AActor* Actor : Overlapped)
	{
		POTION_DEBUG(FColor::Green, "효과 적용 대상: %s (EffectsToApply=%d개, TagsToApply=%d개)",
			*GetNameSafe(Actor), EffectsToApply.Num(), TagsToApply.Num());
		ApplyPotionEffectToTarget(Actor);
	}

	// 깨짐 연출(전 클라, OnUseEffect BP 훅) 후 소멸
	MulticastPlayUseEffect();
	Destroy();
}

// [POTION-020] 포션이 사용 충돌로 깨지는 순간 모든 클라이언트에서 파손음을 재생한다.
void APotionItem::MulticastPlayBreakGlassSound_Implementation(FVector Location)
{
	if (BreakGlassSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, BreakGlassSound, Location, BreakGlassVolume);
	}
}

// [POTION-021] Q 투척 굴림음의 로컬 재생 상태를 모든 클라이언트에서 동일하게 전환한다.
void APotionItem::MulticastSetRollingSoundEnabled_Implementation(bool bEnabled)
{
	bRollingSoundEnabled = bEnabled;
	RollingBelowSpeedTime = 0.0f;

	if (!bEnabled && RollingAudioComponent)
	{
		RollingAudioComponent->Stop();
	}
}
