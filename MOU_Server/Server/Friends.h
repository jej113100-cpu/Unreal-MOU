// MOU 채팅 서버 - 친구 관계 저장소 (v7)
//
// [Accounts 와 같은 동기 방식이다 — ChatLog 와 다르다]
//   ChatLog 는 비동기 큐다. 채팅 한 줄이 늦게 저장되거나 몇 줄 유실돼도
//   게임이 망가지지 않기 때문에 지연을 없애는 쪽을 택했다.
//
//   친구는 정반대다. "수락을 눌렀는데 서버가 죽어서 친구가 아니다" 는 있을 수 없다.
//   그래서 호출한 스레드에서 그 자리에 쓰고 커밋한다. 친구 신청은 자주 일어나는
//   일이 아니라 이 정도 지연은 문제되지 않는다.
//
// [★★ 관계를 한 줄로만 저장한다 — 이 파일에서 가장 중요한 규칙]
//
//   (a,b) 와 (b,a) 를 둘 다 넣으면 수락/삭제 때 두 줄을 같이 고쳐야 하고,
//   하나만 고쳐진 순간 **"나는 친구인데 상대는 아닌" 상태**가 만들어진다.
//   한 줄이면 그럴 수가 없다.
//
//   대신 저장 전에 항상 (low, high) 로 정렬해야 한다. 이 정렬을 코드 여러 곳에서
//   하면 한 곳만 빠뜨렸을 때 **관계가 중복 저장되고 조용히 어긋난다.**
//   그래서 정렬은 이 파일 안의 헬퍼 하나에서만 하고, 밖으로는 SQL 을 노출하지 않는다.
//
// [requested_by 컬럼이 상태와 방향을 동시에 담는다]
//
//     requested_by == 0   -> 수락됨 (서로 친구)
//     requested_by == X   -> 대기 중이고, 신청을 보낸 사람이 X
//
//   상태 컬럼과 방향 컬럼을 따로 두지 않은 이유: 따로 두면 "대기 중인데 방향이
//   없는" 또는 "수락됐는데 방향이 남아 있는" 모순 상태를 표현할 수 있게 된다.
//   한 컬럼이면 수락 시 0 으로 덮으면 끝이라 모순이 생길 자리가 없다.
//
// [대응하는 문서]
//   CHAT_DESIGN.md 4절(친구 시스템), 8절(DB 스키마)

#pragma once

#include "ChatProtocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace MOU
{
	/**
	 * 친구 목록의 한 줄. 프로토콜의 FriendEntry 로 그대로 옮겨진다.
	 *
	 * Presence 와 UnreadCount 가 여기 없는 이유: 둘 다 DB 가 아니라 다른 곳이
	 * 안다(Presence 는 세션, UnreadCount 는 dm_messages). 라우팅 계층이 합친다.
	 */
	struct FriendRow
	{
		uint64_t     UserId = 0;      // 상대방의 계정 번호
		std::string  Nickname;        // 상대방 닉네임 (조회 시점의 값)
		EFriendState State = EFriendState::Friend;
	};

	namespace Friends
	{
		/**
		 * 친구 DB 를 연다. 서버 시작 시 한 번 부른다.
		 * 계정/채팅로그와 같은 파일을 써도 된다(테이블이 다르므로).
		 */
		bool Start(const char* DbPath);

		/** DB 를 닫는다. Start 가 실패했어도 부르는 것이 안전하다. */
		void Stop();

		/**
		 * 검색어로 사람을 찾아 친구 신청을 만든다.
		 *
		 * ★ Query 는 **사용자가 친 문자열 그대로**다. 파싱을 여기서 하는 이유는
		 *   나중에 "닉네임#태그" 를 붙일 때 프로토콜과 클라를 안 바꾸기 위해서다
		 *   (ChatProtocol.h 의 kMaxFriendQueryLen 주석).
		 *
		 *   지금:   '#' 없음 -> nickname 완전 일치(대소문자 무시)
		 *           '#' 있음 -> InvalidFormat
		 *   나중에: '#' 있음 -> nickname + tag 로 조회
		 *
		 * ★ 상대가 이미 나에게 신청해 둔 상태면 **그 자리에서 친구가 된다.**
		 *   둘 다 신청했는데 수락을 기다리게 하면 "나도 걔도 신청했는데 왜 친구가
		 *   아니지" 가 된다. 그 경우 bOutBecameFriends 가 true 로 온다 —
		 *   호출자는 양쪽에 FriendUpdate 를 보내야 한다.
		 *
		 * @param OutTargetId        찾은 상대의 계정 번호 (성공했을 때만)
		 * @param OutTargetNickname  찾은 상대의 닉네임 (성공했을 때만)
		 * @param bOutBecameFriends  맞신청이라 즉시 친구가 됐는가
		 */
		EFriendResult Add(uint64_t RequesterId, const std::string& Query,
		                  uint64_t& OutTargetId, std::string& OutTargetNickname,
		                  bool& bOutBecameFriends);

		/**
		 * 받은 신청에 응답한다.
		 *
		 * ★ 내가 **받은** 신청만 응답할 수 있다. 내가 보낸 신청을 스스로 수락해
		 *   친구가 되는 것을 막아야 하므로, 방향을 여기서 반드시 확인한다.
		 *   (거절은 Remove 와 결과가 같지만, 호출자가 보낼 알림이 달라 나눠 둔다.)
		 */
		EFriendResult Respond(uint64_t UserId, uint64_t FromUserId, bool bAccept);

		/**
		 * 친구를 끊거나, 내가 보낸 신청을 취소한다.
		 * 둘 다 "그 줄을 지운다" 라서 같은 함수다.
		 */
		EFriendResult Remove(uint64_t UserId, uint64_t TargetId);

		/**
		 * 친구 + 대기 중인 신청을 전부 가져온다. 닉네임 순으로 정렬돼 온다.
		 *
		 * 상한(kMaxFriends)을 넘지 않는다 — 넘으면 FriendListAck 가 한 패킷에
		 * 안 담긴다(ChatProtocol.h 의 static_assert).
		 */
		bool GetList(uint64_t UserId, std::vector<FriendRow>& Out);

		/**
		 * **수락된** 친구의 id 만 가져온다. 대기 중인 신청은 빼고.
		 *
		 * ★ 접속 상태 전파용이다. 상태가 바뀔 때마다 이 목록으로 "알려줄 대상" 을
		 *   정하는데, 그때마다 DB 를 때리면 접속자가 늘수록 로그인·로그아웃이
		 *   느려진다. 로그인 시 한 번 불러 세션에 캐시하고, 친구가 추가/삭제될
		 *   때만 갱신한다(CHAT_DESIGN.md 5-2절).
		 */
		bool GetFriendIds(uint64_t UserId, std::vector<uint64_t>& Out);

		/**
		 * 둘이 **수락된** 친구인가. 대기 중은 false 다.
		 *
		 * DM 을 보낼 자격 검사에 쓴다. 클라가 아무 UserId 에게나 DM 을 쏘는 것을
		 * 막아야 하므로, 보낼 때마다 서버가 이걸 확인한다.
		 */
		bool AreFriends(uint64_t A, uint64_t B);
	}
}
