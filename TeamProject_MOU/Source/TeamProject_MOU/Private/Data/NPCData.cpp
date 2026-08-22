#include "Data/NPCData.h"

#include "GameplayTagsManager.h"


FGameplayTag UNPCData::PatrolTag() const
{
	return UGameplayTagsManager::Get().RequestGameplayTag(TEXT("State.NPC.Patrol"));
}

FGameplayTag UNPCData::TrackingTag() const
{
	return UGameplayTagsManager::Get().RequestGameplayTag(TEXT("State.NPC.Tracking"));
}

FGameplayTag UNPCData::StayTag() const
{
	return UGameplayTagsManager::Get().RequestGameplayTag(TEXT("State.NPC.Stay"));
}

FGameplayTag UNPCData::HomeTag() const
{
	return UGameplayTagsManager::Get().RequestGameplayTag(TEXT("State.NPC.Home"));
}

UNPCData::UNPCData()
	: StartState(ENPCStartState::Patrol)
	, UsePatrol(true)
	, PatrolType(ENPCPatrolType::RandomRadius)
	, PatrolRadius(1000.0f)
	, SplinePatrolRadius(200.0f)
	, AfterActionPolicy(ENPCAfterActionPolicy::RepeatWhileTargetVisible)
	, LostTargetPolicy(ENPCLostTargetPolicy::ReturnToPatrol)
	, ActionRange(200.0f)
	, ActionInterval(1.0f)
	, SightRadius(1500.0f)
	, LoseSightRadius(2000.0f)
{
}

FGameplayTag UNPCData::GetStartStateTag() const
{
	switch (StartState)
	{
	case ENPCStartState::Stay:
		return StayTag();
	case ENPCStartState::Home:
		return HomeTag();
	case ENPCStartState::Patrol:
	default:
		return PatrolTag();
	}
}

bool UNPCData::ShouldRepeatActionWhileTargetVisible() const
{
	return AfterActionPolicy == ENPCAfterActionPolicy::RepeatWhileTargetVisible;
}

FGameplayTag UNPCData::GetOneShotAfterActionStateTag() const
{
	switch (AfterActionPolicy)
	{
	case ENPCAfterActionPolicy::OneShotThenStay:
		return StayTag();
	case ENPCAfterActionPolicy::OneShotThenTracking:
		// 기존 데이터 호환용 값이며, OneShot 완료 후에는 더 이상 Tracking으로 복귀하지 않는다.
		return StayTag();
	case ENPCAfterActionPolicy::OneShotThenPatrol:
		return PatrolTag();
	case ENPCAfterActionPolicy::RepeatWhileTargetVisible:
	default:
		return TrackingTag();
	}
}

bool UNPCData::ShouldReturnHomeOnLostTarget() const
{
	return LostTargetPolicy == ENPCLostTargetPolicy::ReturnHome;
}

FGameplayTag UNPCData::GetLostTargetStateTag() const
{
	switch (LostTargetPolicy)
	{
	case ENPCLostTargetPolicy::ReturnToStay:
		return StayTag();
	case ENPCLostTargetPolicy::ReturnHome:
		return HomeTag();
	case ENPCLostTargetPolicy::ReturnToPatrol:
	default:
		return PatrolTag();
	}
}
