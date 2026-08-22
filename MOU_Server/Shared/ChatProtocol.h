// MOU 채팅 서버 <-> 클라이언트 공용 프로토콜 정의.
//
// 이 파일은 언리얼 클라이언트에서도 그대로 include 하므로
// STL 이나 플랫폼 헤더에 의존하지 않는다. <cstdint> 만 쓴다.
#pragma once

#include <cstdint>

namespace MOU
{
	// 헤더 구조나 오피코드 의미가 바뀌면 올린다.
	// 로그인 시점에 서버가 이 값을 검사하고, 다르면 명확한 사유와 함께 거부한다.
	// 이게 없으면 서버만 업데이트했을 때 클라이언트가 원인 모를 재접속을 무한 반복한다.
	//
	//   1 -> 2 : LoginReqBody 에 Version, LoginAckBody 에 Result/ServerVersion 추가
	//   2 -> 3 : 계정 시스템 도입. LoginReq 가 이름 대신 아이디/비밀번호를 보낸다.
	//            UserId 가 "접속 일련번호" 에서 "계정 고유번호" 로 바뀌었다.
	//   3 -> 4 : 로비(방 목록) 추가. 방 생성/조회/참여 옵코드가 붙었다.
	//   4 -> 5 : 대기실 추가. 서버가 방 멤버를 추적한다.
	//            RoomJoin 이 "주소 조회" 에서 "실제 입장" 으로 의미가 바뀌었고,
	//            RoomLeaveReq 를 참여자도 보낼 수 있게 됐다.
	//   5 -> 6 : 호스트 준비 신호 추가. RoomStart 의 의미가 "떠나라" 에서 "시작됐다" 로
	//            좁아지고, 참여자가 실제로 떠나는 시점은 RoomHostReady 가 정한다.
	//            v5 까지는 참여자가 고정 3초를 세고 떠났다. 호스트의 맵 로딩이 그보다
	//            오래 걸리면 튕겼고, 빨리 끝나도 3초를 그냥 버렸다.
	constexpr uint16_t kProtocolVersion = 6;

	// BodySize 가 이 값을 넘으면 악성 패킷으로 보고 연결을 끊는다.
	constexpr uint32_t kMaxBodySize = 4096;
	constexpr uint32_t kMaxNameLen  = 32;   // 닉네임 (화면에 보이는 이름)
	constexpr uint32_t kMaxTextLen  = 512;

	// 로그인 아이디와 비밀번호의 최대 바이트 수 (UTF-8 기준, 널 종료 포함).
	constexpr uint32_t kMaxLoginIdLen  = 24;
	constexpr uint32_t kMaxPasswordLen = 64;

	// 계정 정책. 서버가 검사하고, 클라이언트는 미리 걸러서 왕복을 아낀다.
	constexpr uint32_t kMinLoginIdLen  = 3;
	constexpr uint32_t kMinPasswordLen = 6;

	// --- 로비 ---
	constexpr uint32_t kMaxRoomTitleLen  = 48;
	constexpr uint32_t kRoomPasswordLen  = 4;    // 숫자 4자리. 널 종료를 두지 않는다
	constexpr uint32_t kMaxPlayersInRoom = 4;    // 1~4인 게임
	constexpr uint32_t kMaxRoomsInList   = 20;   // 한 번에 내려주는 방 개수 상한
	constexpr uint32_t kMaxAddressLen    = 16;   // "255.255.255.255" + 널

	enum class EOpcode : uint16_t
	{
		None          = 0,
		LoginReq      = 1,
		LoginAck      = 2,
		ChatSend      = 3,
		ChatBroadcast = 4,
		HistoryReq    = 5,   // 6단계에서 사용
		HistoryAck    = 6,   // 6단계에서 사용
		WhisperSend   = 7,   // 9단계에서 사용
		SetDead       = 8,   // 지금은 테스트 클라가 보내지만, 나중에는 리슨서버만 보낸다
		Heartbeat     = 9,
		RegisterReq   = 10,  // 계정 생성
		RegisterAck   = 11,

