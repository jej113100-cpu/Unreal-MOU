#include "NatPortMapping.h"

#include "Net.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// UPnP IGD 매핑 순서 (언리얼 쪽 NatPortMapping.cpp 와 같은 절차다)
//   1. SSDP M-SEARCH 를 239.255.255.250:1900 으로 → LOCATION URL
//   2. 그 URL 을 HTTP GET → 디바이스 설명 XML
//   3. XML 에서 WANIPConnection/WANPPPConnection 의 제어 URL 추출
//   4. SOAP GetExternalIPAddress → 사설 대역이면 CGNAT, 더 진행할 이유가 없다
//   5. SOAP AddPortMapping
//   6. 종료 시 SOAP DeletePortMapping
//
// ★ 전부 블로킹이다. main() 이 accept 루프에 들어가기 전에 한 번만 돈다.
// ─────────────────────────────────────────────────────────────────────

namespace MOU::Nat
{
namespace
{
	constexpr const char* kSsdpIp   = "239.255.255.250";
	constexpr uint16_t    kSsdpPort = 1900;

	/** HTTP 왕복 하나당 상한(초). */
	constexpr int kHttpTimeoutMs = 5000;

	/** 외부 포트가 충돌할 때 번호를 올려가며 다시 시도하는 횟수. */
	constexpr int kMaxPortAttempts = 8;

	const char* const kServiceCandidates[] =
	{
		"urn:schemas-upnp-org:service:WANIPConnection:1",
		"urn:schemas-upnp-org:service:WANIPConnection:2",
		"urn:schemas-upnp-org:service:WANPPPConnection:1",
	};

	// --- 모듈 상태 (main 스레드에서만 만진다) --------------------------------
	std::string GControlUrl;
	std::string GServiceType;
	std::string GExternalIp;
	std::string GLocalIp;
	uint16_t    GMappedPort = 0;
	bool        GIsTcp      = true;

	/** 소켓을 손으로 닫으면 반환 경로가 여러 갈래라 반드시 어딘가에서 샌다. */
	struct FSocketGuard
	{
		SocketHandle Sock = kInvalidSocket;

		explicit FSocketGuard(SocketHandle InSock) : Sock(InSock) {}
		~FSocketGuard() { if (Sock != kInvalidSocket) { CloseSocket(Sock); } }

		FSocketGuard(const FSocketGuard&)            = delete;
		FSocketGuard& operator=(const FSocketGuard&) = delete;
	};

	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	/** 대소문자를 무시하고 찾는다. HTTP 헤더 이름은 규격상 대소문자를 가리지 않는다. */
	size_t FindNoCase(const std::string& Haystack, const std::string& Needle, size_t From = 0)
	{
		if (Needle.empty() || Haystack.size() < Needle.size())
		{
			return std::string::npos;
		}

		for (size_t Index = From; Index + Needle.size() <= Haystack.size(); ++Index)
		{
			size_t Offset = 0;
			for (; Offset < Needle.size(); ++Offset)
			{
				const char A = static_cast<char>(std::tolower(static_cast<unsigned char>(Haystack[Index + Offset])));
				const char B = static_cast<char>(std::tolower(static_cast<unsigned char>(Needle[Offset])));
				if (A != B)
				{
					break;
				}
			}
			if (Offset == Needle.size())
			{
				return Index;
			}
		}
		return std::string::npos;
	}

	std::string Trim(const std::string& Text)
	{
		size_t Begin = 0;
		size_t End   = Text.size();
		while (Begin < End && std::isspace(static_cast<unsigned char>(Text[Begin])))   { ++Begin; }
		while (End > Begin && std::isspace(static_cast<unsigned char>(Text[End - 1]))) { --End; }
		return Text.substr(Begin, End - Begin);
	}

