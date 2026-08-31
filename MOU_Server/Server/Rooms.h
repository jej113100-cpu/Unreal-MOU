// MOU 로비 - 방 목록 레지스트리.
//
// [이 서버가 하는 일과 하지 않는 일]
//   한다:   "지금 어떤 방이 열려 있고, 호스트 주소가 무엇인지" 를 보관하고 알려준다.
//   안 한다: 게임 시뮬레이션. 그건 방장의 리슨서버가 전부 처리한다.
//
//   즉 이 서버는 주소록이다. 참가자가 방 주소를 받아 호스트에게 직접 붙고 나면
//   게임 트래픽은 이 프로세스를 한 바이트도 지나가지 않는다.
//   그래서 방이 몇 개 열려도 서버 부하는 사실상 늘지 않는다.
//
// [왜 SQLite 에 저장하지 않는가]
//   방은 휘발성이다. 서버가 재시작하면 호스트들의 리슨서버도 이미 죽어 있으므로,
//   방 목록을 복원하는 것은 오히려 "들어갈 수 없는 방" 을 보여주는 셈이 된다.
//   계정(영속)과 채팅 로그(영속)와 달리 방은 메모리에만 둔다.
//
// [수명]
//   방은 호스트의 TCP 연결에 묶인다. 호스트가 접속을 끊으면(게임 종료, 강제 종료,
//   랜선 뽑힘 모두 포함) 세션 정리 과정에서 방도 같이 사라진다.
//   그래서 "유령 방" 이 목록에 남지 않는다.
//
//   >> 호스트 이양은 하지 않는다. <<
//   호스트가 곧 리슨서버라서 호스트 프로세스가 죽으면 게임 세션 자체가 사라진다.
//   남은 사람을 새 호스트로 세우려면 리슨서버를 다시 열고 전원이 새 주소로
//   재접속해야 하는데 UE 가 이를 기본 지원하지 않는다. 방을 없애고 모두
//   메인메뉴로 돌려보내는 편이 정직하다. 남은 멤버에게는 RoomClosed 를 보낸다.
//
// [v5 — 멤버 추적]
//   v4 까지 방은 {호스트, 주소, 비번} 뿐이었다. 참여자를 어디에도 기록하지 않아서
//   서버가 참여자에게 아무것도 보낼 수 없었고("방장이 나갔다" 조차),
//   인원수는 호스트가 신고하는 값에 의존했다.
//   이제 Members 를 직접 들고 있으므로 인원수도 준비 상태도 서버가 진실을 안다.
//
// [락 규칙 — 중요]
//   이 모듈의 함수는 절대 세션 락(SessionManager)을 잡지 않는다.
//   대신 "누구에게 알려야 하는지" 를 UserId 목록으로 돌려주고,
//   실제 전송은 호출자(Server.cpp)가 락을 푼 뒤에 한다.
//   두 락을 겹쳐 잡으면 반대 순서로 잡는 코드가 하나만 생겨도 데드락이 난다.

#pragma once

