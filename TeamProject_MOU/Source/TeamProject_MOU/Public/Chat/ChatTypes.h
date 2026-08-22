// MOU 채팅 시스템 - 블루프린트 / UMG 에 노출되는 데이터 타입 모음.
//
// [이 파일의 위치]
//   채팅 시스템은 3개 층으로 나뉜다.
//     1) ChatProtocol.h        : 서버와 공유하는 원시 패킷 구조체 (MOU_Server/Shared/)
//     2) ChatFraming / Runnable: 소켓 + 바이트 다루는 층. C++ 전용
//     3) ChatTypes.h  <- 여기 : 위젯/블루프린트가 보는 층. UObject 시스템 타입
//
//   왜 나누는가: ChatProtocol.h 의 구조체는 고정 길이 char 배열과 uint64 를 쓰기 때문에
//   블루프린트에 그대로 노출할 수 없다 (BP 는 uint64 와 char[] 를 모르고,
//   USTRUCT 도 아니다). 그래서 소켓 층에서 받은 패킷을 여기 정의된
//   FChatMessage 같은 "언리얼 친화적인" 형태로 한 번 변환해서 UI 로 넘긴다.
//
// [수정 시 주의]
//   EChatChannelBP 의 숫자 값은 MOU::EChatChannel 과 반드시 일치해야 한다.
//   불일치하면 ChatFraming.h 의 static_assert 가 컴파일 타임에 막아준다.
//   채널을 추가하려면 ChatProtocol.h(서버 공용) -> 여기 순서로 둘 다 고쳐야 한다.

#pragma once

#include "CoreMinimal.h"
#include "ChatTypes.generated.h"

// 채팅 관련 로그는 전부 이 카테고리로 나간다.
// 에디터 출력 로그 창에서 "LogMOUChat" 으로 필터링하면 채팅만 볼 수 있다.
// 콘솔에서 상세 로그를 켜려면: Log LogMOUChat Verbose
DECLARE_LOG_CATEGORY_EXTERN(LogMOUChat, Log, All);

/**
 * 채팅 채널. MOU::EChatChannel (ChatProtocol.h) 의 블루프린트 미러.
 *
 * 채널별로 "누가 받는가" 는 전적으로 채팅 서버가 결정한다 (Server.cpp 의 RouteChat).
 * 클라이언트는 채널 값을 붙여서 보낼 뿐이고, 자격이 없으면 서버가 조용히 버린다.
 *   - All  : 접속자 전원
 *   - Team : 나와 TeamId 가 같은 사람
 *   - Dead : 죽은 사람끼리. 살아있는데 보내면 서버가 무응답으로 폐기한다
 *   - Whisper : 9단계 예정. 지금 보내면 아무에게도 전달되지 않는다
 *   - System  : 서버만 생성한다. 클라이언트가 보내면 서버가 무시한다
 */
UENUM(BlueprintType)
enum class EChatChannelBP : uint8
{
	All     = 0 UMETA(DisplayName = "전체"),
	Team    = 1 UMETA(DisplayName = "팀"),
	Dead    = 2 UMETA(DisplayName = "사망"),
	Whisper = 3 UMETA(DisplayName = "귓속말"),
	System  = 4 UMETA(DisplayName = "시스템")
};

/**
 * 채팅 서버와의 연결 상태.
 *
 * UChatSubsystem 이 이 값을 들고 있고, 바뀔 때마다 OnChatStateChanged 로 알린다.
 * UI 는 이걸 보고 "연결 중...", "채팅 서버 끊김" 같은 표시를 하면 된다.
 *
 * 주의: Connected 와 LoggedIn 은 다르다.
 *   Connected = TCP 는 붙었지만 아직 내 UserId 가 없다. 이 상태에서 채팅을 보내면
 *               서버가 무시한다 (Server.cpp HandleChatSend 의 bAuthed 검사).
 *   LoggedIn  = LoginAck 를 받아 UserId 가 확정됐다. 이때부터 채팅 가능.
 */
