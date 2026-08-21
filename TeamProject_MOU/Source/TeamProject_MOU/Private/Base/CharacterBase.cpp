// Fill out your copyright notice in the Description page of Project Settings.

#include "Base/CharacterBase.h"

#include "Base/BaseAttributeSet.h"
#include "Components/GrabFollowComponent.h"
#include "Components/StatusComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ACharacterBase::ACharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// GAS 어빌리티 시스템 컴포넌트 생성 및 네트워크 리플리케이션 설정
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(AscReplicationMode);

	// 플레이어 및 방해 NPC 공통 상태 관리 컴포넌트 생성
	StatusComponent = CreateDefaultSubobject<UStatusComponent>(TEXT("StatusComponent"));

	// 잡힌 캐릭터의 위치 및 회전을 운반자 소켓에 동기화하는 컴포넌트 생성
	GrabFollowComponent = CreateDefaultSubobject<UGrabFollowComponent>(TEXT("GrabFollowComponent"));

	// 캡슐 콜리전 기본 크기 설정
	GetCapsuleComponent()->InitCapsuleSize(35.0f, 90.0f);

	// 컨트롤러 회전 사용 안 함 (캐릭터 이동 방향으로 자동 회전)
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// 이동 물리 및 점프 관련 기본값 설정
	GetCharacterMovement()->JumpZVelocity = 500.0f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 300.0f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.0f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.0f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// 기본 AttributeSet 생성 및 등록
	BaseAttribute = CreateDefaultSubobject<UBaseAttributeSet>(TEXT("AttributeSet"));
	BaseAttributeSet.Add(BaseAttribute);
}

void ACharacterBase::BeginPlay()
{
	Super::BeginPlay();

	// ASC가 Character에 있으므로 서버와 모든 클라이언트에서 ActorInfo를 초기화합니다.
	// AI NPC는 클라이언트에서 PlayerState가 없어 OnRep_PlayerState가 호출되지 않을 수 있습니다.
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
		AbilitySystemComponent->SetReplicationMode(AscReplicationMode);
	}

	// Attribute 변경 감지 델리게이트 바인딩
	BindAttributeChangeDelegates();
}

void ACharacterBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (AbilitySystemComponent)
	{
		// 서버 환경: GAS 어빌리티 정보 초기화 및 초기 스킬 부여
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
		InitializeAbilityMulti(InitalAbilities, 1);
		BindAttributeChangeDelegates();
	}
}

void ACharacterBase::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	if (AbilitySystemComponent)
	{
		// 클라이언트 환경: GAS 어빌리티 정보 초기화
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
		BindAttributeChangeDelegates();
	}
}

void ACharacterBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ACharacterBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

UAbilitySystemComponent* ACharacterBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

bool ACharacterBase::CanMove() const
{
	// StatusComponent를 이용해 이동 가능 상태(기절/잡힘/넉백 없음) 확인
	if (StatusComponent)
	{
		return StatusComponent->CanMove();
	}

	return true;
}

bool ACharacterBase::CanAct() const
{
	// StatusComponent를 이용해 행동 가능 상태(기절/잡힘 없음) 확인
	if (StatusComponent)
	{
		return StatusComponent->CanAct();
	}

	return true;
}

FGameplayAbilitySpecHandle ACharacterBase::InitializeAbility(TSubclassOf<UGameplayAbility> AbilityToGet, int32 AbilityLevel)
{
	if (HasAuthority() && AbilitySystemComponent && AbilityToGet)
	{
		return AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(AbilityToGet, AbilityLevel));
	}

	return FGameplayAbilitySpecHandle();
}

void ACharacterBase::InitializeAbilityMulti(TArray<TSubclassOf<UGameplayAbility>> AbilityToAcquire, int32 AbilityLevel)
{
	if (!HasAuthority())
	{
		return;
	}

	for (const TSubclassOf<UGameplayAbility>& AbilityItem : AbilityToAcquire)
	{
		if (AbilityItem)
		{
			InitializeAbility(AbilityItem, AbilityLevel);
		}
	}
}

void ACharacterBase::BindAttributeChangeDelegates()
{
	if (AttributeDelegatesBound || !AbilitySystemComponent || !BaseAttribute)
	{
		return;
	}

	AttributeDelegatesBound = true;

	// 체력(Health) 변경 델리게이트 바인딩
	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute())
		.AddUObject(this, &ACharacterBase::HandleHealthChanged);

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMaxHealthAttribute())
		.AddUObject(this, &ACharacterBase::HandleMaxHealthChanged);

	// 스태미나(Stemina) 변경 델리게이트 바인딩
	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetSteminaAttribute())
		.AddUObject(this, &ACharacterBase::HandleSteminaChanged);

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMaxSteminaAttribute())
		.AddUObject(this, &ACharacterBase::HandleMaxSteminaChanged);

	// 이동속도(MoveSpeed) 변경 델리게이트 바인딩
	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMoveSpeedAttribute())
		.AddUObject(this, &ACharacterBase::HandleMoveSpeedChanged);

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMaxMoveSpeedAttribute())
		.AddUObject(this, &ACharacterBase::HandleMaxMoveSpeedChanged);

	// 무게 변경 델리게이트 바인딩
	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetCurrentWeightAttribute())
		.AddUObject(this, &ACharacterBase::HandleCurrentWeightChanged);

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMaxWeightAttribute())
		.AddUObject(this, &ACharacterBase::HandleMaxWeightChanged);

	// 초기 속도 값 동기화
	if (GetCharacterMovement())
	{
		GetCharacterMovement()->MaxWalkSpeed = BaseAttribute->GetMoveSpeed();
	}
}

