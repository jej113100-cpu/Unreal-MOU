// MOU 채팅 서버 검증용 콘솔 클라이언트.
//
// 기존 실습 클라이언트와 달리, 이름을 문자열에 붙여 보내지 않는다.
// 로그인 패킷으로 이름을 알리고, 이후 서버가 부여한 UserId 로 식별된다.
//
// 사용법:
//   TestClient <ip> <port> <name> [mode]
//
//   mode 생략   : 대화형. /team /dead /alive /all 명령 사용 가능
//   mode split  : 패킷 하나를 1바이트씩 쪼개 전송   (분할 테스트)
//   mode merge  : 패킷 3개를 한 번의 send 로 전송   (합침 테스트)
//   mode bad    : BodySize 를 999999 로 위조해 전송 (방어 테스트)

#include "Framing.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace MOU;

namespace
{
	SocketHandle GSock = kInvalidSocket;
	std::atomic<bool> GRunning{ true };
	uint64_t GMyUserId = 0;

	const char* LoginResultName(ELoginResult R)
	{
		switch (R)
		{
		case ELoginResult::Success:         return "성공";
		case ELoginResult::VersionMismatch: return "프로토콜 버전 불일치";
		case ELoginResult::InvalidRequest:  return "잘못된 요청";
		case ELoginResult::AccountNotFound: return "없는 아이디";
		case ELoginResult::WrongPassword:   return "비밀번호가 틀림";
		case ELoginResult::DuplicateId:     return "이미 있는 아이디";
		case ELoginResult::InvalidFormat:   return "아이디/비밀번호 형식 위반";
		case ELoginResult::ServerError:     return "서버 오류";
		default:                            return "알 수 없음";
		}
	}

	const char* RoomResultName(ERoomResult R)
	{
		switch (R)
		{
		case ERoomResult::Success:        return "성공";
		case ERoomResult::NotAuthed:      return "로그인 필요";
		case ERoomResult::NotFound:       return "없는 방";
		case ERoomResult::WrongPassword:  return "방 비밀번호 불일치";
		case ERoomResult::Full:           return "정원 초과";
		case ERoomResult::AlreadyStarted: return "이미 시작된 방";
		case ERoomResult::AlreadyHosting: return "이미 방을 갖고 있음";
		case ERoomResult::NotInRoom:      return "방에 있지 않음";
		case ERoomResult::NotHost:        return "방장만 가능";
		case ERoomResult::NotAllReady:    return "준비하지 않은 사람이 있음";
		case ERoomResult::NotStarted:     return "아직 시작되지 않은 방";
		default:                          return "잘못된 요청";
		}
	}

	const char* ChannelName(uint8_t Channel)
	{
		switch (static_cast<EChatChannel>(Channel))
		{
		case EChatChannel::All:     return "전체";
		case EChatChannel::Team:    return "팀";
		case EChatChannel::Dead:    return "사망";
		case EChatChannel::Whisper: return "귓속말";
		case EChatChannel::System:  return "시스템";
		default:                    return "?";
		}
	}

	// --- 친구 (v7) 표시용 이름 ---

	const char* FriendStateName(uint8_t State)
	{
		switch (static_cast<EFriendState>(State))
		{
		case EFriendState::Friend:          return "친구";
		case EFriendState::PendingOutgoing: return "신청함(대기)";
		case EFriendState::PendingIncoming: return "신청받음";
		default:                            return "?";
		}
	}

	const char* PresenceName(uint8_t Presence)
	{
		switch (static_cast<EPresence>(Presence))
		{
		case EPresence::Offline: return "오프라인";
		case EPresence::Online:  return "온라인";
		case EPresence::InGame:  return "게임중";
		default:                 return "?";
		}
	}

	const char* FriendResultName(uint8_t Result)
	{
		switch (static_cast<EFriendResult>(Result))
		{
		case EFriendResult::Success:        return "성공";
		case EFriendResult::NotAuthed:      return "로그인 안 됨";
		case EFriendResult::NotFound:       return "그런 닉네임 없음";
		case EFriendResult::AmbiguousName:  return "동명이인 여러 명";
		case EFriendResult::AlreadyFriend:  return "이미 친구";
		case EFriendResult::AlreadyPending: return "이미 신청함";
		case EFriendResult::SelfRequest:    return "자기 자신";
		case EFriendResult::LimitReached:   return "친구 수 상한";
		case EFriendResult::InvalidFormat:  return "형식 오류";
		case EFriendResult::DbError:        return "DB 오류";
		default:                            return "?";
		}
	}

