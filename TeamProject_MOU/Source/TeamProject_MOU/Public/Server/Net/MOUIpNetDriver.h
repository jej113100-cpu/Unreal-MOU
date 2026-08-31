// MOU 넷드라이버 - 클라이언트 바인드 포트와 relay 실제 소켓 등록을 위한 최소 상속. (v11)
//
// [왜 필요한가]
//   UDP 홀펀칭은 "방장이 참여자의 **정확한 공인 IP:포트** 로 먼저 한 발 쏜다" 로
//   성립한다. 참여자 쪽 공유기가 port-restricted cone 이라, 포트가 하나만 달라도
//   전부 막힌다(실측: 같은 포트 답장은 즉시 통과, 다른 포트는 10발 중 0발).
//
//   그런데 UE 의 기본 동작은 클라이언트가 **임시 포트**로 붙는 것이다 —
//   UIpNetDriver::GetClientPort() 가 0 을 돌려주고, 소켓은 0.0.0.0:0 에 bind 된다.
//   그 번호는 소켓을 열기 전까지 아무도 모르므로 punch 대상을 정할 수 없다.
//
//   GetClientPort() 는 virtual 이다. 그것 하나만 갈아끼우면 나머지 엔진 동작은
//   그대로 두고 포트만 예측 가능하게 만들 수 있다.
//
// [왜 전역 값을 쓰는가]
//   넷드라이버는 엔진이 만든다. 우리가 생성 시점에 값을 넣어줄 자리가 없고,
//   ClientTravel 이 시작되는 시점에는 이미 UServerSubsystem 이 포트를 확보해 둔 뒤다.
//   그래서 "확보한 쪽이 적어두고, 만들어진 쪽이 읽는다" 로 잇는다.
//
// [설정]
//   DefaultEngine.ini 의 GameNetDriver 를 이 클래스로 바꿔야 적용된다.
//   안 바꾸면 엔진 기본 드라이버가 쓰이고, 포트는 예전처럼 임시 포트가 된다
//   (= 홀펀칭만 동작하지 않고 나머지는 그대로).
#pragma once

#include "CoreMinimal.h"
#include "IpNetDriver.h"
#include "ChatProtocol.h"
#include <atomic>
#include "MOUIpNetDriver.generated.h"

/** 실제 UE 소켓이 bind 된 직후 relay 등록에 쓸 비공개 capability 묶음. */
struct FMOUPendingRelayRegistration
{
	FString Address;
	int32   Port = 0;
	uint64  RouteId = 0;
	TArray<uint8> Token;

	bool IsPresent() const
	{
		return !Address.IsEmpty() && Port > 0 && RouteId != 0 &&
			Token.Num() == static_cast<int32>(MOU::kRelayTokenBytes);
	}
};

UCLASS(Transient, Config = Engine)
class TEAMPROJECT_MOU_API UMOUIpNetDriver : public UIpNetDriver
{
	GENERATED_BODY()

public:
	/**
	 * 클라이언트가 bind 할 포트. 0 이면 엔진 기본(임시 포트)과 같다.
	 *
	 * UServerSubsystem 이 게임 포트를 실제로 확보한 뒤 그 번호를 여기에 적는다.
	 * 확보에 실패했으면 0 으로 두어 예전 동작으로 떨어진다 — 홀펀칭만 못 하고
	 * 나머지는 그대로 도는 편이, 접속 자체를 막는 것보다 낫다.
	 */
	static void SetDesiredClientPort(int32 Port);
	static int32 GetDesiredClientPort();

	/** 방장이 실제 listen socket 을 bind 할 포트. 0이면 엔진의 기본 선택을 쓴다. */
	static void SetDesiredListenPort(int32 Port);

	/** InitListen 성공 뒤 실제 listen socket 에서 보낼 host-facing relay 등록들. */
	static void SetPendingHostRelayRegistrations(const TArray<FMOUPendingRelayRegistration>& Registrations);

	/** InitBase(클라이언트) 성공 뒤 실제 client socket 에서 보낼 guest-facing relay 등록. */
	static void SetPendingClientRelayRegistration(const FMOUPendingRelayRegistration& Registration);

	/** 방을 나가거나 연결을 끊을 때 아직 소비되지 않은 capability 를 지운다. */
	static void ClearPendingRelayRegistrations();

	//~ UIpNetDriver
	virtual int GetClientPort() override;
	virtual bool InitBase(bool bInitAsClient, FNetworkNotify* InNotify, const FURL& URL,
		bool bReuseAddressAndPort, FString& Error) override;
	virtual bool InitListen(FNetworkNotify* InNotify, FURL& ListenURL,
		bool bReuseAddressAndPort, FString& Error) override;
	//~ End UIpNetDriver

private:
	/**
	 * 여러 스레드가 만지지는 않지만(둘 다 게임 스레드), 넷드라이버 생성 시점과
	 * 설정 시점이 멀어서 atomic 으로 두어 의도를 분명히 한다.
	 */
	static std::atomic<int32> DesiredClientPort;
	static std::atomic<int32> DesiredListenPort;
	static FCriticalSection RelayRegistrationMutex;
	static TArray<FMOUPendingRelayRegistration> PendingHostRelayRegistrations;
	static FMOUPendingRelayRegistration PendingClientRelayRegistration;
};
