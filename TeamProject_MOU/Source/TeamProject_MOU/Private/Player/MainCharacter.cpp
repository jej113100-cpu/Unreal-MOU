#include "Player/MainCharacter.h"
#include "Components/InteractionComponent.h"
#include "Components/CarryingComponent.h"
#include "Components/StatusComponent.h"
#include "Base/BaseAttributeSet.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Base/ItemBase.h"
#include "Base/PackageBase.h"
#include "Base/EventObjectBase.h"
#include "Components/InventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "Components/CapsuleComponent.h"
#include "Ability/GA_Sprint.h"
#include "Ability/GA_PushObject.h"
#include "Ability/GA_CarryItem.h"
#include "Ability/GA_HeavyCarry.h"
#include "Ability/GA_CoopCarry.h"
#include "Ability/GA_CarryCharacter.h"
#include "Ability/GA_Revive.h"
#include "Ability/GA_ThrowItem.h"
#include "Ability/GA_Emote.h"
#include "Ability/GA_Jump.h"
#include "Ability/GA_EquipSlot.h"
#include "Ability/GA_Interact.h"
#include "Ability/GA_Groggy.h"
#include "Ability/GA_Death.h"

AMainCharacter::AMainCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// 컴포넌트 추가
	InteractionComponent = CreateDefaultSubobject<UInteractionComponent>(TEXT("InteractionComponent"));
	CarryingComponent = CreateDefaultSubobject<UCarryingComponent>(TEXT("CarryingComponent"));
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComponent"));

	// 시야(상호작용) Trace가 캐릭터를 인식할 수 있도록 Visibility 채널 Block 처리
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void AMainCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 눈(얼굴)과 입 메시에 적용된 동적 머티리얼 인스턴스(DMI) 생성 및 캐싱
	TArray<UStaticMeshComponent*> StaticMeshes;
	GetComponents<UStaticMeshComponent>(StaticMeshes);
	for (UStaticMeshComponent* SM : StaticMeshes)
	{
		// 블루프린트에서 만든 컴포넌트 이름에 'Eye' 또는 'Mouth'가 포함되어 있는지 확인
		if (SM->GetName().Contains(TEXT("Eye")) || SM->GetName().Contains(TEXT("Mouth")))
		{
			if (UMaterialInstanceDynamic* DMI = SM->CreateAndSetMaterialInstanceDynamic(0))
			{
				FaceMaterialInstances.Add(DMI);
			}
		}
	}

	if (InteractionComponent)
	{
		InteractionComponent->OnFocusedInteractableChanged.AddDynamic(this, &AMainCharacter::OnFocusedActorChanged);
	}

	if (InventoryComponent)
	{
		InventoryComponent->OnEquipRequested.AddDynamic(this, &AMainCharacter::OnEquipRequested);
	}

	if (HasAuthority() && AbilitySystemComponent)
	{
		TSubclassOf<UGameplayAbility> SprintClass = SprintAbilityClass ? SprintAbilityClass : TSubclassOf<UGameplayAbility>(UGA_Sprint::StaticClass());
		SprintAbilitySpecHandle = AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(SprintClass, 1));

		TSubclassOf<UGameplayAbility> PushClass = PushAbilityClass ? PushAbilityClass : TSubclassOf<UGameplayAbility>(UGA_PushObject::StaticClass());
		PushAbilitySpecHandle = AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(PushClass, 1));

		TSubclassOf<UGameplayAbility> ReviveClass = ReviveAbilityClass ? ReviveAbilityClass : TSubclassOf<UGameplayAbility>(UGA_Revive::StaticClass());
		ReviveAbilitySpecHandle = AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(ReviveClass, 1));

		TSubclassOf<UGameplayAbility> ThrowClass = ThrowAbilityClass ? ThrowAbilityClass : TSubclassOf<UGameplayAbility>(UGA_ThrowItem::StaticClass());
		ThrowAbilitySpecHandle = AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(ThrowClass, 1));

		TSubclassOf<UGameplayAbility> EmoteClass = EmoteAbilityClass ? EmoteAbilityClass : TSubclassOf<UGameplayAbility>(UGA_Emote::StaticClass());
		EmoteAbilitySpecHandle = AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(EmoteClass, 1));

		TSubclassOf<UGameplayAbility> JumpClass = JumpAbilityClass ? JumpAbilityClass : TSubclassOf<UGameplayAbility>(UGA_Jump::StaticClass());
		JumpAbilitySpecHandle = AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(JumpClass, 1));

		TSubclassOf<UGameplayAbility> InteractClass = InteractAbilityClass ? InteractAbilityClass : TSubclassOf<UGameplayAbility>(UGA_Interact::StaticClass());
		InteractAbilitySpecHandle = AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(InteractClass, 1));

		TSubclassOf<UGameplayAbility> GroggyClass = GroggyAbilityClass ? GroggyAbilityClass : TSubclassOf<UGameplayAbility>(UGA_Groggy::StaticClass());
		GroggyAbilitySpecHandle = AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(GroggyClass, 1));

		TSubclassOf<UGameplayAbility> DeathClass = DeathAbilityClass ? DeathAbilityClass : TSubclassOf<UGameplayAbility>(UGA_Death::StaticClass());
		DeathAbilitySpecHandle = AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(DeathClass, 1));
	}
}

void AMainCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 매 프레임 스태미나 소모 및 회복 처리
	UpdateStamina(DeltaTime);

	// [팀원 부활 차징 처리]
	if (bIsHoldingRevive)
	{
		if (!CurrentReviveTarget.IsValid() || !CurrentReviveTarget->bIsGroggy || CurrentReviveTarget->bIsDead)
		{
			CancelReviveHold();
		}
		else
		{
			// 거리 검사 (300 유닛 이상 멀어지면 차징 취소)
			float DistSq = FVector::DistSquared(GetActorLocation(), CurrentReviveTarget->GetActorLocation());
			const float MaxReviveDistSq = FMath::Square(300.0f);

			if (DistSq > MaxReviveDistSq)
			{
				CancelReviveHold();
			}
			else
			{
				CurrentReviveHoldTime += DeltaTime;
				float Progress = GetReviveProgress();

				OnReviveHoldProgress(Progress);
				CurrentReviveTarget->SetReviveProgress(Progress);

				if (CurrentReviveHoldTime >= GetRequiredReviveTime())
				{
					CompleteReviveHold();
				}
			}
		}
	}

	// [밀기 모드] 캐릭터의 실제 이동량만큼 이벤트 오브젝트를 함께 이동시킴
	// Simulated Proxy(다른 클라이언트 화면에 보이는 타 플레이어)는 서버의 복제를 따라가므로 이 로직을 실행하지 않음
	if (bIsPushingMode && CurrentPushedObject && (IsLocallyControlled() || HasAuthority()))
	{
		FVector CurrentLoc = GetActorLocation();
		FVector DeltaMove = CurrentLoc - LastCharacterLocation;
		DeltaMove.Z = 0.0f; // 상하 이동은 무시

		bool bCanMove = true;
		AEventObjectBase* EventObj = Cast<AEventObjectBase>(CurrentPushedObject);
		if (EventObj)
		{
			bCanMove = EventObj->IsReadyToMove();
		}

		if (!bCanMove)
		{
			if (!DeltaMove.IsNearlyZero())
			{
				// 인원 부족 또는 입력 불일치: 애니메이션은 나오지만 캐릭터의 실제 위치는 이전으로 강제 고정
				SetActorLocation(LastCharacterLocation, false);
			}
			return;
		}

		if (bCanMove && !DeltaMove.IsNearlyZero())
		{
			// 협동 밀기 시 여러 명이 동시에 상자 위치를 중복 적용(2배속/N배속 가속)하지 않도록,
			// 첫 번째 푸셔(또는 단독 푸셔)만 상자를 이동시키고 낭떠러지 추락을 검사함
			bool bIsPrimaryPusher = true;
			if (EventObj && EventObj->CurrentPushers.Num() > 0)
			{
				bIsPrimaryPusher = (EventObj->CurrentPushers[0] == this);
			}

			if (bIsPrimaryPusher)
			{
				// 0. 장애물(다른 상자나 벽) 감지 (바닥/경사면은 무시)
				FVector BoxCenter, BoxExtents;
				CurrentPushedObject->GetActorBounds(false, BoxCenter, BoxExtents);
				
				// 바닥 긁힘 방지를 위해 살짝 위(10cm)에서 전방으로 검사
				FVector TraceStart = BoxCenter + FVector(0, 0, 10.0f);
				FVector TraceEnd = TraceStart + DeltaMove;
				
				FCollisionQueryParams BoxParams;
				BoxParams.AddIgnoredActor(CurrentPushedObject);
				BoxParams.AddIgnoredActor(this);
				
				FHitResult BoxHit;
				// 상자보다 아주 살짝 작은 크기의 박스로 Sweep
				bool bHitObstacle = GetWorld()->SweepSingleByChannel(
					BoxHit, TraceStart, TraceEnd, 
					CurrentPushedObject->GetActorQuat(), ECC_Visibility, 
					FCollisionShape::MakeBox(BoxExtents * 0.95f), BoxParams);
					
				// 수직 장애물에 부딪혔을 때만 멈춤 (경사면 무시)
				if (bHitObstacle && FMath::Abs(BoxHit.ImpactNormal.Z) < 0.5f)
				{
					// 전방에 벽이나 다른 상자가 있으므로 캐릭터의 이동 취소
					SetActorLocation(LastCharacterLocation, false);
					return;
				}

				// 1. 경사면 이동 완벽 허용을 위해 Sweep 끄고 이동 적용 (장애물은 위에서 걸러냄)
				CurrentPushedObject->AddActorWorldOffset(DeltaMove, false);

				// 2. 바닥 감지 (낭떠러지 체크)
				FVector TraceStartFall = BoxCenter;
				FVector TraceEndFall = TraceStartFall - FVector(0, 0, BoxExtents.Z + 50.0f); // 상자 바닥에서 50cm 아래 검사
				FHitResult FallHit;
				FCollisionQueryParams FallParams;
				FallParams.AddIgnoredActor(CurrentPushedObject);
				FallParams.AddIgnoredActor(this);

				bool bHitFall = GetWorld()->LineTraceSingleByChannel(FallHit, TraceStartFall, TraceEndFall, ECC_Visibility, FallParams);
				if (!bHitFall)
				{
					// 바닥이 없으면 추락 처리 (서버에서만 판정하여 모두에게 동기화)
					if (HasAuthority())
					{
						EventObj->MulticastFallOffLedge();
					}
				}
			}

			LastCharacterLocation = CurrentLoc;
		}
		else
		{
			LastCharacterLocation = CurrentLoc;
		}
	}

	// [2인 협동 운반] 서로를 바라보도록 캐릭터 회전 고정
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		if (APackageBase* HeavyPackage = Cast<APackageBase>(CarryingComponent->GetCarriedActor()))
		{
			if (HeavyPackage->PackageType == EPackageType::Heavy && HeavyPackage->CurrentCarriers.Num() >= 2)
			{
				if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
				{
					MoveComp->bOrientRotationToMovement = false; // 강제 회전 방지
				}
				
				// 택배(상대방)를 바라보도록 부드럽게 회전
				FVector DirToPackage = HeavyPackage->GetActorLocation() - GetActorLocation();
				DirToPackage.Z = 0.0f; // 상하 회전 방지
				if (!DirToPackage.IsNearlyZero())
				{
					SetActorRotation(FMath::RInterpTo(GetActorRotation(), DirToPackage.Rotation(), DeltaTime, 10.0f));
				}
			}
			else
			{
				// 혼자 들거나 다른 아이템을 들 때는 이동 방향 회전 원상복구
				if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
				{
					MoveComp->bOrientRotationToMovement = true;
				}
			}
		}
	}
	else if (!bIsPushingMode)
	{
		// 물건을 안 들고 있고 밀기 모드도 아닐 때만 원래 이동 방향 회전 복구
		if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		{
			MoveComp->bOrientRotationToMovement = true;
		}
	}
}

void AMainCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMainCharacter, bIsSprinting);
	DOREPLIFETIME(AMainCharacter, bIsStunned);
	DOREPLIFETIME(AMainCharacter, bIsPushingMode);
	DOREPLIFETIME(AMainCharacter, CurrentPushedObject);
	DOREPLIFETIME(AMainCharacter, CurrentPushInput);
	DOREPLIFETIME(AMainCharacter, DownCount);
	DOREPLIFETIME(AMainCharacter, bIsGroggy);
	DOREPLIFETIME(AMainCharacter, bIsDead);
	DOREPLIFETIME(AMainCharacter, bIsReviving);
}

void AMainCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInputComponent)
	{
		return;
	}

	// F키: 상호작용
	if (InteractAction)
	{
		EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &AMainCharacter::OnInteract);
		EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Completed, this, &AMainCharacter::OnInteractEnd);
		EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Canceled, this, &AMainCharacter::OnInteractEnd);
	}

	// E키: 물건 잡기 / 놓기
	if (GrabOrDropAction)
	{
		EnhancedInputComponent->BindAction(GrabOrDropAction, ETriggerEvent::Started, this, &AMainCharacter::OnGrabOrDrop);
	}

	// Q키: 물건 던지기
	if (ThrowAction)
	{
		EnhancedInputComponent->BindAction(ThrowAction, ETriggerEvent::Started, this, &AMainCharacter::OnThrow);
	}

	// Shift키: 달리기
	if (SprintAction)
	{
		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &AMainCharacter::OnSprintStart);
		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &AMainCharacter::OnSprintEnd);
	}
	
	// 좌클릭: 아이템 사용
	if (UseAction)
	{
		EnhancedInputComponent->BindAction(UseAction, ETriggerEvent::Started, this, &AMainCharacter::OnUse);
	}

	// Space키: 점프 (ATeamProject_MOUCharacter 상속 JumpAction 사용)
	if (JumpAction)
	{
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AMainCharacter::OnJumpStartInput);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AMainCharacter::OnJumpEndInput);
	}

	// ` 키 (백틱): 이모트 퀵슬롯 UI 토글
	if (EmoteToggleAction)
	{
		EnhancedInputComponent->BindAction(EmoteToggleAction, ETriggerEvent::Started, this, &AMainCharacter::OnEmoteToggle);
	}

	// 인벤토리 단축키 (1, 2, 3)
	if (Slot1Action)
	{
		EnhancedInputComponent->BindAction(Slot1Action, ETriggerEvent::Started, this, &AMainCharacter::OnSlot1);
	}
	if (Slot2Action)
	{
		EnhancedInputComponent->BindAction(Slot2Action, ETriggerEvent::Started, this, &AMainCharacter::OnSlot2);
	}
	if (Slot3Action)
	{
		EnhancedInputComponent->BindAction(Slot3Action, ETriggerEvent::Started, this, &AMainCharacter::OnSlot3);
	}
}

void AMainCharacter::DoMove(float Right, float Forward)
{
	// 기절, 잡힘, 넉백 등 이동 불가능한 상태 체크
	if (!CanMove())
	{
		return;
	}

	// 밀기 모드 중에는 좌우(Right) 이동 차단. 앞/뒤(W/S)만 허용
	if (bIsPushingMode)
	{
		Right = 0.0f;

		// 입력 상태 동기화 (+1.0: 밀기(전진), -1.0: 당기기(후진), 0.0: 정지)
		float NewInput = 0.0f;
		if (Forward > 0.1f) NewInput = 1.0f;
		else if (Forward < -0.1f) NewInput = -1.0f;

		if (!FMath::IsNearlyEqual(CurrentPushInput, NewInput))
		{
			CurrentPushInput = NewInput;
			if (!HasAuthority())
			{
				ServerSetPushInput(NewInput);
			}
		}

		// 인원수 충족 여부에 관계없이 상시 이동 입력을 주어 헛발질(애니메이션)이 재생되게 함
		// 실제 이동 차단은 Tick에서 수행함
		if (FMath::Abs(Forward) > 0.0f)
		{
			AddMovementInput(LockedPushDirection, Forward);
		}
		
		// Super::DoMove()는 카메라 회전을 기준으로 방향을 잡으므로 생략
		return;
	}

	// [이모트 취소 로직] 이동 키 입력이 들어왔고 현재 재생 중인 이모트가 있다면 즉시 취소
	if ((FMath::Abs(Right) > 0.1f || FMath::Abs(Forward) > 0.1f) && CurrentEmoteMontage != nullptr)
	{
		if (GetMesh() && GetMesh()->GetAnimInstance())
		{
			// 몽타주 정지 시 OnEmoteMontageEnded가 호출되어 표정도 자동으로 0으로 돌아감
			GetMesh()->GetAnimInstance()->Montage_Stop(0.2f, CurrentEmoteMontage);
		}
	}

	Super::DoMove(Right, Forward);
}

void AMainCharacter::OnFocusedActorChanged(AActor* NewFocusedActor)
{
	// 이전 포커스 액터가 아이템이면 UI 숨김
	if (AItemBase* PrevItem = Cast<AItemBase>(PreviousFocusedActor))
	{
		PrevItem->HideItemInfo();
	}

	// [요구사항] 물건을 잡고 있을 때는 UI 표시 안 함
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		PreviousFocusedActor = nullptr;
		if (bIsAimingAtGroggyTarget)
		{
			bIsAimingAtGroggyTarget = false;
			FocusedGroggyTarget = nullptr;
			OnReviveTargetAimChanged(false, nullptr);
		}
		return;
	}

	// 새 포커스 액터가 아이템이면 UI 표시
	if (AItemBase* NewItem = Cast<AItemBase>(NewFocusedActor))
	{
		NewItem->ShowItemInfo();
	}

	// 새 포커스 액터가 그로기 상태의 팀원인지 확인
	if (AMainCharacter* TargetChar = Cast<AMainCharacter>(NewFocusedActor))
	{
		if (TargetChar->bIsGroggy && !TargetChar->bIsDead && TargetChar != this)
		{
			bIsAimingAtGroggyTarget = true;
			FocusedGroggyTarget = TargetChar;
			OnReviveTargetAimChanged(true, TargetChar);
		}
		else if (bIsAimingAtGroggyTarget)
		{
			bIsAimingAtGroggyTarget = false;
			FocusedGroggyTarget = nullptr;
			OnReviveTargetAimChanged(false, nullptr);
		}
	}
	else if (bIsAimingAtGroggyTarget)
	{
		bIsAimingAtGroggyTarget = false;
		FocusedGroggyTarget = nullptr;
		OnReviveTargetAimChanged(false, nullptr);
	}

	PreviousFocusedActor = NewFocusedActor;
}

void AMainCharacter::DoJumpStart()
{
	// 이동 불가능 상태일 때 점프 차단
	if (!CanMove())
	{
		return;
	}

	Super::DoJumpStart();
}

void AMainCharacter::OnInteract()
{
	// 밀기 모드 중이라면 토글(해제) 처리
	if (bIsPushingMode)
	{
		StopPushMode();
		return;
	}

	// 행동 불가능(기절/그로기/사망) 상태 체크
	if (bIsGroggy || bIsDead)
	{
		return;
	}

	if (InteractionComponent)
	{
		// 다른 캐릭터 살리기 케이스: 대상이 그로기 상태의 MainCharacter이면 홀드 차징 시작
		AActor* Focused = InteractionComponent->GetFocusedInteractable();
		if (AMainCharacter* GroggyTarget = Cast<AMainCharacter>(Focused))
		{
			if (GroggyTarget->bIsGroggy && !GroggyTarget->bIsDead && GroggyTarget != this)
			{
				StartReviveHold(GroggyTarget);
				return;
			}
		}

		// 일반 상호작용 (밀기, 퀘스트, 레벨이동, NPC 대화 등)
		if (!CanAct())
		{
			return;
		}
		InteractionComponent->PerformInteraction();
	}
}

void AMainCharacter::OnInteractEnd()
{
	// 상호작용 키를 뗐을 때 부활 차징 중이었다면 취소
	if (bIsHoldingRevive)
	{
		CancelReviveHold();
	}
}

void AMainCharacter::OnGrabOrDrop()
{
	if (!CanAct() || bIsPushingMode)
	{
		return;
	}

	if (CarryingComponent)
	{
		bool bWasCarrying = CarryingComponent->IsCarrying();
		CarryingComponent->GrabOrDrop();
		
		// 들고 있던 물건을 방금 내려놓았다면, 현재 바라보고 있는 대상의 UI를 다시 갱신(표시)
		if (bWasCarrying && !CarryingComponent->IsCarrying() && InteractionComponent)
		{
			OnFocusedActorChanged(InteractionComponent->GetFocusedInteractable());
		}
	}
}

