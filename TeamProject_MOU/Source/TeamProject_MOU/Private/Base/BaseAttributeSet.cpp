// Fill out your copyright notice in the Description page of Project Settings.

#include "Base/BaseAttributeSet.h"

#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"
#include "Player/MainCharacter.h"

UBaseAttributeSet::UBaseAttributeSet()
{
	Health = 100.0f;
	MaxHealth = 100.0f;
	Stemina = 100.0f;
	MaxStemina = 100.0f;
	MoveSpeed = 300.0f;
	MaxMoveSpeed = 2000.0f;
	CurrentWeight = 0.0f;
	MaxWeight = 100.0f;
	Battery = 100.0f;
	MaxBattery = 100.0f;
}

void UBaseAttributeSet::ResetHealthToMax()
{
	SetHealth(GetMaxHealth());
}

void UBaseAttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, Health, OldValue);
}

void UBaseAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, MaxHealth, OldValue);
}

void UBaseAttributeSet::OnRep_MoveSpeed(const FGameplayAttributeData& OldValue) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, MoveSpeed, OldValue);
}

void UBaseAttributeSet::OnRep_MaxMoveSpeed(const FGameplayAttributeData& OldValue) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, MaxMoveSpeed, OldValue);
}

void UBaseAttributeSet::OnRep_Stemina(const FGameplayAttributeData& OldValue) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, Stemina, OldValue);
}

void UBaseAttributeSet::OnRep_MaxStemina(const FGameplayAttributeData& OldValue) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, MaxStemina, OldValue);
}

void UBaseAttributeSet::OnRep_CurrentWeight(const FGameplayAttributeData& OldValue) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, CurrentWeight, OldValue);
}

void UBaseAttributeSet::OnRep_MaxWeight(const FGameplayAttributeData& OldValue) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, MaxWeight, OldValue);
}

void UBaseAttributeSet::OnRep_Battery(const FGameplayAttributeData& OldValue) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, Battery, OldValue);
}

void UBaseAttributeSet::OnRep_MaxBattery(const FGameplayAttributeData& OldValue) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, MaxBattery, OldValue);
}

void UBaseAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, MoveSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, MaxMoveSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, Stemina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, MaxStemina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, CurrentWeight, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, MaxWeight, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, Battery, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, MaxBattery, COND_None, REPNOTIFY_Always);
}

void UBaseAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetMoveSpeedAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxMoveSpeed());
	}
	else if(Attribute == GetSteminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxStemina());
	}
	else if(Attribute == GetCurrentWeightAttribute())
	{
		NewValue = FMath::Max(0.0f, NewValue);
	}
	else if (Attribute == GetBatteryAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxBattery());
	}
}

void UBaseAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.0f, GetMaxHealth()));

		if (GetHealth() <= 0.0f)
		{
			if (AMainCharacter* MainCharacter = Cast<AMainCharacter>(GetOwningActor()))
			{
				MainCharacter->HandleHealthZero();
			}
		}
	}
	else if (Data.EvaluatedData.Attribute == GetMoveSpeedAttribute())
	{
		SetMoveSpeed(FMath::Clamp(GetMoveSpeed(), 0.0f, GetMaxMoveSpeed()));
	}
	else if(Data.EvaluatedData.Attribute == GetSteminaAttribute())
	{
		SetStemina(FMath::Clamp(GetStemina(), 0.0f, GetMaxStemina()));
	}
	else if(Data.EvaluatedData.Attribute == GetCurrentWeightAttribute())
	{
		SetCurrentWeight(FMath::Max(0.0f, GetCurrentWeight()));
	}
	else if (Data.EvaluatedData.Attribute == GetBatteryAttribute())
	{
		SetBattery(FMath::Clamp(GetBattery(), 0.0f, GetMaxBattery()));
	}
}
