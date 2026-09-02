#include "Traps/Actors/TrapBearTrap.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Traps/Components/TrapTriggerComponent.h"
#include "Traps/Components/TrapPayloadComponent.h"
#include "Traps/Data/TrapDataAsset.h"
#include "Traps/Interfaces/TrapTargetInterface.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

ATrapBearTrap::ATrapBearTrap()
{
	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
	BaseMesh->SetupAttachment(RootScene);
	BaseMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));

	LeftJawMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftJawMesh"));
	LeftJawMesh->SetupAttachment(BaseMesh);
	LeftJawMesh->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	LeftJawMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	LeftJawMesh->CanCharacterStepUpOn = ECB_No;
	LeftJawMesh->SetCanEverAffectNavigation(false);

	RightJawMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightJawMesh"));
	RightJawMesh->SetupAttachment(BaseMesh);
	RightJawMesh->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	RightJawMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	RightJawMesh->CanCharacterStepUpOn = ECB_No;
	RightJawMesh->SetCanEverAffectNavigation(false);

	if (BaseTriggerBox)
	{
		BaseTriggerBox->SetupAttachment(BaseMesh);
		BaseTriggerBox->InitBoxExtent(FVector(60.0f, 60.0f, 30.0f));
	}

	if (PayloadComponent)
	{
		PayloadComponent->DefaultHazardType = ETrapHazardType::CrowdControl;
	}
}

void ATrapBearTrap::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATrapBearTrap, TrappedActor);
}

bool ATrapBearTrap::CanActivate() const
{
	return (CurrentState == ETrapState::Idle) && (TrappedActor == nullptr);
}

void ATrapBearTrap::BeginPlay()
{
	Super::BeginPlay();

	if (LeftJawMesh)
	{
		LeftJawMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		LeftJawMesh->CanCharacterStepUpOn = ECB_No;
	}
	if (RightJawMesh)
	{
		RightJawMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		RightJawMesh->CanCharacterStepUpOn = ECB_No;
	}
}

void ATrapBearTrap::OnStateEntered(ETrapState NewState)
{
	Super::OnStateEntered(NewState);

	if (HasAuthority())
	{
		if (NewState == ETrapState::Active)
		{
			// 부모 ATrapBase의 1초 자동 타이머(StateTimerHandle)를 취소하여
			// 곰덫이 1초 만에 스스로 Cooldown으로 넘어가는 것을 원천 차단!
			if (GetWorld())
			{
				GetWorld()->GetTimerManager().ClearTimer(StateTimerHandle);
			}

			MulticastOnJawSnap(true);
		}
		else if (NewState == ETrapState::Disarmed)
		{
			ReleaseTrappedActor();
		}
	}
}

void ATrapBearTrap::MulticastOnJawSnap_Implementation(bool bSnapped)
{
	OnJawSnap_BP(bSnapped);
}

void ATrapBearTrap::ExecuteTrapPayload()
{
	if (!HasAuthority() || !TriggerComponent)
	{
		return;
	}

	const TArray<AActor*>& Overlapped = TriggerComponent->GetOverlappingActors();
	AActor* TargetToTrap = Overlapped.Num() > 0 ? Overlapped[0] : nullptr;

	if (TargetToTrap && TargetToTrap->GetClass()->ImplementsInterface(UTrapTargetInterface::StaticClass()))
	{
		TrappedActor = TargetToTrap;
		CurrentEscapeInputCount = 0;

		// 1. 대미지 및 상태이상 적용
		if (PayloadComponent)
		{
			PayloadComponent->ExecutePayloadOnActor(TrappedActor, TrapData);
		}

		// 2. 소지 물품 강제 드랍
		ITrapTargetInterface::Execute_ForceDropCarriedItem(TrappedActor);

		// 3. 캐릭터 이동 완전 속박 (Rooting)
		if (ACharacter* Char = Cast<ACharacter>(TrappedActor))
		{
			if (UCharacterMovementComponent* MoveComp = Char->GetCharacterMovement())
			{
				MoveComp->DisableMovement();
				MoveComp->StopMovementImmediately();
			}
		}

		// 4. 자동 해제 타이머 가동
		if (GetWorld())
		{
			GetWorld()->GetTimerManager().SetTimer(
				AutoReleaseTimerHandle,
				this,
				&ATrapBearTrap::ReleaseTrappedActor,
				MaxHoldDuration,
				false
			);
		}
	}
}

void ATrapBearTrap::RequestSelfEscape(AActor* EscapingActor)
{
	if (!HasAuthority() || EscapingActor != TrappedActor)
	{
		return;
	}

	CurrentEscapeInputCount++;
	if (CurrentEscapeInputCount >= RequiredEscapeInputs)
	{
		ReleaseTrappedActor();
	}
}

void ATrapBearTrap::AssistDisarm(AActor* HelperActor)
{
	if (!HasAuthority() || !HelperActor || HelperActor == TrappedActor)
	{
		return;
	}

	// 팀원이 도와주면 즉시 풀림!
	ReleaseTrappedActor();
}

void ATrapBearTrap::ReleaseTrappedActor()
{
	if (!HasAuthority())
	{
		return;
	}

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(AutoReleaseTimerHandle);
	}

	// 속박 해제: 정상 보행 모드 복구
	if (TrappedActor)
	{
		if (ACharacter* Char = Cast<ACharacter>(TrappedActor))
		{
			if (UCharacterMovementComponent* MoveComp = Char->GetCharacterMovement())
			{
				MoveComp->SetMovementMode(MOVE_Walking);
			}
		}
	}

	TrappedActor = nullptr;
	CurrentEscapeInputCount = 0;

	MulticastOnJawSnap(false);
	SetTrapState(ETrapState::Cooldown);
}

void ATrapBearTrap::OnRep_TrappedActor()
{
	// MulticastOnJawSnap이 모든 클라이언트에서 직접 OnJawSnap_BP를 호출하므로 중복 실행 방지
}
