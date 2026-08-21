// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.generated.h"

#define ATTRIBUTE_ACCESSORS_BASIC(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

UCLASS()
class TEAMPROJECT_MOU_API UBaseAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UBaseAttributeSet();

	UPROPERTY(BlueprintReadOnly, Category = "Attribute", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS_BASIC(UBaseAttributeSet, Health);

	UPROPERTY(BlueprintReadOnly, Category = "Attribute", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS_BASIC(UBaseAttributeSet, MaxHealth);

	UPROPERTY(BlueprintReadOnly, Category = "Attribute", ReplicatedUsing = OnRep_MoveSpeed)
	FGameplayAttributeData MoveSpeed;
	ATTRIBUTE_ACCESSORS_BASIC(UBaseAttributeSet, MoveSpeed);

	UPROPERTY(BlueprintReadOnly, Category = "Attribute", ReplicatedUsing = OnRep_MaxMoveSpeed)
	FGameplayAttributeData MaxMoveSpeed;
	ATTRIBUTE_ACCESSORS_BASIC(UBaseAttributeSet, MaxMoveSpeed);

	UPROPERTY(BlueprintReadOnly, Category = "Attribute", ReplicatedUsing = OnRep_Stemina)
	FGameplayAttributeData Stemina;
	ATTRIBUTE_ACCESSORS_BASIC(UBaseAttributeSet, Stemina);

	UPROPERTY(BlueprintReadOnly, Category = "Attribute", ReplicatedUsing = OnRep_MaxStemina)
	FGameplayAttributeData MaxStemina;
	ATTRIBUTE_ACCESSORS_BASIC(UBaseAttributeSet, MaxStemina);

	// 현재 들고 있는 무게 (과적 계산용)
	UPROPERTY(BlueprintReadOnly, Category = "Attribute", ReplicatedUsing = OnRep_CurrentWeight)
	FGameplayAttributeData CurrentWeight;
	ATTRIBUTE_ACCESSORS_BASIC(UBaseAttributeSet, CurrentWeight);

	// 과적 패널티 없이 들 수 있는 최대 무게
	UPROPERTY(BlueprintReadOnly, Category = "Attribute", ReplicatedUsing = OnRep_MaxWeight)
	FGameplayAttributeData MaxWeight;
	ATTRIBUTE_ACCESSORS_BASIC(UBaseAttributeSet, MaxWeight);

	UFUNCTION(BlueprintCallable, Category = "Attribute")
	void ResetHealthToMax();

	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue) const;

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue) const;

	UFUNCTION()
	void OnRep_MoveSpeed(const FGameplayAttributeData& OldValue) const;

	UFUNCTION()
	void OnRep_MaxMoveSpeed(const FGameplayAttributeData& OldValue) const;

	UFUNCTION()
	void OnRep_Stemina(const FGameplayAttributeData& OldValue) const;

	UFUNCTION()
	void OnRep_MaxStemina(const FGameplayAttributeData& OldValue) const;

	UFUNCTION()
	void OnRep_CurrentWeight(const FGameplayAttributeData& OldValue) const;

	UFUNCTION()
	void OnRep_MaxWeight(const FGameplayAttributeData& OldValue) const;


	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data) override;
};
