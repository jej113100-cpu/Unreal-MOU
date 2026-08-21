// MOU 음성 - 지터버퍼.
//
// [이 파일이 시스템 어디에 있나]
//
//     ClientReceiveVoiceFrame  (도착 간격이 들쭉날쭉하고 순서도 뒤집힌다)
//       ▼ Push(Seq, Opus)
//   ★ FVoiceJitterBuffer  ← 이 파일. 번호 순서로 다시 세운다
//       ▼ Pop()  (재생 쪽이 일정한 속도로 꺼낸다)
//     디코더 -> UVoiceSynthComponent
//
// [★ 이 버퍼가 하는 일은 "기다리는 것" 이다]
//
//   UDP 는 20ms 마다 보내도 5ms, 40ms, 3ms... 로 도착한다. 순서도 뒤집힌다.
//   받는 즉시 재생하면 그 흔들림이 그대로 **소리의 끊김**이 된다.
//
//   그래서 일부러 몇 프레임을 모아두고 그 뒤에서 꺼내 쓴다. 늦게 온 프레임도
//   아직 재생 전이면 제자리를 찾아 들어간다. **지연을 주고 안정성을 사는 것**이고,
//   그 값이 MOUVoice::TargetJitterFrames(3프레임 = 60ms)다.
//
// [★★ Seq 는 uint16 이라 22분마다 0 으로 돌아간다]
//
//   이 파일의 모든 순서 비교는 **차이를 uint16 으로 계산**해서 그 순환을 견딘다.
//   `if (A > B)` 같은 크기 비교를 쓰면 65535 -> 0 이 되는 순간 "6만개를 잃었다" 가
//   되어 스트림이 통째로 무너진다. 22분에 한 번, 재현도 안 되는 버그가 된다.
//
// [왜 별도 클래스인가]
//   설계 문서 6절은 이 기능을 UVoicePlaybackComponent 안에 뒀지만, 순환 산술과
//   유실 판정은 **혼자서 틀리기 쉬운 로직**이라 떼어냈다. UObject 도 엔진 의존도
//   없어서 눈으로 읽고 검증하기 쉽다.
//
// [대응하는 문서]
//   VOICE_INTEGRATION.md 13절(TargetJitterFrames), 14절 V4

#pragma once

#include "CoreMinimal.h"
#include "Voice/VoiceTypes.h"

/** Pop() 이 재생 쪽에 시키는 일. */
enum class EVoiceJitterResult : uint8
{
	/** 정상 프레임을 꺼냈다. 디코딩해서 재생하면 된다. */
	Frame,

	/**
	 * 이 번호는 안 온다고 확정됐다(뒤 번호가 이미 도착해 있다).
	 * 재생 쪽이 은폐(PLC) 프레임을 만들어 메워야 한다.
	 */
	Conceal,

	/**
	 * 아직 재생할 것이 없다. **유실이 아니라 그냥 안 온 것이다.**
	 * 기다려야 한다 - 여기서 은폐하면 멀쩡히 오고 있는 소리를 밀어내게 된다.
	 */
	Starved,
};

/**
 * 발신자 한 명(정확히는 한 스트림)의 프레임을 번호 순서로 다시 세운다.
 *
 * **게임 스레드 전용.** 오디오 렌더 스레드에서 부르면 안 된다(할당이 일어난다).
 */
class FVoiceJitterBuffer
{
public:
	/**
	 * 도착한 프레임을 넣는다.
	 *
	 * @return 버퍼에 들어갔으면 true. 너무 늦었거나 중복이면 false.
	 */
	bool Push(uint16 Seq, const TArray<uint8>& Payload);

	/**
	 * 다음에 재생할 것을 꺼낸다.
	 *
	 * @param OutPayload  EVoiceJitterResult::Frame 일 때만 채워진다.
	 */
	EVoiceJitterResult Pop(TArray<uint8>& OutPayload);

	/** 새 발화가 시작될 때 등, 처음부터 다시 시작한다. */
	void Reset();

	/** 지금 대기 중인 프레임 수. 이 값이 곧 현재 지연(x 20ms)이다. */
	int32 GetPendingCount() const { return PendingCount; }

	/** 목표 깊이만큼 모여서 재생이 시작됐는지. */
	bool IsPrimed() const { return bPrimed; }

	// --- 진단 ---------------------------------------------------------------

	/** 이미 재생하고 지나간 뒤에 도착한 프레임 수. 지연이 크면 이 값이 오른다. */
	int32 GetLateCount() const { return LateCount; }

	/** 같은 번호가 두 번 온 횟수. */
	int32 GetDuplicateCount() const { return DuplicateCount; }

	/** 유실로 확정해 은폐를 요청한 횟수. */
	int32 GetConcealCount() const { return ConcealCount; }

	/** 재생이 뒤처져 따라잡기를 한 횟수. 계속 오르면 목표 깊이가 너무 낮다. */
	int32 GetResyncCount() const { return ResyncCount; }

	/** 데이터가 없어 재생이 멈춘 횟수. */
	int32 GetStarveCount() const { return StarveCount; }

private:
	/** 재생 위치를 InSeq 로 옮기고 버퍼를 비운다. */
	void ResyncTo(uint16 InSeq);

	struct FSlot
	{
		TArray<uint8> Payload;
		uint16 Seq    = 0;
		bool   bValid = false;
	};

	/**
	 * Seq % 용량 으로 자리를 정하는 고정 크기 배열.
	 *
	 * TMap 을 쓰지 않는 이유: 20ms 마다 도는 경로라 **할당이 없어야 한다.**
	 * 담을 개수가 8개로 고정이라 배열로 충분하고, 자리 계산도 나눗셈 한 번이다.
	 */
	FSlot Slots[MOUVoice::MaxJitterFrames];

	/** 다음에 꺼낼 번호. */
	uint16 NextSeq = 0;

	/** 첫 프레임을 받아 기준 번호가 정해졌는지. */
	bool bStarted = false;

	/** 목표 깊이만큼 모여 재생이 시작됐는지. */
	bool bPrimed = false;

	int32 PendingCount = 0;

	int32 LateCount      = 0;
	int32 DuplicateCount = 0;
	int32 ConcealCount   = 0;
	int32 ResyncCount    = 0;
	int32 StarveCount    = 0;
};