void AMainCharacter::OnThrow()
{
	if (!CanAct() || bIsPushingMode)
	{
		return;
	}

	if (AbilitySystemComponent)
	{
		if (ThrowAbilitySpecHandle.IsValid())
		{
			AbilitySystemComponent->TryActivateAbility(ThrowAbilitySpecHandle);
		}
		else
		{
			static const FGameplayTagContainer ThrowTagContainer(FGameplayTag::RequestGameplayTag(FName("Ability.Player.Throw")));
			AbilitySystemComponent->TryActivateAbilitiesByTag(ThrowTagContainer);
		}
	}
	else if (CarryingComponent)
	{
		CarryingComponent->Throw();
	}
}

void AMainCharacter::OnSprintStart()
{
	if (!CanMove() || bIsPushingMode)
	{
		bIsSprinting = false;
		return;
	}

	if (AbilitySystemComponent)
	{
		static const FGameplayTag BlockSprintTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Block.Sprint"), false);
		static const FGameplayTag HeavyCarryTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Carrying.Heavy"), false);
		static const FGameplayTag OverloadedTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Encumbered.Overloaded"), false);
		static const FGameplayTag ImmobileTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Encumbered.Immobile"), false);
		static const FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
		static const FGameplayTag GroggyTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Groggy"), false);
		static const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Dead"), false);
		static const FGameplayTag StunnedTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);

		if ((BlockSprintTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(BlockSprintTag)) ||
			(HeavyCarryTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeavyCarryTag)) ||
			(OverloadedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(OverloadedTag)) ||
			(ImmobileTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(ImmobileTag)) ||
			(PushingTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(PushingTag)) ||
			(GroggyTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(GroggyTag)) ||
			(DeadTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(DeadTag)) ||
			(StunnedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(StunnedTag)))
		{
			bIsSprinting = false;
			return;
		}

		bool bActivated = false;
		if (SprintAbilitySpecHandle.IsValid())
		{
			bActivated = AbilitySystemComponent->TryActivateAbility(SprintAbilitySpecHandle);
		}
		else
		{
			static const FGameplayTagContainer SprintTagContainer(FGameplayTag::RequestGameplayTag(FName("Ability.Player.Sprint")));
			bActivated = AbilitySystemComponent->TryActivateAbilitiesByTag(SprintTagContainer);
		}

		if (!bActivated)
		{
			bIsSprinting = false;
			return;
		}
	}

	bIsSprinting = true;

	if (!HasAuthority())
	{
		ServerSetSprinting(true);
	}
}

void AMainCharacter::OnSprintEnd()
{
	bIsSprinting = false;

	if (AbilitySystemComponent)
	{
		if (SprintAbilitySpecHandle.IsValid())
		{
			AbilitySystemComponent->CancelAbilityHandle(SprintAbilitySpecHandle);
		}
		else
		{
			static const FGameplayTagContainer SprintTagContainer(FGameplayTag::RequestGameplayTag(FName("Ability.Player.Sprint")));
			AbilitySystemComponent->CancelAbilities(&SprintTagContainer);
		}
	}

	if (!HasAuthority())
	{
		ServerSetSprinting(false);
	}
}

void AMainCharacter::ServerSetSprinting_Implementation(bool bSprint)
{
	if (bSprint)
	{
		if (!CanMove() || bIsPushingMode)
		{
			bIsSprinting = false;
			return;
		}

		if (AbilitySystemComponent)
		{
			static const FGameplayTag BlockSprintTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Block.Sprint"), false);
			static const FGameplayTag HeavyCarryTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Carrying.Heavy"), false);
			static const FGameplayTag OverloadedTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Encumbered.Overloaded"), false);
			static const FGameplayTag ImmobileTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Encumbered.Immobile"), false);
			static const FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
			static const FGameplayTag GroggyTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Groggy"), false);
			static const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Dead"), false);
			static const FGameplayTag StunnedTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);

			if ((BlockSprintTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(BlockSprintTag)) ||
				(HeavyCarryTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeavyCarryTag)) ||
				(OverloadedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(OverloadedTag)) ||
				(ImmobileTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(ImmobileTag)) ||
				(PushingTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(PushingTag)) ||
				(GroggyTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(GroggyTag)) ||
				(DeadTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(DeadTag)) ||
				(StunnedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(StunnedTag)))
			{
				bIsSprinting = false;
				return;
			}

			if (SprintAbilitySpecHandle.IsValid())
			{
				AbilitySystemComponent->TryActivateAbility(SprintAbilitySpecHandle);
			}
			else
			{
				static const FGameplayTagContainer SprintTagContainer(FGameplayTag::RequestGameplayTag(FName("Ability.Player.Sprint")));
				AbilitySystemComponent->TryActivateAbilitiesByTag(SprintTagContainer);
			}
		}

		bIsSprinting = true;
	}
	else
	{
		bIsSprinting = false;

		if (AbilitySystemComponent)
		{
			if (SprintAbilitySpecHandle.IsValid())
			{
				AbilitySystemComponent->CancelAbilityHandle(SprintAbilitySpecHandle);
			}
			else
			{
				static const FGameplayTagContainer SprintTagContainer(FGameplayTag::RequestGameplayTag(FName("Ability.Player.Sprint")));
				AbilitySystemComponent->CancelAbilities(&SprintTagContainer);
			}
		}
	}
}

void AMainCharacter::OnJumpStartInput()
{
	// 밀기 모드 중이거나 행동 불가 상태일 때는 점프 차단
	if (!CanMove() || bIsPushingMode)
	{
		return;
	}

	if (AbilitySystemComponent)
	{
		static const FGameplayTag BlockJumpTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Block.Jump"), false);
		static const FGameplayTag HeavyCarryTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Carrying.Heavy"), false);
		static const FGameplayTag ImmobileTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Encumbered.Immobile"), false);
		static const FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
		static const FGameplayTag GroggyTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Groggy"), false);
		static const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Dead"), false);
		static const FGameplayTag StunnedTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);

		if ((BlockJumpTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(BlockJumpTag)) ||
			(HeavyCarryTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeavyCarryTag)) ||
			(ImmobileTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(ImmobileTag)) ||
			(PushingTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(PushingTag)) ||
			(GroggyTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(GroggyTag)) ||
			(DeadTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(DeadTag)) ||
			(StunnedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(StunnedTag)))
		{
			return; // 점프 차단
		}

		if (JumpAbilitySpecHandle.IsValid())
		{
			AbilitySystemComponent->TryActivateAbility(JumpAbilitySpecHandle);
		}
		else
		{
			static const FGameplayTagContainer JumpTagContainer(FGameplayTag::RequestGameplayTag(FName("Ability.Player.Jump")));
			AbilitySystemComponent->TryActivateAbilitiesByTag(JumpTagContainer);
		}
	}
	else
	{
		DoJumpStart();
	}
}

