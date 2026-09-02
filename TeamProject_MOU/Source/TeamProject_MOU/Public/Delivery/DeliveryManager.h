#pragma once

#include "CoreMinimal.h"
#include "Game/LevelSettlementState.h"
#include "GameFramework/Info.h"
#include "DeliveryManager.generated.h"

class APackageBase;

USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FDeliveryProgress
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	int32 TotalItemCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	int32 DeliveredItemCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	int32 BrokenItemCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	int32 EarnedGold = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	TArray<FSettlementItemEntry> DeliveredItems;

	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	TArray<FPlayerSettlementData> PlayerResults;

	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	int32 Revision = 0;

	int32 GetProcessedItemCount() const { return DeliveredItemCount + BrokenItemCount; }
	bool IsComplete() const { return TotalItemCount > 0 && GetProcessedItemCount() >= TotalItemCount; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnDeliveryProgressChanged, const FDeliveryProgress&, Progress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnAllDeliveryItemsProcessed, const FDeliveryProgress&, FinalProgress);

/** 배달 물품의 정상/파손 납품과 금액만 집계합니다. 레벨 완료는 처리하지 않습니다. */
UCLASS(BlueprintType)
class TEAMPROJECT_MOU_API ADeliveryManager : public AInfo
{
	GENERATED_BODY()

public:
	ADeliveryManager();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Delivery|Events")
	FOnDeliveryProgressChanged OnDeliveryProgressChanged;

	UPROPERTY(BlueprintAssignable, Category = "Delivery|Events")
	FOnAllDeliveryItemsProcessed OnAllDeliveryItemsProcessed;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Progress, Category = "Delivery")
	FDeliveryProgress Progress;

	// 정상 납품이면 true를 반환합니다. 파손 물품도 처리 수량에는 포함됩니다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Delivery")
	bool TryDeliverPackage(APackageBase* Package);

	// LoadoutSpawner가 물품 하나를 스폰할 때마다 서버에서 호출합니다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Delivery")
	bool RegisterDeliveryPackage(APackageBase* Package);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Delivery")
	bool RegisterDeliveryZone(FName ZoneID);

	UFUNCTION(BlueprintPure, Category = "Delivery")
	FName GetAssignedZoneID(const APackageBase* Package) const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	bool IsPackageAssignedToZone(const APackageBase* Package, FName ZoneID) const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	FDeliveryProgress GetProgress() const { return Progress; }

	UFUNCTION(BlueprintPure, Category = "Delivery|Debug")
	int32 GetRegisteredZoneCount() const { return RegisteredZoneIDs.Num(); }

	UFUNCTION(BlueprintPure, Category = "Delivery|Debug")
	int32 GetWaitingPackageCount() const { return PackagesWaitingForZone.Num(); }

	UFUNCTION(BlueprintPure, Category = "Delivery|Debug")
	bool IsPackageWaitingForZone(const APackageBase* Package) const;

	UFUNCTION(BlueprintPure, Category = "Delivery", meta = (WorldContext = "WorldContextObject"))
	static ADeliveryManager* GetDeliveryManager(const UObject* WorldContextObject);

private:
	TMap<UClass*, int32> RemainingRequiredCounts;
	TMap<UClass*, int32> RegisteredCounts;
	TSet<TWeakObjectPtr<APackageBase>> ProcessedPackages;
	TMap<TWeakObjectPtr<APackageBase>, FName> AssignedZones;
	TArray<FName> RegisteredZoneIDs;
	TArray<TWeakObjectPtr<APackageBase>> PackagesWaitingForZone;
	bool bCompletionBroadcast = false;
	bool bPendingInitialized = false;

	void InitializeFromPendingDelivery();
	void DiscoverPlacedZones();
	void AssignWaitingPackages();
	void ClearPackageFromPlayerHands(APackageBase* Package);
	void RecordPlayerDelivery(APackageBase* Package, int32 DeliveryValue);
	bool AssignRandomZone(APackageBase* Package);
	void BroadcastProgress();

	UFUNCTION()
	void OnRep_Progress();
};
