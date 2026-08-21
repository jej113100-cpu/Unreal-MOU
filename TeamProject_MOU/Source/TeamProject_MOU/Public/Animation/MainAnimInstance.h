#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MainAnimInstance.generated.h"

class AMainCharacter;
class UCharacterMovementComponent;

/**
 * UMainAnimInstance
 * MainCharacter의 상태(이동, 점프, 달리기, 물건 들기, 물체 밀기, 기절 등)를
 * 애니메이션 그래프로 전달하기 위한 C++ AnimInstance 베이스 클래스입니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API UMainAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UMainAnimInstance();

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	// ---------------------------------------------------------
	// [캐릭터 및 무브먼트 참조]
	// ---------------------------------------------------------

	// 소유자 캐릭터 참조 (MainCharacter)
	UPROPERTY(BlueprintReadOnly, Category = "Animation|References")
	TObjectPtr<AMainCharacter> MainCharacter;

	// 소유자 캐릭터의 무브먼트 컴포넌트 참조
	UPROPERTY(BlueprintReadOnly, Category = "Animation|References")
	TObjectPtr<UCharacterMovementComponent> MovementComponent;

	// ---------------------------------------------------------
	// [애니메이션 그래프 전달 변수]
	// ---------------------------------------------------------

	// 캐릭터 수평 이동 속도 (0 ~ 800)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Movement")
	float GroundSpeed = 0.0f;

	// 캐릭터가 실제 이동 중인지 여부 (GroundSpeed > 3.0f)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Movement")
	bool bShouldMove = false;

	// 캐릭터가 공중에 떠 있는지 여부 (점프/낙하)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsFalling = false;

	// 캐릭터가 달리기(Sprint) 중인지 여부
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsSprinting = false;

	// 캐릭터가 물품을 들고(Carrying) 있는 상태인지 여부
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State")
	bool bIsCarrying = false;

	// 캐릭터가 물체를 밀고(Pushing) 있는 상태인지 여부
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State")
	bool bIsPushing = false;

	// 캐릭터가 기절(Stunned) 상태인지 여부
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Status")
	bool bIsStunned = false;

	// 캐릭터가 잡힌(Held) 상태인지 여부
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Status")
	bool bIsHeld = false;

	// [Heavy 전용] 무거운 택배를 혼자 들고 있는 상태 (= 드래그 애니메이션)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State")
	bool bIsHeavySingleCarry = false;

	// [Heavy 전용] 내가 두 번째 운반자인지 여부 (= IK로 뒤쪽 잘는 사람)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State")
	bool bIsSecondCarrier = false;

};
