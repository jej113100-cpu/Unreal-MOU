#pragma once

#include "CoreMinimal.h"
#include "TeamProject_MOUCharacter.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagAssetInterface.h"
#include "GameFramework/Character.h"
#include "GameplayAbilitySpecHandle.h"
#include "Interfaces/PushableInterface.h"
#include "CharacterBase.generated.h"

class AController;
class UAbilitySystemComponent;
class UBaseAttributeSet;
class UGameplayAbility;
class UGrabFollowComponent;
class UStatusComponent;
struct FOnAttributeChangeData;

/**
 * ACharacterBase
 * 플레이어 및 방해 NPC/적대 세력의 최상위 공통 베이스 캐릭터 클래스.
 * GAS(Gameplay Ability System) 및 공통 상태 관리(StatusComponent)를 내장합니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API ACharacterBase : public ATeamProject_MOUCharacter, public IAbilitySystemInterface, public IPushableInterface, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	ACharacterBase();

	// ---------------------------------------------------------
	// [GAS 및 상태 관리 컴포넌트]
	// ---------------------------------------------------------

	// 어빌리티 시스템 컴포넌트 (GAS 핵심 기능 관리)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	// 어트리뷰트 세트 배열 (GAS 어트리뷰트 보관함)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TArray<TObjectPtr<UAttributeSet>> BaseAttributeSet;

	// 기본 어트리뷰트 세트 (체력, 스태미나, 이동속도 속성 소유)
	UPROPERTY(EditAnywhere, Category = "Attributes")
	TObjectPtr<UBaseAttributeSet> BaseAttribute;

	// 캐릭터가 패널티 없이 소지할 수 있는 기본 최대 무게 (에디터 디테일 창에서 바로 수정 가능)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Attributes")
	float DefaultMaxWeight = 100.0f;

	// 상태 이상 및 디버프 관리 컴포넌트 (플레이어 및 NPC 공통 사용)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
	TObjectPtr<UStatusComponent> StatusComponent;

	// 잡힌 캐릭터가 운반자의 소켓 위치를 따라가도록 관리하는 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grab")
	TObjectPtr<UGrabFollowComponent> GrabFollowComponent;

	// 캐릭터 생성 시 기본 부여할 Gameplay Ability 스킬 배열
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GASGamePlayAbility")
	TArray<TSubclassOf<UGameplayAbility>> InitalAbilities;

	// ---------------------------------------------------------
	// [공통 캐릭터 상태 판정 함수] - AIController 및 PlayerController 공유
	// ---------------------------------------------------------

	// 캐릭터가 현재 이동 가능한 상태인지 확인 (기절/잡힘/넉백 상태 시 false)
	UFUNCTION(BlueprintCallable, Category = "Status")
	virtual bool CanMove() const;

	// 캐릭터가 현재 행동(공격, 상호작용, 스킬) 가능한 상태인지 확인
	UFUNCTION(BlueprintCallable, Category = "Status")
	virtual bool CanAct() const;

	// ---------------------------------------------------------
	// [Blueprint 이벤트] - UI 및 연출 업데이트용 델리게이트 알림
	// ---------------------------------------------------------

	// 체력(Health/MaxHealth) 변경 시 호출되는 이벤트 (UI 및 AI 업데이트용)
	UFUNCTION(BlueprintImplementableEvent, Category = "Attributes")
	void OnHealthUpdated(float CurrentHealth, float MaxHealth);
	 
	// 스태미나(Stemina/MaxStemina) 변경 시 호출되는 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "Attributes")
	void OnSteminaupdated(float CurrentStemina, float MaxStemina);

	// 이동속도(MoveSpeed/MaxMoveSpeed) 변경 시 호출되는 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "Attributes")
	void OnSpeedUpdated(float CurrentSpeed, float MaxSpeed);

	// 무게(CurrentWeight/MaxWeight) 변경 시 호출되는 이벤트 (UI 업데이트용)
	UFUNCTION(BlueprintImplementableEvent, Category = "Attributes")
	void OnWeightUpdated(float CurrentWeight, float MaxWeight, float WeightRatio);

	// 배터리(Battery/MaxBattery) 변경 시 호출되는 이벤트 (UI 업데이트용)
	UFUNCTION(BlueprintImplementableEvent, Category = "Attributes")
	void OnBatteryUpdated(float CurrentBattery, float MaxBattery, float BatteryRatio);

protected:
	// GAS 리플리케이션 모드 (멀티플레이 동기화 설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AbilitySystem")
	EGameplayEffectReplicationMode AscReplicationMode = EGameplayEffectReplicationMode::Mixed;
	
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;

	// GAS Attribute 변경 감지 델리게이트 바인딩 함수
	virtual void BindAttributeChangeDelegates();

	// 체력 변경 콜백 핸들러
	virtual void HandleHealthChanged(const FOnAttributeChangeData& Data);
	virtual void HandleMaxHealthChanged(const FOnAttributeChangeData& Data);

	 
	// 스태미나 변경 콜백 핸들러
	virtual void HandleSteminaChanged(const FOnAttributeChangeData& Data);
	virtual void HandleMaxSteminaChanged(const FOnAttributeChangeData& Data);

	// 이동속도 변경 콜백 핸들러
	void HandleMoveSpeedChanged(const FOnAttributeChangeData& Data);
	void HandleMaxMoveSpeedChanged(const FOnAttributeChangeData& Data);

	// 무게 변경 콜백 핸들러 (과적 시스템 연동)
	void HandleCurrentWeightChanged(const FOnAttributeChangeData& Data);
	void HandleMaxWeightChanged(const FOnAttributeChangeData& Data);

	// 배터리 변경 콜백 핸들러 (발광 시스템 연동)
	virtual void HandleBatteryChanged(const FOnAttributeChangeData& Data);
	virtual void HandleMaxBatteryChanged(const FOnAttributeChangeData& Data);

public:
	// 현재 과적 및 운반 상태에 따른 적정 걷기 속도 계산 (기본 300, 과적1: 255, 과적2/무거운택배: 150, 과적3: 0)
	UFUNCTION(BlueprintCallable, Category = "Attributes|Movement")
	float GetCalculatedWalkSpeed() const;

	// 과적(Encumbrance) 상태 업데이트 및 이동속도/상태 디버프 적용
	UFUNCTION(BlueprintCallable, Category = "Status|Encumbrance")
	void UpdateEncumbranceState(float InCurrentWeight, float InMaxWeight);

protected:
	// ---------------------------------------------------------
	// [과적(Encumbrance) GameplayEffect 설정]
	// ---------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Status|Encumbrance")
	TSubclassOf<class UGameplayEffect> EncumberedTier1Effect; // 100%~130%: 경미 감속

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Status|Encumbrance")
	TSubclassOf<class UGameplayEffect> EncumberedTier2Effect; // 130%~150%: 심한 감속 & 달리기 금지

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Status|Encumbrance")
	TSubclassOf<class UGameplayEffect> EncumberedTier3Effect; // 150% 초과: 이동 불가

	UPROPERTY(Transient)
	FActiveGameplayEffectHandle ActiveEncumbranceEffectHandle;

	// 델리게이트 중복 바인딩 방지 플래그
	bool AttributeDelegatesBound = false;

public:
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// IAbilitySystemInterface 구현: AbilitySystemComponent 반환
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// IGameplayTagAssetInterface 구현
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
	virtual bool HasMatchingGameplayTag(FGameplayTag TagToCheck) const override;
	virtual bool HasAllMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const override;
	virtual bool HasAnyMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const override;

	// StatusComponent 반환
	UStatusComponent* GetStatusComponent() const { return StatusComponent; }

	// 단일 Gameplay Ability 부여 함수
	FGameplayAbilitySpecHandle InitializeAbility(TSubclassOf<UGameplayAbility> AbilityToGet, int32 AbilityLevel);

	// 다수 Gameplay Ability 일괄 부여 함수
	void InitializeAbilityMulti(TArray<TSubclassOf<UGameplayAbility>> AbilityToAcquire, int32 AbilityLevel);

	// ---------------------------------------------------------
	// [밀기 인터페이스 구현 (IPushableInterface)]
	// ---------------------------------------------------------
	virtual float GetPushResistance_Implementation() const override;
	virtual void Push_Implementation(AActor* Pusher, FVector PushDirection) override;
};
