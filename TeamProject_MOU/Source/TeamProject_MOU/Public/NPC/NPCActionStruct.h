#pragma once

#include "CoreMinimal.h"
#include "NPCActionStruct.generated.h"

class UAnimMontage;

/* 잡기 성공 후 실행할 마무리 행동 */
UENUM(BlueprintType)
enum class ENPCGrabFinishType : uint8
{
	Release UMETA(DisplayName = "Release", ToolTip = "잡은 대상을 바닥에 내려놓습니다."),
	Throw   UMETA(DisplayName = "Throw", ToolTip = "잡은 대상을 지정된 힘으로 던집니다.")
};

/* NPC 밀기 행동 설정 */
USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FNPCPushActionData
{
	GENERATED_BODY()

	/* 밀기 행동에 사용할 몽타주 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Push", meta = (ToolTip = "NPC 메시에 맞는 밀기 몽타주"))
	TObjectPtr<UAnimMontage> Montage = nullptr;

	/* 대상을 수평 방향으로 미는 힘 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Push", meta = (ClampMin = "0.0", ToolTip = "NPC 전방으로 적용할 밀기 힘"))
	float HorizontalPower = 500.0f;

	/* 밀 때 추가로 적용할 위쪽 힘 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Push", meta = (ClampMin = "0.0", ToolTip = "대상을 밀 때 추가할 수직 방향 힘"))
	float VerticalPower = 200.0f;

	/* 기존 수평 속도를 밀기 속도로 덮어쓸지 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Push", meta = (ToolTip = "활성화하면 대상의 기존 XY 속도를 밀기 속도로 교체합니다."))
	bool bOverrideHorizontalVelocity = true;

	/* 기존 수직 속도를 밀기 속도로 덮어쓸지 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Push", meta = (ToolTip = "활성화하면 대상의 기존 Z 속도를 밀기 속도로 교체합니다."))
	bool bOverrideVerticalVelocity = true;
};

/* NPC 던지기 행동 설정 */
USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FNPCThrowActionData
{
	GENERATED_BODY()

	/* 던지기 행동에 사용할 몽타주 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Throw", meta = (ToolTip = "NPC 메시에 맞는 던지기 몽타주"))
	TObjectPtr<UAnimMontage> Montage = nullptr;

	/* 대상을 NPC 전방으로 던지는 힘 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Throw", meta = (ClampMin = "0.0", ToolTip = "NPC 전방으로 적용할 던지기 힘"))
	float HorizontalPower = 1400.0f;

	/* 대상을 위로 띄우는 힘 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Throw", meta = (ClampMin = "0.0", ToolTip = "던질 때 추가할 수직 방향 힘"))
	float VerticalPower = 450.0f;

	/* 기존 수평 속도를 던지기 속도로 덮어쓸지 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Throw", meta = (ToolTip = "활성화하면 대상의 기존 XY 속도를 던지기 속도로 교체합니다."))
	bool bOverrideHorizontalVelocity = true;

	/* 기존 수직 속도를 던지기 속도로 덮어쓸지 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Throw", meta = (ToolTip = "활성화하면 대상의 기존 Z 속도를 던지기 속도로 교체합니다."))
	bool bOverrideVerticalVelocity = true;
};

/* NPC 잡기 행동 설정 */
USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FNPCGrabActionData
{
	GENERATED_BODY()

	/* 잡기 행동에 사용할 단일 몽타주 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Grab", meta = (ToolTip = "NPC 메시에 맞는 잡기 시작/유지/해제 몽타주"))
	TObjectPtr<UAnimMontage> Montage = nullptr;

	/* 잡기 성공 후 실행할 마무리 행동 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Grab", meta = (ToolTip = "잡은 대상을 내려놓을지 던질지 결정합니다."))
	ENPCGrabFinishType FinishType = ENPCGrabFinishType::Release;

	/* 잡힌 대상을 따라가게 할 소켓 이름 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Grab", meta = (ToolTip = "잡힌 대상이 따라갈 NPC 메시 소켓 이름"))
	FName SocketName = TEXT("GrabSocket");

	/* 잡기 소켓을 기준으로 적용할 상대 위치 보정값 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Grab", meta = (ToolTip = "잡힌 대상 위치에 추가할 상대 오프셋"))
	FVector RelativeOffset = FVector::ZeroVector;

	/* 잡힌 대상이 NPC 소켓 회전을 따라갈지 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Grab", meta = (ToolTip = "활성화하면 잡힌 대상이 NPC 소켓의 회전을 따라갑니다."))
	bool bInheritRotation = true;

	/* 잡은 상태를 유지할 시간 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Grab", meta = (ClampMin = "0.0", ToolTip = "잡기 성공 후 내려놓거나 던지기 전까지 유지할 시간"))
	float HoldDuration = 1.0f;

	/* 잡기 성공 후 사용할 던지기 설정 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Grab", meta = (ToolTip = "Finish Type이 Throw일 때 사용할 설정"))
	FNPCThrowActionData Throw;
};

/* NPC의 밀기, 잡기, 던지기 행동 설정 모음 */
USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FNPCActionStruct
{
	GENERATED_BODY()

	/* 밀기 행동 설정 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action", meta = (ToolTip = "NPC 밀기 행동에 사용하는 설정"))
	FNPCPushActionData Push;

	/* 잡기 행동 설정 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action", meta = (ToolTip = "NPC 잡기 행동에 사용하는 설정"))
	FNPCGrabActionData Grab;

	/* 독립적인 던지기 행동 설정 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action", meta = (ToolTip = "잡기와 별개로 실행하는 NPC 던지기 행동 설정"))
	FNPCThrowActionData Throw;
};
