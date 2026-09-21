#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFocusedInteractableChanged, AActor*, NewFocusedActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteractExecuted, AActor*, InteractedActor);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractionComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void PerformInteraction();

	// 일부 상호작용 대상(예: 배치된 아이템 드론)은 서버에서 Interact가 실행돼야 상태가 바뀐다.
	// PerformInteraction은 클라 로컬에서 도므로, 그런 대상에 한해 이 RPC로 서버에서 Interact를 재실행한다.
	// (플레이어 캐릭터에 붙은 컴포넌트라 항상 로컬 소유 → 소유권 문제 없이 서버 도달)
	UFUNCTION(Server, Reliable)
	void ServerRunInteract(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	AActor* GetFocusedInteractable() const { return FocusedActor; }

	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnFocusedInteractableChanged OnFocusedInteractableChanged;

	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnInteractExecuted OnInteractExecuted;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	float InteractionDistance = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	float InteractionSphereRadius = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

private:
	void UpdateFocusedInteractable();

	UPROPERTY()
	TObjectPtr<AActor> FocusedActor;

	// 블루프린트 상호작용 여부 판정 캐시 (매 틱 리플렉션/문자열 검색 방지)
	TMap<TWeakObjectPtr<UClass>, bool> BPInteractableClassCache;
};
