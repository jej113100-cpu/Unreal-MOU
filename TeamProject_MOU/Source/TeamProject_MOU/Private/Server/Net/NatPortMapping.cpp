#include "Server/Net/NatPortMapping.h"

#include "SocketSubsystem.h"
#include "Sockets.h"
#include "IPAddress.h"
#include "Server/Chat/ChatTypes.h"   // LogMOUServer

// ─────────────────────────────────────────────────────────────────────
// 구현은 전부 여기에. 헤더는 플랫폼 헤더를 노출하지 않는다 (헤더 상단 참고).
//
// ★ Build.cs 를 건드리지 않는다.
//   HTTP 모듈(FHttpModule)을 쓰지 않고 HTTP 를 FSocket 위에 직접 얹었다. 이유는 둘이다.
//     1. 헤더의 함수들이 전부 "블로킹" 이다. FHttpModule 은 비동기고 완료 콜백이
//        게임 스레드 틱에서 나오므로, 워커 스레드에서 결과를 기다릴 수가 없다.
//     2. SSDP(UDP 멀티캐스트) 때문에 어차피 raw 소켓이 필요하고, UPnP 가 쓰는
//        HTTP 는 Content-Length 하나만 처리하면 되는 단순한 형태다.
//   XmlParser 모듈도 안 쓴다 — 디바이스 설명에서 뽑아야 하는 것이 태그 두 개뿐이라
//   전체 XML DOM 을 세울 이유가 없다. Build.cs 는 팀 공용 파일이라 안 건드리는 쪽이 낫다.
//
// UPnP IGD 매핑 순서 — 아래 함수들이 이 순서로 이어진다
//   1. DiscoverGateway
//       └ SendSsdpMSearch      239.255.255.250:1900 으로 M-SEARCH → LOCATION URL 획득
//       └ FetchDeviceDescription  그 URL 을 GET → 디바이스 설명 XML
//           └ ParseControlUrl     XML 에서 WANIPConnection 의 제어 URL 추출
//   2. GetExternalIP           공유기의 외부 IP 확인. 사설 대역이면 CGNAT — 매핑해도 소용없다
//   3. AddMapping              SOAP AddPortMapping. UE 리플리케이션은 UDP 다
//   4. RefreshMaping           Lease 를 쓸 때만. 같은 인자로 다시 Add 하면 갱신이다
//   5. DeleteMapping           방을 나가거나 게임을 끌 때. 빠뜨리면 공유기에 그대로 남는다
//
// ★ 전부 블로킹이다. 게임 스레드에서 부르면 최대 수 초간 프레임이 멈춘다.
//   FRunnable 워커에서 부르고 결과만 TQueue 로 게임 스레드에 넘길 것.
// ─────────────────────────────────────────────────────────────────────

namespace
{
	/** SSDP 멀티캐스트 주소. UPnP 규격에 고정돼 있다. */
	const TCHAR* const SsdpMulticastIp   = TEXT("239.255.255.250");
	constexpr int32    SsdpMulticastPort = 1900;

	/** 공유기 응답을 기다리는 기본 상한. HTTP 왕복 하나당. */
	constexpr float HttpTimeoutSeconds = 5.0f;

	/**
	 * 우리가 찾는 서비스. 앞에 있는 것부터 시도한다.
	 * 광랜/케이블은 보통 WANIPConnection, 예전 ADSL(PPPoE) 공유기는 WANPPPConnection 이다.
	 */
	const TCHAR* const ServiceCandidates[] =
	{
		TEXT("urn:schemas-upnp-org:service:WANIPConnection:1"),
		TEXT("urn:schemas-upnp-org:service:WANIPConnection:2"),
		TEXT("urn:schemas-upnp-org:service:WANPPPConnection:1"),
	};

	/** FSocket 은 반환 경로가 여러 갈래라 손으로 닫으면 반드시 어딘가에서 샌다. */
	struct FSocketGuard
	{
		FSocket* Socket = nullptr;

		explicit FSocketGuard(FSocket* InSocket) : Socket(InSocket) {}

		~FSocketGuard()
		{
			if (Socket)
			{
				Socket->Close();
				if (ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
				{
					Subsystem->DestroySocket(Socket);
				}
			}
		}

		FSocketGuard(const FSocketGuard&) = delete;
		FSocketGuard& operator=(const FSocketGuard&) = delete;
	};

	/** `http://192.168.0.1:5000/rootDesc.xml` → 호스트 / 포트 / 경로 */
	bool ParseHttpUrl(const FString& Url, FString& OutHost, int32& OutPort, FString& OutPath)
	{
		FString Remainder;
		if (!Url.Split(TEXT("://"), nullptr, &Remainder))
		{
			return false;
		}

		FString HostPort = Remainder;
		OutPath = TEXT("/");

		int32 SlashIndex = INDEX_NONE;
		if (Remainder.FindChar(TEXT('/'), SlashIndex))
		{
			HostPort = Remainder.Left(SlashIndex);
			OutPath  = Remainder.Mid(SlashIndex);
		}

		OutPort = 80;
		int32 ColonIndex = INDEX_NONE;
		if (HostPort.FindLastChar(TEXT(':'), ColonIndex))
		{
			OutHost = HostPort.Left(ColonIndex);
			OutPort = FCString::Atoi(*HostPort.Mid(ColonIndex + 1));
		}
		else
		{
			OutHost = HostPort;
		}

		return !OutHost.IsEmpty() && OutPort > 0;
	}

	/** `<tag>값</tag>` 에서 값만. 네임스페이스 접두사가 붙는 경우가 있어 접미 일치로 찾는다. */
	FString ExtractXmlTag(const FString& Xml, const FString& Tag)
	{
		const FString OpenTag  = FString::Printf(TEXT("<%s>"), *Tag);
		const FString CloseTag = FString::Printf(TEXT("</%s>"), *Tag);

		const int32 Start = Xml.Find(OpenTag, ESearchCase::IgnoreCase);
		if (Start == INDEX_NONE)
		{
			return FString();
		}

		const int32 ValueStart = Start + OpenTag.Len();
		const int32 End = Xml.Find(CloseTag, ESearchCase::IgnoreCase, ESearchDir::FromStart, ValueStart);
		if (End == INDEX_NONE)
		{
			return FString();
		}

		return Xml.Mid(ValueStart, End - ValueStart).TrimStartAndEnd();
	}

