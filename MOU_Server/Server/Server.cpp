// MOU 채팅 서버 (1~3단계: 프로토콜 + 세션 + 프레이밍)
//
// 리슨서버와 별개의 프로세스로 상시 가동된다.
// 호스트가 게임을 종료해도 이 프로세스는 살아있으므로 채팅 로그가 유지된다.
//
// 사용법: Server <port> [db경로] [--upnp] [--public-ip <주소>]

#include "Accounts.h"
#include "ChatLog.h"
#include "DirectMessages.h"
#include "Friends.h"
#include "NatPortMapping.h"
#include "Rooms.h"
#include "Session.h"
#include "UdpRelay.h"
#include "Framing.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace MOU;

namespace
{
	SessionManager GSessions;
	std::atomic<bool> GRunning{ true };

	// 이 서버가 외부에 노출된 주소. --public-ip 인자나 UPnP 결과로 채워진다.
	// 비어 있으면 치환을 하지 않는다(예전과 똑같이 동작한다).
	std::string GPublicIp;

	/** --relay 로 켰을 때만 만드는, 게임 패킷 비해석 UDP 릴레이. */
	std::unique_ptr<UdpRelay> GRelay;
	std::string GRelayPublicIp;
	std::string GRelayLanIp;
	std::atomic<uint64_t> GNextRelayRouteId{ 1 };

	/** 한 참여자에게 줄 host/guest capability 는 반드시 분리한다. */
	struct FRelayRouteAssignment
	{
		uint64_t        GuestUserId = 0;
		RelayHostRoute  Host;
		RelayGuestRoute Guest;
	};

	std::mutex GRelayRoutesMutex;
	std::unordered_map<uint32_t, std::vector<FRelayRouteAssignment>> GRelayRoutesByRoom;

	/**
	 * 사설/루프백 대역인가.
	 *
	 * [왜 필요한가]
	 *   방의 호스트 주소는 accept() 에서 읽은 상대 IP 를 쓴다. 호스트가 서버와
	 *   **다른** 공유기 뒤에 있으면 그 값이 곧 호스트의 공인 IP 라 정확하다.
	 *
	 *   그런데 호스트가 서버와 **같은** 공유기 안에 있으면(특히 서버 PC 본인이
	 *   방장일 때) 그 값이 사설 IP 가 된다. 게다가 호스트가 공인 IP 로 접속해
	 *   헤어핀을 타면 공유기가 출발지를 자기 주소로 바꿔서 게이트웨이 IP
	 *   (예: 192.168.35.1) 가 찍힌다. 그 주소를 받은 외부 참가자는 자기 네트워크의
	 *   엉뚱한 기기로 가거나 아무 데도 못 가서 무한 로딩에 걸린다.
	 *
	 *   그래서 "사설이면 서버 자신의 공인 IP 로 바꾼다" 는 판정이 필요하다.
	 */
	bool IsPrivateAddress(const std::string& Address)
	{
		unsigned A = 0, B = 0, C = 0, D = 0;
		if (std::sscanf(Address.c_str(), "%u.%u.%u.%u", &A, &B, &C, &D) != 4)
		{
			return false;   // 파싱 실패. 함부로 바꾸지 않는다
		}

		if (A == 10)  return true;                        // 10.0.0.0/8
		if (A == 127) return true;                        // 루프백
		if (A == 172 && B >= 16 && B <= 31) return true;  // 172.16.0.0/12
		if (A == 192 && B == 168) return true;            // 192.168.0.0/16
		if (A == 169 && B == 254) return true;            // 링크로컬
		if (A == 100 && B >= 64 && B <= 127) return true; // CGNAT 100.64.0.0/10
		return false;
	}

	/**
	 * 방에 기록할 호스트 주소를 정한다.
	 *
	 * 클라이언트가 보내온 값은 절대 쓰지 않는다 — 남의 주소를 적어 엉뚱한 곳으로
	 * 접속을 몰아주는 장난을 막기 위해서다. 서버가 아는 두 값 중에서만 고른다:
	 *   1) accept() 에서 읽은 상대 IP (공인이면 그대로 쓴다)
	 *   2) 서버 자신의 공인 IP (상대가 사설이면 = 같은 공유기 안에 있다는 뜻)
	 *
	 * [한계] 호스트와 참가자가 **둘 다** 서버와 같은 공유기 안에 있으면, 참가자는
	 *   공인 IP 로 나갔다 돌아오는 헤어핀 접속을 하게 된다. 공유기가 헤어핀을
	 *   지원하지 않으면 그 조합만 실패한다. 전원이 LAN 안에 있는 상황이라면
	 *   애초에 --public-ip 를 주지 않는 편이 낫다.
	 */
	/**
	 * 사설 주소를 서버의 공인 IP 로 치환한다. bVerbose 가 거짓이면 조용히 한다.
	 *
	 * ★ 로그를 끄는 스위치가 필요한 이유: 이 함수는 방을 만들 때(드물다)와
	 *   엔드포인트를 관측할 때(자주, 한 등록당 여러 번) 둘 다 쓴다.
	 *   방 생성용 문구를 후자에서도 찍으면 "호스트가 서버와 같은 네트워크에 있다" 가
	 *   엉뚱한 맥락에서 도배되어 실제로 오해를 샀다.
	 */
	std::string ResolveHostAddress(const std::string& PeerAddress, bool bVerbose = true)
	{
		if (GPublicIp.empty() || !IsPrivateAddress(PeerAddress))
		{
			return PeerAddress;
		}

		if (bVerbose)
		{
			std::printf("[방 주소] 호스트가 서버와 같은 네트워크에 있다(%s). "
			            "외부 참가자를 위해 %s 로 바꿔 기록한다.\n",
			            PeerAddress.c_str(), GPublicIp.c_str());
		}
		return GPublicIp;
	}

	/** std::random_device 는 Windows/Linux 에서 OS 난수원을 사용한다. token 은 로그에 남기지 않는다. */
	bool MakeRelayToken(FUdpRelayToken& OutToken)
	{
		try
		{
			std::random_device Random;
			bool bAnyNonZero = false;
			for (std::uint8_t& Byte : OutToken)
			{
				Byte = static_cast<std::uint8_t>(Random());
				bAnyNonZero = bAnyNonZero || Byte != 0;
			}
			return bAnyNonZero;
		}
		catch (...)
		{
			return false;
		}
	}

	/** RoomStart 의 방장 전용 경로 목록을 복사한다. */
	std::vector<RelayHostRoute> GetHostRelayRoutes(uint32_t RoomId)
	{
		std::vector<RelayHostRoute> Result;
		std::lock_guard<std::mutex> Lock(GRelayRoutesMutex);
		const auto It = GRelayRoutesByRoom.find(RoomId);
		if (It == GRelayRoutesByRoom.end())
		{
			return Result;
		}

		Result.reserve(It->second.size());
		for (const FRelayRouteAssignment& Assignment : It->second)
		{
			Result.push_back(Assignment.Host);
		}
		return Result;
	}

	/** RoomHostReady 를 개인화할 때 해당 참여자의 guest-facing 경로 하나를 얻는다. */
	bool GetGuestRelayRoute(uint32_t RoomId, uint64_t GuestUserId, RelayGuestRoute& OutRoute)
	{
		OutRoute = {};
		std::lock_guard<std::mutex> Lock(GRelayRoutesMutex);
		const auto It = GRelayRoutesByRoom.find(RoomId);
		if (It == GRelayRoutesByRoom.end())
		{
			return false;
		}

		for (const FRelayRouteAssignment& Assignment : It->second)
		{
			if (Assignment.GuestUserId == GuestUserId)
			{
				OutRoute = Assignment.Guest;
				return true;
			}
		}
		return false;
	}

	void SetRelayAddress(RelayHostRoute& Route, const std::string& Address)
	{
		CopyFixedString(Route.Address, kMaxAddressLen, Address);
	}

	void SetRelayAddress(RelayGuestRoute& Route, const std::string& Address)
	{
		CopyFixedString(Route.Address, kMaxAddressLen, Address);
	}

	void ReleaseRelayRoutesForRoom(uint32_t RoomId)
	{
		if (RoomId == 0)
		{
			return;
		}
		{
			std::lock_guard<std::mutex> Lock(GRelayRoutesMutex);
			GRelayRoutesByRoom.erase(RoomId);
		}
		if (GRelay)
		{
			GRelay->RemoveRoutesForRoom(RoomId);
		}
	}

	void ReleaseRelayRouteForGuest(uint32_t RoomId, uint64_t GuestUserId)
	{
		uint64_t RouteId = 0;
		{
			std::lock_guard<std::mutex> Lock(GRelayRoutesMutex);
			const auto It = GRelayRoutesByRoom.find(RoomId);
			if (It == GRelayRoutesByRoom.end())
			{
				return;
			}
			for (auto RouteIt = It->second.begin(); RouteIt != It->second.end(); ++RouteIt)
			{
				if (RouteIt->GuestUserId == GuestUserId)
				{
					RouteId = RouteIt->Host.RouteId;
					It->second.erase(RouteIt);
					break;
				}
			}
			if (It->second.empty())
			{
				GRelayRoutesByRoom.erase(It);
			}
		}
		if (RouteId != 0 && GRelay)
		{
			GRelay->RemoveRoute(RouteId);
		}
	}

	/** 게임 시작 시 모든 참여자별 relay 포트 쌍과 서로 다른 capability 를 만든다. */
	bool AllocateRelayRoutes(uint32_t RoomId, uint64_t HostUserId,
	                         const std::vector<uint64_t>& Recipients)
	{
		if (!GRelay || !GRelay->IsRunning() || GRelayPublicIp.empty())
		{
			return false;
		}

		std::vector<FRelayRouteAssignment> Created;
		Created.reserve(Recipients.size());
		for (const uint64_t UserId : Recipients)
		{
			if (UserId == HostUserId)
			{
				continue;
			}

			FUdpRelayToken HostToken{};
			FUdpRelayToken GuestToken{};
			FUdpRelayPortPair Ports{};
			const uint64_t RouteId = GNextRelayRouteId.fetch_add(1);
			if (RouteId == 0 || !MakeRelayToken(HostToken) || !MakeRelayToken(GuestToken) ||
				HostToken == GuestToken || !GRelay->CreateRoute(RouteId, HostToken, GuestToken, RoomId, Ports))
			{
				for (const FRelayRouteAssignment& Assignment : Created)
				{
					GRelay->RemoveRoute(Assignment.Host.RouteId);
				}
				return false;
			}

			FRelayRouteAssignment Assignment{};
			Assignment.GuestUserId = UserId;
			CopyFixedString(Assignment.Host.Address, kMaxAddressLen, GRelayPublicIp);
			Assignment.Host.HostPort = Ports.HostPort;
			Assignment.Host.RouteId = RouteId;
			std::copy(HostToken.begin(), HostToken.end(), Assignment.Host.HostToken);

			CopyFixedString(Assignment.Guest.Address, kMaxAddressLen, GRelayPublicIp);
			Assignment.Guest.GuestPort = Ports.GuestPort;
			Assignment.Guest.RouteId = RouteId;
			std::copy(GuestToken.begin(), GuestToken.end(), Assignment.Guest.GuestToken);
			Created.push_back(Assignment);
		}

		if (Created.empty())
		{
			return true;  // 혼자 시작한 방은 relay 경로가 필요 없다.
		}

		std::lock_guard<std::mutex> Lock(GRelayRoutesMutex);
		GRelayRoutesByRoom[RoomId] = std::move(Created);
		return true;
	}

	/**
	 * 도달성 프로브를 쏠 UDP 소켓. (v9)
	 *
	 * ★ 보내기 전용이다. bind 하지 않으므로 서버 쪽 공유기에 인바운드 포워딩을
	 *   새로 넣을 필요가 없다. 응답은 UDP 가 아니라 호스트가 TCP 로 신고한다.
	 *
	 * 한 번 만들어 두고 계속 쓴다. 프로브마다 소켓을 새로 여는 것은 낭비고,
	 * 짧은 시간에 여러 방이 만들어지면 소켓 고갈로 이어질 수 있다.
	 */
	SocketHandle GProbeSock = kInvalidSocket;

	/** 호스트의 공인주소:포트 로 프로브 한 발. 성공하면 true. */
	bool SendHostProbe(const std::string& Address, uint16_t Port, uint32_t Nonce)
	{
		if (GProbeSock == kInvalidSocket || Address.empty() || Port == 0)
		{
			return false;
		}

		sockaddr_in Dest{};
		Dest.sin_family = AF_INET;
		Dest.sin_port   = ::htons(Port);
		if (::inet_pton(AF_INET, Address.c_str(), &Dest.sin_addr) != 1)
		{
			return false;
		}

		HostProbeDatagram Datagram{};
		Datagram.Magic = kHostProbeMagic;
		Datagram.Nonce = Nonce;

		const int Sent = ::sendto(GProbeSock, reinterpret_cast<const char*>(&Datagram),
		                          static_cast<int>(sizeof(Datagram)), 0,
		                          reinterpret_cast<const sockaddr*>(&Dest), sizeof(Dest));
		return Sent == static_cast<int>(sizeof(Datagram));
	}

