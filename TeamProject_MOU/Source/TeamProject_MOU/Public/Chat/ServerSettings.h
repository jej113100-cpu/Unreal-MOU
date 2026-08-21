// MOU 채팅/로그인 서버 주소 설정.
//
// [왜 이 파일이 생겼나]
//   서버(MOU_Server)는 팀에서 **한 대의 PC 에서만** 돌린다. 나머지 팀원은
//   PIE 1인 실행으로 그 PC 에 붙어야 한다.
//
//   그런데 예전에는 접속 주소가 여러 파일에 127.0.0.1 로 박혀 있었다.
//   127.0.0.1 은 "지금 이 코드가 돌고 있는 바로 그 컴퓨터" 라는 뜻이라,
//   서버를 켠 사람은 자기 서버에 붙어 잘 되지만 서버를 켜지 않은 팀원은
//   **자기 자신에게** 접속을 시도하다 100% 실패한다. 코드는 똑같은데
//   사람마다 다른 곳을 가리키는, 눈에 안 보이는 버그였다.
//
// [해결 방식]
//   주소를 이 클래스 한 군데로 모으고, 실제 값은 Config/DefaultGame.ini 에 둔다.
//   DefaultGame.ini 는 git 으로 공유되므로 팀 전원이 같은 주소를 자동으로 받는다.
//
//   중요한 점: 값은 127.0.0.1 이 아니라 **서버 PC 의 LAN IP**(예: 192.168.0.32) 다.
//   서버를 켠 사람도 자기 LAN IP 로 자기 자신에게 접속할 수 있으므로,
//   "서버 켠 사람 / 안 켠 사람" 을 코드가 구분할 필요가 없다. 값 하나로 전원이 동작한다.
//
// [우선순위] 위가 이긴다.
//   1. 실행 인자
//        -MOUServer=192.168.0.32:9000
//        -MOUChatHost=192.168.0.32  -MOUChatPort=9000
//      개인 PC 에서 임시로 다른 서버를 볼 때 쓴다. 아무 파일도 건드리지 않는다.
//   2. 개인 설정  Saved/Config/WindowsEditor/Game.ini
//      Saved/ 는 .gitignore 대상이라 남의 환경에 영향을 주지 않는다.
//      런타임에 만들려면 콘솔에서  MOU.Chat.SetServer <주소> [포트]
//   3. 팀 공유 설정  Config/DefaultGame.ini   ← 평소에는 이것만 쓰면 된다
//        [/Script/TeamProject_MOU.MOUServerSettings]
//        ServerHost=192.168.0.32
//        ServerPort=9000
//   4. 127.0.0.1:9000 — ini 가 통째로 없을 때만 오는 최후의 값
//
// [에디터에서 바꾸기]
//   Project Settings -> Game -> MOU Server
//   여기서 저장하면 Config/DefaultGame.ini 에 쓰이므로 그대로 커밋하면 팀에 퍼진다.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ServerSettings.generated.h"

/**
 * 채팅/로그인 서버의 접속 대상. 코드 어디에서도 주소를 직접 적지 않고
 * 반드시 ResolveEndpoint() 를 거치게 하는 것이 이 클래스의 목적이다.
 */
UCLASS(config = Game, defaultconfig, BlueprintType, meta = (DisplayName = "MOU Server"))
class TEAMPROJECT_MOU_API UMOUServerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMOUServerSettings();

	/** Project Settings 의 Game 카테고리에 표시한다. */
	virtual FName GetCategoryName() const override;

	/**
	 * 서버 PC 의 주소. LAN 이면 192.168.x.x 같은 사설 IP, 외부면 공인 IP/도메인.
	 *
	 * ★ 127.0.0.1 / localhost 를 넣지 말 것. 그러면 다시 사람마다 자기 PC 를
	 *   가리키게 되어, 서버를 켜지 않은 팀원은 전부 접속에 실패한다.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "MOU|Chat")
	FString ServerHost;

	/** 서버를 띄울 때 준 포트와 같아야 한다. (Server.exe <port>) */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "MOU|Chat", meta = (ClampMin = "1", ClampMax = "65535"))
	int32 ServerPort;

	/**
	 * 실제로 접속할 주소를 결정한다. 위 우선순위를 그대로 구현한 유일한 함수다.
	 *
	 * @param OutSource nullptr 이 아니면 어느 단계에서 값이 왔는지 사람이 읽을 수 있는
	 *                  문자열로 채운다. "왜 저기에 붙었지?" 를 로그로 추적하기 위한 것.
	 */
	static void ResolveEndpoint(FString& OutHost, int32& OutPort, FString* OutSource = nullptr);

	/** 블루프린트/위젯용 단축. 내부는 ResolveEndpoint 와 같다. */
	UFUNCTION(BlueprintPure, Category = "MOU|Chat")
	static FString GetResolvedServerHost();

	UFUNCTION(BlueprintPure, Category = "MOU|Chat")
	static int32 GetResolvedServerPort();

	/** "192.168.0.32:9000 (팀 공유 설정)" 처럼 화면/로그에 그대로 쓸 수 있는 문구. */
	UFUNCTION(BlueprintPure, Category = "MOU|Chat")
	static FString GetResolvedEndpointText();

	/**
	 * 주소를 **이 PC 에만** 저장한다 (Saved/Config/<플랫폼>/Game.ini).
	 * Saved/ 는 git 에 올라가지 않으므로 팀원 설정을 건드리지 않는다.
	 * 팀 전체 기본값을 바꾸려면 Config/DefaultGame.ini 를 고쳐 커밋해야 한다.
	 */
	static void SaveEndpointOverrideForThisMachine(const FString& InHost, int32 InPort);

	/** SaveEndpointOverrideForThisMachine 으로 저장한 개인 설정을 지운다. */
	static void ClearEndpointOverrideForThisMachine();
};
