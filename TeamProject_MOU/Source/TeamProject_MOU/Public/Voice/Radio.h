// MOU - 무전기 아이템.
//
// [이 클래스가 하는 일]
//
//   AItemBase (줍기/놓기/던지기/장착 - 아이템 공통. **다른 파트 소유, 수정 금지**)
//     └ ★ ARadio  ← 이 파일
//          │  · Z = 전원 토글 (인벤토리에 있어도 동작해야 한다)
//          │  · X 홀드 = PTT 송신 (손에 들었을 때만)
//          │  · 배터리 = ItemBase 의 CurrentDurability 재사용
//          ▼ 붙여서 쓴다
//        URadioComponent (전원/손에듦 복제, 라우터 등록, 스피커 반경)
//
// [★ 왜 좌클릭(OnUse)이 아니라 Z / X 인가]
//
//   **좌클릭으로는 두 조작 다 제대로 안 된다.** 무전기가 특이해서가 아니라
//   좌클릭 경로 자체의 한계다:
//
//   1. 좌클릭은 **인벤토리 아이템에 닿지 않는다.**
//      AMainCharacter::OnUse 는 CarryingComponent 가 들고 있는 것만 호출한다.
//      그런데 무전기는 가방에 넣은 채로도 켜져서 수신하고 배터리가 닳으므로,
//      **가방 안의 무전기를 끌 수단**이 반드시 필요하다. Z 는 전역키라서 된다.
//
//   2. 좌클릭에는 **뗀 순간이 없다.**
//      UseAction 은 ETriggerEvent::Started 에만 바인딩돼 있다. PTT 는 누름과
//      뗌이 다 필요한데 Completed 를 붙이려면 MainCharacter 를 고쳐야 하고,
//      그건 다른 파트 소유다.
//
//   그래서 OnUse_Implementation 은 **의도적으로 아무 일도 하지 않는다.**
//   Z 가 이미 전원을 담당하는데 좌클릭도 토글하면 중복이고, 무엇보다
//   **실수로 무전기가 켜지는** 사고가 난다 - 이 게임에서 그건 위치 노출이다.
//
// [★ 확정된 규칙 (2026-08-20)]
//
//   | 상태 | 송신 | 수신 | 스피커 반경 | 배터리 |
//   |---|---|---|---|---|
//   | 손에 듦 | **가능** | 가능 | 100% | 닳음 |
//   | 인벤토리 | 불가 | 가능 | StowedRadiusScale 배 | **닳음** |
//   | 드롭/던짐 | - | - | - | 안 닳음 |
//
//   · **드롭하면 전원이 자동으로 꺼진다.** 자발적/비자발적(사망·기절)을
//     구분하지 않는다 - 떨어지면 무조건 꺼진다.
//   · 인벤토리에 넣어도 전원은 유지된다(수신은 계속된다). 대신 배터리는 닳는다.
//     "켜둔 채로 가방에 넣을까" 가 실제 선택이 되게 하기 위해서다.
//   · 손에 들었다고 자동으로 켜지지 않는다. 전원은 항상 직접 조작한다.
//
//   ★ 이전 설계의 "바닥에 떨어진 무전기가 계속 울려 NPC 를 유인한다" 는
//     **폐기됐다.** NPC 는 무전기가 아니라 무전기를 들고 있는 사람을 쫓는다.
//
// [★★ AWeaponItemBase 를 상속하지 않는 이유]
//
//   무전기는 무기가 아니다. 무기 쪽 상태(WeaponUseCount, Fire, 피아식별)를
//   하나도 쓰지 않으므로 AItemBase 를 직접 상속한다.
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 6절(클래스), 7-3절(전원/송신), 7-4절(스피커 소리)
//   ※ 문서의 7-4절은 위 폐기 결정 이전에 쓰인 것이라 아직 옛 설계를 담고 있다.

#pragma once

#include "CoreMinimal.h"
#include "Base/ItemBase.h"
#include "Radio.generated.h"

class APawn;
class URadioComponent;

/**
 * 무전기 아이템. AItemBase 를 상속하므로 줍기/놓기/던지기/인벤토리가 그대로 동작한다.
 */