	/**
	 * 참여자가 보낸 등록 데이터그램을 처리한다. (v10)
	 *
	 * 이 함수의 존재 이유가 곧 홀펀칭의 전부다 — 방장은 참여자의 공인 게임
	 * 엔드포인트를 알 방법이 없고(그 패킷 자체가 방장의 NAT 에서 막힌다),
	 * 바깥에 있으면서 양쪽과 이미 이야기하고 있는 것은 이 서버뿐이다.
	 *
	 * ★ 주소는 **관측값**이다. 데이터그램 안에는 UserId 만 들어 있고 주소는 없다.
	 *   그래도 UserId 는 위조할 수 있으므로, TCP 세션의 공인 IP 와 다르면 버린다.
	 *   그러면 남을 엉뚱한 곳으로 punch 하게 만들 수 없다.
	 */
	void HandleClientEndpointDatagram(const ClientEndpointDatagram& Datagram,
	                                  const sockaddr_in& From)
	{
		char AddrText[INET_ADDRSTRLEN] = {};
		if (::inet_ntop(AF_INET, &From.sin_addr, AddrText, sizeof(AddrText)) == nullptr)
		{
			return;
		}
		// ★ 방 주소와 같은 규칙을 여기에도 건다.
		//   서버와 같은 공유기 안에 있는 사람은 출발지가 사설 주소(대개 게이트웨이)로
		//   보인다. 그 값을 그대로 punch 대상으로 넘기면 방장은 자기 LAN 의 엉뚱한
		//   기기로 쏘게 되고, 구멍은 아무 데도 안 뚫린다.
		//   실제로 Player1 의 엔드포인트가 192.168.35.1 로 관측되고 있었다.
		// 방 주소와 같은 규칙. 조용히 한다 — 등록은 자주 오고, 방 생성용 문구를
		// 여기서 찍으면 엉뚱한 맥락에서 도배된다.
		const std::string SourceAddress = ResolveHostAddress(AddrText, /*bVerbose=*/false);
		const uint16_t    SourcePort    = ::ntohs(From.sin_port);

		SessionPtr Target;
		GSessions.ForEach([&](const SessionPtr& Session)
		{
			if (Session->bAuthed && Session->UserId == Datagram.UserId)
			{
				Target = Session;
			}
		});

		if (!Target)
		{
			std::printf("[엔드포인트] 모르는 UserId %llu 의 등록을 버린다 (%s:%u)\n",
			            static_cast<unsigned long long>(Datagram.UserId),
			            SourceAddress.c_str(), SourcePort);
			return;
		}

		// ★ 위조 차단. 남의 UserId 를 적어 보내면 여기서 걸린다 —
		//   그 사람의 TCP 연결은 다른 IP 에서 오고 있기 때문이다.
		if (ResolveHostAddress(Target->PeerAddress, /*bVerbose=*/false) != SourceAddress)
		{
			std::printf("[거부] 엔드포인트 등록의 출발지가 세션과 다르다: %s (세션은 %s)\n",
			            SourceAddress.c_str(), Target->PeerAddress.c_str());
			return;
		}

		Target->GameEndpointAddress = SourceAddress;
		Target->GameEndpointPort    = SourcePort;

		// ★ 이 사람이 방장이면 방의 공인 후보를 관측값으로 덮는다. (v10)
		//
		//   방을 만들 때 적은 공인 후보는 UPnP 가 알려준 포트다. 그런데 홀펀칭으로
		//   실제 뚫리는 구멍은 **동적 바인딩의 포트**이고, UPnP 정적 매핑이 그 번호를
		//   점유하고 있으면 둘이 달라진다(실측: UPnP 있을 때 1035, 없을 때 7777).
		//   참여자가 붙어야 하는 곳은 뚫린 쪽이다.
		Rooms::UpdateHostEndpoint(Target->UserId, SourceAddress, SourcePort);

		std::printf("[엔드포인트] %s(%llu) 의 게임 주소를 %s:%u 로 관측했다\n",
		            Target->Name.c_str(),
		            static_cast<unsigned long long>(Target->UserId),
		            SourceAddress.c_str(), SourcePort);

		ClientEndpointAckBody Ack{};
		Ack.Nonce     = Datagram.Nonce;
		Ack.Port      = SourcePort;
		Ack.bObserved = 1;
		CopyFixedString(Ack.Address, kMaxAddressLen, SourceAddress);
		SendPacket(Target->Sock, EOpcode::ClientEndpointAck, &Ack, sizeof(Ack));
	}

	/**
	 * 프로브 소켓으로 들어오는 UDP 를 계속 읽는다. 전용 스레드에서 돈다.
	 *
	 * 지금 들어오는 것은 참여자의 엔드포인트 등록 하나뿐이다.
	 * (프로브 응답은 UDP 가 아니라 호스트가 TCP 로 신고한다 — 서버는 쏘기만 했다)
	 */
	void UdpReceiveLoop()
	{
		std::printf("[UDP] 엔드포인트 수신 스레드 시작\n");

		while (GRunning)
		{
			char Buffer[512];
			sockaddr_in From{};
			socklen_t   FromLen = sizeof(From);

			const int Read = ::recvfrom(GProbeSock, Buffer, static_cast<int>(sizeof(Buffer)), 0,
			                            reinterpret_cast<sockaddr*>(&From), &FromLen);
			if (Read < 0)
			{
				if (IsRecvTimeout(LastNetError()))
				{
					continue;   // 타임아웃은 정상이다. GRunning 을 다시 보라는 뜻일 뿐
				}
				if (!GRunning)
				{
					break;
				}
				continue;
			}

			if (Read < static_cast<int>(sizeof(ClientEndpointDatagram)))
			{
				continue;   // 우리 것이 아니다
			}

			ClientEndpointDatagram Datagram{};
			std::memcpy(&Datagram, Buffer, sizeof(Datagram));
			if (Datagram.Magic != kClientEndpointMagic)
			{
				continue;
			}

			HandleClientEndpointDatagram(Datagram, From);
		}

		std::printf("[UDP] 엔드포인트 수신 스레드 종료\n");
	}

	/** HostCandidate 하나를 채운다. 주소가 비었거나 너무 길면 아무것도 하지 않고 false. */
	bool PushCandidate(std::vector<HostCandidate>& Out, const std::string& Address,
	                   uint16_t Port, EHostAddrKind Kind)
	{
		if (Address.empty() || Address.size() >= kMaxAddressLen || Port == 0)
		{
			return false;
		}
		if (Out.size() >= kMaxHostCandidates)
		{
			return false;
		}
		// 같은 주소가 두 번 들어가는 것을 막는다. 방장이 서버와 같은 LAN 이면
		// 공인 후보와 사설 후보가 같은 값이 될 수 있다.
		for (const HostCandidate& C : Out)
		{
			if (Address == C.Address && Port == C.Port)
			{
				return false;
			}
		}

		HostCandidate C{};
		CopyFixedString(C.Address, kMaxAddressLen, Address);
		C.Port = Port;
		C.Kind = static_cast<uint8_t>(Kind);
		Out.push_back(C);
		return true;
	}

	/**
	 * 방에 기록할 호스트 주소 후보들을 만든다. (v8)
	 *
	 * [왜 하나가 아니라 목록인가]
	 *   v7 까지는 공인 주소 하나만 내려줬다. 그러면 **방장과 같은 공유기 안에 있는
	 *   참가자**까지 공인 IP 로 나갔다 돌아오는 헤어핀 접속을 해야 한다.
	 *   헤어핀을 지원하지 않는 공유기가 흔해서, 바로 옆자리 사람이 못 들어왔다.
	 *   실측으로 확인한 문제다 — 같은 LAN 에서 사설 IP 로는 즉시 붙는데
	 *   공인 IP 로는 핸드셰이크가 통째로 사라졌다.
	 *
	 * [무엇을 믿는가]
	 *   1) 공인 후보 — accept() 에서 읽은 값. 클라이언트가 못 건드린다
	 *   2) 사설 후보 — 클라이언트 신고값. **사설 대역일 때만** 받는다
	 *
	 *   2번을 그냥 믿으면 남의 공인 주소를 적어 접속을 몰아주는 장난이 가능하다.
	 *   사설 대역으로 제한하면 최악이 "같은 LAN 안의 엉뚱한 기기" 까지고,
	 *   받는 쪽도 자기 서브넷과 맞을 때만 그 후보를 쓴다.
	 *
	 * 순서가 곧 우선순위는 아니다 — 고르는 것은 받는 쪽이다.
	 */
	std::vector<HostCandidate> BuildHostCandidates(const std::string& PeerAddress,
	                                               const std::string& ReportedLanAddress,
	                                               uint16_t HostPort)
	{
		std::vector<HostCandidate> Candidates;

		PushCandidate(Candidates, ResolveHostAddress(PeerAddress), HostPort, EHostAddrKind::Public);

		if (!ReportedLanAddress.empty())
		{
			if (IsPrivateAddress(ReportedLanAddress))
			{
				PushCandidate(Candidates, ReportedLanAddress, HostPort, EHostAddrKind::Lan);
			}
			else
			{
				// 조용히 버리지 않고 남긴다. 공인 주소를 사설 자리에 넣어 보내는 것은
				// 버그이거나 장난이고, 둘 다 알아야 한다.
				std::printf("[거부] 사설이 아닌 주소를 LAN 후보로 신고했다: %s\n",
				            ReportedLanAddress.c_str());
			}
		}

		return Candidates;
	}

	/** 방의 후보 목록을 패킷 배열에 옮긴다. 반환값은 실제로 채운 개수. */
	uint8_t FillCandidates(HostCandidate (&Dest)[kMaxHostCandidates],
	                       const std::vector<HostCandidate>& Src)
	{
		uint8_t Count = 0;
		for (const HostCandidate& C : Src)
		{
			if (Count >= kMaxHostCandidates)
			{
				break;
			}
			Dest[Count++] = C;
		}
		return Count;
	}

	// Ctrl+C 로 서버를 내릴 때 큐에 남은 채팅 로그를 마저 쓰고 나간다.
	// 이게 없으면 accept() 에서 블록된 채 프로세스가 즉사해서
	// 아직 커밋 안 된 로그가 통째로 사라진다.
	//
	// 윈도우 CRT 는 SIGINT 핸들러를 별도 스레드에서 호출하므로
	// 여기서 ChatLog::Stop() 이 라이터 스레드를 join 해도 데드락이 나지 않는다.
	void OnInterrupt(int)
	{
		GRunning = false;

		// ★ 공유기에 열어둔 포트를 먼저 지운다. 영구 매핑이라 여기서 안 지우면
		//   프로세스가 사라져도 공유기에는 그대로 남는다.
		//   네트워크 왕복이라 몇 백 ms 걸릴 수 있는데, 그 대기가 곧 "확실히 지웠다" 는 보장이다.
		//   Start 를 안 했거나 실패했으면 아무 일도 하지 않는다.
		Nat::Stop();

		ChatLog::Stop();
		Accounts::Stop();
		// v7. 둘 다 동기 커밋이라 큐에 남은 것이 없지만, 커넥션을 닫아야
		// WAL 이 정리된다. Start 가 실패했어도 부르는 것이 안전하다.
		Friends::Stop();
		DirectMessages::Stop();
		std::_Exit(0);   // 소켓과 메모리 회수는 OS 에 맡긴다
	}

	const char* ChannelName(EChatChannel Channel)
	{
		switch (Channel)
		{
		case EChatChannel::All:     return "전체";
		case EChatChannel::Team:    return "팀";
		case EChatChannel::Dead:    return "사망";
		case EChatChannel::Whisper: return "귓속말";
		case EChatChannel::System:  return "시스템";
		default:                    return "알수없음";
		}
	}

	// ------------------------------------------------------------------
	// 라우팅
	//
	// 기존 실습의 SendMSG() 는 무조건 전원에게 보냈다.
	// 여기서는 채널에 따라 수신자를 고른다.
	// 귓속말은 9단계에서 case 하나만 추가하면 된다.
	// ------------------------------------------------------------------
	void RouteChat(const SessionPtr& Sender, EChatChannel Channel,
	               const char* Text, uint16_t TextLen)
	{
		// 발화 자격 검증. 클라이언트가 보낸 채널 값을 그대로 믿지 않는다.
		if (Channel == EChatChannel::Dead && !Sender->bDead)
		{
			std::printf("[거부] %s(%llu) 가 살아있는 상태로 사망 채널에 발화 시도\n",
			            Sender->Name.c_str(), static_cast<unsigned long long>(Sender->UserId));
			return;
		}
		if (Channel == EChatChannel::System)
		{
			return;   // 시스템 메시지는 서버만 만든다
		}

		ChatBroadcastBody Out{};
		Out.SenderUserId = Sender->UserId;                        // 서버 보관값
		Out.Timestamp    = static_cast<int64_t>(std::time(nullptr));
		Out.TextLen      = TextLen;
		Out.Channel      = static_cast<uint8_t>(Channel);
		CopyFixedString(Out.SenderName, kMaxNameLen, Sender->Name);   // 서버 보관값

		int DeliverCount = 0;
		GSessions.ForEach([&](const SessionPtr& Target)
		{
			if (!Target->bAuthed)
			{
				return;
			}

			bool bDeliver = false;
			switch (Channel)
			{
			case EChatChannel::All:
				bDeliver = true;
				break;
			case EChatChannel::Team:
				bDeliver = (Target->TeamId == Sender->TeamId);
				break;
			case EChatChannel::Dead:
				bDeliver = Target->bDead;          // 죽은 사람에게만
				break;
			case EChatChannel::Whisper:
				bDeliver = false;                  // 9단계
				break;
			default:
				break;
			}

			if (bDeliver)
			{
				SendPacket2(Target->Sock, EOpcode::ChatBroadcast,
				            &Out, sizeof(Out), Text, TextLen);
				++DeliverCount;
			}
		});

		std::printf("[%s] %s: %.*s  (수신 %d명)\n",
		            ChannelName(Channel), Sender->Name.c_str(),
		            static_cast<int>(TextLen), Text, DeliverCount);

		// 수신자가 0명이어도 기록은 남긴다.
		// "아무도 못 들었지만 말한 것은 사실" 이므로 나중에 신고/관전 조회 때 필요하다.
		// Enqueue 는 큐에 넣기만 하고 바로 돌아오므로 여기서 디스크를 기다리지 않는다.
		ChatLog::Enqueue(Out.Timestamp, Sender->UserId, Sender->Name,
		                 static_cast<uint8_t>(Channel), Sender->TeamId, Text, TextLen);
	}

	// ------------------------------------------------------------------
	// 패킷 핸들러. false 를 반환하면 연결을 끊는다.
	// ------------------------------------------------------------------
	// 계정 모듈의 결과를 프로토콜 사유 코드로 옮긴다.
	// 두 enum 을 따로 두는 이유는 계정 모듈이 프로토콜을 몰라도 되게 하기 위함이다.
	ELoginResult ToLoginResult(EAccountResult R)
	{
		switch (R)
		{
		case EAccountResult::Success:       return ELoginResult::Success;
		case EAccountResult::NotFound:      return ELoginResult::AccountNotFound;
		case EAccountResult::WrongPassword: return ELoginResult::WrongPassword;
		case EAccountResult::DuplicateId:   return ELoginResult::DuplicateId;
		case EAccountResult::InvalidFormat: return ELoginResult::InvalidFormat;
		default:                            return ELoginResult::ServerError;
		}
	}

	const char* AccountResultName(EAccountResult R)
	{
		switch (R)
		{
		case EAccountResult::Success:       return "성공";
		case EAccountResult::NotFound:      return "없는 아이디";
		case EAccountResult::WrongPassword: return "비밀번호 불일치";
		case EAccountResult::DuplicateId:   return "이미 있는 아이디";
		case EAccountResult::InvalidFormat: return "형식 위반";
		default:                            return "서버 오류";
		}
	}

