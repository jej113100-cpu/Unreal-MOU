// MOU 채팅 서버 - 계정 저장소 (아이디/비밀번호/닉네임).
//
// [ChatLog 와 무엇이 다른가 — 중요]
//   ChatLog 는 비동기 큐다. 채팅 한 줄이 늦게 저장되거나 서버가 죽어 몇 줄 유실돼도
//   게임이 망가지지 않기 때문에, 지연을 없애는 쪽을 택했다.
//
//   계정은 정반대다. "가입 버튼을 눌렀는데 서버가 죽어서 계정이 없다" 는 있을 수 없다.
//   그래서 여기서는 호출한 스레드에서 그 자리에서 쓰고 커밋한다(동기).
//   로그인은 자주 일어나는 일이 아니라 이 정도 지연은 문제되지 않는다.
//
// [커넥션을 따로 쓰는 이유]
//   ChatLog 의 sqlite3* 는 라이터 스레드 전용이다. 남이 끼어들면 그 전제가 깨진다.
//   여기서는 여러 클라이언트 스레드가 동시에 로그인할 수 있으므로
//   자체 커넥션 + 뮤텍스로 직렬화한다.
//
// [UserId 가 이제 무엇인가]
//   v2 까지 UserId 는 AssignUserId() 가 주는 접속 일련번호였다.
//   그래서 같은 사람이 재접속하면 번호가 달라졌고, chat_log.sender_id 도 의미가 없었다.
//   v3 부터 UserId = accounts.id 다. 서버를 재시작해도 같은 계정이면 같은 번호다.

#pragma once

#include <cstdint>
#include <string>

namespace MOU
{
	/** 계정 작업 결과. ELoginResult 로 그대로 옮겨 클라이언트에 보낸다. */
	enum class EAccountResult : uint8_t
	{
		Success = 0,
		NotFound,        // 아이디 없음
		WrongPassword,
		DuplicateId,     // 가입 시 이미 있는 아이디
		InvalidFormat,   // 길이 규칙 위반
		DbError
	};

	namespace Accounts
	{
		/**
		 * 계정 DB 를 연다. 서버 시작 시 한 번 부른다.
		 * 채팅 로그와 같은 파일을 써도 되고 달라도 된다(테이블이 다르므로).
		 */
		bool Start(const char* DbPath);

		/** DB 를 닫는다. Start 가 실패했어도 부르는 것이 안전하다. */
		void Stop();

		/**
		 * 계정을 만든다. 성공하면 OutUserId 에 새 계정 번호가 담긴다.
		 * 이 함수가 돌아온 시점에는 이미 디스크에 커밋되어 있다.
		 */
		EAccountResult Create(const std::string& LoginId, const std::string& Password,
		                      const std::string& Nickname, uint64_t& OutUserId);

		/**
		 * 아이디/비밀번호를 검증한다.
		 * 성공 시 OutUserId 와 OutNickname 이 채워진다.
		 *
		 * 실패 사유로 NotFound 와 WrongPassword 를 구분해 돌려주는데,
		 * 이건 "어느 아이디가 존재하는지" 를 외부에 알려주는 것이라 보안상 손해다.
		 * 다만 팀 내부용 프로젝트라 디버깅 편의를 택했다.
		 * 외부 서비스로 낼 거라면 둘 다 같은 사유로 뭉뚱그려야 한다.
		 */
		EAccountResult Authenticate(const std::string& LoginId, const std::string& Password,
		                            uint64_t& OutUserId, std::string& OutNickname);

		/**
		 * 계정 번호로 닉네임만 조회한다. 없으면 false.
		 *
		 * ★ 왜 필요한가 (v7): 친구 알림은 **접속해 있는 쪽에게, 접속하지 않은
		 *   쪽에 대해** 보내는 경우가 있다 — 오프라인이던 사람의 신청을 수락하는
		 *   순간이 그렇다. 그때 상대 닉네임을 세션에서만 찾으면 **빈 이름이
		 *   나가서 친구 목록에 이름 없는 줄이 생긴다.** 세션에 없으면 여기서 읽는다.
		 */
		bool GetNickname(uint64_t UserId, std::string& OutNickname);
	}
}
