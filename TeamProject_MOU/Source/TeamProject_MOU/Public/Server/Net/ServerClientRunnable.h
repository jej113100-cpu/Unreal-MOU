// MOU 서버 - Server.exe 와 통신하는 전용 워커 스레드.
//
// [이 클래스가 존재하는 이유]
//   TCP 의 Connect / Recv 는 블로킹 호출이다. 게임 스레드에서 부르면
//   서버가 꺼져 있을 때 에디터가 몇 초씩 멈춘다. 그래서 소켓 작업 전부를
//   별도 스레드로 뺀다.
//
// [스레드 경계 - 코드 리뷰 시 가장 중요한 부분]
//
//     게임 스레드                          워커 스레드 (이 클래스)
//   ┌─────────────────┐               ┌──────────────────────────┐
//   │ UServerSubsystem  │               │  FServerClientRunnable     │
//   │                 │               │                          │
//   │ SendChat()      │──OutboundQ──▶ │  PumpSend() -> Socket    │
//   │                 │               │                          │
//   │ Tick()          │◀──InboundQ──  │  PumpRecv() <- Socket    │
//   │  -> 델리게이트   │◀──EventQ────  │  (파싱 후 구조체로)        │
//   └─────────────────┘               └──────────────────────────┘
//
//   >> 이 클래스 안에서는 UObject / UMG 위젯을 절대 건드리지 않는다. <<
//   언리얼의 UObject 는 스레드 세이프하지 않다. 워커 스레드에서 위젯 함수를 부르거나
//   델리게이트를 브로드캐스트하면 재현이 어려운 랜덤 크래시가 난다.
//   그래서 여기서는 순수 데이터(FChatMessage, FServerClientEvent)만 만들어 큐에 넣고,
//   게임 스레드의 UServerSubsystem::Tick 이 꺼내서 그때 델리게이트를 쏜다.
//
// [대응하는 서버 코드]
//   MOU_Server/Server/Server.cpp 의 ClientThread() 와 대칭이다.
//   서버는 클라이언트 1명당 스레드 1개, 클라이언트는 서버 연결 1개당 스레드 1개.

#pragma once

#include "CoreMinimal.h"
#include "Server/Net/ChatFraming.h"
#include "Server/Chat/ChatTypes.h"
#include "Containers/Queue.h"
#include "HAL/Runnable.h"

#include "Server/Lobby/LobbyTypes.h"

// EServerClientEventType / FServerClientEvent 는 여기 있었지만 LobbyBackend.h 로 옮겼다.
// 그 둘은 "소켓 워커가 알리는 사건" 이 아니라 "백엔드가 게임 스레드에 알리는 사건" 이고,
// EOS 백엔드도 같은 그릇에 결과를 담아야 하기 때문이다.
#include "Server/Lobby/LobbyBackend.h"

#include <atomic>

class FSocket;
class ISocketSubsystem;

/**
 * 서버(Server.exe)와 붙어있는 TCP 클라이언트 스레드.
 *
 * [이 클래스는 백엔드가 아니다]
 *   ILobbyBackend 를 구현하는 것은 FSocketLobbyBackend 이고, 이 클래스는 그 안에서
 *   소켓과 스레드만 담당한다. 나누어 둔 이유는 역할이 다르기 때문이다 —
 *   여기는 "바이트를 주고받는 곳", 백엔드는 "무엇을 주고받을지 정하는 곳" 이다.
 *
 * 소유자는 FSocketLobbyBackend 하나뿐이다. 직접 생성하지 않는다.
 */
class TEAMPROJECT_MOU_API FServerClientRunnable : public FRunnable
{
public:
	FServerClientRunnable(const FString& InHost, int32 InPort);
	virtual ~FServerClientRunnable();

	// --- FRunnable 인터페이스 ---------------------------------------------

	/**
	 * 주의: Init() 은 새 스레드에서 실행되지만, 이 함수가 끝날 때까지
	 * FRunnableThread::Create() 를 호출한 게임 스레드가 대기한다.
	 * 따라서 여기서 Connect() 같은 블로킹 작업을 하면 게임 스레드가 같이 멈춘다.
	 * 실제 접속은 반드시 Run() 안에서 한다.
	 */
	virtual bool   Init() override;
	virtual uint32 Run() override;
	virtual void   Stop() override;
	virtual void   Exit() override;

