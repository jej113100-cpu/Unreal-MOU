#include "Rooms.h"

#include "Framing.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>

namespace MOU
{
namespace Rooms
{
namespace
{
	struct Member
	{
		uint64_t    UserId = 0;
		std::string Name;
		bool        bReady = false;
	};

	struct Room
	{
		uint32_t    RoomId = 0;
		uint64_t    HostUserId = 0;
		std::string HostName;
		std::vector<HostCandidate> Candidates;   // 호스트에게 가는 길들 (v8). Server.cpp 가 만든다

		/**
		 * 이 방은 같은 LAN 안에서만 들어올 수 있는가. (v9)
		 *
		 * 호스트가 도달성 프로브 결과를 신고하면 켜진다. 서버가 스스로 판정하지 않는다 —
		 * 패킷이 실제로 도착했는지는 호스트 프로세스 안에서만 관측되기 때문이다.
		 *
		 * 기본값이 false 인 이유: 프로브를 못 돌린 경우(구버전 클라, 프로브 실패)에
		 * 방을 막아버리면 잘 되던 조합까지 못 쓰게 된다. 모르면 예전처럼 둔다.
		 */
		bool        bLanOnly = false;

		std::string Title;
		bool        bHasPassword = false;
		std::string Password;       // 숫자 4자리. 방과 함께 사라지는 휘발성 값이다
		uint8_t     MaxPlayers = static_cast<uint8_t>(kMaxPlayersInRoom);
		ERoomState  State = ERoomState::Waiting;

		// [0] 은 항상 방장이다. Create 에서 넣고, 방장이 나가면 방 자체가 사라지므로
		// 이 불변식은 방이 살아있는 동안 유지된다.
		std::vector<Member> Members;
	};

	std::mutex               GMutex;
	std::map<uint32_t, Room> GRooms;       // RoomId -> Room. 번호순 정렬이 목록 순서로도 쓸 만하다
	uint32_t                 GNextRoomId = 1;

	// --- 아래 헬퍼들은 전부 GMutex 를 이미 잡은 상태에서만 부른다 ---

	// 한 사람이 방을 여러 개 만들지 못하게 막는다.
	// 리슨서버는 한 프로세스당 하나뿐이라, 방이 둘이면 하나는 반드시 유령이 된다.
	const Room* FindRoomByHost(uint64_t HostUserId)
	{
		for (const auto& Pair : GRooms)
		{
			if (Pair.second.HostUserId == HostUserId)
			{
				return &Pair.second;
			}
		}
		return nullptr;
	}

	/** 이 사람이 멤버로 들어가 있는 방. 없으면 nullptr. */
	Room* FindRoomOfMember(uint64_t UserId)
	{
		for (auto& Pair : GRooms)
		{
			for (const Member& M : Pair.second.Members)
			{
				if (M.UserId == UserId)
				{
					return &Pair.second;
				}
			}
		}
		return nullptr;
	}

	/**
	 * 방장을 뺀 참여자가 전부 준비했는가.
	 *
	 * 참여자가 아무도 없으면(방장 혼자) true 다. 혼자 시작하는 것을 막지 않는다 —
	 * 1인 테스트가 개발 중에 가장 많이 필요하기 때문이다.
	 */
	bool AreAllReady(const Room& R)
	{
		for (const Member& M : R.Members)
		{
			if (M.UserId != R.HostUserId && !M.bReady)
			{
				return false;
			}
		}
		return true;
	}

