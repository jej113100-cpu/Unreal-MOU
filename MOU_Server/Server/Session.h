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
