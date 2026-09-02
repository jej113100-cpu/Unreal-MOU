#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Data/CharacterVisualDataAsset.h"
#include "CharacterVisualComponent.generated.h"

class UMaterialInstanceDynamic;
class UAbilitySystemComponent;
class ACharacterBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVisualPresetApplied, const FCharacterVisualPreset&, AppliedPreset);

/**
 * 캐릭터의 얼굴 표정(MI) 및 LED 발광 색상/강도를 상태 우선순위에 따라 통합 제어하는 컴포넌트
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UCharacterVisualComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCharacterVisualComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 비주얼 프리셋 적용 시 브로드캐스트되는 델리게이트
	UPROPERTY(BlueprintAssignable, Category = "Visual")
	FOnVisualPresetApplied OnVisualPresetApplied;

	// 데이터 에셋 런타임 변경
	UFUNCTION(BlueprintCallable, Category = "Visual")
	void SetVisualDataAsset(UCharacterVisualDataAsset* InDataAsset);

	// 무게 비율 변경 시 호출 (CharacterBase의 OnWeightUpdated에서 연동)
	UFUNCTION(BlueprintCallable, Category = "Visual")
	void HandleWeightUpdated(float CurrentWeight, float MaxWeight, float WeightRatio);

	// 상태 태그 변경 시 호출 (외부 또는 GAS 연동)
	UFUNCTION(BlueprintCallable, Category = "Visual")
	void RefreshVisualState();

	// 수동으로 특정 표정과 색상을 적용 (하위 호환 및 디버그용)
	UFUNCTION(BlueprintCallable, Category = "Visual")
	void ApplyCustomEmotion(int32 EmotionIndex, FLinearColor InColor, float Intensity = 1.0f);

	// 일시적 오버라이드 (이모트 몽타주 재생 등)
	UFUNCTION(BlueprintCallable, Category = "Visual")
	void SetTemporaryOverride(const FCharacterVisualPreset& OverridePreset, float Duration = 0.0f);

	// 태그에 설정된 프리셋을 찾아 지정된 시간 동안 일시적으로 오버라이드 (Data Asset 설정값 직접 사용)
	UFUNCTION(BlueprintCallable, Category = "Visual")
	void SetTagTemporaryOverride(const FGameplayTag& Tag, float Duration = 0.0f);

	// 일시적 오버라이드 해제 및 원래 우선순위 상태로 복원
	UFUNCTION(BlueprintCallable, Category = "Visual")
	void ClearTemporaryOverride();

	// 현재 적용 중인 비주얼 프리셋 반환
	UFUNCTION(BlueprintPure, Category = "Visual")
	const FCharacterVisualPreset& GetCurrentPreset() const { return CurrentPreset; }

	// 현재 무게 등급 반환
	UFUNCTION(BlueprintPure, Category = "Visual")
	EWeightGrade GetCurrentWeightGrade() const { return CurrentWeightGrade; }

	// 얼굴 동적 머티리얼 인스턴스 배열 반환
	UFUNCTION(BlueprintPure, Category = "Visual")
	const TArray<UMaterialInstanceDynamic*>& GetFaceMaterialInstances() const { return FaceMaterialInstances; }

	// 신체 동적 머티리얼 인스턴스 배열 반환
	UFUNCTION(BlueprintPure, Category = "Visual")
	const TArray<UMaterialInstanceDynamic*>& GetBodyMaterialInstances() const { return BodyMaterialInstances; }

	// 동적 머티리얼 수동 재초기화
	UFUNCTION(BlueprintCallable, Category = "Visual")
	void ReinitializeDynamicMaterials();

protected:
	// 비주얼 설정 데이터 에셋
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UCharacterVisualDataAsset> VisualDataAsset;

	// 얼굴 동적 머티리얼 인스턴스 (Eye, Mouth 메시 등)
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FaceMaterialInstances;

	// 신체 동적 머티리얼 인스턴스 (Skeletal Mesh)
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BodyMaterialInstances;

	// 현재 적용 중인 프리셋
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Visual")
	FCharacterVisualPreset CurrentPreset;

	// 현재 계산된 무게 등급
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Visual")
	EWeightGrade CurrentWeightGrade = EWeightGrade::Light;

	// 임시 오버라이드 활성화 여부
	UPROPERTY(Transient)
	bool bHasTemporaryOverride = false;

	UPROPERTY(Transient)
	FCharacterVisualPreset TemporaryOverridePreset;

	FTimerHandle TemporaryOverrideTimerHandle;

private:
	void InitDynamicMaterials();
	void BindAbilitySystemEvents();
	void UnbindAbilitySystemEvents();

	// 활성화된 태그 및 과적 수치를 검사하여 최상위 우선순위 프리셋 산출
	FCharacterVisualPreset ResolveHighestPriorityPreset() const;

	// MID에 파라미터 실제 적용
	void ApplyPresetToMaterials(const FCharacterVisualPreset& Preset);

	void HandleGameplayTagChanged(const FGameplayTag Tag, int32 NewCount);

	TWeakObjectPtr<ACharacterBase> OwnerCharacter;
	TWeakObjectPtr<UAbilitySystemComponent> CachedASC;
	FDelegateHandle TagChangeDelegateHandle;
};
