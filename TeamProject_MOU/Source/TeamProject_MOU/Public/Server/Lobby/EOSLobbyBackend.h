// MOU 로비 - Epic Online Services 백엔드 (뼈대).
//
// [지금 상태: 아직 붙지 않았다]
//   이 파일은 동작하는 EOS 연동이 아니다. Start() 를 부르면 "미구현" 사유와 함께
//   ConnectFailed 를 돌려준다. 그런데도 저장소에 들어있는 이유는 두 가지다.
//
//     1. 전환 비용을 지금 확정해두기 위해서.
//        EOS 로 간다는 결정이 프로젝트 전체를 뒤집는 일인지, 파일 하나를 채우는
//        일인지는 이 클래스가 컴파일되는 순간 판가름난다. 지금은 후자다.
//     2. 무엇을 무엇으로 바꿔야 하는지 적어두기 위해서.
//        아래 각 함수 주석에 대응하는 EOS API 를 적어뒀다.
//
// [왜 EOS 인가 — 자체 서버로 못 하는 것]
//   자체 서버가 해결하지 못하는 문제는 방 목록이 아니라 **NAT** 다.
//   지금 참가자는 서버가 알려준 호스트의 공인 IP:7777 로 직접 붙는데,
//   호스트가 포트포워딩을 하지 않으면 공유기가 그 접속을 막는다.
//   EOS 는 P2P 홀펀칭과 릴레이를 SDK 안에서 처리하므로 이 문제가 사라진다.
//   덤으로 안티치트와 보이스도 같은 SDK 안에 있다.
//
// [붙이는 순서 — 실제 작업 계획]
//   1. Epic Games Dev Portal 에서 Product 를 만들고 ProductId/SandboxId/ClientId 발급
//   2. 플러그인 켜기: OnlineSubsystemEOS, EOSShared, SocketSubsystemEOS
//   3. DefaultEngine.ini 에 [/Script/OnlineSubsystemEOS.EOSSettings] 블록 작성
//   4. Build.cs 의 PrivateDependencyModuleNames 에
//      "OnlineSubsystem", "OnlineSubsystemUtils", "OnlineSubsystemEOS" 추가
//   5. 이 파일의 각 함수를 IOnlineSessionPtr / IOnlineIdentityPtr 호출로 채운다
//   6. Project Settings -> Game -> MOU Server -> Lobby Backend 를 EOS 로 바꾼다
//      (코드 수정 없이 여기까지 오는 것이 이 인터페이스의 목적이다)
//
// [EOS 로 가도 자체 서버가 남는 것]
//   - 사망자 채널: "죽은 사람에게만 보이는 채팅" 은 게임 상태를 아는 쪽만 판정할 수 있다.
//                  EOS 는 누가 죽었는지 모른다.
//   - 채팅 로그:   호스트가 나가도 남아야 하므로 영속 저장소가 필요하다.
//   - 커스터마이징: 계정에 묶인 게임 데이터. EOS Player Data Storage 는 용도가 다르다.
//   그래서 현실적인 최종 구성은 "EOS = 계정/세션, 자체 서버 = 채팅/게임 데이터" 다.
//   EOS Connect 의 ProductUserId 를 accounts 테이블의 외부 키로 저장하면 둘이 이어진다.

#pragma once

#include "CoreMinimal.h"
#include "Server/Lobby/LobbyBackend.h"
#include "Containers/Queue.h"

/**
 * EOS 백엔드. 아직 뼈대다 — 모든 요청은 조용히 무시되고, Start() 가 실패를 알린다.
 *
 * 실패를 조용히 삼키지 않고 ConnectFailed 로 올리는 이유:
 * 설정을 EOS 로 바꿔놓고 왜 아무 일도 안 일어나는지 헤매는 것보다,
 * 화면에 "EOS 백엔드는 아직 구현되지 않았다" 가 뜨는 편이 낫다.
 */
class TEAMPROJECT_MOU_API FEOSLobbyBackend final : public ILobbyBackend
{
public:
	virtual FString GetBackendName() const override { return TEXT("EOS"); }

	/**
	 * EOS Lobby 에도 방 채팅은 있지만, 사망자/팀 채널 판정은 게임 상태를 아는 쪽만
	 * 할 수 있다. 그래서 채팅은 EOS 로 옮기지 않는다.
	 */
	virtual bool SupportsChat() const override { return false; }

