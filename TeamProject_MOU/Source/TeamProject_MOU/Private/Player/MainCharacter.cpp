#include "Player/MainCharacter.h"
#include "Components/InteractionComponent.h"
#include "Components/CarryingComponent.h"
#include "Components/StatusComponent.h"
#include "Components/CharacterVisualComponent.h"
#include "Data/CharacterVisualDataAsset.h"
#include "Base/BaseAttributeSet.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Base/ItemBase.h"
#include "Item/WeaponItemBase.h"
#include "Base/PackageBase.h"
#include "Base/EventObjectBase.h"
#include "Components/InventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
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
#include "Ability/GA_Knockdown.h"

AMainCharacter::AMainCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// 컴포넌트 추가
	InteractionComponent = CreateDefaultSubobject<UInteractionComponent>(TEXT("InteractionComponent"));
	CarryingComponent = CreateDefaultSubobject<UCarryingComponent>(TEXT("CarryingComponent"));
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComponent"));

	// 발광(손전등 대체) 포인트 라이트 컴포넌트 생성 및 메시 부착
	FlashlightLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FlashlightLight"));
	FlashlightLight->SetupAttachment(GetMesh());
	FlashlightLight->SetRelativeLocation(FVector(0.0f, 30.0f, 60.0f));
	FlashlightLight->SetIntensity(3000.0f);
	FlashlightLight->SetAttenuationRadius(800.0f);
	FlashlightLight->SetVisibility(false);
	FlashlightLight->SetCastShadows(true);

	// 발광 색상 프리셋 기본값 5종
	FlashlightColorPresets.Add(FLinearColor::White);                          // 1. 화이트
	FlashlightColorPresets.Add(FLinearColor(1.0f, 0.0f, 0.5f, 1.0f));       // 2. 핑크/마젠타
	FlashlightColorPresets.Add(FLinearColor(0.0f, 0.94f, 1.0f, 1.0f));      // 3. 시안/하늘색
	FlashlightColorPresets.Add(FLinearColor(0.2f, 1.0f, 0.2f, 1.0f));       // 4. 네온그린
	FlashlightColorPresets.Add(FLinearColor(1.0f, 0.8f, 0.0f, 1.0f));       // 5. 옐로우/골드

	// 시야(상호작용) Trace가 캐릭터를 인식할 수 있도록 Visibility 채널 Block 처리
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	if (GetCharacterMovement())
	{
		GetCharacterMovement()->bOrientRotationToMovement = false;
	}

	// CameraBoom은 베이스 클래스 소유라 삭제 불가 → 완전히 비활성화
	if (GetCameraBoom())
	{
		GetCameraBoom()->TargetArmLength = 0.0f;
		GetCameraBoom()->bDoCollisionTest = false;
		GetCameraBoom()->bUsePawnControlRotation = false;
	}

	// FollowCamera를 head 소켓에 직접 부착 (1인칭: CameraBoom 불필요)
	if (GetFollowCamera() && GetMesh())
	{
		GetFollowCamera()->SetupAttachment(GetMesh(), FName("head"));
		GetFollowCamera()->SetRelativeLocation(FirstPersonCameraOffset);
		GetFollowCamera()->SetRelativeRotation(FirstPersonCameraRotation);
		GetFollowCamera()->bUsePawnControlRotation = true;
		GetFollowCamera()->SetFieldOfView(90.0f);
	}
}

void AMainCharacter::BeginPlay()
{
	Super::BeginPlay();

	UpdateFirstPersonMeshVisibility();

	// 비주얼 컴포넌트에서 생성된 동적 머티리얼 인스턴스(DMI) 동기화
	if (VisualComponent)
	{
		FaceMaterialInstances = VisualComponent->GetFaceMaterialInstances();
		BodyMaterialInstances = VisualComponent->GetBodyMaterialInstances();
	}

	// 초기 발광 상태 적용
	UpdateFlashlightVisuals();

	if (InteractionComponent)
	{
		InteractionComponent->OnFocusedInteractableChanged.AddDynamic(this, &AMainCharacter::OnFocusedActorChanged);
		InteractionComponent->OnInteractExecuted.AddDynamic(this, &AMainCharacter::HandleInteractExecuted);
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

		TSubclassOf<UGameplayAbility> KnockdownClass = KnockdownAbilityClass ? KnockdownAbilityClass : TSubclassOf<UGameplayAbility>(UGA_Knockdown::StaticClass());
		KnockdownAbilitySpecHandle = AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(KnockdownClass, 1));
	}

	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		SkelMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}

	if (GetFollowCamera() && GetMesh())
	{
		if (GetFollowCamera()->GetAttachParent() != GetMesh() || GetFollowCamera()->GetAttachSocketName() != FName("head"))
		{
			GetFollowCamera()->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, FName("head"));
			if (!FirstPersonCameraOffset.IsNearlyZero())
			{
				GetFollowCamera()->SetRelativeLocation(FirstPersonCameraOffset);
			}
			if (!FirstPersonCameraRotation.IsNearlyZero())
			{
				GetFollowCamera()->SetRelativeRotation(FirstPersonCameraRotation);
			}
		}
		else
		{
			if (!FirstPersonCameraOffset.IsNearlyZero())
			{
				GetFollowCamera()->SetRelativeLocation(FirstPersonCameraOffset);
			}
			if (!FirstPersonCameraRotation.IsNearlyZero())
			{
				GetFollowCamera()->SetRelativeRotation(FirstPersonCameraRotation);
			}
		}
	}
}

void AMainCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	UpdateFirstPersonMeshVisibility();
}

void AMainCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	UpdateFirstPersonMeshVisibility();
}