	/**
	 * 받은 바이트를 FString 으로.
	 * ★ 길이를 지정해 변환하면 결과가 널 종료되지 않고, 변환 후 TCHAR 개수도
	 *   원본 바이트 수와 다르다(멀티바이트). 반드시 Converter.Length() 를 써야 한다.
	 */
	FString Utf8ToString(const uint8* Data, int32 Length)
	{
		if (!Data || Length <= 0)
		{
			return FString();
		}
		const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Data), Length);
		return FString(Converter.Length(), Converter.Get());
	}

	/** 헤더 끝(\r\n\r\n) 바로 다음 바이트 위치. 아직 안 왔으면 INDEX_NONE. */
	int32 FindHeaderEnd(const TArray<uint8>& Raw)
	{
		for (int32 Index = 0; Index + 3 < Raw.Num(); ++Index)
		{
			if (Raw[Index] == '\r' && Raw[Index + 1] == '\n' &&
				Raw[Index + 2] == '\r' && Raw[Index + 3] == '\n')
			{
				return Index + 4;
			}
		}
		return INDEX_NONE;
	}

	/** 호스트 문자열을 FInternetAddr 로. IP 리터럴이 아니면 이름 해석을 시도한다. */
	TSharedPtr<FInternetAddr> ResolveAddress(ISocketSubsystem& Subsystem, const FString& Host, int32 Port)
	{
		TSharedRef<FInternetAddr> Addr = Subsystem.CreateInternetAddr();

		bool bIsValid = false;
		Addr->SetIp(*Host, bIsValid);

		if (!bIsValid)
		{
			// 공유기의 LOCATION 은 거의 항상 IP 리터럴이지만 규격상 이름일 수도 있다.
			const FAddressInfoResult Info = Subsystem.GetAddressInfo(
				*Host, nullptr, EAddressInfoFlags::Default, NAME_None);

			if (Info.ReturnCode != SE_NO_ERROR || Info.Results.Num() == 0)
			{
				return nullptr;
			}
			Addr = Info.Results[0].Address->Clone();
		}

		Addr->SetPort(Port);
		return Addr;
	}

	/**
	 * 블로킹 HTTP 왕복. 요청 문자열을 통째로 받아 보내고, 응답 본문만 돌려준다.
	 *
	 * Content-Length 를 존중한다 — UPnP 공유기 중에 연결을 안 끊고 keep-alive 로
	 * 붙잡고 있는 것이 있어서, "끊길 때까지 읽기" 로만 하면 타임아웃까지 멈춘다.
	 */
	bool HttpRequestBlocking(const FString& Host, int32 Port, const FString& Request,
	                         int32& OutStatusCode, FString& OutBody, float TimeoutSeconds)
	{
		OutStatusCode = 0;
		OutBody.Reset();

		ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!Subsystem)
		{
			return false;
		}

		const TSharedPtr<FInternetAddr> Addr = ResolveAddress(*Subsystem, Host, Port);
		if (!Addr.IsValid())
		{
			return false;
		}

		FSocketGuard Guard(Subsystem->CreateSocket(NAME_Stream, TEXT("MOU.UPnP.Http"), Addr->GetProtocolType()));
		if (!Guard.Socket)
		{
			return false;
		}

		Guard.Socket->SetNonBlocking(false);
		if (!Guard.Socket->Connect(*Addr))
		{
			return false;
		}

		// 보내기 — 한 번에 다 안 나갈 수 있으므로 남은 만큼 반복한다.
		const FTCHARToUTF8 RequestUtf8(*Request);
		const uint8* SendCursor = reinterpret_cast<const uint8*>(RequestUtf8.Get());
		int32 Remaining = RequestUtf8.Length();
		while (Remaining > 0)
		{
			int32 Sent = 0;
			if (!Guard.Socket->Send(SendCursor, Remaining, Sent) || Sent <= 0)
			{
				return false;
			}
			SendCursor += Sent;
			Remaining  -= Sent;
		}

		// 받기 — 헤더가 다 올 때까지 모으고, Content-Length 를 안 뒤 그만큼 더 모은다.
		TArray<uint8> Raw;
		TArray<uint8> Chunk;
		Chunk.SetNumUninitialized(4096);

		int32 HeaderEnd    = INDEX_NONE;
		int32 ContentLength = -1;

		const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		while (FPlatformTime::Seconds() < Deadline)
		{
			if (HeaderEnd != INDEX_NONE && ContentLength >= 0 &&
				Raw.Num() - HeaderEnd >= ContentLength)
			{
				break;   // 본문까지 다 받았다
			}

			if (!Guard.Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(200)))
			{
				continue;
			}

			int32 Read = 0;
			if (!Guard.Socket->Recv(Chunk.GetData(), Chunk.Num(), Read) || Read <= 0)
			{
				break;   // 상대가 끊었다. Content-Length 가 없던 응답이면 이게 정상 종료다
			}
			Raw.Append(Chunk.GetData(), Read);

			if (HeaderEnd == INDEX_NONE)
			{
				HeaderEnd = FindHeaderEnd(Raw);
				if (HeaderEnd != INDEX_NONE)
				{
					const FString Header = Utf8ToString(Raw.GetData(), HeaderEnd);

					// "HTTP/1.1 200 OK"
					FString StatusLine = Header;
					int32 LineEnd = INDEX_NONE;
					if (Header.FindChar(TEXT('\r'), LineEnd))
					{
						StatusLine = Header.Left(LineEnd);
					}
					TArray<FString> StatusTokens;
					StatusLine.ParseIntoArray(StatusTokens, TEXT(" "), true);
					if (StatusTokens.Num() >= 2)
					{
						OutStatusCode = FCString::Atoi(*StatusTokens[1]);
					}

					TArray<FString> HeaderLines;
					Header.ParseIntoArrayLines(HeaderLines);
					for (const FString& Line : HeaderLines)
					{
						if (Line.StartsWith(TEXT("CONTENT-LENGTH:"), ESearchCase::IgnoreCase))
						{
							ContentLength = FCString::Atoi(*Line.Mid(15).TrimStartAndEnd());
							break;
						}
					}
				}
			}
		}

		if (HeaderEnd == INDEX_NONE)
		{
			return false;   // 헤더조차 못 받았다 — 타임아웃
		}

		const int32 BodyLength = (ContentLength >= 0)
			? FMath::Min(ContentLength, Raw.Num() - HeaderEnd)
			: Raw.Num() - HeaderEnd;

		if (BodyLength > 0)
		{
			OutBody = Utf8ToString(Raw.GetData() + HeaderEnd, BodyLength);
		}

		return true;
	}
}   // namespace

