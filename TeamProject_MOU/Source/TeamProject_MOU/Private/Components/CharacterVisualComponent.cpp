#include "Components/CharacterVisualComponent.h"
#include "Base/CharacterBase.h"
#include "Player/MainCharacter.h"
#include "Components/StatusComponent.h"
#include "AbilitySystemComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"

UCharacterVisualComponent::UCharacterVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCharacterVisualComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<ACharacterBase>(GetOwner());

	InitDynamicMaterials();
	BindAbilitySystemEvents();

	// 기본 데이터 에셋이 설정되지 않은 경우 생성
	if (!VisualDataAsset)
	{
		VisualDataAsset = NewObject<UCharacterVisualDataAsset>(this, TEXT("DefaultVisualDataAsset"));
	}

	RefreshVisualState();
}

void UCharacterVisualComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindAbilitySystemEvents();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TemporaryOverrideTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UCharacterVisualComponent::InitDynamicMaterials()
{
	FaceMaterialInstances.Empty();
	BodyMaterialInstances.Empty();

	if (!OwnerCharacter.IsValid())
	{
		return;
	}

	// 1. 눈(Eye), 입(Mouth) 등 얼굴용 스태틱 메시 컴포넌트 탐색 및 DMI 생성
	TArray<UStaticMeshComponent*> StaticMeshes;
	OwnerCharacter->GetComponents<UStaticMeshComponent>(StaticMeshes);

	for (UStaticMeshComponent* SM : StaticMeshes)
	{
		if (SM)
		{
			FString CompName = SM->GetName();
			bool bIsFaceMesh = CompName.Contains(TEXT("Eye")) || CompName.Contains(TEXT("Mouth")) || CompName.Contains(TEXT("Face"));

			if (bIsFaceMesh)
			{
				int32 NumMaterials = SM->GetNumMaterials();
				for (int32 i = 0; i < NumMaterials; ++i)
				{
					if (UMaterialInstanceDynamic* DMI = SM->CreateAndSetMaterialInstanceDynamic(i))
					{
						FaceMaterialInstances.Add(DMI);
					}
				}
			}
		}
	}

	// 2. 신체 스켈레탈 메시 머티리얼 슬롯 탐색 및 DMI 생성
	if (USkeletalMeshComponent* SkelMesh = OwnerCharacter->GetMesh())
	{
		int32 MatCount = SkelMesh->GetNumMaterials();
		for (int32 i = 0; i < MatCount; ++i)
		{
			if (UMaterialInstanceDynamic* DMI = SkelMesh->CreateAndSetMaterialInstanceDynamic(i))
			{
				BodyMaterialInstances.Add(DMI);
			}
		}
	}
}

void UCharacterVisualComponent::ReinitializeDynamicMaterials()
{
	InitDynamicMaterials();
	RefreshVisualState();
}

void UCharacterVisualComponent::BindAbilitySystemEvents()
{
	if (!OwnerCharacter.IsValid())
	{
		return;
	}

	UAbilitySystemComponent* ASC = OwnerCharacter->GetAbilitySystemComponent();
	if (ASC && ASC != CachedASC.Get())
	{
		UnbindAbilitySystemEvents();
		CachedASC = ASC;

		TagChangeDelegateHandle = ASC->RegisterGenericGameplayTagEvent().AddUObject(
			this, &UCharacterVisualComponent::HandleGameplayTagChanged);
	}
}

void UCharacterVisualComponent::UnbindAbilitySystemEvents()
{
	if (CachedASC.IsValid() && TagChangeDelegateHandle.IsValid())
	{
		CachedASC->RegisterGenericGameplayTagEvent().Remove(TagChangeDelegateHandle);
		TagChangeDelegateHandle.Reset();
	}
	CachedASC.Reset();
}

void UCharacterVisualComponent::HandleGameplayTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	RefreshVisualState();
}

void UCharacterVisualComponent::SetVisualDataAsset(UCharacterVisualDataAsset* InDataAsset)
{
	if (InDataAsset)
	{
		VisualDataAsset = InDataAsset;
		RefreshVisualState();
	}
}

void UCharacterVisualComponent::HandleWeightUpdated(float CurrentWeight, float MaxWeight, float WeightRatio)
{
	CurrentWeightGrade = UCharacterVisualDataAsset::CalculateWeightGrade(WeightRatio);
	RefreshVisualState();
}

void UCharacterVisualComponent::RefreshVisualState()
{
	if (!CachedASC.IsValid())
	{
		BindAbilitySystemEvents();
	}

	FCharacterVisualPreset BestPreset = ResolveHighestPriorityPreset();
	ApplyPresetToMaterials(BestPreset);
}

