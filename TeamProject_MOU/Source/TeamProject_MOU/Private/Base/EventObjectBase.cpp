#include "Base/EventObjectBase.h"
#include "Player/MainCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Components/WidgetComponent.h"
#include "Net/UnrealNetwork.h"

AEventObjectBase::AEventObjectBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// 보통 이벤트 오브젝트는 조금 무거운 느낌으로 설정
	ItemWeight = 50.0f;
	bCanBeStoredInInventory = false;
}

void AEventObjectBase::BeginPlay()
{
	Super::BeginPlay();

	// 밀기 전용 오브젝트의 경우 질량을 매우 높게 설정하여 캐릭터가 부딪혀서 밀리는 것을 방지
	if (bIsPushable)
	{
		if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(GetRootComponent()))
		{
			// 물리 충돌로 밀리는 현상 방지: 아예 물리 시뮬레이션을 끄고 스크립트로만 이동하게 만듦
			PrimComp->SetSimulatePhysics(false);

			// 레벨에 살짝 떠있을 경우를 대비해 시작 시 바닥으로 강제 스냅
			FVector BoxCenter, BoxExtents;
			GetActorBounds(false, BoxCenter, BoxExtents);
			FHitResult GroundHit;
			FVector Start = BoxCenter;
			FVector End = Start - FVector(0, 0, 1000.0f);
			FCollisionQueryParams Params;
			Params.AddIgnoredActor(this);
			if (GetWorld()->LineTraceSingleByChannel(GroundHit, Start, End, ECC_Visibility, Params))
			{
				// (원하는 바닥 위치 + 상자 반 높이) - 현재 상자 중심 = 이동해야 할 Z 거리
				float TargetCenterZ = GroundHit.ImpactPoint.Z + BoxExtents.Z;
				float ZOffset = TargetCenterZ - BoxCenter.Z;
				AddActorWorldOffset(FVector(0, 0, ZOffset));
			}
		}
	}

	// [요구사항] 이벤트 오브젝트는 Info Widget을 표시하지 않음
	if (InfoWidgetComponent)
	{
		InfoWidgetComponent->DestroyComponent();
		InfoWidgetComponent = nullptr;
	}
}

void AEventObjectBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AEventObjectBase, CurrentPushers);
}


void AEventObjectBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 바닥에 떨어졌는지 체크 (추락 후 착지 감지)
	if (bIsFallingFromLedge)
	{
		if (FallTimer > 0.0f)
		{
			FallTimer -= DeltaTime;
		}
		else
		{
			// 속도가 0에 가까워지면 착지한 것으로 간주
			if (GetVelocity().SizeSquared() < 10.0f)
			{
				bIsFallingFromLedge = false;
				if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(GetRootComponent()))
				{
					// 몸으로 밀리지 않도록 다시 물리 시뮬레이션 끄기
					PrimComp->SetSimulatePhysics(false);
					
					// 바닥에 정확히 스냅
					FVector BoxCenter, BoxExtents;
					GetActorBounds(false, BoxCenter, BoxExtents);
					FHitResult GroundHit;
					FVector Start = BoxCenter;
					FVector End = Start - FVector(0, 0, 1000.0f);
					FCollisionQueryParams Params;
					Params.AddIgnoredActor(this);
					if (GetWorld()->LineTraceSingleByChannel(GroundHit, Start, End, ECC_Visibility, Params))
					{
						float TargetCenterZ = GroundHit.ImpactPoint.Z + BoxExtents.Z;
						float ZOffset = TargetCenterZ - BoxCenter.Z;
						AddActorWorldOffset(FVector(0, 0, ZOffset));
					}
				}
			}
		}
	}

	// 디버그 라인 그리기 (바운딩 박스 정중앙 기준)
	if (bShowDebugPushDistance)
	{
		FVector BoxCenter = GetComponentsBoundingBox().GetCenter();
		DrawDebugCircle(GetWorld(), BoxCenter, MaxPushDistance, 32, FColor::Green, false, -1.0f, 0, 2.0f, FVector(0, 1, 0), FVector(1, 0, 0), false);
	}
}

