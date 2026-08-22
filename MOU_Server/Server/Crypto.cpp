#include "Crypto.h"

#include <cstring>
#include <random>

namespace MOU
{
namespace Crypto
{
namespace
{
	// ------------------------------------------------------------------
	// SHA-256 (FIPS 180-4)
	//
	// 표준 문서의 의사코드를 그대로 옮긴 것이다.
	// 성능보다 "읽고 표준과 대조할 수 있는 것" 을 우선했다.
	// ------------------------------------------------------------------

	// 처음 64개 소수의 세제곱근 소수부 앞 32비트.
	const uint32_t K[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
	};

	inline uint32_t Rotr(uint32_t X, uint32_t N) { return (X >> N) | (X << (32 - N)); }

	void Sha256Compress(uint32_t State[8], const uint8_t Block[64])
	{
		uint32_t W[64];

		// 앞 16워드는 입력 블록을 빅엔디안으로 읽은 것이다.
		// 프로토콜 본문과 달리 해시는 표준이 빅엔디안으로 못박아서 직접 조립한다.
		for (int i = 0; i < 16; ++i)
		{
			W[i] = (static_cast<uint32_t>(Block[i * 4    ]) << 24)
			     | (static_cast<uint32_t>(Block[i * 4 + 1]) << 16)
			     | (static_cast<uint32_t>(Block[i * 4 + 2]) <<  8)
			     | (static_cast<uint32_t>(Block[i * 4 + 3]));
		}
		for (int i = 16; i < 64; ++i)
		{
			const uint32_t S0 = Rotr(W[i - 15], 7) ^ Rotr(W[i - 15], 18) ^ (W[i - 15] >> 3);
			const uint32_t S1 = Rotr(W[i - 2], 17) ^ Rotr(W[i - 2],  19) ^ (W[i - 2]  >> 10);
			W[i] = W[i - 16] + S0 + W[i - 7] + S1;
		}

		uint32_t a = State[0], b = State[1], c = State[2], d = State[3];
		uint32_t e = State[4], f = State[5], g = State[6], h = State[7];

		for (int i = 0; i < 64; ++i)
		{
			const uint32_t S1    = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
			const uint32_t Ch    = (e & f) ^ (~e & g);
			const uint32_t Temp1 = h + S1 + Ch + K[i] + W[i];
			const uint32_t S0    = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
			const uint32_t Maj   = (a & b) ^ (a & c) ^ (b & c);
			const uint32_t Temp2 = S0 + Maj;

			h = g; g = f; f = e; e = d + Temp1;
			d = c; c = b; b = a; a = Temp1 + Temp2;
		}

		State[0] += a; State[1] += b; State[2] += c; State[3] += d;
		State[4] += e; State[5] += f; State[6] += g; State[7] += h;
	}
}

void Sha256(const uint8_t* Data, size_t Len, uint8_t* Out)
{
	// 처음 8개 소수의 제곱근 소수부 앞 32비트.
	uint32_t State[8] = {
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
		0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
	};

	size_t Offset = 0;
	for (; Offset + 64 <= Len; Offset += 64)
	{
		Sha256Compress(State, Data + Offset);
	}

	// 패딩: 남은 바이트 + 0x80 + 0 채움 + 원본 비트길이(64비트 빅엔디안).
	uint8_t Tail[128] = {};
	const size_t Remain = Len - Offset;
	std::memcpy(Tail, Data + Offset, Remain);
	Tail[Remain] = 0x80;

	// 길이 필드(8바이트)가 들어갈 자리가 남는지에 따라 블록이 1개 또는 2개가 된다.
	const size_t TailBlocks = (Remain + 1 + 8 <= 64) ? 64 : 128;
	const uint64_t BitLen = static_cast<uint64_t>(Len) * 8;
	for (int i = 0; i < 8; ++i)
	{
		Tail[TailBlocks - 1 - i] = static_cast<uint8_t>((BitLen >> (i * 8)) & 0xFF);
	}

	Sha256Compress(State, Tail);
	if (TailBlocks == 128)
	{
		Sha256Compress(State, Tail + 64);
	}

	for (int i = 0; i < 8; ++i)
	{
		Out[i * 4    ] = static_cast<uint8_t>((State[i] >> 24) & 0xFF);
		Out[i * 4 + 1] = static_cast<uint8_t>((State[i] >> 16) & 0xFF);
		Out[i * 4 + 2] = static_cast<uint8_t>((State[i] >>  8) & 0xFF);
		Out[i * 4 + 3] = static_cast<uint8_t>((State[i]      ) & 0xFF);
	}
}

namespace
{
	// HMAC-SHA256 (RFC 2104). 블록 크기는 SHA-256 기준 64바이트.
	void HmacSha256(const uint8_t* Key, size_t KeyLen,
	                const uint8_t* Data, size_t DataLen, uint8_t* Out)
	{
		constexpr size_t kBlock = 64;

		uint8_t KeyBlock[kBlock] = {};
		if (KeyLen > kBlock)
		{
			// 키가 블록보다 길면 먼저 해시해서 줄인다.
			Sha256(Key, KeyLen, KeyBlock);
		}
		else
		{
			std::memcpy(KeyBlock, Key, KeyLen);
		}

		uint8_t Inner[kBlock], Outer[kBlock];
		for (size_t i = 0; i < kBlock; ++i)
		{
			Inner[i] = static_cast<uint8_t>(KeyBlock[i] ^ 0x36);
			Outer[i] = static_cast<uint8_t>(KeyBlock[i] ^ 0x5c);
		}

		// H(Outer || H(Inner || Data))
		// Sha256 이 스트리밍 API 가 아니라 이어붙인 버퍼를 만들어 넘긴다.
		std::string InnerBuf;
		InnerBuf.resize(kBlock + DataLen);
		std::memcpy(&InnerBuf[0], Inner, kBlock);
		if (DataLen > 0)
		{
			std::memcpy(&InnerBuf[kBlock], Data, DataLen);
		}

		uint8_t InnerHash[kSha256Size];
		Sha256(reinterpret_cast<const uint8_t*>(InnerBuf.data()), InnerBuf.size(), InnerHash);

		uint8_t OuterBuf[kBlock + kSha256Size];
		std::memcpy(OuterBuf, Outer, kBlock);
		std::memcpy(OuterBuf + kBlock, InnerHash, kSha256Size);

		Sha256(OuterBuf, sizeof(OuterBuf), Out);
	}
}

void Pbkdf2HmacSha256(const uint8_t* Password, size_t PasswordLen,
                      const uint8_t* Salt, size_t SaltLen,
                      uint32_t Iterations, uint8_t* Out)
{
	// dkLen == hLen 이므로 블록 인덱스는 1 하나뿐이다.
	// U1 = HMAC(P, S || INT(1)), Ui = HMAC(P, Ui-1), 결과 = U1 xor U2 xor ...
	std::string First;
	First.resize(SaltLen + 4);
	if (SaltLen > 0)
	{
		std::memcpy(&First[0], Salt, SaltLen);
	}
	First[SaltLen + 0] = 0;
	First[SaltLen + 1] = 0;
	First[SaltLen + 2] = 0;
	First[SaltLen + 3] = 1;

	uint8_t U[kSha256Size];
	HmacSha256(Password, PasswordLen,
	           reinterpret_cast<const uint8_t*>(First.data()), First.size(), U);

	uint8_t Result[kSha256Size];
	std::memcpy(Result, U, kSha256Size);

	for (uint32_t Iter = 1; Iter < Iterations; ++Iter)
	{
		HmacSha256(Password, PasswordLen, U, kSha256Size, U);
		for (size_t i = 0; i < kSha256Size; ++i)
		{
			Result[i] = static_cast<uint8_t>(Result[i] ^ U[i]);
		}
	}

	std::memcpy(Out, Result, kSha256Size);
}

void RandomBytes(uint8_t* Out, size_t Len)
{
	// MSVC 의 random_device 는 OS 의 암호학적 난수를 쓴다.
	// mt19937 같은 의사난수로 솔트를 만들면 예측 가능해져서 솔트의 의미가 없다.
	static thread_local std::random_device Rd;
	for (size_t i = 0; i < Len; ++i)
	{
		Out[i] = static_cast<uint8_t>(Rd() & 0xFF);
	}
}

std::string ToHex(const uint8_t* Data, size_t Len)
{
	static const char* Digits = "0123456789abcdef";
	std::string Out;
	Out.resize(Len * 2);
	for (size_t i = 0; i < Len; ++i)
	{
		Out[i * 2    ] = Digits[(Data[i] >> 4) & 0xF];
		Out[i * 2 + 1] = Digits[Data[i] & 0xF];
	}
	return Out;
}

bool FromHex(const std::string& Hex, uint8_t* Out, size_t OutLen)
{
	if (Hex.size() != OutLen * 2)
	{
		return false;
	}
	auto Nibble = [](char C, uint8_t& Value) -> bool
	{
		if (C >= '0' && C <= '9') { Value = static_cast<uint8_t>(C - '0');      return true; }
		if (C >= 'a' && C <= 'f') { Value = static_cast<uint8_t>(C - 'a' + 10); return true; }
		if (C >= 'A' && C <= 'F') { Value = static_cast<uint8_t>(C - 'A' + 10); return true; }
		return false;
	};
	for (size_t i = 0; i < OutLen; ++i)
	{
		uint8_t Hi = 0, Lo = 0;
		if (!Nibble(Hex[i * 2], Hi) || !Nibble(Hex[i * 2 + 1], Lo))
		{
			return false;
		}
		Out[i] = static_cast<uint8_t>((Hi << 4) | Lo);
	}
	return true;
}

bool ConstantTimeEquals(const uint8_t* A, const uint8_t* B, size_t Len)
{
	// 다른 바이트를 만나도 끝까지 돈다. 걸리는 시간이 내용과 무관해야 한다.
	uint8_t Diff = 0;
	for (size_t i = 0; i < Len; ++i)
	{
		Diff = static_cast<uint8_t>(Diff | (A[i] ^ B[i]));
	}
	return Diff == 0;
}

} // namespace Crypto
} // namespace MOU