	// 채팅 패킷 하나를 완성된 바이트 배열로 만든다.
	// 테스트 모드에서 이 바이트열을 마음대로 쪼개거나 붙여서 보낸다.
	std::vector<char> BuildChatPacket(EChatChannel Channel, const std::string& Text)
	{
		const uint16_t TextLen = static_cast<uint16_t>(
			Text.size() > kMaxTextLen ? kMaxTextLen : Text.size());

		ChatSendBody Body{};
		Body.TargetUserId = 0;
		Body.TextLen      = TextLen;
		Body.Channel      = static_cast<uint8_t>(Channel);

		PacketHeader Header{};
		Header.BodySize = static_cast<uint32_t>(sizeof(Body) + TextLen);
		Header.Opcode   = static_cast<uint16_t>(EOpcode::ChatSend);

		std::vector<char> Packet(sizeof(Header) + Header.BodySize);
		std::memcpy(Packet.data(), &Header, sizeof(Header));
		std::memcpy(Packet.data() + sizeof(Header), &Body, sizeof(Body));
		std::memcpy(Packet.data() + sizeof(Header) + sizeof(Body), Text.data(), TextLen);
		return Packet;
	}

	// ------------------------------------------------------------------
	// 수신 스레드. 서버와 동일한 프레이밍 로직을 쓴다.
	// ------------------------------------------------------------------
	void RecvThread()
	{
		std::vector<char> RecvBuf;
		char Temp[1024];
		PacketHeader Header{};
		std::vector<char> Body;

		while (GRunning)
		{
			const int Received = ::recv(GSock, Temp, sizeof(Temp), 0);

			if (Received == 0)
			{
				// 서버가 정상적으로 연결을 닫았다.
				std::printf("[연결 종료] 서버가 연결을 닫았습니다.\n");
				GRunning = false;
				return;
			}
			if (Received < 0)
			{
				// 타임아웃은 정상이다. 종료 요청이 없으면 계속 기다린다.
				if (IsRecvTimeout(LastNetError()) && GRunning)
				{
					continue;
				}
				std::printf("[연결 끊김] 수신 오류 %d\n", LastNetError());
				GRunning = false;
				return;
			}

			RecvBuf.insert(RecvBuf.end(), Temp, Temp + Received);

			for (;;)
			{
				const EFrameResult Result = TryExtractPacket(RecvBuf, Header, Body);
				if (Result == EFrameResult::NeedMore)
				{
					break;
				}
				if (Result == EFrameResult::Malformed)
				{
					std::printf("[오류] 서버가 비정상 패킷을 보냈습니다.\n");
					GRunning = false;
					return;
				}

				switch (static_cast<EOpcode>(Header.Opcode))
				{
				case EOpcode::LoginAck:
					if (Body.size() >= sizeof(LoginAckBody))
					{
						LoginAckBody Ack{};
						std::memcpy(&Ack, Body.data(), sizeof(Ack));

						if (Ack.bSuccess == 0)
						{
							if (static_cast<ELoginResult>(Ack.Result) == ELoginResult::VersionMismatch)
							{
								std::printf("[로그인 실패] 프로토콜 버전 불일치. 이쪽=%u, 서버=%u\n"
								            "             서버와 클라이언트를 같은 커밋으로 다시 빌드할 것.\n",
								            kProtocolVersion, Ack.ServerVersion);
							}
							else
							{
								std::printf("[로그인 실패] %s (코드 %u)\n",
								            LoginResultName(static_cast<ELoginResult>(Ack.Result)),
								            Ack.Result);
							}
							GRunning = false;
							return;
						}

						GMyUserId = Ack.UserId;
						std::printf("[로그인 성공] UserId=%llu, 이름=%s, 팀=%d\n",
						            static_cast<unsigned long long>(Ack.UserId),
						            ReadFixedString(Ack.Name, kMaxNameLen).c_str(),
						            Ack.TeamId);
					}
					break;

				case EOpcode::RegisterAck:
					if (Body.size() >= sizeof(RegisterAckBody))
					{
						RegisterAckBody Ack{};
						std::memcpy(&Ack, Body.data(), sizeof(Ack));
						if (Ack.bSuccess != 0)
						{
							std::printf("[가입 성공] 이어서 로그인한다.\n");
						}
						else
						{
							std::printf("[가입 실패] %s (코드 %u)\n",
							            LoginResultName(static_cast<ELoginResult>(Ack.Result)),
							            Ack.Result);
						}
					}
					break;

				case EOpcode::RoomCreateAck:
					if (Body.size() >= sizeof(RoomCreateAckBody))
					{
						RoomCreateAckBody Ack{};
						std::memcpy(&Ack, Body.data(), sizeof(Ack));
						if (Ack.bSuccess)
						{
							std::printf("[방 생성 성공] 방번호 #%u\n", Ack.RoomId);
						}
						else
						{
							std::printf("[방 생성 실패] %s\n",
							            RoomResultName(static_cast<ERoomResult>(Ack.Result)));
						}
					}
					break;

				case EOpcode::RoomListAck:
					if (Body.size() >= sizeof(RoomListAckBody))
					{
						RoomListAckBody Head{};
						std::memcpy(&Head, Body.data(), sizeof(Head));

						const size_t Expected = sizeof(Head) + sizeof(RoomInfo) * Head.Count;
						if (Body.size() < Expected)
						{
							std::printf("[방 목록] 크기가 맞지 않는다\n");
							break;
						}

						std::printf("[방 목록] %u개\n", Head.Count);
						for (uint16_t i = 0; i < Head.Count; ++i)
						{
							RoomInfo Info{};
							std::memcpy(&Info, Body.data() + sizeof(Head) + i * sizeof(RoomInfo),
							            sizeof(Info));
							std::printf("  #%u \"%s\" 방장=%s %u/%u %s\n",
							            Info.RoomId,
							            ReadFixedString(Info.Title, kMaxRoomTitleLen).c_str(),
							            ReadFixedString(Info.HostName, kMaxNameLen).c_str(),
							            Info.CurrentPlayers, Info.MaxPlayers,
							            Info.bHasPassword ? "[비번]" : "");
						}
					}
					break;

				case EOpcode::RoomJoinAck:
					if (Body.size() >= sizeof(RoomJoinAckBody))
					{
						RoomJoinAckBody Ack{};
						std::memcpy(&Ack, Body.data(), sizeof(Ack));
						if (Ack.bSuccess)
						{
							const uint8_t Count = Ack.CandidateCount <= kMaxHostCandidates
								? Ack.CandidateCount : static_cast<uint8_t>(kMaxHostCandidates);
							if (Count > 0)
							{
								const HostCandidate& Candidate = Ack.Candidates[0];
								std::printf("[방 참여 성공] #%u -> 후보 %s:%u (총 %u개)\n",
								            Ack.RoomId,
								            ReadFixedString(Candidate.Address, kMaxAddressLen).c_str(), Candidate.Port,
								            static_cast<unsigned>(Count));
							}
						}
						else
						{
							std::printf("[방 참여 실패] %s\n",
							            RoomResultName(static_cast<ERoomResult>(Ack.Result)));
						}
					}
					break;

				case EOpcode::RoomStart:
					if (Body.size() >= sizeof(RoomStartBody))
					{
						RoomStartBody Start{};
						std::memcpy(&Start, Body.data(), sizeof(Start));
						// v6 부터 이 신호는 "떠나라" 가 아니다. 호스트가 리슨서버를 다 열고
						// RoomHostReady 를 보내줄 때까지 기다린다.
						std::printf("[게임 시작] #%u — 호스트가 서버를 여는 중이다 (후보 %u개, relay host 경로 %u개)\n",
						            Start.RoomId, static_cast<unsigned>(Start.CandidateCount),
						            static_cast<unsigned>(Start.RelayRouteCount));
					}
					break;

				case EOpcode::RoomHostReady:
					if (Body.size() >= sizeof(RoomHostReadyBody))
					{
						RoomHostReadyBody Ready{};
						std::memcpy(&Ready, Body.data(), sizeof(Ready));
						const uint8_t Count = Ready.CandidateCount <= kMaxHostCandidates
							? Ready.CandidateCount : static_cast<uint8_t>(kMaxHostCandidates);
						if (Count > 0)
						{
							const HostCandidate& Candidate = Ready.Candidates[0];
							std::printf("[호스트 준비 완료] #%u -> 지금 %s:%u 로 접속하면 된다",
							            Ready.RoomId,
							            ReadFixedString(Candidate.Address, kMaxAddressLen).c_str(), Candidate.Port);
							if (Ready.Relay.GuestPort != 0)
							{
								std::printf(" (direct 실패시 relay %s:%u)",
								            ReadFixedString(Ready.Relay.Address, kMaxAddressLen).c_str(),
								            Ready.Relay.GuestPort);
							}
							std::printf("\n");
						}
					}
					break;

				case EOpcode::ChatBroadcast:
					if (Body.size() >= sizeof(ChatBroadcastBody))
					{
						ChatBroadcastBody Msg{};
						std::memcpy(&Msg, Body.data(), sizeof(Msg));
						const char* Text = Body.data() + sizeof(Msg);
						std::printf("[%s] %s: %.*s\n",
						            ChannelName(Msg.Channel),
						            ReadFixedString(Msg.SenderName, kMaxNameLen).c_str(),
						            static_cast<int>(Msg.TextLen), Text);
					}
					break;

				// --- 친구 (v7) ---
				case EOpcode::FriendListAck:
					if (Body.size() >= sizeof(FriendListAckBody))
					{
						FriendListAckBody Head{};
						std::memcpy(&Head, Body.data(), sizeof(Head));

						std::printf("[친구] %u명\n", Head.Count);

						const char* Cur = Body.data() + sizeof(Head);
						const size_t Avail = Body.size() - sizeof(Head);

						for (uint16_t i = 0; i < Head.Count; ++i)
						{
							if ((i + 1) * sizeof(FriendEntry) > Avail) { break; }

							FriendEntry E{};
							std::memcpy(&E, Cur + i * sizeof(FriendEntry), sizeof(E));

							std::printf("   %-16s id=%-4llu %-16s %-9s%s\n",
							            ReadFixedString(E.Nickname, kMaxNameLen).c_str(),
							            static_cast<unsigned long long>(E.UserId),
							            FriendStateName(E.State),
							            PresenceName(E.Presence),
							            E.UnreadCount ? "  [안읽음]" : "");
						}
					}
					break;

				case EOpcode::FriendAddAck:
					if (Body.size() >= sizeof(FriendAddAckBody))
					{
						FriendAddAckBody Ack{};
						std::memcpy(&Ack, Body.data(), sizeof(Ack));
						std::printf("[친구] 신청 %s (%s) target=%llu\n",
						            Ack.bSuccess ? "성공" : "실패",
						            FriendResultName(Ack.Result),
						            static_cast<unsigned long long>(Ack.TargetUserId));
					}
					break;

				case EOpcode::FriendRequestIncoming:
					if (Body.size() >= sizeof(FriendRequestIncomingBody))
					{
						FriendRequestIncomingBody Note{};
						std::memcpy(&Note, Body.data(), sizeof(Note));
						std::printf("[친구] ★ %s (id=%llu) 님이 친구 신청을 보냈습니다. "
						            "/accept %llu 또는 /decline %llu\n",
						            ReadFixedString(Note.FromNickname, kMaxNameLen).c_str(),
						            static_cast<unsigned long long>(Note.FromUserId),
						            static_cast<unsigned long long>(Note.FromUserId),
						            static_cast<unsigned long long>(Note.FromUserId));
					}
					break;

				case EOpcode::FriendUpdate:
					if (Body.size() >= sizeof(FriendUpdateBody))
					{
						FriendUpdateBody U{};
						std::memcpy(&U, Body.data(), sizeof(U));
						if (U.bRemoved)
						{
							std::printf("[친구] - %s (id=%llu) 목록에서 제거\n",
							            ReadFixedString(U.Nickname, kMaxNameLen).c_str(),
							            static_cast<unsigned long long>(U.UserId));
						}
						else
						{
							std::printf("[친구] + %s (id=%llu) %s / %s\n",
							            ReadFixedString(U.Nickname, kMaxNameLen).c_str(),
							            static_cast<unsigned long long>(U.UserId),
							            FriendStateName(U.State),
							            PresenceName(U.Presence));
						}
					}
					break;

				case EOpcode::DmHistoryAck:
					if (Body.size() >= sizeof(DmHistoryAckBody))
					{
						DmHistoryAckBody Ack{};
						std::memcpy(&Ack, Body.data(), sizeof(Ack));

						std::printf("[기록] peer=%llu %u개%s\n",
						            static_cast<unsigned long long>(Ack.PeerUserId),
						            Ack.Count, Ack.bHasMore ? " (더 있음)" : "");

						// ★ DmEntry 는 가변 길이다. 인덱싱이 아니라 순회로 읽는다.
						size_t Off = sizeof(Ack);
						for (uint16_t i = 0; i < Ack.Count; ++i)
						{
							if (Off + sizeof(DmEntry) > Body.size()) { break; }

							DmEntry E{};
							std::memcpy(&E, Body.data() + Off, sizeof(E));
							Off += sizeof(E);

							if (Off + E.TextLen > Body.size()) { break; }

							std::printf("   #%llu from=%llu: %.*s\n",
							            static_cast<unsigned long long>(E.MessageId),
							            static_cast<unsigned long long>(E.FromUserId),
							            static_cast<int>(E.TextLen), Body.data() + Off);
							Off += E.TextLen;
						}
					}
					break;

				case EOpcode::DirectMessage:
					if (Body.size() >= sizeof(DirectMessageBody))
					{
						DirectMessageBody M{};
						std::memcpy(&M, Body.data(), sizeof(M));
						const char* Text = Body.data() + sizeof(M);

						// 본문 길이를 실제 도착량으로 자른다. 서버를 믿더라도
						// 여기서 넘치면 남의 메모리를 읽는다.
						const size_t Avail = Body.size() - sizeof(M);
						const size_t Len = (M.TextLen <= Avail) ? M.TextLen : Avail;

						std::printf("[DM] #%llu %llu -> %llu: %.*s\n",
						            static_cast<unsigned long long>(M.MessageId),
						            static_cast<unsigned long long>(M.FromUserId),
						            static_cast<unsigned long long>(M.ToUserId),
						            static_cast<int>(Len), Text);
					}
					break;

				case EOpcode::FriendPresence:
					if (Body.size() >= sizeof(FriendPresenceBody))
					{
						FriendPresenceBody P{};
						std::memcpy(&P, Body.data(), sizeof(P));
						std::printf("[상태] id=%llu -> %s\n",
						            static_cast<unsigned long long>(P.UserId),
						            PresenceName(P.Presence));
					}
					break;

				default:
					std::printf("[수신] 오피코드 %u (%zu바이트)\n",
					            Header.Opcode, Body.size());
					break;
				}
			}
		}
	}

