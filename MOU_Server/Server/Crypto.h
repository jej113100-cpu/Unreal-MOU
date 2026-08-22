// MOU 채팅 서버 - 비밀번호 해시용 최소 암호 유틸.
//
// [왜 직접 구현하는가]
//   OpenSSL 같은 라이브러리를 붙이면 팀원 전원이 빌드 환경을 맞춰야 한다.
//   윈도우 전용 CNG(bcrypt.h)를 쓰면 서버가 윈도우에 묶인다(Net.h 는 POSIX 분기를 갖고 있다).
//   필요한 게 SHA-256 하나뿐이라 표준 C++ 만으로 자체 구현했다.
//
// [왜 SHA-256 한 번이 아니라 PBKDF2 인가]
//   SHA-256 은 빠르라고 만든 함수다. GPU 로 초당 수십억 번 시도할 수 있어서
//   비밀번호를 그대로 해시하면 사전 공격에 사실상 무방비다.
//   PBKDF2 는 같은 해시를 수만 번 반복해서 "느리게" 만든다.
//   솔트는 같은 비밀번호가 같은 해시로 저장되는 것을 막는다(레인보우 테이블 방어).
//
// [한계 — 알고 쓸 것]
//   실무 표준은 Argon2id 나 bcrypt 다. PBKDF2 는 GPU 내성이 그들보다 약하다.
//   다만 "평문 저장" 이나 "솔트 없는 SHA-256" 과는 격이 다르고,
//   이 프로젝트 규모에서는 충분한 선택이다.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace MOU
{
	namespace Crypto
	{
		/** SHA-256 다이제스트 크기(바이트). */
		constexpr size_t kSha256Size = 32;

		/** 저장에 쓰는 솔트 크기(바이트). 16바이트면 충돌 걱정이 없다. */
		constexpr size_t kSaltSize = 16;

		/**
		 * PBKDF2 반복 횟수.
		 *
		 * 크면 안전하지만 로그인 1회당 서버 CPU 를 그만큼 쓴다.
		 * 10만 회면 최신 PC 에서 수십 ms 수준이라, 로그인 빈도를 생각하면 무난하다.
		 *
		 * >> 이 값을 바꾸면 기존 계정은 로그인할 수 없게 된다. <<
		 *    이미 저장된 해시는 옛 반복 횟수로 만들어졌기 때문이다.
		 *    바꿔야 한다면 accounts 테이블에 iterations 컬럼을 두고 계정마다 기록해야 한다.
		 */
		constexpr uint32_t kPbkdf2Iterations = 100000;

		/** 임의 바이트를 채운다. 솔트 생성에 쓴다. */
		void RandomBytes(uint8_t* Out, size_t Len);

		/** 바이트열 -> 소문자 16진 문자열. DB 에 TEXT 로 넣기 위한 것이다. */
		std::string ToHex(const uint8_t* Data, size_t Len);

		/** 16진 문자열 -> 바이트열. 형식이 잘못되면 false. */
		bool FromHex(const std::string& Hex, uint8_t* Out, size_t OutLen);

		/** SHA-256. Out 은 kSha256Size 바이트여야 한다. */
		void Sha256(const uint8_t* Data, size_t Len, uint8_t* Out);

		/**
		 * PBKDF2-HMAC-SHA256. Out 은 kSha256Size 바이트여야 한다.
		 * 출력 길이를 해시 크기로 고정했다(dkLen = hLen 이라 블록이 하나뿐이다).
		 */
		void Pbkdf2HmacSha256(const uint8_t* Password, size_t PasswordLen,
		                      const uint8_t* Salt, size_t SaltLen,
		                      uint32_t Iterations, uint8_t* Out);

		/**
		 * 두 바이트열을 상수 시간에 비교한다.
		 *
		 * memcmp 는 다른 바이트를 만나면 즉시 반환해서, 걸린 시간으로
		 * "앞에서 몇 바이트나 맞았는지" 를 추측당할 수 있다(타이밍 공격).
		 * 비밀 값 비교에는 반드시 이 함수를 쓴다.
		 */
		bool ConstantTimeEquals(const uint8_t* A, const uint8_t* B, size_t Len);
	}
}