	// 로그인 거부를 사유와 함께 알린다.
	// 그냥 연결을 끊어버리면 클라이언트는 원인을 모른 채 3초마다 재접속만 반복한다.
	// ★ 전방 선언. 정의는 아래 "친구" 절에 있다.
	//
	//   로그인 핸들러가 이 파일에서 가장 먼저 나오는데(계정 -> 로비 -> 친구 순),
	//   접속 알림은 친구 기능이라 정의가 뒤에 온다. 정의를 위로 끌어올리면
	//   그것이 의존하는 SendToUsers/FindAuthedSession 까지 줄줄이 따라 올라와
	//   파일의 주제별 순서가 무너진다. 선언 한 줄이 싸다.
	void BroadcastPresence(const SessionPtr& Subject, EPresence NewPresence);
	void DeliverPendingDirectMessages(const SessionPtr& Session);

	void SendLoginFailure(const SessionPtr& Session, ELoginResult Reason)
	{
		LoginAckBody Ack{};
		Ack.bSuccess      = 0;
		Ack.Result        = static_cast<uint8_t>(Reason);
		Ack.ServerVersion = kProtocolVersion;
		SendPacket(Session->Sock, EOpcode::LoginAck, &Ack, sizeof(Ack));
	}

	bool HandleLoginReq(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		// Version 은 LoginReqBody 의 첫 필드다.
		// 구조체 크기가 안 맞더라도 이 2바이트만은 읽어서 정확한 사유를 돌려준다.
		if (BodySize < sizeof(uint16_t))
		{
			std::printf("[거부] LoginReq 가 너무 짧다 (%u바이트)\n", BodySize);
			SendLoginFailure(Session, ELoginResult::InvalidRequest);
			return false;
		}

		uint16_t ClientVersion = 0;
		std::memcpy(&ClientVersion, Body, sizeof(ClientVersion));

		if (ClientVersion != kProtocolVersion)
		{
			std::printf("[거부] 프로토콜 버전 불일치. 클라이언트=%u, 서버=%u"
			            " (양쪽을 같은 커밋으로 다시 빌드할 것)\n",
			            ClientVersion, kProtocolVersion);
			SendLoginFailure(Session, ELoginResult::VersionMismatch);
			return false;
		}

		if (BodySize < sizeof(LoginReqBody))
		{
			std::printf("[거부] LoginReq 크기 부족 (%u < %u)\n",
			            BodySize, static_cast<uint32_t>(sizeof(LoginReqBody)));
			SendLoginFailure(Session, ELoginResult::InvalidRequest);
			return false;
		}

		LoginReqBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		const std::string LoginId  = ReadFixedString(Req.LoginId,  kMaxLoginIdLen);
		const std::string Password = ReadFixedString(Req.Password, kMaxPasswordLen);

		// 계정 검증. UserId 는 이제 서버가 세는 번호가 아니라 accounts.id 다.
		// 그래서 같은 계정으로 재접속하면 언제나 같은 번호가 나온다.
		uint64_t    AccountId = 0;
		std::string Nickname;
		const EAccountResult AuthResult =
			Accounts::Authenticate(LoginId, Password, AccountId, Nickname);

		if (AuthResult != EAccountResult::Success)
		{
			std::printf("[거부] 로그인 실패: id=%s 사유=%s\n",
			            LoginId.c_str(), AccountResultName(AuthResult));
			SendLoginFailure(Session, ToLoginResult(AuthResult));
			// 연결은 유지한다. 사용자가 비번을 고쳐 다시 시도할 수 있어야 한다.
			return true;
		}

		Session->UserId  = AccountId;
		Session->Name    = Nickname;
		Session->TeamId  = Req.TeamId;
		Session->bAuthed = true;

		// ★ 친구 캐시를 여기서 한 번 채운다(Session.h 의 FriendIds 주석).
		//   이 목록은 "접속 상태가 바뀌었을 때 알려줄 대상" 이라, 상태가 바뀔
		//   때마다 DB 를 때리지 않으려고 캐시한다. 실패해도 로그인은 시킨다 —
		//   친구 목록이 비어 보이는 것과 로그인이 안 되는 것은 심각도가 다르다.
		{
			std::vector<uint64_t> FriendIds;
			if (Friends::GetFriendIds(Session->UserId, FriendIds))
			{
				Session->SetFriendIds(std::move(FriendIds));
			}
			else
			{
				std::printf("[경고] %s 의 친구 목록을 읽지 못했다. 접속 알림이 안 갈 수 있다.\n",
				            Session->Name.c_str());
			}
		}

		LoginAckBody Ack{};
		Ack.UserId        = Session->UserId;
		Ack.TeamId        = Session->TeamId;
		Ack.bSuccess      = 1;
		Ack.Result        = static_cast<uint8_t>(ELoginResult::Success);
		Ack.ServerVersion = kProtocolVersion;
		CopyFixedString(Ack.Name, kMaxNameLen, Session->Name);

		std::printf("[로그인] %s -> UserId=%llu, Team=%d\n",
		            Session->Name.c_str(),
		            static_cast<unsigned long long>(Session->UserId), Session->TeamId);

		// ★ LoginAck 를 먼저 보내고 알린다. 순서를 바꾸면 친구 쪽에서
		//   "온라인" 을 받았는데 정작 본인은 아직 로그인 절차 중인 창이 생긴다.
		const bool bAckSent = SendPacket(Session->Sock, EOpcode::LoginAck, &Ack, sizeof(Ack));

		// 내가 접속했음을 친구들에게 알린다 (M4).
		BroadcastPresence(Session, EPresence::Online);

		// 오프라인 동안 온 DM 을 내려준다 (M5).
		// 친구 목록(FriendListReq)보다 먼저 갈 수 있는데 문제되지 않는다 —
		// 클라는 DM 을 UserId 로 묶어 두었다가 목록이 오면 붙이면 된다.
		DeliverPendingDirectMessages(Session);

		return bAckSent;
	}

	// 계정 생성. 로그인과 달리 세션 상태를 바꾸지 않는다.
	// 가입에 성공해도 자동 로그인은 시키지 않고, 클라이언트가 이어서 LoginReq 를 보낸다.
	// 가입과 로그인을 분리해두면 나중에 "가입 즉시 이메일 인증" 같은 단계를 끼우기 쉽다.
	bool HandleRegisterReq(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		auto SendResult = [&](ELoginResult Reason)
		{
			RegisterAckBody Ack{};
			Ack.bSuccess      = (Reason == ELoginResult::Success) ? 1 : 0;
			Ack.Result        = static_cast<uint8_t>(Reason);
			Ack.ServerVersion = kProtocolVersion;
			return SendPacket(Session->Sock, EOpcode::RegisterAck, &Ack, sizeof(Ack));
		};

		if (BodySize < sizeof(uint16_t))
		{
			return SendResult(ELoginResult::InvalidRequest);
		}

		uint16_t ClientVersion = 0;
		std::memcpy(&ClientVersion, Body, sizeof(ClientVersion));
		if (ClientVersion != kProtocolVersion)
		{
			std::printf("[거부] 가입 요청 버전 불일치. 클라이언트=%u, 서버=%u\n",
			            ClientVersion, kProtocolVersion);
			return SendResult(ELoginResult::VersionMismatch);
		}

		if (BodySize < sizeof(RegisterReqBody))
		{
			return SendResult(ELoginResult::InvalidRequest);
		}

		RegisterReqBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		const std::string LoginId  = ReadFixedString(Req.LoginId,  kMaxLoginIdLen);
		const std::string Password = ReadFixedString(Req.Password, kMaxPasswordLen);
		const std::string Nickname = ReadFixedString(Req.Nickname, kMaxNameLen);

		uint64_t NewUserId = 0;
		const EAccountResult R = Accounts::Create(LoginId, Password, Nickname, NewUserId);

		if (R == EAccountResult::Success)
		{
			std::printf("[가입] %s (닉네임 %s) -> UserId=%llu\n",
			            LoginId.c_str(), Nickname.c_str(),
			            static_cast<unsigned long long>(NewUserId));
		}
		else
		{
			std::printf("[거부] 가입 실패: id=%s 사유=%s\n",
			            LoginId.c_str(), AccountResultName(R));
		}

		return SendResult(ToLoginResult(R));
	}

	// ------------------------------------------------------------------
	// 로비 / 대기실
	//
	// 게임 트래픽은 여기를 지나가지 않는다. 참가자는 호스트의 리슨서버에 직접 붙는다.
	// 이 서버가 관리하는 것은 "누가 어느 방에 있고 준비했는가" 까지다.
	//
	// [락 순서] Rooms:: 함수는 세션 락을 잡지 않고 대상 UserId 목록만 돌려준다.
	//   전송은 여기서, 방 락이 풀린 뒤에 한다. 두 락을 겹쳐 잡지 않기 위해서다.
	// ------------------------------------------------------------------

	/** UserId 목록에 해당하는 세션에만 패킷을 보낸다. */
	void SendToUsers(const std::vector<uint64_t>& UserIds, EOpcode Op,
	                 const void* Head, uint32_t HeadSize,
	                 const void* Tail, uint32_t TailSize)
	{
		if (UserIds.empty())
		{
			return;
		}

		GSessions.ForEach([&](const SessionPtr& Target)
		{
			if (!Target->bAuthed)
			{
				return;
			}
			if (std::find(UserIds.begin(), UserIds.end(), Target->UserId) == UserIds.end())
			{
				return;
			}
			SendPacket2(Target->Sock, Op, Head, HeadSize, Tail, TailSize);
		});
	}

	// ------------------------------------------------------------------
	// 친구 (v7)
	//
	// [락 순서] Friends:: 는 DB 락만, GSessions.ForEach 는 세션 락만 잡는다.
	//   **두 락을 겹쳐 잡지 않는다** — DB 조회를 먼저 끝내고 그 결과로 전송한다.
	//   로비 쪽(Rooms)이 지키는 규칙과 같다.
	// ------------------------------------------------------------------

	/**
	 * 이 사람의 지금 접속 상태를 판정한다. **서버만 안다 — 클라 주장은 안 받는다.**
	 *
	 * 근거가 두 곳(세션, 방)에 나뉘어 있어서 여기서 합친다.
	 *   세션 없음            -> Offline
	 *   세션 있고 방이 InGame -> InGame
	 *   그 외                -> Online
	 */
	EPresence ComputePresence(uint64_t UserId)
	{
		bool bOnline = false;

		GSessions.ForEach([&](const SessionPtr& S)
		{
			if (S->bAuthed && S->UserId == UserId)
			{
				bOnline = true;
			}
		});

		if (!bOnline)
		{
			return EPresence::Offline;
		}

		// ★ "대기중"(방에 앉아 있음)은 따로 두지 않는다. 친구 입장에서 온라인과
		//   대기중은 둘 다 "지금 말 걸어도 된다" 라서 행동이 안 바뀐다.
		ERoomState State = ERoomState::Waiting;
		if (Rooms::GetRoomStateOf(UserId, State) && State == ERoomState::InGame)
		{
			return EPresence::InGame;
		}

		return EPresence::Online;
	}

	/** 접속해 있으면 그 세션. 없으면 nullptr. */
	SessionPtr FindAuthedSession(uint64_t UserId)
	{
		SessionPtr Found;

		GSessions.ForEach([&](const SessionPtr& S)
		{
			if (S->bAuthed && S->UserId == UserId)
			{
				Found = S;
			}
		});

		return Found;
	}

	/**
	 * 닉네임을 구한다. 접속 중이면 세션에서, 아니면 DB 에서.
	 *
	 * ★★ 세션에서만 찾으면 안 된다. 친구 알림은 **접속한 쪽에게, 접속하지 않은
	 *   쪽에 대해** 보내는 경우가 있다 — 오프라인이던 사람의 신청을 수락하는
	 *   순간이 정확히 그렇다. 그때 빈 이름이 나가면 상대 친구 목록에
	 *   **이름 없는 줄**이 생기고, 다시 로그인할 때까지 그대로 남는다.
	 *   (통합 테스트에서 실제로 잡힌 버그다.)
	 */
	std::string ResolveNickname(uint64_t UserId)
	{
		if (const SessionPtr S = FindAuthedSession(UserId))
		{
			return S->Name;
		}

		std::string Nick;
		Accounts::GetNickname(UserId, Nick);
		return Nick;
	}

	/**
	 * 친구 하나의 변화를 한 사람에게 알린다(목록 전체 대신 델타).
	 *
	 * @param Recipient  받을 사람. 오프라인이면 아무 일도 안 한다 —
	 *                   다음 로그인 때 FriendListAck 로 최신 상태를 받는다.
	 * @param AboutId    변화가 생긴 친구
	 */
	void SendFriendUpdate(uint64_t Recipient, uint64_t AboutId, const std::string& AboutNick,
	                      EFriendState State, bool bRemoved)
	{
		const SessionPtr Target = FindAuthedSession(Recipient);
		if (!Target)
		{
			return;
		}

		FriendUpdateBody Body{};
		Body.UserId   = AboutId;
		Body.State    = static_cast<uint8_t>(State);
		Body.bRemoved = bRemoved ? 1 : 0;
		// 지워진 상대의 상태는 의미가 없다. 굳이 세션을 뒤지지 않는다.
		Body.Presence = bRemoved ? static_cast<uint8_t>(EPresence::Offline)
		                         : static_cast<uint8_t>(ComputePresence(AboutId));
		CopyFixedString(Body.Nickname, kMaxNameLen, AboutNick);

		SendPacket(Target->Sock, EOpcode::FriendUpdate, &Body, sizeof(Body));
	}