// ─────────────────────────────────────────────────────────────────────
// 1단계 — 공유기 찾기
// ─────────────────────────────────────────────────────────────────────

ENatMapResult FNatPortMapping::DiscoverGateway(float TimeoutSeconds)
{
	ControlUrl.Reset();
	ServiceType.Reset();

	FString LocationUrl;
	const ENatMapResult SearchResult = SendSsdpMSearch(LocationUrl, TimeoutSeconds);
	if (SearchResult != ENatMapResult::Success)
	{
		return SearchResult;
	}

	UE_LOG(LogMOUServer, Log, TEXT("[NAT] 공유기 발견: %s"), *LocationUrl);

	const ENatMapResult DescribeResult = FetchDeviceDescription(LocationUrl);
	if (DescribeResult != ENatMapResult::Success)
	{
		return DescribeResult;
	}

	UE_LOG(LogMOUServer, Log, TEXT("[NAT] 제어 URL: %s (%s)"), *ControlUrl, *ServiceType);
	return ENatMapResult::Success;
}

ENatMapResult FNatPortMapping::SendSsdpMSearch(FString& OutLocationUrl, float TimeoutSeconds)
{
	OutLocationUrl.Reset();

	ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Subsystem)
	{
		return ENatMapResult::NetworkError;
	}

	// ─────────────────────────────────────────────────────────────────
	// ★ 어느 주소에 bind 하느냐가 이 함수의 성패를 가른다. (2026-08-28)
	//
	//   [무슨 일이 있었나]
	//     원래 여기는 SetAnyAddress() — 즉 0.0.0.0 에 bind 했다. 그런데 공유기가
	//     UPnP 를 멀쩡히 지원하는데도 "SSDP 응답 없음" 이 간헐적으로 났다.
	//
	//     같은 PC, 같은 순간에 bind 주소만 바꿔 실측한 결과다:
	//       192.168.0.32 (인터페이스 IP) 에 bind  ->  공유기가 즉시 응답
	//       0.0.0.0            에 bind            ->  응답이 아예 안 옴
	//
	//   [왜 그런가]
	//     SSDP 는 **멀티캐스트로 보내고 유니캐스트로 받는다.** 우리는
	//     239.255.255.250 으로 보냈는데 답은 192.168.0.1 에서 온다. 보낸 곳과
	//     답한 곳이 다르다. 0.0.0.0 에 bind 하면 윈도우가 이 응답을 우리가 만든
	//     흐름과 연결짓지 못하고 버린다. 인터페이스 IP 에 bind 하면 연결된다.
	//
	//     방화벽 규칙(UnrealEditor.exe 인바운드 Allow)이 이미 있어도 소용없다 —
	//     규칙 이전에 흐름 매칭에서 떨어지기 때문이다. 이것이 "가끔 성공" 의 정체였다.
	//
	//   [왜 어댑터를 전부 도는가]
	//     팀원 PC 마다 Hyper-V / VPN / VirtualBox 가상 어댑터가 있을 수 있고,
	//     그 중 어느 것이 공유기로 가는 길인지 여기서는 모른다. 하나씩 보내보고
	//     먼저 답이 오는 것을 쓴다. 대개 첫 번째에서 끝난다.
	// ─────────────────────────────────────────────────────────────────
	TArray<TSharedPtr<FInternetAddr>> BindCandidates;
	{
		TArray<TSharedPtr<FInternetAddr>> Adapters;
		if (Subsystem->GetLocalAdapterAddresses(Adapters))
		{
			for (const TSharedPtr<FInternetAddr>& Adapter : Adapters)
			{
				if (Adapter.IsValid()
					&& Adapter->GetProtocolType() == FNetworkProtocolTypes::IPv4
					&& Adapter->IsValid()               // 0.0.0.0 은 걸러진다
					&& !Adapter->ToString(false).StartsWith(TEXT("127.")))
				{
					BindCandidates.Add(Adapter);
				}
			}
		}

		// 어댑터 열거가 실패하는 플랫폼/상황을 대비한 마지막 수단.
		// 여기까지 오면 위 문제로 응답을 못 받을 가능성이 크지만, 아무것도
		// 시도하지 않는 것보다는 낫다.
		if (BindCandidates.Num() == 0)
		{
			TSharedRef<FInternetAddr> AnyAddr = Subsystem->CreateInternetAddr();
			AnyAddr->SetAnyAddress();
			BindCandidates.Add(AnyAddr);
			UE_LOG(LogMOUServer, Warning,
				TEXT("[NAT] 로컬 어댑터 목록을 얻지 못했다. 0.0.0.0 으로 시도한다 — ")
				TEXT("윈도우에서는 이 경로로 SSDP 응답이 안 올 수 있다."));
		}
	}

	TSharedRef<FInternetAddr> Destination = Subsystem->CreateInternetAddr();
	bool bDestinationValid = false;
	Destination->SetIp(SsdpMulticastIp, bDestinationValid);
	Destination->SetPort(SsdpMulticastPort);
	if (!bDestinationValid)
	{
		return ENatMapResult::NetworkError;
	}
	// 찾는 대상. IGD:1 이 안 잡히는 공유기가 있어 IGD:2 와 rootdevice 까지 던진다.
	const TCHAR* const SearchTargets[] =
	{
		TEXT("urn:schemas-upnp-org:device:InternetGatewayDevice:1"),
		TEXT("urn:schemas-upnp-org:device:InternetGatewayDevice:2"),
		TEXT("upnp:rootdevice"),
	};

	// ★ 대상을 하나씩 순서대로 던진다. (2026-08-28 원복)
	//
	//   [왜 원래대로 돌렸나]
	//     8/26 에 "셋을 한꺼번에 쏘고 한 번만 기다린다" 로 바꿔 9초를 3초로 줄였다.
	//     그런데 그 순차 구조는 속도를 대가로 **응답자의 우선순위**를 보장하고
	//     있었다. upnp:rootdevice 는 LAN 의 모든 UPnP 기기(TV, 프린터, NAS,
	//     윈도우 SSDP 서비스)가 답하는 반면, IGD 를 혼자 먼저 물어보면 공유기만
	//     답한다. 한꺼번에 쏘면 먼저 답한 아무 기기가 공유기로 뽑힌다.
	//
	//     ST 를 보고 IGD 를 골라내는 방식으로도 고칠 수 있고 실제로 그렇게
	//     고쳤었다. 하지만 이 경로는 **실기 검증이 어렵고**(내 PC 공유기는 UPnP 가
	//     꺼져 있다) 이미 두 번 사고가 났다. 여기서 아낄 수 있는 것은 몇 초이고,
	//     잃는 것은 "참여자가 아예 못 들어온다" 다. 교환 비율이 맞지 않는다.
	//
	//   [그래도 9초는 아니다]
	//     원래 코드는 대상마다 3초씩 = 최악 9초였다. 대상당 대기를 1.5초로 잡는다.
	//     MX 는 2 로 둔다 — 규격상 공유기는 0~MX초 사이 **임의 시점**에 답하므로,
	//     MX 1 에 대기 1초는 경계에 걸린다(폴링 간격 100ms 를 빼면 실질 0.9초).
	//     순서(= 정확성)는 그대로 두고 여유만 준다.
	const float PerTargetSeconds = FMath::Max(TimeoutSeconds * 0.5f, 1.5f);

	// 어댑터가 여러 개인 PC 에서 전체 시간이 늘어나는 것을 막는다.
	// (어댑터 수 × 대상 수 × 대기) 가 그대로 곱해지면 방 만들기가 눈에 띄게 늦어진다.
	const double OverallDeadline = FPlatformTime::Seconds() + FMath::Max(TimeoutSeconds, 3.0f) * 3.0;

	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(4096);

	for (const TSharedPtr<FInternetAddr>& Candidate : BindCandidates)
	{
		if (FPlatformTime::Seconds() >= OverallDeadline)
		{
			break;
		}

		// 어댑터마다 소켓을 새로 만든다. bind 주소가 곧 "어느 길로 나갈 것인가" 다.
		FSocketGuard Guard(Subsystem->CreateSocket(NAME_DGram, TEXT("MOU.UPnP.Ssdp"), FNetworkProtocolTypes::IPv4));
		if (!Guard.Socket)
		{
			continue;
		}

		Guard.Socket->SetNonBlocking(true);

		// 응답은 우리가 보낸 포트로 유니캐스트로 돌아온다. 포트는 아무거나 좋다.
		TSharedRef<FInternetAddr> BindAddr = Candidate->Clone();
		BindAddr->SetPort(0);
		if (!Guard.Socket->Bind(*BindAddr))
		{
			UE_LOG(LogMOUServer, Verbose, TEXT("[NAT] %s 에 bind 하지 못했다. 다음 어댑터로."),
				*Candidate->ToString(false));
			continue;
		}

		UE_LOG(LogMOUServer, Verbose, TEXT("[NAT] %s 에서 SSDP 탐색을 시도한다."),
			*Candidate->ToString(false));

		for (const TCHAR* SearchTarget : SearchTargets)
		{
			if (FPlatformTime::Seconds() >= OverallDeadline)
			{
				break;
			}

			// MX 는 "이 초 안에 아무 때나 답하라" 는 뜻이다. 규격상 1~5.
			const FString Request = FString::Printf(
				TEXT("M-SEARCH * HTTP/1.1\r\n")
				TEXT("HOST: %s:%d\r\n")
				TEXT("MAN: \"ssdp:discover\"\r\n")
				TEXT("MX: 2\r\n")
				TEXT("ST: %s\r\n")
				TEXT("\r\n"),
				SsdpMulticastIp, SsdpMulticastPort, SearchTarget);

			const FTCHARToUTF8 RequestUtf8(*Request);
			int32 Sent = 0;
			Guard.Socket->SendTo(reinterpret_cast<const uint8*>(RequestUtf8.Get()),
			                     RequestUtf8.Length(), Sent, *Destination);

			// 이 ST 에 대한 응답만 기다린다. 공유기가 여러 대면 먼저 답한 것을 쓴다.
			const double Deadline = FMath::Min(
				FPlatformTime::Seconds() + PerTargetSeconds, OverallDeadline);
			while (FPlatformTime::Seconds() < Deadline)
			{
				if (!Guard.Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(100)))
				{
					continue;
				}

				int32 Read = 0;
				TSharedRef<FInternetAddr> From = Subsystem->CreateInternetAddr();
				if (!Guard.Socket->RecvFrom(Buffer.GetData(), Buffer.Num(), Read, *From) || Read <= 0)
				{
					continue;
				}

				const FString Response = Utf8ToString(Buffer.GetData(), Read);

				TArray<FString> Lines;
				Response.ParseIntoArrayLines(Lines);
				for (const FString& Line : Lines)
				{
					if (Line.StartsWith(TEXT("LOCATION:"), ESearchCase::IgnoreCase))
					{
						OutLocationUrl = Line.Mid(9).TrimStartAndEnd();
						if (!OutLocationUrl.IsEmpty())
						{
							UE_LOG(LogMOUServer, Log,
								TEXT("[NAT] %s 로 공유기를 찾았다: %s  (%s 에서 보냄)"),
								SearchTarget, *OutLocationUrl, *Candidate->ToString(false));
							return ENatMapResult::Success;
						}
					}
				}
			}
		}
	}

	// 여기까지 왔으면 어느 어댑터에서도 아무도 답하지 않았다.
	// UPnP 를 껐거나 지원하지 않는 공유기다 — 실패가 아니라 "이 경로로는 못 간다" 는 뜻이다.
	UE_LOG(LogMOUServer, Verbose, TEXT("[NAT] 어댑터 %d개에서 모두 응답이 없었다."),
		BindCandidates.Num());
	UE_LOG(LogMOUServer, Warning, TEXT("[NAT] SSDP 응답 없음. UPnP 미지원이거나 공유기에서 꺼져 있다."));
	return ENatMapResult::NoGatewayFound;
}

