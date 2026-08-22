// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayAbilityBase.generated.h"

/**
 * 
 */
class ACharacterBase;
class UBaseAttributeSet;

UCLASS()
class TEAMPROJECT_MOU_API UGameplayAbilityBase : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGameplayAbilityBase();

	UFUNCTION(BlueprintPure, Category = "Ability")
	ACharacterBase* GetCharacterFromActorInfo() const;

	UFUNCTION(BlueprintPure, Category = "Ability")
	UBaseAttributeSet* GetBaseAttributeSet() const;
};
