// UDP relay for listen-server fallback transport.
//
// This module deliberately knows nothing about chat/session state.  It shares
// only the fixed registration wire with Unreal so the two implementations do
// not drift. The caller creates a route after its authenticated control channel
// has created a room, then delivers ports and fresh tokens to the two peers.
// The relay only moves UDP datagrams; it never interprets Unreal packets.
//
// Wire procedure (version 1)
// --------------------------
//  1. Start() binds the relay to a local interface and a *forwarded* UDP port
//     range.  BindAddress is normally "0.0.0.0" or the server's LAN address,
//     not the router's WAN address.
//  2. CreateRoute() reserves two adjacent ports.  Give HostPort to the listen
//     host and GuestPort to the joining peer, each with its own random 32-byte
//     token.  A guest capability must never authenticate the host-facing port.
//  3. Each peer sends FUdpRelayRegistrationDatagram from the exact UDP socket
//     it will use for game traffic: the host to HostPort, the guest to
//     GuestPort.  Use MakeUdpRelayRegistrationDatagram() so RouteId is encoded
//     in network byte order.
//  4. Once both registrations are observed, a host packet received on
//     HostPort is emitted from GuestPort to the guest, and vice versa.  Thus a
//     peer always sees a stable, role-specific relay source port.
//
// Registration packets are control packets only and are never forwarded.
// All other UDP payloads are opaque to this class.  The caller must remove a
// route when its room ends.  The idle timeout is only a safety net.
#pragma once

