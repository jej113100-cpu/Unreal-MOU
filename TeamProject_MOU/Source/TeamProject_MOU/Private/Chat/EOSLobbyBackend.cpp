// MOU 로비 - EOS 백엔드 (뼈대) 구현.
//
// 지금은 "아직 안 된다" 를 정확히 알리는 것이 이 파일이 하는 일 전부다.
// 붙이는 순서와 각 함수가 무엇으로 바뀌는지는 EOSLobbyBackend.h 주석에 있다.

#include "Chat/EOSLobbyBackend.h"

bool FEOSLobbyBackend::Start(const FString& /*Host*/, int32 /*Port*/)
{
	bStarted = true;

	// 실패를 사건으로 올린다. 로그만 남기면 화면에는 아무 변화가 없어서
	// "설정을 바꿨는데 왜 로그인 창이 안 넘어가지" 를 한참 헤매게 된다.
	FChatClientEvent Event;
	Event.Type   = EChatClientEventType::ConnectFailed;
	Event.Detail = TEXT("EOS 백엔드는 아직 구현되지 않았습니다. ")
	               TEXT("Project Settings -> Game -> MOU Server -> Lobby Backend 를 ")
	               TEXT("'자체 서버 (TCP)' 로 되돌리세요.");
	InboundEvents.Enqueue(MoveTemp(Event));

	UE_LOG(LogMOUChat, Error,
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

bool FEOSLobbyBackend::DequeueEvent(FChatClientEvent& Out)
{
	return InboundEvents.Dequeue(Out);
}

bool FEOSLobbyBackend::DequeueMessage(FChatMessage& /*Out*/)
{
	return false;   // SupportsChat() == false. 채팅은 이 백엔드를 통하지 않는다
}

void FEOSLobbyBackend::LogNotImplemented(const TCHAR* What) const
{
	UE_LOG(LogMOUChat, Warning, TEXT("EOS 백엔드 미구현: %s"), What);
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

void FEOSLobbyBackend::CreateRoom(const FString&, const FString&, int32)
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
