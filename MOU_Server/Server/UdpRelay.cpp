#include "UdpRelay.h"

#include "Net.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
	#include <fcntl.h>
	#include <sys/select.h>
#endif

namespace MOU
{
	namespace
	{
		using FClock = std::chrono::steady_clock;

		constexpr std::uint32_t kHardMaxRoutes = 256;
		constexpr std::uint32_t kMaximumPortRangeSize = 4096;
		constexpr std::uint32_t kMaxDatagramsDrainedPerSocket = 32;
		constexpr auto kMinimumRegistrationInterval = std::chrono::milliseconds(250);

		bool IsAllZero(const FUdpRelayToken& Token)
		{
			std::uint8_t Combined = 0;
			for (const std::uint8_t Byte : Token)
			{
				Combined |= Byte;
			}
			return Combined == 0;
		}

		bool ConstantTimeEqual(const std::uint8_t* Left, const std::uint8_t* Right, std::size_t Length)
		{
			std::uint8_t Difference = 0;
			for (std::size_t Index = 0; Index < Length; ++Index)
			{
				Difference |= static_cast<std::uint8_t>(Left[Index] ^ Right[Index]);
			}
			return Difference == 0;
		}

		bool IsSameEndpoint(const sockaddr_in& Left, const sockaddr_in& Right)
		{
			return Left.sin_family == Right.sin_family &&
			       Left.sin_port == Right.sin_port &&
			       Left.sin_addr.s_addr == Right.sin_addr.s_addr;
		}

		bool IsRegistrationCandidate(const std::uint8_t* Data, std::size_t Length)
		{
			return Length == sizeof(FUdpRelayRegistrationDatagram) &&
			       std::memcmp(Data, kRelayRegistrationMagic, sizeof(kRelayRegistrationMagic)) == 0;
		}

		bool IsValidPeerByte(std::uint8_t Value)
		{
			return Value == static_cast<std::uint8_t>(ERelayPeerRole::Host) ||
			       Value == static_cast<std::uint8_t>(ERelayPeerRole::Guest);
		}

		bool IsSocketWouldBlock(int Error)
		{
		#ifdef _WIN32
			return Error == WSAEWOULDBLOCK;
		#else
			return Error == EAGAIN || Error == EWOULDBLOCK;
		#endif
		}

		bool IsSocketInterrupted(int Error)
		{
		#ifdef _WIN32
			return Error == WSAEINTR;
		#else
			return Error == EINTR;
		#endif
		}

		bool SetSocketNonBlocking(SocketHandle Socket)
		{
		#ifdef _WIN32
			u_long Enabled = 1;
			return ::ioctlsocket(Socket, FIONBIO, &Enabled) == 0;
		#else
			const int ExistingFlags = ::fcntl(Socket, F_GETFL, 0);
			return ExistingFlags >= 0 && ::fcntl(Socket, F_SETFL, ExistingFlags | O_NONBLOCK) == 0;
		#endif
		}

		void SetSocketBufferBestEffort(SocketHandle Socket)
		{
			// A relay has two hops per gameplay packet.  Larger kernel queues reduce
			// loss from short replication bursts, but a refusal is not fatal.
			const int Bytes = 1024 * 1024;
		#ifdef _WIN32
			const char* Value = reinterpret_cast<const char*>(&Bytes);
		#else
			const void* Value = &Bytes;
		#endif
			(void)::setsockopt(Socket, SOL_SOCKET, SO_RCVBUF, Value, sizeof(Bytes));
			(void)::setsockopt(Socket, SOL_SOCKET, SO_SNDBUF, Value, sizeof(Bytes));
		}

		bool IsSelectableSocket(SocketHandle Socket)
		{
		#ifdef _WIN32
			// Winsock fd_set limits the count of entries, not the SOCKET value.
			return Socket != kInvalidSocket;
		#else
			return Socket != kInvalidSocket && Socket >= 0 && Socket < FD_SETSIZE;
		#endif
		}

