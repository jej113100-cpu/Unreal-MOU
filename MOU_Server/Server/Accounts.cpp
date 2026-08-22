#include "Accounts.h"

#include "ChatProtocol.h"
#include "Crypto.h"
#include "sqlite3.h"

#include <cstdio>
#include <mutex>
#include <vector>

namespace MOU
{
namespace Accounts
{
namespace
{
	sqlite3*   GDb = nullptr;
	std::mutex GMutex;   // 여러 클라이언트 스레드가 동시에 로그인할 수 있다

	bool Exec(const char* Sql)
	{
		char* ErrMsg = nullptr;
		if (sqlite3_exec(GDb, Sql, nullptr, nullptr, &ErrMsg) != SQLITE_OK)
		{
			std::printf("[계정] SQL 실패: %s\n", ErrMsg ? ErrMsg : "?");
			sqlite3_free(ErrMsg);
			return false;
		}
		return true;
	}

	// 길이 규칙 검사. 프로토콜의 상수를 그대로 쓴다.
	bool IsFormatValid(const std::string& LoginId, const std::string& Password)
	{
		if (LoginId.size()  < kMinLoginIdLen  || LoginId.size()  >= kMaxLoginIdLen)  return false;
		if (Password.size() < kMinPasswordLen || Password.size() >= kMaxPasswordLen) return false;
		return true;
	}

	// 비밀번호를 솔트와 함께 해시한다.
	void HashPassword(const std::string& Password, const uint8_t* Salt, uint8_t* OutHash)
	{
		Crypto::Pbkdf2HmacSha256(
			reinterpret_cast<const uint8_t*>(Password.data()), Password.size(),
			Salt, Crypto::kSaltSize,
			Crypto::kPbkdf2Iterations, OutHash);
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
		std::printf("[계정] DB 열기 실패: %s\n", sqlite3_errmsg(GDb));
		sqlite3_close(GDb);
		GDb = nullptr;
		return false;
	}

	// 채팅 로그와 같은 파일을 공유할 수 있으므로 여기서도 WAL 로 맞춘다.
	// 계정은 유실되면 안 되므로 synchronous 는 FULL 로 둔다(로그와 다른 점).
	Exec("PRAGMA journal_mode=WAL;");
	Exec("PRAGMA synchronous=FULL;");
	// 다른 커넥션이 쓰는 중이면 즉시 실패하지 말고 5초까지 기다린다.
	sqlite3_busy_timeout(GDb, 5000);

	const char* kSchema =
		"CREATE TABLE IF NOT EXISTS accounts("
		"  id        INTEGER PRIMARY KEY AUTOINCREMENT,"   // 이게 곧 UserId 다
		"  login_id  TEXT    NOT NULL UNIQUE COLLATE NOCASE,"  // 대소문자 구분 없이 유일
		"  pw_salt   TEXT    NOT NULL,"                    // 16진 문자열
		"  pw_hash   TEXT    NOT NULL,"                    // 16진 문자열. 평문 비번은 어디에도 없다
		"  nickname  TEXT    NOT NULL,"
		"  created_at INTEGER NOT NULL"
		");";

	if (!Exec(kSchema))
	{
		sqlite3_close(GDb);
		GDb = nullptr;
		return false;
	}

