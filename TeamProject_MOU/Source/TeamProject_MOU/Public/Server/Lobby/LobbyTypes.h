// MOU 로비 - 블루프린트 / UMG 에 노출되는 방 관련 타입.
//
// [이 파일의 위치] ChatTypes.h 와 같은 층이다.
//   ChatProtocol.h 의 RoomInfo 는 고정 길이 char 배열과 uint64 를 써서
//   블루프린트에 그대로 노출할 수 없다. 그래서 여기서 UE 친화적인 형태로 다시 정의하고,
//   소켓 층(FServerClientRunnable)이 변환해서 올려보낸다.
//
// [수정 시 주의]
//   EMOURoomResultBP 의 숫자 값은 MOU::ERoomResult 와 반드시 일치해야 한다.
//   불일치하면 ChatFraming.h 의 static_assert 가 컴파일 타임에 막아준다.

#pragma once

#include "CoreMinimal.h"
#include "ChatProtocol.h"
#include "LobbyTypes.generated.h"

/**
 * 방 요청의 결과. MOU::ERoomResult 의 블루프린트 미러.
 */
UENUM(BlueprintType)
enum class EMOURoomResultBP : uint8
{
	Success        = 0 UMETA(DisplayName = "성공"),
	NotAuthed      = 1 UMETA(DisplayName = "로그인 필요"),
	NotFound       = 2 UMETA(DisplayName = "없는 방"),
	WrongPassword  = 3 UMETA(DisplayName = "방 비밀번호 불일치"),
	Full           = 4 UMETA(DisplayName = "정원 초과"),
	AlreadyStarted = 5 UMETA(DisplayName = "이미 시작된 방"),
	AlreadyHosting = 6 UMETA(DisplayName = "이미 방을 만들었음"),
	InvalidRequest = 7 UMETA(DisplayName = "잘못된 요청"),

	// --- 대기실 (v5) ---
	NotInRoom      = 8 UMETA(DisplayName = "방에 있지 않음"),
	NotHost        = 9 UMETA(DisplayName = "방장만 가능"),
	NotAllReady    = 10 UMETA(DisplayName = "준비하지 않은 사람이 있음"),

	// --- 호스트 준비 신호 (v6) ---
	NotStarted     = 11 UMETA(DisplayName = "아직 시작되지 않은 방")
};

/**
 * 계정 인증과 세션 탐색을 무엇으로 하는가.
 *
 * [무엇을 고르든 게임 트래픽은 지나가지 않는다]
 *   여기서 정하는 것은 "방이 열리기 전"과 "계정" 뿐이다. 참가자가 주소를 받고 나면
 *   이동/전투/GAS 는 호스트의 리슨서버가 전부 처리하고, 이 경로는 한 바이트도 타지 않는다.
 *
 * 자세한 배경은 Server/Lobby/LobbyBackend.h 주석에 있다.
 */
UENUM(BlueprintType)
enum class EMOULobbyBackendType : uint8
{
	/**
	 * 자체 서버(MOU_Server/Server.exe)에 TCP 로 붙는다.
	 *
	 * 개발과 LAN 시연에서 쓴다. 외부 SDK 도, Epic/Steam 계정도, 인터넷도 필요 없고
	 * 사망 채널·채팅 로그처럼 게임 상태를 아는 쪽만 판정할 수 있는 기능이 여기에 있다.
	 * 한계는 NAT 다 — 호스트가 포트포워딩을 해야 외부에서 붙을 수 있다.
	 */
	CustomSocket = 0 UMETA(DisplayName = "자체 서버 (TCP)"),

	/**
	 * Epic Online Services 의 Connect(계정) + Lobby/Session(방 목록).
	 *
	 * NAT 트래버설과 릴레이를 SDK 가 해주므로 포트포워딩 없이 외부 접속이 된다.
	 * 출시 단계에서 쓸 목표 백엔드다. 아직 구현체는 뼈대만 있다(FEOSLobbyBackend).
	 */
	EOS = 1 UMETA(DisplayName = "Epic Online Services")
};

