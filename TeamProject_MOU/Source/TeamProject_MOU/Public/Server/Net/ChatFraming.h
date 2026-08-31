// MOU 채팅 - 길이 프리픽스 프레이밍의 언리얼(TArray) 버전.
//
// [왜 서버 코드를 그대로 안 쓰고 다시 짜는가]
//   서버의 MOU_Server/Shared/Framing.cpp 와 로직이 완전히 동일하다.
//   다만 서버는 std::vector<char> 를, 언리얼은 TArray<uint8> 을 쓴다.
//   Framing.h 를 그대로 include 하면 같은 폴더의 Net.h 가 딸려 들어오고,
//   Net.h 는 winsock2.h / windows.h 를 포함하기 때문에 언리얼 매크로와 충돌한다.
//   그래서 "선언은 공유(ChatProtocol.h), 구현은 각자" 로 나눈 것이다.
//
//   >> 서버의 Framing.cpp 를 고치면 이 파일도 같이 고쳐야 한다. <<
//
// [TCP 프레이밍이 왜 필요한지]
//   TCP 는 바이트 스트림이라 서버가 send() 한 횟수와 클라가 Recv() 하는 횟수가 다르다.
//   패킷 3개가 한 번에 뭉쳐 오기도 하고(합침), 1개가 여러 번에 쪼개져 오기도 한다(분할).
//   그래서 PacketHeader.BodySize 를 보고 직접 경계를 잘라야 한다.
//   이 로직은 서버 쪽에서 TestClient 의 split / merge / bad 3종 테스트로 검증된 것과 동일하다.

#pragma once

#include "CoreMinimal.h"
#include "Server/Chat/ChatTypes.h"
#include "Server/Lobby/LobbyTypes.h"   // 아래 static_assert 가 EMOURoomResultBP 를 쓴다
#include "Server/Social/FriendTypes.h" // 아래 static_assert 가 EMOUFriendStateBP 등을 쓴다

// 서버와 공유하는 프로토콜 정의.
// 실제 파일 위치: <저장소 루트>/MOU_Server/Shared/ChatProtocol.h
// TeamProject_MOU.Build.cs 의 PublicIncludePaths 에 그 폴더가 등록되어 있어야 컴파일된다.
//
// 같은 폴더에 있는 Net.h / Framing.h 는 절대 include 하지 말 것 (winsock2.h 충돌).
// 언리얼에서 include 해도 되는 것은 ChatProtocol.h 하나뿐이다.
THIRD_PARTY_INCLUDES_START
#include "ChatProtocol.h"
THIRD_PARTY_INCLUDES_END