void AMainCharacter::OnJumpEndInput()
{
	DoJumpEnd();
}

void AMainCharacter::OnUse()
{
	if (!CanAct())
	{
		return;
	}

	// 현재 들고있는 택배(CarryingComponent)가 있다면 일반 아이템 사용 불가 (손이 비어있어야 함)
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		if (AItemBase* HandItem = Cast<AItemBase>(CarryingComponent->GetCarriedActor()))
		{
			HandItem->OnUse();
		}
		return;
	}

	// TODO: 장착 중인 아이템 사용 로직 (인벤토리 시스템 연동 시 구현)
	if (CarryingComponent && !CarryingComponent->IsCarrying())
	{
		UE_LOG(LogTemp, Warning, TEXT("물건이 없어서 사용할 수 없습니다!"));

	}
}

void AMainCharacter::OnSlot1()
{
	if (!CanAct() || bIsPushingMode) return;

	if (AbilitySystemComponent)
	{
		static const FGameplayTag HeavyCarryTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Carrying.Heavy"), false);
		if (HeavyCarryTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeavyCarryTag))
		{
			return; // 무거운 택배 운반 중 슬롯 교체 차단
		}
	}

	if (InventoryComponent)
	{
		InventoryComponent->RequestSlotAction(0);
	}
}

void AMainCharacter::OnSlot2()
{
	if (!CanAct() || bIsPushingMode) return;

	if (AbilitySystemComponent)
	{
		static const FGameplayTag HeavyCarryTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Carrying.Heavy"), false);
		if (HeavyCarryTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeavyCarryTag))
		{
			return; // 무거운 택배 운반 중 슬롯 교체 차단
		}
	}

	if (InventoryComponent)
	{
		InventoryComponent->RequestSlotAction(1);
	}
}

void AMainCharacter::OnSlot3()
{
	if (!CanAct() || bIsPushingMode) return;

	if (AbilitySystemComponent)
	{
		static const FGameplayTag HeavyCarryTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Carrying.Heavy"), false);
		if (HeavyCarryTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeavyCarryTag))
		{
			return; // 무거운 택배 운반 중 슬롯 교체 차단
		}
	}

	if (InventoryComponent)
	{
		InventoryComponent->RequestSlotAction(2);
	}
}

void AMainCharacter::OnEquipRequested(AItemBase* ItemToEquip)
{
	if (CarryingComponent)
	{
		if (ItemToEquip)
		{
			CarryingComponent->EquipItem(ItemToEquip);
		}
		else
		{
			CarryingComponent->ClearCarriedItem();
		}
	}
}

void AMainCharacter::UpdateStamina(float DeltaTime)
{
	if (!BaseAttribute)
	{
		return;
	}

	float CurrentStamina = BaseAttribute->GetStemina();
	float MaxStamina = BaseAttribute->GetMaxStemina();

	// 고갈(Exhausted) 쿨다운 타이머 처리
	if (CurrentExhaustionTimer > 0.0f)
	{
		CurrentExhaustionTimer -= DeltaTime;
		if (CurrentExhaustionTimer <= 0.0f)
		{
			// 쿨다운 종료 시 Exhausted 상태 태그 제거 (서버 권한)
			if (HasAuthority() && StatusComponent)
			{
				static const FGameplayTag ExhaustedTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Exhausted"), false);
				StatusComponent->RemoveStatusTag(ExhaustedTag);
			}
		}
	}

	// 실제 스태미나 증감 연산은 서버에서만 수행 (AttributeSet의 복제(Replication)를 통해 클라이언트에 동기화됨)
	if (!HasAuthority())
	{
		return;
	}

	// 캐릭터가 실제로 이동 중인지 체크
	bool bIsMoving = GetVelocity().SizeSquared2D() > 10.0f;

	if (bIsSprinting && bIsMoving && CanMove())
	{
		// 달리기 중 스태미나 감속
		float NewStamina = FMath::Clamp(CurrentStamina - (StaminaDrainRate * DeltaTime), 0.0f, MaxStamina);
		BaseAttribute->SetStemina(NewStamina);

		// 스태미나 0 도달 시 처리
		if (NewStamina <= 0.0f)
		{
			OnSprintEnd();

			// State.Player.Exhausted 태그 부여 및 쿨다운 시작
			if (StatusComponent)
			{
				static const FGameplayTag ExhaustedTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Exhausted"), false);
				StatusComponent->AddStatusTag(ExhaustedTag);
			}

			CurrentExhaustionTimer = ExhaustionCooldownDuration;
		}
	}
	else
	{
		// 달리기를 하지 않거나 멈췄을 때 스태미나 회복
		if (CurrentStamina < MaxStamina && CurrentExhaustionTimer <= 0.0f)
		{
			float NewStamina = FMath::Clamp(CurrentStamina + (StaminaRegenRate * DeltaTime), 0.0f, MaxStamina);
			BaseAttribute->SetStemina(NewStamina);
		}
	}
}

// ---------------------------------------------------------
// [이모트 시스템 구현]
// ---------------------------------------------------------

void AMainCharacter::OnEmoteToggle()
{
	if (!CanAct() || bIsGroggy || bIsDead)
	{
		return;
	}

	if (AbilitySystemComponent)
	{
		static const FGameplayTag BlockEmoteTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Block.Emote"), false);
		static const FGameplayTag GroggyTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Groggy"), false);
		static const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Dead"), false);
		static const FGameplayTag StunnedTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);
		static const FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Held"), false);
		static const FGameplayTag HeavyCarryTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Carrying.Heavy"), false);

		if ((BlockEmoteTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(BlockEmoteTag)) ||
			(GroggyTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(GroggyTag)) ||
			(DeadTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(DeadTag)) ||
			(StunnedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(StunnedTag)) ||
			(HeldTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeldTag)) ||
			(HeavyCarryTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeavyCarryTag)))
		{
			return; // 이모트 UI 열기 차단
		}
	}

	// [개선] 밀기 모드 중 이모트 UI 열기 시 밀기 모드 자동 해제
	if (bIsPushingMode)
	{
		StopPushMode();
	}

	// 블루프린트에서 UI 위젯을 띄우는 이벤트를 호출
	OpenEmoteUI();
}

void AMainCharacter::SetEmotion(int32 EmotionIndex, FLinearColor EmoteColor)
{
	for (UMaterialInstanceDynamic* DMI : FaceMaterialInstances)
	{
		if (DMI)
		{
			DMI->SetScalarParameterValue(EmotionParameterName, static_cast<float>(EmotionIndex));
			DMI->SetVectorParameterValue(EmotionColorParameterName, EmoteColor);
		} 
	}
}

