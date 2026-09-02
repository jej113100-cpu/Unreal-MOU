#pragma once

#include "CoreMinimal.h"
#include "TrapEnums.generated.h"

/**
 * 함정의 수명주기 및 네트워크 복제 상태 FSM
 */
UENUM(BlueprintType)
enum class ETrapState : uint8
{
	Idle UMETA(DisplayName = "대기 (Idle)"),
	Warning UMETA(DisplayName = "경고/전조 (Warning)"),
	Active UMETA(DisplayName = "발동/작동 중 (Active)"),
	Cooldown UMETA(DisplayName = "쿨다운 (Cooldown)"),
	Disarmed UMETA(DisplayName = "해제/무력화 (Disarmed)")
};

/**
 * 함정 트리거 감지 방식
 */
UENUM(BlueprintType)
enum class ETrapTriggerType : uint8
{
	PressurePlate UMETA(DisplayName = "압력판 (Overlap)"),
	LaserRaycast UMETA(DisplayName = "레이저 센서 (Raycast)"),
	PeriodicTimer UMETA(DisplayName = "주기적 타이머 (Periodic)"),
	ProximitySensor UMETA(DisplayName = "근접 감지 (Proximity)"),
	RemoteSignal UMETA(DisplayName = "원격 신호/스위치 연동 (Remote)")
};

/**
 * 함정이 인가하는 위험 효과 유형
 */
UENUM(BlueprintType)
enum class ETrapHazardType : uint8
{
	Damage UMETA(DisplayName = "즉시/지속 피해"),
	CrowdControl UMETA(DisplayName = "상태이상 (속박/스턴/둔화)"),
	PhysicsImpulse UMETA(DisplayName = "물리 밀침/넉백"),
	ElectricShock UMETA(DisplayName = "감전 (스턴 + 배터리 방전)"),
	Slippery UMETA(DisplayName = "미끄러운 지면"),
	ItemHazard UMETA(DisplayName = "물품 강제 드랍/파손")
};

/**
 * 가시 바닥 동작 모드
 */
UENUM(BlueprintType)
enum class ESpikeOperationMode : uint8
{
	AlwaysActive UMETA(DisplayName = "상시 돌출형"),
	PeriodicPopup UMETA(DisplayName = "주기적 팝업형"),
	PressureTriggered UMETA(DisplayName = "압력 감지형"),
	RemoteLinked UMETA(DisplayName = "외부 신호/레이저 연동형")
};
