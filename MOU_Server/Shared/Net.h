// 플랫폼별 소켓 API 차이를 흡수하는 얇은 래퍼.
#pragma once

#ifdef _WIN32

	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#pragma comment(lib, "ws2_32.lib")

	namespace MOU
	{
		using SocketHandle = SOCKET;
		inline constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

		inline bool NetInit()     { WSADATA Data; return ::WSAStartup(MAKEWORD(2, 2), &Data) == 0; }
		inline void NetShutdown() { ::WSACleanup(); }
		inline void CloseSocket(SocketHandle Sock) { ::closesocket(Sock); }
		inline int  LastNetError() { return ::WSAGetLastError(); }

		// 보낼 데이터를 다 내보낸 뒤 FIN 을 보낸다. 수신은 계속 열어둔다.
		// 다른 스레드가 recv() 에 블록된 소켓을 곧바로 closesocket() 하면
		// 아직 나가지 않은 송신 데이터가 버려질 수 있다.
		inline void ShutdownSend(SocketHandle Sock) { ::shutdown(Sock, SD_SEND); }

		inline bool SetRecvTimeout(SocketHandle Sock, int Milliseconds)
		{
			const DWORD Timeout = static_cast<DWORD>(Milliseconds);
			return ::setsockopt(Sock, SOL_SOCKET, SO_RCVTIMEO,
			                    reinterpret_cast<const char*>(&Timeout), sizeof(Timeout)) == 0;
		}

		inline bool IsRecvTimeout(int ErrorCode) { return ErrorCode == WSAETIMEDOUT; }
	}

#else

	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <unistd.h>
	#include <cerrno>

	namespace MOU
	{
		using SocketHandle = int;
		inline constexpr SocketHandle kInvalidSocket = -1;

		inline bool NetInit()     { return true; }
		inline void NetShutdown() {}
		inline void CloseSocket(SocketHandle Sock) { ::close(Sock); }
		inline int  LastNetError() { return errno; }

		// 보낼 데이터를 다 내보낸 뒤 FIN 을 보낸다. 수신은 계속 열어둔다.
		inline void ShutdownSend(SocketHandle Sock) { ::shutdown(Sock, SHUT_WR); }

		inline bool SetRecvTimeout(SocketHandle Sock, int Milliseconds)
		{
			timeval Timeout{};
			Timeout.tv_sec  = Milliseconds / 1000;
			Timeout.tv_usec = (Milliseconds % 1000) * 1000;
			return ::setsockopt(Sock, SOL_SOCKET, SO_RCVTIMEO, &Timeout, sizeof(Timeout)) == 0;
		}

		inline bool IsRecvTimeout(int ErrorCode)
		{
			return ErrorCode == EAGAIN || ErrorCode == EWOULDBLOCK;
		}
	}

#endif
