#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPCSpawner.generated.h"

class ACharacterBase;
class USceneComponent;

/** 스포너에서 선택할 NPC 종류와 해당 NPC가 사용할 정찰 액터 설정입니다. */
USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FNPCSpawnDefinition
{
	GENERATED_BODY()

	/** 생성할 NPC 클래스입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC Spawner")
	TSubclassOf<ACharacterBase> NPCClass;

	/** 스플라인 정찰 NPC가 사용할 스플라인 액터입니다. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "NPC Spawner|Patrol")
	TObjectPtr<AActor> PatrolSplineActor = nullptr;

	/** 영역 정찰 NPC가 사용할 Bound 액터입니다. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "NPC Spawner|Patrol")
	TObjectPtr<AActor> PatrolBoundActor = nullptr;
};

/**
 * 월드에 배치한 Spawn Point에서 NPC를 생성하는 서버 권위 스포너입니다.
 * SpawnPointCount를 변경하면 Construction 단계에서 위치 조절용 SceneComponent가 생성됩니다.
 */
UCLASS(Blueprintable)
class TEAMPROJECT_MOU_API ANPCSpawner : public AActor
{
	GENERATED_BODY()

public:
	ANPCSpawner();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 현재 비어 있는 Spawn Point 한 곳에 NPC 하나를 생성합니다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "NPC Spawner")
	ACharacterBase* SpawnOneNPC();

	/** 최대 소환 수까지 비어 있는 Spawn Point에 NPC를 생성합니다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "NPC Spawner")
	void SpawnNPCsToLimit();

protected:
	/** 소환할 NPC 종류와 각 종류가 사용할 스플라인/영역 정찰 액터 목록입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC Spawner")
	TArray<FNPCSpawnDefinition> NPCSpawnDefinitions;

	/** 동시에 존재할 수 있는 최대 NPC 수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC Spawner", meta = (ClampMin = "0", UIMin = "0"))
	int32 MaxSpawnCount = 3;

	/** 현재 이 스포너가 생성하여 살아 있는 NPC 수입니다. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "NPC Spawner")
	int32 CurrentSpawnCount = 0;

	/** Construction에서 생성할 Spawn Point의 개수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC Spawner", meta = (ClampMin = "0", UIMin = "0"))
	int32 SpawnPointCount = 3;

	/** 게임 시작 시 자동으로 NPC를 생성할지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC Spawner")
	bool bSpawnOnBeginPlay = true;

	/** 소환된 NPC가 제거되었을 때 최대 수량까지 자동으로 다시 채울지 결정합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC Spawner|Respawn")
	bool bAutoRespawn = true;

	/** NPC가 제거된 뒤 부족한 수량을 다시 소환하기까지 기다리는 시간입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC Spawner|Respawn", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float RespawnDelay = 5.0f;

	/** Construction에서 생성된 위치 조절용 Spawn Point 목록입니다. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Instanced, Category = "NPC Spawner")
	TArray<TObjectPtr<USceneComponent>> SpawnPoints;

	/** 현재 이 스포너가 생성하여 살아 있는 NPC 목록입니다. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "NPC Spawner")
	TArray<TObjectPtr<ACharacterBase>> SpawnedNPCs;

private:
	UPROPERTY(VisibleAnywhere, Category = "NPC Spawner")
	TObjectPtr<USceneComponent> SceneRoot;

	/** 이 스포너가 생성한 NPC 목록입니다. */
	FTimerHandle RespawnTimerHandle;

	UFUNCTION()
	void HandleSpawnedNPCDestroyed(AActor* DestroyedActor);

	void RebuildSpawnPoints();
	void RemoveInvalidSpawnedNPCs();
	void ScheduleRespawn();
	void RespawnMissingNPCs();
	bool IsSpawnPointOccupied(const USceneComponent* SpawnPoint) const;
	void AssignPatrolActors(ACharacterBase* SpawnedNPC, const FNPCSpawnDefinition& SpawnDefinition) const;
};
