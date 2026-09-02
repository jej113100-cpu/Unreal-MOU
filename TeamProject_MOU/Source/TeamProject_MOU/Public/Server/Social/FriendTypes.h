// MOU 친구 시스템 - 블루프린트 / UMG 에 노출되는 데이터 타입 (v7).
//
// [이 파일의 위치]
//   ChatTypes.h 와 같은 층이다. 서버와 공유하는 원시 구조체(ChatProtocol.h)를
//   블루프린트가 다룰 수 있는 형태로 한 번 변환한 것이다.
//
//   왜 그대로 못 쓰는가: 프로토콜 구조체는 uint64 와 고정 길이 char 배열을 쓴다.
//   블루프린트는 uint64 를 모르고 char[] 도 모른다. 그래서 UserId 는 int64 로,
//   Nickname 은 FString 으로 바꿔서 넘긴다.
//
// [★ UserId 가 int64 인 이유 — 값이 잘릴 수 있다]
//   서버의 UserId 는 uint64 다. 블루프린트에 uint64 가 없어서 int64 로 받는다.
//   accounts.id 는 AUTOINCREMENT 라 실사용 범위에서 2^63 을 넘을 일이 없으므로
//   안전하지만, **비교나 저장에 int32 를 쓰면 안 된다.**
//
// [수정 시 주의]
//   아래 enum 의 숫자 값은 MOU::EFriendState / EPresence / EFriendResult 와
//   반드시 일치해야 한다. 불일치는 ChatFraming.h 의 static_assert 가 막는다.
//
// [대응하는 문서]
//   CHAT_DESIGN.md 4절(친구), 5절(접속 상태)

#pragma once

#include "CoreMinimal.h"
#include "FriendTypes.generated.h"

/**
 * 친구 관계의 상태. MOU::EFriendState 의 블루프린트 미러.
 *
 * ★ 대칭이 아니다. 보낸 신청과 받은 신청은 화면이 다르다 —
 *   보낸 쪽은 "대기 중" 회색, 받은 쪽은 수락/거절 버튼이 뜬다.
 */
UENUM(BlueprintType)
enum class EMOUFriendStateBP : uint8
{
	/** 서로 수락했다. 메시지를 주고받을 수 있다 */
	Friend          = 0 UMETA(DisplayName = "친구"),
	/** 내가 신청했고 상대가 아직 응답하지 않았다 */
	PendingOutgoing = 1 UMETA(DisplayName = "신청함"),
	/** 상대가 신청했고 내가 수락/거절해야 한다 */
	PendingIncoming = 2 UMETA(DisplayName = "신청받음"),
};

/**
 * 친구의 접속 상태. MOU::EPresence 의 블루프린트 미러.
 *
 * ★ 서버가 판정한 값이다. 클라가 추측하지 않는다 —
 *   추측하면 두 클라가 같은 사람을 다르게 표시한다.
 */
UENUM(BlueprintType)
enum class EMOUPresenceBP : uint8
{
	Offline = 0 UMETA(DisplayName = "오프라인"),
	Online  = 1 UMETA(DisplayName = "온라인"),
	InGame  = 2 UMETA(DisplayName = "게임중"),
};

/** 친구 요청의 결과. MOU::EFriendResult 의 블루프린트 미러. */
UENUM(BlueprintType)
enum class EMOUFriendResultBP : uint8
{
	Success        = 0 UMETA(DisplayName = "성공"),
	NotAuthed      = 1 UMETA(DisplayName = "로그인 필요"),
	NotFound       = 2 UMETA(DisplayName = "없는 닉네임"),
	/** ★ 같은 닉네임이 여럿이다. 나중에 #태그를 붙이면 사라질 오류다 */
	AmbiguousName  = 3 UMETA(DisplayName = "동명이인"),
	AlreadyFriend  = 4 UMETA(DisplayName = "이미 친구"),
	AlreadyPending = 5 UMETA(DisplayName = "이미 신청함"),
	SelfRequest    = 6 UMETA(DisplayName = "자기 자신"),
	LimitReached   = 7 UMETA(DisplayName = "친구 수 상한"),
	InvalidFormat  = 8 UMETA(DisplayName = "형식 오류"),
	DbError        = 9 UMETA(DisplayName = "서버 오류"),
};

/**
 * 친구 목록의 한 줄.
 *
 * 서버의 FriendEntry 를 그대로 옮긴 것이다. 이 구조체 하나가 곧 UI 의 한 줄이다.
 */
USTRUCT(BlueprintType)
struct FMOUFriend
{
	GENERATED_BODY()

	/** 상대의 계정 번호. 서버를 재시작해도 같은 값이다(accounts.id) */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Friend")
	int64 UserId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Friend")
	FString Nickname;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Friend")
	EMOUFriendStateBP State = EMOUFriendStateBP::Friend;

	/**
	 * ★ State 가 Friend 가 아니면 항상 Offline 이다.
	 *   신청만 걸어두고 남의 온/오프라인을 훔쳐보는 것을 서버가 막는다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Friend")
	EMOUPresenceBP Presence = EMOUPresenceBP::Offline;

	/** 안 읽은 DM 개수. 0 이면 배지를 숨긴다 */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Friend")
	int32 UnreadCount = 0;

	/** 정렬/표시 편의. 지금 말을 걸 수 있는가 */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Friend")
	bool bIsOnline = false;
};

/**
 * 1:1 메시지 한 통.
 *
 * ★ MessageId 는 **서버가 매긴다.** 클라가 만들지 않는다 —
 *   이 값이 기록 페이징의 기준(커서)이라, 클라마다 다르면 페이징이 깨진다.
 */
USTRUCT(BlueprintType)
struct FMOUDirectMessage
{
	GENERATED_BODY()

	/** 서버가 매긴 번호. 위로 스크롤할 때 커서로 쓴다 */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Messenger")
	int64 MessageId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Messenger")
	int64 FromUserId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Messenger")
	int64 ToUserId = 0;

	/** Unix epoch (초). 서버 시계 기준이다 */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Messenger")
	int64 Timestamp = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Messenger")
	FString Text;

	/**
	 * 내가 보낸 것인가. 말풍선을 좌/우 어느 쪽에 붙일지 정한다.
	 *
	 * 서버가 채워 보내는 값이 아니라 **받은 쪽이 자기 UserId 와 비교해** 채운다
	 * (UServerSubsystem 이 한다). 위젯이 매번 비교하지 않게 하려는 것이다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Messenger")
	bool bIsMine = false;

	/** 대화 상대의 UserId. 내가 보냈으면 To, 받았으면 From 이다 */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Messenger")
	int64 PeerUserId = 0;
};