	/**
	 * 내 접속 상태가 바뀐 것을 **접속해 있는 내 친구들에게만** 알린다 (v7 M4).
	 *
	 * ★★ 폴링하지 않는 이유: 친구 93명 x 접속자 N명이 몇 초마다 전체 목록을
	 *   요청하면, 아무 일도 안 일어나는 동안에도 트래픽이 계속 흐른다.
	 *   상태 변화는 드문 사건이라 **바뀔 때만 밀어주는 쪽이 압도적으로 싸다.**
	 *
	 * ★ 대상은 세션에 캐시된 FriendIds 다. 여기서 DB 를 조회하면 로그인/로그아웃
	 *   마다 친구 테이블을 때리게 되어 접속자가 늘수록 느려진다(Session.h).
	 *
	 * ★ 오프라인 친구에게는 보내지 않는다 — 받을 사람이 없다. 그 친구가 나중에
	 *   로그인하면 FriendListAck 로 최신 상태를 한 번에 받는다.
	 *
	 * @param Subject  상태가 바뀐 사람의 세션. 이 사람의 친구 목록을 쓴다.
	 */
	void BroadcastPresence(const SessionPtr& Subject, EPresence NewPresence)
	{
		if (!Subject || !Subject->bAuthed)
		{
			return;
		}

		// 복사본을 받는다 — 아래에서 세션을 순회하는 동안 남이 목록을 고칠 수
		// 있고, 그러면 반복자가 깨진다(Session.h 의 CopyFriendIds 주석).
		const std::vector<uint64_t> FriendIds = Subject->CopyFriendIds();

		if (FriendIds.empty())
		{
			return;
		}

		FriendPresenceBody Body{};
		Body.UserId   = Subject->UserId;
		Body.Presence = static_cast<uint8_t>(NewPresence);

		SendToUsers(FriendIds, EOpcode::FriendPresence, &Body, sizeof(Body), nullptr, 0);
	}

	/**
	 * 여러 사람의 상태 변화를 한꺼번에 알린다. 게임 시작/종료처럼 방 인원이
	 * 통째로 같은 상태가 될 때 쓴다.
	 *
	 * ★ UserId 목록을 받는 이유: 방을 떠난 뒤에는 Rooms 가 더 이상 그 사람을
	 *   모르므로, 떠나기 **전에** 뽑아둔 목록으로 알려야 한다.
	 */
	void BroadcastPresenceFor(const std::vector<uint64_t>& UserIds, EPresence NewPresence)
	{
		for (const uint64_t Id : UserIds)
		{
			if (const SessionPtr S = FindAuthedSession(Id))
			{
				BroadcastPresence(S, NewPresence);
			}
		}
	}

	/**
	 * 접속해 있는 두 사람의 친구 캐시를 갱신한다.
	 *
	 * ★ 오프라인인 쪽은 건드릴 것이 없다 — 다음 로그인 때 DB 에서 새로 읽는다.
	 *   그래서 캐시와 DB 가 어긋날 수 있는 구간이 없다.
	 */
	void SyncFriendCaches(uint64_t A, uint64_t B, bool bNowFriends)
	{
		if (const SessionPtr SA = FindAuthedSession(A))
		{
			bNowFriends ? SA->AddFriendId(B) : SA->RemoveFriendId(B);
		}
		if (const SessionPtr SB = FindAuthedSession(B))
		{
			bNowFriends ? SB->AddFriendId(A) : SB->RemoveFriendId(A);
		}
	}

	/**
	 * 방의 현재 명단을 멤버 전원에게 보낸다.
	 * 누가 들어오고 나가고 준비를 누를 때마다 부른다 — 대기실 UI 는 이것만 보고 그린다.
	 */
	void BroadcastRoomMembers(uint32_t RoomId)
	{
		std::vector<RoomMemberInfo> Members;
		std::vector<uint64_t>       Recipients;
		bool bAllReady = false;

		if (!Rooms::GetMembers(RoomId, Members, bAllReady, Recipients))
		{
			return;   // 이미 사라진 방
		}

		RoomMemberListBody Head{};
		Head.RoomId    = RoomId;
		Head.Count     = static_cast<uint8_t>(Members.size());
		Head.bAllReady = bAllReady ? 1 : 0;

		SendToUsers(Recipients, EOpcode::RoomMemberList,
		            &Head, sizeof(Head),
		            Members.empty() ? nullptr : Members.data(),
		            static_cast<uint32_t>(Members.size() * sizeof(RoomMemberInfo)));
	}

	/** 방이 사라졌음을 남은 멤버에게 알린다. 이들은 메인메뉴로 돌아가야 한다. */
	void NotifyRoomClosed(const std::vector<uint64_t>& Recipients,
	                      uint32_t RoomId, ERoomCloseReason Reason)
	{
		RoomClosedBody Body{};
		Body.RoomId = RoomId;
		Body.Reason = static_cast<uint8_t>(Reason);

		SendToUsers(Recipients, EOpcode::RoomClosed, &Body, sizeof(Body), nullptr, 0);
	}

	/**
	 * 이 사람을 지금 속한 방에서 빼고, 남은 사람들에게 알린다.
	 * 방장이면 방이 사라지고 남은 사람 전원에게 RoomClosed 가 간다.
	 *
	 * 접속 종료 경로에서도 그대로 부른다 — 정상 종료든 랜선이 뽑혔든 처리가 같아야 한다.
	 */
	void LeaveRoomAndNotify(const SessionPtr& Session)
	{
		uint32_t RoomId = 0;
		bool bRoomClosed = false;
		std::vector<uint64_t> Recipients;

		Rooms::Leave(Session->UserId, RoomId, bRoomClosed, Recipients);
		if (RoomId == 0)
		{
			return;   // 어느 방에도 없었다
		}

		// relay route 는 방 멤버십과 같은 수명이다. 방장이 나가면 모두, 참여자가
		// 나가면 그 사람의 포트 쌍만 즉시 닫아 늦은 UDP가 다음 방에 섞이지 않게 한다.
		if (bRoomClosed)
		{
			ReleaseRelayRoutesForRoom(RoomId);
		}
		else
		{
			ReleaseRelayRouteForGuest(RoomId, Session->UserId);
		}

		if (bRoomClosed)
		{
			std::printf("[방 삭제] #%u 방장 %s(%llu) 가 나갔다. 남은 %zu명에게 통보. 남은 방 %zu개\n",
			            RoomId, Session->Name.c_str(),
			            static_cast<unsigned long long>(Session->UserId),
			            Recipients.size(), Rooms::Count());
			NotifyRoomClosed(Recipients, RoomId, ERoomCloseReason::HostLeft);

			// ★ 방이 사라졌으니 남아 있던 사람들은 다시 "온라인" 이다 (M4).
			//   게임이 시작된 방이었다면 이들은 "게임중" 으로 보이고 있었고,
			//   여기서 되돌리지 않으면 **친구 목록에 영영 게임중으로 남는다.**
			BroadcastPresenceFor(Recipients, EPresence::Online);
		}
		else
		{
			std::printf("[방 퇴장] #%u 에서 %s(%llu) 가 나갔다\n",
			            RoomId, Session->Name.c_str(),
			            static_cast<unsigned long long>(Session->UserId));
			BroadcastRoomMembers(RoomId);
		}

		// ★ 나간 사람 본인의 상태는 **여기서 알리지 않는다.** 호출자가 정한다:
		//     · 스스로 나감  -> HandleRoomLeaveReq 가 Online 을 알린다
		//     · 접속 종료    -> ClientThread 가 Offline 을 알린다
		//   여기서 Online 을 보내면 접속 종료 경로에서 Online -> Offline 두 개가
		//   연달아 나가 친구 화면이 깜빡인다.
	}

	bool HandleRoomCreateReq(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		auto Reply = [&](ERoomResult R, uint32_t RoomId)
		{
			RoomCreateAckBody Ack{};
			Ack.RoomId   = RoomId;
			Ack.bSuccess = (R == ERoomResult::Success) ? 1 : 0;
			Ack.Result   = static_cast<uint8_t>(R);
			return SendPacket(Session->Sock, EOpcode::RoomCreateAck, &Ack, sizeof(Ack));
		};

		if (!Session->bAuthed)
		{
			return Reply(ERoomResult::NotAuthed, 0);
		}
		if (BodySize < sizeof(RoomCreateReqBody))
		{
			return Reply(ERoomResult::InvalidRequest, 0);
		}

		RoomCreateReqBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		const std::string Title = ReadFixedString(Req.Title, kMaxRoomTitleLen);
		// 비밀번호는 널 종료가 없는 고정 4바이트다. 길이를 지정해 그대로 읽는다.
		const std::string Password(Req.Password, kRoomPasswordLen);

		// 공인 주소는 서버가 관측하고, 사설 주소는 호스트가 신고한 값을 검사해서 받는다.
		// 판단은 전부 BuildHostCandidates 안에 있다.
		const std::string ReportedLan = ReadFixedString(Req.LanAddress, kMaxAddressLen);
		const std::vector<HostCandidate> Candidates =
			BuildHostCandidates(Session->PeerAddress, ReportedLan, Req.HostPort);

		uint32_t NewRoomId = 0;
		const ERoomResult R = Rooms::Create(
			Session->UserId, Session->Name, Candidates, Session->bLanOnly,
			Title, Req.bHasPassword != 0, Password, Req.MaxPlayers, NewRoomId);

		if (R == ERoomResult::Success)
		{
			std::printf("[방 생성] #%u \"%s\" 방장=%s(%llu) %s\n",
			            NewRoomId, Title.c_str(), Session->Name.c_str(),
			            static_cast<unsigned long long>(Session->UserId),
			            Req.bHasPassword ? "[비번]" : "");
			for (const HostCandidate& C : Candidates)
			{
				std::printf("[방 생성]   후보 %s:%u (%s)\n", C.Address, C.Port,
				            C.Kind == static_cast<uint8_t>(EHostAddrKind::Lan) ? "LAN" : "공인");
			}

			// 프로브를 돌리지 않은 클라이언트도 있을 수 있다(구버전, 프로브 실패).
			// 그때는 "모름" 이고, 방은 예전처럼 아무 제한 없이 동작한다.
			std::printf("[방 생성]   외부 접속: %s\n",
			            !Session->bHasReachabilityReport ? "확인 안 됨"
			            : (Session->bLanOnly ? "**불가** (같은 LAN 전용 방)" : "가능"));
		}
		else
		{
			std::printf("[거부] 방 생성 실패: %s (사유 %u)\n", Title.c_str(), static_cast<unsigned>(R));
		}

		const bool bSent = Reply(R, NewRoomId);

		// Ack 를 먼저 보내고 명단을 보낸다. 그래야 클라이언트가 RoomId 를 알게 된 뒤에
		// 명단이 도착해서, 어느 방 명단인지 헷갈릴 일이 없다.
		if (R == ERoomResult::Success)
		{
			BroadcastRoomMembers(NewRoomId);
		}
		return bSent;
	}

	bool HandleRoomListReq(const SessionPtr& Session, const char*, uint32_t)
	{
		if (!Session->bAuthed)
		{
			// 로그인하지 않은 사람에게는 목록을 주지 않는다.
			RoomListAckBody Empty{};
			Empty.Count = 0;
			return SendPacket(Session->Sock, EOpcode::RoomListAck, &Empty, sizeof(Empty));
		}

		std::vector<RoomInfo> List;
		Rooms::ListWaiting(List, kMaxRoomsInList);

		RoomListAckBody Head{};
		Head.Count = static_cast<uint16_t>(List.size());

		// 고정 헤더 + 가변 배열. ChatBroadcast 와 같은 2조각 전송 방식이다.
		return SendPacket2(Session->Sock, EOpcode::RoomListAck,
		                   &Head, sizeof(Head),
		                   List.empty() ? nullptr : reinterpret_cast<const char*>(List.data()),
		                   static_cast<uint32_t>(List.size() * sizeof(RoomInfo)));
	}

	bool HandleRoomJoinReq(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		RoomJoinAckBody Ack{};

		auto Reply = [&](ERoomResult R)
		{
			Ack.bSuccess = (R == ERoomResult::Success) ? 1 : 0;
			Ack.Result   = static_cast<uint8_t>(R);
			return SendPacket(Session->Sock, EOpcode::RoomJoinAck, &Ack, sizeof(Ack));
		};

		if (!Session->bAuthed)
		{
			return Reply(ERoomResult::NotAuthed);
		}
		if (BodySize < sizeof(RoomJoinReqBody))
		{
			return Reply(ERoomResult::InvalidRequest);
		}

		RoomJoinReqBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		const std::string Password(Req.Password, kRoomPasswordLen);

		std::vector<HostCandidate> Candidates;
		bool bLanOnly = false;
		const ERoomResult R = Rooms::Join(Req.RoomId, Session->UserId, Session->Name,
		                                  Password, Candidates, bLanOnly);

		Ack.RoomId = Req.RoomId;
		if (R == ERoomResult::Success)
		{
			Ack.CandidateCount = FillCandidates(Ack.Candidates, Candidates);
			Ack.bLanOnly       = bLanOnly ? 1 : 0;
			std::printf("[방 참여] #%u <- %s(%llu), 주소 후보 %u개 전달\n",
			            Req.RoomId, Session->Name.c_str(),
			            static_cast<unsigned long long>(Session->UserId),
			            static_cast<unsigned>(Ack.CandidateCount));
		}
		else
		{
			std::printf("[거부] 방 참여 실패: #%u <- %s (사유 %u)\n",
			            Req.RoomId, Session->Name.c_str(), static_cast<unsigned>(R));
		}

		const bool bSent = Reply(R);

		// 들어온 사람에게도, 이미 있던 사람에게도 갱신된 명단이 필요하다.
		// Ack 다음에 보내야 새 참여자가 RoomId 를 안 상태로 명단을 받는다.
		if (R == ERoomResult::Success)
		{
			BroadcastRoomMembers(Req.RoomId);
		}
		return bSent;
	}

	bool HandleRoomStateUpdate(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		if (!Session->bAuthed || BodySize < sizeof(RoomStateUpdateBody))
		{
			return true;   // 조용히 무시한다. 상태 갱신은 응답이 없는 단방향 통지다
		}

		RoomStateUpdateBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		// Req.CurrentPlayers 는 읽지 않는다. v5 부터 인원은 서버가 직접 센다.
		const ERoomResult R = Rooms::UpdateState(
			Req.RoomId, Session->UserId, static_cast<ERoomState>(Req.State));

		if (R == ERoomResult::Success)
		{
			std::printf("[방 갱신] #%u 상태 %s\n", Req.RoomId,
			            Req.State == static_cast<uint8_t>(ERoomState::InGame) ? "게임중" : "대기중");
		}
		return true;
	}

	bool HandleRoomLeaveReq(const SessionPtr& Session, const char*, uint32_t)
	{
		if (Session->bAuthed)
		{
			// v4 까지는 방장 전용이었다. 이제 참여자도 이걸로 대기실에서 나간다.
			LeaveRoomAndNotify(Session);

			// 스스로 나갔으므로 다시 "온라인" 이다 (M4).
			// 게임중이던 방에서 나온 경우 이게 없으면 계속 게임중으로 보인다.
			BroadcastPresence(Session, EPresence::Online);
		}
		return true;
	}