void AMainCharacter::UpdateFirstPersonMeshVisibility()
{
	if (IsLocallyControlled())
	{
		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			SkelMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			SkelMesh->HideBoneByName(FName("neck"), PBO_None);
			SkelMesh->bCastHiddenShadow = true;
		}

		if (GetFollowCamera() && GetMesh())
		{
			if (GetFollowCamera()->GetAttachParent() != GetMesh() || GetFollowCamera()->GetAttachSocketName() != FName("head"))
			{
				GetFollowCamera()->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, FName("head"));
				if (!FirstPersonCameraOffset.IsNearlyZero())
				{
					GetFollowCamera()->SetRelativeLocation(FirstPersonCameraOffset);
				}
				if (!FirstPersonCameraRotation.IsNearlyZero())
				{
					GetFollowCamera()->SetRelativeRotation(FirstPersonCameraRotation);
				}
			}
			else
			{
				if (!FirstPersonCameraOffset.IsNearlyZero())
				{
					GetFollowCamera()->SetRelativeLocation(FirstPersonCameraOffset);
				}
				if (!FirstPersonCameraRotation.IsNearlyZero())
				{
					GetFollowCamera()->SetRelativeRotation(FirstPersonCameraRotation);
				}
			}
		}

		TArray<UStaticMeshComponent*> StaticMeshes;
		GetComponents<UStaticMeshComponent>(StaticMeshes);
		for (UStaticMeshComponent* SM : StaticMeshes)
		{
			if (SM && (SM->GetName().Contains(TEXT("Eye")) || SM->GetName().Contains(TEXT("Mouth"))))
			{
				SM->SetOwnerNoSee(true);
			}
		}

		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->ViewPitchMin = -70.0f;
				PC->PlayerCameraManager->ViewPitchMax = 70.0f;
			}
		}
	}
}

void AMainCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// [달리기 정지 감지] 방향키를 떼어 가속이 멈춘 경우 즉시 달리기 해제 (대기 모션 복귀)
	if (bIsSprinting && GetCharacterMovement() && GetCharacterMovement()->GetCurrentAcceleration().IsNearlyZero())
	{
		StopSprinting();
	}

	// [밀기 모드 입력 해제 감지] 키를 떼어 가속이 멈춘 경우 CurrentPushInput을 즉시 0으로 리셋 (상자 미끄러짐/밀림 방지)
	if (IsLocallyControlled() && bIsPushingMode)
	{
		if (GetCharacterMovement() && GetCharacterMovement()->GetCurrentAcceleration().IsNearlyZero())
		{
			if (!FMath::IsNearlyZero(CurrentPushInput))
			{
				CurrentPushInput = 0.0f;
				if (!HasAuthority())
				{
					ServerSetPushInput(0.0f);
				}
			}
		}
	}

	// 매 프레임 스태미나 소모 및 회복 처리
	UpdateStamina(DeltaTime);

	// [발광 배터리 소모 처리 (서버 권한)]
	if (HasAuthority() && bIsFlashlightOn && BaseAttribute)
	{
		float CurrentBat = BaseAttribute->GetBattery();
		float NewBat = FMath::Max(0.0f, CurrentBat - (BatteryDrainRate * DeltaTime));
		BaseAttribute->SetBattery(NewBat);

		// 배터리 0 도달 시 자동 소등 및 차단
		if (NewBat <= 0.0f)
		{
			ServerToggleFlashlight(false);
		}
	}

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

	// [밀기 모드] 거리 이탈 검사 및 상자 메쉬 관통 방지 클램프
	// 상자 이동 자체는 EventObjectBase::Tick(서버)에서 중앙 제어하므로, 캐릭터는 클램프와 이탈만 담당
	// (시뮬레이트 프록시는 서버의 위치 복제를 그대로 따르므로 프록시에서 StopPushMode가 오작동하지 않도록 제한)
	if (bIsPushingMode && CurrentPushedObject && (IsLocallyControlled() || HasAuthority()))
	{
		AEventObjectBase* EventObj = Cast<AEventObjectBase>(CurrentPushedObject);

		// 1. 손 앵커 포인트 기준 거리 이탈 검사 (상자가 이동하면서 원래 잡았던 손 위치에서 110cm 이상 멀어지면 자동 밀기 해제)
		FVector WorldAnchor = CurrentPushedObject->GetActorTransform().TransformPosition(PushLocalAnchor);
		float Dist2D = FVector::Dist2D(GetActorLocation(), WorldAnchor);
		float DetachLimit = EventObj ? EventObj->PushDetachDistance : 110.0f;
		if (Dist2D > DetachLimit)
		{
			StopPushMode();
			return;
		}

		FVector CurrentLoc = GetActorLocation();

		// 2. 상자가 이동 불가능한 상태(인원 부족/입력 불일치 등)일 때 캐릭터가 앞으로 나아가지 못하도록 이전 위치로 고정
		bool bCanMove = EventObj ? EventObj->IsReadyToMove() : false;
		if (!bCanMove)
		{
			SetActorLocation(LastCharacterLocation, false);
			CurrentLoc = LastCharacterLocation;
		}
		else
		{
			// [단방향 메쉬 파고들기 차단] 캐릭터가 상자 중심 방향으로 파고들었을 때만 상자 표면 쪽으로 밀어냄 (상자가 멀어질 때는 절대 끌어당기지 않음)
			FVector BoxCenter = CurrentPushedObject->GetComponentsBoundingBox().GetCenter();
			FVector ToBox = BoxCenter - CurrentLoc;
			ToBox.Z = 0.0f;
			float DistToCenter = ToBox.Size();

			if (DistToCenter < PushInitialDistToBox && PushInitialDistToBox > 0.0f && !ToBox.IsNearlyZero())
			{
				FVector ClampedLoc = BoxCenter - (ToBox / DistToCenter) * PushInitialDistToBox;
				ClampedLoc.Z = CurrentLoc.Z;
				SetActorLocation(ClampedLoc, false);
				CurrentLoc = ClampedLoc;
			}
		}

		LastCharacterLocation = CurrentLoc;
	}

	// [넉다운/그로기/사망 시: FollowCamera가 head 소켓에 부착되어 있으므로
	//  bUsePawnControlRotation만 토글하면 컨트롤러 회전 vs 본 회전이 자동 전환됨]
	bool bIsDown = (bIsStunned || bIsGroggy || bIsDead || IsStunned());
	if (!bIsDown && GetStatusComponent())
	{
		static const FGameplayTag StunTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);
		static const FGameplayTag PrimaryStunTag = FGameplayTag::RequestGameplayTag(FName("State.Primary.Stuned"), false);
		bIsDown = (StunTag.IsValid() && GetStatusComponent()->HasStatusTag(StunTag)) ||
			(PrimaryStunTag.IsValid() && GetStatusComponent()->HasStatusTag(PrimaryStunTag));
	}

	if (IsLocallyControlled() && GetFollowCamera())
	{
		GetFollowCamera()->bUsePawnControlRotation = !bIsDown;
	}

	// [2인 협동 운반 및 회전 제어]
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		if (APackageBase* HeavyPackage = Cast<APackageBase>(CarryingComponent->GetCarriedActor()))
		{
			if (HeavyPackage->PackageType == EPackageType::Heavy && HeavyPackage->CurrentCarriers.Num() >= 2)
			{
				AActor* OtherCarrier = (HeavyPackage->CurrentCarriers[0] == this) ? HeavyPackage->CurrentCarriers[1].Get() : HeavyPackage->CurrentCarriers[0].Get();
				if (OtherCarrier)
				{
					FVector DirToOther = OtherCarrier->GetActorLocation() - GetActorLocation();
					DirToOther.Z = 0.0f;
					if (!DirToOther.IsNearlyZero())
					{
						SetActorRotation(FMath::RInterpTo(GetActorRotation(), DirToOther.Rotation(), DeltaTime, 10.0f));
					}
				}
			}
		}
	}

	// [상태별 캐릭터 몸체 회전(bUseControllerRotationYaw) 및 시야각 데드존 제어]
	bool bIsRotationLocked = (bIsPushingMode || bIsDown);

	if (bIsRotationLocked)
	{
		bUseControllerRotationYaw = false;

		// 밀기 모드 중 컨트롤러 회전이 에임오프셋 범위를 벗어나지 않도록 보장
		if (bIsPushingMode && IsLocallyControlled() && GetController())
		{
			FRotator ControlRot = GetController()->GetControlRotation();
			float ForwardYaw = GetActorRotation().Yaw;
			float DeltaYaw = FRotator::NormalizeAxis(ControlRot.Yaw - ForwardYaw);

			if (DeltaYaw > PushCameraYawLimit)
			{
				ControlRot.Yaw = FRotator::NormalizeAxis(ForwardYaw + PushCameraYawLimit);
				GetController()->SetControlRotation(ControlRot);
			}
			else if (DeltaYaw < -PushCameraYawLimit)
			{
				ControlRot.Yaw = FRotator::NormalizeAxis(ForwardYaw - PushCameraYawLimit);
				GetController()->SetControlRotation(ControlRot);
			}
		}
	}
	else
	{
		float Speed = GetVelocity().Size2D();
		bool bMoving = (Speed > 5.0f) || (GetCharacterMovement() && GetCharacterMovement()->GetCurrentAcceleration().SizeSquared() > 0.0f);

		if (bMoving)
		{
			bUseControllerRotationYaw = true;
		}
		else
		{
			bUseControllerRotationYaw = false;

			FRotator AimRot = GetBaseAimRotation();
			FRotator ActorRot = GetActorRotation();
			float DeltaYaw = FRotator::NormalizeAxis(AimRot.Yaw - ActorRot.Yaw);

			if (FMath::Abs(DeltaYaw) > AimYawDeadzone)
			{
				float TargetYaw = (DeltaYaw > 0.0f) ? (AimRot.Yaw - AimYawDeadzone) : (AimRot.Yaw + AimYawDeadzone);
				FRotator TargetRot = ActorRot;
				TargetRot.Yaw = TargetYaw;
				SetActorRotation(FMath::RInterpTo(ActorRot, TargetRot, DeltaTime, TurnInPlaceInterpSpeed));
			}
		}
	}

	// [AimYaw 복제: 로컬 플레이어가 계산한 AimYaw를 서버로 전송하여 다른 클라이언트에도 동기화]
	if (IsLocallyControlled() && !HasAuthority())
	{
		FRotator AimRot = GetBaseAimRotation();
		FRotator ActorRot = GetActorRotation();
		float CurrentAimYaw = FMath::Clamp(FRotator::NormalizeAxis(AimRot.Yaw - ActorRot.Yaw), -70.0f, 70.0f);
		ServerSetAimYaw(CurrentAimYaw);
	}
	else if (IsLocallyControlled() && HasAuthority())
	{
		FRotator AimRot = GetBaseAimRotation();
		FRotator ActorRot = GetActorRotation();
		ReplicatedAimYaw = FMath::Clamp(FRotator::NormalizeAxis(AimRot.Yaw - ActorRot.Yaw), -70.0f, 70.0f);
	}
}

void AMainCharacter::ServerSetAimYaw_Implementation(float NewAimYaw)
{
	ReplicatedAimYaw = NewAimYaw;
}

void AMainCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMainCharacter, bIsSprinting);
	DOREPLIFETIME(AMainCharacter, bIsStunned);
	DOREPLIFETIME(AMainCharacter, bIsPushingMode);
	DOREPLIFETIME(AMainCharacter, CurrentPushedObject);
	DOREPLIFETIME(AMainCharacter, CurrentPushInput);
	DOREPLIFETIME(AMainCharacter, LockedPushDirection);
	DOREPLIFETIME(AMainCharacter, PushLocalAnchor);
	DOREPLIFETIME(AMainCharacter, DownCount);
	DOREPLIFETIME(AMainCharacter, KnockdownCount);
	DOREPLIFETIME(AMainCharacter, bIsGroggy);
	DOREPLIFETIME(AMainCharacter, bIsDead);
	DOREPLIFETIME(AMainCharacter, bIsReviving);
	DOREPLIFETIME(AMainCharacter, bIsHoldingRevive);
	DOREPLIFETIME(AMainCharacter, bIsFlashlightOn);
	DOREPLIFETIME(AMainCharacter, FlashlightColorIndex);
	DOREPLIFETIME(AMainCharacter, ReplicatedAimYaw);
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

	// 4번 키: 발광(손전등) On/Off 토글
	if (FlashlightToggleAction)
	{
		EnhancedInputComponent->BindAction(FlashlightToggleAction, ETriggerEvent::Started, this, &AMainCharacter::ToggleFlashlight);
	}

	// 5번 키: 발광 색상 순환 변경
	if (FlashlightColorAction)
	{
		EnhancedInputComponent->BindAction(FlashlightColorAction, ETriggerEvent::Started, this, &AMainCharacter::CycleFlashlightColor);
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

	// [달리기 입력/이동 연동 처리]
	// 전진 이동 입력이 없거나(W를 뗐음) 멈춘 경우 달리기를 일시 해제(Idle/Walk 복귀)
	if (bIsSprinting && Forward <= 0.1f)
	{
		StopSprinting();
	}
	// Shift 키를 계속 누르고 있는 상태에서 다시 전진(W) 입력을 주면 달리기 재개
	else if (bWantsToSprint && !bIsSprinting && Forward > 0.1f)
	{
		StartSprinting();
	}

	// [이모트 취소 로직] 이동 키 입력이 들어왔고 현재 재생 중인 이모트가 있다면 즉시 취소 (서버/모든 클라이언트 동기화)
	if ((FMath::Abs(Right) > 0.1f || FMath::Abs(Forward) > 0.1f) && CurrentEmoteMontage != nullptr)
	{
		StopEmote();
	}

	Super::DoMove(Right, Forward);
}

void AMainCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() == nullptr)
	{
		return;
	}

	// [밀기 모드 카메라 각도 제한]
	// 몸체는 박스를 향해 고정되어 있으므로, 카메라는 에임오프셋 허용 범위(±PushCameraYawLimit, 기본 ±70도) 내에서만 회전
	if (bIsPushingMode)
	{
		AddControllerPitchInput(Pitch);

		if (FMath::Abs(Yaw) > 0.0f)
		{
			float ForwardYaw = GetActorRotation().Yaw;

			AddControllerYawInput(Yaw);

			FRotator NewControlRot = GetController()->GetControlRotation();
			float DeltaYaw = FRotator::NormalizeAxis(NewControlRot.Yaw - ForwardYaw);

			if (DeltaYaw > PushCameraYawLimit)
			{
				NewControlRot.Yaw = FRotator::NormalizeAxis(ForwardYaw + PushCameraYawLimit);
				GetController()->SetControlRotation(NewControlRot);
			}
			else if (DeltaYaw < -PushCameraYawLimit)
			{
				NewControlRot.Yaw = FRotator::NormalizeAxis(ForwardYaw - PushCameraYawLimit);
				GetController()->SetControlRotation(NewControlRot);
			}
		}
		return;
	}

	Super::DoLook(Yaw, Pitch);
}