void ACharacterBase::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	// 체력 변경 시 Blueprint 이벤트 호출 (UI 업데이트)
	OnHealthUpdated(BaseAttribute->GetHealth(), BaseAttribute->GetMaxHealth());
}

void ACharacterBase::HandleMaxHealthChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	OnHealthUpdated(BaseAttribute->GetHealth(), BaseAttribute->GetMaxHealth());
}

void ACharacterBase::HandleSteminaChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	// 스태미나 변경 시 Blueprint 이벤트 호출
	OnSteminaupdated(BaseAttribute->GetStemina(), BaseAttribute->GetMaxStemina());
}

void ACharacterBase::HandleMaxSteminaChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	OnSteminaupdated(BaseAttribute->GetStemina(), BaseAttribute->GetMaxStemina());
}

void ACharacterBase::HandleMoveSpeedChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	// 이동속도 속성 변경 시 CharacterMovement의 MaxWalkSpeed 동기화
	GetCharacterMovement()->MaxWalkSpeed = Data.NewValue;
	OnSpeedUpdated(BaseAttribute->GetMoveSpeed(), BaseAttribute->GetMaxMoveSpeed());
}

void ACharacterBase::HandleMaxMoveSpeedChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	GetCharacterMovement()->MaxWalkSpeed = Data.NewValue;
	OnSpeedUpdated(BaseAttribute->GetMoveSpeed(), BaseAttribute->GetMaxMoveSpeed());
}

void ACharacterBase::HandleCurrentWeightChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute) return;
	UpdateEncumbranceState(Data.NewValue, BaseAttribute->GetMaxWeight());
}

void ACharacterBase::HandleMaxWeightChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute) return;
	UpdateEncumbranceState(BaseAttribute->GetCurrentWeight(), Data.NewValue);
}

void ACharacterBase::UpdateEncumbranceState(float InCurrentWeight, float InMaxWeight)
{
	if (InMaxWeight <= 0.0f) return;

	float WeightRatio = InCurrentWeight / InMaxWeight;

	static const FGameplayTag Encumbered_HeavyTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Encumbered.Heavy"), false);
	static const FGameplayTag Encumbered_OverloadedTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Encumbered.Overloaded"), false);
	static const FGameplayTag Encumbered_ImmobileTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Encumbered.Immobile"), false);

	if (HasAuthority() && AbilitySystemComponent)
	{
		if (ActiveEncumbranceEffectHandle.IsValid())
		{
			AbilitySystemComponent->RemoveActiveGameplayEffect(ActiveEncumbranceEffectHandle);
			ActiveEncumbranceEffectHandle.Invalidate();
		}

		TSubclassOf<UGameplayEffect> EffectToApply = nullptr;

		if (WeightRatio > 1.5f)
		{
			EffectToApply = EncumberedTier3Effect;
		}
		else if (WeightRatio > 1.3f)
		{
			EffectToApply = EncumberedTier2Effect;
		}
		else if (WeightRatio > 1.0f)
		{
			EffectToApply = EncumberedTier1Effect;
		}

		if (EffectToApply)
		{
			FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
			Context.AddSourceObject(this);
			ActiveEncumbranceEffectHandle = AbilitySystemComponent->ApplyGameplayEffectToSelf(
				EffectToApply.GetDefaultObject(), 1.0f, Context);
		}
		else if (BaseAttribute)
		{
			// GE가 할당되지 않았거나 과적이 해제(<= 1.0f)되었을 때 C++ 속도 직접 복구 및 적용 폴백
			float TargetSpeed = 300.0f;
			if (WeightRatio > 1.5f)
			{
				TargetSpeed = 0.0f;
			}
			else if (WeightRatio > 1.3f)
			{
				TargetSpeed = 150.0f;
			}
			else if (WeightRatio > 1.0f)
			{
				TargetSpeed = 255.0f;
			}

			BaseAttribute->SetMoveSpeed(TargetSpeed);
			if (GetCharacterMovement())
			{
				GetCharacterMovement()->MaxWalkSpeed = TargetSpeed;
			}
		}
	}

	if (StatusComponent)
	{
		StatusComponent->RemoveStatusTag(Encumbered_HeavyTag);
		StatusComponent->RemoveStatusTag(Encumbered_OverloadedTag);
		StatusComponent->RemoveStatusTag(Encumbered_ImmobileTag);

		if (WeightRatio > 1.5f)
		{
			StatusComponent->AddStatusTag(Encumbered_ImmobileTag);
		}
		else if (WeightRatio > 1.3f)
		{
			StatusComponent->AddStatusTag(Encumbered_OverloadedTag);
		}
		else if (WeightRatio > 1.0f)
		{
			StatusComponent->AddStatusTag(Encumbered_HeavyTag);
		}
	}
}

void ACharacterBase::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.Reset();
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->GetOwnedGameplayTags(TagContainer);
	}
}

bool ACharacterBase::HasMatchingGameplayTag(FGameplayTag TagToCheck) const
{
	if (AbilitySystemComponent)
	{
		return AbilitySystemComponent->HasMatchingGameplayTag(TagToCheck);
	}
	return false;
}

bool ACharacterBase::HasAllMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const
{
	if (AbilitySystemComponent)
	{
		return AbilitySystemComponent->HasAllMatchingGameplayTags(TagContainer);
	}
	return false;
}

bool ACharacterBase::HasAnyMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const
{
	if (AbilitySystemComponent)
	{
		return AbilitySystemComponent->HasAnyMatchingGameplayTags(TagContainer);
	}
	return false;
}

float ACharacterBase::GetPushResistance_Implementation() const
{
	return 0.0f;
}

void ACharacterBase::Push_Implementation(AActor* Pusher, FVector PushDirection)
{
	LaunchCharacter(PushDirection * 500.0f + FVector(0, 0, 200.0f), true, true);
}