	/** 방의 멤버 UserId 를 담는다. 나간 본인 등 한 명을 뺄 수 있다(Except=0 이면 전원). */
	void CollectMemberIds(const Room& R, uint64_t Except, std::vector<uint64_t>& Out)
	{
		Out.clear();
		for (const Member& M : R.Members)
		{
			if (M.UserId != Except)
			{
				Out.push_back(M.UserId);
			}
		}
	}
}

ERoomResult Create(uint64_t HostUserId, const std::string& HostName,
                   const std::vector<HostCandidate>& Candidates,
                   bool bLanOnly,
                   const std::string& Title, bool bHasPassword,
                   const std::string& Password, uint8_t MaxPlayers,
                   uint32_t& OutRoomId)
{
	if (Candidates.empty() || Title.empty())
	{
		return ERoomResult::InvalidRequest;
	}
	if (bHasPassword && Password.size() != kRoomPasswordLen)
	{
		return ERoomResult::InvalidRequest;
	}

	std::lock_guard<std::mutex> Lock(GMutex);

	if (FindRoomByHost(HostUserId) != nullptr)
	{
		return ERoomResult::AlreadyHosting;
	}
	// 남의 방에 들어가 있으면서 방을 새로 만들 수는 없다.
	// 먼저 나가야 이전 방의 멤버 목록이 정리된다.
	if (FindRoomOfMember(HostUserId) != nullptr)
	{
		return ERoomResult::AlreadyHosting;
	}

	Room NewRoom;
	NewRoom.RoomId       = GNextRoomId++;
	NewRoom.HostUserId   = HostUserId;
	NewRoom.HostName     = HostName;
	NewRoom.Candidates   = Candidates;
	NewRoom.bLanOnly     = bLanOnly;   // 프로브 결과. 방보다 먼저 올 수 있어 세션이 들고 있다가 여기서 얹는다

	NewRoom.Title        = Title;
	NewRoom.bHasPassword = bHasPassword;
	NewRoom.Password     = bHasPassword ? Password : std::string();
	NewRoom.MaxPlayers   = (MaxPlayers == 0 || MaxPlayers > kMaxPlayersInRoom)
	                       ? static_cast<uint8_t>(kMaxPlayersInRoom) : MaxPlayers;

	// 방장은 [0] 번 멤버로 들어간다. 준비 여부를 묻지 않으므로 true 로 둔다.
	Member Host;
	Host.UserId = HostUserId;
	Host.Name   = HostName;
	Host.bReady = true;
	NewRoom.Members.push_back(std::move(Host));

	OutRoomId = NewRoom.RoomId;
	GRooms.emplace(NewRoom.RoomId, std::move(NewRoom));
	return ERoomResult::Success;
}

ERoomResult Join(uint32_t RoomId, uint64_t UserId, const std::string& Name,
                 const std::string& Password,
                 std::vector<HostCandidate>& OutCandidates, bool& bOutLanOnly)
{
	std::lock_guard<std::mutex> Lock(GMutex);

	auto It = GRooms.find(RoomId);
	if (It == GRooms.end())
	{
		return ERoomResult::NotFound;
	}

	Room& R = It->second;

	if (R.State != ERoomState::Waiting)
	{
		return ERoomResult::AlreadyStarted;
	}
	if (R.bHasPassword && Password != R.Password)
	{
		return ERoomResult::WrongPassword;
	}

	// 이미 이 방에 있으면 다시 넣지 않는다. 재전송이나 더블클릭으로
	// 같은 사람이 목록에 두 번 뜨는 것을 막는다. 주소는 그대로 다시 준다.
	for (const Member& M : R.Members)
	{
		if (M.UserId == UserId)
		{
			OutCandidates = R.Candidates;
			bOutLanOnly   = R.bLanOnly;
			return ERoomResult::Success;
		}
	}

	if (R.Members.size() >= R.MaxPlayers)
	{
		return ERoomResult::Full;
	}
	// 다른 방에 있던 사람이면 거기서 먼저 빠져나와야 한다.
	// 조용히 옮겨주면 원래 방의 멤버들에게 알릴 기회를 놓친다.
	if (FindRoomOfMember(UserId) != nullptr)
	{
		return ERoomResult::InvalidRequest;
	}

	Member NewMember;
	NewMember.UserId = UserId;
	NewMember.Name   = Name;
	NewMember.bReady = false;   // 들어오면 준비 안 된 상태로 시작한다
	R.Members.push_back(std::move(NewMember));

	OutCandidates = R.Candidates;
	bOutLanOnly   = R.bLanOnly;

	return ERoomResult::Success;
}

void Leave(uint64_t UserId, uint32_t& OutRoomId, bool& bOutRoomClosed,
           std::vector<uint64_t>& OutNotifyUserIds)
{
	OutRoomId      = 0;
	bOutRoomClosed = false;
	OutNotifyUserIds.clear();

	std::lock_guard<std::mutex> Lock(GMutex);

	Room* R = FindRoomOfMember(UserId);
	if (R == nullptr)
	{
		return;   // 어느 방에도 없다. 접속 종료 경로에서 그냥 불러도 안전하다
	}

	OutRoomId = R->RoomId;

	// 나갈 사람을 뺀 나머지가 소식을 받을 대상이다. 방을 지우기 전에 떠 둬야 한다.
	CollectMemberIds(*R, UserId, OutNotifyUserIds);

	if (R->HostUserId == UserId)
	{
		// 방장이 나갔다 = 리슨서버가 사라졌다 = 방이 성립하지 않는다.
		// 이양하지 않고 없앤다. 남은 사람들에게는 호출자가 RoomClosed 를 보낸다.
		bOutRoomClosed = true;
		GRooms.erase(R->RoomId);
		return;
	}

	R->Members.erase(
		std::remove_if(R->Members.begin(), R->Members.end(),
		               [UserId](const Member& M) { return M.UserId == UserId; }),
		R->Members.end());
}

ERoomResult SetReady(uint64_t UserId, bool bReady, uint32_t& OutRoomId)
{
	OutRoomId = 0;

	std::lock_guard<std::mutex> Lock(GMutex);

	Room* R = FindRoomOfMember(UserId);
	if (R == nullptr)
	{
		return ERoomResult::NotInRoom;
	}
	if (R->State != ERoomState::Waiting)
	{
		return ERoomResult::AlreadyStarted;
	}

	OutRoomId = R->RoomId;

	for (Member& M : R->Members)
	{
		if (M.UserId == UserId)
		{
			// 방장은 늘 준비된 것으로 본다. 자기가 시작 버튼을 누르는 사람이라
			// 따로 준비를 물을 이유가 없다.
			M.bReady = (UserId == R->HostUserId) ? true : bReady;
			return ERoomResult::Success;
		}
	}
	return ERoomResult::NotInRoom;
}

void UpdateHostEndpoint(uint64_t HostUserId, const std::string& Address, uint16_t Port)
{
	if (Address.empty() || Port == 0)
	{
		return;
	}

	std::lock_guard<std::mutex> Lock(GMutex);

	Room* R = FindRoomOfMember(HostUserId);
	if (R == nullptr || R->HostUserId != HostUserId)
	{
		return;   // 방장이 아니면 방 주소를 고칠 이유가 없다
	}

	// ★ 공인 후보를 **덮지 않는다.** 별도 후보로 **추가**한다.
	//
	//   [왜 그런가 — 덮었다가 잘 되던 것을 깼다]
	//     방장이 공유기에 수동 포워딩을 해둔 경우(외부 UDP 7777 -> 그 PC), 공인
	//     후보 211.244.139.254:7777 은 **이미 동작하는 길**이다. 그런데 관측된
	//     동적 바인딩 포트(예: 1037)로 덮어버리면 그 포트에는 포워딩이 없어서
	//     아무도 못 들어온다. 실제로 그렇게 회귀가 났다.
	//
	//   두 길은 성격이 다르고 둘 다 유효할 수 있다:
	//     Public  정적 포워딩/UPnP 로 열린 길. 미요청 인바운드가 통하는 방장용
	//     Punch   홀펀칭으로 뚫는 길. 정적 인바운드가 막힌 방장용
	//   어느 것을 쓸지는 받는 쪽이 방의 bLanOnly 를 보고 정한다.
	for (HostCandidate& C : R->Candidates)
	{
		if (C.Kind == static_cast<uint8_t>(EHostAddrKind::Punch))
		{
			CopyFixedString(C.Address, kMaxAddressLen, Address);   // 이미 있으면 갱신
			C.Port = Port;
			return;
		}
	}

	if (R->Candidates.size() < kMaxHostCandidates)
	{
		HostCandidate Punch{};
		CopyFixedString(Punch.Address, kMaxAddressLen, Address);
		Punch.Port = Port;
		Punch.Kind = static_cast<uint8_t>(EHostAddrKind::Punch);
		R->Candidates.push_back(Punch);
	}
}

ERoomResult SetReachability(uint64_t HostUserId, bool bReachable, uint32_t& OutRoomId)
{
	OutRoomId = 0;

	std::lock_guard<std::mutex> Lock(GMutex);

	Room* R = FindRoomOfMember(HostUserId);
	if (R == nullptr)
	{
		return ERoomResult::NotInRoom;
	}
	if (R->HostUserId != HostUserId)
	{
		return ERoomResult::NotHost;
	}

	// 서버가 판정하지 않고 호스트의 신고를 그대로 받는다.
	// 패킷이 실제로 도착했는지는 호스트 프로세스 안에서만 관측되기 때문이다.
	R->bLanOnly = !bReachable;
	OutRoomId   = R->RoomId;
	return ERoomResult::Success;
}

ERoomResult StartGame(uint64_t HostUserId, uint32_t& OutRoomId,
                      std::vector<HostCandidate>& OutCandidates, bool& bOutLanOnly,
                      std::vector<uint64_t>& OutNotifyUserIds)
{
	OutRoomId = 0;
	OutNotifyUserIds.clear();

	std::lock_guard<std::mutex> Lock(GMutex);

	Room* R = FindRoomOfMember(HostUserId);
	if (R == nullptr)
	{
		return ERoomResult::NotInRoom;
	}
	if (R->HostUserId != HostUserId)
	{
		return ERoomResult::NotHost;
	}
	if (R->State != ERoomState::Waiting)
	{
		return ERoomResult::AlreadyStarted;
	}
	// 클라이언트가 버튼을 잠가두지만 그건 UX 일 뿐이다. 판정은 여기서 다시 한다.
	if (!AreAllReady(*R))
	{
		return ERoomResult::NotAllReady;
	}

	R->State = ERoomState::InGame;   // 목록에서 사라진다

	OutRoomId      = R->RoomId;
	OutCandidates = R->Candidates;
	bOutLanOnly   = R->bLanOnly;

	CollectMemberIds(*R, /*Except=*/0, OutNotifyUserIds);   // 방장도 포함
	return ERoomResult::Success;
}

ERoomResult MarkHostReady(uint64_t HostUserId, uint32_t& OutRoomId,
                          std::vector<HostCandidate>& OutCandidates, bool& bOutLanOnly,
                          std::vector<uint64_t>& OutNotifyUserIds)
{
	OutRoomId = 0;
	OutNotifyUserIds.clear();

	std::lock_guard<std::mutex> Lock(GMutex);

	Room* R = FindRoomOfMember(HostUserId);
	if (R == nullptr)
	{
		return ERoomResult::NotInRoom;
	}
	if (R->HostUserId != HostUserId)
	{
		return ERoomResult::NotHost;
	}
	// StartGame 이 State 를 InGame 으로 바꾼다. 그 전에 온 신고는 순서가 뒤집힌 것이므로
	// 중계하지 않는다. 대기실에 있는 사람을 아직 열리지도 않은 게임으로 보낼 수는 없다.
	if (R->State != ERoomState::InGame)
	{
		return ERoomResult::NotStarted;
	}

	OutRoomId      = R->RoomId;
	OutCandidates = R->Candidates;
	bOutLanOnly   = R->bLanOnly;


	// 방장은 뺀다. 이 신호를 보낸 당사자이고, 이미 자기 리슨서버 안에 있다.
	CollectMemberIds(*R, /*Except=*/HostUserId, OutNotifyUserIds);
	return ERoomResult::Success;
}

bool GetMembers(uint32_t RoomId, std::vector<RoomMemberInfo>& OutMembers,
                bool& bOutAllReady, std::vector<uint64_t>& OutNotifyUserIds)
{
	OutMembers.clear();
	OutNotifyUserIds.clear();
	bOutAllReady = false;

	std::lock_guard<std::mutex> Lock(GMutex);

	auto It = GRooms.find(RoomId);
	if (It == GRooms.end())
	{
		return false;
	}

	const Room& R = It->second;

	for (const Member& M : R.Members)
	{
		RoomMemberInfo Info{};
		Info.UserId  = M.UserId;
		Info.bIsHost = (M.UserId == R.HostUserId) ? 1 : 0;
		Info.bReady  = M.bReady ? 1 : 0;
		CopyFixedString(Info.Name, kMaxNameLen, M.Name);
		OutMembers.push_back(Info);

		OutNotifyUserIds.push_back(M.UserId);
	}

	bOutAllReady = AreAllReady(R);
	return true;
}

uint32_t FindRoomOf(uint64_t UserId)
{
	std::lock_guard<std::mutex> Lock(GMutex);
	const Room* R = FindRoomOfMember(UserId);
	return R ? R->RoomId : 0;
}

bool GetRoomStateOf(uint64_t UserId, ERoomState& OutState)
{
	std::lock_guard<std::mutex> Lock(GMutex);
	const Room* R = FindRoomOfMember(UserId);

	if (R == nullptr)
	{
		return false;
	}

	OutState = R->State;
	return true;
}

void ListWaiting(std::vector<RoomInfo>& Out, size_t MaxCount)
{
	std::lock_guard<std::mutex> Lock(GMutex);

	Out.clear();

	// 최근에 만든 방이 위로 오도록 뒤에서부터 훑는다(RoomId 는 증가하므로).
	for (auto It = GRooms.rbegin(); It != GRooms.rend() && Out.size() < MaxCount; ++It)
	{
		const Room& R = It->second;

		// 시작됐거나 꽉 찬 방은 들어갈 수 없으므로 목록에서 뺀다.
		if (R.State != ERoomState::Waiting || R.Members.size() >= R.MaxPlayers)
		{
			continue;
		}

		RoomInfo Info{};
		Info.RoomId         = R.RoomId;
		Info.HostUserId     = R.HostUserId;
		// 서버가 직접 센 값이다. v4 까지는 호스트가 신고하는 값이라 늘 어긋나 있었다.
		Info.CurrentPlayers = static_cast<uint8_t>(R.Members.size());
		Info.MaxPlayers     = R.MaxPlayers;
		Info.bHasPassword   = R.bHasPassword ? 1 : 0;
		Info.State          = static_cast<uint8_t>(R.State);
		CopyFixedString(Info.Title,    kMaxRoomTitleLen, R.Title);
		CopyFixedString(Info.HostName, kMaxNameLen,      R.HostName);

		Out.push_back(Info);
	}
}

ERoomResult UpdateState(uint32_t RoomId, uint64_t RequesterUserId, ERoomState State)
{
	std::lock_guard<std::mutex> Lock(GMutex);

	auto It = GRooms.find(RoomId);
	if (It == GRooms.end())
	{
		return ERoomResult::NotFound;
	}

	Room& R = It->second;

	// 방장만 고칠 수 있다. 남이 남의 방을 "시작됨" 으로 만들어 목록에서 지우는 것을 막는다.
	if (R.HostUserId != RequesterUserId)
	{
		return ERoomResult::NotAuthed;
	}

	R.State = State;
	return ERoomResult::Success;
}

size_t Count()
{
	std::lock_guard<std::mutex> Lock(GMutex);
	return GRooms.size();
}

} // namespace Rooms
} // namespace MOU