ENatMapResult FNatPortMapping::FetchDeviceDescription(const FString& LocationUrl)
{
	FString Host;
	int32   Port = 0;
	FString Path;
	if (!ParseHttpUrl(LocationUrl, Host, Port, Path))
	{
		return ENatMapResult::NetworkError;
	}

	const FString Request = FString::Printf(
		TEXT("GET %s HTTP/1.1\r\n")
		TEXT("HOST: %s:%d\r\n")
		TEXT("CONNECTION: close\r\n")
		TEXT("USER-AGENT: MOU/1.0 UPnP/1.1\r\n")
		TEXT("\r\n"),
		*Path, *Host, Port);

	int32   StatusCode = 0;
	FString Body;
	if (!HttpRequestBlocking(Host, Port, Request, StatusCode, Body, HttpTimeoutSeconds))
	{
		return ENatMapResult::Timeout;
	}
	if (StatusCode != 200 || Body.IsEmpty())
	{
		return ENatMapResult::GatewayRefused;
	}

	const ENatMapResult ParseResult = ParseControlUrl(Body);
	if (ParseResult != ENatMapResult::Success)
	{
		return ParseResult;
	}

	// ParseControlUrl 이 뽑아준 값은 대개 상대 경로(`/upnp/control/WANIPConn1`)다.
	// 절대 URL 로 바꿔둬야 이후 SOAP 요청이 이 값 하나만 보고 나갈 수 있다.
	if (!ControlUrl.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase) &&
		!ControlUrl.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
	{
		if (!ControlUrl.StartsWith(TEXT("/")))
		{
			ControlUrl = TEXT("/") + ControlUrl;
		}
		ControlUrl = FString::Printf(TEXT("http://%s:%d%s"), *Host, Port, *ControlUrl);
	}

	return ENatMapResult::Success;
}

