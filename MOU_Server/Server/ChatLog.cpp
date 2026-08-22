#include "ChatLog.h"

#include "sqlite3.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace MOU
{
namespace ChatLog
{
namespace
{
	// 큐 상한. 디스크가 느려도 메모리가 무한정 늘지 않게 막는다.
	// 한 줄이 대략 200바이트 안팎이므로 1만 줄이면 몇 MB 수준이다.
	constexpr size_t kMaxQueuedEntries = 10000;

	// 한 트랜잭션에 몰아 쓸 최대 줄 수.
	// 줄마다 커밋하면 커밋마다 디스크 동기화가 일어나 훨씬 느리다.
	constexpr size_t kMaxBatchRows = 256;

	struct LogEntry
	{
		int64_t     Timestamp = 0;
		uint64_t    SenderUserId = 0;
		std::string SenderName;
		std::string Text;
		int32_t     TeamId = -1;
		uint8_t     Channel = 0;
	};

	std::mutex              GMutex;
	std::condition_variable GCv;
	std::deque<LogEntry>    GQueue;
	bool                    GStopping = false;

	std::thread   GWriterThread;
	sqlite3*      GDb = nullptr;
	sqlite3_stmt* GInsertStmt = nullptr;

	std::atomic<uint64_t> GWritten{ 0 };
	std::atomic<uint64_t> GDropped{ 0 };

	// 큐가 넘칠 때 로그를 매번 찍으면 콘솔이 도배된다. 처음 한 번만 알린다.
	bool GWarnedOverflow = false;

	bool Exec(const char* Sql)
	{
		char* ErrMsg = nullptr;
		if (sqlite3_exec(GDb, Sql, nullptr, nullptr, &ErrMsg) != SQLITE_OK)
		{
			std::printf("[채팅로그] SQL 실패: %s (%s)\n", ErrMsg ? ErrMsg : "?", Sql);
			sqlite3_free(ErrMsg);
			return false;
		}
		return true;
	}

	// 한 묶음을 트랜잭션 하나로 쓴다.
	// 이 함수는 라이터 스레드에서만 불리므로 GDb / GInsertStmt 에 락이 필요 없다.
	void WriteBatch(const std::vector<LogEntry>& Batch)
	{
		if (Batch.empty() || GDb == nullptr)
		{
			return;
		}

		if (!Exec("BEGIN;"))
		{
			return;
		}

		uint64_t OkCount = 0;
		for (const LogEntry& E : Batch)
		{
			sqlite3_reset(GInsertStmt);
			sqlite3_clear_bindings(GInsertStmt);

			sqlite3_bind_int64(GInsertStmt, 1, E.Timestamp);
			sqlite3_bind_int64(GInsertStmt, 2, static_cast<sqlite3_int64>(E.SenderUserId));
			// SQLITE_TRANSIENT: sqlite 가 문자열을 자체 복사하게 한다.
			// STATIC 을 쓰면 Batch 가 사라진 뒤를 가리키게 된다.
			sqlite3_bind_text(GInsertStmt, 3, E.SenderName.c_str(),
			                  static_cast<int>(E.SenderName.size()), SQLITE_TRANSIENT);
			sqlite3_bind_int(GInsertStmt, 4, E.Channel);
			sqlite3_bind_int(GInsertStmt, 5, E.TeamId);
			sqlite3_bind_text(GInsertStmt, 6, E.Text.c_str(),
			                  static_cast<int>(E.Text.size()), SQLITE_TRANSIENT);

			if (sqlite3_step(GInsertStmt) == SQLITE_DONE)
			{
				++OkCount;
			}
			else
			{
				std::printf("[채팅로그] INSERT 실패: %s\n", sqlite3_errmsg(GDb));
			}
		}
		sqlite3_reset(GInsertStmt);

		if (Exec("COMMIT;"))
		{
			GWritten.fetch_add(OkCount, std::memory_order_relaxed);
		}
		else
		{
			Exec("ROLLBACK;");
		}
	}

	// DB 라이터 스레드의 본체. 서버 전체에 이 스레드 하나뿐이다.
	void WriterLoop()
	{
		std::vector<LogEntry> Batch;
		Batch.reserve(kMaxBatchRows);

		for (;;)
		{
			{
				std::unique_lock<std::mutex> Lock(GMutex);

				// 할 일이 없으면 잔다. 폴링하지 않으므로 유휴 시 CPU 를 쓰지 않는다.
				GCv.wait(Lock, [] { return GStopping || !GQueue.empty(); });

				// 종료 신호가 와도 큐가 남아있으면 마저 쓴다(정상 종료 시 무손실).
				if (GQueue.empty())
				{
					if (GStopping)
					{
						break;
					}
					continue;
				}

				const size_t Take = (GQueue.size() < kMaxBatchRows) ? GQueue.size() : kMaxBatchRows;
				for (size_t i = 0; i < Take; ++i)
				{
					Batch.push_back(std::move(GQueue.front()));
					GQueue.pop_front();
				}
			}
			// 락을 놓고 쓴다. 디스크 I/O 동안 클라이언트 스레드가 Enqueue 를 계속할 수 있다.
			WriteBatch(Batch);
			Batch.clear();
		}
	}
}

bool Start(const char* DbPath)
{
	if (GDb != nullptr)
	{
		return true;   // 이미 시작됨
	}

	if (sqlite3_open(DbPath, &GDb) != SQLITE_OK)
	{
		std::printf("[채팅로그] DB 열기 실패: %s\n", sqlite3_errmsg(GDb));
		sqlite3_close(GDb);
		GDb = nullptr;
		return false;
	}

	// WAL: 읽는 쪽(나중에 관전/검색 기능)이 쓰는 쪽을 막지 않는다.
	// synchronous=NORMAL: WAL 에서는 커밋마다 fsync 하지 않는다.
	//   OS 가 죽으면 최근 몇 커밋이 날아갈 수 있지만 DB 가 깨지지는 않는다.
	//   채팅 로그에는 충분한 보증이고, FULL 보다 훨씬 빠르다.
	Exec("PRAGMA journal_mode=WAL;");
	Exec("PRAGMA synchronous=NORMAL;");

	const char* kSchema =
		"CREATE TABLE IF NOT EXISTS chat_log("
		"  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  ts          INTEGER NOT NULL,"   // Unix epoch(초). 패킷의 Timestamp 그대로
		"  sender_id   INTEGER NOT NULL,"
		"  sender_name TEXT    NOT NULL,"
		"  channel     INTEGER NOT NULL,"   // EChatChannel
		"  team_id     INTEGER NOT NULL,"
		"  text        TEXT    NOT NULL"
		");"
		// "최근 N줄" 조회가 주 용도라 시간 인덱스를 둔다.
		"CREATE INDEX IF NOT EXISTS idx_chat_log_ts ON chat_log(ts);";

	if (!Exec(kSchema))
	{
		sqlite3_close(GDb);
		GDb = nullptr;
		return false;
	}

	// 구문을 매번 파싱하지 않도록 한 번만 준비해두고 재사용한다.
	const char* kInsert =
		"INSERT INTO chat_log(ts, sender_id, sender_name, channel, team_id, text)"
		" VALUES(?,?,?,?,?,?);";

	if (sqlite3_prepare_v2(GDb, kInsert, -1, &GInsertStmt, nullptr) != SQLITE_OK)
	{
		std::printf("[채팅로그] INSERT 준비 실패: %s\n", sqlite3_errmsg(GDb));
		sqlite3_close(GDb);
		GDb = nullptr;
		return false;
	}

	GStopping = false;
	GWriterThread = std::thread(WriterLoop);

	std::printf("[채팅로그] %s 에 기록한다 (SQLite %s)\n", DbPath, sqlite3_libversion());
	return true;
}

void Enqueue(int64_t Timestamp, uint64_t SenderUserId, const std::string& SenderName,
             uint8_t Channel, int32_t TeamId, const char* Text, uint16_t TextLen)
{
	if (GDb == nullptr)
	{
		// Start 가 실패한 상태. 채팅은 계속되어야 하므로 조용히 버리되 집계는 한다.
		GDropped.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	LogEntry Entry;
	Entry.Timestamp    = Timestamp;
	Entry.SenderUserId = SenderUserId;
	Entry.SenderName   = SenderName;
	Entry.Channel      = Channel;
	Entry.TeamId       = TeamId;
	Entry.Text.assign(Text, TextLen);   // 널 종료가 아니므로 길이로 복사한다

	{
		std::lock_guard<std::mutex> Lock(GMutex);

		if (GQueue.size() >= kMaxQueuedEntries)
		{
			GDropped.fetch_add(1, std::memory_order_relaxed);
			if (!GWarnedOverflow)
			{
				GWarnedOverflow = true;
				std::printf("[채팅로그] 큐가 상한(%zu)에 도달해 기록을 버리기 시작한다."
				            " 디스크가 채팅 속도를 못 따라가고 있다.\n", kMaxQueuedEntries);
			}
			return;
		}

		GQueue.push_back(std::move(Entry));
	}
	// 락 밖에서 깨운다. 락을 쥔 채 깨우면 깨어난 스레드가 곧바로 락 대기에 걸린다.
	GCv.notify_one();
}

void Stop()
{
	if (GWriterThread.joinable())
	{
		{
			std::lock_guard<std::mutex> Lock(GMutex);
			GStopping = true;
		}
		GCv.notify_one();
		GWriterThread.join();   // 남은 큐를 다 쓸 때까지 기다린다
	}

	if (GInsertStmt != nullptr)
	{
		sqlite3_finalize(GInsertStmt);
		GInsertStmt = nullptr;
	}
	if (GDb != nullptr)
	{
		sqlite3_close(GDb);
		GDb = nullptr;
		std::printf("[채팅로그] 종료. 기록 %llu줄, 유실 %llu줄\n",
		            static_cast<unsigned long long>(GWritten.load()),
		            static_cast<unsigned long long>(GDropped.load()));
	}
}

uint64_t GetWrittenCount() { return GWritten.load(std::memory_order_relaxed); }
uint64_t GetDroppedCount() { return GDropped.load(std::memory_order_relaxed); }

} // namespace ChatLog
} // namespace MOU
