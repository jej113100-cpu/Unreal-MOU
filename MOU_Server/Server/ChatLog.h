// MOU 채팅 서버 - 채팅 로그 영속화 (6단계, SQLite)
//
// [왜 비동기인가]
//   RouteChat 은 클라이언트 스레드 위에서 돈다. 거기서 곧바로 sqlite3_step 을 부르면
//   디스크 쓰기(특히 커밋 시 fsync)가 끝날 때까지 그 스레드가 멈춘다.
//   RouteChat 은 SessionManager 락 안에서 브로드캐스트를 하므로,
//   결국 "한 명이 채팅 칠 때마다 서버 전체 채팅이 디스크 대기만큼 밀리는" 구조가 된다.
//   README 에 적힌 "send() 를 세션 락 안에서 부른다" 와 같은 종류의 문제를 하나 더 만드는 셈이다.
//
//   그래서 언리얼 클라이언트에서 쓴 것과 같은 방식으로 나눈다.
//     클라이언트 스레드 N개 --Enqueue--> [큐] --> DB 라이터 스레드 1개 --> chat_log.db
//   Enqueue 는 메모리 복사와 큐 push 만 하고 즉시 리턴하므로 채팅 지연이 생기지 않는다.
//
// [클라이언트 쪽 TQueue 와 다른 점]
//   언리얼의 TQueue<SPSC> 는 생산자가 하나일 때만 쓸 수 있다.
//   여기는 접속자마다 스레드가 하나씩 있어서 생산자가 여럿(MPSC)이다.
//   그래서 mutex + condition_variable 로 직접 만든다.
//
// [트레이드오프 — 알고 쓸 것]
//   큐에 있고 아직 커밋되지 않은 메시지는 서버가 비정상 종료하면 사라진다.
//   정상 종료(Stop) 시에는 남은 큐를 전부 비우고 닫으므로 유실이 없다.
//   채팅 로그 성격상 이 정도는 감수하고 지연을 택했다.

#pragma once

#include <cstdint>
#include <string>

namespace MOU
{
	namespace ChatLog
	{
		/**
		 * DB 를 열고 라이터 스레드를 띄운다. 서버 시작 시 한 번만 부른다.
		 *
		 * 실패해도 서버는 계속 돌아야 한다. 채팅 로그가 안 남는 것과
		 * 채팅 자체가 안 되는 것은 심각도가 다르기 때문이다.
		 * 실패 시 이후의 Enqueue 는 조용히 버려진다(Dropped 로 집계됨).
		 *
		 * @param DbPath  DB 파일 경로. 없으면 새로 만든다.
		 * @return 열기 성공 여부
		 */
		bool Start(const char* DbPath);

		/**
		 * 큐에 남은 것을 전부 쓰고 라이터 스레드를 정리한 뒤 DB 를 닫는다.
		 * Start 가 실패했더라도 부르는 것이 안전하다.
		 */
		void Stop();

		/**
		 * 채팅 한 줄을 기록 대기열에 넣는다. 호출자를 막지 않는다.
		 *
		 * Text 는 널 종료가 아닐 수 있으므로 길이를 따로 받는다
		 * (프로토콜상 ChatBroadcast 뒤에 붙는 UTF-8 본문이 그렇다).
		 * 내부에서 std::string 으로 복사하므로 호출자는 Text 수명을 신경쓰지 않아도 된다.
		 *
		 * 큐가 상한(kMaxQueuedEntries)에 닿으면 이 메시지를 버린다.
		 * 디스크가 느릴 때 메모리가 무한정 늘어나 서버가 죽는 것보다 낫다.
		 */
		void Enqueue(int64_t Timestamp, uint64_t SenderUserId, const std::string& SenderName,
		             uint8_t Channel, int32_t TeamId, const char* Text, uint16_t TextLen);

		/** 지금까지 실제로 DB 에 커밋된 줄 수. */
		uint64_t GetWrittenCount();

		/** 큐가 넘쳐서 버린 줄 수. 0 이 아니면 디스크가 못 따라가고 있다는 뜻이다. */
		uint64_t GetDroppedCount();
	}
}
