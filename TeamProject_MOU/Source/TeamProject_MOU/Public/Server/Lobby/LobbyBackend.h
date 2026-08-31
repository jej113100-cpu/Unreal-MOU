// MOU 로비 - 계정/세션 탐색 백엔드 인터페이스.
//
// [왜 이 인터페이스가 필요한가]
//   리슨서버에는 두 가지 한계가 있고, 둘 다 게임 로직과는 상관이 없다.
//
//     1. 자기 존재를 알릴 방법이 없다.
//        방이 열리기 전에는 RPC 를 주고받을 상대(연결된 세션) 자체가 없으므로,
//        "누가 방을 열었는가" 는 리슨서버 밖 어딘가에 적혀 있어야 한다.
//     2. 계정이 영속하지 않는다.
//        호스트가 나가면 프로세스가 통째로 사라진다. 같은 계정으로 다시 들어왔을 때
//        UserId 와 커스터마이징이 유지되려면 리슨서버가 죽어도 살아있는 저장소가 필요하다.
//
//   이 두 가지를 채우는 방법은 여러 개이고, 어느 것을 쓰든 게임 트래픽(이동/전투/GAS)은
//   전혀 지나가지 않는다. 참가자는 주소를 받는 순간 호스트의 리슨서버에 직접 붙는다.
//
//     FSocketLobbyBackend  MOU_Server(Server.exe) 에 붙는 자체 TCP 구현.  현재 기본값
//     FEOSLobbyBackend     Epic Online Services 의 Lobby/Session/Connect.  전환 대상
//
//   Steam 을 쓴다면 FSteamLobbyBackend 를 같은 자리에 하나 더 만들면 된다.
//
// [교체 시 무엇이 바뀌고 무엇이 안 바뀌는가]
//   안 바뀜: UServerSubsystem 의 블루프린트 API, 모든 UMG 위젯, 게임 로직 전부.
//            위젯은 UServerSubsystem 의 델리게이트만 보고 있어서 백엔드를 모른다.
//   바뀜:    이 인터페이스의 구현체 하나. 그리고 그것을 고르는 설정값 하나.
//
//   >> 그래서 "EOS 로 간다" 가 프로젝트 전체를 뒤집는 일이 아니라 파일 하나를 더 쓰는
//      일이 되도록 여기서 선을 그어둔다. <<
//
// [경계 규칙]
//   - 이 인터페이스의 함수는 전부 **게임 스레드에서만** 호출한다.
//     FSocketLobbyBackend 의 송신 큐가 SPSC(단일 생산자) 모드라서, 다른 스레드에서
//     부르면 큐가 조용히 깨진다.
//   - 결과는 절대 콜백으로 돌려주지 않는다. FServerClientEvent 로 만들어 큐에 넣고,
//     UServerSubsystem::Tick 이 게임 스레드에서 꺼내 델리게이트를 쏜다.
//     구현체가 워커 스레드를 쓰든(소켓) SDK 콜백을 쓰든(EOS) UObject 를 만지는 지점은
//     Tick 한 곳뿐이어야 한다.
//   - 여기에는 UObject 가 없다. 인터페이스가 UINTERFACE 가 아닌 순수 C++ 인 이유는
//     구현체가 워커 스레드를 소유하고 GC 와 무관한 수명을 갖기 때문이다.

#pragma once

#include "CoreMinimal.h"
#include "Server/Chat/ChatTypes.h"
#include "Server/Lobby/LobbyTypes.h"
#include "Server/Social/FriendTypes.h"

/**
 * 백엔드가 게임 스레드에 알리는 사건의 종류.
 *
 * [이름이 프로토콜 옵코드와 같은 이유]
 *   자체 서버 프로토콜이 먼저 있었고 그 이름을 그대로 물려받았다.
 *   EOS 백엔드에서는 이 값들이 옵코드가 아니라 "SDK 콜백을 우리 말로 번역한 결과" 다.
 *   예: EOS_Lobby_CreateLobby 의 완료 콜백 -> RoomCreateAck
 */