void AMainCharacter::OnFocusedActorChanged(AActor* NewFocusedActor)
{
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
		if (bIsAimingAtItem)
		{
			bIsAimingAtItem = false;
			FocusedItem = nullptr;
			OnItemAimChanged(false, nullptr);
		}
		return;
	}

	// 새 포커스 액터가 아이템인지 확인 (이벤트 오브젝트 제외하고 순수 아이템/택배만 UI 표시)
	if (AItemBase* NewItem = Cast<AItemBase>(NewFocusedActor))
	{
		// [요구사항] 이벤트 오브젝트(AEventObjectBase)는 아이템 정보 UI 표시 대상에서 완전 제외
		if (!NewItem->IsA<AEventObjectBase>())
		{
			bIsAimingAtItem = true;
			FocusedItem = NewItem;
			OnItemAimChanged(true, NewItem);
		}
		else if (bIsAimingAtItem)
		{
			bIsAimingAtItem = false;
			FocusedItem = nullptr;
			OnItemAimChanged(false, nullptr);
		}
	}
	else if (bIsAimingAtItem)
	{
		bIsAimingAtItem = false;
		FocusedItem = nullptr;
		OnItemAimChanged(false, nullptr);
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

void AMainCharacter::HandleInteractExecuted(AActor* InteractedActor)
{
	OnInteractWithActor(InteractedActor);
}

void AMainCharacter::DoJumpStart()
{
	// 이동 불가능 상태일 때 점프 차단
	if (!CanMove())
	{
		return;
	}

	if (CurrentEmoteMontage != nullptr)
	{
		StopEmote();
	}

	Super::DoJumpStart();
}

void AMainCharacter::OnInteract()
{
	// 상호작용 실행 시 달리기 즉시 해제 및 이모트 취소
	StopSprinting();

	if (CurrentEmoteMontage != nullptr)
	{
		StopEmote();
	}

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

	if (CurrentEmoteMontage != nullptr)
	{
		StopEmote();
	}

	// 줍기/내려놓기 시 달리기 즉시 해제
	StopSprinting();

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

	if (CurrentEmoteMontage != nullptr)
	{
		StopEmote();
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
	bWantsToSprint = true;
	StartSprinting();
}

void AMainCharacter::OnSprintEnd()
{
	bWantsToSprint = false;
	StopSprinting();
}

void AMainCharacter::StartSprinting()
{
	if (!CanMove() || bIsPushingMode)
	{
		bIsSprinting = false;
		return;
	}

	// [정지 상태 달리기 차단] 방향키를 누르지 않았을 때(정지 상태)는 달리기를 활성화하지 않음 (대기 모션 유지)
	if (GetCharacterMovement() && GetCharacterMovement()->GetCurrentAcceleration().IsNearlyZero())
	{
		bIsSprinting = false;
		return;
	}

	// 무거운 택배 들고 있는지 직접 확인
	if (CarryingComponent)
	{
		if (APackageBase* Pkg = Cast<APackageBase>(CarryingComponent->GetCarriedActor()))
		{
			if (Pkg->PackageType == EPackageType::Heavy)
			{
				bIsSprinting = false;
				return;
			}
		}
	}

	// 과적 2단계 이상 (소지 무게 > 130%)이면 달리기 차단
	if (BaseAttribute)
	{
		float MaxW = BaseAttribute->GetMaxWeight();
		if (MaxW > 0.0f && (BaseAttribute->GetCurrentWeight() / MaxW) > 1.3f)
		{
			bIsSprinting = false;
			return;
		}

		if (BaseAttribute->GetStemina() <= 0.0f)
		{
			bIsSprinting = false;
			return;
		}
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
		static const FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Held"), false);
		static const FGameplayTag ExhaustedTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Exhausted"), false);

		if ((BlockSprintTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(BlockSprintTag)) ||
			(HeavyCarryTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeavyCarryTag)) ||
			(OverloadedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(OverloadedTag)) ||
			(ImmobileTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(ImmobileTag)) ||
			(PushingTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(PushingTag)) ||
			(GroggyTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(GroggyTag)) ||
			(DeadTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(DeadTag)) ||
			(StunnedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(StunnedTag)) ||
			(HeldTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeldTag)) ||
			(ExhaustedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(ExhaustedTag)))
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

void AMainCharacter::StopSprinting()
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

		// 무거운 택배 들고 있는지 직접 확인
		if (CarryingComponent)
		{
			if (APackageBase* Pkg = Cast<APackageBase>(CarryingComponent->GetCarriedActor()))
			{
				if (Pkg->PackageType == EPackageType::Heavy)
				{
					bIsSprinting = false;
					return;
				}
			}
		}

		// 과적 2단계 이상 (소지 무게 > 130%)이면 달리기 차단
		if (BaseAttribute)
		{
			float MaxW = BaseAttribute->GetMaxWeight();
			if (MaxW > 0.0f && (BaseAttribute->GetCurrentWeight() / MaxW) > 1.3f)
			{
				bIsSprinting = false;
				return;
			}

			if (BaseAttribute->GetStemina() <= 0.0f)
			{
				bIsSprinting = false;
				return;
			}
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
			static const FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Held"), false);
			static const FGameplayTag ExhaustedTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Exhausted"), false);

			if ((BlockSprintTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(BlockSprintTag)) ||
				(HeavyCarryTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeavyCarryTag)) ||
				(OverloadedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(OverloadedTag)) ||
				(ImmobileTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(ImmobileTag)) ||
				(PushingTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(PushingTag)) ||
				(GroggyTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(GroggyTag)) ||
				(DeadTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(DeadTag)) ||
				(StunnedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(StunnedTag)) ||
				(HeldTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeldTag)) ||
				(ExhaustedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(ExhaustedTag)))
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

	// 과적 3단계 (소지 무게 > 150%)이면 점프 차단
	if (BaseAttribute)
	{
		float MaxW = BaseAttribute->GetMaxWeight();
		if (MaxW > 0.0f && (BaseAttribute->GetCurrentWeight() / MaxW) > 1.5f)
		{
			return; // 과적 3단계 점프 차단
		}
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

		// [2인 협동 운반 여부 확인]
		bool bIs2PersonCarrying = false;
		if (CarryingComponent && CarryingComponent->IsCarrying())
		{
			if (APackageBase* HeavyPackage = Cast<APackageBase>(CarryingComponent->GetCarriedActor()))
			{
				bIs2PersonCarrying = (HeavyPackage->PackageType == EPackageType::Heavy && HeavyPackage->CurrentCarriers.Num() >= 2);
			}
		}

		// 무거운 택배를 1명이서 들고 있을 때만 점프 차단 (2명이서 같이 들고 있을 때는 점프 허용)
		const bool bBlockHeavyCarryJump = HeavyCarryTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(HeavyCarryTag) && !bIs2PersonCarrying;

		if ((BlockJumpTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(BlockJumpTag)) ||
			bBlockHeavyCarryJump ||
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

// 손에 든 무기가 지금 "사용 중"이면 true (사용 중엔 슬롯 변경 차단). [WEAPON-016]
bool AMainCharacter::IsHandWeaponInUse() const
{
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		if (const AWeaponItemBase* HandWeapon = Cast<AWeaponItemBase>(CarryingComponent->GetCarriedActor()))
		{
			return HandWeapon->IsInUse();
		}
	}
	return false;
}

void AMainCharacter::OnSlot1()
{
	if (!CanAct() || bIsPushingMode || IsHandWeaponInUse()) return;

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
	if (!CanAct() || bIsPushingMode || IsHandWeaponInUse()) return;

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
	if (!CanAct() || bIsPushingMode || IsHandWeaponInUse()) return;

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
			StopSprinting();

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
	if (VisualComponent)
	{
		if (EmotionIndex == 0)
		{
			VisualComponent->ClearTemporaryOverride();
			VisualComponent->RefreshVisualState();
		}
		else
		{
			VisualComponent->ApplyCustomEmotion(EmotionIndex, EmoteColor);
		}
	}
	else
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

	// 1. 선택한 감정 표정과 색상으로 얼굴 머티리얼 임시 오버라이드 적용 (우선순위 70)
	if (VisualComponent)
	{
		FCharacterVisualPreset EmotePreset(static_cast<float>(EmotionIndex), EmoteColor, 1.5f, 70);
		VisualComponent->SetTemporaryOverride(EmotePreset);
	}
	else
	{
		SetEmotion(EmotionIndex, EmoteColor);
	}

	// 2. 이모트 몽타주 재생
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	AnimInstance->Montage_Play(EmoteMontage);
	CurrentEmoteMontage = EmoteMontage;

	// 3. 몽타주가 끝날 때를 감지하기 위해 델리게이트 바인딩
	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AMainCharacter::OnEmoteMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, EmoteMontage);
}

void AMainCharacter::StopEmote()
{
	if (CurrentEmoteMontage == nullptr)
	{
		return;
	}

	if (!HasAuthority())
	{
		ServerStopEmote();
	}
	else
	{
		MulticastStopEmote();
	}
}

void AMainCharacter::ServerStopEmote_Implementation()
{
	MulticastStopEmote();
}

void AMainCharacter::MulticastStopEmote_Implementation()
{
	if (CurrentEmoteMontage && GetMesh() && GetMesh()->GetAnimInstance())
	{
		GetMesh()->GetAnimInstance()->Montage_Stop(0.2f, CurrentEmoteMontage);
	}

	if (VisualComponent)
	{
		VisualComponent->ClearTemporaryOverride();
	}
	CurrentEmoteMontage = nullptr;
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
		// 이모트가 끝났거나 중단(이동으로 인한 취소)되었으므로 원래 우선순위 상태로 복원
		if (VisualComponent)
		{
			VisualComponent->ClearTemporaryOverride();
		}
		else
		{
			SetEmotion(0, FLinearColor(0.0f, 0.623294f, 1.0f, 1.0f));
		}
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
	if (bIsStunned)
	{
		return;
	}

	if (!HasAuthority())
	{
		ServerKnockdown();
		return;
	}

	// GAS GA_Knockdown 어빌리티 실행
	if (AbilitySystemComponent && KnockdownAbilitySpecHandle.IsValid())
	{
		AbilitySystemComponent->TryActivateAbility(KnockdownAbilitySpecHandle);
	}
}

void AMainCharacter::ServerKnockdown_Implementation()
{
	Knockdown();
}

void AMainCharacter::PlayHitReaction(float Duration)
{
	if (HasAuthority())
	{
		MulticastPlayHitReaction(Duration);
	}
	else
	{
		if (VisualComponent)
		{
			static const FGameplayTag HitTag = FGameplayTag::RequestGameplayTag(FName("State.Player.HitReaction"), false);
			VisualComponent->SetTagTemporaryOverride(HitTag, Duration);
		}
	}
}

void AMainCharacter::MulticastPlayHitReaction_Implementation(float Duration)
{
	if (VisualComponent)
	{
		static const FGameplayTag HitTag = FGameplayTag::RequestGameplayTag(FName("State.Player.HitReaction"), false);
		VisualComponent->SetTagTemporaryOverride(HitTag, Duration);
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
	if (VisualComponent)
	{
		VisualComponent->RefreshVisualState();
	}
	OnEnterGroggy();
}

void AMainCharacter::MulticastOnRevived_Implementation()
{
	if (VisualComponent)
	{
		VisualComponent->ClearTemporaryOverride();
		VisualComponent->RefreshVisualState();
	}
	OnRevived();
}

void AMainCharacter::MulticastOnDeath_Implementation()
{
	if (VisualComponent)
	{
		VisualComponent->RefreshVisualState();
	}
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
		if (VisualComponent)
		{
			VisualComponent->RefreshVisualState();
		}
	}

	// 무기/아이템 들고 있으면 해제
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		CarryingComponent->GrabOrDrop();
	}

	// 달리기 중이었다면 달리기 즉시 강제 취소
	StopSprinting();

	bIsPushingMode = true;
	CurrentPushedObject = TargetObject;

	if (AbilitySystemComponent)
	{
		static const FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
		if (PushingTag.IsValid())
		{
			AbilitySystemComponent->AddLooseGameplayTag(PushingTag);
		}

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

	if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(TargetObject->GetRootComponent()))
	{
		PrimComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		PrimComp->IgnoreActorWhenMoving(this, true);
		GetCapsuleComponent()->IgnoreActorWhenMoving(TargetObject, true);
		GetMesh()->IgnoreActorWhenMoving(TargetObject, true);
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

	// 상자 중심점과의 초기 수평 거리 기록 (메쉬 관통 방지용)
	PushInitialDistToBox = FVector::Dist2D(GetActorLocation(), BoxCenter);

	// 상자 로컬 공간 기준 손 접촉 앵커 위치 기록 (거리 이탈 감지용)
	if (TargetObject)
	{
		PushLocalAnchor = TargetObject->GetActorTransform().InverseTransformPosition(GetActorLocation());
	}

	// 밀기 시작 시 로컬 카메라 방향도 밀기 방향 기준 에임오프셋 범위 내로 즉시 보정
	if (IsLocallyControlled() && GetController())
	{
		FRotator ControlRot = GetController()->GetControlRotation();
		float ForwardYaw = Dir.Rotation().Yaw;
		float DeltaYaw = FRotator::NormalizeAxis(ControlRot.Yaw - ForwardYaw);
		if (FMath::Abs(DeltaYaw) > PushCameraYawLimit)
		{
			ControlRot.Yaw = FRotator::NormalizeAxis(ForwardYaw + FMath::Clamp(DeltaYaw, -PushCameraYawLimit, PushCameraYawLimit));
			GetController()->SetControlRotation(ControlRot);
		}
	}

	// 밀기 중에는 마우스 회전 간섭 차단
	bUseControllerRotationYaw = false;

	if (GetCharacterMovement())
	{
		GetCharacterMovement()->bOrientRotationToMovement = false;
	}

	LastCharacterLocation = GetActorLocation();

	if (StatusComponent)
	{
		static const FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
		StatusComponent->AddStatusTag(PushingTag);
	}


	UpdateCharacterSpeed();
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
		if (VisualComponent)
		{
			VisualComponent->ClearTemporaryOverride();
			VisualComponent->RefreshVisualState();
		}
		else
		{
			ChangeEmotion(0);
		}
	}

	bIsPushingMode = false;

	if (AbilitySystemComponent)
	{
		static const FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
		if (PushingTag.IsValid())
		{
			AbilitySystemComponent->RemoveLooseGameplayTag(PushingTag);
		}

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
	PushInitialDistToBox = 0.0f;
	PushLocalAnchor = FVector::ZeroVector;

	if (StatusComponent)
	{
		static const FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
		StatusComponent->RemoveStatusTag(PushingTag);
	}

	if (GetCharacterMovement())
	{
		GetCharacterMovement()->bOrientRotationToMovement = false;
	}

	// 2인 운반 중이 아니라면 1인칭 조작계(bUseControllerRotationYaw) 복구
	bool bIs2PersonCarrying = false;
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		if (APackageBase* HeavyPackage = Cast<APackageBase>(CarryingComponent->GetCarriedActor()))
		{
			bIs2PersonCarrying = (HeavyPackage->PackageType == EPackageType::Heavy && HeavyPackage->CurrentCarriers.Num() >= 2);
		}
	}

	if (!bIs2PersonCarrying)
	{
		bUseControllerRotationYaw = true;
	}

	// 밀기 모드 해제 후 현재 과적 및 기본 상태에 맞춰 이동속도 즉시 재계산 및 복구
	UpdateCharacterSpeed();
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
	// 리플리케이션(복제)에 따른 태그 및 상태 업데이트
	if (AbilitySystemComponent)
	{
		static const FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
		if (PushingTag.IsValid())
		{
			if (bIsPushingMode)
			{
				AbilitySystemComponent->AddLooseGameplayTag(PushingTag);
			}
			else
			{
				AbilitySystemComponent->RemoveLooseGameplayTag(PushingTag);
			}
		}
	}

	if (bIsPushingMode)
	{
		bUseControllerRotationYaw = false;
		if (GetCharacterMovement())
		{
			GetCharacterMovement()->bOrientRotationToMovement = false;
		}

		if (CurrentPushedObject)
		{
			if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(CurrentPushedObject->GetRootComponent()))
			{
				PrimComp->IgnoreActorWhenMoving(this, true);
			}
			GetCapsuleComponent()->IgnoreActorWhenMoving(CurrentPushedObject, true);
			GetMesh()->IgnoreActorWhenMoving(CurrentPushedObject, true);
		}
	}
	else
	{
		if (CurrentPushedObject)
		{
			if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(CurrentPushedObject->GetRootComponent()))
			{
				PrimComp->IgnoreActorWhenMoving(this, false);
			}
			GetCapsuleComponent()->IgnoreActorWhenMoving(CurrentPushedObject, false);
			GetMesh()->IgnoreActorWhenMoving(CurrentPushedObject, false);
		}

		if (GetCharacterMovement())
		{
			GetCharacterMovement()->bOrientRotationToMovement = false;
		}
	}

	UpdateCharacterSpeed();
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