	bool HandleRoomReadyReq(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		if (!Session->bAuthed || BodySize < sizeof(RoomReadyReqBody))
		{
			return true;   // 준비 토글도 응답 없는 단방향이다. 결과는 명단으로 돌아온다
		}

		RoomReadyReqBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		uint32_t RoomId = 0;
		const ERoomResult R = Rooms::SetReady(Session->UserId, Req.bReady != 0, RoomId);

		if (R == ERoomResult::Success)
		{
			std::printf("[준비] #%u %s(%llu) -> %s\n", RoomId, Session->Name.c_str(),
			            static_cast<unsigned long long>(Session->UserId),
			            Req.bReady ? "준비완료" : "준비해제");
			// 방장의 "게임 시작" 버튼이 켜질지 말지가 여기서 갈린다.
			// 명단에 bAllReady 가 실려 나가므로 전원이 같은 판정을 본다.
			BroadcastRoomMembers(RoomId);
		}
		return true;
	}

	bool HandleRoomStartReq(const SessionPtr& Session, const char*, uint32_t)
	{
		if (!Session->bAuthed)
		{
			return true;
		}

		uint32_t    RoomId = 0;
		std::vector<HostCandidate> Candidates;
		std::vector<uint64_t> Recipients;

		bool bLanOnly = false;
		const ERoomResult R = Rooms::StartGame(Session->UserId, RoomId,
		                                       Candidates, bLanOnly, Recipients);
		if (R != ERoomResult::Success)
		{
			std::printf("[거부] 게임 시작 실패: %s(%llu) (사유 %u)\n", Session->Name.c_str(),
			            static_cast<unsigned long long>(Session->UserId), static_cast<unsigned>(R));
			return true;
		}

		std::printf("[게임 시작] #%u 방장=%s, 주소 후보 %zu개, 인원 %zu명\n",
		            RoomId, Session->Name.c_str(), Candidates.size(), Recipients.size());

		RoomStartBody Start{};
		Start.RoomId         = RoomId;
		Start.CandidateCount = FillCandidates(Start.Candidates, Candidates);
		Start.bLanOnly       = bLanOnly ? 1 : 0;

		// ★ 홀펀칭 대상: 방장을 뺀 참여자들의 **관측된** 공인 게임 엔드포인트. (v10)
		//   방장은 OpenLevel 직전에 이 주소들로 한 발씩 쏴서 자기 NAT 에 구멍을 낸다.
		//   등록을 안 한 참여자는 여기 안 들어간다 — 그 사람은 예전처럼 붙어야 하고,
		//   방장 네트워크가 인바운드를 받는 경우에만 성공한다.
		{
			uint8_t PunchCount = 0;
			GSessions.ForEach([&](const SessionPtr& Member)
			{
				if (PunchCount >= kMaxPlayersInRoom || Member->UserId == Session->UserId)
				{
					return;
				}
				if (Member->GameEndpointPort == 0 || Member->GameEndpointAddress.empty())
				{
					return;
				}
				// 이 방의 멤버만. Recipients 에 방장도 들어 있어 위에서 걸렀다.
				bool bInThisRoom = false;
				for (const uint64_t Id : Recipients)
				{
					if (Id == Member->UserId) { bInThisRoom = true; break; }
				}
				if (!bInThisRoom)
				{
					return;
				}

				PeerEndpoint& Target = Start.PunchTargets[PunchCount++];
				CopyFixedString(Target.Address, kMaxAddressLen, Member->GameEndpointAddress);
				Target.Port = Member->GameEndpointPort;
			});
			Start.PunchTargetCount = PunchCount;

			std::printf("[게임 시작]   홀펀칭 대상 %u명\n", static_cast<unsigned>(PunchCount));
			for (uint8_t i = 0; i < PunchCount; ++i)
			{
				std::printf("[게임 시작]     %s:%u\n",
				            Start.PunchTargets[i].Address, Start.PunchTargets[i].Port);
			}
		}


		// 직접 후보는 전원에게 같지만, relay capability 는 절대로 브로드캐스트하지
		// 않는다. 방장만 host-facing 경로 전부를 받고, 참여자 RoomStart 는 0으로 둔다.
		const bool bRelayAllocated = AllocateRelayRoutes(RoomId, Session->UserId, Recipients);
		if (bRelayAllocated)
		{
			std::vector<RelayHostRoute> HostRoutes = GetHostRelayRoutes(RoomId);
			if (!GRelayLanIp.empty() && IsPrivateAddress(Session->PeerAddress))
			{
				for (RelayHostRoute& Route : HostRoutes)
				{
					SetRelayAddress(Route, GRelayLanIp);
				}
			}
			Start.RelayRouteCount = static_cast<uint8_t>(std::min<std::size_t>(HostRoutes.size(), kMaxRelayRoutes));
			for (uint8_t Index = 0; Index < Start.RelayRouteCount; ++Index)
			{
				Start.RelayRoutes[Index] = HostRoutes[Index];
			}
			if (Start.RelayRouteCount > 0)
			{
				std::printf("[릴레이] #%u 방장에 %u개 host-facing 경로를 전달했다.\n",
				            RoomId, static_cast<unsigned>(Start.RelayRouteCount));
			}
		}
		else if (GRelay && GRelay->IsRunning())
		{
			std::printf("[릴레이] #%u 경로 할당 실패. 이번 방은 직접 연결만 시도한다.\n", RoomId);
		}

		// 방장에게만 capability 가 채워진 body 를 보낸다.
		SendPacket(Session->Sock, EOpcode::RoomStart, &Start, sizeof(Start));

		// 나머지 멤버에게는 relay 필드가 모두 0인 같은 RoomStart 를 보낸다.
		std::vector<uint64_t> GuestRecipients;
		GuestRecipients.reserve(Recipients.size());
		for (const uint64_t UserId : Recipients)
		{
			if (UserId != Session->UserId)
			{
				GuestRecipients.push_back(UserId);
			}
		}
		Start.RelayRouteCount = 0;
		std::memset(Start.RelayRoutes, 0, sizeof(Start.RelayRoutes));
		SendToUsers(GuestRecipients, EOpcode::RoomStart, &Start, sizeof(Start), nullptr, 0);

		// ★ 방이 InGame 이 됐으므로 이 방 사람들의 친구에게 "게임중" 을 알린다 (M4).
		//   Recipients 는 방 멤버 전원이다 - 방장도 포함된다.
		BroadcastPresenceFor(Recipients, EPresence::InGame);
		return true;
	}

	/**
	 * 호스트의 리슨서버가 열렸다는 신고. 참여자에게 출발 신호를 넘긴다.
	 *
	 * [이 핸들러가 하는 일은 중계뿐이다]
	 *   서버는 호스트의 게임 포트에 붙어보지 않는다. 붙어보는 순간 이 프로세스가
	 *   게임 네트워크에 발을 담그는 셈이고, "방 주소록" 이라는 역할이 흐려진다.
	 *   신고가 거짓이면 참여자가 접속에 실패할 뿐인데, 그건 방장이 자기 방을
	 *   망가뜨리는 것이라 굳이 서버가 막아줄 이유가 없다.
	 *
	 * [응답 패킷이 없는 이유]
	 *   호스트는 이 시점에 이미 자기 맵에서 게임을 돌리고 있다. 성공/실패를 받아도
	 *   할 수 있는 일이 없다. 거부 사유는 서버 콘솔에만 남긴다.
	 */
	/**
	 * 호스트가 "내 공인주소:포트 로 UDP 를 한 발 쏴달라" 고 요청했다. (v9)
	 *
	 * 서버가 하는 일은 쏘는 것까지다. 그 패킷이 실제로 도착했는지는 호스트만 알고,
	 * 결과는 RoomReachabilityReq 로 다시 올라온다.
	 *
	 * ★ 목적지 주소는 **세션의 피어 주소**를 쓴다. 클라이언트가 보낸 주소로 쏘면
	 *   이 서버가 남에게 UDP 를 대신 쏴주는 도구가 된다.
	 *   클라이언트가 정하는 것은 포트 하나뿐이다.
	 */
	bool HandleHostProbeReq(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		if (!Session->bAuthed || BodySize < sizeof(HostProbeReqBody))
		{
			return true;
		}

		HostProbeReqBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		const bool bSent = SendHostProbe(Session->PeerAddress, Req.Port, Req.Nonce);

		std::printf("[프로브] %s(%llu) 의 %s:%u 로 UDP 발사 %s\n",
		            Session->Name.c_str(),
		            static_cast<unsigned long long>(Session->UserId),
		            Session->PeerAddress.c_str(), Req.Port,
		            bSent ? "성공" : "실패");

		HostProbeSentBody Ack{};
		Ack.Nonce = Req.Nonce;
		Ack.bSent = bSent ? 1 : 0;
		return SendPacket(Session->Sock, EOpcode::HostProbeSent, &Ack, sizeof(Ack));
	}

	/** 호스트가 프로브 결과를 신고했다. 방에 표시만 하고 판정하지 않는다. (v9) */
	bool HandleRoomReachabilityReq(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		if (!Session->bAuthed || BodySize < sizeof(RoomReachabilityReqBody))
		{
			return true;
		}

		RoomReachabilityReqBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		const bool bReachable = (Req.bReachable != 0);

		// ★ 세션에 먼저 적는다. 이것이 진실의 원본이다.
		//
		//   프로브는 **방을 만들기 전에** 돈다 — 그때만 게임 포트가 비어 있기 때문이다.
		//   그래서 이 신고가 RoomCreateReq 보다 먼저 오는 것이 오히려 정상이다.
		//   방에만 적으려 하면 "아직 방이 없다"(NotInRoom)로 버려지고, 정작 방에는
		//   표시가 안 붙는다. 실제로 그렇게 조용히 버려지고 있었다.
		Session->bLanOnly               = !bReachable;
		Session->bHasReachabilityReport = true;

		std::printf("[도달성] %s(%llu) 는 %s\n", Session->Name.c_str(),
		            static_cast<unsigned long long>(Session->UserId),
		            bReachable ? "외부에서 들어올 수 있다"
		                       : "**같은 LAN 에서만** 들어올 수 있다 (공유기가 포워딩을 안 한다)");

		// 이미 방이 있으면 그 방도 같이 갱신한다. 방을 만든 뒤 다시 확인한 경우다.
		// 방이 없으면 위에 적어둔 값이 RoomCreateReq 에서 얹힌다 — 오류가 아니다.
		uint32_t RoomId = 0;
		if (Rooms::SetReachability(Session->UserId, bReachable, RoomId) == ERoomResult::Success)
		{
			std::printf("[도달성]   방 #%u 에 반영했다.\n", RoomId);
		}
		return true;
	}

	bool HandleRoomHostReadyReq(const SessionPtr& Session, const char*, uint32_t)
	{
		if (!Session->bAuthed)
		{
			return true;
		}

		uint32_t    RoomId = 0;
		std::vector<HostCandidate> Candidates;
		std::vector<uint64_t> Recipients;

		bool bLanOnly = false;
		const ERoomResult R = Rooms::MarkHostReady(Session->UserId, RoomId,
		                                           Candidates, bLanOnly, Recipients);
		if (R != ERoomResult::Success)
		{
			std::printf("[거부] 호스트 준비 신고 실패: %s(%llu) (사유 %u)\n", Session->Name.c_str(),
			            static_cast<unsigned long long>(Session->UserId), static_cast<unsigned>(R));
			return true;
		}

		std::printf("[호스트 준비] #%u 리슨서버가 열렸다. 참여자 %zu명에게 출발 신호 (주소 후보 %zu개)\n",
		            RoomId, Recipients.size(), Candidates.size());

		// GuestPort/token 은 참여자마다 다르므로, RoomHostReady 는 더 이상 하나를
		// 브로드캐스트할 수 없다. direct 후보는 같고 Relay 만 개인화한다.
		for (const uint64_t GuestUserId : Recipients)
		{
			RoomHostReadyBody Ready{};
			Ready.RoomId         = RoomId;
			Ready.CandidateCount = FillCandidates(Ready.Candidates, Candidates);
			Ready.bLanOnly       = bLanOnly ? 1 : 0;
			if (const SessionPtr Guest = FindAuthedSession(GuestUserId))
			{
				GetGuestRelayRoute(RoomId, GuestUserId, Ready.Relay);
				if (!GRelayLanIp.empty() && IsPrivateAddress(Guest->PeerAddress))
				{
					SetRelayAddress(Ready.Relay, GRelayLanIp);
				}
				SendPacket(Guest->Sock, EOpcode::RoomHostReady, &Ready, sizeof(Ready));
			}
		}
		return true;
	}

	bool HandleChatSend(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		if (!Session->bAuthed)
		{
			return true;   // 로그인 전 채팅은 무시하되 연결은 유지
		}
		if (BodySize < sizeof(ChatSendBody))
		{
			return false;
		}

		ChatSendBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		// 선언한 TextLen 과 실제 도착한 바이트 수가 맞는지 확인한다.
		// 이 검사가 없으면 TextLen 을 크게 속여 버퍼 밖을 읽게 만들 수 있다.
		if (Req.TextLen > kMaxTextLen)
		{
			return false;
		}
		if (sizeof(ChatSendBody) + Req.TextLen != BodySize)
		{
			return false;
		}

		const char* Text = Body + sizeof(ChatSendBody);
		RouteChat(Session, static_cast<EChatChannel>(Req.Channel), Text, Req.TextLen);
		return true;
	}

	bool HandleSetDead(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		if (BodySize < sizeof(SetDeadBody))
		{
			return false;
		}

		SetDeadBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		// 지금은 테스트 편의를 위해 클라이언트가 자기 상태를 바꾸도록 열어둔다.
		// 8단계에서 리슨서버 전용 인증 연결에서만 받도록 잠근다.
		Session->bDead = (Req.bDead != 0);
		std::printf("[상태] %s -> %s\n", Session->Name.c_str(),
		            Session->bDead ? "사망" : "생존");
		return true;
	}

	// ------------------------------------------------------------------
	// 친구 요청 처리 (v7)
	// ------------------------------------------------------------------