	/** `<tag>값</tag>` 에서 값만 꺼낸다. */
	std::string ExtractTag(const std::string& Xml, const std::string& Tag)
	{
		const std::string Open  = "<"  + Tag + ">";
		const std::string Close = "</" + Tag + ">";

		const size_t Start = FindNoCase(Xml, Open);
		if (Start == std::string::npos)
		{
			return std::string();
		}

		const size_t ValueStart = Start + Open.size();
		const size_t End        = FindNoCase(Xml, Close, ValueStart);
		if (End == std::string::npos)
		{
			return std::string();
		}

		return Trim(Xml.substr(ValueStart, End - ValueStart));
	}

	/** `http://192.168.0.1:1900/rootDesc.xml` → 호스트 / 포트 / 경로 */
	bool ParseUrl(const std::string& Url, std::string& OutHost, uint16_t& OutPort, std::string& OutPath)
	{
		const size_t SchemeEnd = Url.find("://");
		if (SchemeEnd == std::string::npos)
		{
			return false;
		}

		const std::string Remainder = Url.substr(SchemeEnd + 3);

		std::string HostPort = Remainder;
		OutPath = "/";

		const size_t SlashIndex = Remainder.find('/');
		if (SlashIndex != std::string::npos)
		{
			HostPort = Remainder.substr(0, SlashIndex);
			OutPath  = Remainder.substr(SlashIndex);
		}

		OutPort = 80;
		const size_t ColonIndex = HostPort.rfind(':');
		if (ColonIndex != std::string::npos)
		{
			OutHost = HostPort.substr(0, ColonIndex);
			OutPort = static_cast<uint16_t>(std::atoi(HostPort.c_str() + ColonIndex + 1));
		}
		else
		{
			OutHost = HostPort;
		}

		return !OutHost.empty() && OutPort != 0;
	}

	/**
	 * 블로킹 HTTP 왕복. 상태 코드와 본문만 돌려준다.
	 *
	 * Content-Length 를 존중한다 — 연결을 안 끊고 keep-alive 로 붙잡는 공유기가 있어서
	 * "끊길 때까지 읽기" 로만 하면 타임아웃까지 그대로 멈춘다.
	 */
	bool HttpRoundTrip(const std::string& Host, uint16_t Port, const std::string& Request,
	                   int& OutStatus, std::string& OutBody)
	{
		OutStatus = 0;
		OutBody.clear();

		FSocketGuard Guard(::socket(PF_INET, SOCK_STREAM, 0));
		if (Guard.Sock == kInvalidSocket)
		{
			return false;
		}

		sockaddr_in Addr{};
		Addr.sin_family = AF_INET;
		Addr.sin_port   = htons(Port);
		if (::inet_pton(AF_INET, Host.c_str(), &Addr.sin_addr) != 1)
		{
			return false;   // 공유기 주소는 항상 IP 리터럴이다. 이름 해석은 하지 않는다
		}

		SetRecvTimeout(Guard.Sock, kHttpTimeoutMs);

		if (::connect(Guard.Sock, reinterpret_cast<sockaddr*>(&Addr), sizeof(Addr)) != 0)
		{
			return false;
		}

		// 보내기 — 한 번에 다 안 나갈 수 있다.
		size_t Sent = 0;
		while (Sent < Request.size())
		{
			const int Wrote = ::send(Guard.Sock, Request.data() + Sent,
			                         static_cast<int>(Request.size() - Sent), 0);
			if (Wrote <= 0)
			{
				return false;
			}
			Sent += static_cast<size_t>(Wrote);
		}

		// 받기 — 헤더를 다 모은 뒤 Content-Length 만큼 더 모은다.
		std::string Raw;
		char        Chunk[4096];

		size_t HeaderEnd     = std::string::npos;
		long   ContentLength = -1;

		const int64_t Deadline = NowMs() + kHttpTimeoutMs;
		while (NowMs() < Deadline)
		{
			if (HeaderEnd != std::string::npos && ContentLength >= 0 &&
				Raw.size() - HeaderEnd >= static_cast<size_t>(ContentLength))
			{
				break;
			}

			const int Read = ::recv(Guard.Sock, Chunk, sizeof(Chunk), 0);
			if (Read <= 0)
			{
				break;   // 상대가 끊었다. Content-Length 가 없던 응답이면 이것이 정상 종료다
			}
			Raw.append(Chunk, static_cast<size_t>(Read));

			if (HeaderEnd == std::string::npos)
			{
				const size_t Separator = Raw.find("\r\n\r\n");
				if (Separator != std::string::npos)
				{
					HeaderEnd = Separator + 4;

					const std::string Header = Raw.substr(0, Separator);

					// "HTTP/1.1 200 OK"
					const size_t FirstSpace = Header.find(' ');
					if (FirstSpace != std::string::npos)
					{
						OutStatus = std::atoi(Header.c_str() + FirstSpace + 1);
					}

					const size_t LengthPos = FindNoCase(Header, "content-length:");
					if (LengthPos != std::string::npos)
					{
						ContentLength = std::atol(Header.c_str() + LengthPos + 15);
					}
				}
			}
		}

		if (HeaderEnd == std::string::npos)
		{
			return false;   // 헤더조차 못 받았다
		}

		OutBody = (ContentLength >= 0)
			? Raw.substr(HeaderEnd, static_cast<size_t>(ContentLength))
			: Raw.substr(HeaderEnd);

		return true;
	}

