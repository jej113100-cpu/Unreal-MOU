#include "Components/CarryingComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Base/ItemBase.h"
#include "Base/PackageBase.h"
#include "Base/EventObjectBase.h"
#include "Base/CharacterBase.h"
#include "Base/BaseAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "Components/CapsuleComponent.h"
#include "Player/MainCharacter.h"
#include "AbilitySystemComponent.h"
#include "Ability/GA_CarryItem.h"
#include "Ability/GA_HeavyCarry.h"
#include "Ability/GA_CarryCharacter.h"
#include "Components/InventoryComponent.h"

UCarryingComponent::UCarryingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCarryingComponent::BeginPlay()
{
	Super::BeginPlay();
	
	SetIsReplicated(true);
}

void UCarryingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCarryingComponent, CarriedActor);
}

bool UCarryingComponent::IsCarryingCharacter() const
{
	return Cast<AMainCharacter>(CarriedActor) != nullptr;
}

#include "GameFramework/CharacterMovementComponent.h"

void UCarryingComponent::OnRep_CarriedActor(AActor* OldCarriedActor)
{
	// 클라이언트에서 내려놓았을 때 충돌 다시 켜기
	if (OldCarriedActor && !CarriedActor)
	{
		if (AMainCharacter* DroppedChar = Cast<AMainCharacter>(OldCarriedActor))
		{
			if (UCapsuleComponent* Capsule = DroppedChar->GetCapsuleComponent())
			{
				Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
			if (UCharacterMovementComponent* MoveComp = DroppedChar->GetCharacterMovement())
			{
				MoveComp->SetMovementMode(MOVE_Falling);
			}
		}
	}
	
	// 클라이언트에서 들었을 때 충돌 끄기
	if (CarriedActor)
	{
		if (AMainCharacter* GrabbedChar = Cast<AMainCharacter>(CarriedActor))
		{
			if (UCapsuleComponent* Capsule = GrabbedChar->GetCapsuleComponent())
			{
				Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			if (UCharacterMovementComponent* MoveComp = GrabbedChar->GetCharacterMovement())
			{
				MoveComp->SetMovementMode(MOVE_None);
			}
		}
	}

	// 서버로부터 CarriedActor 값이 복제되어 클라이언트에 도착했을 때 애니메이션 재생 등을 트리거
	OnCarriedStateChanged.Broadcast(CarriedActor);
}

void UCarryingComponent::GrabOrDrop()
{
	if (!GetOwner()->HasAuthority())
	{
		ServerGrabOrDrop();
		return;
	}

	if (IsCarrying())
	{
		// Drop 로직
		if (AItemBase* Item = Cast<AItemBase>(CarriedActor))
		{
			// 놓을 위치 (캐릭터 앞쪽 바닥 근처)
			FVector DropLoc = GetOwner()->GetActorLocation() + GetOwner()->GetActorForwardVector() * 80.0f;
			// 택배인 경우 운반자 리스트에서 본인 제거 (과적/속도 원상복구)
			if (APackageBase* Package = Cast<APackageBase>(Item))
			{
				Package->RemoveCarrier(GetOwner());
				
				// 아직 운반자가 남아있다면 바닥에 떨어뜨리지 않고 유지합니다.
				if (Package->CurrentCarriers.Num() == 0)
				{
					Item->Drop(DropLoc, GetOwner());
				}
			}
			else
			{
				Item->Drop(DropLoc, GetOwner());
			}
		}
		else if (AMainCharacter* CharacterToDrop = Cast<AMainCharacter>(CarriedActor))
		{
			FVector DropLoc = GetOwner()->GetActorLocation() + GetOwner()->GetActorForwardVector() * 80.0f;
			CharacterToDrop->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			CharacterToDrop->SetActorLocation(DropLoc);
			
			// 충돌 다시 켜고 이동 모드 복구
			if (UCapsuleComponent* Capsule = CharacterToDrop->GetCapsuleComponent())
			{
				Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
			if (UCharacterMovementComponent* MoveComp = CharacterToDrop->GetCharacterMovement())
			{
				MoveComp->SetMovementMode(MOVE_Falling);
			}
		}
		
		// 내려놓을 때 표정 복구
		if (ACharacterBase* Char = Cast<ACharacterBase>(GetOwner()))
		{
			if (Char->GetVisualComponent())
			{
				Char->GetVisualComponent()->ClearTemporaryOverride();
				Char->GetVisualComponent()->RefreshVisualState();
			}
		}

		if (ACharacterBase* Char = Cast<ACharacterBase>(GetOwner()))
		{
			if (Char->GetAbilitySystemComponent() && ActiveCarryAbilitySpecHandle.IsValid())
			{
				Char->GetAbilitySystemComponent()->CancelAbilityHandle(ActiveCarryAbilitySpecHandle);
				Char->GetAbilitySystemComponent()->ClearAbility(ActiveCarryAbilitySpecHandle);
				ActiveCarryAbilitySpecHandle = FGameplayAbilitySpecHandle();
			}
		}

		CarriedActor = nullptr;
		UpdateCharacterTotalWeight();
		OnCarriedStateChanged.Broadcast(nullptr);
		return;
	}

	if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		FVector OwnerLocation = OwnerCharacter->GetActorLocation();
		
		TArray<AActor*> OverlappedActors;
		TArray<AActor*> ActorsToIgnore;
		ActorsToIgnore.Add(GetOwner());

		UKismetSystemLibrary::SphereOverlapActors(
			this,
			OwnerLocation,
			180.0f,
			{ UEngineTypes::ConvertToObjectType(ECC_WorldDynamic), UEngineTypes::ConvertToObjectType(ECC_PhysicsBody), UEngineTypes::ConvertToObjectType(ECC_Pawn) },
			AActor::StaticClass(),
			ActorsToIgnore,
			OverlappedActors);

		// 가장 가까운 아이템 또는 죽은 플레이어 선택
		AActor* ClosestActor = nullptr;
		float MinDistanceSq = MAX_FLT;

		for (AActor* Actor : OverlappedActors)
		{
			if (AItemBase* Item = Cast<AItemBase>(Actor))
			{
				// 다른 사람이 이미 들고 있어 집을 수 없는 아이템은 대상에서 제외 (가로채기 방지 및 헛손질 방지)
				if (!Item->CanBePickedUpBy(GetOwner()))
				{
					continue;
				}

				// 밀기 전용 오브젝트 제외
				if (AEventObjectBase* EventObj = Cast<AEventObjectBase>(Item))
				{
					if (EventObj->bIsPushable)
					{
						continue;
					}
				}

				float DistSq = FVector::DistSquared(OwnerLocation, Actor->GetActorLocation());
				if (DistSq < MinDistanceSq)
				{
					MinDistanceSq = DistSq;
					ClosestActor = Actor;
				}
			}
			else if (AMainCharacter* HitChar = Cast<AMainCharacter>(Actor))
			{
				if (HitChar->bIsDead) // 완전히 죽은 상태일 때만 잡을 수 있음 (그로기 제외)
				{
					float DistSq = FVector::DistSquared(OwnerLocation, Actor->GetActorLocation());
					if (DistSq < MinDistanceSq)
					{
						MinDistanceSq = DistSq;
						ClosestActor = HitChar;
					}
				}
			}
		}

		if (ClosestActor)
		{
			AItemBase* HitItem = Cast<AItemBase>(ClosestActor);
			AMainCharacter* HitCharacter = Cast<AMainCharacter>(ClosestActor);

			if (HitItem)
			{
				if (!HitItem->CanBePickedUpBy(GetOwner()))
				{
					return;
				}

				// [요구사항] 밀기 전용 이벤트 오브젝트는 잡거나 던질 수 없음
				if (AEventObjectBase* EventObj = Cast<AEventObjectBase>(HitItem))
				{
					if (EventObj->bIsPushable)
					{
						UE_LOG(LogTemp, Warning, TEXT("밀기 전용 오브젝트는 잡을 수 없습니다."));
						return;
					}
				}

				bool bShouldAttach = true;

				if (APackageBase* Package = Cast<APackageBase>(HitItem))
				{
					if (Package->PackageType == EPackageType::Heavy)
					{
						bShouldAttach = false;
					}

					Package->AddCarrier(GetOwner());
				}

				// 아이템 PickUp 처리 (물리 끄기 등 내부 로직 실행)
				// 중요: 물리 시뮬레이션을 끄는 처리가 반드시 AttachToComponent 이전에 수행되어야 합니다.
				// 그렇지 않으면 부착(Attach) 직후 물리엔진 충돌로 인해 아이템이 저 멀리 튕겨나가 투명해지는 버그가 발생합니다.
				// 또한, 택배(Heavy)의 경우 부착(Attach)하지 않더라도 물리는 꺼야 정상적으로 2인 운반이 가능합니다.
				HitItem->PickUp(GetOwner());

				if (bShouldAttach)
				{
					if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
					{
						HitItem->AttachToComponent(Character->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, CarrySocketName);

						if (APackageBase* Package = Cast<APackageBase>(HitItem))
						{
							HitItem->SetActorRelativeRotation(Package->SingleCarryRotationOffset);
						}

						FVector BoxCenter = HitItem->GetComponentsBoundingBox().GetCenter();
						FVector Origin = HitItem->GetActorLocation();
						if (!Origin.Equals(BoxCenter, 1.0f))
						{
							FVector Offset = Origin - BoxCenter;
							Offset = HitItem->GetActorTransform().InverseTransformVectorNoScale(Offset);
							HitItem->SetActorRelativeLocation(Offset);
						}
					}
				}

				CarriedActor = HitItem;
			}
			else if (HitCharacter)
			{
				// 죽은 캐릭터를 잡는 로직
				// 캡슐 콜리전 끄기 및 이동 막기
				if (UCapsuleComponent* Capsule = HitCharacter->GetCapsuleComponent())
				{
					Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				}
				if (UCharacterMovementComponent* MoveComp = HitCharacter->GetCharacterMovement())
				{
					MoveComp->SetMovementMode(MOVE_None);
				}

				if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
				{
					HitCharacter->AttachToComponent(Character->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, FName("CharacterSocket"));
					
					// 척추(Spine) 뼈가 소켓 위치에 정확히 오도록 오프셋 계산
					FVector SpineOffset = FVector::ZeroVector;
					if (USkeletalMeshComponent* HitMesh = HitCharacter->GetMesh())
					{
						// Actor 공간 기준(RTS_Actor)으로 척추뼈의 로컬 위치를 가져옵니다.
						// 대부분의 마네킹에서 spine_02 뼈가 등/가슴 중앙에 위치합니다.
						SpineOffset = HitMesh->GetSocketTransform(FName("spine_02"), RTS_Actor).GetLocation();
					}
					
					// 척추 위치만큼 액터를 반대로 이동시키면, 척추가 소켓 기준점(ZeroVector)에 정확히 맞물리게 됩니다.
					HitCharacter->SetActorRelativeLocation(-SpineOffset);
					HitCharacter->SetActorRelativeRotation(FRotator::ZeroRotator); 
				}

				CarriedActor = HitCharacter;
			}
			

			UpdateCharacterTotalWeight();

			// 표정 변화 적용
			if (ACharacterBase* Char = Cast<ACharacterBase>(GetOwner()))
			{
				if (Char->GetVisualComponent())
				{
					Char->GetVisualComponent()->RefreshVisualState();
				}
			}

			if (ACharacterBase* Char = Cast<ACharacterBase>(GetOwner()))
			{
				if (UAbilitySystemComponent* ASC = Char->GetAbilitySystemComponent())
				{
					if (ActiveCarryAbilitySpecHandle.IsValid())
					{
						ASC->CancelAbilityHandle(ActiveCarryAbilitySpecHandle);
						ASC->ClearAbility(ActiveCarryAbilitySpecHandle);
						ActiveCarryAbilitySpecHandle = FGameplayAbilitySpecHandle();
					}

					AMainCharacter* MainCharOwner = Cast<AMainCharacter>(Char);
					TSubclassOf<UGameplayAbility> AbilityClassToGive = (MainCharOwner && MainCharOwner->CarryItemAbilityClass) ? MainCharOwner->CarryItemAbilityClass : TSubclassOf<UGameplayAbility>(UGA_CarryItem::StaticClass());
					
					if (HitCharacter)
					{
						AbilityClassToGive = (MainCharOwner && MainCharOwner->CarryCharacterAbilityClass) ? MainCharOwner->CarryCharacterAbilityClass : TSubclassOf<UGameplayAbility>(UGA_CarryCharacter::StaticClass());
					}
					else if (APackageBase* Package = Cast<APackageBase>(HitItem))
					{
						if (Package->PackageType == EPackageType::Heavy)
						{
							AbilityClassToGive = (MainCharOwner && MainCharOwner->HeavyCarryAbilityClass) ? MainCharOwner->HeavyCarryAbilityClass : TSubclassOf<UGameplayAbility>(UGA_HeavyCarry::StaticClass());
						}
					}

					ActiveCarryAbilitySpecHandle = ASC->GiveAbility(FGameplayAbilitySpec(AbilityClassToGive, 1));
					ASC->TryActivateAbility(ActiveCarryAbilitySpecHandle);
				}
			}

			OnCarriedStateChanged.Broadcast(CarriedActor);
		}
	}
}

void UCarryingComponent::ServerGrabOrDrop_Implementation()
{
	GrabOrDrop();
}

void UCarryingComponent::Throw()
{
	if (!GetOwner()->HasAuthority())
	{
		ServerThrow();
		return;
	}

	if (IsCarrying())
	{
		if (AItemBase* Item = Cast<AItemBase>(CarriedActor))
		{
			// [개선] 무거운 택배는 혼자 들든 둘이 들든 던질 수 없습니다.
			if (APackageBase* Package = Cast<APackageBase>(Item))
			{
				if (Package->PackageType == EPackageType::Heavy)
				{
					UE_LOG(LogTemp, Warning, TEXT("무거운 택배는 던질 수 없습니다!"));
					return;
				}
				
				// 가벼운 택배인 경우 운반자 리스트에서 본인 제거
				Package->RemoveCarrier(GetOwner());
			}
			
			// 캐릭터의 전방과 위쪽(사선) 방향으로 힘(임펄스) 계산
			FVector ThrowVel = GetOwner()->GetActorForwardVector() * DefaultThrowForce + FVector(0, 0, DefaultThrowForce * 0.4f);
			Item->Throw(ThrowVel, GetOwner());
		}
		else if (AMainCharacter* CharacterToThrow = Cast<AMainCharacter>(CarriedActor))
		{
			// 사람을 던지는 로직
			FVector ThrowVel = GetOwner()->GetActorForwardVector() * DefaultThrowForce + FVector(0, 0, DefaultThrowForce * 0.4f);
			
			CharacterToThrow->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			
			if (UCapsuleComponent* Capsule = CharacterToThrow->GetCapsuleComponent())
			{
				Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
			if (UCharacterMovementComponent* MoveComp = CharacterToThrow->GetCharacterMovement())
			{
				MoveComp->SetMovementMode(MOVE_Falling);
				// 캐릭터 런치
				CharacterToThrow->LaunchCharacter(ThrowVel, true, true);
			}
		}

		// 던질 때 표정 복구
		if (ACharacterBase* Char = Cast<ACharacterBase>(GetOwner()))
		{
			if (Char->GetVisualComponent())
			{
				Char->GetVisualComponent()->ClearTemporaryOverride();
				Char->GetVisualComponent()->RefreshVisualState();
			}
		}

		if (ACharacterBase* Char = Cast<ACharacterBase>(GetOwner()))
		{
			if (Char->GetAbilitySystemComponent() && ActiveCarryAbilitySpecHandle.IsValid())
			{
				Char->GetAbilitySystemComponent()->CancelAbilityHandle(ActiveCarryAbilitySpecHandle);
				Char->GetAbilitySystemComponent()->ClearAbility(ActiveCarryAbilitySpecHandle);
				ActiveCarryAbilitySpecHandle = FGameplayAbilitySpecHandle();
			}
		}
	}
	
	CarriedActor = nullptr;
	UpdateCharacterTotalWeight();
	OnCarriedStateChanged.Broadcast(nullptr);
	UE_LOG(LogTemp, Log, TEXT("물건을 던졌습니다."));
}

void UCarryingComponent::ServerThrow_Implementation()
{
	Throw();
}

void UCarryingComponent::EquipItem(AActor* ItemToEquip)
{
	if (!ItemToEquip) return;
	
	if (GetOwner()->HasAuthority())
	{
		MulticastEquipItem(ItemToEquip);

		if (ACharacterBase* Char = Cast<ACharacterBase>(GetOwner()))
		{
			if (UAbilitySystemComponent* ASC = Char->GetAbilitySystemComponent())
			{
				if (ActiveCarryAbilitySpecHandle.IsValid())
				{
					ASC->CancelAbilityHandle(ActiveCarryAbilitySpecHandle);
					ASC->ClearAbility(ActiveCarryAbilitySpecHandle);
					ActiveCarryAbilitySpecHandle = FGameplayAbilitySpecHandle();
				}

				AMainCharacter* MainCharOwner = Cast<AMainCharacter>(Char);
				TSubclassOf<UGameplayAbility> AbilityClassToGive = (MainCharOwner && MainCharOwner->CarryItemAbilityClass) ? MainCharOwner->CarryItemAbilityClass : TSubclassOf<UGameplayAbility>(UGA_CarryItem::StaticClass());

				if (APackageBase* Package = Cast<APackageBase>(ItemToEquip))
				{
					if (Package->PackageType == EPackageType::Heavy)
					{
						AbilityClassToGive = (MainCharOwner && MainCharOwner->HeavyCarryAbilityClass) ? MainCharOwner->HeavyCarryAbilityClass : TSubclassOf<UGameplayAbility>(UGA_HeavyCarry::StaticClass());
					}
				}

				ActiveCarryAbilitySpecHandle = ASC->GiveAbility(FGameplayAbilitySpec(AbilityClassToGive, 1));
				ASC->TryActivateAbility(ActiveCarryAbilitySpecHandle);
			}
		}
	}
}

void UCarryingComponent::MulticastEquipItem_Implementation(AActor* ItemToEquip)
{
	if (!ItemToEquip) return;

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		ItemToEquip->AttachToComponent(Character->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, CarrySocketName);

		// 중심점 오프셋 적용
		FVector BoxCenter = ItemToEquip->GetComponentsBoundingBox().GetCenter();
		FVector Origin = ItemToEquip->GetActorLocation();
		if (!Origin.Equals(BoxCenter, 1.0f))
		{
			FVector Offset = Origin - BoxCenter;
			Offset = ItemToEquip->GetActorTransform().InverseTransformVectorNoScale(Offset);
			ItemToEquip->SetActorRelativeLocation(Offset);
		}
	}

	CarriedActor = ItemToEquip;

	if (GetOwner()->HasAuthority())
	{
		UpdateCharacterTotalWeight();
	}

	OnCarriedStateChanged.Broadcast(CarriedActor);
}

void UCarryingComponent::ClearCarriedItem()
{
	if (GetOwner()->HasAuthority())
	{
		MulticastClearCarriedItem();

		if (ACharacterBase* Char = Cast<ACharacterBase>(GetOwner()))
		{
			if (UAbilitySystemComponent* ASC = Char->GetAbilitySystemComponent())
			{
				if (ActiveCarryAbilitySpecHandle.IsValid())
				{
					ASC->CancelAbilityHandle(ActiveCarryAbilitySpecHandle);
					ASC->ClearAbility(ActiveCarryAbilitySpecHandle);
					ActiveCarryAbilitySpecHandle = FGameplayAbilitySpecHandle();
				}
			}
		}
	}
}

void UCarryingComponent::MulticastClearCarriedItem_Implementation()
{
	CarriedActor = nullptr;

	if (GetOwner()->HasAuthority())
	{
		UpdateCharacterTotalWeight();
	}

	OnCarriedStateChanged.Broadcast(nullptr);
}

void UCarryingComponent::UpdateCharacterTotalWeight()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	ACharacterBase* Char = Cast<ACharacterBase>(GetOwner());
	if (!Char || !Char->BaseAttribute)
	{
		return;
	}

	float TotalWeight = 0.0f;

	// 1. 현재 손에 든 물건 무게
	if (AItemBase* HandItem = Cast<AItemBase>(CarriedActor))
	{
		if (APackageBase* Package = Cast<APackageBase>(HandItem))
		{
			if (Package->AppliedWeightMap.Contains(Char))
			{
				TotalWeight += Package->AppliedWeightMap[Char];
			}
			else
			{
				TotalWeight += Package->ItemWeight;
			}
		}
		else
		{
			TotalWeight += HandItem->ItemWeight;
		}
	}
	else if (AMainCharacter* CarriedChar = Cast<AMainCharacter>(CarriedActor))
	{
		TotalWeight += 50.0f; // 시체 운반 기본 무게
	}

	// 2. 인벤토리 슬롯에 보관 중인 모든 아이템 무게 합산 (손에 든 아이템과 중복 합산 방지)
	if (UInventoryComponent* InvComp = Char->FindComponentByClass<UInventoryComponent>())
	{
		for (AItemBase* SlotItem : InvComp->InventorySlots)
		{
			if (SlotItem && SlotItem != CarriedActor)
			{
				TotalWeight += SlotItem->ItemWeight;
			}
		}
	}

	Char->BaseAttribute->SetCurrentWeight(TotalWeight);
}
