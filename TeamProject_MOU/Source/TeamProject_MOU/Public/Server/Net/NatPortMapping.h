//NAT 포트포워딩 서버 관련 직접 구현
//Steam Lobby 나 EOS의 NAT의 시스템을 이해하기 위한 직접 프로그래밍
//구현 성공시 Steam Lobby 와 EOS 를 사용하지 않고 직접 사용.
//구현 실패 또는 접속 실패시 자동적으로 Steam Lobby나 EOS 를 사용하게 변환
#pragma once

#include "CoreMinimal.h"

// ─────────────────────────────────────────────────────────────────────
// 이 파일이 시스템 어디에 있는가
//
//   [게임 스레드] ULobbyWidgetBase / UServerSubsystem
//        │   방을 만들기 직전에 매핑을 시도한다
//        ▼
//   [워커 스레드] FRunnable            ← 블로킹 I/O 는 반드시 여기서
//        │
//        ▼
//   FNatPortMapping (이 파일)  ──SSDP/SOAP──▶  [공유기 (IGD)]
//
// ★ 대화 상대가 Server.exe 가 아니라 "공유기" 다.
//   그래서 Framing / ChatFraming 처럼 서버 쪽에 같은 것을 한 벌 더 만들 필요가
//   없다. 프레이밍이 두 벌인 이유는 서버와 클라이언트가 같은 바이트를 주고받아야
//   해서인데(SERVER_INTEGRATION.md 6절), 포트 매핑에는 우리 코드끼리 합의할
//   wire format 자체가 없다. MOU_Server 에는 대응 파일이 없는 것이 정상이다.
//
// 어디와 정보를 주고받는가
//   · 공유기 — SSDP(UDP 멀티캐스트)로 찾고, SOAP(HTTP)로 매핑을 요청한다
//   · Server.exe — 직접 통신하지 않는다. 결과(외부 포트)만 CreateRoom 에 실린다
//
// 수정 시 같이 봐야 하는 곳
//   · 외부 포트가 내부 포트와 달라지면 CreateRoom(..., HostPort) 에 "외부"
//     포트를 넘겨야 한다. 리슨서버는 내부 포트 그대로 돌아도 된다
//     → ServerSubsystem.h / LobbyWidgetBase.h. 프로토콜과 서버 코드는 안 바뀐다
//   · Build.cs — HTTP / XmlParser 모듈이 필요해지면 추가 (팀 공용 파일)
//
// ★ 플랫폼 헤더(winsock 등)를 이 헤더에 두지 말 것
//   <WinSock2.h> 는 <windows.h> 를 끌고 오고, 그것이 언리얼 매크로(TEXT,
//   GetObject 등)와 충돌한다. 실제로 이 충돌 때문에 Shared/Framing.h 를
//   언리얼에서 include 하지 못하고 ChatFraming 으로 재구현했다.
//   구현에 필요한 헤더는 전부 .cpp 에 둔다 — 자세한 것은 NatPortMapping.cpp 상단.
//
// ★ 소켓과 스레드는 언리얼 것을 쓴다
//   소켓 : ISocketSubsystem / FSocket   (Build.cs 에 Sockets, Networking 있음)
//   스레드: FRunnable                    (std::thread 아님)
//   FServerClientRunnable 이 같은 구조다. 종료를 Kill(true) 로 기다리지 않으면
//   PIE 를 껐다 켤 때 에디터가 통째로 죽는다.
// ─────────────────────────────────────────────────────────────────────

/*실패 사유 각각 UI 안내와 풀백 판단이 달라서 나눠서 정리*/
enum class ENatMapResult : uint8
{
	Success,
	NoGatewayFound, //.SSDP 무응답 -  UPnP 미지원이거나 라우터에서 꺼둠
	CarrierGradeNat,// 라우터의 외부 IP 자체가 사설대역 - UPnP로는 못푸는 문제
	PortConflict, // 다른 기기가 그 외부 포트를 이미 쓰는중 (UPnP 에러 718)
	GatewayRefused, //라우터가 거부(권한 등)
	NetworkError,
	Timeout,
	Unknown
};

/* 해제할 때 필요한 정보. 재탐색  없이 바로 Delegate하려고 ControlUrl을 들고 있다.*/
struct FNatMappingHandle
{
	uint16 ExternalPort = 0;
	uint16 InternalPort = 0;
	bool bUdp = true;
	FString ControlUrl;
	FString ServiceType;

	bool IsValid() const { return ExternalPort != 0;}
};
class FNatPortMapping
{
public:
	// 라우터 찾기
	/*SSDP M-RESEARCH 를 239.255.255.250:1900 으로 쏘고 IGD 응답을 기다린다.
	 성공하면 ControlUrl/ServiceType을 내부에 보관. 블로킹*/
	ENatMapResult DiscoverGateway(float TimeoutSeconds = 3.0f);

	//CGNAT 판별
	/* SOAP GetExternalIPAddress. 돌려받은 IP가 사설 대역이면 CGNAT이라
	매핑이 성공해도 외부에서 못 붙음 매핑 전에 확인*/
	ENatMapResult GetExternalIP(FString& OutIP);

	//매핑 추가
	ENatMapResult AddMapping(uint16 InternalPort,
		uint16 DesiredExternalPort,
		bool bUdp,
		const FString& Description,
		uint32 LeaseSeconds,
		FNatMappingHandle& OutHandle);

	//갱신 (Lease 쓸때만)
	ENatMapResult RefreshMaping(const FNatMappingHandle& Handle, uint32 LeaseSeconds);

	//해제
	/*방을 나가거나 게임을 끌때 반드시 호출. 안하면 라우터에 매핑이 영구히 남는다.*/
	ENatMapResult DeleteMapping(const FNatMappingHandle& Handle);

	//사설대역 (10/8. 172.16/12, 192/168/16, 100.64/10=CGNAT) 판별
	static bool IsPrivateAddress(const FString& Ip);

private:
	ENatMapResult SendSsdpMSearch(FString& OutLocationUrl, float TimeoutSeconds);
	// 디바이스 설명 XML → WANIPConnection/WANPPPConnection 의 제어 URL 추출
	ENatMapResult FetchDeviceDescription(const FString& LocationUrl);
	ENatMapResult ParseControlUrl(const FString& Xml);
	// SOAP 공통 송신 + UPnP 에러코드 파싱
	ENatMapResult SendSoapAction(const FString& ActionName,
		const FString& ArgsXml,
		FString& OutResponseBody);
	/** NewInternalClient 에 넣을 내 LAN IP. NIC 가 여러 개일 때
	 *  게이트웨이로 가는 경로의 IP 를 골라야 한다 — 아무거나 넣으면 라우터가 거부한다. */
	FString GetLocalIpForGateway() const;

	FString ControlUrl;
	FString ServiceType;   // WANIPConnection:1 / WANPPPConnection:1
};

