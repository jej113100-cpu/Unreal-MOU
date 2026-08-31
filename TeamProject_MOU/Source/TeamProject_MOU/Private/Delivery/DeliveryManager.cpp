#include "Delivery/DeliveryManager.h"

#include "Base/PackageBase.h"
#include "Base/ProjectGameInstanceBase.h"
#include "Base/ProjectGameStateBase.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CarryingComponent.h"
#include "Delivery/DeliveryZone.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Item/DeliveryData.h"
#include "Net/UnrealNetwork.h"
#include "Player/MainCharacter.h"

ADeliveryManager::ADeliveryManager()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
}

void ADeliveryManager::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		InitializeFromPendingDelivery();
	}
}

bool ADeliveryManager::RegisterDeliveryPackage(APackageBase* Package)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Delivery] RegisterPackage failed: not authority. Package=%s"), *GetNameSafe(Package));
		return false;
	}
	if (!IsValid(Package))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Delivery] RegisterPackage failed: invalid package"));
		return false;
	}
	// World BeginPlay 중 액터 실행 순서는 보장되지 않으므로 스포너가 먼저 호출해도 준비되게 합니다.
	InitializeFromPendingDelivery();
	if (RegisteredZoneIDs.IsEmpty())
	{
		DiscoverPlacedZones();
	}
	// 자동 탐색과 스포너의 직접 등록이 같은 프레임에 호출될 수 있으므로 중복 등록은 성공으로 취급합니다.
	if (AssignedZones.Contains(Package))
	{
		UE_LOG(LogTemp, Log, TEXT("[Delivery] Package already assigned. Package=%s Zone=%s"),
			*GetNameSafe(Package), *GetAssignedZoneID(Package).ToString());
		return true;
	}
	if (PackagesWaitingForZone.Contains(Package))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Delivery] Package is registered but waiting: no zone available. Package=%s"),
			*GetNameSafe(Package));
		return true;
	}

	UClass* PackageClass = Package->GetClass();
	const int32* RequiredCount = RemainingRequiredCounts.Find(PackageClass);
	if (!RequiredCount || *RequiredCount <= 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Delivery] RegisterPackage failed: class is not in PendingDeliveryData. Package=%s Class=%s Total=%d"),
			*GetNameSafe(Package), *GetNameSafe(PackageClass), Progress.TotalItemCount);
		return false;
	}
	if (RegisteredCounts.FindOrAdd(PackageClass) >= *RequiredCount)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Delivery] RegisterPackage failed: required count already registered. Package=%s Class=%s Registered=%d Required=%d"),
			*GetNameSafe(Package), *GetNameSafe(PackageClass), RegisteredCounts.FindRef(PackageClass), *RequiredCount);
		return false;
	}

	++RegisteredCounts.FindOrAdd(PackageClass);
	if (Package->MeshComponent)
	{
		// 기존 운반 시스템이 PhysicsBody 오브젝트 타입을 검색하므로 PhysicsActor 프로필을 유지합니다.
		// 배달 대상 여부는 충돌 채널이 아니라 AssignedZones 등록 정보로 판별합니다.
		Package->MeshComponent->SetGenerateOverlapEvents(true);
	}
	if (!AssignRandomZone(Package))
	{
		PackagesWaitingForZone.Add(Package);
		UE_LOG(LogTemp, Warning, TEXT("[Delivery] Package registered, waiting for zone. Package=%s RegisteredZones=%d"),
			*GetNameSafe(Package), RegisteredZoneIDs.Num());
	}
	return true;
}

bool ADeliveryManager::RegisterDeliveryZone(FName ZoneID)
{
	if (!HasAuthority() || ZoneID.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Delivery] RegisterZone failed. Authority=%d Zone=%s"),
			HasAuthority(), *ZoneID.ToString());
		return false;
	}
	if (RegisteredZoneIDs.Contains(ZoneID)) return true;

	RegisteredZoneIDs.Add(ZoneID);
	UE_LOG(LogTemp, Log, TEXT("[Delivery] Zone registered. Zone=%s TotalZones=%d WaitingPackages=%d"),
		*ZoneID.ToString(), RegisteredZoneIDs.Num(), PackagesWaitingForZone.Num());
	AssignWaitingPackages();
	return true;
}

void ADeliveryManager::DiscoverPlacedZones()
{
	if (!HasAuthority()) return;

	for (TActorIterator<ADeliveryZone> It(GetWorld()); It; ++It)
	{
		RegisterDeliveryZone(It->GetEffectiveZoneID());
	}

	UE_LOG(LogTemp, Log, TEXT("[Delivery] Placed zone discovery finished. TotalZones=%d"),
		RegisteredZoneIDs.Num());
}

FName ADeliveryManager::GetAssignedZoneID(const APackageBase* Package) const
{
	if (!Package) return NAME_None;
	if (const FName* ZoneID = AssignedZones.Find(Package))
	{
		return *ZoneID;
	}
	return NAME_None;
}

bool ADeliveryManager::IsPackageAssignedToZone(const APackageBase* Package, FName ZoneID) const
{
	return !ZoneID.IsNone() && GetAssignedZoneID(Package) == ZoneID;
}

bool ADeliveryManager::IsPackageWaitingForZone(const APackageBase* Package) const
{
	return Package && PackagesWaitingForZone.Contains(Package);
}