namespace MOUChat
{
	// 블루프린트용 미러 enum 이 서버 프로토콜과 어긋나지 않는지 컴파일 타임에 검사한다.
	// 누군가 ChatProtocol.h 의 채널 번호만 바꾸고 ChatTypes.h 를 안 고치면 여기서 빌드가 깨진다.
	// 런타임에 "팀 채팅이 사망 채널로 가는" 식의 조용한 버그로 번지는 것보다 낫다.
	static_assert(static_cast<uint8>(EChatChannelBP::All)     == static_cast<uint8>(MOU::EChatChannel::All),     "EChatChannelBP::All 이 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EChatChannelBP::Team)    == static_cast<uint8>(MOU::EChatChannel::Team),    "EChatChannelBP::Team 이 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EChatChannelBP::Dead)    == static_cast<uint8>(MOU::EChatChannel::Dead),    "EChatChannelBP::Dead 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EChatChannelBP::Whisper) == static_cast<uint8>(MOU::EChatChannel::Whisper), "EChatChannelBP::Whisper 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EChatChannelBP::System)  == static_cast<uint8>(MOU::EChatChannel::System),  "EChatChannelBP::System 이 서버 정의와 다르다");

	// 로그인 거부 사유도 같은 이유로 맞춰둔다.
	static_assert(static_cast<uint8>(EChatLoginResultBP::Success)         == static_cast<uint8>(MOU::ELoginResult::Success),         "EChatLoginResultBP::Success 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EChatLoginResultBP::VersionMismatch) == static_cast<uint8>(MOU::ELoginResult::VersionMismatch), "EChatLoginResultBP::VersionMismatch 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EChatLoginResultBP::InvalidRequest)  == static_cast<uint8>(MOU::ELoginResult::InvalidRequest),  "EChatLoginResultBP::InvalidRequest 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EChatLoginResultBP::AccountNotFound) == static_cast<uint8>(MOU::ELoginResult::AccountNotFound), "EChatLoginResultBP::AccountNotFound 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EChatLoginResultBP::WrongPassword)   == static_cast<uint8>(MOU::ELoginResult::WrongPassword),   "EChatLoginResultBP::WrongPassword 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EChatLoginResultBP::DuplicateId)     == static_cast<uint8>(MOU::ELoginResult::DuplicateId),     "EChatLoginResultBP::DuplicateId 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EChatLoginResultBP::InvalidFormat)   == static_cast<uint8>(MOU::ELoginResult::InvalidFormat),   "EChatLoginResultBP::InvalidFormat 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EChatLoginResultBP::ServerError)     == static_cast<uint8>(MOU::ELoginResult::ServerError),     "EChatLoginResultBP::ServerError 가 서버 정의와 다르다");

	// 로비 결과 코드도 같은 이유로 맞춰둔다.
	static_assert(static_cast<uint8>(EMOURoomResultBP::Success)        == static_cast<uint8>(MOU::ERoomResult::Success),        "EMOURoomResultBP::Success 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOURoomResultBP::NotAuthed)      == static_cast<uint8>(MOU::ERoomResult::NotAuthed),      "EMOURoomResultBP::NotAuthed 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOURoomResultBP::NotFound)       == static_cast<uint8>(MOU::ERoomResult::NotFound),       "EMOURoomResultBP::NotFound 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOURoomResultBP::WrongPassword)  == static_cast<uint8>(MOU::ERoomResult::WrongPassword),  "EMOURoomResultBP::WrongPassword 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOURoomResultBP::Full)           == static_cast<uint8>(MOU::ERoomResult::Full),           "EMOURoomResultBP::Full 이 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOURoomResultBP::AlreadyStarted) == static_cast<uint8>(MOU::ERoomResult::AlreadyStarted), "EMOURoomResultBP::AlreadyStarted 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOURoomResultBP::AlreadyHosting) == static_cast<uint8>(MOU::ERoomResult::AlreadyHosting), "EMOURoomResultBP::AlreadyHosting 이 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOURoomResultBP::InvalidRequest) == static_cast<uint8>(MOU::ERoomResult::InvalidRequest), "EMOURoomResultBP::InvalidRequest 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOURoomResultBP::NotInRoom)      == static_cast<uint8>(MOU::ERoomResult::NotInRoom),      "EMOURoomResultBP::NotInRoom 이 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOURoomResultBP::NotHost)        == static_cast<uint8>(MOU::ERoomResult::NotHost),        "EMOURoomResultBP::NotHost 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOURoomResultBP::NotAllReady)    == static_cast<uint8>(MOU::ERoomResult::NotAllReady),    "EMOURoomResultBP::NotAllReady 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOURoomResultBP::NotStarted)     == static_cast<uint8>(MOU::ERoomResult::NotStarted),     "EMOURoomResultBP::NotStarted 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOURoomStateBP::Waiting)         == static_cast<uint8>(MOU::ERoomState::Waiting),         "EMOURoomStateBP::Waiting 이 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOURoomStateBP::InGame)          == static_cast<uint8>(MOU::ERoomState::InGame),          "EMOURoomStateBP::InGame 이 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOURoomCloseReasonBP::HostLeft)  == static_cast<uint8>(MOU::ERoomCloseReason::HostLeft),  "EMOURoomCloseReasonBP::HostLeft 가 서버 정의와 다르다");

	// --- 친구 / 메신저 (v7) ---
	// 서버 enum 을 고치고 여기를 안 고치면 **값이 조용히 어긋나** 친구 상태가
	// 엉뚱하게 표시된다. 컴파일 타임에 잡는 것이 이 블록의 목적이다.
	static_assert(static_cast<uint8>(EMOUFriendStateBP::Friend)          == static_cast<uint8>(MOU::EFriendState::Friend),          "EMOUFriendStateBP::Friend 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOUFriendStateBP::PendingOutgoing) == static_cast<uint8>(MOU::EFriendState::PendingOutgoing), "EMOUFriendStateBP::PendingOutgoing 이 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOUFriendStateBP::PendingIncoming) == static_cast<uint8>(MOU::EFriendState::PendingIncoming), "EMOUFriendStateBP::PendingIncoming 이 서버 정의와 다르다");

	static_assert(static_cast<uint8>(EMOUPresenceBP::Offline) == static_cast<uint8>(MOU::EPresence::Offline), "EMOUPresenceBP::Offline 이 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOUPresenceBP::Online)  == static_cast<uint8>(MOU::EPresence::Online),  "EMOUPresenceBP::Online 이 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOUPresenceBP::InGame)  == static_cast<uint8>(MOU::EPresence::InGame),  "EMOUPresenceBP::InGame 이 서버 정의와 다르다");

	static_assert(static_cast<uint8>(EMOUFriendResultBP::Success)        == static_cast<uint8>(MOU::EFriendResult::Success),        "EMOUFriendResultBP::Success 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOUFriendResultBP::NotAuthed)      == static_cast<uint8>(MOU::EFriendResult::NotAuthed),      "EMOUFriendResultBP::NotAuthed 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOUFriendResultBP::NotFound)       == static_cast<uint8>(MOU::EFriendResult::NotFound),       "EMOUFriendResultBP::NotFound 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOUFriendResultBP::AmbiguousName)  == static_cast<uint8>(MOU::EFriendResult::AmbiguousName),  "EMOUFriendResultBP::AmbiguousName 이 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOUFriendResultBP::AlreadyFriend)  == static_cast<uint8>(MOU::EFriendResult::AlreadyFriend),  "EMOUFriendResultBP::AlreadyFriend 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOUFriendResultBP::AlreadyPending) == static_cast<uint8>(MOU::EFriendResult::AlreadyPending), "EMOUFriendResultBP::AlreadyPending 이 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOUFriendResultBP::SelfRequest)    == static_cast<uint8>(MOU::EFriendResult::SelfRequest),    "EMOUFriendResultBP::SelfRequest 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOUFriendResultBP::LimitReached)   == static_cast<uint8>(MOU::EFriendResult::LimitReached),   "EMOUFriendResultBP::LimitReached 가 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOUFriendResultBP::InvalidFormat)  == static_cast<uint8>(MOU::EFriendResult::InvalidFormat),  "EMOUFriendResultBP::InvalidFormat 이 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOUFriendResultBP::DbError)        == static_cast<uint8>(MOU::EFriendResult::DbError),        "EMOUFriendResultBP::DbError 가 서버 정의와 다르다");

	// 호스트 주소 후보의 성격 (v8). 값이 어긋나면 LAN 후보를 공인으로 착각한다.
	static_assert(static_cast<uint8>(EMOUHostAddrKindBP::Public) == static_cast<uint8>(MOU::EHostAddrKind::Public), "EMOUHostAddrKindBP::Public 이 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOUHostAddrKindBP::Lan)    == static_cast<uint8>(MOU::EHostAddrKind::Lan),    "EMOUHostAddrKindBP::Lan 이 서버 정의와 다르다");
	static_assert(static_cast<uint8>(EMOUHostAddrKindBP::Punch)  == static_cast<uint8>(MOU::EHostAddrKind::Punch),  "EMOUHostAddrKindBP::Punch 가 서버 정의와 다르다");

	/** TryExtractPacket 의 결과. 서버 MOU::EFrameResult 와 같은 의미다. */
	enum class EFrameResult : uint8
	{
		/** 완성된 패킷 하나를 꺼냈다. 버퍼에 더 남아있을 수 있으니 다시 호출해야 한다. */
		Ok,
		/** 아직 덜 왔다. 다음 Recv 를 기다린다. 버퍼는 건드리지 않았다. */
		NeedMore,
		/** BodySize 가 허용치를 넘었다. 프레이밍이 깨졌으므로 연결을 끊어야 한다. */
		Malformed
	};

	/**
	 * Buffer 앞쪽에서 완성된 패킷 하나를 꺼내고, 꺼낸 만큼 앞부분을 지운다.
	 *
	 * 호출자는 Ok 가 나오는 동안 반복 호출해야 한다.
	 * 한 번의 Recv 에 패킷이 여러 개 붙어 왔을 수 있기 때문이다.
	 *
	 * @param Buffer     수신 누적 버퍼. 이 함수가 앞부분을 소비한다 (in/out)
	 * @param OutHeader  꺼낸 패킷의 헤더
	 * @param OutBody    꺼낸 패킷의 바디 (헤더 제외)
	 */
	EFrameResult TryExtractPacket(TArray<uint8>& Buffer, MOU::PacketHeader& OutHeader, TArray<uint8>& OutBody);

	/**
	 * 헤더 + 바디를 하나의 바이트 배열로 조립한다. 실제 전송은 하지 않는다.
	 *
	 * 조립과 전송을 분리한 이유: 패킷은 게임 스레드에서 만들고,
	 * 전송은 워커 스레드가 한다 (UServerSubsystem -> TQueue -> FServerClientRunnable).
	 *
	 * 바디를 두 조각(BodyA/BodyB)으로 받는 이유는 "고정 헤더부 + 가변 길이 텍스트" 형태의
	 * 패킷(ChatSend, ChatBroadcast)이 있기 때문이다. 서버의 SendPacket2 와 같은 구조다.
	 *
	 * @return 전체 바디 크기가 kMaxBodySize 를 넘으면 false. 이때 OutBytes 는 비어있다.
	 */
	bool BuildPacket(TArray<uint8>& OutBytes, MOU::EOpcode Opcode,
	                 const void* BodyA, uint32 SizeA,
	                 const void* BodyB = nullptr, uint32 SizeB = 0);

	/**
	 * FString 을 UTF-8 바이트로 인코딩하되 MaxBytes 를 넘지 않게 자른다.
	 *
	 * 단순히 MaxBytes 에서 싹둑 자르면 한글처럼 여러 바이트를 쓰는 문자의 한가운데가 잘려
	 * 깨진 문자가 남는다. 이 함수는 문자 경계까지 되돌려서 자른다.
	 *
	 * @return 실제 인코딩된 바이트 수
	 */
	int32 EncodeUtf8Clamped(const FString& Src, int32 MaxBytes, TArray<uint8>& OutBytes);

	/** FString 을 UTF-8 로 인코딩했을 때의 바이트 수. 글자 수가 아니다 (한글 1자 = 3바이트). */
	int32 GetUtf8Length(const FString& Src);

	/**
	 * 고정 길이 char 배열(LoginReqBody::Name 등)에 문자열을 UTF-8 로 담는다.
	 * 남는 공간은 0 으로 채우고, 항상 널 종료를 보장한다.
	 * 서버의 CopyFixedString 과 짝을 이룬다.
	 */
	void CopyFixedString(char* Dest, int32 DestSize, const FString& Src);

	/**
	 * 고정 길이 char 배열에서 문자열을 읽는다.
	 * 서버가 널 종료를 빼먹었을 경우를 대비해 MaxSize 로 제한한다.
	 * 서버의 ReadFixedString 과 짝을 이룬다.
	 */
	FString ReadFixedString(const char* Src, int32 MaxSize);

	/**
	 * 패킷의 HostCandidate 배열을 BP 구조체 배열로 옮긴다. (v8)
	 *
	 * RoomJoinAck / RoomStart / RoomHostReady 셋이 같은 배열을 실어 보내므로
	 * 여기 한 곳에만 둔다. 셋 중 하나만 고치는 실수가 나오지 않게 하기 위해서다.
	 *
	 * 주소가 비었거나 포트가 0 인 항목은 버린다 — 서버가 CandidateCount 를
	 * 잘못 보내도 쓰레기 주소로 여행하지 않는다.
	 */
	void ReadHostCandidates(const MOU::HostCandidate* Src, int32 Count,
		TArray<FMOUHostCandidate>& OutCandidates);

	/** v11 relay wire 경로를 네이티브 전송 상태로 옮긴다. Token 은 UI에 노출하지 않는다. */
	FMOUGameRelayRoute ReadRelayHostRoute(const MOU::RelayHostRoute& Src);
	FMOUGameRelayRoute ReadRelayGuestRoute(const MOU::RelayGuestRoute& Src);

	/** 길이가 명시된 UTF-8 바이트열(ChatBroadcast 뒤에 붙는 본문)을 FString 으로 변환한다. */
	FString Utf8ToString(const uint8* Src, int32 Len);
}