	/**
	 * 친구 + 대기 중인 신청을 전부 내려준다. 로그인 직후 클라가 한 번 부른다.
	 *
	 * ★ 그 뒤로는 이걸 다시 부르지 않는다. 변화는 FriendUpdate / FriendPresence
	 *   델타로만 간다 — 93명 목록 4KB 를 상태가 바뀔 때마다 다시 보낼 이유가 없다.
	 */
	bool HandleFriendListReq(const SessionPtr& Session, const char*, uint32_t)
	{
		if (!Session->bAuthed)
		{
			return true;   // 로그인 전에는 조용히 무시한다
		}

		std::vector<FriendRow> Rows;
		if (!Friends::GetList(Session->UserId, Rows))
		{
			FriendListAckBody Empty{};
			return SendPacket(Session->Sock, EOpcode::FriendListAck, &Empty, sizeof(Empty));
		}

		// 안 읽은 개수는 DM 쪽이 안다. 친구마다 COUNT 를 돌리면 친구 수만큼
		// 질의가 나가므로 한 번에 받아 맞춰 넣는다.
		std::vector<UnreadCount> Unread;
		DirectMessages::GetUnreadCounts(Session->UserId, Unread);

		std::vector<FriendEntry> Entries;
		Entries.reserve(Rows.size());

		for (const FriendRow& Row : Rows)
		{
			FriendEntry E{};
			E.UserId = Row.UserId;
			E.State  = static_cast<uint8_t>(Row.State);
			CopyFixedString(E.Nickname, kMaxNameLen, Row.Nickname);

			// ★ 아직 친구가 아닌 상대의 접속 상태는 알려주지 않는다.
			//   신청만 걸어두면 남의 온/오프라인을 훔쳐볼 수 있게 되는 것을 막는다.
			E.Presence = (Row.State == EFriendState::Friend)
				? static_cast<uint8_t>(ComputePresence(Row.UserId))
				: static_cast<uint8_t>(EPresence::Offline);

			for (const UnreadCount& U : Unread)
			{
				if (U.PeerUserId == Row.UserId)
				{
					// 65535 를 넘을 일은 없지만, 넘으면 잘라서 보낸다.
					E.UnreadCount = static_cast<uint16_t>(
						U.Count > 0xFFFFu ? 0xFFFFu : U.Count);
					break;
				}
			}

			Entries.push_back(E);
		}

		FriendListAckBody Head{};
		Head.Count = static_cast<uint16_t>(Entries.size());

		return SendPacket2(Session->Sock, EOpcode::FriendListAck,
		                   &Head, sizeof(Head),
		                   Entries.empty() ? nullptr : Entries.data(),
		                   static_cast<uint32_t>(Entries.size() * sizeof(FriendEntry)));
	}

	/** 닉네임으로 찾아 신청한다. 상대가 이미 나에게 신청해 뒀으면 즉시 친구가 된다. */
	bool HandleFriendAddReq(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		auto SendAck = [&](EFriendResult R, uint64_t TargetId)
		{
			FriendAddAckBody Ack{};
			Ack.TargetUserId = TargetId;
			Ack.bSuccess     = (R == EFriendResult::Success) ? 1 : 0;
			Ack.Result       = static_cast<uint8_t>(R);
			return SendPacket(Session->Sock, EOpcode::FriendAddAck, &Ack, sizeof(Ack));
		};

		if (!Session->bAuthed)
		{
			return SendAck(EFriendResult::NotAuthed, 0);
		}
		if (BodySize < sizeof(FriendAddReqBody))
		{
			return SendAck(EFriendResult::InvalidFormat, 0);
		}

		FriendAddReqBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		const std::string Query = ReadFixedString(Req.Query, kMaxFriendQueryLen);

		uint64_t    TargetId = 0;
		std::string TargetNick;
		bool        bBecameFriends = false;

		const EFriendResult R =
			Friends::Add(Session->UserId, Query, TargetId, TargetNick, bBecameFriends);

		if (R != EFriendResult::Success)
		{
			std::printf("[친구] %s 의 신청 실패: \"%s\" 사유=%u\n",
			            Session->Name.c_str(), Query.c_str(), static_cast<unsigned>(R));
			return SendAck(R, 0);
		}

		if (bBecameFriends)
		{
			// 맞신청이라 그 자리에서 친구가 됐다. 양쪽 다 갱신해야 한다.
			SyncFriendCaches(Session->UserId, TargetId, /*bNowFriends=*/true);
			SendFriendUpdate(TargetId, Session->UserId, Session->Name,
			                 EFriendState::Friend, /*bRemoved=*/false);
			SendFriendUpdate(Session->UserId, TargetId, TargetNick,
			                 EFriendState::Friend, /*bRemoved=*/false);

			std::printf("[친구] %s <-> %s 맞신청으로 친구 성립\n",
			            Session->Name.c_str(), TargetNick.c_str());
		}
		else
		{
			// 대기 상태. 상대가 접속해 있으면 지금 알려준다.
			// 오프라인이면 다음 로그인 때 FriendListAck 에 PendingIncoming 으로 들어간다.
			if (const SessionPtr Target = FindAuthedSession(TargetId))
			{
				FriendRequestIncomingBody Note{};
				Note.FromUserId = Session->UserId;
				CopyFixedString(Note.FromNickname, kMaxNameLen, Session->Name);
				SendPacket(Target->Sock, EOpcode::FriendRequestIncoming, &Note, sizeof(Note));
			}

			std::printf("[친구] %s -> %s 신청\n",
			            Session->Name.c_str(), TargetNick.c_str());
		}

		return SendAck(EFriendResult::Success, TargetId);
	}

	/**
	 * 받은 신청에 수락/거절한다.
	 *
	 * ★ 방향 검사는 Friends::Respond 안에 있다. 여기서 또 하지 않는다 —
	 *   같은 판정이 두 곳에 있으면 언젠가 갈라진다.
	 */
	bool HandleFriendRespondReq(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		if (!Session->bAuthed || BodySize < sizeof(FriendRespondReqBody))
		{
			return true;
		}

		FriendRespondReqBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		const bool bAccept = (Req.bAccept != 0);

		const EFriendResult R = Friends::Respond(Session->UserId, Req.FromUserId, bAccept);
		if (R != EFriendResult::Success)
		{
			std::printf("[친구] %s 의 응답 실패: from=%llu 사유=%u\n",
			            Session->Name.c_str(),
			            static_cast<unsigned long long>(Req.FromUserId),
			            static_cast<unsigned>(R));
			return true;
		}

		// ★ 상대가 오프라인이어도 이름이 필요하다 — 이 이름은 **나에게 가는**
		//   FriendUpdate 에 실린다(ResolveNickname 주석).
		const std::string FromNick = ResolveNickname(Req.FromUserId);

		if (bAccept)
		{
			SyncFriendCaches(Session->UserId, Req.FromUserId, /*bNowFriends=*/true);

			// 양쪽에 보낸다. 수락한 쪽도 자기 화면을 직접 고치지 않고 이 신호로
			// 갱신하게 해야 두 클라가 같은 그림을 본다.
			SendFriendUpdate(Req.FromUserId, Session->UserId, Session->Name,
			                 EFriendState::Friend, /*bRemoved=*/false);
			SendFriendUpdate(Session->UserId, Req.FromUserId, FromNick,
			                 EFriendState::Friend, /*bRemoved=*/false);

			std::printf("[친구] %s 가 %llu 의 신청을 수락\n",
			            Session->Name.c_str(),
			            static_cast<unsigned long long>(Req.FromUserId));
		}
		else
		{
			// 거절은 줄이 사라진 것이라 양쪽 목록에서 지워야 한다.
			SendFriendUpdate(Req.FromUserId, Session->UserId, Session->Name,
			                 EFriendState::Friend, /*bRemoved=*/true);
			SendFriendUpdate(Session->UserId, Req.FromUserId, FromNick,
			                 EFriendState::Friend, /*bRemoved=*/true);

			std::printf("[친구] %s 가 %llu 의 신청을 거절\n",
			            Session->Name.c_str(),
			            static_cast<unsigned long long>(Req.FromUserId));
		}

		return true;
	}

	/** 친구를 끊거나 내가 보낸 신청을 취소한다. 둘 다 "그 줄을 지운다" 라서 같다. */
	bool HandleFriendRemoveReq(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		if (!Session->bAuthed || BodySize < sizeof(FriendRemoveReqBody))
		{
			return true;
		}

		FriendRemoveReqBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		const EFriendResult R = Friends::Remove(Session->UserId, Req.TargetUserId);
		if (R != EFriendResult::Success)
		{
			return true;
		}

		SyncFriendCaches(Session->UserId, Req.TargetUserId, /*bNowFriends=*/false);

		// 오프라인 상대여도 이름이 필요하다(ResolveNickname 주석).
		const std::string TargetNick = ResolveNickname(Req.TargetUserId);

		SendFriendUpdate(Req.TargetUserId, Session->UserId, Session->Name,
		                 EFriendState::Friend, /*bRemoved=*/true);
		SendFriendUpdate(Session->UserId, Req.TargetUserId, TargetNick,
		                 EFriendState::Friend, /*bRemoved=*/true);

		std::printf("[친구] %s 가 %llu 를 삭제\n",
		            Session->Name.c_str(),
		            static_cast<unsigned long long>(Req.TargetUserId));
		return true;
	}

	// ------------------------------------------------------------------
	// 메신저 1:1 DM (v7)
	// ------------------------------------------------------------------

	/**
	 * DM 한 통을 한 사람에게 내보낸다. 오프라인이면 아무 일도 하지 않는다.
	 *
	 * 보낸 사람에게도 같은 형태로 되돌려준다 — 클라가 자기 화면에 먼저 그려두면
	 * 서버가 매긴 MessageId/Timestamp 를 모르게 되고, 그러면 커서 페이징의
	 * 기준이 클라마다 달라진다(ChatProtocol.h 의 DirectMessageBody 주석).
	 */
	void DeliverDirectMessage(uint64_t ToUserId, const DmRow& Row, uint64_t PeerToUserId)
	{
		const SessionPtr Target = FindAuthedSession(ToUserId);
		if (!Target)
		{
			return;
		}

		DirectMessageBody Head{};
		Head.MessageId  = Row.MessageId;
		Head.FromUserId = Row.FromUserId;
		Head.ToUserId   = PeerToUserId;
		Head.Timestamp  = Row.Timestamp;
		Head.TextLen    = static_cast<uint16_t>(Row.Text.size());

		SendPacket2(Target->Sock, EOpcode::DirectMessage,
		            &Head, sizeof(Head),
		            Row.Text.empty() ? nullptr : Row.Text.data(),
		            static_cast<uint32_t>(Row.Text.size()));
	}

	/**
	 * DM 을 저장하고, 상대가 접속해 있으면 밀어준다.
	 *
	 * ★★ **저장이 먼저다.** 순서를 바꾸면 전송에는 성공했는데 기록이 없는
	 *   메시지가 생기고, 대화창을 다시 열었을 때 방금 나눈 대화가 사라진다
	 *   (DirectMessages.h 헤더 주석).
	 */
	bool HandleDirectMessageSend(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		if (!Session->bAuthed || BodySize < sizeof(DirectMessageSendBody))
		{
			return true;
		}

		DirectMessageSendBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		// 본문은 구조체 뒤에 이어붙어 온다. 길이가 실제로 도착했는지 확인한다 —
		// 이걸 안 보면 위조된 TextLen 으로 남의 메모리를 읽게 된다.
		if (BodySize < sizeof(Req) + Req.TextLen || Req.TextLen == 0)
		{
			return true;
		}
		if (Req.TextLen > kMaxTextLen)
		{
			return true;
		}

		const char* Text = Body + sizeof(Req);

		// ★★ 친구가 아니면 보낼 수 없다. 클라가 아무 UserId 에게나 쏘는 것을
		//   서버가 막아야 한다 — UI 가 친구만 보여준다는 것은 방어가 아니다.
		if (!Friends::AreFriends(Session->UserId, Req.TargetUserId))
		{
			std::printf("[거부] %s -> %llu DM: 친구가 아니다\n",
			            Session->Name.c_str(),
			            static_cast<unsigned long long>(Req.TargetUserId));
			return true;
		}

		uint64_t MessageId = 0;
		int64_t  Timestamp = 0;

		if (!DirectMessages::Send(Session->UserId, Req.TargetUserId,
		                          Text, Req.TextLen, MessageId, Timestamp))
		{
			std::printf("[오류] DM 저장 실패: %s -> %llu\n",
			            Session->Name.c_str(),
			            static_cast<unsigned long long>(Req.TargetUserId));
			return true;
		}

		DmRow Row;
		Row.MessageId  = MessageId;
		Row.FromUserId = Session->UserId;
		Row.Timestamp  = Timestamp;
		Row.Text.assign(Text, Req.TextLen);

		// 받는 사람에게. 오프라인이면 조용히 넘어가고, 다음 로그인 때
		// 밀린 메시지로 받는다(저장은 위에서 이미 끝났다).
		DeliverDirectMessage(Req.TargetUserId, Row, Req.TargetUserId);

		// 보낸 사람에게도 되돌려준다(위 함수 주석).
		DeliverDirectMessage(Session->UserId, Row, Req.TargetUserId);

		std::printf("[DM] %s -> %llu (%u바이트)%s\n",
		            Session->Name.c_str(),
		            static_cast<unsigned long long>(Req.TargetUserId),
		            Req.TextLen,
		            FindAuthedSession(Req.TargetUserId) ? "" : " [오프라인 - 보관]");
		return true;
	}

	/**
	 * 로그인 직후 밀린 DM 을 내려준다.
	 *
	 * ★ 여기서 읽음 처리를 하지 않는다. 받는 것과 읽는 것은 다르다 —
	 *   접속만 하고 대화창을 안 열었으면 여전히 안 읽은 것이고 배지도 떠 있어야
	 *   한다. 읽음은 대화창을 열 때(DmHistoryReq) 찍힌다.
	 */
	void DeliverPendingDirectMessages(const SessionPtr& Session)
	{
		std::vector<DmRow> Pending;

		// 상한을 둔다. 오래 안 들어온 계정에 수천 통이 쌓여 있으면 로그인
		// 직후 그것을 한꺼번에 밀어넣게 되고, 그동안 다른 처리가 밀린다.
		// 넘친 것은 대화창을 열 때 기록 조회로 따라온다.
		if (!DirectMessages::GetPending(Session->UserId, kDmPageSize * 4, Pending))
		{
			return;
		}

		for (const DmRow& Row : Pending)
		{
			DeliverDirectMessage(Session->UserId, Row, Session->UserId);
		}

		if (!Pending.empty())
		{
			std::printf("[DM] %s 에게 밀린 메시지 %zu통 전달\n",
			            Session->Name.c_str(), Pending.size());
		}
	}