	virtual bool Start(const FString& Host, int32 Port) override;
	virtual void Shutdown() override;
	virtual bool IsRunning() const override { return bStarted; }

	// --- 계정 ---
	// EOS_Connect_Login / EOS_Auth_Login 으로 바뀐다.
	// 아이디/비밀번호를 우리가 받지 않게 되는 것이 가장 큰 변화다 —
	// 인증은 Epic 계정이나 디바이스 ID 가 대신하고, 우리는 ProductUserId 만 받는다.
	virtual void SendLogin(const FString& LoginId, const FString& Password, int32 TeamId) override;
	virtual void SendRegister(const FString& LoginId, const FString& Password, const FString& Nickname) override;

	// --- 채팅 ---
	// 구현하지 않는다. SupportsChat() 이 false 이므로 UServerSubsystem 이 먼저 막는다.
	virtual void SendChat(EChatChannelBP Channel, const FString& Text) override;
	virtual void SendSetDead(int64 UserId, bool bDead) override;

	// --- 로비 ---
	// IOnlineSessionPtr 로 바뀐다.
	//   CreateRoom      -> CreateSession    (Title/비번은 FOnlineSessionSetting 으로)
	//   RequestRoomList -> FindSessions     (결과를 FMOURoomInfo 로 변환)
	//   JoinRoom        -> JoinSession      (주소는 GetResolvedConnectString 이 준다)
	//   LeaveRoom       -> DestroySession / UnregisterPlayer
	//   SetReady        -> UpdateSession 의 멤버 세팅
	//   StartGame       -> StartSession
	//   NotifyHostReady -> UpdateSession 으로 "HostReady" 속성을 켠다.
	//                      참여자는 OnSessionSettingsUpdated 에서 그 변화를 보고 떠난다.
	//                      즉 v6 의 호스트 준비 신호는 EOS 에서도 그대로 필요하다 —
	//                      리슨서버가 언제 열리는지는 어느 백엔드도 대신 알아줄 수 없다.
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
	//
	// ★ 전부 빈 구현이다. EOS 에는 Friends 인터페이스(EOS_Friends_*)와
	//   Presence 인터페이스(EOS_Presence_*)가 이미 있으므로 나중에 그것으로
	//   채우면 된다. 다만 **1:1 메시지 보관은 EOS 가 제공하지 않으므로**
	//   그때도 DM 만은 자체 서버가 필요하다 — 붙일 때 이 점을 먼저 확인할 것.
	virtual void RequestFriendList() override;
	virtual void AddFriend(const FString& Query) override;
	virtual void RespondFriendRequest(int64 FromUserId, bool bAccept) override;
	virtual void RemoveFriend(int64 TargetUserId) override;
	virtual void SendDirectMessage(int64 TargetUserId, const FString& Text) override;
	virtual void RequestDmHistory(int64 PeerUserId, int64 BeforeMessageId) override;

	virtual bool DequeueEvent(FServerClientEvent& Out) override;
	virtual bool DequeueMessage(FChatMessage& Out) override;

private:
	/**
	 * 아직 안 되는 기능을 불렀다고 로그에 남긴다.
	 *
	 * 사건 큐에 실패를 넣지는 않는다. 요청마다 실패 팝업을 띄우면 화면이 뒤덮이고,
	 * 진짜 원인(백엔드가 시작조차 안 됐다)은 이미 Start() 가 알렸기 때문이다.
	 */
	void LogNotImplemented(const TCHAR* What) const;

	/**
	 * 게임 스레드로 올릴 사건들.
	 *
	 * 실제 구현에서는 EOS SDK 콜백이 여기에 넣는다. SDK 콜백이 게임 스레드에서 온다는
	 * 보장이 없으므로, 콜백 안에서 델리게이트를 쏘지 말고 반드시 이 큐를 거쳐야 한다.
	 *
	 * SPSC 가 아니라 Mpsc 인 이유: 소켓 백엔드는 생산자가 워커 스레드 하나뿐이지만,
	 * SDK 콜백은 어느 스레드에서 올지 우리가 통제하지 못한다.
	 */
	TQueue<FServerClientEvent, EQueueMode::Mpsc> InboundEvents;

	bool bStarted = false;
};