UCLASS()
class TEAMPROJECT_MOU_API ARadio : public AItemBase
{
	GENERATED_BODY()

public:
	ARadio();

	// --- 찾기 ---------------------------------------------------------------

	/**
	 * 이 폰이 가지고 있는 무전기를 찾는다. **손에 든 것과 인벤토리에 있는 것 둘 다.**
	 *
	 * ★ Z(전원)/X(송신) 입력 처리에서 쓰라고 만든 함수다. 두 키는 전역키인데,
	 *   특히 Z 는 **가방 안의 무전기도 끌 수 있어야** 하므로 "손에 든 것"만
	 *   찾아서는 안 된다.
	 *
	 * 인벤토리 구조를 알 필요 없이 **부착 관계**만 타고 내려간다 - 손에 들면
	 * 소켓에 붙고, 인벤토리에 넣어도 AItemBase::OnUnequipped 가 플레이어에
	 * 붙여두기 때문이다("투명한 주머니"). 그래서 아이템 파트의 인벤토리 구현이
	 * 바뀌어도 여기가 안 깨진다.
	 *
	 * 두 대 이상이면 먼저 찾은 것을 준다(라우터의 홀더당 1대 규칙과 같은 태도).
	 */
	UFUNCTION(BlueprintPure, Category = "Radio")
	static ARadio* FindCarriedBy(const APawn* Pawn);

	// --- 조회 ---------------------------------------------------------------

	/** 지금 켜져 있는지. UI 표시용. */
	UFUNCTION(BlueprintPure, Category = "Radio")
	bool IsPoweredOn() const;

	/** 손에 들려 있는지. 꺼져 있어도 상관없이 "손에 들었나" 만 본다. */
	UFUNCTION(BlueprintPure, Category = "Radio")
	bool IsInHand() const;

	UFUNCTION(BlueprintPure, Category = "Radio")
	URadioComponent* GetRadioComponent() const { return RadioComponent; }

	// --- 전원 ---------------------------------------------------------------

	/**
	 * 전원을 켜고 끈다. 클라에서 불러도 되고, 서버로 위임된다.
	 *
	 * 배터리가 없으면 켜지지 않는다. 실제 판정은 전부 서버에서 한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radio")
	void SetPowered(bool bOn);

	UFUNCTION(BlueprintCallable, Category = "Radio")
	void TogglePower();

	// --- 배터리 (ItemBase 의 CurrentDurability 재사용) -----------------------
	//
	// ★ 새 변수를 만들지 않는다. CurrentDurability 는 이미 복제되고 있고
	//   (OnRep_CurrentDurability), UI 갱신 훅까지 붙어 있다. CurrentUseCount 는
	//   복제되지 않아서 쓸 수 없다 - AWeaponItemBase 가 같은 이유로 별도 변수를
	//   만들었지만, 여기서는 복제되는 쪽을 그대로 쓰면 된다.

	/** 배터리 잔량 0~1. UI 게이지용. */
	UFUNCTION(BlueprintPure, Category = "Radio|Battery")
	float GetBatteryPercent() const;

	UFUNCTION(BlueprintPure, Category = "Radio|Battery")
	bool HasBattery() const;

	/**
	 * 배터리 소모 속도(초당). MaxDurability 기준이다.
	 *
	 * 기본값 0.1 이면 100 짜리 배터리가 약 16분 간다. **인벤토리에 넣어도
	 * 똑같이 닳는다** - 그래야 켜둘지 말지가 진짜 선택이 된다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Radio|Battery", meta = (ClampMin = "0.0"))
	float BatteryDrainPerSecond = 0.1f;

	/** 배터리를 채운다. 서버 전용(배터리 아이템 등에서 호출). */
	UFUNCTION(BlueprintCallable, Category = "Radio|Battery")
	void RechargeBattery(float Amount);

	// --- 송신 (PTT) ---------------------------------------------------------

