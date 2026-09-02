#include "DirectMessages.h"

#include "ChatProtocol.h"
#include "sqlite3.h"

#include <algorithm>
#include <cstdio>
#include <mutex>

namespace MOU
{
namespace DirectMessages
{
namespace
{
	sqlite3*   GDb = nullptr;
	std::mutex GMutex;

	bool Exec(const char* Sql)
	{
		char* ErrMsg = nullptr;
		if (sqlite3_exec(GDb, Sql, nullptr, nullptr, &ErrMsg) != SQLITE_OK)
		{
			std::printf("[메신저] SQL 실패: %s\n", ErrMsg ? ErrMsg : "?");
			sqlite3_free(ErrMsg);
			return false;
		}
		return true;
	}

	/**
	 * 결과 한 줄을 DmRow 로 옮긴다. 컬럼 순서는 (id, from_id, sent_at, text) 고정.
	 *
	 * ★ text 는 sqlite3_column_bytes 로 길이를 받아 쓴다. strlen 을 쓰면
	 *   본문에 널이 섞였을 때 뒤가 잘린다 — UTF-8 본문은 클라가 보낸 바이트
	 *   그대로라 그런 값이 들어올 수 있다.
	 */
	DmRow ReadRow(sqlite3_stmt* St)
	{
		DmRow Row;
		Row.MessageId  = static_cast<uint64_t>(sqlite3_column_int64(St, 0));
		Row.FromUserId = static_cast<uint64_t>(sqlite3_column_int64(St, 1));
		Row.Timestamp  = static_cast<int64_t>(sqlite3_column_int64(St, 2));

		const char* Text = reinterpret_cast<const char*>(sqlite3_column_text(St, 3));
		const int   Len  = sqlite3_column_bytes(St, 3);

		if (Text != nullptr && Len > 0)
		{
			Row.Text.assign(Text, static_cast<size_t>(Len));
		}

		return Row;
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
		std::printf("[메신저] DB 열기 실패: %s\n", sqlite3_errmsg(GDb));
		sqlite3_close(GDb);
		GDb = nullptr;
		return false;
	}

	// ★ synchronous=FULL. ChatLog(비동기, 유실 허용)와 갈리는 지점이다 —
	//   친구에게 보낸 메시지가 사라지는 것은 버그다(헤더 주석).
	Exec("PRAGMA journal_mode=WAL;");
	Exec("PRAGMA synchronous=FULL;");
	sqlite3_busy_timeout(GDb, 5000);

	const char* kSchema =
		"CREATE TABLE IF NOT EXISTS dm_messages("
		// AUTOINCREMENT 로 두는 이유: 커서 페이징의 기준이라 **재사용되면 안 된다.**
		// 그냥 INTEGER PRIMARY KEY 면 지운 행의 번호가 다시 쓰일 수 있고,
		// 그러면 "이 번호보다 오래된 것" 이라는 커서의 의미가 깨진다.
		"  id      INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  from_id INTEGER NOT NULL,"
		"  to_id   INTEGER NOT NULL,"
		"  text    TEXT    NOT NULL,"
		"  sent_at INTEGER NOT NULL,"
		"  read_at INTEGER"                       // NULL = 안 읽음
		");"

		// ★ 가장 잦은 질의는 "두 사람 사이의 대화를 최신순으로" 다.
		//   관계를 한 줄로 저장하는 friends 와 달리 여기는 방향이 있는 두 줄이
		//   섞여 있어서((A->B) 와 (B->A)), 인덱스도 양방향 두 개가 필요하다.
		"CREATE INDEX IF NOT EXISTS idx_dm_pair  ON dm_messages(from_id, to_id, id DESC);"
		"CREATE INDEX IF NOT EXISTS idx_dm_rpair ON dm_messages(to_id, from_id, id DESC);"

		// 로그인 직후 "안 읽은 것 전부" 와 배지 개수 세기 전용.
		// 부분 인덱스라 읽은 메시지는 인덱스에 들어가지 않는다 - 대부분의 행이
		// 읽음 상태가 되므로 전체 인덱스보다 훨씬 작게 유지된다.
		"CREATE INDEX IF NOT EXISTS idx_dm_unread ON dm_messages(to_id, from_id)"
		"  WHERE read_at IS NULL;";

	if (!Exec(kSchema))
	{
		sqlite3_close(GDb);
		GDb = nullptr;
		return false;
	}