bool ADeliveryManager::AssignRandomZone(APackageBase* Package)
{
	if (!IsValid(Package) || RegisteredZoneIDs.IsEmpty()) return false;

	const FName ZoneID = RegisteredZoneIDs[FMath::RandRange(0, RegisteredZoneIDs.Num() - 1)];
	AssignedZones.Add(Package, ZoneID);
	UE_LOG(LogTemp, Log, TEXT("[Delivery] Package assigned. Package=%s Zone=%s"),
		*GetNameSafe(Package), *ZoneID.ToString());
	return true;
}

void ADeliveryManager::AssignWaitingPackages()
{
	if (RegisteredZoneIDs.IsEmpty()) return;

	for (int32 Index = PackagesWaitingForZone.Num() - 1; Index >= 0; --Index)
	{
		APackageBase* Package = PackagesWaitingForZone[Index].Get();
		if (!IsValid(Package) || AssignRandomZone(Package))
		{
			PackagesWaitingForZone.RemoveAtSwap(Index);
		}
	}
}

void ADeliveryManager::InitializeFromPendingDelivery()
{
	if (bPendingInitialized) return;
	bPendingInitialized = true;

	const UProjectGameInstanceBase* GameInstance = GetGameInstance<UProjectGameInstanceBase>();
	if (!GameInstance)
	{
		bPendingInitialized = false;
		return;
	}

	const FDeliveryData& Data = GameInstance->PendingDeliveryData;
	if (!Data.SelectedItemInstances.IsEmpty())
	{
		for (const FStoredItemInstanceData& Item : Data.SelectedItemInstances)
		{
			UClass* PackageClass = Item.ItemClass.Get();
			if (PackageClass && PackageClass->IsChildOf(APackageBase::StaticClass()))
			{
				++RemainingRequiredCounts.FindOrAdd(PackageClass);
				++Progress.TotalItemCount;
			}
		}
	}
	else
	{
		for (const FStoredItemData& Item : Data.SelectedItems)
		{
			UClass* PackageClass = Item.ItemClass.Get();
			if (PackageClass && PackageClass->IsChildOf(APackageBase::StaticClass()) && Item.Quantity > 0)
			{
				RemainingRequiredCounts.FindOrAdd(PackageClass) += Item.Quantity;
				Progress.TotalItemCount += Item.Quantity;
			}
		}
	}

	BroadcastProgress();
	UE_LOG(LogTemp, Log, TEXT("[Delivery] Pending initialized. Total=%d RequiredClasses=%d"),
		Progress.TotalItemCount, RemainingRequiredCounts.Num());
}

bool ADeliveryManager::TryDeliverPackage(APackageBase* Package)
{
	if (!HasAuthority() || !IsValid(Package) || ProcessedPackages.Contains(Package)) return false;

	int32* RemainingCount = RemainingRequiredCounts.Find(Package->GetClass());
	if (!RemainingCount || *RemainingCount <= 0) return false;

	ProcessedPackages.Add(Package);
	AssignedZones.Remove(Package);
	--(*RemainingCount);
	const bool bDeliveredIntact = !Package->bIsBroken;
	if (bDeliveredIntact)
	{
		++Progress.DeliveredItemCount;
		const int32 DeliveryValue = FMath::Max(0, Package->GetCurrentValue());
		UE_LOG(LogTemp, Log,
			TEXT("[Delivery] Reward calculated. Package=%s Value=%d Base=%d Durability=%.1f/%.1f Type=%d Spoil=%.1f Broken=%d"),
			*GetNameSafe(Package), DeliveryValue, Package->BaseValue,
			Package->CurrentDurability, Package->MaxDurability,
			static_cast<int32>(Package->PackageType), Package->CurrentSpoilTime, Package->bIsBroken);
		Progress.EarnedGold += DeliveryValue;
		if (AProjectGameStateBase* GameState = GetWorld()->GetGameState<AProjectGameStateBase>())
		{
			GameState->AddGold(DeliveryValue);
		}
	}
	else
	{
		++Progress.BrokenItemCount;
	}

	ClearPackageFromPlayerHands(Package);
	Package->Destroy();
	BroadcastProgress();
	return bDeliveredIntact;
}

void ADeliveryManager::ClearPackageFromPlayerHands(APackageBase* Package)
{
	if (!HasAuthority() || !IsValid(Package)) return;

	for (TActorIterator<AMainCharacter> It(GetWorld()); It; ++It)
	{
		AMainCharacter* Character = *It;
		UCarryingComponent* CarryingComponent = Character->GetCarryingComponent();
		if (!CarryingComponent || CarryingComponent->GetCarriedActor() != Package) continue;

		CarryingComponent->ClearCarriedItem();
		Character->ChangeEmotion(0);
		UE_LOG(LogTemp, Log, TEXT("[Delivery] Cleared delivered package from player hands. Player=%s Package=%s"),
			*GetNameSafe(Character), *GetNameSafe(Package));
	}
}

void ADeliveryManager::BroadcastProgress()
{
	++Progress.Revision;
	OnDeliveryProgressChanged.Broadcast(Progress);
	if (Progress.IsComplete() && !bCompletionBroadcast)
	{
		bCompletionBroadcast = true;
		OnAllDeliveryItemsProcessed.Broadcast(Progress);
	}
	ForceNetUpdate();
}

ADeliveryManager* ADeliveryManager::GetDeliveryManager(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World) return nullptr;

	for (TActorIterator<ADeliveryManager> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ADeliveryManager::OnRep_Progress()
{
	OnDeliveryProgressChanged.Broadcast(Progress);
	if (Progress.IsComplete())
	{
		OnAllDeliveryItemsProcessed.Broadcast(Progress);
	}
}

void ADeliveryManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADeliveryManager, Progress);
}