#include "ChatProtocol.h" // relay registration wire is shared with Unreal; never duplicate it

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace MOU
{
	constexpr std::size_t kUdpRelayTokenBytes = kRelayTokenBytes;
	constexpr std::size_t kUdpRelayMaxDatagramBytes = 65507; // IPv4 UDP payload maximum.

	// Shared protocol aliases: Server.exe and Unreal must never evolve this wire separately.
	using EUdpRelayPeer = ERelayPeerRole;
	using FUdpRelayRegistrationDatagram = RelayRegistrationDatagram;
	static_assert(sizeof(FUdpRelayRegistrationDatagram) == sizeof(RelayRegistrationDatagram),
	              "Relay registration aliases must remain wire-identical.");

	using FUdpRelayToken = std::array<std::uint8_t, kUdpRelayTokenBytes>;

	/** Fills the fixed registration wire payload; Token is copied verbatim. */
	void MakeUdpRelayRegistrationDatagram(FUdpRelayRegistrationDatagram& OutDatagram,
	                                      std::uint64_t RouteId,
	                                      EUdpRelayPeer Peer,
	                                      const FUdpRelayToken& Token);

	/** Decodes the big-endian RouteId field in a received registration packet. */
	std::uint64_t DecodeUdpRelayRegistrationRouteId(const FUdpRelayRegistrationDatagram& Datagram);

	/** Ports returned by CreateRoute().  They are adjacent but intentionally role-specific. */
	struct FUdpRelayPortPair
	{
		std::uint16_t HostPort = 0;
		std::uint16_t GuestPort = 0;

		[[nodiscard]] bool IsValid() const { return HostPort != 0 && GuestPort != 0; }
	};

	/** A snapshot safe to expose to an authenticated session/control server. */
	struct FUdpRelayRouteStatus
	{
		FUdpRelayPortPair Ports;
		bool bHostRegistered = false;
		bool bGuestRegistered = false;
		bool bActive = false;
	};

	/** Lightweight process-wide observability counters. */
	struct FUdpRelayCounters
	{
		std::uint64_t AcceptedRegistrations = 0;
		std::uint64_t RejectedRegistrations = 0;
		std::uint64_t ForwardedPackets = 0;
		std::uint64_t ForwardedBytes = 0;
		std::uint64_t DroppedPackets = 0;
		std::uint64_t RateLimitedPackets = 0;
	};

	/**
	 * Runtime limits.  The default range has 64 pairs but the conservative route
	 * limit is 24 because Windows select() normally supports only FD_SETSIZE=64
	 * sockets.  Start() rejects a configuration that cannot be selected safely.
	 */
	struct FUdpRelayConfig
	{
		std::string BindAddress = "0.0.0.0";
		std::uint16_t FirstPort = 10000;
		std::uint16_t LastPort = 10127;
		std::uint32_t MaxRoutes = 24;

		// Per registered direction, fixed one-second window.  These are caps,
		// not bandwidth guarantees; UDP may still drop under load.
		std::uint32_t MaxPacketsPerSecondPerPeer = 4000;
		std::uint32_t MaxBytesPerSecondPerPeer = 8 * 1024 * 1024;

		// A route with no accepted registration or forwarded packet for this
		// long is discarded.  Set to 0 to disable automatic expiry.
		std::uint32_t IdleRouteTimeoutSeconds = 300;

		// Stop() waits at most roughly this long for a select() sleep.  Values
		// above 250 are rejected so a shutdown cannot hang on a stale socket.
		std::uint32_t SelectTimeoutMilliseconds = 50;
	};

	/**
	 * A small TURN-like UDP relay with no game authority.  MOU::NetInit() must
	 * have succeeded before Start(), and Stop() must finish before NetShutdown().
	 * All public member functions are thread-safe.
	 */
	class UdpRelay final
	{
	public:
		using RouteId = std::uint64_t;

		UdpRelay() = default;
		~UdpRelay();

		UdpRelay(const UdpRelay&) = delete;
		UdpRelay& operator=(const UdpRelay&) = delete;

		/** Starts the worker.  It does not perform UPnP or discover a WAN address. */
		[[nodiscard]] bool Start(const FUdpRelayConfig& Config);

		/** Convenience overload using FUdpRelayConfig's remaining safe defaults. */
		[[nodiscard]] bool Start(const std::string& BindAddress,
		                         std::uint16_t FirstPort,
		                         std::uint16_t LastPort);

		/** Stops forwarding, invalidates all routes, and joins the worker thread. */
		void Stop();

		[[nodiscard]] bool IsRunning() const { return bRunning_.load(std::memory_order_acquire); }

		/**
		 * Allocates an adjacent host/guest socket pair for one route.
		 *
		 * RoomId is opaque bookkeeping so a room-destruction path can remove all
		 * of its routes.  HostToken and GuestToken must be distinct, newly generated
		 * cryptographically random non-zero 32-byte values; neither is logged here.
		 */
		[[nodiscard]] bool CreateRoute(RouteId Id,
		                               const FUdpRelayToken& HostToken,
		                               const FUdpRelayToken& GuestToken,
		                               std::uint64_t RoomId,
		                               FUdpRelayPortPair& OutPorts);

		/** Removes one route.  Safe to call even after it has expired. */
		[[nodiscard]] bool RemoveRoute(RouteId Id);

		/** Removes all relay routes belonging to an opaque room id. */
		[[nodiscard]] std::size_t RemoveRoutesForRoom(std::uint64_t RoomId);

		[[nodiscard]] bool GetRoutePorts(RouteId Id, FUdpRelayPortPair& OutPorts) const;
		[[nodiscard]] bool GetRouteStatus(RouteId Id, FUdpRelayRouteStatus& OutStatus) const;
		[[nodiscard]] FUdpRelayCounters GetCounters() const;

	private:
		struct FRoute;

		void WorkerMain();
		void RemoveIdleRoutes();

		[[nodiscard]] bool IsConfigValid(const FUdpRelayConfig& Config) const;
		[[nodiscard]] bool TryAllocateSocketPair(const std::shared_ptr<FRoute>& Route);

		mutable std::mutex Mutex_;
		std::unordered_map<RouteId, std::shared_ptr<FRoute>> Routes_;
		FUdpRelayConfig Config_;
		std::uint32_t NextPairIndex_ = 0;
		std::thread Worker_;
		std::atomic<bool> bRunning_{ false };

		std::atomic<std::uint64_t> AcceptedRegistrations_{ 0 };
		std::atomic<std::uint64_t> RejectedRegistrations_{ 0 };
		std::atomic<std::uint64_t> ForwardedPackets_{ 0 };
		std::atomic<std::uint64_t> ForwardedBytes_{ 0 };
		std::atomic<std::uint64_t> DroppedPackets_{ 0 };
		std::atomic<std::uint64_t> RateLimitedPackets_{ 0 };
	};
}