ENatMapResult FNatPortMapping::ParseControlUrl(const FString& Xml)
{
	// UPnP 규격상 <service> 안의 순서가 serviceType → serviceId → SCPDURL → controlURL 이다.
	// 그래서 serviceType 을 찾은 지점부터 앞으로 훑되, </service> 를 넘지 않는 선에서
	// 첫 <controlURL> 을 집으면 같은 서비스의 것이 보장된다.
	for (const TCHAR* Candidate : ServiceCandidates)
	{
		const int32 TypeIndex = Xml.Find(Candidate, ESearchCase::IgnoreCase);
		if (TypeIndex == INDEX_NONE)
		{
			continue;
		}

		const int32 ServiceEnd = Xml.Find(TEXT("</service>"), ESearchCase::IgnoreCase,
		                                  ESearchDir::FromStart, TypeIndex);
		const int32 ControlIndex = Xml.Find(TEXT("<controlURL>"), ESearchCase::IgnoreCase,
		                                    ESearchDir::FromStart, TypeIndex);

		if (ControlIndex == INDEX_NONE || (ServiceEnd != INDEX_NONE && ControlIndex > ServiceEnd))
		{
			continue;   // 이 서비스에는 제어 URL 이 없다. 다음 후보로
		}

		const FString Tail  = Xml.Mid(ControlIndex);
		const FString Value = ExtractXmlTag(Tail, TEXT("controlURL"));
		if (Value.IsEmpty())
		{
			continue;
		}

		ControlUrl  = Value;
		ServiceType = Candidate;
		return ENatMapResult::Success;
	}

	// IGD 라고 답했는데 WAN 연결 서비스가 없다 — 포트 매핑을 지원하지 않는 기기다.
	UE_LOG(LogMOUServer, Warning, TEXT("[NAT] 디바이스 설명에 WANIPConnection/WANPPPConnection 이 없다."));
	return ENatMapResult::NoGatewayFound;
}

// ─────────────────────────────────────────────────────────────────────
// SOAP 공통
// ─────────────────────────────────────────────────────────────────────