	/** 사설 대역인가. 100.64/10 은 통신사 NAT(CGNAT) 대역이다. */
	bool IsPrivateIp(const std::string& Ip)
	{
		unsigned int A = 0, B = 0, C = 0, D = 0;
		if (std::sscanf(Ip.c_str(), "%u.%u.%u.%u", &A, &B, &C, &D) != 4)
		{
			return false;
		}

		if (A == 10)                        return true;
		if (A == 127)                       return true;
		if (A == 172 && B >= 16 && B <= 31) return true;
		if (A == 192 && B == 168)           return true;
		if (A == 169 && B == 254)           return true;
		if (A == 100 && B >= 64 && B <= 127) return true;   // CGNAT
		return false;
	}

	/**
	 * 공유기로 나가는 경로의 내 LAN IP.
	 *
	 * UDP 소켓을 공유기 쪽으로 "연결" 해보고 OS 가 고른 출발지 주소를 읽는다.
	 * 패킷은 나가지 않는다. NIC 가 여러 개일 때 어느 것이 실제로 공유기로 가는
	 * 길인지 아는 방법은 이것이 가장 확실하다.
	 */
	std::string LocalIpTowardGateway(const std::string& GatewayHost)
	{
		FSocketGuard Guard(::socket(PF_INET, SOCK_DGRAM, 0));
		if (Guard.Sock == kInvalidSocket)
		{
			return std::string();
		}

		sockaddr_in Addr{};
		Addr.sin_family = AF_INET;
		Addr.sin_port   = htons(kSsdpPort);
		if (::inet_pton(AF_INET, GatewayHost.c_str(), &Addr.sin_addr) != 1)
		{
			return std::string();
		}

		if (::connect(Guard.Sock, reinterpret_cast<sockaddr*>(&Addr), sizeof(Addr)) != 0)
		{
			return std::string();
		}

		sockaddr_in Local{};
		socklen_t   LocalLen = sizeof(Local);
		if (::getsockname(Guard.Sock, reinterpret_cast<sockaddr*>(&Local), &LocalLen) != 0)
		{
			return std::string();
		}

		char Buffer[INET_ADDRSTRLEN]{};
		if (::inet_ntop(AF_INET, &Local.sin_addr, Buffer, sizeof(Buffer)) == nullptr)
		{
			return std::string();
		}

		return std::string(Buffer);
	}