void AMainCharacter::OnRep_IsHoldingRevive()
{
	static const FGameplayTag ReviveTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Reviving"), false);
	if (StatusComponent)
	{
		if (bIsHoldingRevive)
		{
			StatusComponent->AddStatusTag(ReviveTag);
		}
		else
		{
			StatusComponent->RemoveStatusTag(ReviveTag);
		}
	}

	if (VisualComponent)
	{
		VisualComponent->RefreshVisualState();
	}
}

void AMainCharacter::StartReviveHold(AMainCharacter* Target)
{
	if (!Target || !Target->bIsGroggy || Target->bIsDead || Target == this)
	{
		return;
	}

	if (!HasAuthority())
	{
		ServerStartReviveHold(Target);
	}

	bIsHoldingRevive = true;
	CurrentReviveHoldTime = 0.0f;
	CurrentReviveTarget = Target;

	// 살리는 동안 이동 즉시 정지 (이동 차단)
	if (GetCharacterMovement())
	{
		GetCharacterMovement()->StopMovementImmediately();
	}

	// 부활 태그 추가 및 비주얼 갱신
	static const FGameplayTag ReviveTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Reviving"), false);
	if (StatusComponent)
	{
		StatusComponent->AddStatusTag(ReviveTag);
	}

	// GAS 어빌리티 활성화
	if (AbilitySystemComponent && ReviveAbilitySpecHandle.IsValid())
	{
		AbilitySystemComponent->TryActivateAbility(ReviveAbilitySpecHandle);
	}

	if (VisualComponent)
	{
		VisualComponent->RefreshVisualState();
	}

	OnReviveHoldStarted(Target);
	Target->SetReviveProgress(0.0f);
}