ENatMapResult FNatPortMapping::SendSoapAction(const FString& ActionName,
                                              const FString& ArgsXml,
                                              FString& OutResponseBody)
{
	OutResponseBody.Reset();

	if (ControlUrl.IsEmpty() || ServiceType.IsEmpty())
	{
		return ENatMapResult::NoGatewayFound;   // DiscoverGateway 를 먼저 불러야 한다
	}

	FString Host;
	int32   Port = 0;
	FString Path;
	if (!ParseHttpUrl(ControlUrl, Host, Port, Path))
	{
		return ENatMapResult::NetworkError;
	}

	const FString Envelope = FString::Printf(
		TEXT("<?xml version=\"1.0\"?>\r\n")
		TEXT("<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" ")
		TEXT("s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">")
		TEXT("<s:Body><u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>"),
		*ActionName, *ServiceType, *ArgsXml, *ActionName);

	const FTCHARToUTF8 EnvelopeUtf8(*Envelope);

	const FString Request = FString::Printf(
		TEXT("POST %s HTTP/1.1\r\n")
		TEXT("HOST: %s:%d\r\n")
		TEXT("CONTENT-TYPE: text/xml; charset=\"utf-8\"\r\n")
		TEXT("SOAPACTION: \"%s#%s\"\r\n")
		TEXT("CONTENT-LENGTH: %d\r\n")
		TEXT("CONNECTION: close\r\n")
		TEXT("\r\n")
		TEXT("%s"),
		*Path, *Host, Port, *ServiceType, *ActionName, EnvelopeUtf8.Length(), *Envelope);

	int32 StatusCode = 0;
	if (!HttpRequestBlocking(Host, Port, Request, StatusCode, OutResponseBody, HttpTimeoutSeconds))
	{
		return ENatMapResult::Timeout;
	}

	if (StatusCode == 200)
	{
		return ENatMapResult::Success;
	}

	// SOAP 실패는 HTTP 500 에 <errorCode> 를 실어 온다.
	// ★ 본문(OutResponseBody)은 실패해도 채워서 돌려준다 — AddMapping 이 725 를
	//   직접 보고 Lease 없이 재시도해야 하기 때문이다.
	const int32 ErrorCode = FCString::Atoi(*ExtractXmlTag(OutResponseBody, TEXT("errorCode")));

	// ★ 713/714 는 "그 자리에 매핑이 없다" 는 **정상 답변**이지 실패가 아니다.
	//   ListMappings 는 목록의 끝을 713/714 로 알게 되므로, 목록을 한 번 읽을 때마다
	//   경고가 반드시 한 줄 찍힌다. 그러면 멀쩡한 조회가 고장난 것처럼 보인다.
	//   (DeleteMapping 도 이미 714 를 성공으로 취급한다)
	const bool bNoSuchEntry = (ErrorCode == 713 || ErrorCode == 714);
	UE_CLOG(!bNoSuchEntry, LogMOUServer, Warning, TEXT("[NAT] %s 실패. HTTP %d, UPnP 오류 %d"),
	       *ActionName, StatusCode, ErrorCode);
	UE_CLOG(bNoSuchEntry, LogMOUServer, Verbose, TEXT("[NAT] %s: 해당 매핑 없음 (UPnP %d)"),
	       *ActionName, ErrorCode);

	switch (ErrorCode)
	{
	case 718:   // ConflictInMappingEntry — 다른 기기가 그 외부 포트를 이미 쓰고 있다
		return ENatMapResult::PortConflict;
	case 714:   // NoSuchEntryInArray — 지우려는 매핑이 원래 없다
	case 725:   // OnlyPermanentLeasesSupported — Lease 를 못 받는 공유기
	case 401:   // InvalidAction
	case 402:   // InvalidArgs
	case 501:   // ActionFailed
	case 606:   // ActionNotAuthorized
		return ENatMapResult::GatewayRefused;
	default:
		return (StatusCode == 0) ? ENatMapResult::NetworkError : ENatMapResult::Unknown;
	}
}

// ─────────────────────────────────────────────────────────────────────
// 2단계 — 외부 IP 확인 (CGNAT 판별)
// ─────────────────────────────────────────────────────────────────────

ENatMapResult FNatPortMapping::GetExternalIP(FString& OutIP)
{
	OutIP.Reset();

	FString Response;
	const ENatMapResult Result = SendSoapAction(TEXT("GetExternalIPAddress"), FString(), Response);
	if (Result != ENatMapResult::Success)
	{
		return Result;
	}

	OutIP = ExtractXmlTag(Response, TEXT("NewExternalIPAddress"));
	if (OutIP.IsEmpty())
	{
		return ENatMapResult::Unknown;
	}

	// ★ 공유기의 "외부" IP 가 사설 대역이면, 그 위에 통신사 NAT 이 한 겹 더 있다는 뜻이다.
	//   이 경우 포트를 열어봐야 통신사 장비에서 막히므로 매핑을 시도할 필요가 없다.
	//   UPnP 로는 풀 수 없는 문제고, 릴레이(EOS/Steam P2P)로 가야 한다.
	if (IsPrivateAddress(OutIP))
	{
		UE_LOG(LogMOUServer, Warning,
		       TEXT("[NAT] 공유기의 외부 IP 가 사설 대역이다 (%s). CGNAT 환경이라 포트포워딩으로는 못 뚫는다."),
		       *OutIP);
		return ENatMapResult::CarrierGradeNat;
	}

	UE_LOG(LogMOUServer, Log, TEXT("[NAT] 외부 IP: %s"), *OutIP);
	return ENatMapResult::Success;
}

// ─────────────────────────────────────────────────────────────────────
// 3단계 — 매핑 추가
// ─────────────────────────────────────────────────────────────────────

ENatMapResult FNatPortMapping::AddMapping(uint16 InternalPort,
                                          uint16 DesiredExternalPort,
                                          bool bUdp,
                                          const FString& Description,
                                          uint32 LeaseSeconds,
                                          FNatMappingHandle& OutHandle)
{
	OutHandle = FNatMappingHandle();

	if (ControlUrl.IsEmpty())
	{
		return ENatMapResult::NoGatewayFound;
	}

	// NewInternalClient 는 "이 LAN 안에서 누구에게 넘길 것인가" 다.
	// 여기가 틀리면 공유기는 매핑을 만들어주고도 엉뚱한 기기로 패킷을 보낸다.
	const FString LocalIp = GetLocalIpForGateway();
	if (LocalIp.IsEmpty())
	{
		UE_LOG(LogMOUServer, Warning, TEXT("[NAT] 내 LAN IP 를 못 찾았다."));
		return ENatMapResult::NetworkError;
	}

	auto BuildArgs = [&](uint32 Lease)
	{
		return FString::Printf(
			TEXT("<NewRemoteHost></NewRemoteHost>")
			TEXT("<NewExternalPort>%u</NewExternalPort>")
			TEXT("<NewProtocol>%s</NewProtocol>")
			TEXT("<NewInternalPort>%u</NewInternalPort>")
			TEXT("<NewInternalClient>%s</NewInternalClient>")
			TEXT("<NewEnabled>1</NewEnabled>")
			TEXT("<NewPortMappingDescription>%s</NewPortMappingDescription>")
			TEXT("<NewLeaseDuration>%u</NewLeaseDuration>"),
			static_cast<uint32>(DesiredExternalPort),
			bUdp ? TEXT("UDP") : TEXT("TCP"),
			static_cast<uint32>(InternalPort),
			*LocalIp,
			*Description,
			Lease);
	};

	FString Response;
	ENatMapResult Result = SendSoapAction(TEXT("AddPortMapping"), BuildArgs(LeaseSeconds), Response);

	// ★ Lease 를 못 받는 공유기가 흔하다 (UPnP 오류 725).
	//   그럴 때는 영구 매핑(0)으로 한 번 더 시도한다. 대신 영구로 잡히면
	//   DeleteMapping 을 반드시 불러야 한다 — 안 부르면 공유기에 그대로 남는다.
	if (Result != ENatMapResult::Success && LeaseSeconds != 0)
	{
		const int32 ErrorCode = FCString::Atoi(*ExtractXmlTag(Response, TEXT("errorCode")));
		if (ErrorCode == 725)
		{
			UE_LOG(LogMOUServer, Log, TEXT("[NAT] Lease 를 지원하지 않는 공유기다. 영구 매핑으로 재시도한다."));
			Result = SendSoapAction(TEXT("AddPortMapping"), BuildArgs(0), Response);
		}
	}

	if (Result != ENatMapResult::Success)
	{
		// PortConflict 면 호출자가 다른 외부 포트로 다시 부르면 된다.
		// 리슨서버는 내부 포트 그대로 두고, CreateRoom 에 "외부" 포트만 신고하면
		// 프로토콜도 Server.exe 도 안 바뀐다 (헤더 상단 참고).
		return Result;
	}

	OutHandle.ExternalPort = DesiredExternalPort;
	OutHandle.InternalPort = InternalPort;
	OutHandle.bUdp         = bUdp;
	OutHandle.ControlUrl   = ControlUrl;
	OutHandle.ServiceType  = ServiceType;
	OutHandle.Description  = Description;

	UE_LOG(LogMOUServer, Log, TEXT("[NAT] 매핑 성공: 외부 %u -> %s:%u (%s)"),
	       static_cast<uint32>(DesiredExternalPort), *LocalIp,
	       static_cast<uint32>(InternalPort), bUdp ? TEXT("UDP") : TEXT("TCP"));

	return ENatMapResult::Success;
}

// ─────────────────────────────────────────────────────────────────────
// 4단계 — 갱신
// ─────────────────────────────────────────────────────────────────────

ENatMapResult FNatPortMapping::RefreshMaping(const FNatMappingHandle& Handle, uint32 LeaseSeconds)
{
	if (!Handle.IsValid())
	{
		return ENatMapResult::Unknown;
	}

	// UPnP 에는 갱신 액션이 따로 없다. 같은 인자로 AddPortMapping 을 다시 부르면
	// 기존 항목을 덮어쓰는 것이 규격이다.
	//
	// ★ 설명을 핸들에서 가져와야 한다. 여기서 다른 문자열을 쓰면 갱신할 때마다
	//   공유기 목록의 설명이 바뀌고, 설명으로 소유자를 가리는 청소 로직이
	//   그 항목을 남의 것으로 보고 영영 안 지운다.
	FNatMappingHandle Unused;
	return AddMapping(Handle.InternalPort, Handle.ExternalPort, Handle.bUdp,
	                  Handle.Description.IsEmpty() ? MOUNat::MappingDescription : *Handle.Description,
	                  LeaseSeconds, Unused);
}

// ─────────────────────────────────────────────────────────────────────
// 5단계 — 해제
// ─────────────────────────────────────────────────────────────────────

ENatMapResult FNatPortMapping::DeleteMapping(const FNatMappingHandle& Handle)
{
	if (!Handle.IsValid())
	{
		return ENatMapResult::Unknown;
	}

	// 핸들이 제어 URL 을 들고 있는 이유가 이것이다 —
	// 종료 시점에 SSDP 탐색을 다시 돌리면 몇 초가 더 걸리고, 그 사이 공유기가
	// 응답하지 않으면 매핑을 못 지운 채 프로세스가 죽는다.
	TGuardValue<FString> ControlGuard(ControlUrl,
		Handle.ControlUrl.IsEmpty() ? ControlUrl : Handle.ControlUrl);
	TGuardValue<FString> ServiceGuard(ServiceType,
		Handle.ServiceType.IsEmpty() ? ServiceType : Handle.ServiceType);

	const FString Args = FString::Printf(
		TEXT("<NewRemoteHost></NewRemoteHost>")
		TEXT("<NewExternalPort>%u</NewExternalPort>")
		TEXT("<NewProtocol>%s</NewProtocol>"),
		static_cast<uint32>(Handle.ExternalPort),
		Handle.bUdp ? TEXT("UDP") : TEXT("TCP"));

	FString Response;
	const ENatMapResult Result = SendSoapAction(TEXT("DeletePortMapping"), Args, Response);

	// 이미 없는 매핑을 지우려 한 것(714)은 실패로 볼 이유가 없다.
	// 공유기가 재부팅됐거나 Lease 가 만료된 경우인데, 원하던 상태(없음)는 같다.
	if (Result != ENatMapResult::Success)
	{
		const int32 ErrorCode = FCString::Atoi(*ExtractXmlTag(Response, TEXT("errorCode")));
		if (ErrorCode == 714)
		{
			return ENatMapResult::Success;
		}
		return Result;
	}

	UE_LOG(LogMOUServer, Log, TEXT("[NAT] 매핑 해제: 외부 %u"), static_cast<uint32>(Handle.ExternalPort));
	return ENatMapResult::Success;
}

// ─────────────────────────────────────────────────────────────────────
// 6단계 — 조회 / 청소 (2026-08-28)
//
// [왜 만들었나]
//   공용 네트워크라 공유기 관리페이지에 못 들어간다. 그런데 게임이 크래시로 죽으면
//   DeleteMapping 이 안 불려 매핑이 남고, 다음 실행은 718(PortConflict)을 받아
//   7778, 7779... 로 밀려간다. 밀린 포트로 방이 광고되면 아무도 못 들어온다.
//
//   UPnP 규격에 조회 액션이 있다는 것이 답이다. 관리자 로그인이 필요 없고,
//   공유기가 우리에게 매핑 테이블을 그대로 읽어준다.
// ─────────────────────────────────────────────────────────────────────

ENatMapResult FNatPortMapping::QueryMappingEntry(const FString& ActionName,
                                                 const FString& ArgsXml,
                                                 FNatMappingEntry& OutEntry,
                                                 bool& bOutEndOfList)
{
	OutEntry       = FNatMappingEntry();
	bOutEndOfList  = false;

	FString Response;
	const ENatMapResult Result = SendSoapAction(ActionName, ArgsXml, Response);

	if (Result != ENatMapResult::Success)
	{
		// 713 SpecifiedArrayIndexInvalid / 714 NoSuchEntryInArray.
		// 목록을 끝까지 읽었다는 뜻이다. 실패로 올리면 호출자가 오류로 착각한다.
		const int32 ErrorCode = FCString::Atoi(*ExtractXmlTag(Response, TEXT("errorCode")));
		if (ErrorCode == 713 || ErrorCode == 714)
		{
			bOutEndOfList = true;
			return ENatMapResult::Success;
		}
		return Result;
	}

	// GetGeneric 은 외부 포트/프로토콜까지 돌려주고, GetSpecific 은 그 둘이 입력이라
	// 응답에 없다. 없으면 비워두고 호출자가 채운다.
	const FString ExternalText = ExtractXmlTag(Response, TEXT("NewExternalPort"));
	const FString ProtocolText = ExtractXmlTag(Response, TEXT("NewProtocol"));

	OutEntry.ExternalPort   = static_cast<uint16>(FCString::Atoi(*ExternalText));
	OutEntry.InternalPort   = static_cast<uint16>(FCString::Atoi(*ExtractXmlTag(Response, TEXT("NewInternalPort"))));
	OutEntry.InternalClient = ExtractXmlTag(Response, TEXT("NewInternalClient"));
	OutEntry.Description    = ExtractXmlTag(Response, TEXT("NewPortMappingDescription"));
	OutEntry.LeaseSeconds   = static_cast<uint32>(FCString::Atoi(*ExtractXmlTag(Response, TEXT("NewLeaseDuration"))));
	OutEntry.bEnabled       = ExtractXmlTag(Response, TEXT("NewEnabled")) != TEXT("0");
	OutEntry.bUdp           = !ProtocolText.Equals(TEXT("TCP"), ESearchCase::IgnoreCase);

	return ENatMapResult::Success;
}

ENatMapResult FNatPortMapping::ListMappings(TArray<FNatMappingEntry>& OutEntries, int32 MaxEntries)
{
	OutEntries.Reset();

	if (ControlUrl.IsEmpty())
	{
		return ENatMapResult::NoGatewayFound;
	}

	for (int32 Index = 0; Index < MaxEntries; ++Index)
	{
		const FString Args = FString::Printf(
			TEXT("<NewPortMappingIndex>%d</NewPortMappingIndex>"), Index);

		FNatMappingEntry Entry;
		bool bEndOfList = false;
		const ENatMapResult Result =
			QueryMappingEntry(TEXT("GetGenericPortMappingEntry"), Args, Entry, bEndOfList);

		if (bEndOfList)
		{
			return ENatMapResult::Success;   // 정상 종료. 여기까지가 전부다
		}
		if (Result != ENatMapResult::Success)
		{
			// 중간에 끊겨도 지금까지 읽은 것은 그대로 돌려준다.
			// 부분 목록이라도 있는 편이 아무것도 없는 것보다 낫다.
			return Result;
		}

		OutEntries.Add(MoveTemp(Entry));
	}

	UE_LOG(LogMOUServer, Warning,
	       TEXT("[NAT] 매핑이 %d개를 넘는다. 목록을 여기서 끊는다."), MaxEntries);
	return ENatMapResult::Success;
}

ENatMapResult FNatPortMapping::GetMappingFor(uint16 ExternalPort, bool bUdp,
                                             FNatMappingEntry& OutEntry, bool& bOutFound)
{
	OutEntry  = FNatMappingEntry();
	bOutFound = false;

	if (ControlUrl.IsEmpty())
	{
		return ENatMapResult::NoGatewayFound;
	}

	const FString Args = FString::Printf(
		TEXT("<NewRemoteHost></NewRemoteHost>")
		TEXT("<NewExternalPort>%u</NewExternalPort>")
		TEXT("<NewProtocol>%s</NewProtocol>"),
		static_cast<uint32>(ExternalPort),
		bUdp ? TEXT("UDP") : TEXT("TCP"));

	bool bEndOfList = false;
	const ENatMapResult Result =
		QueryMappingEntry(TEXT("GetSpecificPortMappingEntry"), Args, OutEntry, bEndOfList);

	if (Result != ENatMapResult::Success)
	{
		return Result;
	}
	if (bEndOfList)
	{
		return ENatMapResult::Success;   // 그 포트에는 매핑이 없다. 오류가 아니다
	}

	// GetSpecific 응답에는 외부 포트와 프로토콜이 없다. 물어본 값으로 채운다.
	OutEntry.ExternalPort = ExternalPort;
	OutEntry.bUdp         = bUdp;
	bOutFound             = true;
	return ENatMapResult::Success;
}

ENatMapResult FNatPortMapping::DeleteMappingByPort(uint16 ExternalPort, bool bUdp)
{
	// ControlUrl/ServiceType 은 비워둔다 — DeleteMapping 의 TGuardValue 가
	// 비어 있으면 현재 값을 그대로 쓴다.
	FNatMappingHandle Temp;
	Temp.ExternalPort = ExternalPort;
	Temp.bUdp         = bUdp;
	return DeleteMapping(Temp);
}

// ─────────────────────────────────────────────────────────────────────
// 보조
// ─────────────────────────────────────────────────────────────────────

bool FNatPortMapping::IsPrivateAddress(const FString& Ip)
{
	TArray<FString> Octets;
	Ip.ParseIntoArray(Octets, TEXT("."), true);
	if (Octets.Num() != 4)
	{
		return false;
	}

	const int32 First  = FCString::Atoi(*Octets[0]);
	const int32 Second = FCString::Atoi(*Octets[1]);

	if (First == 10)                                    return true;   // 10.0.0.0/8
	if (First == 127)                                   return true;   // 루프백
	if (First == 172 && Second >= 16 && Second <= 31)   return true;   // 172.16.0.0/12
	if (First == 192 && Second == 168)                  return true;   // 192.168.0.0/16
	if (First == 169 && Second == 254)                  return true;   // 링크 로컬
	if (First == 100 && Second >= 64 && Second <= 127)  return true;   // 100.64.0.0/10 = CGNAT

	return false;
}

FString FNatPortMapping::GetLocalIpForGateway() const
{
	ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Subsystem || ControlUrl.IsEmpty())
	{
		return FString();
	}

	FString GatewayHost;
	int32   GatewayPort = 0;
	FString GatewayPath;
	if (!ParseHttpUrl(ControlUrl, GatewayHost, GatewayPort, GatewayPath))
	{
		return FString();
	}

	// 1순위: UDP 소켓을 공유기 쪽으로 "연결" 해보고 OS 가 고른 출발지 주소를 읽는다.
	// 패킷은 나가지 않는다. NIC 가 여러 개(유선+무선+가상)일 때 어느 것이
	// 실제로 공유기로 가는 길인지 아는 방법은 이게 가장 확실하다.
	{
		const TSharedPtr<FInternetAddr> GatewayAddr = ResolveAddress(*Subsystem, GatewayHost, GatewayPort);
		if (GatewayAddr.IsValid())
		{
			FSocketGuard Guard(Subsystem->CreateSocket(NAME_DGram, TEXT("MOU.UPnP.RouteProbe"),
			                                           GatewayAddr->GetProtocolType()));
			if (Guard.Socket && Guard.Socket->Connect(*GatewayAddr))
			{
				TSharedRef<FInternetAddr> LocalAddr = Subsystem->CreateInternetAddr();
				Guard.Socket->GetAddress(*LocalAddr);

				const FString Candidate = LocalAddr->ToString(false);
				if (!Candidate.IsEmpty() && Candidate != TEXT("0.0.0.0"))
				{
					return Candidate;
				}
			}
		}
	}

	// 2순위: 어댑터를 전부 훑어서 공유기와 앞자리가 가장 많이 겹치는 것을 고른다.
	TArray<TSharedPtr<FInternetAddr>> Adapters;
	if (Subsystem->GetLocalAdapterAddresses(Adapters))
	{
		FString Best;
		int32   BestScore = -1;

		TArray<FString> GatewayOctets;
		GatewayHost.ParseIntoArray(GatewayOctets, TEXT("."), true);

		for (const TSharedPtr<FInternetAddr>& Adapter : Adapters)
		{
			if (!Adapter.IsValid())
			{
				continue;
			}

			const FString Candidate = Adapter->ToString(false);
			if (Candidate.IsEmpty() || Candidate == TEXT("0.0.0.0") || Candidate.StartsWith(TEXT("127.")))
			{
				continue;
			}

			TArray<FString> CandidateOctets;
			Candidate.ParseIntoArray(CandidateOctets, TEXT("."), true);

			int32 Score = 0;
			for (int32 Index = 0; Index < FMath::Min3(GatewayOctets.Num(), CandidateOctets.Num(), 4); ++Index)
			{
				if (GatewayOctets[Index] != CandidateOctets[Index])
				{
					break;
				}
				++Score;
			}

			if (Score > BestScore)
			{
				BestScore = Score;
				Best      = Candidate;
			}
		}

		return Best;
	}

	return FString();
}