	std::printf("[계정] %s 준비 완료\n", DbPath);
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

EAccountResult Create(const std::string& LoginId, const std::string& Password,
                      const std::string& Nickname, uint64_t& OutUserId)
{
	if (!IsFormatValid(LoginId, Password))
	{
		return EAccountResult::InvalidFormat;
	}

	std::lock_guard<std::mutex> Lock(GMutex);
	if (GDb == nullptr)
	{
		return EAccountResult::DbError;
	}

	uint8_t Salt[Crypto::kSaltSize];
	Crypto::RandomBytes(Salt, sizeof(Salt));

	uint8_t Hash[Crypto::kSha256Size];
	HashPassword(Password, Salt, Hash);

	const std::string SaltHex = Crypto::ToHex(Salt, sizeof(Salt));
	const std::string HashHex = Crypto::ToHex(Hash, sizeof(Hash));

	// 닉네임이 비어 있으면 아이디를 그대로 쓴다.
	const std::string FinalNick = Nickname.empty() ? LoginId : Nickname;

	sqlite3_stmt* St = nullptr;
	const char* Sql =
		"INSERT INTO accounts(login_id, pw_salt, pw_hash, nickname, created_at)"
		" VALUES(?,?,?,?, strftime('%s','now'));";

	if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
	{
		std::printf("[계정] INSERT 준비 실패: %s\n", sqlite3_errmsg(GDb));
		return EAccountResult::DbError;
	}

	sqlite3_bind_text(St, 1, LoginId.c_str(),   static_cast<int>(LoginId.size()),   SQLITE_TRANSIENT);
	sqlite3_bind_text(St, 2, SaltHex.c_str(),   static_cast<int>(SaltHex.size()),   SQLITE_TRANSIENT);
	sqlite3_bind_text(St, 3, HashHex.c_str(),   static_cast<int>(HashHex.size()),   SQLITE_TRANSIENT);
	sqlite3_bind_text(St, 4, FinalNick.c_str(), static_cast<int>(FinalNick.size()), SQLITE_TRANSIENT);

	const int Step = sqlite3_step(St);
	sqlite3_finalize(St);

	if (Step != SQLITE_DONE)
	{
		// UNIQUE 제약 위반 = 이미 있는 아이디. 이건 정상적인 실패다.
		if (sqlite3_extended_errcode(GDb) == SQLITE_CONSTRAINT_UNIQUE)
		{
			return EAccountResult::DuplicateId;
		}
		std::printf("[계정] INSERT 실패: %s\n", sqlite3_errmsg(GDb));
		return EAccountResult::DbError;
	}

	OutUserId = static_cast<uint64_t>(sqlite3_last_insert_rowid(GDb));
	return EAccountResult::Success;
}

EAccountResult Authenticate(const std::string& LoginId, const std::string& Password,
                            uint64_t& OutUserId, std::string& OutNickname)
{
	if (!IsFormatValid(LoginId, Password))
	{
		return EAccountResult::InvalidFormat;
	}

	std::lock_guard<std::mutex> Lock(GMutex);
	if (GDb == nullptr)
	{
		return EAccountResult::DbError;
	}

	sqlite3_stmt* St = nullptr;
	const char* Sql = "SELECT id, pw_salt, pw_hash, nickname FROM accounts WHERE login_id = ?;";
	if (sqlite3_prepare_v2(GDb, Sql, -1, &St, nullptr) != SQLITE_OK)
	{
		std::printf("[계정] SELECT 준비 실패: %s\n", sqlite3_errmsg(GDb));
		return EAccountResult::DbError;
	}
	sqlite3_bind_text(St, 1, LoginId.c_str(), static_cast<int>(LoginId.size()), SQLITE_TRANSIENT);

	if (sqlite3_step(St) != SQLITE_ROW)
	{
		sqlite3_finalize(St);
		return EAccountResult::NotFound;
	}

	const uint64_t Id       = static_cast<uint64_t>(sqlite3_column_int64(St, 0));
	const char*    SaltText = reinterpret_cast<const char*>(sqlite3_column_text(St, 1));
	const char*    HashText = reinterpret_cast<const char*>(sqlite3_column_text(St, 2));
	const char*    NickText = reinterpret_cast<const char*>(sqlite3_column_text(St, 3));

	const std::string SaltHex  = SaltText ? SaltText : "";
	const std::string HashHex  = HashText ? HashText : "";
	const std::string Nickname = NickText ? NickText : "";
	sqlite3_finalize(St);

	uint8_t Salt[Crypto::kSaltSize];
	uint8_t Stored[Crypto::kSha256Size];
	if (!Crypto::FromHex(SaltHex, Salt, sizeof(Salt)) ||
	    !Crypto::FromHex(HashHex, Stored, sizeof(Stored)))
	{
		std::printf("[계정] 저장된 해시 형식이 깨졌다: login_id=%s\n", LoginId.c_str());
		return EAccountResult::DbError;
	}

	uint8_t Computed[Crypto::kSha256Size];
	HashPassword(Password, Salt, Computed);

	// memcmp 대신 상수 시간 비교. 타이밍으로 정답을 좁혀가는 공격을 막는다.
	if (!Crypto::ConstantTimeEquals(Computed, Stored, Crypto::kSha256Size))
	{
		return EAccountResult::WrongPassword;
	}

	OutUserId   = Id;
	OutNickname = Nickname;
	return EAccountResult::Success;
}

} // namespace Accounts
} // namespace MOU