FCharacterVisualPreset UCharacterVisualComponent::ResolveHighestPriorityPreset() const
{
	if (!VisualDataAsset)
	{
		return FCharacterVisualPreset();
	}

	// 1. 기본 베이스라인: 현재 과적(Encumbrance) 등급의 프리셋
	FCharacterVisualPreset BestPreset = VisualDataAsset->GetEncumbrancePreset(CurrentWeightGrade);

	// 2. 현재 소유한 Gameplay Tag 검사 및 우선순위 비교
	FGameplayTagContainer ActiveTags;
	if (OwnerCharacter.IsValid())
	{
		OwnerCharacter->GetOwnedGameplayTags(ActiveTags);
		if (UStatusComponent* StatusComp = OwnerCharacter->GetStatusComponent())
		{
			ActiveTags.AppendTags(StatusComp->GetActiveStatusTags());
		}

		if (AMainCharacter* MainChar = Cast<AMainCharacter>(OwnerCharacter.Get()))
		{
			if (MainChar->IsHoldingRevive())
			{
				static const FGameplayTag ReviveTag = FGameplayTag::RequestGameplayTag(FName("State.Player.Reviving"), false);
				ActiveTags.AddTag(ReviveTag);
			}
		}
	}

	for (const auto& Pair : VisualDataAsset->StatusTagPresets)
	{
		const FGameplayTag& StatusTag = Pair.Key;
		const FCharacterVisualPreset& Preset = Pair.Value;

		if (ActiveTags.HasTag(StatusTag))
		{
			if (Preset.Priority > BestPreset.Priority)
			{
				BestPreset = Preset;
			}
		}
	}

	// 3. 임시 오버라이드(이모트 등)가 활성화되어 있고 우선순위가 더 높거나 같으면 오버라이드 적용
	if (bHasTemporaryOverride && TemporaryOverridePreset.Priority >= BestPreset.Priority)
	{
		BestPreset = TemporaryOverridePreset;
	}

	return BestPreset;
}

void UCharacterVisualComponent::ApplyPresetToMaterials(const FCharacterVisualPreset& Preset)
{
	if (!VisualDataAsset)
	{
		return;
	}

	// 얼굴 DMI가 아직 캐싱되지 않았을 경우 재탐색 시도 (BP 동적 생성 컴포넌트 대응)
	if (FaceMaterialInstances.Num() == 0)
	{
		InitDynamicMaterials();
	}

	const FName EmotionScalar = VisualDataAsset->EmotionScalarParamName;
	const FName EmissionColor = VisualDataAsset->EmissionColorParamName;
	const FName EmissionIntensity = VisualDataAsset->EmissionIntensityParamName;

	// 1. 얼굴 머티리얼 파라미터 적용 (Emotion index, LED Color, Intensity)
	for (UMaterialInstanceDynamic* DMI : FaceMaterialInstances)
	{
		if (DMI)
		{
			DMI->SetScalarParameterValue(EmotionScalar, Preset.EmotionIndex);
			DMI->SetVectorParameterValue(EmissionColor, Preset.LEDColor);
			DMI->SetScalarParameterValue(EmissionIntensity, Preset.EmissionIntensity);
		}
	}

	// 2. 신체 머티리얼 파라미터 적용
	for (UMaterialInstanceDynamic* DMI : BodyMaterialInstances)
	{
		if (DMI)
		{
			DMI->SetVectorParameterValue(EmissionColor, Preset.LEDColor);
			DMI->SetScalarParameterValue(EmissionIntensity, Preset.EmissionIntensity);
		}
	}

	CurrentPreset = Preset;
	OnVisualPresetApplied.Broadcast(CurrentPreset);
}

void UCharacterVisualComponent::ApplyCustomEmotion(int32 EmotionIndex, FLinearColor InColor, float Intensity)
{
	FCharacterVisualPreset CustomPreset(static_cast<float>(EmotionIndex), InColor, Intensity, 999);
	ApplyPresetToMaterials(CustomPreset);
}

void UCharacterVisualComponent::SetTemporaryOverride(const FCharacterVisualPreset& OverridePreset, float Duration)
{
	bHasTemporaryOverride = true;
	TemporaryOverridePreset = OverridePreset;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TemporaryOverrideTimerHandle);
		if (Duration > 0.0f)
		{
			World->GetTimerManager().SetTimer(TemporaryOverrideTimerHandle, this, &UCharacterVisualComponent::ClearTemporaryOverride, Duration, false);
		}
	}

	RefreshVisualState();
}

void UCharacterVisualComponent::SetTagTemporaryOverride(const FGameplayTag& Tag, float Duration)
{
	if (VisualDataAsset)
	{
		FCharacterVisualPreset Preset;
		if (VisualDataAsset->GetStatusPreset(Tag, Preset))
		{
			// 데이터 에셋에서 Priority를 기본값(0)으로 두었더라도, 일시 피격 반응이 과적 상태(10~60)를 이기도록 기본 75 보장
			if (Preset.Priority <= 0)
			{
				Preset.Priority = 75;
			}
			SetTemporaryOverride(Preset, Duration);
			return;
		}
	}

	// Data Asset에 태그가 등록되지 않았을 경우 기본 피격 프리셋 폴백
	SetTemporaryOverride(FCharacterVisualPreset(1.0f, FLinearColor(1.0f, 0.8f, 0.0f, 1.0f), 1.5f, 75), Duration);
}

void UCharacterVisualComponent::ClearTemporaryOverride()
{
	bHasTemporaryOverride = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TemporaryOverrideTimerHandle);
	}

	RefreshVisualState();
}
