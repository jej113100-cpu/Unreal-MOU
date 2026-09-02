#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "GameCycleState.generated.h"

USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FGameCycleContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Game Cycle")
	int32 EconomyTime = 0;

	// 완료된 누적 일수입니다. EconomyTime 2가 Day 1입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Game Cycle")
	int32 CompletedDay = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Game Cycle")
	int32 DebtCycle = 1;

	// 현재 상환 회차에서 완료한 일수입니다. 0~7 범위입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Game Cycle")
	int32 CompletedDayInCycle = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Game Cycle")
	int32 RemainingDays = 7;

	UPROPERTY(BlueprintReadOnly, Category = "Game Cycle")
	bool bDebtDeadline = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnGameCycleChanged, const FGameCycleContext&, Context);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnGameDayPassed, const FGameCycleContext&, Context);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnGameDebtDeadlineReached, const FGameCycleContext&, Context);

/**
 * 경제 시간으로부터 일일/7일 주기만 계산하고 알리는 독립 복제 액터입니다.
 * 구독자가 수행할 실제 행동(이자, 퀘스트, UI 등)은 알지 않습니다.
 */
UCLASS(BlueprintType)
class TEAMPROJECT_MOU_API AGameCycleState : public AInfo
{
	GENERATED_BODY()

public:
	AGameCycleState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Game Cycle|Events")
	FOnGameCycleChanged OnEconomyTimeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Game Cycle|Events")
	FOnGameDayPassed OnDayPassed;

	UPROPERTY(BlueprintAssignable, Category = "Game Cycle|Events")
	FOnGameDebtDeadlineReached OnDebtDeadlineReached;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_EconomyTime, Category = "Game Cycle")
	int32 EconomyTime = 0;

	// 서버에서 Economy 시간이 변경될 때 호출합니다. 시간이 건너뛰면 누락된 서버 콜백도 순서대로 발생합니다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Game Cycle")
	void NotifyEconomyTimeAdvanced(int32 NewEconomyTime);

	// 레벨 진입 시 저장된 시간을 복구합니다. 과거 콜백은 재발행하지 않습니다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Game Cycle")
	void InitializeEconomyTime(int32 InitialEconomyTime);

	UFUNCTION(BlueprintPure, Category = "Game Cycle")
	FGameCycleContext GetCurrentCycleContext() const;

	// 클라이언트 UI가 GameMode 참조 없이 복제 액터를 찾을 때 사용합니다.
	UFUNCTION(BlueprintPure, Category = "Game Cycle", meta = (WorldContext = "WorldContextObject"))
	static AGameCycleState* GetGameCycleState(const UObject* WorldContextObject);

private:
	UPROPERTY(ReplicatedUsing = OnRep_LastCompletedDay)
	int32 LastCompletedDay = 0;

	UPROPERTY(ReplicatedUsing = OnRep_DebtDeadlineCount)
	int32 DebtDeadlineCount = 0;

	FGameCycleContext BuildContext(int32 AtEconomyTime) const;

	UFUNCTION()
	void OnRep_EconomyTime();

	UFUNCTION()
	void OnRep_LastCompletedDay();

	UFUNCTION()
	void OnRep_DebtDeadlineCount();
};