void AMainCharacter::PlayEmote(UAnimMontage* EmoteMontage, int32 EmotionIndex, FLinearColor EmoteColor)
{
	if (!EmoteMontage || !GetMesh() || !GetMesh()->GetAnimInstance() || !CanAct() || bIsGroggy || bIsDead)
	{
		return;
	}

	if (AbilitySystemComponent)
	{
		static const FGameplayTag BlockEmoteTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Block.Emote"), false);
		static const FGameplayTag GroggyTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Groggy"), false);
		static const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Dead"), false);
		static const FGameplayTag StunnedTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);
		static const FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Held"), false);
		static const FGameplayTag HeavyCarryTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Carrying.Heavy"), false);

		if ((BlockEmoteTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(BlockEmoteTag)) ||
			(GroggyTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(GroggyTag)) ||
			(DeadTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(DeadTag)) ||
			(StunnedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(StunnedTag)) ||
			(HeldTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeldTag)) ||
			(HeavyCarryTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeavyCarryTag)))
		{
			return; // 이모트 재생 차단
		}
	}

	// [개선] 밀기 모드 중 이모트 실행 시 밀기 모드 자동 해제
	if (bIsPushingMode)
	{
		StopPushMode();
	}

	// [이모트 사용 시 자동 Drop] 물건을 들고 있는 상태라면 먼저 내려놓음
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		CarryingComponent->GrabOrDrop();
	}

	if (!HasAuthority())
	{
		ServerPlayEmote(EmoteMontage, EmotionIndex, EmoteColor);
	}
	else
	{
		MulticastPlayEmote(EmoteMontage, EmotionIndex, EmoteColor);
	}
}

void AMainCharacter::ServerPlayEmote_Implementation(UAnimMontage* EmoteMontage, int32 EmotionIndex, FLinearColor EmoteColor)
{
	if (!CanAct() || bIsGroggy || bIsDead)
	{
		return;
	}

	if (AbilitySystemComponent)
	{
		static const FGameplayTag BlockEmoteTag = FGameplayTag::RequestGameplayTag(FName("Ability.Player.Block.Emote"), false);
		static const FGameplayTag GroggyTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Groggy"), false);
		static const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Dead"), false);
		static const FGameplayTag StunnedTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);
		static const FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Held"), false);

		if ((BlockEmoteTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(BlockEmoteTag)) ||
			(GroggyTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(GroggyTag)) ||
			(DeadTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(DeadTag)) ||
			(StunnedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(StunnedTag)) ||
			(HeldTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeldTag)))
		{
			return;
		}
	}

	MulticastPlayEmote(EmoteMontage, EmotionIndex, EmoteColor);
}

void AMainCharacter::MulticastPlayEmote_Implementation(UAnimMontage* EmoteMontage, int32 EmotionIndex, FLinearColor EmoteColor)
{
	if (!EmoteMontage || !GetMesh() || !GetMesh()->GetAnimInstance())
	{
		return;
	}

	// 1. 선택한 감정 표정과 색상으로 얼굴 머티리얼 변경
	SetEmotion(EmotionIndex, EmoteColor);

	// 2. 이모트 몽타주 재생
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	AnimInstance->Montage_Play(EmoteMontage);
	CurrentEmoteMontage = EmoteMontage;

	// 3. 몽타주가 끝날 때를 감지하기 위해 델리게이트 바인딩
	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AMainCharacter::OnEmoteMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, EmoteMontage);
}

void AMainCharacter::ChangeEmotion(int32 EmotionIndex, FLinearColor EmoteColor)
{
	if (HasAuthority())
	{
		MulticastChangeEmotion(EmotionIndex, EmoteColor);
	}
	else
	{
		ServerChangeEmotion(EmotionIndex, EmoteColor);
	}
}

void AMainCharacter::ServerChangeEmotion_Implementation(int32 EmotionIndex, FLinearColor EmoteColor)
{
	MulticastChangeEmotion(EmotionIndex, EmoteColor);
}

void AMainCharacter::MulticastChangeEmotion_Implementation(int32 EmotionIndex, FLinearColor EmoteColor)
{
	SetEmotion(EmotionIndex, EmoteColor);
}

void AMainCharacter::OnEmoteMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// 방금 끝난 몽타주가 이모트 몽타주인지 확인
	if (Montage == CurrentEmoteMontage)
	{
		// 이모트가 끝났거나 중단(이동으로 인한 취소)되었으므로 기본 표정(0)과 기본 색상으로 복귀
		SetEmotion(0, FLinearColor(0.0f, 0.623294f, 1.0f, 1.0f));
		CurrentEmoteMontage = nullptr;
	}
}

bool AMainCharacter::CanAct() const
{
	if (bIsStunned || bIsGroggy || bIsDead || bIsReviving)
	{
		return false;
	}
	return Super::CanAct();
}

bool AMainCharacter::CanMove() const
{
	if (bIsStunned || bIsGroggy || bIsDead || bIsReviving || bIsHoldingRevive)
	{
		return false;
	}
	return Super::CanMove();
}

void AMainCharacter::Knockdown()
{
	if (!HasAuthority())
	{
		return;
	}

	// 이미 기절 상태면 무시
	if (bIsStunned)
	{
		return;
	}

	bIsStunned = true;

	// 부활 차징 중이었다면 취소
	if (bIsHoldingRevive)
	{
		CancelReviveHold();
	}

	// 물건을 들고 있었다면 떨어뜨림 (GrabOrDrop은 서버에서만 호출해야 함)
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		CarryingComponent->GrabOrDrop();
	}

	// 달리기 취소
	if (bIsSprinting)
	{
		ServerSetSprinting_Implementation(false);
	}

	// 애니메이션 재생 멀티캐스트 호출
	MulticastKnockdown();
}

void AMainCharacter::MulticastKnockdown_Implementation()
{
	if (KnockdownMontage && GetMesh() && GetMesh()->GetAnimInstance())
	{
		float AnimDuration = GetMesh()->GetAnimInstance()->Montage_Play(KnockdownMontage);
		
		// 몽타주가 재생되지 않았을 경우(0.0) 대비 안전장치
		if (AnimDuration <= 0.0f)
		{
			AnimDuration = 2.0f;
		}

		// 서버에서 타이머를 설정하여 기절 상태 복구
		if (HasAuthority())
		{
			GetWorldTimerManager().SetTimer(KnockdownTimerHandle, this, &AMainCharacter::OnKnockdownEnd, AnimDuration, false);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("KnockdownMontage가 할당되지 않았거나 재생할 수 없습니다. 즉시 회복됩니다."));
		if (HasAuthority())
		{
			OnKnockdownEnd();
		}
	}
}

void AMainCharacter::OnKnockdownEnd()
{
	if (HasAuthority())
	{
		bIsStunned = false;
	}
}

// ---------------------------------------------------------
// [그로기 및 사망 / 부활 시스템]
// ---------------------------------------------------------