enum class EServerClientEventType : uint8
{
	/** 접속 시도를 시작했다. */
	Connecting,
	/** 백엔드가 준비됐다. 아직 로그인 전이라 대부분의 기능은 막혀 있다. */
	Connected,
	/** 접속/초기화 실패. 잠시 후 자동으로 재시도한다. Detail 에 사유가 들어있다. */
	ConnectFailed,
	/** 로그인 결과. Login 필드에 확정된 신원이 들어있다. */
	LoginAck,
	/** 계정 생성 결과. Login.bSuccess / Login.Result 에 결과가 들어있다. */
	RegisterAck,
	/** 방 생성 결과. RoomId 와 RoomResult 가 유효하다. */
	RoomCreateAck,
	/** 방 목록 도착. Rooms 배열이 유효하다. */
	RoomListAck,
	/** 방 참여 결과. Join 이 유효하다. */
	RoomJoinAck,
	/** 대기실 명단 갱신. Members / RoomId / bAllReady 가 유효하다. */
	RoomMemberList,
	/** 방이 사라졌다. RoomId 와 CloseReason 이 유효하다. 대기실을 닫아야 한다. */
	RoomClosed,
	/**
	 * 게임이 시작됐다. RoomId 와 Join(호스트 주소)이 유효하다.
	 *
	 * [주의] 이것은 "떠나라" 가 아니다. 호스트는 이 신호를 받고 리슨서버를 열기 시작하고,
	 * 참여자는 RoomHostReady 가 올 때까지 기다린다.
	 */
	RoomStart,
	/**
	 * 호스트의 리슨서버가 실제로 열렸다. Join 에 붙을 주소가 들어있다.
	 * 참여자에게만 온다 — 호스트는 자기가 보낸 신호를 되받지 않는다.
	 */
	RoomHostReady,
	/** 연결이 끊겼다. 서버 종료, 강제 차단, 프레이밍 오류 등. */
	Disconnected,

	// --- 친구 / 메신저 (v7) ---
	//
	// ★ 새 값은 **끝에 붙인다.** 중간에 끼우면 뒤 값이 전부 밀리는데,
	//   이 enum 은 네트워크로 나가지 않으므로 프로토콜은 안 깨지지만
	//   블루프린트가 정수로 저장해 둔 참조가 조용히 다른 것을 가리키게 된다.

	/** 친구 목록 전체 도착. Friends 배열이 유효하다. 로그인 직후 한 번 */
	FriendListAck,
	/** 친구 신청 결과. FriendResult / TargetUserId 가 유효하다 */
	FriendAddAck,
	/** 누가 나에게 신청했다. Friend 에 상대 정보가 들어있다 */
	FriendRequestIncoming,
	/** 친구 하나가 바뀌었다(추가/삭제/수락). Friend 와 bFriendRemoved 가 유효하다 */
	FriendUpdate,
	/** 친구의 접속 상태만 바뀌었다. Friend.UserId / Friend.Presence 가 유효하다 */
	FriendPresence,
	/** DM 한 통 도착(실시간 또는 로그인 시 밀린 것). DirectMessage 가 유효하다 */
	DirectMessage,
	/** 대화 기록 도착. History / PeerUserId / bHasMoreHistory 가 유효하다 */
	DmHistoryAck,

	// --- 도달성 프로브 (v9) ---

	/**
	 * 서버가 프로브 UDP 를 쐈다(또는 못 쐈다). ProbeNonce / bProbeSent 가 유효하다.
	 *
	 * 호스트는 이 신호를 받은 뒤부터 시간을 센다. 서버가 쏘기 전부터 세면
	 * 왕복 지연만큼 손해를 보고, 서버가 못 쐈는데 기다리는 일도 생긴다.
	 */
	HostProbeSent,

	/** 서버가 내 공인 게임 엔드포인트를 관측해 알려줬다. Detail 에 "IP:포트". (v10) */
	ClientEndpointAck
};

/**
 * 백엔드 -> 게임 스레드 사건 전달용 구조체.
 *
 * USTRUCT 가 아니다. 워커 스레드나 SDK 콜백 스레드에서 만들어지므로
 * UObject 시스템과 무관해야 한다.
 */
struct FServerClientEvent
{
	EServerClientEventType Type = EServerClientEventType::Disconnected;

	/** Type == LoginAck / RegisterAck 일 때만 유효 */
	FChatLoginResult Login;

	/** Type == RoomListAck 일 때만 유효 */
	TArray<FMOURoomInfo> Rooms;

	/** Type == RoomJoinAck / RoomStart / RoomHostReady 일 때만 유효 */
	FMOURoomJoinResult Join;

	/** Type == RoomMemberList 일 때만 유효 */
	TArray<FMOURoomMember> Members;

	/** Type == RoomCreateAck / RoomMemberList / RoomClosed / RoomStart / RoomHostReady 일 때 유효 */
	int32            RoomId = 0;
	EMOURoomResultBP RoomResult = EMOURoomResultBP::Success;
	bool             bRoomSuccess = false;

	/** Type == HostProbeSent 일 때만 유효 (v9) */
	uint32 ProbeNonce  = 0;
	bool   bProbeSent  = false;

	/** Type == RoomStart 일 때만 유효. 방장이 punch 할 대상들 (v10) */
	TArray<FMOUHostCandidate> PunchTargets;

	/** Type == RoomStart 일 때만 유효. 방장 전용 relay host-facing 경로들 (v11). */
	TArray<FMOUGameRelayRoute> HostRelayRoutes;