	std::printf("[메신저] %s 준비 완료 (페이지 %u개)\n", DbPath, kDmPageSize);
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

bool Send(uint64_t FromId, uint64_t ToId, const char* Text, uint32_t TextLen,
          uint64_t& OutMessageId, int64_t& OutTimestamp)
{
	if (Text == nullptr || TextLen == 0 || TextLen > kMaxTextLen)
	{
		return false;
	}

	std::lock_guard<std::mutex> Lock(GMutex);
	if (GDb == nullptr)
	{
		return false;
	}

	sqlite3_stmt* St = nullptr;
	const char* Sql =
		"INSERT INTO dm_messages(from_id, to_id, text, sent_at)"
		" VALUES(?,?,?, strftime('%s','now'));";

	if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
	{
		std::printf("[메신저] INSERT 준비 실패: %s\n", sqlite3_errmsg(GDb));
		return false;
	}

	sqlite3_bind_int64(St, 1, static_cast<sqlite3_int64>(FromId));
	sqlite3_bind_int64(St, 2, static_cast<sqlite3_int64>(ToId));
	// 길이를 명시해서 넘긴다. 널 종료를 가정하지 않는다(헤더 주석).
	sqlite3_bind_text(St, 3, Text, static_cast<int>(TextLen), SQLITE_TRANSIENT);

	const int Step = sqlite3_step(St);
	sqlite3_finalize(St);

	if (Step != SQLITE_DONE)
	{
		std::printf("[메신저] INSERT 실패: %s\n", sqlite3_errmsg(GDb));
		return false;
	}

	OutMessageId = static_cast<uint64_t>(sqlite3_last_insert_rowid(GDb));

	// ★ 시각은 방금 넣은 행에서 되읽는다. 여기서 time(nullptr) 을 따로 부르면
	//   DB 에 적힌 값과 클라에 보낸 값이 1초 어긋날 수 있고, 그러면 기록을
	//   다시 불러왔을 때 같은 메시지의 시각이 달라 보인다.
	sqlite3_stmt* Read = nullptr;
	if (sqlite3_prepare_v2(GDb, "SELECT sent_at FROM dm_messages WHERE id = ?;", -1, &Read, nullptr) == SQLITE_OK)
	{
		sqlite3_bind_int64(Read, 1, static_cast<sqlite3_int64>(OutMessageId));
		if (sqlite3_step(Read) == SQLITE_ROW)
		{
			OutTimestamp = static_cast<int64_t>(sqlite3_column_int64(Read, 0));
		}
		sqlite3_finalize(Read);
	}

	return true;
}

bool GetHistory(uint64_t UserId, uint64_t PeerId, uint64_t BeforeMessageId,
                uint32_t Limit, std::vector<DmRow>& Out, bool& bOutHasMore)
{
	Out.clear();
	bOutHasMore = false;

	if (Limit == 0)
	{
		return true;
	}

	std::lock_guard<std::mutex> Lock(GMutex);
	if (GDb == nullptr)
	{
		return false;
	}

	sqlite3_stmt* St = nullptr;
	// ★ 최신 쪽부터 내림차순으로 뽑는다. "최근 50개" 를 얻으려면 이 방향이어야
	//   인덱스(id DESC)를 그대로 탄다. 화면 순서로 뒤집는 것은 아래에서 한다.
	//
	// ★ ?3 = 0 은 "커서 없음(가장 최근부터)" 이다. id 는 AUTOINCREMENT 라
	//   1부터 시작하므로 0 을 특수값으로 써도 실제 메시지와 겹치지 않는다.
	const char* Sql =
		"SELECT id, from_id, sent_at, text FROM dm_messages"
		" WHERE ((from_id = ?1 AND to_id = ?2) OR (from_id = ?2 AND to_id = ?1))"
		"   AND (?3 = 0 OR id < ?3)"
		" ORDER BY id DESC"
		" LIMIT ?4;";

	if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
	{
		std::printf("[메신저] 기록 준비 실패: %s\n", sqlite3_errmsg(GDb));
		return false;
	}

	sqlite3_bind_int64(St, 1, static_cast<sqlite3_int64>(UserId));
	sqlite3_bind_int64(St, 2, static_cast<sqlite3_int64>(PeerId));
	sqlite3_bind_int64(St, 3, static_cast<sqlite3_int64>(BeforeMessageId));

	// ★ 한 개 더 요청해서 "더 있는가" 를 판정한다. COUNT(*) 를 따로 돌리면
	//   같은 조건을 두 번 스캔하게 되고, 그 사이에 새 메시지가 들어오면
	//   개수와 내용이 어긋난다.
	sqlite3_bind_int(St, 4, static_cast<int>(Limit) + 1);

	while (sqlite3_step(St) == SQLITE_ROW)
	{
		Out.push_back(ReadRow(St));
	}

	sqlite3_finalize(St);

	if (Out.size() > Limit)
	{
		bOutHasMore = true;
		Out.pop_back();   // 판정용으로 한 개 더 받은 것이라 버린다
	}

	// 내림차순으로 뽑았으므로 화면에 그릴 순서(오래된 것 -> 최신)로 뒤집는다.
	std::reverse(Out.begin(), Out.end());
	return true;
}

bool MarkRead(uint64_t ReaderId, uint64_t PeerId)
{
	std::lock_guard<std::mutex> Lock(GMutex);
	if (GDb == nullptr)
	{
		return false;
	}

	sqlite3_stmt* St = nullptr;
	// 내가 **받은** 것만 읽음 처리한다. 내가 보낸 것의 read_at 은 상대가 찍는다.
	const char* Sql =
		"UPDATE dm_messages SET read_at = strftime('%s','now')"
		" WHERE to_id = ? AND from_id = ? AND read_at IS NULL;";

	if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
	{
		std::printf("[메신저] 읽음 처리 준비 실패: %s\n", sqlite3_errmsg(GDb));
		return false;
	}

	sqlite3_bind_int64(St, 1, static_cast<sqlite3_int64>(ReaderId));
	sqlite3_bind_int64(St, 2, static_cast<sqlite3_int64>(PeerId));

	const int Step = sqlite3_step(St);
	sqlite3_finalize(St);

	if (Step != SQLITE_DONE)
	{
		std::printf("[메신저] 읽음 처리 실패: %s\n", sqlite3_errmsg(GDb));
		return false;
	}

	return true;
}

bool GetUnreadCounts(uint64_t UserId, std::vector<UnreadCount>& Out)
{
	Out.clear();

	std::lock_guard<std::mutex> Lock(GMutex);
	if (GDb == nullptr)
	{
		return false;
	}

	sqlite3_stmt* St = nullptr;
	// ★ 친구마다 COUNT 를 돌리면 친구 수만큼 질의가 나간다. 한 번에 받는다.
	const char* Sql =
		"SELECT from_id, COUNT(*) FROM dm_messages"
		" WHERE to_id = ? AND read_at IS NULL"
		" GROUP BY from_id;";

	if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
	{
		std::printf("[메신저] 안읽음 집계 준비 실패: %s\n", sqlite3_errmsg(GDb));
		return false;
	}

	sqlite3_bind_int64(St, 1, static_cast<sqlite3_int64>(UserId));

	while (sqlite3_step(St) == SQLITE_ROW)
	{
		UnreadCount Row;
		Row.PeerUserId = static_cast<uint64_t>(sqlite3_column_int64(St, 0));
		Row.Count      = static_cast<uint32_t>(sqlite3_column_int(St, 1));
		Out.push_back(Row);
	}

	sqlite3_finalize(St);
	return true;
}

bool GetPending(uint64_t UserId, uint32_t Limit, std::vector<DmRow>& Out)
{
	Out.clear();

	if (Limit == 0)
	{
		return true;
	}

	std::lock_guard<std::mutex> Lock(GMutex);
	if (GDb == nullptr)
	{
		return false;
	}

	sqlite3_stmt* St = nullptr;
	// 오래된 것부터 올린다 - 받는 쪽이 도착 순서대로 그리면 되도록.
	// ★ 여기서 읽음 처리를 하지 않는다(헤더 주석). 받는 것과 읽는 것은 다르다.
	const char* Sql =
		"SELECT id, from_id, sent_at, text FROM dm_messages"
		" WHERE to_id = ? AND read_at IS NULL"
		" ORDER BY id ASC"
		" LIMIT ?;";

	if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
	{
		std::printf("[메신저] 밀린 메시지 준비 실패: %s\n", sqlite3_errmsg(GDb));
		return false;
	}

	sqlite3_bind_int64(St, 1, static_cast<sqlite3_int64>(UserId));
	sqlite3_bind_int(St, 2, static_cast<int>(Limit));

	while (sqlite3_step(St) == SQLITE_ROW)
	{
		Out.push_back(ReadRow(St));
	}

	sqlite3_finalize(St);
	return true;
}

} // namespace DirectMessages
} // namespace MOU