		// --- 로비 (v4) ---
		// 게임 트래픽은 여기를 거치지 않는다. 참가자는 호스트의 리슨서버에 직접 붙는다.
		// 이 서버가 관리하는 것은 "누가 어느 방에 있고 준비했는가" 까지다.
		RoomCreateReq   = 12,
		RoomCreateAck   = 13,
		RoomListReq     = 14,
		RoomListAck     = 15,
		RoomJoinReq     = 16,
		RoomJoinAck     = 17,
		RoomLeaveReq    = 18,  // 누구나 보낸다. 호스트가 보내면 방이 사라진다
		RoomStateUpdate = 19,  // 호스트가 진행 상태를 갱신한다 (인원수는 서버가 센다)

		// --- 대기실 (v5) ---
		// 서버가 방 멤버를 추적하면서 생긴 것들이다.
		// v4 까지는 방이 {호스트, 주소, 비번} 뿐이라 참여자에게 아무것도 보낼 수 없었다.
		RoomMemberList  = 20,  // 서버 -> 방 멤버 전원. 멤버/준비상태가 바뀔 때마다
		RoomReadyReq    = 21,  // 참여자 -> 서버. 준비 토글
		RoomClosed      = 22,  // 서버 -> 남은 멤버. 방이 사라졌다
		RoomStartReq    = 23,  // 호스트 -> 서버. 게임 시작 (전원 준비 완료여야 한다)
		RoomStart       = 24,  // 서버 -> 방 멤버 전원. 호스트 주소를 함께 내려준다

		// --- 호스트 준비 신호 (v6) ---
		// 리슨서버는 호스트가 맵을 다 연 뒤에야 접속을 받는다. 그 시점을 아는 것은
		// 호스트 프로세스 자신뿐이므로, 호스트가 알리고 서버는 참여자에게 그대로 넘긴다.
		// 서버는 여기서도 중계만 한다 — 게임 상태를 들여다보지 않는다.
		RoomHostReadyReq = 25,  // 호스트 -> 서버. 리슨서버가 열려서 접속을 받는 중이다
		RoomHostReady    = 26,  // 서버 -> 참여자. 지금 떠나라 (호스트 주소 동봉)
	};

	/** 방 관련 요청의 결과. */
	enum class ERoomResult : uint8_t
	{
		Success        = 0,
		NotAuthed      = 1,   // 로그인하지 않았다
		NotFound       = 2,   // 그런 방이 없다 (이미 닫혔을 수 있다)
		WrongPassword  = 3,
		Full           = 4,
		AlreadyStarted = 5,   // 이미 게임이 시작된 방
		AlreadyHosting = 6,   // 이미 방을 하나 갖고 있다
		InvalidRequest = 7,

		// --- 대기실 (v5) ---
		NotInRoom      = 8,   // 방에 들어가 있지 않다
		NotHost        = 9,   // 방장만 할 수 있는 일이다
		NotAllReady    = 10,  // 아직 준비하지 않은 참여자가 있다

		// --- 호스트 준비 신호 (v6) ---
		NotStarted     = 11,  // 아직 시작되지 않은 방이다 (RoomHostReadyReq 가 너무 이르다)
	};

	/**
	 * 방이 사라진 이유. RoomClosedBody 에 담긴다.
	 *
	 * 호스트 이양은 하지 않는다. 호스트가 곧 리슨서버라서, 호스트 프로세스가 죽으면
	 * 게임 세션 자체가 사라진다. 남은 사람을 새 호스트로 세우려면 리슨서버를 다시 열고
	 * 전원이 새 주소로 재접속해야 하는데, UE 가 이를 기본 지원하지 않는다.
	 * 그래서 방을 없애고 모두 메인메뉴로 돌려보낸다.
	 */
	enum class ERoomCloseReason : uint8_t
	{
		HostLeft = 0,   // 방장이 나갔다 (정상 종료든 랜선이 뽑혔든)
	};

	/** 방의 진행 상태. */
	enum class ERoomState : uint8_t
	{
		Waiting = 0,   // 대기 중. 목록에 노출된다
		InGame  = 1,   // 게임 시작됨. 목록에서 감춘다
	};

	enum class EChatChannel : uint8_t
	{
		All     = 0,
		Team    = 1,
		Dead    = 2,
		Whisper = 3,
		System  = 4,
	};

	// 로그인 거부 사유. LoginAckBody::Result 에 담겨 돌아온다.
	// 클라이언트는 이 값을 보고 "재시도해도 소용없는 실패"인지 판단할 수 있다.
	// RegisterAckBody::Result 에도 같은 enum 을 쓴다.
	enum class ELoginResult : uint8_t
	{
		Success         = 0,
		VersionMismatch = 1,   // 클라와 서버의 kProtocolVersion 이 다르다. 재접속해도 계속 실패한다
		InvalidRequest  = 2,   // 바디 크기가 맞지 않는다