/**
 * 방이 사라진 이유. MOU::ERoomCloseReason 의 블루프린트 미러.
 *
 * 호스트 이양은 하지 않는다 — 호스트가 곧 리슨서버라서, 호스트가 나가면
 * 게임 세션 자체가 사라진다. 남은 사람은 메인메뉴로 돌아가야 한다.
 */
UENUM(BlueprintType)
enum class EMOURoomCloseReasonBP : uint8
{
	HostLeft = 0 UMETA(DisplayName = "방장이 나감")
};

/** 방의 진행 상태. MOU::ERoomState 의 블루프린트 미러. */
UENUM(BlueprintType)
enum class EMOURoomStateBP : uint8
{
	Waiting = 0 UMETA(DisplayName = "대기중"),
	InGame  = 1 UMETA(DisplayName = "게임중")
};

/**
 * 방 목록에 보이는 방 하나.
 *
 * 호스트 주소가 없는 것이 의도적이다.
 * 목록만 보고 비밀번호 방에 바로 붙는 것을 막기 위해,
 * 주소는 JoinRoom() 이 성공했을 때만 따로 내려온다.
 */
USTRUCT(BlueprintType)
struct FMOURoomInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	int32 RoomId = 0;

	/** 방장의 계정 번호. 원본은 uint64 지만 블루프린트가 못 다뤄 int64 로 받는다. */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	int64 HostUserId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	FString Title;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	FString HostName;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	int32 MaxPlayers = 4;

	/** true 면 참여할 때 숫자 4자리를 입력해야 한다. */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	bool bHasPassword = false;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	EMOURoomStateBP State = EMOURoomStateBP::Waiting;
};

/**
 * 대기실에 앉아 있는 사람 하나. MOU::RoomMemberInfo 의 블루프린트 미러.
 *
 * 이 목록은 서버가 진실을 갖고 있고, 바뀔 때마다 통째로 다시 내려온다.
 * 클라이언트가 들고 있는 것은 마지막으로 받은 스냅샷일 뿐이다.
 */
USTRUCT(BlueprintType)
struct FMOURoomMember
{
	GENERATED_BODY()

	/** 계정 번호. 원본은 uint64 지만 블루프린트가 못 다뤄 int64 로 받는다. */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	int64 UserId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	bool bIsHost = false;

	/** 방장은 항상 true 다. 자기가 시작 버튼을 누르는 사람이라 준비를 묻지 않는다. */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	bool bReady = false;
};

/**
 * 호스트 주소가 어떤 성격인가. (v8)
 * MOU::EHostAddrKind 와 값이 같아야 한다 — ChatFraming.h 가 static_assert 로 묶는다.
 */
UENUM(BlueprintType)
enum class EMOUHostAddrKindBP : uint8
{
	Public = 0   UMETA(DisplayName = "공인"),
	Lan    = 1   UMETA(DisplayName = "같은 LAN"),
	Punch  = 2   UMETA(DisplayName = "홀펀칭"),
};

/**
 * 호스트에게 가는 길 하나.
 *
 * 여러 개인 이유: 방장과 **같은 공유기 안에 있는 참가자**는 공인 IP 로 나갔다
 * 돌아오는 헤어핀 접속을 해야 하는데, 그걸 지원하지 않는 공유기가 흔하다.
 * 실측으로 확인했다 — 같은 LAN 에서 사설 IP 로는 즉시 붙는데 공인 IP 로는
 * 핸드셰이크가 통째로 사라졌다. 사설 주소를 같이 내려주면 그 조합이 풀린다.
 */