	// ── 1단계: SSDP 로 공유기 찾기 ────────────────────────────────────────
	EResult DiscoverLocation(std::string& OutLocationUrl)
	{
		FSocketGuard Guard(::socket(PF_INET, SOCK_DGRAM, 0));
		if (Guard.Sock == kInvalidSocket)
		{
			return EResult::NetworkError;
		}

		SetRecvTimeout(Guard.Sock, 1000);

		sockaddr_in Destination{};
		Destination.sin_family = AF_INET;
		Destination.sin_port   = htons(kSsdpPort);
		if (::inet_pton(AF_INET, kSsdpIp, &Destination.sin_addr) != 1)
		{
			return EResult::NetworkError;
		}

		// IGD:1 에 답하지 않는 공유기가 있어 순서대로 던진다.
		const char* const SearchTargets[] =
		{
			"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
			"urn:schemas-upnp-org:device:InternetGatewayDevice:2",
			"upnp:rootdevice",
		};

		// ★ 대상을 하나씩 순서대로 던진다. (2026-08-28 원복)
		//   8/26 에 한꺼번에 쏘도록 바꿨다가, 순차 구조가 보장하던 **응답자
		//   우선순위**가 사라져 LAN 의 아무 UPnP 기기(TV/프린터/NAS)를 공유기로
		//   착각하는 사고가 났다. 여기서 아끼는 것은 몇 초이고 잃는 것은 접속
		//   자체라 교환이 맞지 않는다. 대신 MX 를 1 로 줄이고 대상당 대기를
		//   1초로 잡아 최악 3초로 맞췄다(원래는 대상당 3초 = 최악 9초).
		//   (언리얼 쪽 NatPortMapping.cpp 에 같은 판단이 적용돼 있다)
		constexpr int64_t kPerTargetMs = 1000;

		for (const char* SearchTarget : SearchTargets)
		{
			char Request[512];
			const int Length = std::snprintf(Request, sizeof(Request),
				"M-SEARCH * HTTP/1.1\r\n"
				"HOST: %s:%u\r\n"
				"MAN: \"ssdp:discover\"\r\n"
				"MX: 1\r\n"
				"ST: %s\r\n"
				"\r\n",
				kSsdpIp, static_cast<unsigned>(kSsdpPort), SearchTarget);

			::sendto(Guard.Sock, Request, Length, 0,
			         reinterpret_cast<sockaddr*>(&Destination), sizeof(Destination));

			// 이 ST 에 대한 응답만 기다린다.
			const int64_t Deadline = NowMs() + kPerTargetMs;
			while (NowMs() < Deadline)
			{
				char        Buffer[4096];
				sockaddr_in From{};
				socklen_t   FromLen = sizeof(From);

				const int Read = ::recvfrom(Guard.Sock, Buffer, sizeof(Buffer) - 1, 0,
				                            reinterpret_cast<sockaddr*>(&From), &FromLen);
				if (Read <= 0)
				{
					continue;   // 타임아웃. Deadline 까지 계속 기다린다
				}
				Buffer[Read] = '\0';

				const std::string Response(Buffer, static_cast<size_t>(Read));
				const size_t      LocationPos = FindNoCase(Response, "location:");
				if (LocationPos == std::string::npos)
				{
					continue;
				}

				const size_t ValueStart = LocationPos + 9;
				const size_t LineEnd    = Response.find("\r\n", ValueStart);
				OutLocationUrl = Trim(Response.substr(ValueStart,
					(LineEnd == std::string::npos) ? std::string::npos : LineEnd - ValueStart));

				if (!OutLocationUrl.empty())
				{
					return EResult::Success;
				}
			}
		}

		return EResult::NoGatewayFound;
	}

