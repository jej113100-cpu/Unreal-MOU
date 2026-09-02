#include "Components/GrabFollowComponent.h"

#include "Components/StatusComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

UGrabFollowComponent::UGrabFollowComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UGrabFollowComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UGrabFollowComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 실제 잡기 위치는 서버만 결정하고 클라이언트는 이동 복제 결과를 사용합니다.
	if (GetOwner() && GetOwner()->HasAuthority() && CarrierCharacter)
	{
		SyncGrabTransform();
	}
}

void UGrabFollowComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UGrabFollowComponent, CarrierCharacter);
	DOREPLIFETIME(UGrabFollowComponent, GrabSocketName);
	DOREPLIFETIME(UGrabFollowComponent, RelativeOffset);
	DOREPLIFETIME(UGrabFollowComponent, bFollowRotation);
}

void UGrabFollowComponent::StartGrabFollow(ACharacter* NewCarrier, FName NewSocketName, FVector NewRelativeOffset, bool bInheritRotation)
{
	// 잡기 상태의 원본은 서버만 변경합니다. 클라이언트는 복제된 값을 사용합니다.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	CarrierCharacter = NewCarrier;
	GrabSocketName = NewSocketName;
	RelativeOffset = NewRelativeOffset;
	bFollowRotation = bInheritRotation;

	DisableGrabbedMovement();
	ApplyHeldTag(true);
	SyncGrabTransform();
	GetOwner()->ForceNetUpdate();
}

void UGrabFollowComponent::StopGrabFollow()
{
	// 잡기 해제도 서버에서 확정해 모든 클라이언트에 동일하게 복제합니다.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		FRotator CurrentRot = OwnerCharacter->GetActorRotation();
		OwnerCharacter->SetActorRotation(FRotator(0.0f, CurrentRot.Yaw, 0.0f));
	}

	CarrierCharacter = nullptr;
	ApplyHeldTag(false);
	RestoreGrabbedMovement();
	GetOwner()->ForceNetUpdate();
}

void UGrabFollowComponent::DisableGrabbedMovement()
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* MovementComponent = OwnerCharacter
		? OwnerCharacter->GetCharacterMovement()
		: nullptr;
	if (!MovementComponent)
	{
		return;
	}

	if (!bHasSavedMovementMode)
	{
		PreviousMovementMode = static_cast<uint8>(MovementComponent->MovementMode);
		PreviousCustomMovementMode = MovementComponent->CustomMovementMode;
		bHasSavedMovementMode = true;
	}

	MovementComponent->StopMovementImmediately();
	MovementComponent->DisableMovement();
}

void UGrabFollowComponent::RestoreGrabbedMovement()
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* MovementComponent = OwnerCharacter
		? OwnerCharacter->GetCharacterMovement()
		: nullptr;
	if (!MovementComponent || !bHasSavedMovementMode)
	{
		return;
	}

	MovementComponent->SetMovementMode(
		static_cast<EMovementMode>(PreviousMovementMode), PreviousCustomMovementMode);
	bHasSavedMovementMode = false;
}

void UGrabFollowComponent::OnRep_GrabState()
{
	// 실제 위치는 서버가 갱신하며 클라이언트는 Character Movement 복제를 따릅니다.
}

void UGrabFollowComponent::SyncGrabTransform()
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter || !CarrierCharacter)
	{
		return;
	}

	USkeletalMeshComponent* CarrierMesh = CarrierCharacter->GetMesh();
	FTransform TargetTransform = CarrierCharacter->GetActorTransform();

	if (CarrierMesh && CarrierMesh->DoesSocketExist(GrabSocketName))
	{
		TargetTransform = CarrierMesh->GetSocketTransform(GrabSocketName, RTS_World);
	}

	const FVector TargetLocation = TargetTransform.TransformPosition(RelativeOffset);
	const FRotator TargetRotation = bFollowRotation ? TargetTransform.GetRotation().Rotator() : OwnerCharacter->GetActorRotation();

	OwnerCharacter->SetActorLocationAndRotation(TargetLocation, TargetRotation, false, nullptr, ETeleportType::TeleportPhysics);

	if (UCharacterMovementComponent* MovementComponent = OwnerCharacter->GetCharacterMovement())
	{
		MovementComponent->StopMovementImmediately();
	}
}

void UGrabFollowComponent::ApplyHeldTag(bool bAdd) const
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	const FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Held"), false);
	if (!HeldTag.IsValid())
	{
		return;
	}

	if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (UStatusComponent* StatusComponent = OwnerCharacter->FindComponentByClass<UStatusComponent>())
		{
			if (bAdd)
			{
				StatusComponent->AddStatusTag(HeldTag);
			}
			else
			{
				StatusComponent->RemoveStatusTag(HeldTag);
			}
		}
	}
}
