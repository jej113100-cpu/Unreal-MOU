#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Enum/NPCEnum.h"
#include "NPC/NPCActionStruct.h"
#include "NPCData.generated.h"

UCLASS()
class TEAMPROJECT_MOU_API UNPCData : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:

    UNPCData();

    /* NPC 시작 상태 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Behavior", meta = (ToolTip = "NPC 시작 상태"))
    ENPCStartState StartState;

    /* 정찰 사용 여부 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Behavior", meta = (ToolTip = "정찰 사용 여부"))
    bool UsePatrol;

    /* NPC 정찰 위치를 선택하는 방식 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Patrol", meta = (EditCondition = "UsePatrol", EditConditionHides, ToolTip = "근처 반경, 스플라인, 영역 중 정찰 방식을 선택합니다."))
    ENPCPatrolType PatrolType;

    /* 근처 반경 정찰에서 사용할 탐색 반경 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Patrol", meta = (EditCondition = "UsePatrol && PatrolType == ENPCPatrolType::RandomRadius", EditConditionHides, ClampMin = "0.0", Units = "cm", ToolTip = "NPC 현재 위치를 기준으로 정찰 지점을 찾을 반경입니다."))
    float PatrolRadius;

    /* 스플라인 정찰 지점 주변에서 허용할 무작위 반경 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Patrol", meta = (EditCondition = "UsePatrol && PatrolType == ENPCPatrolType::Spline", EditConditionHides, ClampMin = "0.0", Units = "cm", ToolTip = "스플라인 위 임의 지점을 기준으로 주변 정찰 지점을 찾을 반경입니다."))
    float SplinePatrolRadius;

    /* NPC 행동 후 정책 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Behavior", meta = (ToolTip = "행동 후 정책"))
    ENPCAfterActionPolicy AfterActionPolicy;

    /* NPC 타깃 상실 시 정책 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Behavior", meta = (ToolTip = "타깃 상실 시 정책"))
    ENPCLostTargetPolicy LostTargetPolicy;

    /* 공용 GA 태그 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Ability", meta = (ToolTip = "공용 GA 태그"))
    FGameplayTag PrimaryAbilityTag;

    /* NPC 밀기, 잡기, 던지기 행동 설정 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Action", meta = (ToolTip = "NPC별 밀기, 잡기, 던지기 행동 설정"))
    FNPCActionStruct ActionData;

    /* NPC 행동 시작 범위 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Combat", meta = (ToolTip = "액션 시작 거리"))
    float ActionRange;

    /* NPC 행동 인터벌 시간 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Combat", meta = (ToolTip = "액션 후 인터벌"))
    float ActionInterval;

    /* 감지 범위 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Perception", meta = (ToolTip = "타깃 감지 범위"))
    float SightRadius;

    /* 감지 해제 범위 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Perception", meta = (ToolTip = "타깃 감지 해제 범위"))
    float LoseSightRadius;

    /* 시작 시 적용할 상태 태그 */
    UFUNCTION(BlueprintPure, Category = "NPC|Behavior")
    FGameplayTag GetStartStateTag() const;

    /* 타깃이 보이는 동안 반복 행동하는지 */
    UFUNCTION(BlueprintPure, Category = "NPC|Behavior")
    bool ShouldRepeatActionWhileTargetVisible() const;

    /* 1회 행동 후 적용할 다음 상태 태그 */
    UFUNCTION(BlueprintPure, Category = "NPC|Behavior")
    FGameplayTag GetOneShotAfterActionStateTag() const;

    /* 타깃 상실 시 Home 복귀를 사용하는지 */
    UFUNCTION(BlueprintPure, Category = "NPC|Behavior")
    bool ShouldReturnHomeOnLostTarget() const;

    /* 타깃 상실 시 적용할 상태 태그 */
    UFUNCTION(BlueprintPure, Category = "NPC|Behavior")
    FGameplayTag GetLostTargetStateTag() const;

    /*태그 정보 받아오기*/
    FGameplayTag PatrolTag() const;
    FGameplayTag TrackingTag() const;
    FGameplayTag StayTag() const;
    FGameplayTag HomeTag() const;
};