	// ── 2~3단계: 디바이스 설명 → 제어 URL ─────────────────────────────────
	EResult FetchControlUrl(const std::string& LocationUrl)
	{
		std::string Host;
		uint16_t    Port = 0;
		std::string Path;
		if (!ParseUrl(LocationUrl, Host, Port, Path))
		{
			return EResult::NetworkError;
		}

		char Request[1024];
		std::snprintf(Request, sizeof(Request),
			"GET %s HTTP/1.1\r\n"
			"HOST: %s:%u\r\n"
			"CONNECTION: close\r\n"
			"USER-AGENT: MOU/1.0 UPnP/1.1\r\n"
			"\r\n",
			Path.c_str(), Host.c_str(), static_cast<unsigned>(Port));

		int         Status = 0;
		std::string Body;
		if (!HttpRoundTrip(Host, Port, Request, Status, Body))
		{
			return EResult::Timeout;
		}
		if (Status != 200 || Body.empty())
		{
			return EResult::GatewayRefused;
		}

		// UPnP 규격상 <service> 안의 순서가 serviceType → serviceId → SCPDURL →
		// controlURL 이다. serviceType 을 찾은 지점부터 앞으로 훑되 </service> 를
		// 넘지 않는 선에서 첫 <controlURL> 을 집으면 같은 서비스의 것이 보장된다.
		for (const char* Candidate : kServiceCandidates)
		{
			const size_t TypePos = FindNoCase(Body, Candidate);
			if (TypePos == std::string::npos)
			{
				continue;
			}

			const size_t ServiceEnd = FindNoCase(Body, "</service>", TypePos);
			const size_t ControlPos = FindNoCase(Body, "<controlURL>", TypePos);
			if (ControlPos == std::string::npos ||
				(ServiceEnd != std::string::npos && ControlPos > ServiceEnd))
			{
				continue;
			}

			const std::string Value = ExtractTag(Body.substr(ControlPos), "controlURL");
			if (Value.empty())
			{
				continue;
			}

			// 대개 상대 경로다. 절대 URL 로 바꿔둬야 이후 SOAP 이 이 값만 보고 나갈 수 있다.
			if (Value.rfind("http://", 0) == 0 || Value.rfind("https://", 0) == 0)
			{
				GControlUrl = Value;
			}
			else
			{
				char Absolute[512];
				std::snprintf(Absolute, sizeof(Absolute), "http://%s:%u%s%s",
					Host.c_str(), static_cast<unsigned>(Port),
					(Value[0] == '/') ? "" : "/", Value.c_str());
				GControlUrl = Absolute;
			}

			GServiceType = Candidate;
			return EResult::Success;
		}

		return EResult::NoGatewayFound;   // IGD 인데 WAN 연결 서비스가 없다
	}