	/** Type == RoomHostReady 일 때만 유효. 이 참여자 전용 relay guest-facing 경로 (v11). */
	FMOUGameRelayRoute GuestRelayRoute;

	/**
	 * Type == RoomMemberList 일 때만 유효.
	 * 백엔드가 판정한 값이다. 클라이언트가 Members 를 보고 직접 세지 않는다 —
	 * 목록이 한 박자 늦게 도착한 쪽이 다른 결론을 내면 UI 가 흔들린다.
	 */
	bool bAllReady = false;

	/** Type == RoomClosed 일 때만 유효 */
	EMOURoomCloseReasonBP CloseReason = EMOURoomCloseReasonBP::HostLeft;

	// --- 친구 / 메신저 (v7) ---

	/** Type == FriendListAck 일 때만 유효 */
	TArray<FMOUFriend> Friends;

	/**
	 * Type == FriendRequestIncoming / FriendUpdate / FriendPresence 일 때 유효.
	 *
	 * FriendPresence 는 UserId 와 Presence 만 채워진다 — 그 패킷이 9바이트라
	 * 닉네임을 싣지 않기 때문이다(자주 오므로 작게 유지한다).
	 */
	FMOUFriend Friend;

	/** Type == FriendUpdate 일 때만 유효. true 면 목록에서 지운다 */
	bool bFriendRemoved = false;

	/** Type == FriendAddAck 일 때만 유효 */
	EMOUFriendResultBP FriendResult = EMOUFriendResultBP::Success;
	int64              TargetUserId = 0;
	bool               bFriendSuccess = false;

	/** Type == DirectMessage 일 때만 유효 */
	FMOUDirectMessage DirectMessage;

	/** Type == DmHistoryAck 일 때만 유효. 오래된 것 -> 최신 순으로 담긴다 */
	TArray<FMOUDirectMessage> History;
	int64                     PeerUserId = 0;
	bool                      bHasMoreHistory = false;

	/** 로그 표시용 부가 설명 (실패 사유 등) */
	FString Detail;
};

/**
 * 계정 인증 + 세션 탐색을 제공하는 백엔드.
 *
 * 소유자는 UServerSubsystem 하나뿐이다. 직접 생성하지 말고
 * MOULobbyBackend::Create() 를 쓴다.
 */
class TEAMPROJECT_MOU_API ILobbyBackend
{
public:
	virtual ~ILobbyBackend() = default;

	// --- 정체 ------------------------------------------------------------

	/** 로그에 찍을 이름. "자체 서버(TCP)" / "EOS" 등. */
	virtual FString GetBackendName() const = 0;

	/**
	 * 채팅(전체/팀/사망 채널)을 지원하는가.
	 *
	 * EOS Lobby 는 방 안 채팅만 있고 "죽은 사람에게만 보이는 채널" 같은 판정을 못 한다.
	 * 그 판정은 게임 상태를 아는 쪽이 해야 하는데 EOS 는 그것을 모른다.
	 * 그래서 EOS 로 전환해도 채팅만은 자체 서버가 계속 맡을 가능성이 높다.
	 */
	virtual bool SupportsChat() const = 0;

	// --- 수명 ------------------------------------------------------------

	/**
	 * 백엔드를 켠다. 즉시 반환하고 결과는 Connecting/Connected/ConnectFailed 사건으로 온다.
	 * @return 시작조차 못 했으면 false (스레드 생성 실패, SDK 없음 등)
	 */
	virtual bool Start(const FString& Host, int32 Port) = 0;

	/** 완전히 정리한다. 워커 스레드가 끝날 때까지 반드시 기다린 뒤 반환해야 한다. */
	virtual void Shutdown() = 0;

	/** Start 이후 Shutdown 전인가. */
	virtual bool IsRunning() const = 0;

	// --- 계정 ------------------------------------------------------------

	virtual void SendLogin(const FString& LoginId, const FString& Password, int32 TeamId) = 0;
	virtual void SendRegister(const FString& LoginId, const FString& Password, const FString& Nickname) = 0;

	// --- 채팅 ------------------------------------------------------------

	virtual void SendChat(EChatChannelBP Channel, const FString& Text) = 0;

	/** 생사 상태를 알린다. 사망 채널 발화 자격 판정에 쓰인다. */
	virtual void SendSetDead(int64 UserId, bool bDead) = 0;

	// --- 로비 ------------------------------------------------------------

	virtual void CreateRoom(const FString& Title, const FString& RoomPassword, int32 HostPort,
	                        const FString& LanAddress) = 0;
	virtual void RequestRoomList() = 0;
	virtual void JoinRoom(int32 RoomId, const FString& RoomPassword) = 0;
	virtual void LeaveRoom() = 0;
	virtual void SetReady(bool bReady) = 0;
	virtual void StartGame() = 0;

