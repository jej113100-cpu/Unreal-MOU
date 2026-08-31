// MOU 채팅 서버 - 1:1 메신저 저장소 (v7)
//
// [★ ChatLog 와 왜 다른 테이블인가 — 중요]
//
//   ChatLog 는 **비동기 큐**다. 몇 줄 유실돼도 괜찮다는 전제로 만들었고
//   (ChatLog.h 의 트레이드오프 주석), 조회 기능이 아예 없다. 기록용이다.
//
//   DM 은 그 전제가 하나도 안 맞는다:
//     · 친구에게 보낸 메시지가 서버 재시작으로 사라지면 그건 버그다
//     · 대화창을 열 때마다 **조회**한다 — 자주, 그리고 빠르게
//     · 안 읽음 개수를 세야 한다
//
//   그래서 Accounts 와 같은 동기 방식으로 별도 테이블을 쓴다.
//   사람이 타이핑하는 속도가 상한이라 초당 수천 건이 올 일이 없고,
//   동기 쓰기의 지연이 문제되면 그때 그룹 커밋을 넣으면 된다.
//
// [★★ 저장이 전송보다 먼저다]
//
//   Send() 는 상대의 접속 여부를 **모른다.** 항상 INSERT 하고, 실시간으로
//   밀어줄지는 호출자(라우팅 계층)가 세션을 보고 정한다.
//
//   순서를 바꾸면 — 즉 "접속해 있으면 보내고, 아니면 저장" 으로 만들면 —
//   전송에 성공했지만 기록이 없는 메시지가 생긴다. 그러면 대화창을 다시 열었을 때
//   방금 나눈 대화가 없다. **항상 저장하고, 전송은 그 다음이다.**
//
// [오프라인 메시지는 따로 큐를 두지 않는다]
//   "안 읽은 것"(read_at IS NULL)이 곧 "아직 못 받았을 수 있는 것"이다.
//   별도 전달 큐를 만들면 그 큐와 read_at 이 어긋날 수 있고, 어긋나면
//   메시지가 두 번 오거나 아예 안 온다. 상태를 하나로 유지하는 편이 안전하다.
//
// [대응하는 문서]
//   CHAT_DESIGN.md 6절(메신저), 8절(DB 스키마)

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MOU
{
	/** 대화 기록 한 줄. 프로토콜의 DmEntry + 본문으로 옮겨진다. */
	struct DmRow
	{
		uint64_t    MessageId = 0;
		uint64_t    FromUserId = 0;
		int64_t     Timestamp = 0;   // Unix epoch (초)
		std::string Text;
	};

	/** 안 읽은 메시지 개수. 친구 목록의 배지에 쓴다. */
	struct UnreadCount
	{
		uint64_t PeerUserId = 0;
		uint32_t Count      = 0;
	};

	namespace DirectMessages
	{
		/** DM DB 를 연다. 서버 시작 시 한 번. 계정/친구와 같은 파일이어도 된다. */
		bool Start(const char* DbPath);

		/** DB 를 닫는다. Start 가 실패했어도 부르는 것이 안전하다. */
		void Stop();

		/**
		 * 메시지를 저장한다. **상대의 접속 여부와 무관하게 항상 저장한다**(헤더 ★★).
		 *
		 * 보낼 자격(둘이 친구인가)은 이 함수가 확인하지 않는다 —
		 * 호출자가 Friends::AreFriends 로 먼저 거른다. 여기서 또 확인하면
		 * 같은 판정이 두 곳에 생겨 어긋날 여지가 된다.
		 *
		 * @param Text  널 종료가 아닐 수 있으므로 길이를 따로 받는다
		 *              (프로토콜상 본문이 구조체 뒤에 이어붙는다)
		 * @param OutMessageId  서버가 매긴 번호. 커서 페이징의 기준이 된다
		 * @param OutTimestamp  서버가 찍은 시각. 클라 시계를 믿지 않는다
		 */
		bool Send(uint64_t FromId, uint64_t ToId, const char* Text, uint32_t TextLen,
		          uint64_t& OutMessageId, int64_t& OutTimestamp);

		/**
		 * 두 사람의 대화 기록을 최신 쪽부터 Limit 개 가져온다.
		 * 결과는 **오래된 것 -> 최신 순**으로 담긴다(화면에 그대로 그리는 순서).
		 *
		 * @param BeforeMessageId  0 이면 가장 최근부터. 그 외에는 이 번호보다
		 *   오래된 것만. ★ OFFSET 이 아니라 커서인 이유: 위로 스크롤하는 도중에
		 *   새 메시지가 도착하면 오프셋이 밀려 **같은 줄을 두 번 받거나 한 줄을
		 *   건너뛴다.** id 기준이면 새 메시지가 와도 경계가 안 움직인다.
		 *
		 * @param bOutHasMore  더 위로 스크롤할 것이 남았는가
		 */
		bool GetHistory(uint64_t UserId, uint64_t PeerId, uint64_t BeforeMessageId,
		                uint32_t Limit, std::vector<DmRow>& Out, bool& bOutHasMore);

		/**
		 * 그 사람과의 안 읽은 메시지를 전부 읽음 처리한다.
		 *
		 * ★ 대화창을 여는 것이 곧 읽는 것이라, 창 열기(GetHistory)와 한 왕복에
		 *   묶는다. 클라가 "읽었다" 를 따로 보내게 하면 창은 열었는데 그 신호를
		 *   놓치는 경우가 생기고, 그러면 배지가 영원히 안 사라진다.
		 */
		bool MarkRead(uint64_t ReaderId, uint64_t PeerId);

		/**
		 * 상대별 안 읽은 개수. 친구 목록을 만들 때 한 번에 가져온다.
		 *
		 * 친구마다 COUNT 를 따로 돌리면 친구 수만큼 질의가 나간다.
		 * GROUP BY 로 한 번에 받아 호출자가 맞춰 넣는다.
		 */
		bool GetUnreadCounts(uint64_t UserId, std::vector<UnreadCount>& Out);

		/**
		 * 로그인 시 밀린 메시지를 가져온다(= 안 읽은 것 전부).
		 *
		 * ★ 여기서 읽음 처리를 하지 않는다. 받는 것과 읽는 것은 다르다 —
		 *   접속만 하고 대화창을 안 열었으면 여전히 안 읽은 것이고, 배지도
		 *   떠 있어야 한다. 읽음은 MarkRead 만 찍는다.
		 *
		 * @param Limit  한 번에 너무 많이 내려보내지 않기 위한 상한
		 */
		bool GetPending(uint64_t UserId, uint32_t Limit, std::vector<DmRow>& Out);
	}
}