#include "ChatProtocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace MOU
{
	namespace Rooms
	{
		/**
		 * 방을 만든다.
		 *
		 * @param HostUserId  방장의 계정 번호
		 * @param HostName    방장 닉네임 (목록 표시용)
		 * @param Candidates  호스트에게 가는 길들. (v8)
		 *                    공인 주소는 서버가 TCP 연결에서 읽은 값이고, 사설 주소는
		 *                    호스트가 신고했지만 서버가 사설 대역인지 검사한 뒤에만 들어온다.
		 *                    만드는 곳은 Server.cpp 의 BuildHostCandidates 하나뿐이다.
		 * @param OutRoomId   성공 시 새 방 번호
		 */
		ERoomResult Create(uint64_t HostUserId, const std::string& HostName,
		                   const std::vector<HostCandidate>& Candidates,
		                   bool bLanOnly,
		                   const std::string& Title, bool bHasPassword,
		                   const std::string& Password, uint8_t MaxPlayers,
		                   uint32_t& OutRoomId);

		/**
		 * 방에 들어간다. 성공하면 멤버로 등록되고 호스트 주소를 돌려받는다.
		 *
		 * 비밀번호를 여기서 검사하는 이유는 UX 다 — 틀린 비밀번호로 굳이 접속을
		 * 시도했다가 튕기는 것보다, 주소를 아예 주지 않는 편이 낫다.
		 *
		 * >> 다만 이것이 유일한 관문이어서는 안 된다. <<
		 *    목록을 거치지 않고 호스트 IP 로 직접 붙는 것은 막을 수 없으므로,
		 *    호스트의 GameMode::PreLogin 에서도 반드시 다시 검사해야 한다.
		 */
		ERoomResult Join(uint32_t RoomId, uint64_t UserId, const std::string& Name,
		                 const std::string& Password,
		                 std::vector<HostCandidate>& OutCandidates, bool& bOutLanOnly);

		/**
		 * 방에서 나간다. 방장이 나가면 방이 통째로 사라진다.
		 *
		 * @param OutRoomId       나온 방 번호. 어느 방에도 없었으면 0
		 * @param bOutRoomClosed  true 면 방장이 나가서 방이 없어졌다
		 * @param OutNotifyUserIds 소식을 전해야 할 남은 멤버들.
		 *                        방이 닫혔으면 RoomClosed 를, 아니면 RoomMemberList 를 보낸다.
		 *                        나간 본인은 여기 포함되지 않는다.
		 *
		 * 어느 방에도 속해있지 않아도 안전하다. 접속이 끊길 때 그냥 불러도 된다.
		 */
		void Leave(uint64_t UserId, uint32_t& OutRoomId, bool& bOutRoomClosed,
		           std::vector<uint64_t>& OutNotifyUserIds);

		/**
		 * 준비 상태를 바꾼다. 방장은 늘 준비된 것으로 보므로 호출해도 무시된다.
		 * @param OutRoomId 바뀐 방 번호
		 */
		ERoomResult SetReady(uint64_t UserId, bool bReady, uint32_t& OutRoomId);

		/**
		 * 게임을 시작한다. 방장만, 그리고 전원이 준비했을 때만 성공한다.
		 * 성공하면 방이 InGame 이 되어 목록에서 사라진다.
		 *
		 * [v6] 이 신호는 "게임이 시작됐다" 까지다. 참여자가 실제로 떠나는 시점은
		 *      MarkHostReady 가 정한다 — 호스트의 리슨서버가 아직 안 열렸을 수 있다.
		 *
		 * @param OutNotifyUserIds 시작을 알려야 할 멤버 전원 (방장 포함)
		 */
		ERoomResult StartGame(uint64_t HostUserId, uint32_t& OutRoomId,
		                      std::vector<HostCandidate>& OutCandidates,
		                      bool& bOutLanOnly,
		                      std::vector<uint64_t>& OutNotifyUserIds);

		/**
		 * 호스트가 "리슨서버가 열렸다" 고 알려왔다. 참여자에게 출발 신호를 넘길 때 쓴다.
		 *
		 * [서버는 이 사실을 스스로 알 수 없다]
		 *   맵 로딩이 끝나 접속을 받기 시작한 시점은 호스트 프로세스 안에서만 관측된다.
		 *   그렇다고 서버가 호스트의 게임 포트로 직접 붙어 확인하게 만들면, 이 프로세스가
		 *   게임 트래픽에 발을 담그는 셈이라 "주소록" 이라는 역할을 스스로 깬다.
		 *   그래서 호스트의 신고를 그대로 중계한다.
		 *
		 * @param OutNotifyUserIds 떠나라고 알려야 할 참여자들 (방장 제외 — 이미 그 안에 있다)
		 * @return NotInRoom  어느 방에도 없다
		 *         NotHost    방장이 아니다
		 *         NotStarted 아직 StartGame 을 거치지 않은 방이다
		 */
		ERoomResult MarkHostReady(uint64_t HostUserId, uint32_t& OutRoomId,
		                          std::vector<HostCandidate>& OutCandidates,
		                          bool& bOutLanOnly,
		                          std::vector<uint64_t>& OutNotifyUserIds);

		/**
		 * 호스트가 도달성 프로브 결과를 신고했다. (v9)
		 *
		 * 서버는 이 값을 판정하지 않고 그대로 받는다 — 패킷이 실제로 도착했는지는
		 * 호스트 프로세스 안에서만 관측되기 때문이다. 서버가 하는 일은 UDP 를 한 발
		 * 쏘는 것까지고, 그 뒤는 호스트가 알려줘야 안다.
		 *
		 * @return NotInRoom / NotHost / Success
		 */
		ERoomResult SetReachability(uint64_t HostUserId, bool bReachable, uint32_t& OutRoomId);

		/**
		 * 서버가 관측한 방장의 공인 게임 엔드포인트로 방의 공인 후보를 갱신한다. (v10)
		 *
		 * [왜 나중에 고치는가 — 순서 때문이다]
		 *   엔드포인트 등록은 대기실에 들어간 뒤에 일어나므로 **방 생성보다 늦다.**
		 *   방을 만들 때는 UPnP 가 알려준 포트로 공인 후보를 적어두는데, 그 포트는
		 *   홀펀칭으로 실제 뚫리는 구멍과 다를 수 있다 — UPnP 정적 매핑이 그 번호를
		 *   점유하면 동적 흐름은 다른 번호를 받기 때문이다(실측: 7777 vs 1035).
		 *
		 *   관측값이 들어오면 그것으로 덮는다. 참여자가 붙어야 하는 곳은
		 *   "UPnP 가 열었다고 말한 포트" 가 아니라 "실제로 구멍이 뚫린 포트" 다.
		 */
		void UpdateHostEndpoint(uint64_t HostUserId, const std::string& Address, uint16_t Port);

		/**
		 * 방의 현재 멤버 명단을 뜬다. RoomMemberList 를 만들 때 쓴다.
		 *
		 * @param OutNotifyUserIds 이 명단을 받아야 할 사람들 (= 멤버 전원)
		 * @return 방이 없으면 false
		 */
		bool GetMembers(uint32_t RoomId, std::vector<RoomMemberInfo>& OutMembers,
		                bool& bOutAllReady, std::vector<uint64_t>& OutNotifyUserIds);

		/** 이 사람이 지금 들어가 있는 방. 없으면 0. */
		uint32_t FindRoomOf(uint64_t UserId);

		/**
		 * 이 사람이 들어가 있는 방의 진행 상태. 어느 방에도 없으면 false.
		 *
		 * ★ 친구 접속 상태(EPresence)를 정하려고 v7 에서 추가했다.
		 *   FindRoomOf 로 방 번호를 얻고 상태를 다시 묻는 2단계로 하면 그 사이에
		 *   방이 사라져 판정이 흔들린다 - 한 번의 락 안에서 끝내야 한다.
		 */
		bool GetRoomStateOf(uint64_t UserId, ERoomState& OutState);

		/** 대기 중인 방들을 최신순으로 담아준다. 꽉 찬 방과 시작된 방은 빼고 준다. */
		void ListWaiting(std::vector<RoomInfo>& Out, size_t MaxCount);

		/**
		 * 호스트가 진행 상태를 갱신한다.
		 * 방장이 아닌 사람이 부르면 아무 일도 일어나지 않는다.
		 *
		 * [v5] 인원수는 받지 않는다. 서버가 멤버를 직접 세므로 신고값이 필요 없다.
		 */
		ERoomResult UpdateState(uint32_t RoomId, uint64_t RequesterUserId, ERoomState State);

		/** 지금 열려 있는 방 개수. 로그용. */
		size_t Count();
	}
}