void AMainCharacter::ServerStartReviveHold_Implementation(AMainCharacter* Target)
{
	StartReviveHold(Target);
}

void AMainCharacter::CancelReviveHold()
{
	if (!bIsHoldingRevive)
	{
		return;
	}

	if (!HasAuthority())
	{
		ServerCancelReviveHold();
	}

	bIsHoldingRevive = false;
	CurrentReviveHoldTime = 0.0f;

	// 부활 태그 제거 및 비주얼 갱신
	static const FGameplayTag ReviveTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Reviving"), false);
	if (StatusComponent)
	{
		StatusComponent->RemoveStatusTag(ReviveTag);
	}

	if (AbilitySystemComponent && ReviveAbilitySpecHandle.IsValid())
	{
		AbilitySystemComponent->CancelAbilityHandle(ReviveAbilitySpecHandle);
	}

	if (VisualComponent)
	{
		VisualComponent->RefreshVisualState();
	}

	if (CurrentReviveTarget.IsValid())
	{
		CurrentReviveTarget->SetReviveProgress(0.0f);
	}
	CurrentReviveTarget = nullptr;

	OnReviveHoldEnded(false);
}

void AMainCharacter::ServerCancelReviveHold_Implementation()
{
	CancelReviveHold();
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

	// 부활 태그 제거 및 비주얼 갱신
	static const FGameplayTag ReviveTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Reviving"), false);
	if (StatusComponent)
	{
		StatusComponent->RemoveStatusTag(ReviveTag);
	}

	if (AbilitySystemComponent && ReviveAbilitySpecHandle.IsValid())
	{
		AbilitySystemComponent->CancelAbilityHandle(ReviveAbilitySpecHandle);
	}

	if (VisualComponent)
	{
		VisualComponent->RefreshVisualState();
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

	// 체력 감소(대미지 피격 시)
	if (Data.NewValue < Data.OldValue)
	{
		if (bIsHoldingRevive)
		{
			CancelReviveHold();
		}

		if (!bIsDead && !bIsGroggy && Data.NewValue > 0.0f)
		{
			PlayHitReaction(0.4f);
		}
	}
}

float AMainCharacter::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser)
{
	float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (ActualDamage > 0.0f)
	{
		if (bIsHoldingRevive)
		{
			CancelReviveHold();
		}

		if (!bIsDead && !bIsGroggy)
		{
			PlayHitReaction(0.4f);
		}
	}
	return ActualDamage;
}