	bool DoLogin(const std::string& LoginId, const std::string& Password, int32_t TeamId)
	{
		LoginReqBody Req{};
		Req.Version = kProtocolVersion;   // 서버가 이 값을 검사한다
		CopyFixedString(Req.LoginId,  kMaxLoginIdLen,  LoginId);
		CopyFixedString(Req.Password, kMaxPasswordLen, Password);
		Req.TeamId = TeamId;
		return SendPacket(GSock, EOpcode::LoginReq, &Req, sizeof(Req));
	}

	bool DoRoomCreate(const std::string& Title, const std::string& Password, uint16_t HostPort)
	{
		RoomCreateReqBody Req{};
		CopyFixedString(Req.Title, kMaxRoomTitleLen, Title);
		Req.HostPort     = HostPort;
		Req.MaxPlayers   = static_cast<uint8_t>(kMaxPlayersInRoom);
		Req.bHasPassword = (Password.size() == kRoomPasswordLen) ? 1 : 0;
		if (Req.bHasPassword)
		{
			// 널 종료가 없는 고정 4바이트라 memcpy 로 넣는다.
			std::memcpy(Req.Password, Password.data(), kRoomPasswordLen);
		}
		return SendPacket(GSock, EOpcode::RoomCreateReq, &Req, sizeof(Req));
	}

