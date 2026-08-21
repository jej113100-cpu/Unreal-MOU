#pragma once

#include "CoreMinimal.h"
#include "Base/CharacterBase.h"
#include "Interfaces/InteractableInterface.h"
#include "MainCharacter.generated.h"

class UInteractionComponent;
class UCarryingComponent;
class UInventoryComponent;
class UInputAction;
class AItemBase;

UCLASS()
class TEAMPROJECT_MOU_API AMainCharacter : public ACharacterBase, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AMainCharacter();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

public:
	// 상호작용 컴포넌트 반환
	UFUNCTION(BlueprintCallable, Category = "Components")
	UInteractionComponent* GetInteractionComponent() const { return InteractionComponent; }

	// 운반 컴포넌트 반환
	UFUNCTION(BlueprintCallable, Category = "Components")
	UCarryingComponent* GetCarryingComponent() const { return CarryingComponent; }

	// 인벤토리 컴포넌트 반환
	UFUNCTION(BlueprintCallable, Category = "Components")
	UInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }

	// 현재 달리기(Sprint) 중인지 여부 반환
	UFUNCTION(BlueprintCallable, Category = "Player|Movement")
	bool IsSprinting() const { return bIsSprinting; }

	// 현재 기절(넉다운) 상태인지 여부 반환
	UFUNCTION(BlueprintCallable, Category = "Player|Status")
	bool IsStunned() const { return bIsStunned; }

	// 외부(택배 충돌 등)에서 호출할 넉다운 함수
	UFUNCTION(BlueprintCallable, Category = "Player|Status")
	void Knockdown();

	// ---------------------------------------------------------
	// [그로기 및 사망 (Groggy / Death)]
	// ---------------------------------------------------------

	// 현재 다운 횟수 (1이면 그로기, 2면 사망)
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Player|Status")
	int32 DownCount = 0;

	// 현재 그로기 상태인지 여부
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Player|Status")
	bool bIsGroggy = false;

	// 현재 완전히 사망한 상태인지 여부
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Player|Status")
	bool bIsDead = false;

	// 부활 중(일어나는 중) 상태인지 여부 (입력 차단용)
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Player|Status")
	bool bIsReviving = false;

	// ---------------------------------------------------------
	// [타입별 표정 변화 인덱스 (블루프린트에서 쉽게 변경 가능)]
	// ---------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Emotions")
	int32 EmotionIndex_Normal = 0; // 평상시 기본 표정

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Emotions")
	int32 EmotionIndex_HeavyPackage = 1; // 무거운 택배를 들었을 때

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Emotions")
	int32 EmotionIndex_Pushing = 2; // 물건을 밀 때

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Emotions")
	int32 EmotionIndex_CarryCharacter = 3; // 죽은 캐릭터를 들었을 때

	// 체력이 0이 되었을 때 AttributeSet에서 호출할 함수
	UFUNCTION(BlueprintCallable, Category = "Player|Status")
	void HandleHealthZero();

	// 체력을 전달받아 부활 처리하는 함수
	UFUNCTION(BlueprintCallable, Category = "Player|Status")
	void ReviveCharacter(float RestoredHealth);

	// 타이머 만료 시 호출될 일반 함수 (이 함수가 FinishRevive RPC를 호출함)
	void OnReviveTimerExpired();

	// 부활 애니메이션 완료 후 입력 차단 해제 (서버 및 모든 클라이언트)
	UFUNCTION(NetMulticast, Reliable, BlueprintCallable, Category = "Player|Status")
	void FinishRevive();

	// 블루프린트에서 애니메이션 및 표정 처리를 위한 이벤트들 (Multicast로 모든 클라이언트에 방송)
	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnEnterGroggy();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnRevived();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnDeath();

	// 블루프린트에서 누르는 실제 이벤트 (로컈에서만 실행, 애니메이션과 표정 노드 연결용)
	UFUNCTION(BlueprintImplementableEvent, Category = "Player|Status")
	void OnEnterGroggy();

	UFUNCTION(BlueprintImplementableEvent, Category = "Player|Status")
	void OnRevived();

	UFUNCTION(BlueprintImplementableEvent, Category = "Player|Status")
	void OnDeath();

	// ---------------------------------------------------------
	// [상호작용 인터페이스 (팀원 부활용)]
	// ---------------------------------------------------------
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractPrompt_Implementation() const override;

	// 클라이언트에서 호출하여 서버에서 대상 하를 부활 해주는 Server RPC
	UFUNCTION(Server, Reliable)
	void ServerReviveTarget(AMainCharacter* Target);

	// ---------------------------------------------------------
	// [팀원 부활 홀드(차징) 시스템]
	// ---------------------------------------------------------

	// 부활 완료에 필요한 홀드(차징) 시간 (초) - 에디터에서 자유롭게 조정 가능
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Revive", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float RequiredReviveTime = 3.0f;

	// 현재 부활 차징 중인지 여부
	UPROPERTY(BlueprintReadOnly, Category = "Player|Revive")
	bool bIsHoldingRevive = false;

	// 현재 누적된 차징 시간 (초)
	UPROPERTY(BlueprintReadOnly, Category = "Player|Revive")
	float CurrentReviveHoldTime = 0.0f;

	// 현재 부활을 시도 중인 대상 캐릭터
	UPROPERTY(BlueprintReadOnly, Category = "Player|Revive")
	TWeakObjectPtr<AMainCharacter> CurrentReviveTarget;

	// 현재 에임(조준점)에 그로기 상태의 팀원이 들어와 있는지 여부 (화면 에임 UI 표시용)
	UPROPERTY(BlueprintReadOnly, Category = "Player|Revive")
	bool bIsAimingAtGroggyTarget = false;

	// 현재 에임하고 있는 그로기 팀원
	UPROPERTY(BlueprintReadOnly, Category = "Player|Revive")
	TObjectPtr<AMainCharacter> FocusedGroggyTarget;

	// 에임(조준점)에 그로기 팀원이 들어오거나 나갈 때 호출되는 이벤트 (화면 중앙 UI 띄우기용)
	UFUNCTION(BlueprintImplementableEvent, Category = "Player|Revive")
	void OnReviveTargetAimChanged(bool bIsAiming, AMainCharacter* TargetCharacter);

	// 부활 완료에 필요한 총 시간 반환 (GA_Revive의 ReviveDuration이 설정되어 있다면 우선 적용)
	UFUNCTION(BlueprintPure, Category = "Player|Revive")
	float GetRequiredReviveTime() const;

	// 부활 차징 진행률 반환 (0.0 ~ 1.0)
	UFUNCTION(BlueprintPure, Category = "Player|Revive")
	float GetReviveProgress() const;

	// 부활 차징 여부 반환
	UFUNCTION(BlueprintPure, Category = "Player|Revive")
	bool IsHoldingRevive() const { return bIsHoldingRevive; }

	// 부활 차징 시작
	UFUNCTION(BlueprintCallable, Category = "Player|Revive")
	void StartReviveHold(AMainCharacter* Target);

	// 부활 차징 취소 (키 뗌, 피격, 거리 벗어남 등)
	UFUNCTION(BlueprintCallable, Category = "Player|Revive")
	void CancelReviveHold();

	// 부활 차징 완료 (실제 부활 트리거)
	UFUNCTION(BlueprintCallable, Category = "Player|Revive")
	void CompleteReviveHold();

	// 차징 시작 시 호출되는 Blueprint 이벤트 (UI 표시용)
	UFUNCTION(BlueprintImplementableEvent, Category = "Player|Revive")
	void OnReviveHoldStarted(AMainCharacter* Target);

	// 차징 진행 시 매 틱 호출되는 Blueprint 이벤트 (0.0 ~ 1.0 게이지 갱신용)
	UFUNCTION(BlueprintImplementableEvent, Category = "Player|Revive")
	void OnReviveHoldProgress(float Progress);

	// 차징 종료 시 호출되는 Blueprint 이벤트 (bSuccess: 성공 여부)
	UFUNCTION(BlueprintImplementableEvent, Category = "Player|Revive")
	void OnReviveHoldEnded(bool bSuccess);

	// 대상 캐릭터의 머리 위 UI(WidgetComponent) 등에 진행률을 전달하기 위한 함수
	UFUNCTION(BlueprintCallable, Category = "Player|UI")
	void SetReviveProgress(float Progress);

	// 대상 캐릭터 측에서 진행률 변경을 감지하는 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "Player|UI")
	void OnReviveProgressUpdated(float Progress);

	// --- Push Mode ---
	UFUNCTION(BlueprintCallable, Category = "Player|Push")
	void StartPushMode(AActor* TargetObject);

	UFUNCTION(Server, Reliable)
	void ServerStartPushMode(AActor* TargetObject);

	UFUNCTION(BlueprintCallable, Category = "Player|Push")
	void StopPushMode();

	UFUNCTION(Server, Reliable)
	void ServerStopPushMode();

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_IsPushingMode, Category = "Player|Push")
	bool bIsPushingMode = false;

	UFUNCTION()
	void OnRep_IsPushingMode();

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Player|Push")
	TObjectPtr<AActor> CurrentPushedObject;

	// 밀기 모드 중 현재 입력값 (+1.0: 밀기, -1.0: 당기기, 0.0: 정지)
	UPROPERTY(Replicated)
	float CurrentPushInput = 0.0f;

	UFUNCTION(Server, Reliable)
	void ServerSetPushInput(float NewInput);

	float GetCurrentPushInput() const { return CurrentPushInput; }

	UPROPERTY(BlueprintReadOnly, Category = "Player|Push")
	float PushOffsetDistance = 120.0f;

	// 이동량 계산을 위한 이전 위치 저장
	UPROPERTY(BlueprintReadOnly, Category = "Player|Push")
	FVector LastCharacterLocation;

	// 밀기 진입 시 고정되는 방향
	UPROPERTY(BlueprintReadOnly, Category = "Player|Push")
	FVector LockedPushDirection;

	// 원래 회전 허용 옵션 저장용
	bool bOriginalOrientRotation = true;

	// ---------------------------------------------------------
	// [이동 조작 오버라이드] - ACharacterBase 상태 판정 연동
	// ---------------------------------------------------------

	virtual void DoMove(float Right, float Forward) override;
	virtual void DoJumpStart() override;
	virtual bool CanAct() const override;
	virtual bool CanMove() const override;

