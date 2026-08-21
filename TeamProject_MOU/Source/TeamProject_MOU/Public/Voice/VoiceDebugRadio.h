// MOU 음성 - 테스트용 무전기 액터.
//
// [★ 이 클래스는 임시다. 아이템 파트가 끝나면 지운다]
//
//   진짜 무전기는 팀원이 작업 중인 `AItemBase` 를 상속한 아이템이고,
//   거기에 `URadioComponent` 를 붙이면 된다(RadioComponent.h 상단 주석).
//
//   그런데 그 아이템이 나오기 전까지 **무전 기능을 전혀 검증할 수 없다** -
//   무전기가 없으면 서버가 무전 송신을 거부하기 때문이다. 그래서 콘솔로
//   띄울 수 있는 최소한의 무전기를 만들어 두었다.
//
//   메시도 상호작용도 없다. 있는 것은 `URadioComponent` 하나뿐이고,
//   그게 정확히 "음성 시스템이 아이템에게 요구하는 전부" 라는 것을 보여준다.
//
// [테스트 방법]
//   MOU.Voice.Radio.Spawn      내 손에 무전기 하나
//   MOU.Voice.Radio.Power 1    전원 ON (이게 Z 키에 대응한다)
//   MOU.Voice.Radio.PTT 1      송신 시작 (이게 X 키에 대응한다)
//   MOU.Voice.Radio.Drop       바닥에 떨군다 - 그 자리에서 계속 소리가 난다
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 7-3절, 7-4절, 14절 V6

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoiceDebugRadio.generated.h"

class APawn;
class URadioComponent;
class USceneComponent;

/**
 * 무전기 최소 구현. **테스트 전용이다.**
 */
UCLASS()
class TEAMPROJECT_MOU_API AVoiceDebugRadio : public AActor
{
	GENERATED_BODY()

public:
	AVoiceDebugRadio();

	/**
	 * 폰의 손에 무전기를 하나 만들어 붙인다. **서버에서만 호출한다.**
	 *
	 * 이미 갖고 있으면 그것을 돌려준다 - 두 대를 들면 소리가 두 번 난다(15절).
	 */
	static AVoiceDebugRadio* SpawnAttachedTo(APawn* OwnerPawn);

	/** 이 폰이 들고 있는 테스트 무전기를 찾는다. 없으면 null. */
	static AVoiceDebugRadio* FindHeldBy(APawn* OwnerPawn);

	/** 부착을 풀어 그 자리에 떨군다. 켜져 있으면 **거기서 계속 소리가 난다.** */
	void DropHere();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Radio")
	TObjectPtr<URadioComponent> RadioComponent;

protected:
	virtual void Tick(float DeltaTime) override;

private:
	/** 위치를 눈으로 확인할 수 있게 하는 최소한의 루트. 메시는 없다. */
	UPROPERTY(VisibleAnywhere, Category = "Radio")
	TObjectPtr<USceneComponent> SceneRoot;
};