	bool DoRoomList()
	{
		return SendPacket(GSock, EOpcode::RoomListReq, nullptr, 0);
	}

	bool DoRoomJoin(uint32_t RoomId, const std::string& Password)
	{
		RoomJoinReqBody Req{};
		Req.RoomId = RoomId;
		if (Password.size() == kRoomPasswordLen)
		{
			std::memcpy(Req.Password, Password.data(), kRoomPasswordLen);
		}
		return SendPacket(GSock, EOpcode::RoomJoinReq, &Req, sizeof(Req));
	}

	bool DoRoomStateUpdate(uint32_t RoomId, uint8_t CurrentPlayers, ERoomState State)
	{
		RoomStateUpdateBody Req{};
		Req.RoomId         = RoomId;
		Req.CurrentPlayers = CurrentPlayers;
		Req.State          = static_cast<uint8_t>(State);
		return SendPacket(GSock, EOpcode::RoomStateUpdate, &Req, sizeof(Req));
	}

	bool DoRegister(const std::string& LoginId, const std::string& Password,
	                const std::string& Nickname)
	{
		RegisterReqBody Req{};
		Req.Version = kProtocolVersion;
		CopyFixedString(Req.LoginId,  kMaxLoginIdLen,  LoginId);
		CopyFixedString(Req.Password, kMaxPasswordLen, Password);
		CopyFixedString(Req.Nickname, kMaxNameLen,     Nickname);
		return SendPacket(GSock, EOpcode::RegisterReq, &Req, sizeof(Req));
	}

