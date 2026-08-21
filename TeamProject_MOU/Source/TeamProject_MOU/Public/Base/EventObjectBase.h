#pragma once

#include "CoreMinimal.h"
#include "Base/ItemBase.h"
#include "Interfaces/PushableInterface.h"
#include "EventObjectBase.generated.h"

/**
 * AEventObjectBase
 * 레벨에 배치되어 줍기(들기), 던지기, 상호작용 및 '밀기(Push)'가 모두 가능한 기믹용 베이스 클래스입니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API AEventObjectBase : public AItemBase, public IPushableInterface
{
	GENERATED_BODY()
	
public:
	AEventObjectBase();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	virtual void Tick(float DeltaTime) override;

	// --- IPushableInterface ---
	virtual float GetPushResistance_Implementation() const override;
	virtual void Push_Implementation(AActor* Pusher, FVector PushDirection) override;

	// 상호작용(F) 오버라이드
	virtual void Interact_Implementation(AActor* Interactor) override;

	// 밀기 가능한 최대 거리 설정
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push")
	float MaxPushDistance = 200.0f;

	// 밀기 가능 여부 토글
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push")
	bool bIsPushable = true;

	// 밀기/당기기에 필요한 최소 인원 수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push")
	int32 RequiredPushers = 1;

	// 디버그 라인으로 밀기 가능 거리 표시 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push")
	bool bShowDebugPushDistance = false;

	// 현재 이 상자를 잡고(밀고) 있는 캐릭터 목록 (서버에서 관리 및 모든 클라이언트에 복제)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Push")
	TArray<TObjectPtr<class AMainCharacter>> CurrentPushers;

	// 밀기 모드 진입/해제 시 호출
	void AddPusher(class AMainCharacter* Pusher);
	void RemovePusher(class AMainCharacter* Pusher);

	// 현재 밀기/당기기가 가능한 상태(인원수 충족)인지 확인
	bool IsReadyToMove() const;

	// 낭떠러지에서 떨어질 때 호출 (로컬 전용)
	void FallOffLedge();

	// 서버가 추락을 감지했을 때 모든 클라이언트에게 추락(물리 켜기 및 밀기 해제)을 동기화
	UFUNCTION(NetMulticast, Reliable)
	void MulticastFallOffLedge();

protected:
	bool bIsFallingFromLedge = false;
	float FallTimer = 0.0f;
};
