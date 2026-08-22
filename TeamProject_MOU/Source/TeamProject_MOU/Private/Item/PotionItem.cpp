#include "Item/PotionItem.h"
#include "Base/CharacterBase.h"
#include "Player/MainCharacter.h"
#include "Components/StatusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemBlueprintLibrary.h" // SendGameplayEventToActor
#include "GameplayEffect.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"
#include "TeamProject_MOU.h" // LogTeamProject_MOU
#include "Engine/Engine.h"   // GEngine->AddOnScreenDebugMessage

// [POTION-DEBUG] 화면 + 로그 동시 출력용 임시 매크로 (원인 파악 후 제거)
#define POTION_DEBUG(Color, Fmt, ...) \
	do { \
		UE_LOG(LogTeamProject_MOU, Warning, TEXT("[Potion] " Fmt), ##__VA_ARGS__); \
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 6.0f, Color, FString::Printf(TEXT("[Potion] " Fmt), ##__VA_ARGS__)); } \
	} while (0)

APotionItem::APotionItem()
{
	// 포션은 자기 자신에게 효과를 준다 (치료 도구 등은 FocusedTarget으로 override)
	TargetMode = EConsumeTarget::SelfOnly;
}

// [POTION-001] 소비 효과: 자기 사용 시 대상 하나(자신)에게 적용
// (부모 TryConsumeOnServer에서 서버 권한으로만 호출됨)
void APotionItem::ApplyEffect_Implementation()
{
	// 효과 대상 (SelfOnly면 든 플레이어 = LastOwner)
	ApplyPotionEffectToTarget(ResolveEffectTarget());
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

	// 상태이상 태그 제거 (감전 State.CC.Electirc 등) - StatusComponent 경유
	if (!TagsToRemove.IsEmpty())
	{
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

// [POTION-002] 투척: 부모(소유권 해제 + 물리 투척) 후, 투척형이면 충돌 감지 켜기
void APotionItem::Throw_Implementation(FVector ThrowVelocity, AActor* Thrower)
{
	Super::Throw_Implementation(ThrowVelocity, Thrower);

	// [POTION-DEBUG] 던지기 진입 시 조건 3개 상태 출력
	POTION_DEBUG(FColor::Cyan, "Throw 호출됨: bApplyOnImpact=%d, HasAuthority=%d, MeshComponent=%d",
		bApplyOnImpact ? 1 : 0, HasAuthority() ? 1 : 0, MeshComponent ? 1 : 0);

	if (bApplyOnImpact && HasAuthority() && MeshComponent)
	{
		bHasImpacted = false;
		MeshComponent->SetNotifyRigidBodyCollision(true); // OnComponentHit 활성화
		MeshComponent->OnComponentHit.AddDynamic(this, &APotionItem::OnImpact);
		POTION_DEBUG(FColor::Green, "충돌 감지 바인딩 완료 (OnImpact 대기)");
	}
	else
	{
		// 셋 중 하나라도 false면 여기 → 던져도 절대 안 터짐
		POTION_DEBUG(FColor::Red, "충돌 감지 미설정! 위 3개 조건 중 0인 것이 원인");
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