	void SetDead(bool bDead)
	{
		SetDeadBody Body{};
		Body.UserId = GMyUserId;
		Body.bDead  = bDead ? 1 : 0;
		SendPacket(GSock, EOpcode::SetDead, &Body, sizeof(Body));
	}

	// ==================================================================
	// 테스트 1: 분할
	// 패킷 하나를 1바이트씩 나눠 보낸다.
	// 서버가 조각을 모아 하나의 메시지로 복원하면 통과.
	// ==================================================================
	void RunSplitTest()
	{
		std::printf("\n=== [분할 테스트] 패킷 1개를 1바이트씩 전송 ===\n");
		const std::vector<char> Packet =
			BuildChatPacket(EChatChannel::All, "분할전송테스트");

		std::printf("총 %zu바이트를 1바이트씩 보냅니다...\n", Packet.size());
		for (size_t i = 0; i < Packet.size(); ++i)
		{
			if (!SendAll(GSock, Packet.data() + i, 1))
			{
				std::printf("전송 실패 (%zu번째 바이트)\n", i);
				return;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}

		std::printf("전송 완료.\n");
		std::printf("기대 결과: 서버 콘솔에 '분할전송테스트' 가 정확히 1번 출력\n");
	}

	// ==================================================================
	// 테스트 2: 합침
	// 패킷 3개를 하나의 버퍼로 합쳐 한 번의 send 로 보낸다.
	// 서버가 3개로 분리해 처리하면 통과.
	// ==================================================================
	void RunMergeTest()
	{
		std::printf("\n=== [합침 테스트] 패킷 3개를 한 번에 전송 ===\n");

		std::vector<char> Combined;
		for (int i = 1; i <= 3; ++i)
		{
			const std::vector<char> Packet =
				BuildChatPacket(EChatChannel::All, "합침테스트" + std::to_string(i));
			Combined.insert(Combined.end(), Packet.begin(), Packet.end());
		}

		std::printf("총 %zu바이트를 한 번의 send 로 보냅니다...\n", Combined.size());
		if (!SendAll(GSock, Combined.data(), static_cast<int32_t>(Combined.size())))
		{
			std::printf("전송 실패\n");
			return;
		}

		std::printf("전송 완료.\n");
		std::printf("기대 결과: 서버 콘솔에 합침테스트1/2/3 이 각각 출력 (총 3줄)\n");
	}

	// ==================================================================
	// 테스트 3: 방어
	// BodySize 를 999999 로 위조해 보낸다.
	// 서버가 연결만 끊고 죽지 않으면 통과.
	// ==================================================================
	void RunBadTest()
	{
		std::printf("\n=== [방어 테스트] BodySize=999999 위조 패킷 전송 ===\n");

		PacketHeader Header{};
		Header.BodySize = 999999;
		Header.Opcode   = static_cast<uint16_t>(EOpcode::ChatSend);

		SendAll(GSock, reinterpret_cast<const char*>(&Header), sizeof(Header));
		std::printf("전송 완료.\n");
		std::printf("기대 결과: 서버가 '[차단]' 출력 후 이 연결만 끊음.\n");
		std::printf("          서버 프로세스는 계속 살아있어야 함.\n");
	}

	// 파일이나 파이프로 입력을 넣으면 맨 앞에 UTF-8 BOM 이 붙고,
	// 윈도우 줄바꿈(CRLF)이면 끝에 \r 이 남는다.
	// 그대로 두면 "/dead" 가 명령으로 인식되지 않고 채팅 메시지로 나간다.
	void SanitizeLine(std::string& Line)
	{
		if (Line.size() >= 3 &&
		    static_cast<unsigned char>(Line[0]) == 0xEF &&
		    static_cast<unsigned char>(Line[1]) == 0xBB &&
		    static_cast<unsigned char>(Line[2]) == 0xBF)
		{
			Line.erase(0, 3);
		}
		while (!Line.empty() && (Line.back() == '\r' || Line.back() == '\n'))
		{
			Line.pop_back();
		}
	}

	void RunInteractive()
	{
		std::printf("\n=== 대화형 모드 ===\n");
		std::printf("  /all      전체 채널로 전환\n");
		std::printf("  /team     팀 채널로 전환\n");
		std::printf("  /deadchan 사망 채널로 전환\n");
		std::printf("  /dead     내 상태를 사망으로\n");
		std::printf("  /alive    내 상태를 생존으로\n");
		std::printf("  --- 로비 ---\n");
		std::printf("  /host <방제목> [비번4자리]   방 만들기 (리슨서버 포트는 7777 로 가정)\n");
		std::printf("  /rooms                       대기 중인 방 목록\n");
		std::printf("  /join <방번호> [비번4자리]   방 참여 (성공하면 호스트 주소를 받는다)\n");
		std::printf("  /start <방번호> <인원>       게임 시작 상태로 바꾼다 (RoomStateUpdate)\n");
		std::printf("  /ready | /unready            준비 상태 토글 (참여자용)\n");
		std::printf("  /go                          게임 시작 요청 (방장용, 전원 준비 필요)\n");
		std::printf("  /hostready                   리슨서버를 열었다고 신고 (방장용, /go 다음)\n");
		std::printf("  /close                       내 방 닫기\n");
		std::printf("  --- 친구 (v7) ---\n");
		std::printf("  /friends                     친구 목록 (신청 대기 포함)\n");
		std::printf("  /add <닉네임>                친구 신청\n");
		std::printf("  /accept <userId>             받은 신청 수락\n");
		std::printf("  /decline <userId>            받은 신청 거절\n");
		std::printf("  /unfriend <userId>           친구 삭제 / 보낸 신청 취소\n");
		std::printf("  /dm <userId> <본문>          1:1 메시지 (친구만)\n");
		std::printf("  /hist <userId> [before]      대화 기록 (before 생략=최신+읽음처리)\n");
		std::printf("  /q        종료\n\n");

		EChatChannel Current = EChatChannel::All;
		std::string Line;

		while (GRunning && std::getline(std::cin, Line))
		{
			SanitizeLine(Line);

			if (Line.empty())          { continue; }
			if (Line == "/q")          { break; }
			if (Line == "/all")        { Current = EChatChannel::All;  std::printf("-> 전체 채널\n"); continue; }
			if (Line == "/team")       { Current = EChatChannel::Team; std::printf("-> 팀 채널\n");   continue; }
			if (Line == "/deadchan")   { Current = EChatChannel::Dead; std::printf("-> 사망 채널\n"); continue; }
			if (Line == "/dead")       { SetDead(true);  continue; }
			if (Line == "/alive")      { SetDead(false); continue; }

			// --- 로비 명령 ---
			if (Line.rfind("/host ", 0) == 0)
			{
				// /host <방제목> [비번4자리]
				std::string Rest = Line.substr(6);
				std::string Title = Rest, Pw;
				const size_t Space = Rest.rfind(' ');
				if (Space != std::string::npos && Rest.size() - Space - 1 == kRoomPasswordLen)
				{
					Title = Rest.substr(0, Space);
					Pw    = Rest.substr(Space + 1);
				}
				DoRoomCreate(Title, Pw, 7777);
				continue;
			}
			if (Line == "/rooms")      { DoRoomList(); continue; }
			if (Line.rfind("/join ", 0) == 0)
			{
				// /join <방번호> [비번4자리]
				std::string Rest = Line.substr(6);
				const size_t Space = Rest.find(' ');
				const uint32_t RoomId = static_cast<uint32_t>(std::atoi(Rest.substr(0, Space).c_str()));
				const std::string Pw = (Space == std::string::npos) ? "" : Rest.substr(Space + 1);
				DoRoomJoin(RoomId, Pw);
				continue;
			}
			if (Line.rfind("/start", 0) == 0)
			{
				// /start <방번호> <인원수> -> 게임 시작 상태로 바꾼다
				std::string Rest = Line.size() > 7 ? Line.substr(7) : "";
				const size_t Space = Rest.find(' ');
				const uint32_t RoomId = static_cast<uint32_t>(std::atoi(Rest.substr(0, Space).c_str()));
				const uint8_t Players = (Space == std::string::npos)
					? 1 : static_cast<uint8_t>(std::atoi(Rest.substr(Space + 1).c_str()));
				DoRoomStateUpdate(RoomId, Players, ERoomState::InGame);
				continue;
			}
			if (Line == "/ready" || Line == "/unready")
			{
				RoomReadyReqBody Req{};
				Req.bReady = (Line == "/ready") ? 1 : 0;
				SendPacket(GSock, EOpcode::RoomReadyReq, &Req, sizeof(Req));
				continue;
			}
			if (Line == "/go")         { SendPacket(GSock, EOpcode::RoomStartReq, nullptr, 0); continue; }
			if (Line == "/hostready")
			{
				// 언리얼 클라이언트에서는 리슨서버가 실제로 열린 것을 감지해 자동으로 보낸다.
				// 콘솔에서는 그 시점을 흉내 낼 수 없으므로 손으로 친다.
				SendPacket(GSock, EOpcode::RoomHostReadyReq, nullptr, 0);
				continue;
			}
			if (Line == "/close")      { SendPacket(GSock, EOpcode::RoomLeaveReq, nullptr, 0); continue; }

			// --- 친구 (v7) ---
			if (Line == "/friends")
			{
				SendPacket(GSock, EOpcode::FriendListReq, nullptr, 0);
				continue;
			}
			if (Line.rfind("/add ", 0) == 0)
			{
				FriendAddReqBody Req{};
				// 닉네임을 그대로 넣는다. 파싱(나중의 '#태그')은 서버가 한다.
				CopyFixedString(Req.Query, kMaxFriendQueryLen, Line.substr(5));
				SendPacket(GSock, EOpcode::FriendAddReq, &Req, sizeof(Req));
				continue;
			}
			if (Line.rfind("/accept ", 0) == 0 || Line.rfind("/decline ", 0) == 0)
			{
				const bool bAccept = (Line[1] == 'a');
				FriendRespondReqBody Req{};
				Req.FromUserId = std::strtoull(Line.c_str() + (bAccept ? 8 : 9), nullptr, 10);
				Req.bAccept    = bAccept ? 1 : 0;
				SendPacket(GSock, EOpcode::FriendRespondReq, &Req, sizeof(Req));
				continue;
			}
			if (Line.rfind("/unfriend ", 0) == 0)
			{
				FriendRemoveReqBody Req{};
				Req.TargetUserId = std::strtoull(Line.c_str() + 10, nullptr, 10);
				SendPacket(GSock, EOpcode::FriendRemoveReq, &Req, sizeof(Req));
				continue;
			}
			if (Line.rfind("/hist ", 0) == 0)
			{
				// /hist <userId> [beforeMessageId]
				DmHistoryReqBody Req{};
				const char* P = Line.c_str() + 6;
				char* End = nullptr;
				Req.PeerUserId      = std::strtoull(P, &End, 10);
				Req.BeforeMessageId = (End && *End) ? std::strtoull(End, nullptr, 10) : 0;
				SendPacket(GSock, EOpcode::DmHistoryReq, &Req, sizeof(Req));
				continue;
			}
			if (Line.rfind("/dm ", 0) == 0)
			{
				// /dm <userId> <본문>
				const size_t IdStart = 4;
				const size_t Space   = Line.find(' ', IdStart);
				if (Space == std::string::npos)
				{
					std::printf("사용법: /dm <userId> <본문>\n");
					continue;
				}

				const std::string TextPart = Line.substr(Space + 1);
				if (TextPart.empty() || TextPart.size() > kMaxTextLen)
				{
					std::printf("본문이 비었거나 너무 길다\n");
					continue;
				}

				DirectMessageSendBody Head{};
				Head.TargetUserId = std::strtoull(Line.c_str() + IdStart, nullptr, 10);
				Head.TextLen      = static_cast<uint16_t>(TextPart.size());

				SendPacket2(GSock, EOpcode::DirectMessageSend,
				            &Head, sizeof(Head),
				            TextPart.data(), static_cast<uint32_t>(TextPart.size()));
				continue;
			}

			const std::vector<char> Packet = BuildChatPacket(Current, Line);
			if (!SendAll(GSock, Packet.data(), static_cast<int32_t>(Packet.size())))
			{
				std::printf("[전송 실패]\n");
				break;
			}
		}
	}
}

int main(int argc, char** argv)
{
#ifdef _WIN32
	::SetConsoleOutputCP(CP_UTF8);
	::SetConsoleCP(CP_UTF8);
#endif

	if (argc < 5)
	{
		std::printf("사용법: %s <ip> <port> <아이디> <비밀번호> [split|merge|bad] [--register 닉네임]\n",
		            argv[0]);
		std::printf("  --register 를 주면 로그인 전에 계정을 먼저 만든다.\n");
		return 1;
	}

	const std::string Mode = (argc >= 6 && argv[5][0] != '-') ? argv[5] : "";

	// --register <닉네임> 을 찾는다. 위치는 5번 인자 뒤 아무데나 허용한다.
	std::string RegisterNick;
	bool bWantRegister = false;
	for (int i = 5; i < argc - 1; ++i)
	{
		if (std::strcmp(argv[i], "--register") == 0)
		{
			bWantRegister = true;
			RegisterNick  = argv[i + 1];
			break;
		}
	}

	if (!NetInit())
	{
		std::printf("NetInit() 실패\n");
		return 1;
	}

	GSock = ::socket(PF_INET, SOCK_STREAM, 0);
	if (GSock == kInvalidSocket)
	{
		std::printf("socket() 실패: %d\n", LastNetError());
		return 1;
	}

	sockaddr_in ServerAddr{};
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port   = htons(static_cast<uint16_t>(std::atoi(argv[2])));
	if (::inet_pton(AF_INET, argv[1], &ServerAddr.sin_addr) != 1)
	{
		std::printf("잘못된 IP: %s\n", argv[1]);
		return 1;
	}

	if (::connect(GSock, reinterpret_cast<sockaddr*>(&ServerAddr), sizeof(ServerAddr)) != 0)
	{
		std::printf("connect() 실패: %d\n", LastNetError());
		return 1;
	}
	std::printf("서버에 연결되었습니다.\n");

	// 수신 스레드가 영원히 블록되지 않도록 타임아웃을 건다.
	SetRecvTimeout(GSock, 1000);

	std::thread Receiver(RecvThread);

	// 팀 ID 는 아이디 길이의 홀짝으로 대충 나눈다. 팀 채널 테스트용.
	const int32_t TeamId = static_cast<int32_t>(std::strlen(argv[3]) % 2);

	if (bWantRegister)
	{
		DoRegister(argv[3], argv[4], RegisterNick);
		// RegisterAck 를 받고 나서 로그인해야 한다.
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
	}

	DoLogin(argv[3], argv[4], TeamId);

	// LoginAck 를 받을 시간을 준다.
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	if      (Mode == "split") { RunSplitTest(); std::this_thread::sleep_for(std::chrono::seconds(1)); }
	else if (Mode == "merge") { RunMergeTest(); std::this_thread::sleep_for(std::chrono::seconds(1)); }
	else if (Mode == "bad")   { RunBadTest();   std::this_thread::sleep_for(std::chrono::seconds(2)); }
	else                      { RunInteractive(); }

	// --- 종료 절차 ---
	// 1) FIN 을 보내 아직 큐에 남은 송신 데이터를 확실히 내보낸다.
	//    여기서 곧바로 CloseSocket 을 부르면 마지막으로 보낸 메시지가 유실된다.
	//    (recv 에 블록된 다른 스레드가 있는 소켓을 닫으면 송신 버퍼가 버려진다)
	ShutdownSend(GSock);

	// 2) 서버가 이쪽 FIN 을 보고 연결을 닫으면 수신 스레드가 recv==0 을 보고 끝난다.
	if (Receiver.joinable())
	{
		Receiver.join();
	}

	// 3) 양방향이 모두 정리된 뒤에 닫는다.
	GRunning = false;
	CloseSocket(GSock);
	GSock = kInvalidSocket;

	NetShutdown();
	return 0;
}
