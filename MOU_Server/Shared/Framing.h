// 길이 프리픽스 프레이밍.
//
// TCP 는 바이트 스트림이라 send() 한 번이 recv() 한 번으로 오지 않는다.
// 두 패킷이 붙어서 오기도 하고(합침), 하나가 쪼개져 오기도 한다(분할).
// 그래서 PacketHeader.BodySize 를 보고 직접 경계를 잘라야 한다.
#pragma once

#include "ChatProtocol.h"
#include "Net.h"

#include <string>
#include <vector>

namespace MOU
{
	enum class EFrameResult : uint8_t
	{
		Ok,          // 완성된 패킷 하나를 꺼냈다
		NeedMore,    // 아직 덜 왔다. 다음 recv 를 기다린다
		Malformed,   // BodySize 가 허용치를 넘었다. 연결을 끊어야 한다
	};

	// send() 는 요청한 길이만큼 다 보내지 않을 수 있으므로 전부 나갈 때까지 반복한다.
	bool SendAll(SocketHandle Sock, const char* Data, int32_t Len);

	// 헤더 + 바디를 하나의 버퍼로 합쳐 한 번에 보낸다.
	// 헤더와 바디를 따로 send 하면 그 사이에 다른 스레드의 send 가 끼어들 수 있다.
	bool SendPacket(SocketHandle Sock, EOpcode Op, const void* Body, uint32_t BodySize);

	// 바디가 두 조각인 경우 (고정부 + 가변 길이 텍스트).
	bool SendPacket2(SocketHandle Sock, EOpcode Op,
	                 const void* BodyA, uint32_t SizeA,
	                 const void* BodyB, uint32_t SizeB);

	// Buffer 앞쪽에서 완성된 패킷 하나를 꺼내 OutHeader / OutBody 에 담고,
	// 꺼낸 만큼 Buffer 앞부분을 지운다.
	// Ok 가 반환되는 동안 반복 호출해서 밀린 패킷을 전부 처리해야 한다.
	EFrameResult TryExtractPacket(std::vector<char>& Buffer,
	                              PacketHeader& OutHeader,
	                              std::vector<char>& OutBody);

	// 고정 길이 char 배열에 문자열을 담는다. 항상 널 종료를 보장한다.
	void CopyFixedString(char* Dest, size_t DestSize, const std::string& Src);

	// 클라이언트가 널 종료를 빼먹었을 수 있으므로 길이를 제한해서 읽는다.
	std::string ReadFixedString(const char* Src, size_t MaxSize);
}
