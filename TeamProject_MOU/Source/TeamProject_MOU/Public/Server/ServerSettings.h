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
#include "Server/Lobby/LobbyTypes.h"   // EMOULobbyBackendType
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
	 * 계정 인증과 세션 탐색을 무엇으로 할 것인가.
	 *
	 * 이 값 하나만 바꾸면 백엔드가 통째로 갈린다. 위젯도 게임 로직도 손대지 않는다 —
	 * 그렇게 만들어두려고 ILobbyBackend 가 있다.
	 *
	 * 실행 인자로 이번 실행만 바꿀 수도 있다:  -MOULobbyBackend=EOS
	 *
	 * >> 어느 쪽을 고르든 이동/전투/GAS 는 호스트의 리슨서버가 처리한다.
	 *    여기서 정하는 것은 "방이 열리기 전" 과 "계정" 뿐이다. <<
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "MOU|Lobby")
	EMOULobbyBackendType LobbyBackend;

	/**
	 * 게임 시작 후 방장의 리슨서버가 열리기를 기다리는 최대 시간(초).
	 *
	 * [이 값이 무엇을 재는가]
	 *   방장이 "게임 시작" 을 누르면 서버가 전원에게 RoomStart 를 보낸다. 방장은 그때부터
	 *   맵을 열기 시작하고, 다 열리면 서버에 "준비됐다" 를 보낸다. 참여자는 그 신호를 받고서야
	 *   떠난다. 이 값은 그 "다 열리면" 을 얼마나 기다려줄지다.
	 *
	 * [넘기면 어떻게 되나]
	 *   준비 신호를 보내지 않는다. 참여자는 대기실에 남는다.
	 *   열리지도 않은 주소로 보내 튕기게 만드는 것보다 낫다고 보기 때문이다.
	 *   방장 로그에 사유가 남고, 방장이 방을 나가면 참여자도 메인메뉴로 돌아간다.
	 *
	 * 맵이 크거나 저사양 PC 가 섞여 있으면 늘린다.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "MOU|Lobby", meta = (ClampMin = "1.0", ClampMax = "600.0"))
	float HostReadyTimeoutSeconds;

	/**
	 * 방을 만들 때 공유기에 UPnP 로 포트를 열어달라고 시도할지. (2026-08-28)
	 *
	 * [기본이 true 인 이유 — 끄면 다른 네트워크 방장이 죽는다]
	 *   방장이 되려면 **그 PC 가 있는 네트워크의 공유기**에
	 *     외부 UDP 7777 -> 그 PC 의 LAN IP : 7777
	 *   이 열려 있어야 한다. 팀원 전원이 자기 공유기에 이걸 손으로 넣기는 어렵고,
	 *   UPnP 는 그 일을 게임이 대신 해주는 수단이다.
	 *
	 *   한 번 false 로 바꿨다가("수동 포워딩으로 간다") 다른 네트워크 방장의
	 *   7777 을 아무도 열어주지 않는 상태가 되어 참여자가 전부 못 들어왔다.
	 *   수동 포워딩은 UPnP 의 **대안이 아니라 보완**이다 — 서버 PC 처럼 고정된
	 *   한 대는 수동으로 넣어두고, 나머지는 UPnP 에 맡긴다.
	 *
	 * [비용]
	 *   방 만들기 창이 열릴 때 SSDP 탐색을 한다. 공유기가 답하면 100ms 안쪽,
	 *   UPnP 가 꺼진 공유기면 최대 3초다. 그 3초가 아까워서 끄면 위의 사고가 난다.
	 *
	 * [끄는 것이 맞는 경우]
	 *   방장을 할 PC 전부에 7777/UDP 수동 포워딩이 이미 들어가 있을 때.
	 *   그때는 UPnP 시도가 순수한 낭비다.
	 *
	 * [★ 로그인 서버 쪽 포워딩과 헷갈리지 말 것]
	 *   서버(9000/TCP) 쪽 공유기에 넣은 포워딩은 이 경로와 아무 상관이 없다.
	 *   게임 트래픽은 서버를 거치지 않고 참여자가 방장에게 직접 붙기 때문이다.
	 *
	 * 실행 인자로 이번 실행만 끌 수 있다:  -MOUUseUpnp=0
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "MOU|Lobby")
	bool bUseUpnpPortMapping;

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
	 * 실제로 쓸 백엔드 종류를 결정한다. 주소와 같은 우선순위를 따른다
	 * (실행 인자 -MOULobbyBackend= 가 설정 파일을 이긴다).
	 *
	 * @param OutSource nullptr 이 아니면 어느 단계에서 값이 왔는지 채운다.
	 */
	static EMOULobbyBackendType ResolveBackendType(FString* OutSource = nullptr);

	/** 리슨서버 준비를 기다리는 상한. 설정값이 깨져 있어도 안전한 값을 돌려준다. */
	static float GetHostReadyTimeoutSeconds();

	/**
	 * UPnP 로 포트를 열 것인가. 실행 인자 -MOUUseUpnp=0|1 이 설정 파일을 이긴다.
	 * 다른 값과 우선순위 규칙을 같게 두려고 여기 모아둔다.
	 */
	static bool ShouldUseUpnp();

	/**
	 * 주소를 **이 PC 에만** 저장한다 (Saved/Config/<플랫폼>/Game.ini).
	 * Saved/ 는 git 에 올라가지 않으므로 팀원 설정을 건드리지 않는다.
	 * 팀 전체 기본값을 바꾸려면 Config/DefaultGame.ini 를 고쳐 커밋해야 한다.
	 */
	static void SaveEndpointOverrideForThisMachine(const FString& InHost, int32 InPort);

	/** SaveEndpointOverrideForThisMachine 으로 저장한 개인 설정을 지운다. */
	static void ClearEndpointOverrideForThisMachine();
};
