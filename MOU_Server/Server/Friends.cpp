#include "Friends.h"

#include "sqlite3.h"

#include <cstdio>
#include <mutex>

namespace MOU
{
namespace Friends
{
namespace
{
	sqlite3*   GDb = nullptr;
	std::mutex GMutex;   // 여러 클라이언트 스레드가 동시에 신청/수락할 수 있다

	bool Exec(const char* Sql)
	{
		char* ErrMsg = nullptr;
		if (sqlite3_exec(GDb, Sql, nullptr, nullptr, &ErrMsg) != SQLITE_OK)
		{
			std::printf("[친구] SQL 실패: %s\n", ErrMsg ? ErrMsg : "?");
			sqlite3_free(ErrMsg);
			return false;
		}
		return true;
	}

	/**
	 * ★★ 관계 한 쌍을 항상 같은 순서로 만든다. **정렬은 여기서만 한다.**
	 *
	 *   이 함수를 거치지 않고 SQL 을 직접 짜는 코드가 하나라도 생기면, 그 경로로
	 *   들어온 관계만 (high, low) 순서로 저장돼 **PRIMARY KEY 가 중복을 못 막는다.**
	 *   증상은 "친구를 끊었는데 목록에 남아 있음" 이라 원인을 찾기 고약하다.
	 */
	void OrderPair(uint64_t A, uint64_t B, uint64_t& OutLow, uint64_t& OutHigh)
	{
		OutLow  = (A < B) ? A : B;
		OutHigh = (A < B) ? B : A;
	}

	/** 관계 한 줄의 현재 상태. 없으면 bExists 가 false. */
	struct PairRow
	{
		bool     bExists     = false;
		uint64_t RequestedBy = 0;   // 0 = 수락됨, 그 외 = 신청을 보낸 사람
	};

	/** 락을 이미 잡고 있는 상태에서 부른다. */
	PairRow FetchPair(uint64_t A, uint64_t B)
	{
		PairRow Row;

		uint64_t Low = 0, High = 0;
		OrderPair(A, B, Low, High);

		sqlite3_stmt* St = nullptr;
		const char* Sql = "SELECT requested_by FROM friends WHERE low_id = ? AND high_id = ?;";

		if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
		{
			std::printf("[친구] SELECT 준비 실패: %s\n", sqlite3_errmsg(GDb));
			return Row;
		}

		sqlite3_bind_int64(St, 1, static_cast<sqlite3_int64>(Low));
		sqlite3_bind_int64(St, 2, static_cast<sqlite3_int64>(High));

		if (sqlite3_step(St) == SQLITE_ROW)
		{
			Row.bExists     = true;
			Row.RequestedBy = static_cast<uint64_t>(sqlite3_column_int64(St, 0));
		}

		sqlite3_finalize(St);
		return Row;
	}

	/** 수락된 친구 수. 상한 검사에 쓴다. 락을 이미 잡고 있는 상태에서 부른다. */
	int CountAcceptedFriends(uint64_t UserId)
	{
		sqlite3_stmt* St = nullptr;
		const char* Sql =
			"SELECT COUNT(*) FROM friends"
			" WHERE requested_by = 0 AND (low_id = ?1 OR high_id = ?1);";

		if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
		{
			std::printf("[친구] COUNT 준비 실패: %s\n", sqlite3_errmsg(GDb));
			return -1;
		}

		sqlite3_bind_int64(St, 1, static_cast<sqlite3_int64>(UserId));

		int Count = -1;
		if (sqlite3_step(St) == SQLITE_ROW)
		{
			Count = sqlite3_column_int(St, 0);
		}

		sqlite3_finalize(St);
		return Count;
	}

