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
	//   6 -> 7 : 친구 + 메신저(1:1 DM) + 접속 상태(Presence) 추가.
	//            ★ EChatChannel::Dead 와 SetDead 가 **폐기**됐다. 죽은 사람 채팅은
	//              이 서버가 아니라 방장의 리슨서버가 처리한다 —
	//              CHAT_DESIGN.md 3절. 번호는 호환을 위해 남겨두되 쓰지 않는다.
	constexpr uint16_t kProtocolVersion = 7;

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

	// --- 친구 / 메신저 (v7) ---

	// ★ 이 숫자는 임의로 정한 것이 아니다. FriendListAck 가 한 패킷에 담겨야 한다:
	//
	//     sizeof(FriendListAckBody) + sizeof(FriendEntry) * kMaxFriends <= kMaxBodySize
	//                             2 +                  44 * 93          =  4094  <= 4096
	//
	//   94 로 올리는 순간 4096 을 넘어 **친구가 많은 계정만 목록이 안 오는**
	//   증상이 난다. 파일 끝의 static_assert 가 컴파일 타임에 잡아주므로,
	//   FriendEntry 에 필드를 추가하면 이 값을 같이 줄여야 한다.
	constexpr uint32_t kMaxFriends = 93;

	// 친구 검색어. **닉네임만이 아니라 사용자가 친 문자열 그대로**가 들어온다.
	//
	// ★ 지금은 닉네임 완전 일치로만 찾지만, 나중에 "닉네임#태그" 를 붙일 때
	//   프로토콜을 안 바꾸려고 처음부터 태그까지 담을 폭을 잡아둔다.
	//   파싱은 서버가 하므로 그때 바뀌는 곳은 서버 함수 하나뿐이다.
	//   40바이트 아끼자고 나중에 v8 을 찍는 것이 훨씬 비싸다.
	constexpr uint32_t kMaxFriendQueryLen = kMaxNameLen + 8;   // 닉(32) + '#' + 태그 + 여유

	// 대화창을 열거나 위로 스크롤할 때 한 번에 받는 메시지 수.
	//
	//   sizeof(DmHistoryAckBody) + (sizeof(DmEntry) + 본문) * kDmPageSize <= kMaxBodySize
	//   본문이 길면 서버가 이 개수보다 적게 보낼 수 있다 — 받는 쪽은 Count 를 믿는다.
	constexpr uint32_t kDmPageSize = 50;

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

		// --- 친구 (v7) ---
		// ★ 새 옵코드는 **항상 끝에 붙인다.** 중간에 끼워 넣으면 뒤 번호가 전부
		//   밀려서, 업데이트를 안 한 클라와 조용히 어긋난다(패킷은 도착하는데
		//   엉뚱한 핸들러가 받는다). 쓰지 않게 된 번호도 지우지 않는 이유가 같다.
		FriendListReq         = 27,  // C->S. 내 친구 + 대기 중인 신청 전부
		FriendListAck         = 28,  // S->C. 뒤에 FriendEntry N개
		FriendAddReq          = 29,  // C->S. 닉네임(나중엔 닉#태그)으로 신청
		FriendAddAck          = 30,  // S->C. 신청 결과
		FriendRequestIncoming = 31,  // S->C. 누가 나에게 신청했다 (실시간)
		FriendRespondReq      = 32,  // C->S. 수락 / 거절
		FriendRemoveReq       = 33,  // C->S. 친구 삭제 또는 보낸 신청 취소
		FriendUpdate          = 34,  // S->C. 친구 하나의 상태 변화 (목록 전체 대신 델타)
		FriendPresence        = 35,  // S->C. 친구 하나의 접속 상태만 바뀜

		// --- 메신저 1:1 DM (v7) ---
		DirectMessageSend = 36,  // C->S. DM 보내기
		DirectMessage     = 37,  // S->C. DM 도착 (실시간 또는 로그인 시 밀린 것)
		DmHistoryReq      = 38,  // C->S. 대화 기록 (창 열기 / 위로 스크롤)
		DmHistoryAck      = 39,  // S->C. 뒤에 DmEntry N개
	};

	/**
	 * 친구 관계의 상태.
	 *
	 * ★ 대칭이 아니다. Pending 은 방향이 있고 화면도 다르다 —
	 *   보낸 쪽은 "대기 중" 회색, 받은 쪽은 수락/거절 버튼이 뜬다.
	 *   그래서 한 값으로 뭉뚱그리지 않고 둘로 나눈다.
	 */
	enum class EFriendState : uint8_t
	{
		Friend          = 0,   // 서로 수락함
		PendingOutgoing = 1,   // 내가 보냈고 상대가 아직 응답 안 함
		PendingIncoming = 2,   // 상대가 보냈고 내가 수락/거절해야 함
	};

	/**
	 * 접속 상태. **서버가 판정한다** — 클라가 주장하지 않는다.
	 *
	 * 근거는 전부 서버가 이미 갖고 있다: 세션 유무(SessionManager)와
	 * 방 상태(Rooms). 새로 추적할 것이 없어 추가 비용이 사실상 없다.
	 *
	 * ★ "대기중"(방에 앉아 있음)을 따로 두지 않았다. 친구 입장에서 온라인과
	 *   대기중은 둘 다 "지금 말 걸어도 된다" 라서 행동이 달라지지 않는다.
	 *   행동을 바꾸는 경계는 게임 중인가 아닌가 하나뿐이라 거기서만 나눈다.
	 */
	enum class EPresence : uint8_t
	{
		Offline = 0,   // 인증된 세션이 없다
		Online  = 1,   // 세션이 있고, 방에 없거나 방이 Waiting
		InGame  = 2,   // 방에 있고 그 방이 ERoomState::InGame
	};

	/** 친구 관련 요청의 결과. */
	enum class EFriendResult : uint8_t
	{
		Success        = 0,
		NotAuthed      = 1,   // 로그인하지 않았다
		NotFound       = 2,   // 그런 닉네임이 없다

		// ★ 같은 닉네임이 여럿이다. accounts.nickname 은 UNIQUE 가 아니다.
		//   이 오류가 곧 "#태그" 가 필요해지는 지점이다 — 지금 nickname 에
		//   UNIQUE 를 걸어 막아버리면 태그를 붙일 때 되돌려야 한다.
		//   자세한 배경은 CHAT_DESIGN.md 4-2절.
		AmbiguousName  = 3,

		AlreadyFriend  = 4,
		AlreadyPending = 5,
		SelfRequest    = 6,   // 자기 자신에게 신청
		LimitReached   = 7,   // kMaxFriends 초과
		InvalidFormat  = 8,   // 빈 문자열 등. 지금은 '#' 이 들어와도 여기
		DbError        = 9,
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

	// ------------------------------------------------------------------
	// 친구 (v7)
	// ------------------------------------------------------------------

	/**
	 * 친구 목록의 한 줄. FriendListAckBody 뒤에 Count 개가 이어붙는다.
	 *
	 * ★ 닉네임을 여기 실어 보내는 이유: 클라가 UserId 만 받으면 이름을 알려고
	 *   또 왕복해야 한다. 닉네임은 바뀔 수 있지만 목록을 받는 시점의 값이면
	 *   충분하고, 바뀌면 FriendUpdate 가 새 이름으로 덮는다.
	 */
	struct FriendEntry
	{
		uint64_t UserId;
		char     Nickname[kMaxNameLen];
		uint8_t  State;         // EFriendState
		uint8_t  Presence;      // EPresence. State != Friend 면 항상 Offline 로 채운다
		uint16_t UnreadCount;   // 안 읽은 DM 개수. 목록의 배지에 쓴다
	};

	// 뒤에 Count 개의 FriendEntry 가 이어붙는다.
	// 로그인 직후 한 번만 받고, 그 뒤로는 FriendUpdate / FriendPresence 델타만 온다.
	struct FriendListAckBody
	{
		uint16_t Count;
	};

	struct FriendAddReqBody
	{
		// ★ 파싱하지 않고 **사용자가 친 문자열 그대로** 보낸다.
		//   '#' 이 없으면 서버가 닉네임 완전 일치로 찾고, 나중에 태그가
		//   들어오면 서버만 고치면 된다(kMaxFriendQueryLen 주석).
		char Query[kMaxFriendQueryLen];
	};

	struct FriendAddAckBody
	{
		uint64_t TargetUserId;   // 성공했을 때만 채워진다. 실패면 0
		uint8_t  bSuccess;
		uint8_t  Result;         // EFriendResult
	};

	/** 누가 나에게 신청했다. 받는 즉시 목록에 PendingIncoming 으로 추가한다. */
	struct FriendRequestIncomingBody
	{
		uint64_t FromUserId;
		char     FromNickname[kMaxNameLen];
	};

	struct FriendRespondReqBody
	{
		uint64_t FromUserId;
		uint8_t  bAccept;        // 0 = 거절
	};

	/** 친구 삭제. 아직 수락 안 된 신청을 취소할 때도 같은 것을 쓴다. */
	struct FriendRemoveReqBody
	{
		uint64_t TargetUserId;
	};

	/**
	 * 친구 하나가 바뀌었다(추가 / 삭제 / 수락됨).
	 *
	 * 목록 전체를 다시 내려주지 않는 이유: 친구가 93명이면 4KB 를 통째로
	 * 다시 보내야 하는데, 실제로 바뀐 것은 한 줄이다.
	 */
	struct FriendUpdateBody
	{
		uint64_t UserId;
		char     Nickname[kMaxNameLen];
		uint8_t  State;          // EFriendState
		uint8_t  Presence;       // EPresence
		uint8_t  bRemoved;       // 1 이면 목록에서 지운다 (State 는 무시)
	};

	/**
	 * 접속 상태만 바뀌었다.
	 *
	 * ★ FriendUpdate 와 나눈 이유: 이쪽이 훨씬 자주 온다(로그인/로그아웃/
	 *   게임 시작마다, 친구 수만큼). 닉네임 32바이트를 매번 실어 보낼 이유가
	 *   없어서 9바이트로 유지한다.
	 *
	 * ★ 폴링하지 않는다. 상태가 실제로 바뀌는 순간에만 서버가 밀어준다 —
	 *   아무 일도 없는 동안 트래픽이 흐르지 않는다(CHAT_DESIGN.md 5-2절).
	 */
	struct FriendPresenceBody
	{
		uint64_t UserId;
		uint8_t  Presence;       // EPresence
	};

	// ------------------------------------------------------------------
	// 메신저 1:1 DM (v7)
	// ------------------------------------------------------------------

	// 뒤에 TextLen 바이트의 UTF-8 본문이 이어진다.
	struct DirectMessageSendBody
	{
		uint64_t TargetUserId;
		uint16_t TextLen;
	};

	/**
	 * DM 한 통. 뒤에 TextLen 바이트의 UTF-8 본문이 이어진다.
	 *
	 * ★ 보낸 사람도 자기 메시지를 이 형태로 되받는다. 클라가 자기 화면에
	 *   먼저 그려놓는 방식이면 서버가 매긴 MessageId 와 Timestamp 를 모르고,
	 *   그러면 커서 페이징의 기준이 클라마다 달라진다.
	 *
	 * ★ 상대가 오프라인이면 이 패킷은 지금 안 나간다. 대신 DB 에 남아 있다가
	 *   상대가 로그인할 때 밀린 것으로 나간다 — **저장이 전송보다 먼저다**
	 *   (순서를 바꾸면 "보냈는데 기록이 없는" 메시지가 생긴다).
	 */
	struct DirectMessageBody
	{
		uint64_t MessageId;      // 서버가 매긴다. 커서 페이징의 기준
		uint64_t FromUserId;
		uint64_t ToUserId;
		int64_t  Timestamp;      // Unix epoch (초)
		uint16_t TextLen;
	};

	struct DmHistoryReqBody
	{
		uint64_t PeerUserId;

		// 0 이면 가장 최근부터. 그 외에는 "이 번호보다 오래된 것" 을 달라는 뜻이다.
		//
		// ★ LIMIT/OFFSET 이 아니라 커서인 이유: 위로 스크롤하는 도중에 새
		//   메시지가 도착하면 오프셋이 밀려서 **같은 줄을 두 번 받거나 한 줄을
		//   건너뛴다.** id 기준이면 새 메시지가 와도 경계가 움직이지 않는다.
		uint64_t BeforeMessageId;
	};

	// 뒤에 Count 개의 DmEntry 가 이어붙는다. **오래된 것 -> 최신 순.**
	struct DmHistoryAckBody
	{
		uint64_t PeerUserId;
		uint16_t Count;
		uint8_t  bHasMore;       // 더 위로 스크롤할 것이 남았는가
	};

	/**
	 * 기록 한 줄. 뒤에 TextLen 바이트의 본문이 이어진다.
	 *
	 * ★ 본문 길이가 제각각이라 **배열 인덱싱으로 읽을 수 없다.** 앞에서부터
	 *   순회하면서 TextLen 만큼 건너뛰어야 한다. RoomInfo 처럼 고정 크기가
	 *   아니라는 점을 읽는 쪽이 반드시 지켜야 한다.
	 */
	struct DmEntry
	{
		uint64_t MessageId;
		uint64_t FromUserId;
		int64_t  Timestamp;
		uint16_t TextLen;
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

	// --- 친구 / 메신저 (v7) ---
	static_assert(sizeof(FriendEntry)               == 44, "FriendEntry 에 패딩이 끼었다");
	static_assert(sizeof(FriendListAckBody)         ==  2, "FriendListAckBody 에 패딩이 끼었다");
	static_assert(sizeof(FriendAddReqBody)          == 40, "FriendAddReqBody 에 패딩이 끼었다");
	static_assert(sizeof(FriendAddAckBody)          == 10, "FriendAddAckBody 에 패딩이 끼었다");
	static_assert(sizeof(FriendRequestIncomingBody) == 40, "FriendRequestIncomingBody 에 패딩이 끼었다");
	static_assert(sizeof(FriendRespondReqBody)      ==  9, "FriendRespondReqBody 에 패딩이 끼었다");
	static_assert(sizeof(FriendRemoveReqBody)       ==  8, "FriendRemoveReqBody 에 패딩이 끼었다");
	static_assert(sizeof(FriendUpdateBody)          == 43, "FriendUpdateBody 에 패딩이 끼었다");
	static_assert(sizeof(FriendPresenceBody)        ==  9, "FriendPresenceBody 에 패딩이 끼었다");
	static_assert(sizeof(DirectMessageSendBody)     == 10, "DirectMessageSendBody 에 패딩이 끼었다");
	static_assert(sizeof(DirectMessageBody)         == 34, "DirectMessageBody 에 패딩이 끼었다");
	static_assert(sizeof(DmHistoryReqBody)          == 16, "DmHistoryReqBody 에 패딩이 끼었다");
	static_assert(sizeof(DmHistoryAckBody)          == 11, "DmHistoryAckBody 에 패딩이 끼었다");
	static_assert(sizeof(DmEntry)                   == 26, "DmEntry 에 패딩이 끼었다");

	// ★★ 친구 목록이 한 패킷에 담기는지. **이것이 kMaxFriends 를 정한 근거다.**
	//
	//   2 + 44 * 93 = 4094 <= 4096.  94 로 올리면 여기서 빌드가 깨진다.
	//   FriendEntry 에 필드를 추가할 때도 마찬가지다 — 그때는 kMaxFriends 를
	//   줄이거나, 목록을 여러 패킷으로 쪼개는 설계를 해야 한다.
	//   (kMaxBodySize 를 올리는 것은 모든 패킷의 상한을 같이 올려 악성 패킷
	//    방어가 약해지므로 마지막 수단이다.)
	static_assert(sizeof(FriendListAckBody) + sizeof(FriendEntry) * kMaxFriends <= kMaxBodySize,
	              "친구 목록이 kMaxBodySize 를 넘는다. kMaxFriends 를 줄일 것");

	// 본문이 0바이트여도 한 페이지가 안 담기면 kDmPageSize 자체가 틀린 것이다.
	// (본문이 길면 서버가 Count 를 줄여 보낸다 — 받는 쪽은 Count 를 믿는다.)
	static_assert(sizeof(DmHistoryAckBody) + sizeof(DmEntry) * kDmPageSize <= kMaxBodySize,
	              "DM 한 페이지가 kMaxBodySize 를 넘는다. kDmPageSize 를 줄일 것");

	// 검색어가 닉네임보다 짧으면 정상적인 닉네임조차 못 담는다.
	static_assert(kMaxFriendQueryLen > kMaxNameLen,
	              "친구 검색어 폭이 닉네임보다 좁다");
}
