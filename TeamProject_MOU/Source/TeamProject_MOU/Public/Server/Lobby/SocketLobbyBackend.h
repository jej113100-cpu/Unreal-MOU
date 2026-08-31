// MOU 로비 - 자체 서버(MOU_Server/Server.exe) 백엔드.
//
// [이 파일이 담당하는 층]
//     UServerSubsystem          블루프린트 API, 상태 보관, 델리게이트   (백엔드를 모른다)
//       └─ FSocketLobbyBackend   무엇을 주고받을지 = 패킷 조립        ← 이 파일
//            └─ FServerClientRunnable  바이트를 주고받는 소켓/스레드
//
//   MOU::LoginReqBody 같은 프로토콜 구조체를 직접 다루는 곳은 이 파일과
//   ServerClientRunnable.cpp 뿐이다. 그 위로는 프로토콜이 새어 나가지 않는다.
//   그래서 EOS 백엔드로 갈아끼워도 위쪽 코드가 컴파일 에러조차 나지 않는다.
//
// [보관(Pending) 은 여기 없다]
//   "연결 전에 Login() 을 부르면 보관했다가 연결되는 순간 보낸다" 는 정책은
//   UServerSubsystem 이 갖는다. 백엔드는 시키면 보낼 뿐이다.
//   정책이 백엔드마다 갈리면 EOS 로 바꿨을 때 로그인 타이밍이 달라져
//   UI 가 미묘하게 다르게 동작한다.

#pragma once

#include "CoreMinimal.h"
#include "Server/Net/ChatFraming.h"   // SendEmpty 가 MOU::EOpcode 를 받는다
#include "Server/Lobby/LobbyBackend.h"

class FServerClientRunnable;
class FRunnableThread;

/**
 * 자체 TCP 서버에 붙는 백엔드.
 *
 * 게임 스레드에서만 호출한다 (ILobbyBackend 의 경계 규칙).
 */
class TEAMPROJECT_MOU_API FSocketLobbyBackend final : public ILobbyBackend
{
public:
	FSocketLobbyBackend() = default;
	virtual ~FSocketLobbyBackend() override;

	// --- ILobbyBackend ----------------------------------------------------

	virtual FString GetBackendName() const override { return TEXT("자체 서버(TCP)"); }
	virtual bool    SupportsChat() const override { return true; }

	virtual bool Start(const FString& Host, int32 Port) override;
	virtual void Shutdown() override;
	virtual bool IsRunning() const override { return ServerClient != nullptr; }

	virtual void SendLogin(const FString& LoginId, const FString& Password, int32 TeamId) override;
	virtual void SendRegister(const FString& LoginId, const FString& Password, const FString& Nickname) override;

	virtual void SendChat(EChatChannelBP Channel, const FString& Text) override;
	virtual void SendSetDead(int64 UserId, bool bDead) override;

	virtual void CreateRoom(const FString& Title, const FString& RoomPassword, int32 HostPort,
	                        const FString& LanAddress) override;
	virtual void RequestRoomList() override;
	virtual void JoinRoom(int32 RoomId, const FString& RoomPassword) override;
	virtual void LeaveRoom() override;
	virtual void SetReady(bool bReady) override;
	virtual void StartGame() override;
	virtual void NotifyHostReady() override;
	virtual void RequestHostProbe(int32 Port, uint32 Nonce) override;
	virtual void ReportReachability(bool bReachable) override;
	virtual void UpdateRoomState(int32 RoomId, int32 CurrentPlayers, bool bInGame) override;

	// --- 친구 / 메신저 (v7) ---
	virtual void RequestFriendList() override;
	virtual void AddFriend(const FString& Query) override;
	virtual void RespondFriendRequest(int64 FromUserId, bool bAccept) override;
	virtual void RemoveFriend(int64 TargetUserId) override;
	virtual void SendDirectMessage(int64 TargetUserId, const FString& Text) override;
	virtual void RequestDmHistory(int64 PeerUserId, int64 BeforeMessageId) override;

	virtual bool DequeueEvent(FServerClientEvent& Out) override;
	virtual bool DequeueMessage(FChatMessage& Out) override;

private:
	/** 바디 없는 패킷 하나를 보낸다. RoomListReq / RoomLeaveReq 처럼 인자가 없는 요청용. */
	void SendEmpty(MOU::EOpcode Opcode, const TCHAR* LogLabel);

	/**
	 * 소켓 워커. 게임 스레드에서 생성/파괴하고, 그 사이에는 워커 스레드가 이 객체를 쓴다.
	 *
	 * 스마트 포인터를 쓰지 않고 원시 포인터로 두는 이유:
	 * 러너블의 수명은 "스레드가 끝났는가" 에만 달려 있고, 그 판단은 Shutdown() 하나에서만
	 * 내린다. 참조 카운트로 관리하면 오히려 스레드가 살아있는데 객체가 먼저 사라질 여지가 생긴다.
	 * 반드시 Shutdown() 을 거쳐서만 해제할 것. delete 를 직접 부르지 말 것.
	 */
	FServerClientRunnable* ServerClient = nullptr;
	FRunnableThread*     ServerThread = nullptr;
};