void AMainCharacter::HandleHealthZero()
{
	if (bIsDead)
	{
		return;
	}

	if (DownCount == 0 && !bIsGroggy)
	{
		// 부활 차징 중이었다면 취소
		if (bIsHoldingRevive)
		{
			CancelReviveHold();
		}

		if (AbilitySystemComponent && GroggyAbilitySpecHandle.IsValid())
		{
			AbilitySystemComponent->TryActivateAbility(GroggyAbilitySpecHandle);
		}
		else
		{
			// 첫 번째 다운 (그로기) 폴백
			DownCount = 1;
			bIsGroggy = true;

			if (CarryingComponent && CarryingComponent->IsCarrying())
			{
				CarryingComponent->GrabOrDrop();
			}
			
			OnSprintEnd();
			MulticastOnEnterGroggy();
		}
	}
	else if (DownCount >= 1)
	{
		if (AbilitySystemComponent && DeathAbilitySpecHandle.IsValid())
		{
			AbilitySystemComponent->TryActivateAbility(DeathAbilitySpecHandle);
		}
		else
		{
			// 두 번째 다운 (최종 사망) 폴백
			DownCount = 2;
			bIsDead = true;

			if (InventoryComponent)
			{
				for (int32 i = 0; i < InventoryComponent->InventorySlots.Num(); ++i)
				{
					if (AItemBase* Item = InventoryComponent->InventorySlots[i])
					{
						Item->MulticastOnEquipped(nullptr);
						FVector ScatterImpulse = FVector(
							FMath::RandRange(-400.f, 400.f), 
							FMath::RandRange(-400.f, 400.f), 
							FMath::RandRange(400.f, 700.f)
						);
						Item->Throw(ScatterImpulse, this);
						InventoryComponent->InventorySlots[i] = nullptr;
						InventoryComponent->OnInventorySlotChanged.Broadcast(i, nullptr);
					}
				}
			}
			MulticastOnDeath();
		}
	}
}

void AMainCharacter::ReviveCharacter(float RestoredHealth)
{
	if (!bIsGroggy || bIsDead)
	{
		return;
	}

	if (AbilitySystemComponent && GroggyAbilitySpecHandle.IsValid())
	{
		AbilitySystemComponent->CancelAbilityHandle(GroggyAbilitySpecHandle);
	}

	bIsGroggy = false;
	bIsReviving = true;

	if (BaseAttribute)
	{
		BaseAttribute->SetHealth(RestoredHealth);
	}

	// 모든 클라이언트에 블루프린트 이벤트 방송 (표정 데 애니메이션 복구)
	MulticastOnRevived();

	// 애니메이션이 끝날 즈음(약 2.5초 후) 자동으로 FinishRevive를 호출하여 입력 차단 해제
	FTimerHandle ReviveTimerHandle;
	GetWorld()->GetTimerManager().SetTimer(ReviveTimerHandle, this, &AMainCharacter::OnReviveTimerExpired, 2.5f, false);
}

void AMainCharacter::OnReviveTimerExpired()
{
	// 서버에서 실행된 타이머가 여기를 호출하면, Multicast RPC를 실행하여 모든 클라이언트의 bIsReviving을 강제로 내립니다.
	FinishRevive();
}

void AMainCharacter::FinishRevive_Implementation()
{
	// 서버와 모든 클라이언트에서 입력 차단을 해제
	bIsReviving = false;
}

// Multicast RPC 구현
void AMainCharacter::MulticastOnEnterGroggy_Implementation()
{
	OnEnterGroggy();
}

void AMainCharacter::MulticastOnRevived_Implementation()
{
	OnRevived();
}

void AMainCharacter::MulticastOnDeath_Implementation()
{
	OnDeath();
}

// 클라이언트가 F키를 눌러 서버에서 비로소 살려달라고 요청
void AMainCharacter::ServerReviveTarget_Implementation(AMainCharacter* Target)
{
	if (!Target || !Target->bIsGroggy || Target->bIsDead)
	{
		return;
	}

	if (BaseAttribute)
	{
		float MyHealth = BaseAttribute->GetHealth();
		float HealthToGive = MyHealth / 2.0f;
		if (HealthToGive < 1.0f) HealthToGive = 1.0f;
		float NewMyHealth = FMath::Max(1.0f, MyHealth - HealthToGive);

		BaseAttribute->SetHealth(NewMyHealth);
		Target->ReviveCharacter(HealthToGive);
	}
}

bool AMainCharacter::CanInteract_Implementation(AActor* Interactor) const
{
	// 자신이 그로기 상태이고 사망하지 않았으며, 상호작용을 시도하는 대상이 자신이 아닐 때 부활 상호작용 가능
	return (bIsGroggy && !bIsDead && Interactor != this);
}

void AMainCharacter::Interact_Implementation(AActor* Interactor)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bIsGroggy && !bIsDead)
	{
		AMainCharacter* Reviver = Cast<AMainCharacter>(Interactor);
		if (Reviver && Reviver->BaseAttribute)
		{
			// 살려주는 사람의 현재 체력 가져오기
			float ReviverCurrentHealth = Reviver->BaseAttribute->GetHealth();
			
			// 자신의 체력을 절반 깎고, 그 양을 쓰러진 사람에게 부여
			float HealthToGive = ReviverCurrentHealth / 2.0f;
			
			// 최소 체력 1은 남겨두기 위한 안전장치 (원한다면 제거 가능)
			if (HealthToGive < 1.0f) HealthToGive = 1.0f;
			float NewReviverHealth = FMath::Max(1.0f, ReviverCurrentHealth - HealthToGive);

			Reviver->BaseAttribute->SetHealth(NewReviverHealth);
			
			// 쓰러진 캐릭터 부활 처리
			ReviveCharacter(HealthToGive);
		}
	}
}

FText AMainCharacter::GetInteractPrompt_Implementation() const
{
	if (bIsGroggy && !bIsDead)
	{
		return FText::FromString(TEXT("살리기"));
	}
	return FText::GetEmpty();
}

