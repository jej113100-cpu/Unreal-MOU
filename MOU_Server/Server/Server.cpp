// MOU 채팅 서버 (1~3단계: 프로토콜 + 세션 + 프레이밍)
//
// 리슨서버와 별개의 프로세스로 상시 가동된다.
// 호스트가 게임을 종료해도 이 프로세스는 살아있으므로 채팅 로그가 유지된다.
//
// 사용법: Server <port> [db경로]

#include "Accounts.h"
#include "ChatLog.h"
#include "Rooms.h"
#include "Session.h"
#include "Framing.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

using namespace MOU;

namespace
{
	SessionManager GSessions;
	std::atomic<bool> GRunning{ true };

	// Ctrl+C 로 서버를 내릴 때 큐에 남은 채팅 로그를 마저 쓰고 나간다.
	// 이게 없으면 accept() 에서 블록된 채 프로세스가 즉사해서
	// 아직 커밋 안 된 로그가 통째로 사라진다.
	//
	// 윈도우 CRT 는 SIGINT 핸들러를 별도 스레드에서 호출하므로
	// 여기서 ChatLog::Stop() 이 라이터 스레드를 join 해도 데드락이 나지 않는다.
	void OnInterrupt(int)
	{
		GRunning = false;
		ChatLog::Stop();
		Accounts::Stop();
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

		return SendPacket(Session->Sock, EOpcode::LoginAck, &Ack, sizeof(Ack));
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

		if (bRoomClosed)
		{
			std::printf("[방 삭제] #%u 방장 %s(%llu) 가 나갔다. 남은 %zu명에게 통보. 남은 방 %zu개\n",
			            RoomId, Session->Name.c_str(),
			            static_cast<unsigned long long>(Session->UserId),
			            Recipients.size(), Rooms::Count());
			NotifyRoomClosed(Recipients, RoomId, ERoomCloseReason::HostLeft);
		}
		else
		{
			std::printf("[방 퇴장] #%u 에서 %s(%llu) 가 나갔다\n",
			            RoomId, Session->Name.c_str(),
			            static_cast<unsigned long long>(Session->UserId));
			BroadcastRoomMembers(RoomId);
		}
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

		uint32_t NewRoomId = 0;
		const ERoomResult R = Rooms::Create(
			Session->UserId, Session->Name, Session->PeerAddress, Req.HostPort,
			Title, Req.bHasPassword != 0, Password, Req.MaxPlayers, NewRoomId);

		if (R == ERoomResult::Success)
		{
			std::printf("[방 생성] #%u \"%s\" 방장=%s(%llu) 주소=%s:%u %s\n",
			            NewRoomId, Title.c_str(), Session->Name.c_str(),
			            static_cast<unsigned long long>(Session->UserId),
			            Session->PeerAddress.c_str(), Req.HostPort,
			            Req.bHasPassword ? "[비번]" : "");
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

		std::string HostAddress;
		uint16_t    HostPort = 0;
		const ERoomResult R = Rooms::Join(Req.RoomId, Session->UserId, Session->Name,
		                                  Password, HostAddress, HostPort);

		Ack.RoomId = Req.RoomId;
		if (R == ERoomResult::Success)
		{
			CopyFixedString(Ack.HostAddress, kMaxAddressLen, HostAddress);
			Ack.HostPort = HostPort;
			std::printf("[방 참여] #%u <- %s(%llu), 주소 %s:%u 전달\n",
			            Req.RoomId, Session->Name.c_str(),
			            static_cast<unsigned long long>(Session->UserId),
			            HostAddress.c_str(), HostPort);
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
		std::string HostAddress;
		uint16_t    HostPort = 0;
		std::vector<uint64_t> Recipients;

		const ERoomResult R = Rooms::StartGame(Session->UserId, RoomId,
		                                       HostAddress, HostPort, Recipients);
		if (R != ERoomResult::Success)
		{
			std::printf("[거부] 게임 시작 실패: %s(%llu) (사유 %u)\n", Session->Name.c_str(),
			            static_cast<unsigned long long>(Session->UserId), static_cast<unsigned>(R));
			return true;
		}

		std::printf("[게임 시작] #%u 방장=%s, 호스트 %s:%u, 인원 %zu명\n",
		            RoomId, Session->Name.c_str(), HostAddress.c_str(), HostPort, Recipients.size());

		RoomStartBody Start{};
		Start.RoomId   = RoomId;
		Start.HostPort = HostPort;
		CopyFixedString(Start.HostAddress, kMaxAddressLen, HostAddress);

		SendToUsers(Recipients, EOpcode::RoomStart, &Start, sizeof(Start), nullptr, 0);
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
	bool HandleRoomHostReadyReq(const SessionPtr& Session, const char*, uint32_t)
	{
		if (!Session->bAuthed)
		{
			return true;
		}

		uint32_t    RoomId = 0;
		std::string HostAddress;
		uint16_t    HostPort = 0;
		std::vector<uint64_t> Recipients;

		const ERoomResult R = Rooms::MarkHostReady(Session->UserId, RoomId,
		                                           HostAddress, HostPort, Recipients);
		if (R != ERoomResult::Success)
		{
			std::printf("[거부] 호스트 준비 신고 실패: %s(%llu) (사유 %u)\n", Session->Name.c_str(),
			            static_cast<unsigned long long>(Session->UserId), static_cast<unsigned>(R));
			return true;
		}

		std::printf("[호스트 준비] #%u 리슨서버 %s:%u 가 열렸다. 참여자 %zu명에게 출발 신호\n",
		            RoomId, HostAddress.c_str(), HostPort, Recipients.size());

		RoomHostReadyBody Ready{};
		Ready.RoomId   = RoomId;
		Ready.HostPort = HostPort;
		CopyFixedString(Ready.HostAddress, kMaxAddressLen, HostAddress);

		SendToUsers(Recipients, EOpcode::RoomHostReady, &Ready, sizeof(Ready), nullptr, 0);
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
		case EOpcode::ChatSend:  return HandleChatSend(Session, Data, Size);
		case EOpcode::SetDead:   return HandleSetDead(Session, Data, Size);
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

	if (argc < 2 || argc > 3)
	{
		std::printf("사용법: %s <port> [db경로]\n", argv[0]);
		std::printf("  db경로를 생략하면 현재 디렉터리의 chat_log.db 를 쓴다.\n");
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

	sockaddr_in ServerAddr{};
	ServerAddr.sin_family      = AF_INET;
	ServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	ServerAddr.sin_port        = htons(static_cast<uint16_t>(std::atoi(argv[1])));

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
	const char* DbPath = (argc >= 3) ? argv[2] : "chat_log.db";
	ChatLog::Start(DbPath);

	// 계정도 같은 파일에 둔다(테이블이 다르므로 섞이지 않는다).
	// 커넥션은 별개다 — ChatLog 쪽은 라이터 스레드 전용이라 남이 끼면 안 된다.
	if (!Accounts::Start(DbPath))
	{
		std::printf("[치명] 계정 DB 를 열지 못했다. 아무도 로그인할 수 없다.\n");
		return 1;
	}

	std::printf("=== MOU 채팅 서버 시작 (port %s) ===\n", argv[1]);

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

	ChatLog::Stop();
	Accounts::Stop();
	CloseSocket(ListenSock);
	NetShutdown();
	return 0;
}