USTRUCT(BlueprintType)
struct FMOUHostCandidate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	FString Address;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	int32 Port = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	EMOUHostAddrKindBP Kind = EMOUHostAddrKindBP::Public;

	bool IsValid() const { return !Address.IsEmpty() && Port > 0; }

	/**
	 * ClientTravel 에 넣을 완성된 URL 을 만든다.
	 *
	 * 방 비밀번호는 URL 옵션으로 실어 보낸다.
	 * 호스트의 GameMode::PreLogin 이 이 옵션을 검사해서 최종적으로 막는다.
	 * (로비 서버의 검사는 UX 용이고, 진짜 관문은 호스트다.)
	 */
	FString MakeTravelURL(const FString& RoomPassword) const
	{
		FString URL = FString::Printf(TEXT("%s:%d"), *Address, Port);
		if (!RoomPassword.IsEmpty())
		{
			URL += FString::Printf(TEXT("?RoomPassword=%s"), *RoomPassword);
		}
		return URL;
	}

	FString ToDisplayString() const
	{
		const TCHAR* KindText =
			(Kind == EMOUHostAddrKindBP::Lan)   ? TEXT("LAN")   :
			(Kind == EMOUHostAddrKindBP::Punch) ? TEXT("홀펀칭") : TEXT("공인");
		return FString::Printf(TEXT("%s:%d(%s)"), *Address, Port, KindText);
	}
};

/**
 * 직접 연결이 끝내 실패했을 때만 쓰는 참여자 전용 UDP 릴레이 경로. (v11)
 *
 * 이 타입은 네이티브 전송 상태용이다. Token 은 블루프린트나 로그로 내보내지 않고,
 * UE 넷드라이버가 실제 게임 소켓에서 relay 등록 데이터그램을 만들 때만 사용한다.
 */
struct FMOUGameRelayRoute
{
	FString Address;
	int32   Port = 0;
	uint64  RouteId = 0;
	TArray<uint8> Token;

	bool HasValidToken() const { return Token.Num() == static_cast<int32>(MOU::kRelayTokenBytes); }
	bool IsValid() const { return !Address.IsEmpty() && Port > 0 && RouteId != 0 && HasValidToken(); }

	/** 참여자가 relay 의 guest-facing 포트로 떠날 때 쓴다. Token 은 URL 에 절대 넣지 않는다. */
	FString MakeGuestTravelURL(const FString& RoomPassword) const
	{
		FString URL = FString::Printf(TEXT("%s:%d"), *Address, Port);
		if (!RoomPassword.IsEmpty())
		{
			URL += FString::Printf(TEXT("?RoomPassword=%s"), *RoomPassword);
		}
		return URL;
	}

	FString ToGuestDisplayString() const
	{
		return FString::Printf(TEXT("%s:%d(릴레이)"), *Address, Port);
	}
};

/**
 * 방 참여 요청의 결과.
 *
 * bSuccess 일 때만 Candidates 가 채워진다.
 * 어느 후보로 갈지는 **받는 쪽이** 정한다 — UServerSubsystem::ChooseHostCandidate.
 */
USTRUCT(BlueprintType)
struct FMOURoomJoinResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	int32 RoomId = 0;

	/** 호스트에게 가는 길들. 순서는 우선순위가 아니다 — 고르는 것은 받는 쪽이다. */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	TArray<FMOUHostCandidate> Candidates;

	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	EMOURoomResultBP Result = EMOURoomResultBP::Success;

	/**
	 * 이 방은 같은 LAN 안에서만 들어올 수 있는가. (v9)
	 *
	 * 방장이 도달성 프로브에 실패했다는 뜻이다 — UPnP 매핑은 됐지만 공유기가
	 * 실제로 포워딩을 하지 않거나 ISP 가 인바운드 UDP 를 거른다.
	 *
	 * 이 값이 참인데 내가 공인 후보를 골랐다면 **접속은 반드시 실패한다.**
	 * 그럴 때는 시도하지 말고 그 자리에서 사유를 보여주는 편이 낫다 —
	 * 시도하면 핸드셰이크 타임아웃까지 1분 가까이 화면이 멈춘다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "MOU|Lobby")
	bool bLanOnly = false;

	/** 로그와 화면에 쓸 요약. "218.153.186.240:7777(공인), 192.168.0.32:7777(LAN)" */
	FString ToDisplayString() const
	{
		if (Candidates.Num() == 0)
		{
			return TEXT("(주소 없음)");
		}

		TArray<FString> Parts;
		Parts.Reserve(Candidates.Num());
		for (const FMOUHostCandidate& C : Candidates)
		{
			Parts.Add(C.ToDisplayString());
		}
		return FString::Join(Parts, TEXT(", "));
	}
};