		std::uint32_t SelectableRouteLimit()
		{
			// This worker selects only relay sockets.  Reserving no hidden slots
			// means FD_SETSIZE / 2 is the exact portable upper bound here.
			return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(FD_SETSIZE / 2));
		}

		struct FRateWindow
		{
			FClock::time_point StartedAt{};
			std::uint32_t Packets = 0;
			std::uint64_t Bytes = 0;

			bool Allow(FClock::time_point Now,
			           std::size_t PayloadBytes,
			           const FUdpRelayConfig& Config)
			{
				if (StartedAt.time_since_epoch().count() == 0 || Now - StartedAt >= std::chrono::seconds(1))
				{
					StartedAt = Now;
					Packets = 0;
					Bytes = 0;
				}

				if (Packets >= Config.MaxPacketsPerSecondPerPeer ||
				    PayloadBytes > Config.MaxBytesPerSecondPerPeer ||
				    Bytes > static_cast<std::uint64_t>(Config.MaxBytesPerSecondPerPeer) - PayloadBytes)
				{
					return false;
				}

				++Packets;
				Bytes += PayloadBytes;
				return true;
			}
		};
	}

	struct UdpRelay::FRoute
	{
		struct FPeerState
		{
			bool bRegistered = false;
			sockaddr_in Endpoint{};
			FClock::time_point LastRegistration{};
			FRateWindow RateWindow;
		};

		FRoute(RouteId InId, const FUdpRelayToken& InHostToken,
		       const FUdpRelayToken& InGuestToken, std::uint64_t InRoomId)
			: Id(InId)
			, RoomId(InRoomId)
			, HostToken(InHostToken)
			, GuestToken(InGuestToken)
		{
		}

		~FRoute()
		{
			if (HostSocket != kInvalidSocket)
			{
				CloseSocket(HostSocket);
			}
			if (GuestSocket != kInvalidSocket)
			{
				CloseSocket(GuestSocket);
			}
		}

		RouteId Id = 0;
		std::uint64_t RoomId = 0;
		FUdpRelayToken HostToken{};
		FUdpRelayToken GuestToken{};
		SocketHandle HostSocket = kInvalidSocket;
		SocketHandle GuestSocket = kInvalidSocket;
		FUdpRelayPortPair Ports;
		std::atomic<bool> bActive{ true };

		// Endpoint state is written by the relay worker but may be sampled by the
		// authenticated control thread through GetRouteStatus().
		mutable std::mutex StateMutex;
		FPeerState Host;
		FPeerState Guest;
		bool bReadyLogged = false;
		FClock::time_point LastActivity = FClock::now();
	};

	void MakeUdpRelayRegistrationDatagram(FUdpRelayRegistrationDatagram& OutDatagram,
	                                      std::uint64_t RouteId,
	                                      EUdpRelayPeer Peer,
	                                      const FUdpRelayToken& Token)
	{
		OutDatagram = MakeRelayRegistrationDatagram(RouteId, Peer, Token.data());
	}

	std::uint64_t DecodeUdpRelayRegistrationRouteId(const FUdpRelayRegistrationDatagram& Datagram)
	{
		std::uint64_t Result = 0;
		for (const std::uint8_t Byte : Datagram.RouteId)
		{
			Result = (Result << 8) | Byte;
		}
		return Result;
	}

	UdpRelay::~UdpRelay()
	{
		Stop();
	}

	bool UdpRelay::Start(const FUdpRelayConfig& InConfig)
	{
		if (!IsConfigValid(InConfig))
		{
			return false;
		}

		std::lock_guard<std::mutex> Lock(Mutex_);
		if (bRunning_.load(std::memory_order_acquire) || Worker_.joinable())
		{
			return false;
		}

		Config_ = InConfig;
		if (Config_.BindAddress.empty())
		{
			Config_.BindAddress = "0.0.0.0";
		}
		NextPairIndex_ = 0;
		AcceptedRegistrations_.store(0, std::memory_order_relaxed);
		RejectedRegistrations_.store(0, std::memory_order_relaxed);
		ForwardedPackets_.store(0, std::memory_order_relaxed);
		ForwardedBytes_.store(0, std::memory_order_relaxed);
		DroppedPackets_.store(0, std::memory_order_relaxed);
		RateLimitedPackets_.store(0, std::memory_order_relaxed);

		bRunning_.store(true, std::memory_order_release);
		try
		{
			Worker_ = std::thread(&UdpRelay::WorkerMain, this);
		}
		catch (...)
		{
			bRunning_.store(false, std::memory_order_release);
			return false;
		}
		return true;
	}

	bool UdpRelay::Start(const std::string& BindAddress,
	                     std::uint16_t FirstPort,
	                     std::uint16_t LastPort)
	{
		FUdpRelayConfig Config;
		Config.BindAddress = BindAddress;
		Config.FirstPort = FirstPort;
		Config.LastPort = LastPort;
		return Start(Config);
	}

	void UdpRelay::Stop()
	{
		std::thread WorkerToJoin;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			bRunning_.store(false, std::memory_order_release);
			for (auto& Pair : Routes_)
			{
				Pair.second->bActive.store(false, std::memory_order_release);
			}
			Routes_.clear();
			WorkerToJoin = std::move(Worker_);
		}

		if (WorkerToJoin.joinable())
		{
			WorkerToJoin.join();
		}
	}

	bool UdpRelay::CreateRoute(RouteId Id,
	                           const FUdpRelayToken& HostToken,
	                           const FUdpRelayToken& GuestToken,
	                           std::uint64_t RoomId,
	                           FUdpRelayPortPair& OutPorts)
	{
		OutPorts = {};
		if (IsAllZero(HostToken) || IsAllZero(GuestToken) || HostToken == GuestToken)
		{
			return false;
		}

		std::lock_guard<std::mutex> Lock(Mutex_);
		if (!bRunning_.load(std::memory_order_acquire) || Routes_.find(Id) != Routes_.end() ||
		    Routes_.size() >= Config_.MaxRoutes)
		{
			return false;
		}

		auto Route = std::make_shared<FRoute>(Id, HostToken, GuestToken, RoomId);
		if (!TryAllocateSocketPair(Route))
		{
			return false;
		}

		OutPorts = Route->Ports;
		Routes_.emplace(Id, std::move(Route));
		return true;
	}

	bool UdpRelay::RemoveRoute(RouteId Id)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		const auto It = Routes_.find(Id);
		if (It == Routes_.end())
		{
			return false;
		}

		It->second->bActive.store(false, std::memory_order_release);
		Routes_.erase(It);
		return true;
	}

	std::size_t UdpRelay::RemoveRoutesForRoom(std::uint64_t RoomId)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		std::size_t Removed = 0;
		for (auto It = Routes_.begin(); It != Routes_.end();)
		{
			if (It->second->RoomId != RoomId)
			{
				++It;
				continue;
			}

			It->second->bActive.store(false, std::memory_order_release);
			It = Routes_.erase(It);
			++Removed;
		}
		return Removed;
	}

	bool UdpRelay::GetRoutePorts(RouteId Id, FUdpRelayPortPair& OutPorts) const
	{
		OutPorts = {};
		std::lock_guard<std::mutex> Lock(Mutex_);
		const auto It = Routes_.find(Id);
		if (It == Routes_.end() || !It->second->bActive.load(std::memory_order_acquire))
		{
			return false;
		}

		OutPorts = It->second->Ports;
		return true;
	}

	bool UdpRelay::GetRouteStatus(RouteId Id, FUdpRelayRouteStatus& OutStatus) const
	{
		OutStatus = {};
		std::shared_ptr<FRoute> Route;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			const auto It = Routes_.find(Id);
			if (It == Routes_.end())
			{
				return false;
			}
			Route = It->second;
		}

		std::lock_guard<std::mutex> StateLock(Route->StateMutex);
		OutStatus.Ports = Route->Ports;
		OutStatus.bHostRegistered = Route->Host.bRegistered;
		OutStatus.bGuestRegistered = Route->Guest.bRegistered;
		OutStatus.bActive = Route->bActive.load(std::memory_order_acquire);
		return OutStatus.bActive;
	}

	FUdpRelayCounters UdpRelay::GetCounters() const
	{
		FUdpRelayCounters Result;
		Result.AcceptedRegistrations = AcceptedRegistrations_.load(std::memory_order_relaxed);
		Result.RejectedRegistrations = RejectedRegistrations_.load(std::memory_order_relaxed);
		Result.ForwardedPackets = ForwardedPackets_.load(std::memory_order_relaxed);
		Result.ForwardedBytes = ForwardedBytes_.load(std::memory_order_relaxed);
		Result.DroppedPackets = DroppedPackets_.load(std::memory_order_relaxed);
		Result.RateLimitedPackets = RateLimitedPackets_.load(std::memory_order_relaxed);
		return Result;
	}

	bool UdpRelay::IsConfigValid(const FUdpRelayConfig& Config) const
	{
		if (Config.FirstPort == 0 || Config.LastPort == 0 || Config.FirstPort > Config.LastPort ||
		    Config.MaxRoutes == 0 || Config.MaxPacketsPerSecondPerPeer == 0 ||
		    Config.MaxBytesPerSecondPerPeer == 0 || Config.SelectTimeoutMilliseconds == 0 ||
		    Config.SelectTimeoutMilliseconds > 250)
		{
			return false;
		}

		const std::uint32_t PortCount = static_cast<std::uint32_t>(Config.LastPort) - Config.FirstPort + 1;
		const std::uint32_t PairCount = PortCount / 2;
		if (PortCount > kMaximumPortRangeSize || PairCount == 0 || Config.MaxRoutes > PairCount ||
		    Config.MaxRoutes > kHardMaxRoutes || Config.MaxRoutes > SelectableRouteLimit())
		{
			return false;
		}

		sockaddr_in ParsedAddress{};
		ParsedAddress.sin_family = AF_INET;
		const char* Address = Config.BindAddress.empty() ? "0.0.0.0" : Config.BindAddress.c_str();
		return ::inet_pton(AF_INET, Address, &ParsedAddress.sin_addr) == 1;
	}

	bool UdpRelay::TryAllocateSocketPair(const std::shared_ptr<FRoute>& Route)
	{
		const std::uint32_t PortCount = static_cast<std::uint32_t>(Config_.LastPort) - Config_.FirstPort + 1;
		const std::uint32_t PairCount = PortCount / 2;

		sockaddr_in BindAddress{};
		BindAddress.sin_family = AF_INET;
		if (::inet_pton(AF_INET, Config_.BindAddress.c_str(), &BindAddress.sin_addr) != 1)
		{
			return false;
		}

		auto BindOne = [&](std::uint16_t Port) -> SocketHandle
		{
			const SocketHandle Socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (Socket == kInvalidSocket)
			{
				return kInvalidSocket;
			}

		#ifdef _WIN32
			// Do not opt into SO_REUSEADDR on UDP; on Windows it can permit another
			// process to bind the same relay port.  Exclusive binding is best effort
			// because an older platform may not expose the option.
			const BOOL Exclusive = TRUE;
			(void)::setsockopt(Socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
			                   reinterpret_cast<const char*>(&Exclusive), sizeof(Exclusive));
		#endif

			SetSocketBufferBestEffort(Socket);
			if (!SetSocketNonBlocking(Socket))
			{
				CloseSocket(Socket);
				return kInvalidSocket;
			}

			BindAddress.sin_port = ::htons(Port);
			if (::bind(Socket, reinterpret_cast<const sockaddr*>(&BindAddress), sizeof(BindAddress)) != 0 ||
			    !IsSelectableSocket(Socket))
			{
				CloseSocket(Socket);
				return kInvalidSocket;
			}
			return Socket;
		};

		for (std::uint32_t Attempt = 0; Attempt < PairCount; ++Attempt)
		{
			const std::uint32_t PairIndex = (NextPairIndex_ + Attempt) % PairCount;
			const std::uint32_t First = static_cast<std::uint32_t>(Config_.FirstPort) + PairIndex * 2;
			const std::uint16_t HostPort = static_cast<std::uint16_t>(First);
			const std::uint16_t GuestPort = static_cast<std::uint16_t>(First + 1);

			const SocketHandle HostSocket = BindOne(HostPort);
			if (HostSocket == kInvalidSocket)
			{
				continue;
			}

			const SocketHandle GuestSocket = BindOne(GuestPort);
			if (GuestSocket == kInvalidSocket)
			{
				CloseSocket(HostSocket);
				continue;
			}

			Route->HostSocket = HostSocket;
			Route->GuestSocket = GuestSocket;
			Route->Ports.HostPort = HostPort;
			Route->Ports.GuestPort = GuestPort;
			NextPairIndex_ = (PairIndex + 1) % PairCount;
			return true;
		}
		return false;
	}

	void UdpRelay::WorkerMain()
	{
		// Keeping this local makes FRoute's private implementation detail stay
		// private while the snapshot itself keeps sockets alive during select().
		struct FSocketSnapshot
		{
			std::shared_ptr<FRoute> Route;
			EUdpRelayPeer Peer = EUdpRelayPeer::Host;
			SocketHandle Socket = kInvalidSocket;
		};

		while (bRunning_.load(std::memory_order_acquire))
		{
			std::vector<FSocketSnapshot> Sockets;
			{
				std::lock_guard<std::mutex> Lock(Mutex_);
				Sockets.reserve(Routes_.size() * 2);
				for (const auto& Pair : Routes_)
				{
					const std::shared_ptr<FRoute>& Route = Pair.second;
					if (!Route->bActive.load(std::memory_order_acquire))
					{
						continue;
					}
					Sockets.push_back({ Route, EUdpRelayPeer::Host, Route->HostSocket });
					Sockets.push_back({ Route, EUdpRelayPeer::Guest, Route->GuestSocket });
				}
			}

			if (Sockets.empty())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(Config_.SelectTimeoutMilliseconds));
				RemoveIdleRoutes();
				continue;
			}

			fd_set ReadSet;
			FD_ZERO(&ReadSet);
		#ifndef _WIN32
			int HighestSocket = -1;
		#endif
			for (const FSocketSnapshot& Entry : Sockets)
			{
				FD_SET(Entry.Socket, &ReadSet);
			#ifndef _WIN32
				HighestSocket = std::max(HighestSocket, Entry.Socket);
			#endif
			}

			timeval Timeout{};
			Timeout.tv_sec = static_cast<long>(Config_.SelectTimeoutMilliseconds / 1000);
			Timeout.tv_usec = static_cast<long>((Config_.SelectTimeoutMilliseconds % 1000) * 1000);
			const int Ready = ::select(
		#ifdef _WIN32
				0,
		#else
				HighestSocket + 1,
		#endif
				&ReadSet, nullptr, nullptr, &Timeout);

			if (Ready < 0)
			{
				if (!IsSocketInterrupted(LastNetError()))
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
				continue;
			}
			if (Ready == 0)
			{
				RemoveIdleRoutes();
				continue;
			}

			for (const FSocketSnapshot& Entry : Sockets)
			{
				if (!bRunning_.load(std::memory_order_acquire) || !Entry.Route->bActive.load(std::memory_order_acquire) ||
				    !FD_ISSET(Entry.Socket, &ReadSet))
				{
					continue;
				}

				for (std::uint32_t Drained = 0; Drained < kMaxDatagramsDrainedPerSocket; ++Drained)
				{
					std::array<std::uint8_t, kUdpRelayMaxDatagramBytes> Buffer;
					sockaddr_in From{};
				#ifdef _WIN32
					int WindowsFromLength = static_cast<int>(sizeof(From));
					const int Read = ::recvfrom(Entry.Socket, reinterpret_cast<char*>(Buffer.data()),
					                            static_cast<int>(Buffer.size()), 0,
					                            reinterpret_cast<sockaddr*>(&From), &WindowsFromLength);
				#else
					socklen_t FromLength = static_cast<socklen_t>(sizeof(From));
					const int Read = static_cast<int>(::recvfrom(Entry.Socket, Buffer.data(), Buffer.size(), 0,
					                                                 reinterpret_cast<sockaddr*>(&From), &FromLength));
				#endif
					if (Read < 0)
					{
						if (!IsSocketWouldBlock(LastNetError()))
						{
							DroppedPackets_.fetch_add(1, std::memory_order_relaxed);
						}
						break;
					}

					const std::size_t ReadBytes = static_cast<std::size_t>(Read);
					if (!Entry.Route->bActive.load(std::memory_order_acquire))
					{
						DroppedPackets_.fetch_add(1, std::memory_order_relaxed);
						continue;
					}

					if (IsRegistrationCandidate(Buffer.data(), ReadBytes))
					{
						const auto& Datagram = *reinterpret_cast<const FUdpRelayRegistrationDatagram*>(Buffer.data());
						bool bAccepted = false;
						bool bBecameReady = false;
						{
							std::lock_guard<std::mutex> StateLock(Entry.Route->StateMutex);
							const bool bValid = Datagram.Version == kRelayRegistrationVersion &&
							                    IsValidPeerByte(Datagram.Peer) &&
							                    Datagram.Peer == static_cast<std::uint8_t>(Entry.Peer) &&
							                    Datagram.Reserved[0] == 0 && Datagram.Reserved[1] == 0 &&
							                    DecodeUdpRelayRegistrationRouteId(Datagram) == Entry.Route->Id &&
							                    ConstantTimeEqual(Datagram.Token,
							                                      (Entry.Peer == EUdpRelayPeer::Host
									? Entry.Route->HostToken.data()
									: Entry.Route->GuestToken.data()),
							                                      kUdpRelayTokenBytes);

							if (bValid)
							{
								FRoute::FPeerState& PeerState =
									Entry.Peer == EUdpRelayPeer::Host ? Entry.Route->Host : Entry.Route->Guest;
								const FClock::time_point Now = FClock::now();
								if (!PeerState.bRegistered || !IsSameEndpoint(PeerState.Endpoint, From) ||
									Now - PeerState.LastRegistration >= kMinimumRegistrationInterval)
								{
									PeerState.bRegistered = true;
									PeerState.Endpoint = From; // Observed UDP source; never client supplied.
									PeerState.LastRegistration = Now;
									Entry.Route->LastActivity = Now;
									bAccepted = true;
									if (Entry.Route->Host.bRegistered && Entry.Route->Guest.bRegistered &&
										!Entry.Route->bReadyLogged)
									{
										Entry.Route->bReadyLogged = true;
										bBecameReady = true;
									}
								}
							}
						}

						if (bAccepted)
						{
							AcceptedRegistrations_.fetch_add(1, std::memory_order_relaxed);
							char Address[INET_ADDRSTRLEN] = {};
							(void)::inet_ntop(AF_INET, &From.sin_addr, Address, sizeof(Address));
							std::printf("[릴레이] route=%llu %s 등록: %s:%u\n",
								static_cast<unsigned long long>(Entry.Route->Id),
								Entry.Peer == EUdpRelayPeer::Host ? "host" : "guest",
								Address[0] != '\0' ? Address : "<unknown>",
								static_cast<unsigned>(::ntohs(From.sin_port)));
							if (bBecameReady)
							{
								std::printf("[릴레이] route=%llu 양쪽 등록 완료. UE UDP 전달을 시작한다.\n",
									static_cast<unsigned long long>(Entry.Route->Id));
							}
						}
						else
						{
							RejectedRegistrations_.fetch_add(1, std::memory_order_relaxed);
						}
						continue;
					}

					sockaddr_in Destination{};
					SocketHandle OutgoingSocket = kInvalidSocket;
					bool bRateLimited = false;
					bool bForward = false;
					{
						std::lock_guard<std::mutex> StateLock(Entry.Route->StateMutex);
						FRoute::FPeerState& Source =
							Entry.Peer == EUdpRelayPeer::Host ? Entry.Route->Host : Entry.Route->Guest;
						FRoute::FPeerState& Target =
							Entry.Peer == EUdpRelayPeer::Host ? Entry.Route->Guest : Entry.Route->Host;

						if (!Source.bRegistered || !Target.bRegistered || !IsSameEndpoint(Source.Endpoint, From))
						{
							// Never learn an endpoint from arbitrary gameplay-shaped data.
							// A NAT rebinding must prove possession of the route token again.
						}
						else if (!Source.RateWindow.Allow(FClock::now(), ReadBytes, Config_))
						{
							bRateLimited = true;
						}
						else
						{
							Destination = Target.Endpoint;
							OutgoingSocket = Entry.Peer == EUdpRelayPeer::Host
								? Entry.Route->GuestSocket
								: Entry.Route->HostSocket;
							Entry.Route->LastActivity = FClock::now();
							bForward = true;
						}
					}

					if (!bForward)
					{
						DroppedPackets_.fetch_add(1, std::memory_order_relaxed);
						if (bRateLimited)
						{
							RateLimitedPackets_.fetch_add(1, std::memory_order_relaxed);
						}
						continue;
					}

					const int Sent = ::sendto(OutgoingSocket, reinterpret_cast<const char*>(Buffer.data()),
					                          static_cast<int>(ReadBytes), 0,
					                          reinterpret_cast<const sockaddr*>(&Destination), sizeof(Destination));
					if (Sent == Read)
					{
						ForwardedPackets_.fetch_add(1, std::memory_order_relaxed);
						ForwardedBytes_.fetch_add(ReadBytes, std::memory_order_relaxed);
					}
					else
					{
						DroppedPackets_.fetch_add(1, std::memory_order_relaxed);
					}
				}
			}

			RemoveIdleRoutes();
		}
	}

	void UdpRelay::RemoveIdleRoutes()
	{
		if (Config_.IdleRouteTimeoutSeconds == 0)
		{
			return;
		}

		const FClock::time_point Now = FClock::now();
		const auto Timeout = std::chrono::seconds(Config_.IdleRouteTimeoutSeconds);
		std::vector<std::pair<RouteId, std::shared_ptr<FRoute>>> Snapshot;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			Snapshot.reserve(Routes_.size());
			for (const auto& Pair : Routes_)
			{
				Snapshot.push_back(Pair);
			}
		}

		std::vector<RouteId> Expired;
		for (const auto& Pair : Snapshot)
		{
			const std::shared_ptr<FRoute>& Route = Pair.second;
			std::lock_guard<std::mutex> StateLock(Route->StateMutex);
			if (Route->bActive.load(std::memory_order_acquire) && Now - Route->LastActivity >= Timeout)
			{
				Expired.push_back(Pair.first);
			}
		}

		if (Expired.empty())
		{
			return;
		}

		std::lock_guard<std::mutex> Lock(Mutex_);
		for (const RouteId Id : Expired)
		{
			const auto It = Routes_.find(Id);
			if (It == Routes_.end())
			{
				continue;
			}

			// The route can receive a packet after the first expiry snapshot.  Check
			// activity once more before destroying an otherwise live game session.
			std::lock_guard<std::mutex> StateLock(It->second->StateMutex);
			if (It->second->bActive.load(std::memory_order_acquire) &&
				Now - It->second->LastActivity >= Timeout)
			{
				It->second->bActive.store(false, std::memory_order_release);
				Routes_.erase(It);
			}
		}
	}
}