float AEventObjectBase::GetPushResistance_Implementation() const
{
	return ItemWeight;
}

void AEventObjectBase::Push_Implementation(AActor* Pusher, FVector PushDirection)
{
	// 기존의 IPushableInterface 통로
	// 이제 MainCharacter가 직접 StartPushMode를 호출할 것이므로 이곳에서는 추가 로직이 필요 없을 수도 있습니다.
}

void AEventObjectBase::Interact_Implementation(AActor* Interactor)
{
	if (!Interactor) return;

	if (!bIsPushable)
	{
		UE_LOG(LogTemp, Warning, TEXT("밀기 불가: bIsPushable이 꺼져 있습니다."));
		return;
	}

	// 거리 체크: 캐릭터의 위치와 오브젝트의 바운딩 박스 정중앙 간의 수평(2D) 거리 계산 (높이 무시)
	FVector BoxCenter = GetComponentsBoundingBox().GetCenter();
	float Dist = FVector::Dist2D(Interactor->GetActorLocation(), BoxCenter);
	if (Dist > MaxPushDistance)
	{
		UE_LOG(LogTemp, Warning, TEXT("밀기 실패: 거리가 너무 멉니다 (현재: %f, 최대: %f)"), Dist, MaxPushDistance);
		return;
	}

	// F키 상호작용 시 밀기(Push) 모드로 진입
	// 밀기 모드 진입 시
	if (AMainCharacter* MainChar = Cast<AMainCharacter>(Interactor))
	{
		MainChar->StartPushMode(this);
	}
}

void AEventObjectBase::AddPusher(AMainCharacter* Pusher)
{
	if (HasAuthority() && Pusher && !CurrentPushers.Contains(Pusher))
	{
		CurrentPushers.Add(Pusher);
	}
}

void AEventObjectBase::RemovePusher(AMainCharacter* Pusher)
{
	if (HasAuthority() && Pusher && CurrentPushers.Contains(Pusher))
	{
		CurrentPushers.Remove(Pusher);
	}
}

bool AEventObjectBase::IsReadyToMove() const
{
	if (CurrentPushers.Num() < RequiredPushers)
	{
		return false;
	}

	if (RequiredPushers <= 1)
	{
		return true;
	}

	// 2인 이상 협동 오브젝트: 필요한 인원수만큼 모두 같은 방향으로 밀거나 당기고 있는지 확인
	float CommonSign = 0.0f;
	int32 ActivePusherCount = 0;

	for (const TObjectPtr<AMainCharacter>& Pusher : CurrentPushers)
	{
		if (Pusher)
		{
			float Input = Pusher->GetCurrentPushInput();
			if (FMath::Abs(Input) > 0.1f)
			{
				float Sign = FMath::Sign(Input);
				if (CommonSign == 0.0f)
				{
					CommonSign = Sign;
				}
				else if (CommonSign != Sign)
				{
					// 서로 반대 방향으로 밀고 당김 (충돌 -> 이동 불가)
					return false;
				}
				ActivePusherCount++;
			}
		}
	}

	return ActivePusherCount >= RequiredPushers;
}

void AEventObjectBase::MulticastFallOffLedge_Implementation()
{
	FallOffLedge();
}

void AEventObjectBase::FallOffLedge()
{
	// 상자를 밀던 모든 플레이어의 밀기 모드 강제 해제
	// StopPushMode가 내부적으로 RemovePusher를 호출하여 배열을 수정하므로, 복사본을 만들어 순회하여 크래시 방지
	TArray<TObjectPtr<AMainCharacter>> PushersCopy = CurrentPushers;
	for (AMainCharacter* Pusher : PushersCopy)
	{
		if (Pusher)
		{
			Pusher->StopPushMode();
		}
	}
	CurrentPushers.Empty();

	// 물리 시뮬레이션을 다시 켜서 바닥으로 추락하도록 만듦
	if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(GetRootComponent()))
	{
		PrimComp->SetSimulatePhysics(true);
	}

	bIsFallingFromLedge = true;
	FallTimer = 0.5f; // 떨어지는 초기에는 속도가 낮을 수 있으므로 0.5초 유예
}
