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

/**
 * 우리가 만든 매핑에 붙이는 설명. 공유기 목록에 이 문자열로 남는다.
 *
 * ★ 청소의 안전장치다. MOU.Nat.Clean 과 CleanupStaleMappings 는 "내 LAN IP 를
 *   가리키면서 **이 설명을 가진**" 항목만 지운다. 사용자가 공유기 관리페이지에서
 *   손으로 넣은 포워딩(설명이 다르다)을 실수로 지우지 않기 위해서다.
 *   여기를 고치면 지난 판이 남긴 매핑을 못 알아보게 되니 함부로 바꾸지 말 것.
 */
namespace MOUNat
{
	inline const TCHAR* const MappingDescription = TEXT("MOU");
}

/* 해제할 때 필요한 정보. 재탐색  없이 바로 Delegate하려고 ControlUrl을 들고 있다.*/
struct FNatMappingHandle
{
	uint16 ExternalPort = 0;
	uint16 InternalPort = 0;
	bool bUdp = true;
	FString ControlUrl;
	FString ServiceType;

	/** 갱신할 때 같은 값으로 다시 써야 한다. 안 그러면 공유기 목록의 설명이 바뀌고,
	 *  설명으로 소유자를 가리는 청소 로직이 그 항목을 못 알아본다. */
	FString Description;

	bool IsValid() const { return ExternalPort != 0;}
};

/**
 * 공유기에 **이미 등록돼 있는** 매핑 한 줄. (2026-08-28)
 *
 * [왜 필요한가]
 *   공용 네트워크에서는 공유기 관리페이지에 못 들어간다. 그런데 게임이 크래시로
 *   죽으면 DeleteMapping 이 안 불려서 매핑이 공유기에 그대로 남고, 다음 실행은
 *   그 포트를 피해 7778, 7779... 로 밀려간다. 남은 것을 볼 방법이 없으면
 *   왜 밀리는지도 알 수 없다.
 *
 *   UPnP 규격에는 조회 액션이 있다(GetGenericPortMappingEntry). 관리자 로그인이
 *   필요 없다. 관리페이지가 유일한 확인 수단이 아니다.
 *
 * ★ InternalClient 가 소유자 판정의 유일한 근거다.
 *   공용 네트워크에는 남의 기기 매핑도 같이 들어 있다. 설명("MOU")이나 포트 번호만
 *   보고 지우면 남의 서비스를 끊는다. 지우기 전에 반드시 이 값이 내 LAN IP 와
 *   같은지 확인할 것.
 */
struct FNatMappingEntry
{
	uint16  ExternalPort = 0;
	uint16  InternalPort = 0;
	bool    bUdp         = true;
	bool    bEnabled     = true;
	FString InternalClient;   // 이 매핑이 가리키는 LAN 기기. 소유자 판정용
	FString Description;      // 우리가 만든 것은 "MOU"
	uint32  LeaseSeconds = 0; // 0 이면 영구 매핑

	bool IsValid() const { return ExternalPort != 0; }
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

	// ── 조회 / 청소 (2026-08-28) ────────────────────────────────────
	/**
	 * 공유기의 매핑 테이블을 인덱스 0부터 끝까지 훑는다.
	 *
	 * 끝은 UPnP 오류 713(SpecifiedArrayIndexInvalid) 또는 714(NoSuchEntryInArray)로
	 * 알려준다. 그 둘은 실패가 아니라 "여기까지"라는 뜻이므로 Success 로 돌려준다.
	 *
	 * MaxEntries 는 안전장치다. 인덱스를 계속 올려도 답을 주는 고장난 공유기가
	 * 있어서, 없으면 무한 루프가 된다.
	 */
	ENatMapResult ListMappings(TArray<FNatMappingEntry>& OutEntries, int32 MaxEntries = 64);

	/** 외부 포트 하나만 조회. 매핑이 없으면 bOutFound=false 로 돌아온다(오류 아님). */
	ENatMapResult GetMappingFor(uint16 ExternalPort, bool bUdp,
		FNatMappingEntry& OutEntry, bool& bOutFound);

	/**
	 * 핸들 없이 (외부포트, 프로토콜) 만으로 지운다.
	 *
	 * ★ 남이 만든 매핑도 지울 수 있다. 부르기 전에 반드시
	 *   FNatMappingEntry::InternalClient == GetLocalLanIp() 를 확인할 것.
	 */
	ENatMapResult DeleteMappingByPort(uint16 ExternalPort, bool bUdp);

	/** 이 PC 의 LAN IP. 매핑 소유자 판정에 쓴다. DiscoverGateway 이후에만 유효하다. */
	FString GetLocalLanIp() const { return GetLocalIpForGateway(); }

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
	/**
	 * Get*PortMappingEntry 응답을 FNatMappingEntry 로 옮긴다.
	 *
	 * @param bOutEndOfList  713/714 를 받았다. 테이블의 끝이지 오류가 아니다.
	 *                       (ENatMapResult 에 값을 추가하면 BP enum(EMOUNatResultBP)과
	 *                        static_assert 까지 같이 건드려야 해서 out 파라미터로 뺀다)
	 */
	ENatMapResult QueryMappingEntry(const FString& ActionName, const FString& ArgsXml,
		FNatMappingEntry& OutEntry, bool& bOutEndOfList);
	/** NewInternalClient 에 넣을 내 LAN IP. NIC 가 여러 개일 때
	 *  게이트웨이로 가는 경로의 IP 를 골라야 한다 — 아무거나 넣으면 라우터가 거부한다. */
	FString GetLocalIpForGateway() const;

	FString ControlUrl;
	FString ServiceType;   // WANIPConnection:1 / WANPPPConnection:1
};

