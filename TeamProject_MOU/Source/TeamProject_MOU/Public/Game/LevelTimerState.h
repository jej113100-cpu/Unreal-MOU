#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "LevelTimerState.generated.h"

USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FLevelTimerSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Level Timer")
	float DurationSeconds = 0.0f;

	// GameState의 동기화된 ServerWorldTime 기준 종료 시각입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Level Timer")
	float ServerEndTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Level Timer")
	bool bActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Level Timer")
	bool bExpired = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnLevelTimerStateChanged, const FLevelTimerSnapshot&, Snapshot);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLevelTimerExpired);

UCLASS(BlueprintType)
class TEAMPROJECT_MOU_API ALevelTimerState : public AInfo
{
	GENERATED_BODY()

public:
	ALevelTimerState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Level Timer|Events")
	FOnLevelTimerStateChanged OnLevelTimerStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Level Timer|Events")
	FOnLevelTimerExpired OnLevelTimerExpired;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Snapshot, Category = "Level Timer")
	FLevelTimerSnapshot Snapshot;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Level Timer")
	void StartTimer(float DurationSeconds);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Level Timer")
	void ExpireTimer();

	// 정상 클리어/정산 시 만료 이벤트 없이 타이머를 종료합니다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Level Timer")
	void StopTimer();

	UFUNCTION(BlueprintPure, Category = "Level Timer")
	float GetRemainingSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Level Timer")
	float GetProgress() const;

	UFUNCTION(BlueprintPure, Category = "Level Timer", meta = (WorldContext = "WorldContextObject"))
	static ALevelTimerState* GetLevelTimerState(const UObject* WorldContextObject);

private:
	UFUNCTION()
	void OnRep_Snapshot();

	float GetSynchronizedServerTime() const;
};