	// ── SOAP 공통 ────────────────────────────────────────────────────────
	EResult SendSoap(const char* Action, const std::string& ArgsXml, std::string& OutBody)
	{
		OutBody.clear();

		if (GControlUrl.empty() || GServiceType.empty())
		{
			return EResult::NoGatewayFound;
		}

		std::string Host;
		uint16_t    Port = 0;
		std::string Path;
		if (!ParseUrl(GControlUrl, Host, Port, Path))
		{
			return EResult::NetworkError;
		}

		std::string Envelope =
			"<?xml version=\"1.0\"?>\r\n"
			"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
			"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
			"<s:Body><u:";
		Envelope += Action;
		Envelope += " xmlns:u=\"" + GServiceType + "\">";
		Envelope += ArgsXml;
		Envelope += "</u:";
		Envelope += Action;
		Envelope += "></s:Body></s:Envelope>";

		char Header[1024];
		std::snprintf(Header, sizeof(Header),
			"POST %s HTTP/1.1\r\n"
			"HOST: %s:%u\r\n"
			"CONTENT-TYPE: text/xml; charset=\"utf-8\"\r\n"
			"SOAPACTION: \"%s#%s\"\r\n"
			"CONTENT-LENGTH: %zu\r\n"
			"CONNECTION: close\r\n"
			"\r\n",
			Path.c_str(), Host.c_str(), static_cast<unsigned>(Port),
			GServiceType.c_str(), Action, Envelope.size());

		int Status = 0;
		if (!HttpRoundTrip(Host, Port, std::string(Header) + Envelope, Status, OutBody))
		{
			return EResult::Timeout;
		}

		if (Status == 200)
		{
			return EResult::Success;
		}

		// SOAP 실패는 HTTP 500 에 <errorCode> 를 실어 온다.
		// ★ 본문은 실패해도 채워서 돌려준다 — 호출부가 725(Lease 미지원)를 직접
		//   보고 재시도해야 하기 때문이다.
		const int ErrorCode = std::atoi(ExtractTag(OutBody, "errorCode").c_str());
		switch (ErrorCode)
		{
		case 718: return EResult::PortConflict;     // ConflictInMappingEntry
		case 714:                                   // NoSuchEntryInArray
		case 725:                                   // OnlyPermanentLeasesSupported
		case 401: case 402: case 501: case 606:
			return EResult::GatewayRefused;
		default:
			return (Status == 0) ? EResult::NetworkError : EResult::Unknown;
		}
	}
}   // anonymous namespace

const char* ResultText(EResult Result)
{
	switch (Result)
	{
	case EResult::Success:         return "성공";
	case EResult::NoGatewayFound:  return "공유기가 UPnP 에 응답하지 않음 (미지원이거나 꺼져 있음)";
	case EResult::CarrierGradeNat: return "통신사 NAT(CGNAT) 환경이라 포트를 열 수 없음";
	case EResult::PortConflict:    return "쓸 수 있는 외부 포트를 찾지 못함";
	case EResult::GatewayRefused:  return "공유기가 요청을 거부함";
	case EResult::NetworkError:    return "네트워크 오류";
	case EResult::Timeout:         return "공유기가 제때 응답하지 않음";
	default:                       return "알 수 없는 오류";
	}
}

const std::string& ExternalIp()      { return GExternalIp; }
uint16_t           MappedExternalPort() { return GMappedPort; }

EResult Start(uint16_t Port, bool bTcp)
{
	GIsTcp = bTcp;

	// 1. 공유기 찾기
	std::string LocationUrl;
	{
		const EResult Result = DiscoverLocation(LocationUrl);
		if (Result != EResult::Success)
		{
			return Result;
		}
	}
	std::printf("[NAT] 공유기 발견: %s\n", LocationUrl.c_str());

	// 2~3. 제어 URL
	{
		const EResult Result = FetchControlUrl(LocationUrl);
		if (Result != EResult::Success)
		{
			return Result;
		}
	}
	std::printf("[NAT] 제어 URL: %s (%s)\n", GControlUrl.c_str(), GServiceType.c_str());

	// 4. 외부 IP — CGNAT 판별
	{
		std::string Body;
		if (SendSoap("GetExternalIPAddress", std::string(), Body) == EResult::Success)
		{
			GExternalIp = ExtractTag(Body, "NewExternalIPAddress");
			if (!GExternalIp.empty() && IsPrivateIp(GExternalIp))
			{
				// 공유기의 "외부" IP 가 사설 대역이면 그 위에 통신사 NAT 이 한 겹 더 있다.
				// 포트를 열어봐야 통신사 장비에서 막히므로 시도할 이유가 없다.
				std::printf("[NAT] 공유기의 외부 IP 가 사설 대역이다 (%s).\n", GExternalIp.c_str());
				return EResult::CarrierGradeNat;
			}
			std::printf("[NAT] 외부 IP: %s\n", GExternalIp.empty() ? "(확인 못함)" : GExternalIp.c_str());
		}
		else
		{
			std::printf("[NAT] 외부 IP 를 확인하지 못했다. 매핑은 계속 시도한다.\n");
		}
	}

	// NewInternalClient 가 틀리면 공유기는 매핑을 만들어주고도 엉뚱한 기기로 보낸다.
	{
		std::string GatewayHost;
		uint16_t    GatewayPort = 0;
		std::string GatewayPath;
		ParseUrl(GControlUrl, GatewayHost, GatewayPort, GatewayPath);

		GLocalIp = LocalIpTowardGateway(GatewayHost);
		if (GLocalIp.empty())
		{
			std::printf("[NAT] 내 LAN IP 를 찾지 못했다.\n");
			return EResult::NetworkError;
		}
	}

	// 5. 매핑 추가. 충돌이면 번호를 올려가며 다시 시도한다.
	//
	//   ★ Lease 는 처음부터 0(영구)으로 요청한다. 언리얼 쪽과 다른 선택인데,
	//     Server.exe 는 상시 가동이라 갱신할 주기가 애초에 없고, Lease 를
	//     지원하지 않는 공유기(오류 725)가 흔해서 어차피 0 으로 떨어지기 때문이다.
	//     대신 Stop() 을 반드시 불러야 한다.
	const char* Protocol = GIsTcp ? "TCP" : "UDP";

	for (int Attempt = 0; Attempt < kMaxPortAttempts; ++Attempt)
	{
		const uint16_t External = static_cast<uint16_t>(Port + Attempt);

		char Args[1024];
		std::snprintf(Args, sizeof(Args),
			"<NewRemoteHost></NewRemoteHost>"
			"<NewExternalPort>%u</NewExternalPort>"
			"<NewProtocol>%s</NewProtocol>"
			"<NewInternalPort>%u</NewInternalPort>"
			"<NewInternalClient>%s</NewInternalClient>"
			"<NewEnabled>1</NewEnabled>"
			"<NewPortMappingDescription>MOU Server</NewPortMappingDescription>"
			"<NewLeaseDuration>0</NewLeaseDuration>",
			static_cast<unsigned>(External), Protocol,
			static_cast<unsigned>(Port), GLocalIp.c_str());

		std::string Body;
		const EResult Result = SendSoap("AddPortMapping", Args, Body);

		if (Result == EResult::Success)
		{
			GMappedPort = External;
			std::printf("[NAT] 매핑 성공: 외부 %u -> %s:%u (%s)\n",
				static_cast<unsigned>(External), GLocalIp.c_str(),
				static_cast<unsigned>(Port), Protocol);
			return EResult::Success;
		}

		if (Result != EResult::PortConflict)
		{
			return Result;
		}

		std::printf("[NAT] 외부 포트 %u 가 사용 중이다. 다음 번호로 시도한다.\n",
			static_cast<unsigned>(External));
	}

	return EResult::PortConflict;
}

void Stop()
{
	if (GMappedPort == 0)
	{
		return;   // 연 것이 없다. Start 가 실패했거나 아예 안 불렸다
	}

	char Args[512];
	std::snprintf(Args, sizeof(Args),
		"<NewRemoteHost></NewRemoteHost>"
		"<NewExternalPort>%u</NewExternalPort>"
		"<NewProtocol>%s</NewProtocol>",
		static_cast<unsigned>(GMappedPort), GIsTcp ? "TCP" : "UDP");

	std::string   Body;
	const EResult Result = SendSoap("DeletePortMapping", Args, Body);

	// 이미 없는 매핑을 지우려 한 것(714)은 실패로 볼 이유가 없다 — 원하던 상태는 같다.
	if (Result == EResult::Success ||
		std::atoi(ExtractTag(Body, "errorCode").c_str()) == 714)
	{
		std::printf("[NAT] 매핑 해제: 외부 %u\n", static_cast<unsigned>(GMappedPort));
	}
	else
	{
		std::printf("[NAT] 매핑 해제 실패(%s). 공유기에 외부 포트 %u 가 남았을 수 있다.\n",
			ResultText(Result), static_cast<unsigned>(GMappedPort));
	}

	GMappedPort = 0;
}

}   // namespace MOU::Nat