		// --- 계정 관련 (v3) ---
		AccountNotFound = 3,   // 그런 아이디가 없다
		WrongPassword   = 4,   // 비밀번호가 틀렸다
		DuplicateId     = 5,   // 가입하려는 아이디가 이미 있다
		InvalidFormat   = 6,   // 아이디/비번 길이 규칙 위반
		ServerError     = 7,   // DB 오류 등 서버 문제. 클라이언트 잘못이 아니다
	};

#pragma pack(push, 1)

	// 모든 패킷 앞에 붙는 고정 헤더.
	// BodySize 는 이 헤더를 제외한 페이로드 크기다.
	// TCP 가 메시지 경계를 보장하지 않으므로, 수신측은 이 길이로 직접 잘라야 한다.
	struct PacketHeader
	{
		uint32_t BodySize;
		uint16_t Opcode;
	};

	// [경고] Password 는 지금 평문으로 전송된다.
	//   서버가 저장할 때는 솔트 + PBKDF2 로 해시하지만, 전송 구간에는 암호화가 없다.
	//   같은 네트워크에 있는 사람이 패킷을 뜨면 비밀번호가 그대로 보인다.
	//   >> 실제로 쓰는 비밀번호를 여기에 쓰지 말 것. 팀에도 공지할 것. <<
	//   제대로 하려면 TLS 를 씌워야 하는데, 그건 이 프로젝트 범위를 넘는다.
	struct LoginReqBody
	{
		// Version 은 반드시 첫 필드여야 한다.
		// 구조체 전체 크기가 서로 달라도 서버가 이 2바이트만은 읽을 수 있어야
		// "버전이 안 맞다"고 정확히 알려줄 수 있기 때문이다.
		// 앞으로 필드를 추가할 때는 반드시 뒤에 붙이고 Version 은 그대로 둔다.
		uint16_t Version;
		char     LoginId[kMaxLoginIdLen];
		char     Password[kMaxPasswordLen];
		int32_t  TeamId;                      // 게임 쪽이 정한다. 계정에 저장하지 않는다
	};

	// 계정 생성. 로그인과 같은 이유로 Version 이 첫 필드다.
	struct RegisterReqBody
	{
		uint16_t Version;
		char     LoginId[kMaxLoginIdLen];
		char     Password[kMaxPasswordLen];
		char     Nickname[kMaxNameLen];       // 화면에 보일 이름
	};

	struct RegisterAckBody
	{
		uint8_t  bSuccess;
		uint8_t  Result;                      // ELoginResult
		uint16_t ServerVersion;
	};

	struct LoginAckBody
	{
		uint64_t UserId;                 // 서버가 부여한다. 0 이면 실패
		int32_t  TeamId;
		char     Name[kMaxNameLen];      // 서버가 확정한 이름
		uint8_t  bSuccess;
		uint8_t  Result;                 // ELoginResult. 실패 사유
		uint16_t ServerVersion;          // 버전 불일치 시 어느 쪽이 낡았는지 바로 알 수 있게
	};

	// 뒤에 TextLen 바이트의 UTF-8 본문이 이어진다.
	struct ChatSendBody
	{
		uint64_t TargetUserId;           // Whisper 전용. 그 외에는 0
		uint16_t TextLen;
		uint8_t  Channel;
	};

	// 뒤에 TextLen 바이트의 UTF-8 본문이 이어진다.
	// SenderUserId / SenderName 은 서버가 세션 정보로 채운다.
	// 클라이언트가 보낸 값을 그대로 옮기지 않는다.
	struct ChatBroadcastBody
	{
		uint64_t SenderUserId;
		int64_t  Timestamp;              // Unix epoch (초)
		char     SenderName[kMaxNameLen];
		uint16_t TextLen;
		uint8_t  Channel;
	};

	struct SetDeadBody
	{
		uint64_t UserId;
		uint8_t  bDead;
	};

	// ------------------------------------------------------------------
	// 로비 (v4)
	// ------------------------------------------------------------------

