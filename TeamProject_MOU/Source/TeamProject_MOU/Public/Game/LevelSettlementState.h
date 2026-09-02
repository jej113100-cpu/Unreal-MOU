#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "LevelSettlementState.generated.h"

class AItemBase;
class UTexture2D;

UENUM(BlueprintType)
enum class ELevelSettlementReason : uint8
{
	None,
	Cleared,
	TimeExpired,
	AllPlayersDead,
	Aborted
};

/** 정산 UI의 아이템 목록 한 줄입니다. 같은 클래스의 아이템은 수량과 금액으로 합산합니다. */
USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FSettlementItemEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement|Item")
	TSubclassOf<AItemBase> ItemClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement|Item")
	FText ItemName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement|Item")
	TObjectPtr<UTexture2D> ItemIcon = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement|Item")
	int32 Quantity = 0;

	// 배달 품목은 실제 획득 금액, 약탈 품목은 0입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement|Item")
	int32 EarnedGold = 0;
};

/** 플레이어 한 명의 배달 정산 결과입니다. */
USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FPlayerSettlementData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement|Player")
	int32 PlayerId = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement|Player")
	FString PlayerName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement|Player")
	int32 DeliveredItemCount = 0;

	// 팀 골드와 별개로 이 플레이어가 배달에 기여한 금액입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement|Player")
	int32 EarnedGold = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement|Player")
	int32 KnockdownCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement|Player")
	TArray<FSettlementItemEntry> DeliveredItems;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement|Player")
	int32 LootedItemCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement|Player")
	TArray<FSettlementItemEntry> LootedItems;
};

USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FLevelSettlementData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	ELevelSettlementReason Reason = ELevelSettlementReason::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	bool bSucceeded = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	int32 EarnedGold = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	int32 DeliveredItemCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	int32 FailedDeliveryCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	int32 DeathCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	int32 KnockdownCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	TArray<FSettlementItemEntry> DeliveredItems;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	TArray<FPlayerSettlementData> PlayerResults;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	int32 LootedItemCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	TArray<FSettlementItemEntry> LootedItems;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	float LevelPlayTimeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	float FinalThreatLevel = 0.0f;
};

USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FLevelSettlementSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Settlement")
	FLevelSettlementData Result;

	UPROPERTY(BlueprintReadOnly, Category = "Settlement")
	bool bFinalized = false;

	// 같은 값으로 다시 정산되더라도 복제 변경을 보장합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Settlement")
	int32 Revision = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnLevelSettlementFinalized, const FLevelSettlementData&, Result);

/** 서버에서 한 번 확정한 레벨 정산 결과를 모든 클라이언트에 알립니다. */
UCLASS(BlueprintType)
class TEAMPROJECT_MOU_API ALevelSettlementState : public AInfo
{
	GENERATED_BODY()

public:
	ALevelSettlementState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Settlement|Events")
	FOnLevelSettlementFinalized OnSettlementFinalized;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Snapshot, Category = "Settlement")
	FLevelSettlementSnapshot Snapshot;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Settlement")
	bool FinalizeSettlement(const FLevelSettlementData& Result);

	UFUNCTION(BlueprintPure, Category = "Settlement")
	bool IsFinalized() const { return Snapshot.bFinalized; }

	UFUNCTION(BlueprintPure, Category = "Settlement")
	FLevelSettlementData GetResult() const { return Snapshot.Result; }

	UFUNCTION(BlueprintPure, Category = "Settlement", meta = (WorldContext = "WorldContextObject"))
	static ALevelSettlementState* GetLevelSettlementState(const UObject* WorldContextObject);

private:
	UFUNCTION()
	void OnRep_Snapshot();
};