protected:
	// 상호작용 탐색 컴포넌트 (F키)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UInteractionComponent> InteractionComponent;

	// 물품 잡기/던지기 컴포넌트 (E/Q키)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCarryingComponent> CarryingComponent;

	// 인벤토리 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UInventoryComponent> InventoryComponent;

public:
	// ---------------------------------------------------------
	// [GAS 이동 및 상호작용 어빌리티 설정]
	// ---------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Abilities")
	TSubclassOf<class UGA_Sprint> SprintAbilityClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Abilities")
	TSubclassOf<class UGA_PushObject> PushAbilityClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Abilities")
	TSubclassOf<class UGA_CarryItem> CarryItemAbilityClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Abilities")
	TSubclassOf<class UGA_HeavyCarry> HeavyCarryAbilityClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Abilities")
	TSubclassOf<class UGA_CoopCarry> CoopCarryAbilityClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Abilities")
	TSubclassOf<class UGA_CarryCharacter> CarryCharacterAbilityClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Abilities")
	TSubclassOf<class UGA_Revive> ReviveAbilityClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Abilities")
	TSubclassOf<class UGA_ThrowItem> ThrowAbilityClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Abilities")
	TSubclassOf<class UGA_Emote> EmoteAbilityClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Abilities")
	TSubclassOf<class UGA_Jump> JumpAbilityClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Abilities")
	TSubclassOf<class UGA_Interact> InteractAbilityClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Abilities")
	TSubclassOf<class UGA_Groggy> GroggyAbilityClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Abilities")
	TSubclassOf<class UGA_Death> DeathAbilityClass;

	UPROPERTY(Transient)
	FGameplayAbilitySpecHandle SprintAbilitySpecHandle;

	UPROPERTY(Transient)
	FGameplayAbilitySpecHandle PushAbilitySpecHandle;

	UPROPERTY(Transient)
	FGameplayAbilitySpecHandle ReviveAbilitySpecHandle;

	UPROPERTY(Transient)
	FGameplayAbilitySpecHandle ThrowAbilitySpecHandle;

	UPROPERTY(Transient)
	FGameplayAbilitySpecHandle EmoteAbilitySpecHandle;

	UPROPERTY(Transient)
	FGameplayAbilitySpecHandle JumpAbilitySpecHandle;

	UPROPERTY(Transient)
	FGameplayAbilitySpecHandle InteractAbilitySpecHandle;

	UPROPERTY(Transient)
	FGameplayAbilitySpecHandle GroggyAbilitySpecHandle;

	UPROPERTY(Transient)
	FGameplayAbilitySpecHandle DeathAbilitySpecHandle;