	/**
	 * 무전 송신을 시작/종료한다. 설계상 `X` 키 홀드에 대응한다.
	 *
	 * ★ **로컬 클라 전용이다.** 마이크는 각자 자기 컴퓨터에 있으므로 송신 여부는
	 *   UVoiceSubsystem(LocalPlayerSubsystem)이 들고 있다. 여기서 켰다고 무전이
	 *   나가는 것이 아니다 - 손에 들었는지, 켜져 있는지는 서버가 다시 확인한다
	 *   (UVoiceRouter::FindUsableRadioFor).
	 *
	 * 캐릭터/컨트롤러의 입력 바인딩에서 들고 있는 무전기를 찾아 호출하면 된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radio")
	void StartTransmit();

	UFUNCTION(BlueprintCallable, Category = "Radio")
	void StopTransmit();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// --- AItemBase ----------------------------------------------------------

	/**
	 * 좌클릭. **의도적으로 아무 일도 하지 않는다**(위 ★ 참고).
	 *
	 * 전원은 Z, 송신은 X 다. 여기서 전원을 토글하면 Z 와 중복이고 오조작 위험만 는다.
	 *
	 * Super 도 부르지 않는다 - AItemBase::OnUse 는 CurrentUseCount 를 깎지만
	 * 무전기는 쓸 때마다 닳는 물건이 아니다(닳는 것은 배터리이고, 그건 시간이 깎는다).
	 */
	virtual void OnUse_Implementation() override;

	/**
	 * 집을 때 소유권을 설정하고 **손에 들었다고 표시한다.**
	 *
	 * 소유권: 이게 없으면 클라의 Server RPC 가 버려진다("No owning connection").
	 * 전원 토글이 클라에서 시작될 수 있으므로 필요하다.
	 *
	 * ★ 손에 듦 표시가 여기에도 있는 이유: OnEquipped 는 **인벤토리에서 꺼낼 때만**
	 *   불린다. 바닥에서 줍는 경로(UCarryingComponent::GrabOrDrop)에는 없어서,
	 *   여기서 안 켜주면 주운 무전기로 송신이 안 된다(구현부 ★ 참고).
	 */
	virtual void PickUp_Implementation(AActor* Picker) override;

	/** 놓으면 **전원이 꺼진다**(위 ★). 주인 없는 무전기는 소리를 내지 않는다. */
	virtual void Drop_Implementation(FVector DropLocation, AActor* Dropper = nullptr) override;

	/** 던져도 마찬가지로 꺼진다. */
	virtual void Throw_Implementation(FVector ThrowVelocity, AActor* Thrower = nullptr) override;

	/** 손에 들 때. 전원은 건드리지 않는다 - 켜는 것은 언제나 직접 조작이다. */
	virtual void OnEquipped_Implementation(AActor* Equipper) override;

	/**
	 * 인벤토리에 넣을 때. **전원은 유지하고**(수신은 계속된다) 송신만 끊는다.
	 *
	 * X 를 누른 채로 무전기를 집어넣으면 본인도 모르게 계속 송신되는 상태가
	 * 남는데, 그걸 막는다.
	 */
	virtual void OnUnequipped_Implementation(AActor* Equipper) override;

	// --- 컴포넌트 -----------------------------------------------------------

	/** 무전 기능의 전부. 이 액터는 이걸 들고 다니는 껍데기다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Radio")
	TObjectPtr<URadioComponent> RadioComponent;

private:
	/** 클라의 전원 조작을 서버로 넘긴다. Owner 가 설정돼 있어야 도착한다. */
	UFUNCTION(Server, Reliable)
	void ServerSetPowered(bool bOn);

	/** 서버에서 실제로 전원을 바꾸고 배터리 타이머를 맞춘다. */
	void ApplyPoweredOnServer(bool bOn);

	/** 배터리를 깎는다. 0 이 되면 스스로 꺼진다. 서버 전용. */
	void DrainBattery();

	/** 켜져 있는 동안만 도는 배터리 타이머(서버 전용). */
	FTimerHandle BatteryTimerHandle;

	/**
	 * 이 액터를 로컬 플레이어가 들고 있는가.
	 *
	 * ★ OnEquipped/OnUnequipped 는 멀티캐스트로 **모든 클라에서** 불린다.
	 *   그래서 송신 중단 같은 로컬 조작을 무조건 하면 **남이 무전기를 넣었는데
	 *   내 송신이 끊기는** 버그가 난다. 그걸 막는 검사다.
	 */
	static bool IsLocalPlayerActor(const AActor* Actor);
};
