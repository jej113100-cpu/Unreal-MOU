// MOU 로비 - EOS 백엔드 (뼈대) 구현.
//
// 지금은 "아직 안 된다" 를 정확히 알리는 것이 이 파일이 하는 일 전부다.
// 붙이는 순서와 각 함수가 무엇으로 바뀌는지는 EOSLobbyBackend.h 주석에 있다.

#include "Server/Lobby/EOSLobbyBackend.h"

bool FEOSLobbyBackend::Start(const FString& /*Host*/, int32 /*Port*/)
{
	bStarted = true;

	// 실패를 사건으로 올린다. 로그만 남기면 화면에는 아무 변화가 없어서
	// "설정을 바꿨는데 왜 로그인 창이 안 넘어가지" 를 한참 헤매게 된다.
	FServerClientEvent Event;
	Event.Type   = EServerClientEventType::ConnectFailed;
	Event.Detail = TEXT("EOS 백엔드는 아직 구현되지 않았습니다. ")
	               TEXT("Project Settings -> Game -> MOU Server -> Lobby Backend 를 ")
	               TEXT("'자체 서버 (TCP)' 로 되돌리세요.");
	InboundEvents.Enqueue(MoveTemp(Event));

	UE_LOG(LogMOUServer, Error,
		TEXT("EOS 백엔드가 선택됐지만 아직 구현되지 않았다. 붙이는 순서는 EOSLobbyBackend.h 참고."));

	// Start 자체는 성공으로 돌려준다. false 를 주면 서브시스템이 백엔드를 즉시
	// 버리는데, 그러면 위에서 넣은 사건도 같이 사라져 사유가 화면에 뜨지 않는다.
	return true;
}

void FEOSLobbyBackend::Shutdown()
{
	bStarted = false;
	InboundEvents.Empty();
}

bool FEOSLobbyBackend::DequeueEvent(FServerClientEvent& Out)
{
	return InboundEvents.Dequeue(Out);
}

bool FEOSLobbyBackend::DequeueMessage(FChatMessage& /*Out*/)
{
	return false;   // SupportsChat() == false. 채팅은 이 백엔드를 통하지 않는다
}

void FEOSLobbyBackend::LogNotImplemented(const TCHAR* What) const
{
	UE_LOG(LogMOUServer, Warning, TEXT("EOS 백엔드 미구현: %s"), What);
}

void FEOSLobbyBackend::SendLogin(const FString&, const FString&, int32)
{
	LogNotImplemented(TEXT("로그인 (EOS_Connect_Login)"));
}

void FEOSLobbyBackend::SendRegister(const FString&, const FString&, const FString&)
{
	// EOS 에는 "우리 서비스의 계정을 만든다" 라는 개념이 없다. 계정은 Epic 쪽에 있고
	// 우리는 ProductUserId 를 받을 뿐이다. 이 함수는 EOS 전환 후 UI 에서 사라진다.
	LogNotImplemented(TEXT("계정 생성 (EOS 에는 해당 개념이 없다)"));
}

void FEOSLobbyBackend::SendChat(EChatChannelBP, const FString&)
{
	LogNotImplemented(TEXT("채팅 (자체 서버가 계속 담당한다)"));
}

void FEOSLobbyBackend::SendSetDead(int64, bool)
{
	LogNotImplemented(TEXT("생사 상태 (자체 서버가 계속 담당한다)"));
}

void FEOSLobbyBackend::CreateRoom(const FString&, const FString&, int32, const FString&)
{
	LogNotImplemented(TEXT("방 생성 (CreateSession)"));
}

void FEOSLobbyBackend::RequestRoomList()
{
	LogNotImplemented(TEXT("방 목록 (FindSessions)"));
}

void FEOSLobbyBackend::JoinRoom(int32, const FString&)
{
	LogNotImplemented(TEXT("방 참여 (JoinSession)"));
}

void FEOSLobbyBackend::LeaveRoom()
{
	LogNotImplemented(TEXT("방 나가기 (DestroySession)"));
}

void FEOSLobbyBackend::SetReady(bool)
{
	LogNotImplemented(TEXT("준비 상태 (UpdateSession)"));
}

void FEOSLobbyBackend::StartGame()
{
	LogNotImplemented(TEXT("게임 시작 (StartSession)"));
}

void FEOSLobbyBackend::NotifyHostReady()
{
	LogNotImplemented(TEXT("호스트 준비 신호 (UpdateSession 의 HostReady 속성)"));
}

void FEOSLobbyBackend::UpdateRoomState(int32, int32, bool)
{
	LogNotImplemented(TEXT("방 상태 갱신 (UpdateSession)"));
}

// ---------------------------------------------------------------------------
// 친구 / 메신저 (v7)
//
// ★ EOS 로 옮길 때의 대응 관계:
//     RequestFriendList     -> EOS_Friends_QueryFriends + EOS_Friends_GetFriendAtIndex
//     AddFriend             -> EOS_Friends_SendInvite
//     RespondFriendRequest  -> EOS_Friends_AcceptInvite / RejectInvite
//     RemoveFriend          -> EOS_Friends_... (삭제 API 는 EAS 정책 확인 필요)
//     접속 상태             -> EOS_Presence_QueryPresence + AddNotifyOnPresenceChanged
//
// ★★ **DM 만은 EOS 로 옮길 수 없다.** EOS 에는 1:1 메시지를 보관해주는
//    기능이 없다(P2P 는 양쪽이 동시에 접속해 있어야 한다). 오프라인 보관과
//    기록 조회가 요구사항이므로 DM 은 EOS 로 가더라도 자체 서버가 남는다.
//    이 점을 모르고 "EOS 로 전부 옮긴다" 고 계획하면 중간에 막힌다.
// ---------------------------------------------------------------------------

void FEOSLobbyBackend::RequestFriendList()
{
	LogNotImplemented(TEXT("친구 목록 (EOS_Friends_QueryFriends)"));
}

void FEOSLobbyBackend::AddFriend(const FString&)
{
	LogNotImplemented(TEXT("친구 신청 (EOS_Friends_SendInvite)"));
}

void FEOSLobbyBackend::RespondFriendRequest(int64, bool)
{
	LogNotImplemented(TEXT("친구 신청 응답 (EOS_Friends_AcceptInvite / RejectInvite)"));
}

void FEOSLobbyBackend::RemoveFriend(int64)
{
	LogNotImplemented(TEXT("친구 삭제 (EOS Friends)"));
}

void FEOSLobbyBackend::SendDirectMessage(int64, const FString&)
{
	LogNotImplemented(TEXT("1:1 메시지 (★ EOS 에 보관 기능이 없다 - 위 주석 참고)"));
}

void FEOSLobbyBackend::RequestDmHistory(int64, int64)
{
	LogNotImplemented(TEXT("대화 기록 (★ EOS 에 보관 기능이 없다 - 위 주석 참고)"));
}

// 도달성 프로브 (v9) — EOS 백엔드에서는 의미가 없다.
// EOS 는 릴레이/홀펀칭을 SDK 가 알아서 하므로 "공유기가 포워딩을 하는가" 를
// 물을 필요 자체가 없다. 뼈대만 맞춰두고 아무것도 하지 않는다.
void FEOSLobbyBackend::RequestHostProbe(int32, uint32) {}
void FEOSLobbyBackend::ReportReachability(bool) {}