	/**
	 * 대화 기록을 내려준다. 대화창을 열 때와 위로 스크롤할 때 같은 것을 쓴다.
	 *
	 * ★★ **가장 최근 페이지를 요청할 때(BeforeMessageId == 0)만 읽음 처리한다.**
	 *
	 *   그게 "대화창을 열었다" 는 뜻이기 때문이다. 위로 스크롤(커서 있음)에서도
	 *   찍으면 옛날 기록을 훑어보는 것만으로 읽음이 되는데, 그때 새로 도착한
	 *   메시지까지 같이 읽음이 되어 **배지가 사라진 채 못 본 메시지가 남는다.**
	 *
	 * ★ 클라가 "읽었다" 를 따로 보내지 않는 이유: 창을 여는 것이 곧 읽는 것이다.
	 *   별도 옵코드를 두면 창은 열었는데 그 신호를 놓치는 경우가 생기고,
	 *   그러면 배지가 영원히 안 사라진다(CHAT_DESIGN.md 6-2절).
	 */
	bool HandleDmHistoryReq(const SessionPtr& Session, const char* Body, uint32_t BodySize)
	{
		if (!Session->bAuthed || BodySize < sizeof(DmHistoryReqBody))
		{
			return true;
		}

		DmHistoryReqBody Req{};
		std::memcpy(&Req, Body, sizeof(Req));

		// ★ 친구가 아니면 기록도 볼 수 없다. 보내는 쪽만 막고 조회를 열어두면
		//   과거에 친구였던 사람의 대화를 계속 들여다볼 수 있게 된다.
		if (!Friends::AreFriends(Session->UserId, Req.PeerUserId))
		{
			DmHistoryAckBody Empty{};
			Empty.PeerUserId = Req.PeerUserId;
			return SendPacket(Session->Sock, EOpcode::DmHistoryAck, &Empty, sizeof(Empty));
		}

		std::vector<DmRow> Rows;
		bool bHasMore = false;

		if (!DirectMessages::GetHistory(Session->UserId, Req.PeerUserId,
		                                Req.BeforeMessageId, kDmPageSize, Rows, bHasMore))
		{
			DmHistoryAckBody Empty{};
			Empty.PeerUserId = Req.PeerUserId;
			return SendPacket(Session->Sock, EOpcode::DmHistoryAck, &Empty, sizeof(Empty));
		}

		// 최신 페이지 = 대화창을 연 것 = 읽음(위 ★★).
		if (Req.BeforeMessageId == 0)
		{
			DirectMessages::MarkRead(Session->UserId, Req.PeerUserId);
		}

		// --- 가변 길이 본문을 이어붙인다 ---
		//
		// ★ DmEntry 는 고정 크기가 아니다. 뒤에 TextLen 바이트가 따라오므로
		//   받는 쪽도 배열 인덱싱이 아니라 순회로 읽어야 한다(ChatProtocol.h).
		std::vector<char> Tail;

		for (const DmRow& Row : Rows)
		{
			DmEntry E{};
			E.MessageId  = Row.MessageId;
			E.FromUserId = Row.FromUserId;
			E.Timestamp  = Row.Timestamp;
			E.TextLen    = static_cast<uint16_t>(Row.Text.size());

			const char* Head = reinterpret_cast<const char*>(&E);
			Tail.insert(Tail.end(), Head, Head + sizeof(E));
			Tail.insert(Tail.end(), Row.Text.begin(), Row.Text.end());
		}

		DmHistoryAckBody Ack{};
		Ack.PeerUserId = Req.PeerUserId;
		Ack.Count      = static_cast<uint16_t>(Rows.size());
		Ack.bHasMore   = bHasMore ? 1 : 0;

		return SendPacket2(Session->Sock, EOpcode::DmHistoryAck,
		                   &Ack, sizeof(Ack),
		                   Tail.empty() ? nullptr : Tail.data(),
		                   static_cast<uint32_t>(Tail.size()));
	}

	bool HandlePacket(const SessionPtr& Session, const PacketHeader& Header,
	                  const std::vector<char>& Body)
	{
		const char* Data = Body.empty() ? nullptr : Body.data();
		const uint32_t Size = static_cast<uint32_t>(Body.size());

		switch (static_cast<EOpcode>(Header.Opcode))
		{
		case EOpcode::LoginReq:  return HandleLoginReq(Session, Data, Size);
		case EOpcode::RegisterReq: return HandleRegisterReq(Session, Data, Size);
		case EOpcode::RoomCreateReq:   return HandleRoomCreateReq(Session, Data, Size);
		case EOpcode::RoomListReq:     return HandleRoomListReq(Session, Data, Size);
		case EOpcode::RoomJoinReq:     return HandleRoomJoinReq(Session, Data, Size);
		case EOpcode::RoomLeaveReq:    return HandleRoomLeaveReq(Session, Data, Size);
		case EOpcode::RoomStateUpdate: return HandleRoomStateUpdate(Session, Data, Size);
		case EOpcode::RoomReadyReq:    return HandleRoomReadyReq(Session, Data, Size);
		case EOpcode::RoomStartReq:    return HandleRoomStartReq(Session, Data, Size);
		case EOpcode::RoomHostReadyReq: return HandleRoomHostReadyReq(Session, Data, Size);

		// --- 도달성 프로브 (v9) ---
		case EOpcode::HostProbeReq:        return HandleHostProbeReq(Session, Data, Size);
		case EOpcode::RoomReachabilityReq: return HandleRoomReachabilityReq(Session, Data, Size);
		case EOpcode::ChatSend:  return HandleChatSend(Session, Data, Size);
		case EOpcode::SetDead:   return HandleSetDead(Session, Data, Size);

		// --- 친구 (v7) ---
		case EOpcode::FriendListReq:    return HandleFriendListReq(Session, Data, Size);
		case EOpcode::FriendAddReq:     return HandleFriendAddReq(Session, Data, Size);
		case EOpcode::FriendRespondReq: return HandleFriendRespondReq(Session, Data, Size);
		case EOpcode::FriendRemoveReq:  return HandleFriendRemoveReq(Session, Data, Size);

		// --- 메신저 (v7) ---
		case EOpcode::DirectMessageSend: return HandleDirectMessageSend(Session, Data, Size);
		case EOpcode::DmHistoryReq:      return HandleDmHistoryReq(Session, Data, Size);

		case EOpcode::Heartbeat: return true;
		default:
			std::printf("[경고] 알 수 없는 오피코드 %u\n", Header.Opcode);
			return false;
		}
	}

	// ------------------------------------------------------------------
	// 클라이언트 스레드
	// ------------------------------------------------------------------
	void ClientThread(SessionPtr Session)
	{
		char Temp[1024];
		PacketHeader Header{};
		std::vector<char> Body;

		for (;;)
		{
			const int Received = ::recv(Session->Sock, Temp, sizeof(Temp), 0);

			// 0 이면 정상 종료, 음수면 에러. 기존 코드는 != 0 만 봐서
			// 에러(-1) 일 때 send(sock, buf, -1, 0) 이 호출됐다.
			if (Received <= 0)
			{
				break;
			}

			Session->RecvBuf.insert(Session->RecvBuf.end(), Temp, Temp + Received);

			// 한 번의 recv 에 여러 패킷이 붙어 왔을 수 있으므로 다 꺼낼 때까지 돈다.
			bool bDisconnect = false;
			for (;;)
			{
				const EFrameResult Result = TryExtractPacket(Session->RecvBuf, Header, Body);

				if (Result == EFrameResult::NeedMore)
				{
					break;
				}
				if (Result == EFrameResult::Malformed)
				{
					std::printf("[차단] 비정상 패킷 크기. 연결을 끊는다. (UserId=%llu)\n",
					            static_cast<unsigned long long>(Session->UserId));
					bDisconnect = true;
					break;
				}
				if (!HandlePacket(Session, Header, Body))
				{
					bDisconnect = true;
					break;
				}
			}

			if (bDisconnect)
			{
				break;
			}
		}

		// 방장이 나가면 그 방은 이미 들어갈 수 없는 곳이 된다(리슨서버가 죽었으므로).
		// 목록에 유령 방이 남지 않도록 여기서 반드시 정리한다.
		// 정상 종료든 랜선이 뽑혔든 이 자리를 지나가므로 한 곳에서 처리된다.
		//
		// 남은 멤버에게 통보하는 것까지 같은 함수가 처리한다. 이게 없으면
		// 게스트는 방장이 사라진 줄도 모르고 대기실에 영영 앉아있게 된다.
		if (Session->bAuthed)
		{
			LeaveRoomAndNotify(Session);

			// ★ 친구에게 오프라인을 알린다 (M4). **Remove 보다 먼저** 해야 한다 -
			//   세션이 목록에서 빠진 뒤에는 FriendIds 캐시를 읽을 수 없고,
			//   그러면 친구들 화면에 이 사람이 영원히 온라인으로 남는다.
			//
			//   정상 종료든 랜선이 뽑혔든 이 자리를 지나가므로 한 곳에서 끝난다.
			BroadcastPresence(Session, EPresence::Offline);
		}

		std::printf("[종료] %s (UserId=%llu) 연결 해제\n",
		            Session->Name.empty() ? "(미로그인)" : Session->Name.c_str(),
		            static_cast<unsigned long long>(Session->UserId));

		GSessions.Remove(Session);
		std::printf("       현재 접속자 %zu명\n", GSessions.Count());
	}
}