void AMainCharacter::StartPushMode(AActor* TargetObject)
{
	if (!TargetObject) return;

	if (!HasAuthority())
	{
		ServerStartPushMode(TargetObject);
	}

	// 표정 변경 (서버에서 실행되면 내부적으로 Multicast를 통해 동기화됨)
	if (HasAuthority())
	{
		ChangeEmotion(EmotionIndex_Pushing);
	}

	// 무기/아이템 들고 있으면 해제
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		CarryingComponent->GrabOrDrop();
	}

	// 달리기 중이었다면 달리기 즉시 강제 취소
	if (bIsSprinting)
	{
		bIsSprinting = false;
		if (!HasAuthority())
		{
			ServerSetSprinting(false);
		}
	}

	bIsPushingMode = true;
	CurrentPushedObject = TargetObject;

	if (AbilitySystemComponent)
	{
		if (PushAbilitySpecHandle.IsValid())
		{
			AbilitySystemComponent->TryActivateAbility(PushAbilitySpecHandle);
		}
		else
		{
			static const FGameplayTagContainer PushTagContainer(FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing")));
			AbilitySystemComponent->TryActivateAbilitiesByTag(PushTagContainer);
		}
	}

	if (AEventObjectBase* EventObj = Cast<AEventObjectBase>(CurrentPushedObject))
	{
		EventObj->AddPusher(this);
	}

	// 캐릭터 정렬 연출: 거리는 그대로 유지하되, 대상(박스)을 바라보도록 회전만 수행
	FVector BoxCenter = TargetObject->GetComponentsBoundingBox().GetCenter();
	FVector Dir = BoxCenter - GetActorLocation();
	Dir.Z = 0.0f;
	Dir.Normalize();
	SetActorRotation(Dir.Rotation());
	LockedPushDirection = Dir;

	if (GetCharacterMovement())
	{
		bOriginalOrientRotation = GetCharacterMovement()->bOrientRotationToMovement;
		GetCharacterMovement()->bOrientRotationToMovement = false;
	}

	LastCharacterLocation = GetActorLocation();

	if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(TargetObject->GetRootComponent()))
	{
		PrimComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		PrimComp->IgnoreActorWhenMoving(this, true);
		GetCapsuleComponent()->IgnoreActorWhenMoving(TargetObject, true);
		GetMesh()->IgnoreActorWhenMoving(TargetObject, true);
	}

	if (StatusComponent)
	{
		static const FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
		StatusComponent->AddStatusTag(PushingTag);
	}
}

void AMainCharacter::ServerStartPushMode_Implementation(AActor* TargetObject)
{
	StartPushMode(TargetObject);
}

void AMainCharacter::StopPushMode()
{
	if (!HasAuthority())
	{
		ServerStopPushMode();
	}

	if (HasAuthority())
	{
		ChangeEmotion(EmotionIndex_Normal);
	}

	bIsPushingMode = false;

	if (AbilitySystemComponent)
	{
		if (PushAbilitySpecHandle.IsValid())
		{
			AbilitySystemComponent->CancelAbilityHandle(PushAbilitySpecHandle);
		}
		else
		{
			static const FGameplayTagContainer PushTagContainer(FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing")));
			AbilitySystemComponent->CancelAbilities(&PushTagContainer);
		}
	}
	
	if (CurrentPushedObject)
	{
		if (AEventObjectBase* EventObj = Cast<AEventObjectBase>(CurrentPushedObject))
		{
			EventObj->RemovePusher(this);
		}

		if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(CurrentPushedObject->GetRootComponent()))
		{
			PrimComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			PrimComp->IgnoreActorWhenMoving(this, false);
			GetCapsuleComponent()->IgnoreActorWhenMoving(CurrentPushedObject, false);
			GetMesh()->IgnoreActorWhenMoving(CurrentPushedObject, false);
		}
	}

	CurrentPushInput = 0.0f;
	if (!HasAuthority())
	{
		ServerSetPushInput(0.0f);
	}

	CurrentPushedObject = nullptr;

	if (StatusComponent)
	{
		static const FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
		StatusComponent->RemoveStatusTag(PushingTag);
	}

	if (GetCharacterMovement())
	{
		GetCharacterMovement()->bOrientRotationToMovement = bOriginalOrientRotation;
	}
}

void AMainCharacter::ServerStopPushMode_Implementation()
{
	StopPushMode();
}

void AMainCharacter::ServerSetPushInput_Implementation(float NewInput)
{
	CurrentPushInput = NewInput;
}

void AMainCharacter::OnRep_IsPushingMode()
{
	// 리플리케이션(복제)에 따른 UI나 기타 상태 업데이트가 필요하면 이곳에 작성합니다.
	// bIsPushingMode 값 자체는 이미 서버와 동기화되었으므로 애니메이션은 정상 작동합니다.
}

float AMainCharacter::GetRequiredReviveTime() const
{
	if (ReviveAbilityClass)
	{
		if (const UGA_Revive* ReviveCDO = ReviveAbilityClass->GetDefaultObject<UGA_Revive>())
		{
			return ReviveCDO->ReviveDuration;
		}
	}
	return RequiredReviveTime;
}

float AMainCharacter::GetReviveProgress() const
{
	const float Duration = GetRequiredReviveTime();
	if (Duration <= 0.0f)
	{
		return 1.0f;
	}
	return FMath::Clamp(CurrentReviveHoldTime / Duration, 0.0f, 1.0f);
}

void AMainCharacter::StartReviveHold(AMainCharacter* Target)
{
	if (!Target || !Target->bIsGroggy || Target->bIsDead || Target == this)
	{
		return;
	}

	bIsHoldingRevive = true;
	CurrentReviveHoldTime = 0.0f;
	CurrentReviveTarget = Target;

	// 살리는 동안 이동 즉시 정지 (이동 차단)
	if (GetCharacterMovement())
	{
		GetCharacterMovement()->StopMovementImmediately();
	}

	// GAS 어빌리티 활성화
	if (AbilitySystemComponent && ReviveAbilitySpecHandle.IsValid())
	{
		AbilitySystemComponent->TryActivateAbility(ReviveAbilitySpecHandle);
	}

	OnReviveHoldStarted(Target);
	Target->SetReviveProgress(0.0f);
}

void AMainCharacter::CancelReviveHold()
{
	if (!bIsHoldingRevive)
	{
		return;
	}

	bIsHoldingRevive = false;
	CurrentReviveHoldTime = 0.0f;

	if (AbilitySystemComponent && ReviveAbilitySpecHandle.IsValid())
	{
		AbilitySystemComponent->CancelAbilityHandle(ReviveAbilitySpecHandle);
	}

	if (CurrentReviveTarget.IsValid())
	{
		CurrentReviveTarget->SetReviveProgress(0.0f);
	}
	CurrentReviveTarget = nullptr;

	OnReviveHoldEnded(false);
}

void AMainCharacter::CompleteReviveHold()
{
	if (!bIsHoldingRevive)
	{
		return;
	}

	AMainCharacter* Target = CurrentReviveTarget.Get();

	bIsHoldingRevive = false;
	CurrentReviveHoldTime = 0.0f;
	CurrentReviveTarget = nullptr;

	if (AbilitySystemComponent && ReviveAbilitySpecHandle.IsValid())
	{
		AbilitySystemComponent->CancelAbilityHandle(ReviveAbilitySpecHandle);
	}

	OnReviveHoldEnded(true);

	if (Target && Target->bIsGroggy && !Target->bIsDead)
	{
		Target->SetReviveProgress(0.0f);
		ServerReviveTarget(Target);
	}
}

void AMainCharacter::SetReviveProgress(float Progress)
{
	OnReviveProgressUpdated(Progress);
}

void AMainCharacter::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	Super::HandleHealthChanged(Data);

	// 체력 감소(피격 시) 부활 차징 취소
	if (Data.NewValue < Data.OldValue && bIsHoldingRevive)
	{
		CancelReviveHold();
	}
}

float AMainCharacter::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser)
{
	float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (ActualDamage > 0.0f && bIsHoldingRevive)
	{
		CancelReviveHold();
	}
	return ActualDamage;
}

