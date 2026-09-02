// MOU 서버 - Server.exe 자신의 리슨 포트를 공유기에 열어주는 UPnP 클라이언트.
//
// [왜 서버 쪽에도 있는가]
//   언리얼 클라이언트에도 같은 이름의 파일이 있다
//   (TeamProject_MOU/.../Public/Server/Net/NatPortMapping.h).
//   프로토콜(SSDP/SOAP)은 같지만 **여는 포트가 다르다.**
//
//     언리얼 쪽 : 방장의 리슨서버 포트(7777, UDP). 게임 트래픽이 들어온다
//     여기(서버): Server.exe 의 리슨 포트(9000, TCP). 로그인/채팅/방목록이 들어온다
//
//   포트를 여는 코드는 **그 포트를 실제로 리스닝하는 프로세스**에 있어야 한다.
//   언리얼 클라이언트가 9000 을 대신 열어주면, Server.exe 를 다른 PC 로 옮기는
//   순간 엉뚱한 기기의 포트를 여는 코드가 된다.
//
//   Framing.cpp / ChatFraming.cpp 가 두 벌인 것과 같은 이유로 구현도 두 벌이다 —
//   여기는 언리얼 엔진이 없어 FSocket 을 쓸 수 없고, 저쪽은 winsock 을 직접
//   include 하면 언리얼 매크로와 충돌한다. (SERVER_INTEGRATION.md 6절)
//
// [무엇을 못 푸는가]
//   공유기가 UPnP 를 지원하지 않거나, 통신사 NAT(CGNAT) 안이면 실패한다.
//   실패는 오류가 아니다 — 같은 네트워크에서는 어차피 접속되므로 서버는 그대로 뜬다.
//   CGNAT 은 UPnP 로 풀 수 없고 릴레이(EOS/Steam P2P)가 필요하다.
//
// [수명]
//   main() 에서 listen() 성공 직후 Start() 하고, 종료 핸들러에서 Stop() 한다.
//   Stop() 을 빠뜨리면 공유기에 매핑이 영구히 남는다.

#pragma once

#include <cstdint>
#include <string>

namespace MOU::Nat
{
	/** 실패 사유. 로그 문구와 운영자 판단이 달라져서 bool 로 뭉치지 않는다. */
	enum class EResult
	{
		Success,
		NoGatewayFound,   // SSDP 무응답 — UPnP 미지원이거나 공유기에서 꺼둠
		CarrierGradeNat,  // 공유기의 외부 IP 자체가 사설 대역 — UPnP 로는 못 푼다
		PortConflict,     // 다른 기기가 그 외부 포트를 이미 쓰는 중 (UPnP 오류 718)
		GatewayRefused,   // 공유기가 거부
		NetworkError,
		Timeout,
		Unknown,
	};

	/** 로그에 그대로 쓸 수 있는 한국어 문구. */
	const char* ResultText(EResult Result);

	/**
	 * 공유기에 포트를 열어달라고 요청한다. **블로킹이다** — 최대 십수 초 걸릴 수 있다.
	 * main() 에서 accept 루프에 들어가기 전에 한 번 부른다.
	 *
	 * 성공하면 열린 외부 포트를 내부에 기억해 두고, Stop() 이 그것을 지운다.
	 *
	 * @param Port 열려는 포트. Server.exe 의 리슨 포트를 그대로 준다
	 * @param bTcp Server.exe 는 TCP 다. 게임 리슨서버(UDP)와 다르니 주의
	 */
	EResult Start(uint16_t Port, bool bTcp = true);

	/**
	 * 열어둔 포트를 닫는다. 종료 시 반드시 부른다.
	 * Start() 가 실패했거나 아예 안 불렸어도 부르는 것이 안전하다(아무 일도 하지 않는다).
	 */
	void Stop();

	/** Start() 가 알아낸 공유기의 외부 IP. 실패했으면 빈 문자열. */
	const std::string& ExternalIp();

	/** 실제로 열린 외부 포트. 0 이면 열린 것이 없다. */
	uint16_t MappedExternalPort();
}