	// [호스트 주소를 클라이언트가 보내지 않는 이유]
	//   IP 는 서버가 TCP 연결의 상대 주소에서 직접 읽는다.
	//   클라이언트가 "내 IP 는 여기다" 라고 적어 보내게 하면 남의 주소를 적어
	//   엉뚱한 사람에게 접속을 몰아주는 장난이 가능하다.
	//   이름을 서버가 채우는 것과 같은 원칙이다.
	//   포트는 리슨서버가 실제로 여는 값이라 클라이언트만 알 수 있어 받는다.
	struct RoomCreateReqBody
	{
		char     Title[kMaxRoomTitleLen];
		char     Password[kRoomPasswordLen];   // bHasPassword 가 0 이면 무시한다
		uint16_t HostPort;                     // 리슨서버 포트 (보통 7777)
		uint8_t  bHasPassword;
		uint8_t  MaxPlayers;                   // 1~kMaxPlayersInRoom
	};

	struct RoomCreateAckBody
	{
		uint32_t RoomId;                       // 0 이면 실패
		uint8_t  bSuccess;
		uint8_t  Result;                       // ERoomResult
	};

	// RoomListAckBody 뒤에 Count 개의 RoomInfo 가 이어붙는다.
	//
	// 주의: 여기에는 호스트 주소가 없다.
	//   목록만 보고 바로 접속하지 못하게 일부러 뺐다.
	//   주소는 RoomJoinReq 로 비밀번호 검사를 통과해야 받을 수 있다.
	struct RoomInfo
	{
		uint32_t RoomId;
		uint64_t HostUserId;
		char     Title[kMaxRoomTitleLen];
		char     HostName[kMaxNameLen];
		uint8_t  CurrentPlayers;
		uint8_t  MaxPlayers;
		uint8_t  bHasPassword;
		uint8_t  State;                        // ERoomState
	};

	struct RoomListAckBody
	{
		uint16_t Count;
	};

	struct RoomJoinReqBody
	{
		uint32_t RoomId;
		char     Password[kRoomPasswordLen];   // 비밀번호 방이 아니면 무시된다
	};

	struct RoomJoinAckBody
	{
		uint32_t RoomId;
		char     HostAddress[kMaxAddressLen];  // 성공했을 때만 채워진다
		uint16_t HostPort;
		uint8_t  bSuccess;
		uint8_t  Result;                       // ERoomResult
	};

	// 호스트가 진행 상태를 알린다. 방장만 보낼 수 있다.
	//
	// [v5] CurrentPlayers 는 이제 서버가 무시한다.
	//   서버가 멤버를 직접 세므로 호스트의 신고값을 믿을 이유가 없다.
	//   필드를 지우지 않은 것은 구조체 크기를 유지해 진단을 쉽게 하기 위해서다.
	struct RoomStateUpdateBody
	{
		uint32_t RoomId;
		uint8_t  CurrentPlayers;               // v5 부터 무시된다
		uint8_t  State;                        // ERoomState
	};

	// ------------------------------------------------------------------
	// 대기실 (v5)
	// ------------------------------------------------------------------

	/** 대기실에 앉아 있는 사람 하나. RoomMemberListBody 뒤에 Count 개가 이어진다. */
	struct RoomMemberInfo
	{
		uint64_t UserId;
		char     Name[kMaxNameLen];
		uint8_t  bIsHost;
		uint8_t  bReady;                       // 호스트는 항상 1 로 채워 보낸다
	};

	// 뒤에 Count 개의 RoomMemberInfo 가 이어붙는다.
	//
	// bAllReady 를 서버가 계산해서 내려주는 이유:
	//   "게임 시작 버튼을 켜도 되는가" 의 판정은 서버가 해야 한다.
	//   클라이언트가 멤버 목록을 보고 직접 세면, 목록이 한 박자 늦게 도착한 클라이언트가
	//   서버와 다른 결론을 낸다. 어차피 StartReq 에서 서버가 다시 검사하므로
	//   여기서 같은 답을 미리 주는 편이 UI 가 흔들리지 않는다.
	struct RoomMemberListBody
	{
		uint32_t RoomId;
		uint8_t  Count;
		uint8_t  bAllReady;
	};

	struct RoomReadyReqBody
	{
		uint8_t bReady;
	};

	struct RoomClosedBody
	{
		uint32_t RoomId;
		uint8_t  Reason;                       // ERoomCloseReason
	};