int main(int argc, char** argv)
{
#ifdef _WIN32
	::SetConsoleOutputCP(CP_UTF8);
#endif
	// 출력을 파일로 리다이렉트하면 stdout 이 전체 버퍼링으로 바뀌어
	// 프로세스가 끝날 때까지 로그가 하나도 보이지 않는다.
	// MSVC 는 _IOLBF(줄 버퍼링)를 _IOFBF 와 동일하게 처리하므로 무버퍼로 둔다.
	::setvbuf(stdout, nullptr, _IONBF, 0);

	// 인자 파싱. --upnp / --public-ip / --relay 는 어디에 와도 되고,
	// 나머지는 순서대로 <port> [db경로] 다.
	bool        bUseUpnp = false;
	bool        bUseRelay = false;
	uint16_t    RelayFirstPort = 10000;
	uint16_t    RelayLastPort  = 10127;
	const char* PortArg  = nullptr;
	const char* DbArg    = nullptr;

	for (int Index = 1; Index < argc; ++Index)
	{
		if (std::strcmp(argv[Index], "--upnp") == 0)
		{
			bUseUpnp = true;
		}
		else if (std::strcmp(argv[Index], "--relay") == 0)
		{
			bUseRelay = true;
		}
		else if (std::strncmp(argv[Index], "--relay-public-ip=", 18) == 0)
		{
			GRelayPublicIp = argv[Index] + 18;
			bUseRelay = true;
		}
		else if (std::strcmp(argv[Index], "--relay-public-ip") == 0)
		{
			if (Index + 1 >= argc)
			{
				std::printf("[오류] --relay-public-ip 뒤에 주소가 없다.\n");
				return 1;
			}
			GRelayPublicIp = argv[++Index];
			bUseRelay = true;
		}
		else if (std::strncmp(argv[Index], "--relay-lan-ip=", 15) == 0)
		{
			GRelayLanIp = argv[Index] + 15;
			bUseRelay = true;
		}
		else if (std::strcmp(argv[Index], "--relay-lan-ip") == 0)
		{
			if (Index + 1 >= argc)
			{
				std::printf("[오류] --relay-lan-ip 뒤에 주소가 없다.\n");
				return 1;
			}
			GRelayLanIp = argv[++Index];
			bUseRelay = true;
		}
		else if (std::strncmp(argv[Index], "--relay-ports=", 14) == 0 ||
		         std::strcmp(argv[Index], "--relay-ports") == 0)
		{
			const char* Range = nullptr;
			if (std::strncmp(argv[Index], "--relay-ports=", 14) == 0)
			{
				Range = argv[Index] + 14;
			}
			else if (Index + 1 < argc)
			{
				Range = argv[++Index];
			}
			if (Range == nullptr)
			{
				std::printf("[오류] --relay-ports 뒤에 10000-10127 형태의 범위가 필요하다.\n");
				return 1;
			}
			unsigned First = 0, Last = 0;
			char Trailing = '\0';
			if (std::sscanf(Range, "%u-%u%c", &First, &Last, &Trailing) != 2 || First == 0 || Last > 65535 ||
				First > Last || ((Last - First + 1) % 2) != 0)
			{
				std::printf("[오류] relay UDP 포트 범위 '%s' 가 잘못됐다. 짝수 개의 1-65535 범위를 써야 한다.\n", Range);
				return 1;
			}
			RelayFirstPort = static_cast<uint16_t>(First);
			RelayLastPort  = static_cast<uint16_t>(Last);
			bUseRelay = true;
		}
		// --public-ip=1.2.3.4 와 --public-ip 1.2.3.4 를 둘 다 받는다.
		// 붙여 쓰는 쪽만 지원하면 공백을 넣었을 때 그 값이 db경로로 먹혀
		// 엉뚱한 파일에 계정이 생기는 사고가 난다 (--upnp 때 겪은 그것과 같다).
		else if (std::strncmp(argv[Index], "--public-ip=", 12) == 0)
		{
			GPublicIp = argv[Index] + 12;
		}
		else if (std::strcmp(argv[Index], "--public-ip") == 0)
		{
			if (Index + 1 >= argc)
			{
				std::printf("[오류] --public-ip 뒤에 주소가 없다.\n");
				return 1;
			}
			GPublicIp = argv[++Index];
		}
		else if (PortArg == nullptr)
		{
			PortArg = argv[Index];
		}
		else if (DbArg == nullptr)
		{
			DbArg = argv[Index];
		}
		else
		{
			PortArg = nullptr;   // 인자가 너무 많다. 사용법을 보여준다
			break;
		}
	}

	// 사설 주소를 공인 IP 라고 우기면 치환이 오히려 상황을 악화시킨다.
	// 조용히 무시하지 말고 여기서 멈춰서 알려준다.
	if (!GPublicIp.empty() && IsPrivateAddress(GPublicIp))
	{
		std::printf("[오류] --public-ip 에 사설 주소(%s)를 줬다. 공인 IP 를 줘야 한다.\n",
		            GPublicIp.c_str());
		return 1;
	}
	if (!GRelayPublicIp.empty() && IsPrivateAddress(GRelayPublicIp))
	{
		std::printf("[오류] --relay-public-ip 에 사설 주소(%s)를 줬다. 공인 IP 를 줘야 한다.\n",
		            GRelayPublicIp.c_str());
		return 1;
	}
	if (!GRelayLanIp.empty() && !IsPrivateAddress(GRelayLanIp))
	{
		std::printf("[오류] --relay-lan-ip 는 서버 PC의 사설 LAN IPv4여야 한다: %s\n", GRelayLanIp.c_str());
		return 1;
	}

	if (PortArg == nullptr)
	{
		std::printf("사용법: %s <port> [db경로] [--upnp] [--public-ip <주소>] [--relay]\n", argv[0]);
		std::printf("  db경로를 생략하면 현재 디렉터리의 chat_log.db 를 쓴다.\n");
		std::printf("  --upnp : 공유기(UPnP)에 이 포트를 자동으로 열어달라고 요청한다.\n");
		std::printf("           다른 네트워크에서 접속시킬 때만 필요하다. 같은 공유기\n");
		std::printf("           안에서만 쓸 거라면 켤 이유가 없다.\n");
		std::printf("  --public-ip : 이 서버의 공인 IP. 방장이 서버와 같은 공유기 안에\n");
		std::printf("           있을 때, 방의 호스트 주소로 사설 IP 대신 이 값을 기록한다.\n");
		std::printf("           안 주면 --upnp 성공 시 알아낸 값을 자동으로 쓴다.\n");
		std::printf("  --relay : 직접 UDP 연결 실패 시 사용할 자체 UDP relay 를 켠다.\n");
		std::printf("           기본 범위는 10000-10127/UDP 이며, 공유기에 같은 범위를\n");
		std::printf("           이 PC로 수동 포트포워딩해야 한다. --public-ip 를 재사용한다.\n");
		std::printf("  --relay-public-ip <주소> : relay 전용 공인 IP 를 명시한다.\n");
		std::printf("  --relay-lan-ip <주소> : 서버와 같은 LAN 참가자에게 줄 사설 relay 주소.\n");
		std::printf("  --relay-ports <처음-끝> : 예: --relay-ports 10000-10127\n");
		return 1;
	}

	if (!NetInit())
	{
		std::printf("NetInit() 실패\n");
		return 1;
	}

	const SocketHandle ListenSock = ::socket(PF_INET, SOCK_STREAM, 0);
	if (ListenSock == kInvalidSocket)
	{
		std::printf("socket() 실패: %d\n", LastNetError());
		return 1;
	}

	// 도달성 프로브용 UDP 소켓. (v9)
	// bind 하지 않는다 — 보내기만 하므로 OS 가 알아서 임시 포트를 잡는다.
	// 실패해도 서버는 그대로 돈다. 프로브만 못 하게 될 뿐이고, 그때는 방이
	// "모름(=예전처럼 동작)" 으로 남는다.
	GProbeSock = ::socket(PF_INET, SOCK_DGRAM, 0);
	if (GProbeSock == kInvalidSocket)
	{
		std::printf("[경고] 프로브용 UDP 소켓을 못 만들었다(%d). 도달성 확인 없이 진행한다.\n",
		            LastNetError());
	}
	else
	{
		// ★ v10 부터는 bind 한다. 보내기만 하던 소켓이 이제 **받기도** 해야 한다 —
		//   참여자가 자기 게임 포트에서 등록 데이터그램을 쏘고, 서버는 그 출발지를
		//   관측해 방장에게 알려준다. 그것이 홀펀칭의 유일한 단서다.
		//
		//   TCP 와 같은 번호를 쓴다. 프로토콜이 다르므로 충돌하지 않고,
		//   팀이 외울 포트가 하나로 유지된다.
		//
		// ★★ 공유기에 **외부 UDP <port> -> 서버 PC** 포워딩이 필요하다.
		//    TCP 만 열려 있으면 등록 데이터그램이 서버까지 오지 못하고,
		//    그러면 홀펀칭이 통째로 동작하지 않는다.
		sockaddr_in ProbeAddr{};
		ProbeAddr.sin_family      = AF_INET;
		ProbeAddr.sin_addr.s_addr = ::htonl(INADDR_ANY);
		ProbeAddr.sin_port        = ::htons(static_cast<uint16_t>(std::atoi(PortArg)));

		if (::bind(GProbeSock, reinterpret_cast<sockaddr*>(&ProbeAddr), sizeof(ProbeAddr)) != 0)
		{
			std::printf("[경고] UDP %s 를 열지 못했다(%d). 홀펀칭 없이 진행한다.\n",
			            PortArg, LastNetError());
		}
		else
		{
			// 타임아웃이 있어야 종료할 때 recvfrom 에 갇히지 않는다.
			SetRecvTimeout(GProbeSock, 500);
			std::printf("[UDP] %s 에서 엔드포인트 등록을 받는다.\n", PortArg);
			std::printf("      * 공유기에 '외부 UDP %s -> 이 PC' 포워딩이 있어야 한다.\n", PortArg);
		}
	}

	sockaddr_in ServerAddr{};
	ServerAddr.sin_family      = AF_INET;
	ServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	ServerAddr.sin_port        = htons(static_cast<uint16_t>(std::atoi(PortArg)));

	if (::bind(ListenSock, reinterpret_cast<sockaddr*>(&ServerAddr), sizeof(ServerAddr)) != 0)
	{
		std::printf("bind() 실패: %d\n", LastNetError());
		return 1;
	}
	if (::listen(ListenSock, SOMAXCONN) != 0)
	{
		std::printf("listen() 실패: %d\n", LastNetError());
		return 1;
	}

	std::signal(SIGINT,  OnInterrupt);
	std::signal(SIGTERM, OnInterrupt);

	// 채팅 로그 DB. 두 번째 인자로 경로를 바꿀 수 있다 (테스트용으로 분리할 때 편하다).
	// 열기에 실패해도 서버는 계속 돈다. 로그가 안 남는 것보다 채팅이 끊기는 게 나쁘다.
	const char* DbPath = (DbArg != nullptr) ? DbArg : "chat_log.db";
	ChatLog::Start(DbPath);

	// 계정도 같은 파일에 둔다(테이블이 다르므로 섞이지 않는다).
	// 커넥션은 별개다 — ChatLog 쪽은 라이터 스레드 전용이라 남이 끼면 안 된다.
	if (!Accounts::Start(DbPath))
	{
		std::printf("[치명] 계정 DB 를 열지 못했다. 아무도 로그인할 수 없다.\n");
		return 1;
	}

	// v7 친구 + 메신저. 같은 파일, 각자 별도 커넥션(Accounts 와 같은 이유).
	//
	// ★ 실패해도 서버를 죽이지 않는다. 계정과 심각도가 다르다 - 로그인은
	//   못 하면 아무것도 안 되지만, 친구 목록이 안 뜨는 것은 게임과 채팅을
	//   막지 않는다. 대신 왜 안 되는지는 분명히 말해준다.
	if (!Friends::Start(DbPath))
	{
		std::printf("[경고] 친구 DB 를 열지 못했다. 친구 기능만 동작하지 않는다.\n");
	}
	if (!DirectMessages::Start(DbPath))
	{
		std::printf("[경고] 메신저 DB 를 열지 못했다. DM 만 동작하지 않는다.\n");
	}

	// 공유기에 이 포트를 열어달라고 요청한다. 블로킹이라 accept 루프 전에 끝낸다.
	//
	// ★ 실패해도 서버는 그대로 뜬다. 같은 네트워크에서는 어차피 접속되고,
	//   UPnP 가 안 되는 것은 "이 경로로는 못 간다" 는 뜻이지 서버 오류가 아니다.
	if (bUseUpnp)
	{
		const uint16_t ListenPort = static_cast<uint16_t>(std::atoi(PortArg));
		const Nat::EResult Result = Nat::Start(ListenPort, /*bTcp=*/true);

		if (Result == Nat::EResult::Success)
		{
			std::printf("[NAT] 외부에서는 %s:%u 로 접속하면 된다.\n",
				Nat::ExternalIp().empty() ? "<외부IP>" : Nat::ExternalIp().c_str(),
				static_cast<unsigned>(Nat::MappedExternalPort()));

			// 공유기가 다른 외부 포트를 열어준 경우, 클라이언트는 그 포트로 붙어야 한다.
			if (Nat::MappedExternalPort() != ListenPort)
			{
				std::printf("[NAT] ★ 내부 포트와 외부 포트가 다르다. 클라이언트의 서버 주소를\n");
				std::printf("        외부 포트(%u)로 설정해야 한다.\n",
					static_cast<unsigned>(Nat::MappedExternalPort()));
			}

			// UPnP 가 알아낸 외부 IP 를 방 호스트 주소 치환에도 쓴다.
			// 명시적으로 --public-ip 를 준 쪽이 이긴다 — 사람이 준 값이
			// 더 정확한 상황(공유기가 외부 IP 를 잘못 보고하는 경우)이 있다.
			if (GPublicIp.empty() && !Nat::ExternalIp().empty()
			    && !IsPrivateAddress(Nat::ExternalIp()))
			{
				GPublicIp = Nat::ExternalIp();
				std::printf("[NAT] 공인 IP %s 를 방 호스트 주소 치환에 쓴다.\n",
					GPublicIp.c_str());
			}
		}
		else
		{
			std::printf("[NAT] 포트를 열지 못했다: %s\n", Nat::ResultText(Result));
			std::printf("[NAT] 같은 네트워크에서만 접속할 수 있다. 서버는 그대로 계속한다.\n");
		}
	}

	// relay 는 공인 주소와 **별도 UDP 포트 범위**가 있어야 한다. 기존 9000/UDP 는
	// 엔드포인트 등록용 단일 소켓이라 여러 UE 참여자를 투명하게 중계할 수 없다.
	if (bUseRelay)
	{
		if (GRelayPublicIp.empty())
		{
			GRelayPublicIp = GPublicIp;  // 보통은 로그인 서버와 같은 공인 IP 다.
		}

		if (GRelayPublicIp.empty() || IsPrivateAddress(GRelayPublicIp))
		{
			std::printf("[릴레이] 비활성: --relay-public-ip 또는 --public-ip 로 공인 주소를 줘야 한다.\n");
		}
		else
		{
			FUdpRelayConfig Config;
			Config.FirstPort = RelayFirstPort;
			Config.LastPort  = RelayLastPort;
			GRelay = std::make_unique<UdpRelay>();
			if (!GRelay->Start(Config))
			{
				std::printf("[릴레이] UDP %u-%u 를 열지 못했다. 직접 연결만 계속한다.\n",
				            static_cast<unsigned>(RelayFirstPort), static_cast<unsigned>(RelayLastPort));
				GRelay.reset();
			}
			else
			{
				std::printf("[릴레이] 활성: %s:%u-%u/UDP (참여자당 포트 2개)\n",
				            GRelayPublicIp.c_str(), static_cast<unsigned>(RelayFirstPort),
				            static_cast<unsigned>(RelayLastPort));
				std::printf("[릴레이] ★ 공유기에 외부 UDP %u-%u -> 이 PC 를 수동 포트포워딩해야 한다.\n",
				            static_cast<unsigned>(RelayFirstPort), static_cast<unsigned>(RelayLastPort));
				if (!GRelayLanIp.empty())
				{
					std::printf("[릴레이] 서버와 같은 LAN 참가자에게는 %s 를 내려준다(헤어핀 회피).\n",
					            GRelayLanIp.c_str());
				}
			}
		}
	}

	// 방장이 서버와 같은 공유기 안에 있을 때 무엇으로 바꿔 기록할지.
	// 이게 비어 있으면 예전 동작 그대로이고, 그 조합에서만 외부 참가자가
	// 사설 주소를 받아 무한 로딩에 걸린다. 켤 때 분명히 보이게 찍어둔다.
	if (!GPublicIp.empty())
	{
		std::printf("[방 주소] 방장이 이 서버와 같은 네트워크에 있으면 호스트 주소를 %s 로 기록한다.\n",
			GPublicIp.c_str());
		std::printf("[방 주소] 그 방장의 리슨서버 포트(보통 7777/UDP)도 공유기에 열려 있어야 한다.\n");
	}
	else
	{
		std::printf("[방 주소] --public-ip 가 없다. 방장이 이 서버와 같은 공유기 안이면\n");
		std::printf("          외부 참가자에게 사설 주소가 전달되어 접속하지 못한다.\n");
	}

	std::printf("=== MOU 서버 시작 (port %s) ===\n", PortArg);

	// 엔드포인트 등록 수신. accept 루프와 독립적이라 별도 스레드가 맞다. (v10)
	// GRunning 이 꺼지면 recvfrom 타임아웃(500ms) 다음 바퀴에 스스로 빠져나온다.
	std::thread UdpThread;
	if (GProbeSock != kInvalidSocket)
	{
		UdpThread = std::thread(UdpReceiveLoop);
	}

	while (GRunning)
	{
		sockaddr_in ClientAddr{};
		int AddrSize = sizeof(ClientAddr);

		const SocketHandle ClientSock =
			::accept(ListenSock, reinterpret_cast<sockaddr*>(&ClientAddr),
#ifdef _WIN32
			         &AddrSize);
#else
			         reinterpret_cast<socklen_t*>(&AddrSize));
#endif
		if (ClientSock == kInvalidSocket)
		{
			std::printf("accept() 실패: %d\n", LastNetError());
			continue;
		}

		char AddrText[INET_ADDRSTRLEN] = {};
		::inet_ntop(AF_INET, &ClientAddr.sin_addr, AddrText, sizeof(AddrText));
		std::printf("[접속] %s\n", AddrText);

		// 고정 배열이 아니므로 접속자 수 상한이 없다.
		SessionPtr Session = GSessions.Add(ClientSock);
		// 방을 만들 때 호스트 주소로 쓸 값이다. 여기서 한 번만 확정해둔다.
		Session->PeerAddress = AddrText;
		std::thread(ClientThread, Session).detach();
	}

	// UDP 스레드를 먼저 거둔다. 소켓을 닫기 전에 빠져나와야 이미 닫힌 핸들로
	// recvfrom 을 부르는 일이 없다.
	if (UdpThread.joinable())
	{
		UdpThread.join();
	}
	if (GProbeSock != kInvalidSocket)
	{
		CloseSocket(GProbeSock);
		GProbeSock = kInvalidSocket;
	}
	if (GRelay)
	{
		GRelay->Stop();
		GRelay.reset();
	}
	{
		std::lock_guard<std::mutex> Lock(GRelayRoutesMutex);
		GRelayRoutesByRoom.clear();
	}

	ChatLog::Stop();
	Accounts::Stop();
	Friends::Stop();
	DirectMessages::Stop();
	CloseSocket(ListenSock);
	NetShutdown();
	return 0;
}
