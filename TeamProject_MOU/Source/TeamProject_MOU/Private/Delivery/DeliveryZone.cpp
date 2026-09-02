#include "Delivery/DeliveryZone.h"

#include "Base/PackageBase.h"
#include "Components/BoxComponent.h"
#include "Delivery/DeliveryManager.h"
#include "TimerManager.h"

ADeliveryZone::ADeliveryZone()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	DeliveryBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("DeliveryBounds"));
	SetRootComponent(DeliveryBounds);
	DeliveryBounds->SetBoxExtent(FVector(200.0f));
	DeliveryBounds->SetCollisionProfileName(TEXT("DeliveryZone"));
	// ItemBase가 운반/드롭 과정에서 PhysicsActor 프로필을 복원하므로
	// PhysicsBody도 감지하되 실제 납품 여부는 등록된 Package/Zone ID로 제한합니다.
	DeliveryBounds->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	DeliveryBounds->SetGenerateOverlapEvents(true);
}

FName ADeliveryZone::GetEffectiveZoneID() const
{
	return ZoneID.IsNone() ? GetFName() : ZoneID;
}

void ADeliveryZone::BeginPlay()
{
	Super::BeginPlay();
	// 기존 Blueprint 인스턴스에 저장된 충돌값보다 런타임 배달 규칙을 우선합니다.
	DeliveryBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DeliveryBounds->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	DeliveryBounds->SetGenerateOverlapEvents(true);
	if (HasAuthority())
	{
		ZoneID = GetEffectiveZoneID();
		RegisterWithDeliveryManager();

		DeliveryBounds->OnComponentBeginOverlap.AddDynamic(
			this, &ADeliveryZone::HandleDeliveryOverlap);
		DeliveryBounds->OnComponentEndOverlap.AddDynamic(
			this, &ADeliveryZone::HandleDeliveryEndOverlap);
	}
}

void ADeliveryZone::RegisterWithDeliveryManager()
{
	if (!HasAuthority()) return;

	if (ADeliveryManager* Manager = ADeliveryManager::GetDeliveryManager(this))
	{
		Manager->RegisterDeliveryZone(ZoneID);
		return;
	}

	// GameMode가 Manager를 생성하기 전에 Zone BeginPlay가 실행된 경우 다음 프레임에 다시 시도합니다.
	GetWorldTimerManager().SetTimerForNextTick(this, &ADeliveryZone::RegisterWithDeliveryManager);
}

void ADeliveryZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (TPair<TWeakObjectPtr<APackageBase>, FTimerHandle>& Pair : ActiveDeliveryTimers)
	{
		GetWorldTimerManager().ClearTimer(Pair.Value);
	}
	ActiveDeliveryTimers.Reset();
	Super::EndPlay(EndPlayReason);
}

void ADeliveryZone::HandleDeliveryOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	APackageBase* Package = Cast<APackageBase>(OtherActor);
	if (!Package) return;

	ADeliveryManager* Manager = ADeliveryManager::GetDeliveryManager(this);
	if (!Manager || !Manager->IsPackageAssignedToZone(Package, ZoneID)
		|| ActiveDeliveryTimers.Contains(Package))
	{
		return;
	}

	FTimerHandle& TimerHandle = ActiveDeliveryTimers.Add(Package);
	const TWeakObjectPtr<APackageBase> WeakPackage = Package;
	FTimerDelegate DeliveryDelegate = FTimerDelegate::CreateWeakLambda(this, [this, WeakPackage]()
	{
		CompleteHeldDelivery(WeakPackage);
	});
	GetWorldTimerManager().SetTimer(
		TimerHandle, DeliveryDelegate, FMath::Max(0.1f, RequiredHoldSeconds), false);
}

void ADeliveryZone::HandleDeliveryEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex)
{
	APackageBase* Package = Cast<APackageBase>(OtherActor);
	if (!Package) return;

	if (FTimerHandle* TimerHandle = ActiveDeliveryTimers.Find(Package))
	{
		GetWorldTimerManager().ClearTimer(*TimerHandle);
		ActiveDeliveryTimers.Remove(Package);
	}
}

void ADeliveryZone::CompleteHeldDelivery(TWeakObjectPtr<APackageBase> Package)
{
	ActiveDeliveryTimers.Remove(Package);
	if (!Package.IsValid()) return;

	if (ADeliveryManager* Manager = ADeliveryManager::GetDeliveryManager(this))
	{
		Manager->TryDeliverPackage(Package.Get());
	}
}
