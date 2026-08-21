#include "Animation/MainAnimInstance.h"
#include "Player/MainCharacter.h"
#include "Components/CarryingComponent.h"
#include "Components/StatusComponent.h"
#include "Base/PackageBase.h"
#include "GameFramework/CharacterMovementComponent.h"

UMainAnimInstance::UMainAnimInstance()
{
}

void UMainAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	// 소유자 Pawn을 MainCharacter로 캐스팅 및 무브먼트 컴포넌트 저장
	MainCharacter = Cast<AMainCharacter>(TryGetPawnOwner());
	if (MainCharacter)
	{
		MovementComponent = MainCharacter->GetCharacterMovement();
	}
}

void UMainAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	// 캐릭터 참조가 없으면 재시도
	if (!MainCharacter)
	{
		MainCharacter = Cast<AMainCharacter>(TryGetPawnOwner());
		if (MainCharacter)
		{
			MovementComponent = MainCharacter->GetCharacterMovement();
		}
	}

	if (!MainCharacter || !MovementComponent)
	{
		return;
	}

	// 1. 수평 이동 속도 계산 (XY 평면 속도)
	FVector Velocity = MainCharacter->GetVelocity();
	GroundSpeed = Velocity.Size2D();

	// 2. 가속도 입력 및 이동 여부 판단
	bool bHasAcceleration = MovementComponent->GetCurrentAcceleration().SizeSquared() > 0.0f;

	// 시뮬레이트 프록시(다른 플레이어)는 가속도가 기본적으로 동기화되지 않으므로 속도만으로 판단
	bool bIsProxy = !MainCharacter->IsLocallyControlled();
	bShouldMove = (GroundSpeed > 3.0f) && (bHasAcceleration || bIsProxy);

	// 3. 점프/낙하 공중 체류 여부
	bIsFalling = MovementComponent->IsFalling();

	// 4. 달리기(Sprint) 중인지 여부
	bIsSprinting = MainCharacter->IsSprinting();

	// 5. 물품 들고 있는지(Carrying) 여부 + 무거운 택배 1인/2인 구분
	UCarryingComponent* CarryingComp = MainCharacter->GetCarryingComponent();
	bIsCarrying = (CarryingComp != nullptr) && CarryingComp->IsCarrying();

	// [Heavy 전용] 무거운 택배 1인 단독 운반 vs 2인 협동 구분
	bIsHeavySingleCarry = false;
	bIsSecondCarrier = false;

	if (bIsCarrying && CarryingComp)
	{
		if (APackageBase* Package = Cast<APackageBase>(CarryingComp->GetCarriedActor()))
		{
			if (Package->PackageType == EPackageType::Heavy)
			{
				int32 CarrierCount = Package->CurrentCarriers.Num();

				// 1인: 혼자 들고 있음 → 드래그 애니메이션
				bIsHeavySingleCarry = (CarrierCount == 1);

				// 2인: 내가 두 번째 운반자인지 확인 (CurrentCarriers[1] == 나)
				if (CarrierCount == 2)
				{
					int32 MyIndex = Package->CurrentCarriers.IndexOfByKey(MainCharacter);
					bIsSecondCarrier = (MyIndex == 1);
				}
			}
		}
	}

	// 6. 물체 밀기(Pushing) 및 상태 이상(기절 / 잡힘) 여부
	UStatusComponent* StatusComp = MainCharacter->GetStatusComponent();
	if (StatusComp)
	{
		static const FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Pushing"), false);
		static const FGameplayTag StunTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);
		static const FGameplayTag PrimaryStunTag = FGameplayTag::RequestGameplayTag(FName("State.Primary.Stuned"), false);
		static const FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Held"), false);

		bIsPushing = MainCharacter->bIsPushingMode;
		bIsStunned = (StunTag.IsValid() && StatusComp->HasStatusTag(StunTag)) || (PrimaryStunTag.IsValid() && StatusComp->HasStatusTag(PrimaryStunTag));
		bIsHeld = (HeldTag.IsValid() && StatusComp->HasStatusTag(HeldTag));
	}
	else
	{
		bIsPushing = MainCharacter->bIsPushingMode;
		bIsStunned = false;
		bIsHeld = false;
	}
}