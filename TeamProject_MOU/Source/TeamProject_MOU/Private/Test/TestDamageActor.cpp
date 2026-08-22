#include "Test/TestDamageActor.h"
#include "Components/BoxComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Base/BaseAttributeSet.h"
#include "Base/CharacterBase.h"

ATestDamageActor::ATestDamageActor()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	RootComponent = CollisionBox;
	
	// Default size and collision profile
	CollisionBox->SetBoxExtent(FVector(100.0f, 100.0f, 100.0f));
	CollisionBox->SetCollisionProfileName(TEXT("Trigger"));

	DamageAmount = 10.0f;
}

void ATestDamageActor::BeginPlay()
{
	Super::BeginPlay();
	
	if (HasAuthority())
	{
		CollisionBox->OnComponentBeginOverlap.AddDynamic(this, &ATestDamageActor::OnOverlapBegin);
	}
}

void ATestDamageActor::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, 
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, 
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (OtherActor && OtherActor != this)
	{
		// Try to get ASC from the overlapping actor
		UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OtherActor);
		if (ASC)
		{
			// 정상적인 데미지 파이프라인(GameplayEffect)을 태워서 BaseAttributeSet의 PostGameplayEffectExecute가 호출되도록 수정
			UGameplayEffect* DamageEffect = NewObject<UGameplayEffect>(GetTransientPackage(), FName(TEXT("TestDamage")));
			DamageEffect->DurationPolicy = EGameplayEffectDurationType::Instant;
			DamageEffect->Modifiers.SetNum(1);
			
			// Health 깎기
			FGameplayModifierInfo& HealthModifier = DamageEffect->Modifiers[0];
			HealthModifier.Attribute = UBaseAttributeSet::GetHealthAttribute();
			HealthModifier.ModifierOp = EGameplayModOp::Additive;
			HealthModifier.ModifierMagnitude = FScalableFloat(-DamageAmount);

			ASC->ApplyGameplayEffectToSelf(DamageEffect, 1.0f, ASC->MakeEffectContext());
			
			UE_LOG(LogTemp, Warning, TEXT("TestDamageActor: Applied %f damage via GameplayEffect to %s"), DamageAmount, *OtherActor->GetName());
		}
	}
}
