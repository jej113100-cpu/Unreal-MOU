#include "Framing.h"

#include <algorithm>
#include <cstring>

namespace MOU
{
	bool SendAll(SocketHandle Sock, const char* Data, int32_t Len)
	{
		int32_t Sent = 0;
		while (Sent < Len)
		{
			const int Written = ::send(Sock, Data + Sent, Len - Sent, 0);
			if (Written <= 0)
			{
				return false;   // 연결이 끊겼거나 에러
			}
			Sent += Written;
		}
		return true;
	}

	bool SendPacket(SocketHandle Sock, EOpcode Op, const void* Body, uint32_t BodySize)
	{
		return SendPacket2(Sock, Op, Body, BodySize, nullptr, 0);
	}

	bool SendPacket2(SocketHandle Sock, EOpcode Op,
	                 const void* BodyA, uint32_t SizeA,
	                 const void* BodyB, uint32_t SizeB)
	{
		const uint32_t Total = SizeA + SizeB;
		if (Total > kMaxBodySize)
		{
			return false;
		}

		std::vector<char> Buffer(sizeof(PacketHeader) + Total);

		PacketHeader Header{};
		Header.BodySize = Total;
		Header.Opcode   = static_cast<uint16_t>(Op);
		std::memcpy(Buffer.data(), &Header, sizeof(Header));

		if (SizeA > 0 && BodyA != nullptr)
		{
			std::memcpy(Buffer.data() + sizeof(Header), BodyA, SizeA);
		}
		if (SizeB > 0 && BodyB != nullptr)
		{
			std::memcpy(Buffer.data() + sizeof(Header) + SizeA, BodyB, SizeB);
		}

		return SendAll(Sock, Buffer.data(), static_cast<int32_t>(Buffer.size()));
	}

	EFrameResult TryExtractPacket(std::vector<char>& Buffer,
	                              PacketHeader& OutHeader,
	                              std::vector<char>& OutBody)
	{
		// 1. 헤더조차 다 안 왔으면 더 기다린다.
		if (Buffer.size() < sizeof(PacketHeader))
		{
			return EFrameResult::NeedMore;
		}

		PacketHeader Header{};
		std::memcpy(&Header, Buffer.data(), sizeof(Header));

		// 2. 선언된 길이를 신뢰하기 전에 상한을 검사한다.
		//    이 검사가 없으면 악성 클라가 BodySize 에 큰 값을 넣어 메모리를 폭발시킬 수 있다.
		if (Header.BodySize > kMaxBodySize)
		{
			return EFrameResult::Malformed;
		}

		// 3. 바디가 덜 왔으면 더 기다린다. 지금까지 받은 건 버리지 않는다.
		const size_t Total = sizeof(PacketHeader) + Header.BodySize;
		if (Buffer.size() < Total)
		{
			return EFrameResult::NeedMore;
		}

		// 4. 딱 한 패킷만 꺼내고, 그만큼만 버퍼에서 지운다.
		//    뒤에 남은 바이트는 다음 패킷의 일부이므로 보존해야 한다.
		OutHeader = Header;
		OutBody.assign(Buffer.begin() + sizeof(PacketHeader), Buffer.begin() + Total);
		Buffer.erase(Buffer.begin(), Buffer.begin() + Total);

		return EFrameResult::Ok;
	}

	void CopyFixedString(char* Dest, size_t DestSize, const std::string& Src)
	{
		if (DestSize == 0)
		{
			return;
		}
		const size_t Count = std::min(Src.size(), DestSize - 1);
		std::memcpy(Dest, Src.data(), Count);
		Dest[Count] = '\0';
	}

	std::string ReadFixedString(const char* Src, size_t MaxSize)
	{
		size_t Len = 0;
		while (Len < MaxSize && Src[Len] != '\0')
		{
			++Len;
		}
		return std::string(Src, Len);
	}
}
