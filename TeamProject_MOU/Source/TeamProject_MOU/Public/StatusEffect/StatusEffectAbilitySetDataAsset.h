#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StatusEffectAbilitySetDataAsset.generated.h"

class UGameplayAbility;
class UStatusEffectDataAsset;

/* 상태이상 컴포넌트가 ASC에 지급할 Gameplay Ability 목록 */
UCLASS(BlueprintType)
class TEAMPROJECT_MOU_API UStatusEffectAbilitySetDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/* 상태이상 처리를 담당하는 Gameplay Ability 클래스 목록 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect|Abilities")
	TArray<TSubclassOf<UGameplayAbility>> Abilities;

	/* 목록의 Ability를 지급할 레벨 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect|Abilities", meta = (ClampMin = "1"))
	int32 AbilityLevel = 1;

	/* 이 Ability Set을 사용하는 유닛별 상태이상 설정 목록 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect|Data")
	TArray<TObjectPtr<UStatusEffectDataAsset>> EffectDataAssets;
};