	// --- 친구 / 메신저 (v7) ----------------------------------------------
	//
	// ★ 새 백엔드를 만들지 않고 기존 인터페이스에 얹는 이유: 친구도 결국
	//   "서버에 물어보는 일" 이라 교체 지점이 같다. EOS 로 갈아끼울 때도
	//   이 함수들만 EOS API 로 바꾸면 UServerSubsystem 은 한 줄도 안 바뀐다.

	/** 친구 + 대기 중인 신청 전체를 요청한다. 로그인 직후 한 번이면 된다. */
	virtual void RequestFriendList() = 0;

	/**
	 * 닉네임으로 친구를 신청한다.
	 *
	 * ★ Query 를 파싱하지 않고 **사용자가 친 그대로** 보낸다. 나중에
	 *   "닉네임#태그" 가 들어와도 클라는 바뀌지 않는다 — 서버만 고친다.
	 */
	virtual void AddFriend(const FString& Query) = 0;

	/** 받은 신청에 응답한다. bAccept 가 false 면 거절. */
	virtual void RespondFriendRequest(int64 FromUserId, bool bAccept) = 0;

	/** 친구를 끊거나 내가 보낸 신청을 취소한다. 둘 다 같은 요청이다. */
	virtual void RemoveFriend(int64 TargetUserId) = 0;

	/** 1:1 메시지를 보낸다. 친구가 아니면 서버가 조용히 버린다. */
	virtual void SendDirectMessage(int64 TargetUserId, const FString& Text) = 0;

	/**
	 * 대화 기록을 요청한다.
	 *
	 * @param BeforeMessageId  0 이면 가장 최근 페이지 + **읽음 처리**.
	 *   그 외에는 이 번호보다 오래된 것(위로 스크롤). 이때는 읽음 처리를 하지
	 *   않는다 — 옛 기록을 훑는 것만으로 새 메시지가 읽음이 되면 안 된다.
	 */
	virtual void RequestDmHistory(int64 PeerUserId, int64 BeforeMessageId) = 0;

	/**
	 * "내 리슨서버가 열렸다" 를 알린다. 방장만 부른다.
	 *
	 * 이 신호가 도착해야 참여자가 떠난다. 부르지 않으면 참여자는 대기실에서 기다린다 —
	 * 아직 열리지도 않은 주소로 보내 튕기게 만드는 것보다 낫다.
	 *
	 * 호출 시점은 UServerSubsystem 이 정한다(리슨서버 넷드라이버가 실제로 뜬 것을 확인).
	 * 위젯이나 게임 로직이 직접 부를 일은 없다.
	 */
	virtual void NotifyHostReady() = 0;

	// ── 도달성 프로브 (v9) ────────────────────────────────────────────
	//
	// UPnP 가 "매핑 성공" 이라고 해도 실제로 패킷이 들어온다는 보장이 없다.
	// 규칙을 기록만 하고 NAT 테이블에 반영하지 않는 공유기가 있다 — 실측으로 확인했다.
	// 믿지 말고 한 발 받아본다.

	/** 서버에게 "내 공인주소:이 포트로 UDP 를 한 발 쏴달라" 고 청한다. */
	virtual void RequestHostProbe(int32 Port, uint32 Nonce) = 0;

	/** 프로브 결과를 신고한다. 거짓이면 그 방은 같은 LAN 전용이 된다. */
	virtual void ReportReachability(bool bReachable) = 0;

	virtual void UpdateRoomState(int32 RoomId, int32 CurrentPlayers, bool bInGame) = 0;

	// --- 게임 스레드 펌프 --------------------------------------------------
	//
	// UServerSubsystem::Tick 이 매 프레임 비운다. 더 없으면 false 를 돌려준다.

	virtual bool DequeueEvent(FServerClientEvent& Out) = 0;
	virtual bool DequeueMessage(FChatMessage& Out) = 0;
};

namespace MOULobbyBackend
{
	/**
	 * 설정에 적힌 종류의 백엔드를 만든다. Start() 는 부르지 않는다.
	 *
	 * 없는 종류를 골랐거나 만들 수 없으면(SDK 미포함 등) nullptr 대신 그 종류의
	 * 구현체를 그대로 돌려주고, 실패는 Start() 가 ConnectFailed 사건으로 알린다.
	 * 그래야 "왜 아무 일도 안 일어나지" 대신 화면에 사유가 뜬다.
	 */
	TEAMPROJECT_MOU_API TUniquePtr<ILobbyBackend> Create(EMOULobbyBackendType Type);

	/** 로그/UI 에 쓸 이름. */
	TEAMPROJECT_MOU_API FString GetTypeName(EMOULobbyBackendType Type);
}
