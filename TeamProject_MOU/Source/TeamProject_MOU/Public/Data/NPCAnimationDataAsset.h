#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NPCAnimationDataAsset.generated.h"

class UAnimMontage;

/**
 * NPC 종류별로 달라지는 공용 애니메이션을 보관하는 데이터 에셋입니다.
 * 공용 Gameplay Ability는 이 에셋에서 몽타주를 가져와 NPC별 애니메이션만 교체할 수 있습니다.
 */
UCLASS(BlueprintType)
class TEAMPROJECT_MOU_API UNPCAnimationDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** NPC가 사망할 때 재생할 몽타주입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Animation|Death")
	TObjectPtr<UAnimMontage> DeathMontage;
};