UENUM(BlueprintType)
enum class EChatConnectionState : uint8
{
	Disconnected UMETA(DisplayName = "연결 끊김"),
	Connecting   UMETA(DisplayName = "연결 중"),
	Connected    UMETA(DisplayName = "연결됨(로그인 전)"),
	LoggedIn     UMETA(DisplayName = "로그인 완료")
};

/**
 * 서버에서 받은 채팅 한 줄.
 *
 * [흐름] 채팅서버 ChatBroadcast 패킷
 *        -> FChatClientRunnable::HandlePacket (워커 스레드에서 파싱)
 *        -> TQueue 에 적재
 *        -> UChatSubsystem::Tick (게임 스레드에서 꺼냄)
 *        -> OnChatMessageReceived 델리게이트
 *        -> WBP_Chat 이 한 줄 위젯을 만들어 붙임 (5단계)
 *
 * SenderName / SenderUserId 는 서버가 세션 정보로 채운 값이다.
 * 보낸 사람이 패킷에 적어 보낸 값이 아니므로 위조되지 않는다.
 * 그래서 이 이름을 그대로 UI 에 표시해도 안전하다.
 */
USTRUCT(BlueprintType)
struct FChatMessage
{
	GENERATED_BODY()

	/** 서버가 부여한 발신자 ID. 원본은 uint64 지만 블루프린트가 uint64 를 못 다뤄서 int64 로 받는다. */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Chat")
	int64 SenderUserId = 0;

	/** 서버가 확정한 발신자 이름. 클라이언트가 보낸 문자열이 아니다. */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Chat")
	FString SenderName;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Chat")
	FString Text;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Chat")
	EChatChannelBP Channel = EChatChannelBP::All;

	/** 서버 기준 시각. 패킷에는 Unix epoch(초) 로 오고 여기서 FDateTime 으로 변환된다. */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Chat")
	FDateTime Timestamp;
};

/**
 * 로그인 거부 사유. MOU::ELoginResult (ChatProtocol.h) 의 블루프린트 미러.
 *
 * VersionMismatch 는 재시도해도 계속 실패한다.
 * 서버와 클라이언트를 같은 커밋으로 다시 빌드해야 한다.
 */
UENUM(BlueprintType)
enum class EChatLoginResultBP : uint8
{
	Success         = 0 UMETA(DisplayName = "성공"),
	VersionMismatch = 1 UMETA(DisplayName = "프로토콜 버전 불일치"),
	InvalidRequest  = 2 UMETA(DisplayName = "잘못된 요청"),

	// --- 계정 관련 ---
	AccountNotFound = 3 UMETA(DisplayName = "없는 아이디"),
	WrongPassword   = 4 UMETA(DisplayName = "비밀번호 불일치"),
	DuplicateId     = 5 UMETA(DisplayName = "이미 있는 아이디"),
	InvalidFormat   = 6 UMETA(DisplayName = "형식 위반"),
	ServerError     = 7 UMETA(DisplayName = "서버 오류")
};

/**
 * LoginAck 결과. 내 신원이 여기서 확정된다.
 *
 * 여기 담긴 UserId / Name / TeamId 는 전부 "서버가 정해준" 값이다.
 * 내가 Login() 에 넣은 이름과 다를 수 있다 (예: 빈 이름이면 서버가 "익명3" 으로 채운다).
 * 따라서 UI 에 내 이름을 표시할 때는 내가 입력한 값이 아니라 이 값을 써야 한다.
 */
USTRUCT(BlueprintType)
struct FChatLoginResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Chat")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Chat")
	int64 UserId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Chat")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Chat")
	int32 TeamId = -1;

	/** 실패 사유. bSuccess 가 false 일 때만 의미가 있다. */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Chat")
	EChatLoginResultBP Result = EChatLoginResultBP::Success;

	/** 서버가 알려준 프로토콜 버전. 버전 불일치를 진단할 때 쓴다. */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Chat")
	int32 ServerVersion = 0;
};
