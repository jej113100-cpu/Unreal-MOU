// MOU 음성 - 지터버퍼 구현.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 13절, 14절 V4
//
// [스레드] 게임 스레드 전용. 헤더 상단 주석 참고.

#include "Voice/VoiceJitterBuffer.h"

namespace
{
	/**
	 * uint16 순환을 견디는 "A 가 B 보다 뒤인가" 판정 기준.
	 *
	 * 차이를 uint16 으로 계산했을 때 이 값보다 크면 **음수로 해석**한다.
	 * 즉 Delta 65535 는 "6만개 앞" 이 아니라 "1개 뒤" 다.
	 * 이 한 줄이 22분마다 스트림이 무너지는 것을 막는다(헤더 상단 주석).
	 */
	constexpr uint16 GSeqHalfRange = 32768;
}

bool FVoiceJitterBuffer::Push(uint16 Seq, const TArray<uint8>& Payload)
{
	if (!bStarted)
	{
		// 첫 프레임이 기준이 된다. 이 번호부터 순서대로 재생한다.
		bStarted = true;
		NextSeq  = Seq;
	}

	const uint16 Delta = static_cast<uint16>(Seq - NextSeq);

	// --- 이미 지나간 번호 ---------------------------------------------------
	//
	// 재생이 이 번호를 이미 통과했다(은폐로 메웠거나 정상 재생했거나).
	// 지금 넣어봐야 꺼낼 방법이 없으므로 버린다.
	if (Delta >= GSeqHalfRange)
	{
		++LateCount;
		return false;
	}

	// --- 버퍼 범위를 넘어설 만큼 앞선 프레임 ---------------------------------
	//
	// 재생이 뒤처졌다는 뜻이다. 그대로 두면 이 프레임을 담을 자리가 없고,
	// 억지로 담으면 아직 안 꺼낸 프레임을 덮어쓴다.
	// **따라잡는 편이 낫다** - 밀린 소리를 마저 듣는 것보다 현재를 듣는 것이
	// 대화에서는 훨씬 쓸모 있다.
	if (Delta >= MOUVoice::MaxJitterFrames)
	{
		ResyncTo(Seq);
		++ResyncCount;
	}

	FSlot& Slot = Slots[Seq % MOUVoice::MaxJitterFrames];

	if (Slot.bValid && Slot.Seq == Seq)
	{
		// 같은 프레임이 두 번 왔다. 넣으면 같은 소리가 두 번 난다.
		++DuplicateCount;
		return false;
	}

	// 다른 번호가 들어있다면 그건 이 자리를 쓰던 옛 프레임이다.
	// (위에서 Delta < MaxJitterFrames 를 보장했으므로 아직 안 꺼낸 유효한
	//  프레임을 덮어쓰는 경우는 없다 - 그런 상황은 재동기화로 걸러졌다)
	const bool bReplacingSlot = Slot.bValid;

	Slot.Payload = Payload;
	Slot.Seq     = Seq;
	Slot.bValid  = true;

	if (!bReplacingSlot)
	{
		++PendingCount;
	}

	return true;
}

EVoiceJitterResult FVoiceJitterBuffer::Pop(TArray<uint8>& OutPayload)
{
	if (!bStarted)
	{
		return EVoiceJitterResult::Starved;
	}

	// --- 채워질 때까지 기다린다 ---------------------------------------------
	//
	// 목표 깊이만큼 모이기 전에는 재생을 시작하지 않는다. 바로 시작하면
	// 여유분이 0 이라 **다음 프레임이 조금만 늦어도 즉시 끊긴다.**
	// 이 대기가 곧 지터버퍼가 사는 지연이다.
	if (!bPrimed)
	{
		if (PendingCount < MOUVoice::TargetJitterFrames)
		{
			return EVoiceJitterResult::Starved;
		}

		bPrimed = true;
	}

	FSlot& Slot = Slots[NextSeq % MOUVoice::MaxJitterFrames];

	if (Slot.bValid && Slot.Seq == NextSeq)
	{
		// MoveTemp 로 배열 소유권을 넘긴다. 20ms 마다 도는 경로라 복사를 아낀다.
		OutPayload = MoveTemp(Slot.Payload);
		Slot.bValid = false;

		--PendingCount;
		++NextSeq;

		return EVoiceJitterResult::Frame;
	}

	// --- 자리가 비었다. 유실인가, 아직 안 온 것인가 --------------------------
	//
	// ★ 이 구분이 지터버퍼의 핵심이다.
	//
	//   뒤 번호가 이미 도착해 있다면 이 번호는 **영원히 안 온다**(순서가 뒤집힌
	//   패킷은 아직 올 수 있지만, 그걸 기다리면 재생이 멈춘다). 유실로 확정하고
	//   은폐로 메운 뒤 넘어간다.
	//
	//   버퍼가 통째로 비었다면 **그냥 아직 안 온 것**이다. 여기서 은폐하면
	//   멀쩡히 오고 있는 소리를 밀어내서, 네트워크가 멀쩡한데도 소리가
	//   지직거리게 된다.
	if (PendingCount > 0)
	{
		++NextSeq;
		++ConcealCount;
		return EVoiceJitterResult::Conceal;
	}

	// 다시 목표 깊이만큼 모일 때까지 기다린다.
	// 이걸 안 하면 프레임이 하나 올 때마다 재생했다 멈췄다 해서 계속 끊긴다.
	bPrimed = false;
	++StarveCount;

	return EVoiceJitterResult::Starved;
}

void FVoiceJitterBuffer::ResyncTo(uint16 InSeq)
{
	// 담고 있던 것을 전부 버린다.
	//
	// 남겨서 이어붙이는 방법도 있지만, 여기 온 시점에 이미 지연이 버퍼 용량을
	// 넘겼다는 뜻이라 그 프레임들은 재생해봐야 "몇 초 전 목소리" 다.
	// 깨끗이 버리고 현재부터 다시 쌓는 편이 결과가 낫고 코드도 단순하다.
	for (FSlot& Slot : Slots)
	{
		Slot.bValid = false;
		Slot.Payload.Reset();
	}

	PendingCount = 0;
	NextSeq      = InSeq;

	// 다시 목표 깊이만큼 모은 뒤에 재생을 시작한다.
	bPrimed = false;
}

void FVoiceJitterBuffer::Reset()
{
	ResyncTo(0);

	// ResyncTo 는 "재생 위치 이동" 이라 bStarted 를 건드리지 않는다.
	// 여기서는 기준 번호 자체를 버리므로 다음 Push 가 새 기준을 잡게 한다.
	bStarted = false;
}