	// 게임이 시작됐다. 참여자는 이 주소로 ClientTravel 하면 된다.
	//
	// 방을 만들 때가 아니라 여기서 주소를 다시 내려주는 이유:
	//   참여 시점과 시작 시점 사이에 호스트가 포트를 바꿨을 수도 있고,
	//   무엇보다 "지금 떠나도 된다" 는 신호가 주소와 함께 오는 편이 명확하다.
	struct RoomStartBody
	{
		uint32_t RoomId;
		char     HostAddress[kMaxAddressLen];
		uint16_t HostPort;
	};

	/**
	 * 호스트의 리슨서버가 열렸다. 참여자는 이것을 받고 나서 떠난다.
	 *
	 * [RoomStartBody 와 내용이 같은데 왜 따로 두는가]
	 *   두 신호의 의미가 다르기 때문이다.
	 *     RoomStart      = "게임이 시작됐다. 대기실을 닫아라"
	 *     RoomHostReady  = "지금 접속해도 된다. 떠나라"
	 *   같은 구조체를 돌려쓰면 나중에 한쪽에만 필드를 붙일 때 반드시 갈라야 하는데,
	 *   그때는 이미 양쪽 코드가 얽혀 있어서 지금 가르는 것보다 비싸다.
	 *
	 * 주소를 한 번 더 실어 보내는 이유:
	 *   참여자가 RoomStart 를 놓쳤거나 늦게 처리했더라도 이 패킷 하나만으로 떠날 수 있다.
	 *   주소는 22바이트뿐이라 아낄 이유가 없다.
	 */
	struct RoomHostReadyBody
	{
		uint32_t RoomId;
		char     HostAddress[kMaxAddressLen];
		uint16_t HostPort;
	};

#pragma pack(pop)

	// 패딩이 끼면 서버와 클라이언트의 해석이 어긋난다.
	// #pragma pack(1) 이 빠지거나 필드 순서를 바꿨을 때 여기서 잡힌다.
	static_assert(sizeof(PacketHeader)      ==  6, "PacketHeader 는 6바이트여야 한다");
	static_assert(sizeof(LoginReqBody)      == 94, "LoginReqBody 에 패딩이 끼었다");
	static_assert(sizeof(RegisterReqBody)   == 122, "RegisterReqBody 에 패딩이 끼었다");
	static_assert(sizeof(RegisterAckBody)   ==  4, "RegisterAckBody 에 패딩이 끼었다");
	static_assert(sizeof(LoginAckBody)      == 48, "LoginAckBody 에 패딩이 끼었다");
	static_assert(sizeof(ChatSendBody)      == 11, "ChatSendBody 에 패딩이 끼었다");
	static_assert(sizeof(ChatBroadcastBody) == 51, "ChatBroadcastBody 에 패딩이 끼었다");
	static_assert(sizeof(SetDeadBody)       ==  9, "SetDeadBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomCreateReqBody) == 56, "RoomCreateReqBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomCreateAckBody) ==  6, "RoomCreateAckBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomInfo)          == 96, "RoomInfo 에 패딩이 끼었다");
	static_assert(sizeof(RoomListAckBody)   ==  2, "RoomListAckBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomJoinReqBody)   ==  8, "RoomJoinReqBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomJoinAckBody)   == 24, "RoomJoinAckBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomStateUpdateBody) == 6, "RoomStateUpdateBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomMemberInfo)     == 42, "RoomMemberInfo 에 패딩이 끼었다");
	static_assert(sizeof(RoomMemberListBody) ==  6, "RoomMemberListBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomReadyReqBody)   ==  1, "RoomReadyReqBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomClosedBody)     ==  5, "RoomClosedBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomStartBody)      == 22, "RoomStartBody 에 패딩이 끼었다");
	static_assert(sizeof(RoomHostReadyBody)  == 22, "RoomHostReadyBody 에 패딩이 끼었다");

	// 방 목록 한 번에 담을 수 있는지 확인한다. 넘치면 kMaxRoomsInList 를 줄여야 한다.
	static_assert(sizeof(RoomListAckBody) + sizeof(RoomInfo) * kMaxRoomsInList <= kMaxBodySize,
	              "방 목록이 kMaxBodySize 를 넘는다");
	static_assert(sizeof(RoomMemberListBody) + sizeof(RoomMemberInfo) * kMaxPlayersInRoom <= kMaxBodySize,
	              "대기실 멤버 목록이 kMaxBodySize 를 넘는다");
}
