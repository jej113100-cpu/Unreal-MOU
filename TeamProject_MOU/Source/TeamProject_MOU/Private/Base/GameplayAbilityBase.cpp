#include "Base/GameplayAbilityBase.h"
#include "Base/CharacterBase.h"
#include "Base/BaseAttributeSet.h"

UGameplayAbilityBase::UGameplayAbilityBase()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

ACharacterBase* UGameplayAbilityBase::GetCharacterFromActorInfo() const
{
	if (!CurrentActorInfo)
	{
		return nullptr;
	}
	return Cast<ACharacterBase>(CurrentActorInfo->AvatarActor.Get());
}

UBaseAttributeSet* UGameplayAbilityBase::GetBaseAttributeSet() const
{
	ACharacterBase* Char = GetCharacterFromActorInfo();
	return Char ? Char->BaseAttribute : nullptr;
}