protected:
	virtual void HandleHealthChanged(const struct FOnAttributeChangeData& Data) override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

	// ---------------------------------------------------------
	// [입력 액션 (Enhanced Input Action)]
	// ---------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> GrabOrDropAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ThrowAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> UseAction;

	// 인벤토리 슬롯 단축키
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Slot1Action;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Slot2Action;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Slot3Action;

	// ---------------------------------------------------------
	// [이모트(감정표현) 시스템]
	// ---------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> EmoteToggleAction;

	// 얼굴 동적 머티리얼 인스턴스 배열 (초기화 시 눈, 입 등을 찾아 자동 할당)
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UMaterialInstanceDynamic>> FaceMaterialInstances;

	// 감정 인덱스를 변경하는 머티리얼 파라미터 이름 (머티리얼에 적힌 이름과 정확히 일치해야 함)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emote")
	FName EmotionParameterName = FName("Emotion index"); 

	// 감정 색상을 변경하는 머티리얼 파라미터 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emote")
	FName EmotionColorParameterName = FName("Emission Color");

	// 이모트 퀵슬롯 UI 호출 이벤트 (블루프린트에서 위젯 띄우기 용도)
	UFUNCTION(BlueprintImplementableEvent, Category = "Emote")
	void OpenEmoteUI();

