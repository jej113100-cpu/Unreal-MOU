#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TestDamageActor.generated.h"

class UBoxComponent;

UCLASS()
class TEAMPROJECT_MOU_API ATestDamageActor : public AActor
{
	GENERATED_BODY()
	
public:	
	ATestDamageActor();

protected:
	virtual void BeginPlay() override;

public:	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UBoxComponent* CollisionBox;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	float DamageAmount;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, 
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, 
		bool bFromSweep, const FHitResult& SweepResult);
};