	/**
	 * 검색어로 계정을 찾는다. 락을 이미 잡고 있는 상태에서 부른다.
	 *
	 * ★ LIMIT 2 인 이유: 동명이인이 있는지만 알면 되므로 전부 셀 필요가 없다.
	 *   두 줄째가 나오면 그 시점에 AmbiguousName 이 확정된다.
	 */
	EFriendResult ResolveQuery(const std::string& Query, uint64_t& OutId, std::string& OutNick)
	{
		if (Query.empty() || Query.size() >= kMaxFriendQueryLen)
		{
			return EFriendResult::InvalidFormat;
		}

		// ★ 태그가 들어올 자리. 지금은 거절하고, 나중에 여기서 닉과 태그를
		//   갈라 아래 SQL 의 WHERE 에 tag 조건을 하나 더 붙이면 끝이다.
		//   **바뀌는 곳이 이 함수 하나뿐**이라는 것이 이 구조의 요점이다.
		if (Query.find('#') != std::string::npos)
		{
			return EFriendResult::InvalidFormat;
		}

		sqlite3_stmt* St = nullptr;
		// COLLATE NOCASE: 대소문자를 신경 쓰게 만들 이유가 없다. login_id 와 같은 규칙이다.
		const char* Sql =
			"SELECT id, nickname FROM accounts WHERE nickname = ? COLLATE NOCASE LIMIT 2;";

		if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
		{
			std::printf("[친구] 닉네임 조회 준비 실패: %s\n", sqlite3_errmsg(GDb));
			return EFriendResult::DbError;
		}

		sqlite3_bind_text(St, 1, Query.c_str(), static_cast<int>(Query.size()), SQLITE_TRANSIENT);

		if (sqlite3_step(St) != SQLITE_ROW)
		{
			sqlite3_finalize(St);
			return EFriendResult::NotFound;
		}

		const uint64_t Id       = static_cast<uint64_t>(sqlite3_column_int64(St, 0));
		const char*    NickText = reinterpret_cast<const char*>(sqlite3_column_text(St, 1));
		const std::string Nick  = NickText ? NickText : "";

		// 두 번째 줄이 있으면 누구를 뜻하는지 알 수 없다.
		// ★ 이 오류가 곧 "#태그" 가 필요해지는 지점이다(CHAT_DESIGN.md 4-2절).
		const bool bAmbiguous = (sqlite3_step(St) == SQLITE_ROW);
		sqlite3_finalize(St);

		if (bAmbiguous)
		{
			return EFriendResult::AmbiguousName;
		}

		OutId   = Id;
		OutNick = Nick;
		return EFriendResult::Success;
	}
}

bool Start(const char* DbPath)
{
	if (GDb != nullptr)
	{
		return true;
	}

	if (sqlite3_open(DbPath, &GDb) != SQLITE_OK)
	{
		std::printf("[친구] DB 열기 실패: %s\n", sqlite3_errmsg(GDb));
		sqlite3_close(GDb);
		GDb = nullptr;
		return false;
	}

	// 계정과 같은 이유로 FULL 이다 — 친구 관계는 유실되면 안 된다.
	Exec("PRAGMA journal_mode=WAL;");
	Exec("PRAGMA synchronous=FULL;");
	sqlite3_busy_timeout(GDb, 5000);

	const char* kSchema =
		// 한 쌍당 한 줄. low_id < high_id 가 항상 성립한다(OrderPair).
		"CREATE TABLE IF NOT EXISTS friends("
		"  low_id       INTEGER NOT NULL,"
		"  high_id      INTEGER NOT NULL,"
		// 0 = 수락됨, 그 외 = 신청을 보낸 사람의 id. 헤더 주석 참고.
		"  requested_by INTEGER NOT NULL,"
		"  created_at   INTEGER NOT NULL,"
		"  PRIMARY KEY(low_id, high_id)"
		");"
		// 목록 조회는 "내가 low 인 줄" 과 "내가 high 인 줄" 을 둘 다 봐야 해서
		// 인덱스가 2개 필요하다. PRIMARY KEY 가 low 쪽은 이미 덮지만,
		// high 쪽은 이것이 없으면 전체 스캔이 된다.
		"CREATE INDEX IF NOT EXISTS idx_friends_high ON friends(high_id);"

		// ★ accounts 테이블에 거는 인덱스다. 소유는 Accounts.cpp 지만 여기서
		//   만든다 — 이 인덱스가 필요한 유일한 이유가 친구 검색이기 때문이다.
		//   없으면 친구 추가 때마다 계정 전체를 훑는다.
		//   COLLATE NOCASE 는 ResolveQuery 의 WHERE 와 반드시 일치해야 한다.
		//   다르면 인덱스를 안 타면서도 결과는 맞아 조용히 느려지기만 한다.
		"CREATE INDEX IF NOT EXISTS idx_accounts_nickname ON accounts(nickname COLLATE NOCASE);";

	if (!Exec(kSchema))
	{
		sqlite3_close(GDb);
		GDb = nullptr;
		return false;
	}

	std::printf("[친구] %s 준비 완료 (상한 %u명)\n", DbPath, kMaxFriends);
	return true;
}

void Stop()
{
	std::lock_guard<std::mutex> Lock(GMutex);
	if (GDb != nullptr)
	{
		sqlite3_close(GDb);
		GDb = nullptr;
	}
}

EFriendResult Add(uint64_t RequesterId, const std::string& Query,
                  uint64_t& OutTargetId, std::string& OutTargetNickname,
                  bool& bOutBecameFriends)
{
	bOutBecameFriends = false;

	std::lock_guard<std::mutex> Lock(GMutex);
	if (GDb == nullptr)
	{
		return EFriendResult::DbError;
	}

	uint64_t    TargetId = 0;
	std::string TargetNick;

	const EFriendResult Resolved = ResolveQuery(Query, TargetId, TargetNick);
	if (Resolved != EFriendResult::Success)
	{
		return Resolved;
	}

	if (TargetId == RequesterId)
	{
		return EFriendResult::SelfRequest;
	}

	const PairRow Existing = FetchPair(RequesterId, TargetId);

	if (Existing.bExists)
	{
		if (Existing.RequestedBy == 0)
		{
			return EFriendResult::AlreadyFriend;
		}

		if (Existing.RequestedBy == RequesterId)
		{
			return EFriendResult::AlreadyPending;
		}

		// ★ 상대가 이미 나에게 신청해 뒀다 = 맞신청. 그 자리에서 친구로 만든다.
		//   수락을 기다리게 하면 "둘 다 신청했는데 왜 친구가 아니지" 가 된다.
		uint64_t Low = 0, High = 0;
		OrderPair(RequesterId, TargetId, Low, High);

		sqlite3_stmt* St = nullptr;
		const char* Sql = "UPDATE friends SET requested_by = 0 WHERE low_id = ? AND high_id = ?;";

		if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
		{
			std::printf("[친구] 맞신청 UPDATE 준비 실패: %s\n", sqlite3_errmsg(GDb));
			return EFriendResult::DbError;
		}

		sqlite3_bind_int64(St, 1, static_cast<sqlite3_int64>(Low));
		sqlite3_bind_int64(St, 2, static_cast<sqlite3_int64>(High));

		const int Step = sqlite3_step(St);
		sqlite3_finalize(St);

		if (Step != SQLITE_DONE)
		{
			std::printf("[친구] 맞신청 UPDATE 실패: %s\n", sqlite3_errmsg(GDb));
			return EFriendResult::DbError;
		}

		OutTargetId       = TargetId;
		OutTargetNickname = TargetNick;
		bOutBecameFriends = true;
		return EFriendResult::Success;
	}

	// ★ 상한은 신청하는 쪽에서 미리 본다. 수락 시점에 걸러도 되지만, 그러면
	//   "신청은 받아놓고 수락할 때 거절당하는" 흐름이 되어 설명하기 어렵다.
	const int MyCount = CountAcceptedFriends(RequesterId);
	if (MyCount < 0)
	{
		return EFriendResult::DbError;
	}
	if (static_cast<uint32_t>(MyCount) >= kMaxFriends)
	{
		return EFriendResult::LimitReached;
	}

	uint64_t Low = 0, High = 0;
	OrderPair(RequesterId, TargetId, Low, High);

	sqlite3_stmt* St = nullptr;
	const char* Sql =
		"INSERT INTO friends(low_id, high_id, requested_by, created_at)"
		" VALUES(?,?,?, strftime('%s','now'));";

	if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
	{
		std::printf("[친구] INSERT 준비 실패: %s\n", sqlite3_errmsg(GDb));
		return EFriendResult::DbError;
	}

	sqlite3_bind_int64(St, 1, static_cast<sqlite3_int64>(Low));
	sqlite3_bind_int64(St, 2, static_cast<sqlite3_int64>(High));
	sqlite3_bind_int64(St, 3, static_cast<sqlite3_int64>(RequesterId));

	const int Step = sqlite3_step(St);
	sqlite3_finalize(St);

	if (Step != SQLITE_DONE)
	{
		// 두 스레드가 동시에 같은 쌍을 넣으려 한 경우다. 락 안이라 여기서는
		// 일어나지 않지만, PRIMARY KEY 가 최후 방어로 남아 있다.
		if (sqlite3_extended_errcode(GDb) == SQLITE_CONSTRAINT_PRIMARYKEY)
		{
			return EFriendResult::AlreadyPending;
		}
		std::printf("[친구] INSERT 실패: %s\n", sqlite3_errmsg(GDb));
		return EFriendResult::DbError;
	}

	OutTargetId       = TargetId;
	OutTargetNickname = TargetNick;
	return EFriendResult::Success;
}

EFriendResult Respond(uint64_t UserId, uint64_t FromUserId, bool bAccept)
{
	std::lock_guard<std::mutex> Lock(GMutex);
	if (GDb == nullptr)
	{
		return EFriendResult::DbError;
	}

	const PairRow Existing = FetchPair(UserId, FromUserId);

	if (!Existing.bExists)
	{
		return EFriendResult::NotFound;
	}

	if (Existing.RequestedBy == 0)
	{
		return EFriendResult::AlreadyFriend;
	}

	// ★★ 방향 검사. 이게 없으면 **자기가 보낸 신청을 스스로 수락해서**
	//   상대 동의 없이 친구가 될 수 있다. 클라가 FromUserId 를 마음대로
	//   채워 보낼 수 있으므로 서버가 반드시 막아야 한다.
	if (Existing.RequestedBy != FromUserId)
	{
		return EFriendResult::InvalidFormat;
	}

	uint64_t Low = 0, High = 0;
	OrderPair(UserId, FromUserId, Low, High);

	if (bAccept)
	{
		// 수락하는 쪽의 상한도 여기서 본다. 신청을 받아둔 사이에 내 친구가
		// 꽉 찼을 수 있다.
		const int MyCount = CountAcceptedFriends(UserId);
		if (MyCount < 0)
		{
			return EFriendResult::DbError;
		}
		if (static_cast<uint32_t>(MyCount) >= kMaxFriends)
		{
			return EFriendResult::LimitReached;
		}
	}

	sqlite3_stmt* St = nullptr;
	const char* Sql = bAccept
		? "UPDATE friends SET requested_by = 0 WHERE low_id = ? AND high_id = ?;"
		: "DELETE FROM friends WHERE low_id = ? AND high_id = ?;";

	if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
	{
		std::printf("[친구] 응답 준비 실패: %s\n", sqlite3_errmsg(GDb));
		return EFriendResult::DbError;
	}

	sqlite3_bind_int64(St, 1, static_cast<sqlite3_int64>(Low));
	sqlite3_bind_int64(St, 2, static_cast<sqlite3_int64>(High));

	const int Step = sqlite3_step(St);
	sqlite3_finalize(St);

	if (Step != SQLITE_DONE)
	{
		std::printf("[친구] 응답 실패: %s\n", sqlite3_errmsg(GDb));
		return EFriendResult::DbError;
	}

	return EFriendResult::Success;
}

EFriendResult Remove(uint64_t UserId, uint64_t TargetId)
{
	std::lock_guard<std::mutex> Lock(GMutex);
	if (GDb == nullptr)
	{
		return EFriendResult::DbError;
	}

	if (UserId == TargetId)
	{
		return EFriendResult::SelfRequest;
	}

	uint64_t Low = 0, High = 0;
	OrderPair(UserId, TargetId, Low, High);

	sqlite3_stmt* St = nullptr;
	const char* Sql = "DELETE FROM friends WHERE low_id = ? AND high_id = ?;";

	if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
	{
		std::printf("[친구] DELETE 준비 실패: %s\n", sqlite3_errmsg(GDb));
		return EFriendResult::DbError;
	}

	sqlite3_bind_int64(St, 1, static_cast<sqlite3_int64>(Low));
	sqlite3_bind_int64(St, 2, static_cast<sqlite3_int64>(High));

	const int Step = sqlite3_step(St);
	sqlite3_finalize(St);

	if (Step != SQLITE_DONE)
	{
		std::printf("[친구] DELETE 실패: %s\n", sqlite3_errmsg(GDb));
		return EFriendResult::DbError;
	}

	// 지울 것이 없었어도 성공으로 본다 — 결과 상태("친구가 아니다")가 같고,
	// 호출자가 재시도했을 때 오류를 보는 것이 더 이상하다.
	return EFriendResult::Success;
}

bool GetList(uint64_t UserId, std::vector<FriendRow>& Out)
{
	Out.clear();

	std::lock_guard<std::mutex> Lock(GMutex);
	if (GDb == nullptr)
	{
		return false;
	}

	// ★ JOIN 으로 상대방 닉네임을 같이 가져온다. UserId 만 내려주면 클라가
	//   이름을 알려고 또 왕복해야 한다.
	//
	//   CASE 는 "이 줄에서 상대가 누구인가" 를 고르는 것이다 - 관계를 한 줄로만
	//   저장하므로 내가 low 일 수도 high 일 수도 있다(헤더 주석).
	sqlite3_stmt* St = nullptr;
	const char* Sql =
		"SELECT a.id, a.nickname, f.requested_by"
		" FROM friends f"
		" JOIN accounts a"
		"   ON a.id = CASE WHEN f.low_id = ?1 THEN f.high_id ELSE f.low_id END"
		" WHERE f.low_id = ?1 OR f.high_id = ?1"
		" ORDER BY a.nickname COLLATE NOCASE"
		" LIMIT ?2;";

	if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
	{
		std::printf("[친구] 목록 준비 실패: %s\n", sqlite3_errmsg(GDb));
		return false;
	}

	sqlite3_bind_int64(St, 1, static_cast<sqlite3_int64>(UserId));

	// ★ 상한을 SQL 에 건다. 대기 중인 신청까지 합치면 수락된 친구 수보다
	//   많을 수 있는데, 그대로 다 담으면 FriendListAck 가 한 패킷을 넘긴다
	//   (ChatProtocol.h 의 static_assert 가 보증하는 것은 kMaxFriends 까지다).
	sqlite3_bind_int(St, 2, static_cast<int>(kMaxFriends));

	while (sqlite3_step(St) == SQLITE_ROW)
	{
		FriendRow Row;
		Row.UserId = static_cast<uint64_t>(sqlite3_column_int64(St, 0));

		const char* NickText = reinterpret_cast<const char*>(sqlite3_column_text(St, 1));
		Row.Nickname = NickText ? NickText : "";

		const uint64_t RequestedBy = static_cast<uint64_t>(sqlite3_column_int64(St, 2));

		if (RequestedBy == 0)
		{
			Row.State = EFriendState::Friend;
		}
		else if (RequestedBy == UserId)
		{
			Row.State = EFriendState::PendingOutgoing;
		}
		else
		{
			Row.State = EFriendState::PendingIncoming;
		}

		Out.push_back(std::move(Row));
	}

	sqlite3_finalize(St);
	return true;
}

bool GetFriendIds(uint64_t UserId, std::vector<uint64_t>& Out)
{
	Out.clear();

	std::lock_guard<std::mutex> Lock(GMutex);
	if (GDb == nullptr)
	{
		return false;
	}

	sqlite3_stmt* St = nullptr;
	const char* Sql =
		"SELECT CASE WHEN low_id = ?1 THEN high_id ELSE low_id END"
		" FROM friends"
		" WHERE requested_by = 0 AND (low_id = ?1 OR high_id = ?1);";

	if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
	{
		std::printf("[친구] id 목록 준비 실패: %s\n", sqlite3_errmsg(GDb));
		return false;
	}

	sqlite3_bind_int64(St, 1, static_cast<sqlite3_int64>(UserId));

	while (sqlite3_step(St) == SQLITE_ROW)
	{
		Out.push_back(static_cast<uint64_t>(sqlite3_column_int64(St, 0)));
	}

	sqlite3_finalize(St);
	return true;
}

bool AreFriends(uint64_t A, uint64_t B)
{
	if (A == B)
	{
		return false;
	}

	std::lock_guard<std::mutex> Lock(GMutex);
	if (GDb == nullptr)
	{
		return false;
	}

	const PairRow Row = FetchPair(A, B);
	return Row.bExists && Row.RequestedBy == 0;
}

} // namespace Friends
} // namespace MOU