public:
	// 얼굴 표정과 색상을 즉시 변경합니다.
	UFUNCTION(BlueprintCallable, Category = "Emote")
	void SetEmotion(int32 EmotionIndex, FLinearColor EmoteColor = FLinearColor(0.0f, 0.623294f, 1.0f, 1.0f));

	// 표정과 색상을 변경하고 애니메이션(몽타주)을 재생합니다.
	UFUNCTION(BlueprintCallable, Category = "Emote")
	void PlayEmote(class UAnimMontage* EmoteMontage, int32 EmotionIndex, FLinearColor EmoteColor = FLinearColor(0.0f, 0.623294f, 1.0f, 1.0f));

	UFUNCTION(Server, Reliable)
	void ServerPlayEmote(class UAnimMontage* EmoteMontage, int32 EmotionIndex, FLinearColor EmoteColor);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayEmote(class UAnimMontage* EmoteMontage, int32 EmotionIndex, FLinearColor EmoteColor);

	// 표정 정보만 강제로 바꾸고 복제하는 함수 (애니메이션 몽타주 없이 표정만 바꿀 때 유용함)
	UFUNCTION(BlueprintCallable, Category = "Emote")
	void ChangeEmotion(int32 EmotionIndex, FLinearColor EmoteColor = FLinearColor(0.0f, 0.623294f, 1.0f, 1.0f));

	UFUNCTION(Server, Reliable)
	void ServerChangeEmotion(int32 EmotionIndex, FLinearColor EmoteColor);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastChangeEmotion(int32 EmotionIndex, FLinearColor EmoteColor);

	// 이모트 종료 시 기본 표정 초기화(Index 0)하는 콜백
	UFUNCTION()
	void OnEmoteMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// 현재 재생 중인 이모트 몽타주 캐싱 (이동 시 취소 용도)
	UPROPERTY(Transient)
	TObjectPtr<class UAnimMontage> CurrentEmoteMontage;

	// ---------------------------------------------------------
	// [스태미나 파라미터]
	// ---------------------------------------------------------

	// 초당 스태미나 소모량 (달리기 시)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attributes")
	float StaminaDrainRate = 15.0f;

	// 초당 스태미나 회복량 (달리기 멈춤 시)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attributes")
	float StaminaRegenRate = 10.0f;

	// 스태미나 0 도달 시 달리기 쿨다운 시간(초)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attributes")
	float ExhaustionCooldownDuration = 2.0f;

public:
	// 달리기 시작/종료 함수
	void OnSprintStart();
	void OnSprintEnd();

private:
	// 입력 핸들러 함수들
	void OnInteract();
	void OnInteractEnd();
	void OnGrabOrDrop();
	void OnThrow();
	void OnJumpStartInput();
	void OnJumpEndInput();
	void OnEmoteToggle();
	void OnUse();

	void OnSlot1();
	void OnSlot2();
	void OnSlot3();

	// 인벤토리 장착 요청 콜백
	UFUNCTION()
	void OnEquipRequested(AItemBase* ItemToEquip);

	// 포커스된 상호작용 대상이 변경될 때 호출되는 콜백
	UFUNCTION()
	void OnFocusedActorChanged(AActor* NewFocusedActor);

	// 이전 포커스 액터를 캐싱하여 상태 되돌리기에 사용
	UPROPERTY(Transient)
	TObjectPtr<AActor> PreviousFocusedActor;

	// 달리기 동기화를 위한 서버 RPC
	UFUNCTION(Server, Reliable)
	void ServerSetSprinting(bool bSprint);

	// 스태미나 처리 프라이빗 메서드
	void UpdateStamina(float DeltaTime);

	// 현재 달리기 조작 입력 상태
	UPROPERTY(Replicated)
	bool bIsSprinting = false;

	// 스태미나 고갈에 따른 쿨다운 딜레이 진행 타이머
	float CurrentExhaustionTimer = 0.0f;

	// ---------------------------------------------------------
	// [기절 / 넉다운 (Stun / Knockdown)]
	// ---------------------------------------------------------

protected:
	// 넉다운 시 재생할 몽타주 (에디터에서 할당 필요)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Player|Animation")
	TObjectPtr<class UAnimMontage> KnockdownMontage;

	// 현재 기절(Stun) 상태 여부 (이동/행동 불가)
	UPROPERTY(Replicated)
	bool bIsStunned = false;

	// 클라이언트 동기화용 멀티캐스트
	UFUNCTION(NetMulticast, Reliable)
	void MulticastKnockdown();

	// 넉다운 종료 콜백
	void OnKnockdownEnd();

	// 넉다운 복구 타이머
	FTimerHandle KnockdownTimerHandle;
};
