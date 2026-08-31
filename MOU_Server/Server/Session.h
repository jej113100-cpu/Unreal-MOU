// 접속한 클라이언트 하나의 상태와, 전체 세션 목록 관리.
//
// 기존 실습 코드의 `SOCKET clntSocks[10]` 을 대체한다.
// 고정 배열은 경계 검사가 없어 11번째 접속에서 배열 밖을 덮어썼고,
// 소켓 핸들만 들고 있어서 "누가 보냈는지"를 서버가 알 수 없었다.
#pragma once

#include "ChatProtocol.h"
#include "Net.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace MOU
{
	struct ClientSession
	{
		SocketHandle Sock = kInvalidSocket;

		// --- 신원 정보 ---
		// 전부 서버가 채운다. 클라이언트가 패킷에 담아 보낸 값을 그대로 쓰지 않는다.
		// 클라가 자기 이름을 문자열에 붙여 보내던 방식은 위조가 가능해서,
		// 사망자 채널 같은 권한 판정의 근거로 쓸 수 없다.
		uint64_t    UserId  = 0;
		std::string Name;
		int32_t     TeamId  = -1;
		bool        bDead   = false;
		bool        bAuthed = false;

		// accept() 시점에 읽은 상대 IP. 방을 만들 때 호스트 주소로 쓴다.
		// 클라이언트가 "내 IP 는 여기다" 라고 보내오는 값을 쓰면
		// 남의 주소를 적어 엉뚱한 곳으로 접속을 몰아줄 수 있다.
		std::string PeerAddress;

		/**
		 * 서버가 UDP 로 **관측한** 이 클라이언트의 공인 게임 엔드포인트. (v10)
		 *
		 * 홀펀칭 대상이다. 방장이 이 주소로 미리 한 발 쏘면 자기 NAT 에 구멍이 뚫리고,
		 * 그 뒤 이 참여자의 접속이 그 구멍으로 들어온다.
		 *
		 * ★ 클라이언트가 신고한 값이 아니라 recvfrom 으로 읽은 값이다.
		 *   신고를 믿으면 남을 엉뚱한 곳으로 punch 하게 만들 수 있다.
		 */
		std::string GameEndpointAddress;
		uint16_t    GameEndpointPort = 0;

		/**
		 * 이 사람이 마지막으로 신고한 도달성 결과. (v9)
		 *
		 * [왜 세션에 두는가 — 순서 때문이다]
		 *   프로브는 **방을 만들기 전에** 돈다(그때만 게임 포트가 비어 있다).
		 *   그래서 결과가 RoomCreateReq 보다 먼저 도착하는 것이 오히려 정상이다.
		 *   방에만 기록하려 하면 "아직 방이 없다"(NotInRoom)로 버려진다 —
		 *   실제로 그렇게 버려지고 있었다.
		 *
		 *   세션에 남겨두면 순서와 무관하다. 방을 만들 때 이 값을 얹고,
		 *   방이 이미 있으면 그 방도 같이 갱신한다.
		 */
		bool bLanOnly              = false;
		bool bHasReachabilityReport = false;

		// --- 친구 캐시 (v7) ---
		//
		// [왜 캐시하는가]
		//   접속 상태가 바뀔 때마다 "내 친구 중 접속해 있는 사람" 에게 알려야 한다.
		//   그때마다 friends 테이블을 조회하면 **접속자가 늘수록 로그인·로그아웃이
		//   느려진다** — 상태 변화는 잦고 친구 목록은 거의 안 바뀌기 때문이다.
		//   로그인 시 한 번 채우고, 친구가 추가/삭제될 때만 갱신한다.
		//
		// [★ 왜 뮤텍스가 필요한가 — 다른 필드와 다른 점]
		//   위의 UserId/Name/bDead 는 **그 세션의 스레드만** 만진다.
		//   FriendIds 는 다르다: A 가 B 의 신청을 수락하면 **A 의 스레드가 B 의
		//   FriendIds 도 고쳐야 한다.** 즉 남의 스레드가 쓰는 유일한 필드다.
		//   그래서 이것만 락으로 감싼다.
		//
		//   반드시 아래 헬퍼를 통해서만 접근할 것 — 직접 만지면 락이 빠진다.
		std::mutex            FriendMutex;
		std::vector<uint64_t> FriendIds;   // **수락된** 친구만. 대기 중은 안 들어간다

		/** 목록을 통째로 갈아끼운다. 로그인 시 한 번. */
		void SetFriendIds(std::vector<uint64_t> Ids)
		{
			std::lock_guard<std::mutex> Lock(FriendMutex);
			FriendIds = std::move(Ids);
		}

		/** 친구가 새로 생겼다. 이미 있으면 아무 일도 하지 않는다. */
		void AddFriendId(uint64_t Id)
		{
			std::lock_guard<std::mutex> Lock(FriendMutex);
			for (const uint64_t Existing : FriendIds)
			{
				if (Existing == Id)
				{
					return;
				}
			}
			FriendIds.push_back(Id);
		}

		/** 친구가 사라졌다. 없으면 아무 일도 하지 않는다. */
		void RemoveFriendId(uint64_t Id)
		{
			std::lock_guard<std::mutex> Lock(FriendMutex);
			for (size_t i = 0; i < FriendIds.size(); ++i)
			{
				if (FriendIds[i] == Id)
				{
					FriendIds.erase(FriendIds.begin() + static_cast<ptrdiff_t>(i));
					return;
				}
			}
		}

		/**
		 * 복사본을 돌려준다. **참조를 주지 않는 이유**: 호출자가 이 목록으로
		 * 세션을 순회하는 동안 남이 목록을 고칠 수 있고, 그러면 반복자가 깨진다.
		 * 목록은 길어야 kMaxFriends(93) 개라 복사 비용이 문제되지 않는다.
		 */
		std::vector<uint64_t> CopyFriendIds()
		{
			std::lock_guard<std::mutex> Lock(FriendMutex);
			return FriendIds;
		}

		// TCP 는 메시지 경계를 보장하지 않으므로 세션마다 누적 버퍼를 둔다.
		// 전역으로 두면 클라이언트끼리 데이터가 섞인다.
		std::vector<char> RecvBuf;
	};

	using SessionPtr = std::shared_ptr<ClientSession>;

	class SessionManager
	{
	public:
		// 새 세션을 만들어 목록에 넣는다.
		SessionPtr Add(SocketHandle Sock);

		// 목록에서 빼고 소켓을 닫는다. 둘 다 락 안에서 처리하므로
		// ForEach 순회 중에 소켓이 닫히는 일은 없다.
		void Remove(const SessionPtr& Session);

		uint64_t AssignUserId();
		size_t   Count();

		// 콜백을 세션 목록 락 안에서 실행한다.
		// 주의: 콜백 안에서 Add / Remove 를 호출하면 데드락이 난다.
		template <typename Fn>
		void ForEach(Fn&& Callback)
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			for (const SessionPtr& Session : Sessions)
			{
				Callback(Session);
			}
		}

	private:
		std::mutex              Mutex;
		std::vector<SessionPtr> Sessions;
		uint64_t                NextUserId = 1;
	};
}