	// --- 게임 스레드에서 호출하는 API --------------------------------------

	/**
	 * 조립이 끝난 패킷을 전송 큐에 넣는다. 실제 send 는 워커 스레드가 한다.
	 * 큐가 SPSC(단일 생산자/단일 소비자) 모드이므로 반드시 게임 스레드에서만 호출해야 한다.
	 */
	void EnqueuePacket(TArray<uint8>&& Packet);

	/** 수신된 채팅 한 줄을 꺼낸다. 게임 스레드 전용. 더 없으면 false. */
	bool DequeueMessage(FChatMessage& Out) { return InboundMessages.Dequeue(Out); }

	/** 연결 상태 변화를 꺼낸다. 게임 스레드 전용. 더 없으면 false. */
	bool DequeueEvent(FServerClientEvent& Out) { return InboundEvents.Dequeue(Out); }

private:
	// --- 워커 스레드 내부 -------------------------------------------------

	/** 소켓 생성 + 주소 해석 + Connect 를 한 번 시도한다. */
	bool ConnectOnce(FString& OutError);

	/** 전송 큐를 비운다. 연결이 끊기면 false. */
	bool PumpSend();

	/** 최대 WaitMilliseconds 만큼 대기하며 수신한다. 연결이 끊기면 false. */
	bool PumpRecv();

	/** 완성된 패킷 하나를 처리한다. 여기서 FChatMessage / FServerClientEvent 로 변환된다. */
	void HandlePacket(const MOU::PacketHeader& Header, const TArray<uint8>& Body);

	void PushEvent(EServerClientEventType Type, const FString& Detail = FString());

	void DestroySocketIfNeeded();

	// --- 접속 정보 (생성 후 불변) ------------------------------------------
	FString Host;
	int32   Port = 0;

	// --- 워커 스레드 전용 상태 (게임 스레드에서 접근 금지) ------------------
	ISocketSubsystem* SocketSubsystem = nullptr;
	FSocket*          Socket          = nullptr;

	/** 수신 누적 버퍼. 서버의 ClientSession::RecvBuf 와 같은 역할이다. */
	TArray<uint8> RecvBuffer;

	/** TryExtractPacket 이 바디를 담아주는 임시 버퍼. 매번 새로 만들지 않으려고 멤버로 둔다. */
	TArray<uint8> BodyScratch;

	// --- 스레드 간 공유 ----------------------------------------------------

	/**
	 * Stop() 이 세우는 종료 요청 플래그. 워커 루프가 매 반복마다 확인한다.
	 * 게임 스레드가 쓰고 워커 스레드가 읽으므로 반드시 원자적이어야 한다.
	 */
	std::atomic<bool> bStopRequested{ false };

	/** 게임 스레드 -> 워커. 전송 대기 패킷 */
	TQueue<TArray<uint8>, EQueueMode::Spsc> OutboundPackets;

	/** 워커 -> 게임 스레드. 수신한 채팅 */
	TQueue<FChatMessage, EQueueMode::Spsc> InboundMessages;

	/** 워커 -> 게임 스레드. 연결 상태 변화 */
	TQueue<FServerClientEvent, EQueueMode::Spsc> InboundEvents;

	// --- 튜닝 값 -----------------------------------------------------------

	/**
	 * Recv 대기 시간. 이 값이 곧 Stop() 요청에 대한 응답 지연이자,
	 * 전송 큐에 넣은 패킷이 실제로 나가기까지의 최대 지연이기도 하다.
	 * 짧게 잡으면 반응이 빠르지만 스레드가 자주 깨어난다. 채팅에는 50ms 면 충분하다.
	 */
	static constexpr int32 WaitMilliseconds = 50;

	/** 접속 실패 후 재시도까지의 간격. 서버가 꺼져 있어도 로그가 폭주하지 않게 한다. */
	static constexpr int32 ReconnectDelayMilliseconds = 3000;
};