// ---------------------------------------------------------
// [발광(손전등 대체) 및 배터리 시스템 구현]
// ---------------------------------------------------------

void AMainCharacter::ToggleFlashlight()
{
	// 배터리가 0인데 켜려고 하면 실행 차단
	if (!bIsFlashlightOn && BaseAttribute && BaseAttribute->GetBattery() <= 0.0f)
	{
		return;
	}

	ServerToggleFlashlight(!bIsFlashlightOn);
}

void AMainCharacter::ServerToggleFlashlight_Implementation(bool bNewState)
{
	// 배터리가 없으면 켜기 불가
	if (bNewState && BaseAttribute && BaseAttribute->GetBattery() <= 0.0f)
	{
		bIsFlashlightOn = false;
		UpdateFlashlightVisuals();
		return;
	}

	bIsFlashlightOn = bNewState;
	UpdateFlashlightVisuals();
}

void AMainCharacter::CycleFlashlightColor()
{
	ServerCycleFlashlightColor();
}

void AMainCharacter::ServerCycleFlashlightColor_Implementation()
{
	if (FlashlightColorPresets.Num() > 0)
	{
		FlashlightColorIndex = (FlashlightColorIndex + 1) % FlashlightColorPresets.Num();
		UpdateFlashlightVisuals();
	}
}

FLinearColor AMainCharacter::GetCurrentFlashlightColor() const
{
	if (FlashlightColorPresets.IsValidIndex(FlashlightColorIndex))
	{
		return FlashlightColorPresets[FlashlightColorIndex];
	}
	return FLinearColor::White;
}

void AMainCharacter::OnRep_IsFlashlightOn()
{
	UpdateFlashlightVisuals();
}

void AMainCharacter::OnRep_FlashlightColorIndex()
{
	UpdateFlashlightVisuals();
}

void AMainCharacter::UpdateFlashlightVisuals()
{
	FLinearColor CurrentColor = GetCurrentFlashlightColor();
	float TargetPower = bIsFlashlightOn ? FlashlightEmissionPower : 0.0f;

	// 메시 동적 머티리얼 인스턴스의 Emission Color 및 Power 파라미터 갱신
	for (UMaterialInstanceDynamic* DMI : BodyMaterialInstances)
	{
		if (DMI)
		{
			DMI->SetVectorParameterValue(FName("Emission Color"), CurrentColor);
			DMI->SetScalarParameterValue(FName("Emission Power"), TargetPower);
		}
	}

	// 포인트 라이트 조명 컴포넌트 갱신
	if (FlashlightLight)
	{
		FlashlightLight->SetVisibility(bIsFlashlightOn);
		FlashlightLight->SetLightColor(CurrentColor);
	}
}

