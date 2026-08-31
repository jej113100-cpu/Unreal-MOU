// MOU 채팅 - 프레이밍 구현.
// 대응하는 서버 코드: MOU_Server/Shared/Framing.cpp
// 둘 중 하나만 고치면 프로토콜이 깨진다. 반드시 같이 본다.

#include "Server/Net/ChatFraming.h"

namespace MOUChat
{
	EFrameResult TryExtractPacket(TArray<uint8>& Buffer, MOU::PacketHeader& OutHeader, TArray<uint8>& OutBody)
	{
		const int32 HeaderSize = static_cast<int32>(sizeof(MOU::PacketHeader));

		// 1. 헤더조차 다 안 왔으면 더 기다린다.
		if (Buffer.Num() < HeaderSize)
		{
			return EFrameResult::NeedMore;
		}

		// 헤더는 #pragma pack(1) 로 6바이트 고정이다.
		// 정렬되지 않은 주소에서 바로 읽으면 플랫폼에 따라 문제가 되므로 Memcpy 로 복사해서 읽는다.
		MOU::PacketHeader Header{};
		FMemory::Memcpy(&Header, Buffer.GetData(), HeaderSize);

		// 2. 선언된 길이를 믿기 전에 상한부터 검사한다.
		//    이 검사가 없으면 위조된 BodySize(예: 999999) 하나로 메모리를 폭발시킬 수 있다.
		//    서버 쪽에서 TestClient 의 "bad" 테스트로 검증한 것과 같은 방어다.
		if (Header.BodySize > MOU::kMaxBodySize)
		{
			return EFrameResult::Malformed;
		}

		// 3. 바디가 덜 왔으면 더 기다린다. 지금까지 받은 건 절대 버리지 않는다.
		const int32 Total = HeaderSize + static_cast<int32>(Header.BodySize);
		if (Buffer.Num() < Total)
		{
			return EFrameResult::NeedMore;
		}

		// 4. 딱 한 패킷만 꺼내고 그만큼만 지운다.
		//    뒤에 남은 바이트는 다음 패킷의 일부이므로 보존해야 한다.
		OutHeader = Header;
		OutBody.Reset();
		OutBody.Append(Buffer.GetData() + HeaderSize, static_cast<int32>(Header.BodySize));

		// EAllowShrinking::No : 매 패킷마다 배열을 재할당하지 않게 한다.
		// 채팅은 초당 수십 패킷이 오갈 수 있어서 재할당 비용이 누적된다.
		Buffer.RemoveAt(0, Total, EAllowShrinking::No);

		return EFrameResult::Ok;
	}

	bool BuildPacket(TArray<uint8>& OutBytes, MOU::EOpcode Opcode,
	                 const void* BodyA, uint32 SizeA,
	                 const void* BodyB, uint32 SizeB)
	{
		OutBytes.Reset();

		const uint32 Total = SizeA + SizeB;
		if (Total > MOU::kMaxBodySize)
		{
			// 여기서 막지 않으면 서버가 Malformed 로 판단해 연결을 끊는다.
			UE_LOG(LogMOUServer, Warning, TEXT("BuildPacket: 바디 %u 바이트가 상한 %u 를 초과. 전송을 포기한다."),
				Total, MOU::kMaxBodySize);
			return false;
		}

		MOU::PacketHeader Header{};
		Header.BodySize = Total;
		Header.Opcode   = static_cast<uint16>(Opcode);

		OutBytes.Reserve(static_cast<int32>(sizeof(Header) + Total));
		OutBytes.Append(reinterpret_cast<const uint8*>(&Header), static_cast<int32>(sizeof(Header)));

		if (SizeA > 0 && BodyA != nullptr)
		{
			OutBytes.Append(reinterpret_cast<const uint8*>(BodyA), static_cast<int32>(SizeA));
		}
		if (SizeB > 0 && BodyB != nullptr)
		{
			OutBytes.Append(reinterpret_cast<const uint8*>(BodyB), static_cast<int32>(SizeB));
		}

		return true;
	}

	int32 EncodeUtf8Clamped(const FString& Src, int32 MaxBytes, TArray<uint8>& OutBytes)
	{
		OutBytes.Reset();
		if (MaxBytes <= 0 || Src.IsEmpty())
		{
			return 0;
		}

		// FTCHARToUTF8 은 UTF-16(TCHAR) -> UTF-8 변환기다.
		// Length() 는 "글자 수" 가 아니라 "바이트 수" 를 돌려준다. 프로토콜의 TextLen 도 바이트 수다.
		FTCHARToUTF8 Converted(*Src);
		const uint8* Bytes = reinterpret_cast<const uint8*>(Converted.Get());
		int32 Count = FMath::Min(Converted.Length(), MaxBytes);

		// UTF-8 은 가변 길이라 아무 데서나 자르면 안 된다.
		// 이어지는 바이트는 상위 2비트가 10 (0x80~0xBF) 이므로,
		// 자를 위치가 그런 바이트면 문자 시작점까지 되돌린다.
		// 이 처리가 없으면 "안녕하세" 뒤에 깨진 조각이 붙어 서버 콘솔과 다른 클라 UI 가 같이 깨진다.
		while (Count > 0 && Count < Converted.Length() && (Bytes[Count] & 0xC0) == 0x80)
		{
			--Count;
		}

		OutBytes.Append(Bytes, Count);
		return Count;
	}

	int32 GetUtf8Length(const FString& Src)
	{
		return FTCHARToUTF8(*Src).Length();
	}

	void CopyFixedString(char* Dest, int32 DestSize, const FString& Src)
	{
		if (Dest == nullptr || DestSize <= 0)
		{
			return;
		}

		// 마지막 1바이트는 널 종료용으로 남긴다.
		TArray<uint8> Encoded;
		const int32 Count = EncodeUtf8Clamped(Src, DestSize - 1, Encoded);

		FMemory::Memzero(Dest, DestSize);
		if (Count > 0)
		{
			FMemory::Memcpy(Dest, Encoded.GetData(), Count);
		}
	}

	FString ReadFixedString(const char* Src, int32 MaxSize)
	{
		if (Src == nullptr || MaxSize <= 0)
		{
			return FString();
		}

		// 서버가 널 종료를 빼먹었더라도 배열 밖으로 나가지 않도록 MaxSize 로 제한한다.
		int32 Len = 0;
		while (Len < MaxSize && Src[Len] != '\0')
		{
			++Len;
		}

		return Utf8ToString(reinterpret_cast<const uint8*>(Src), Len);
	}

	FString Utf8ToString(const uint8* Src, int32 Len)
	{
		if (Src == nullptr || Len <= 0)
		{
			return FString();
		}

		// UTF8_TO_TCHAR 는 널 종료 문자열을 요구하므로, 길이만 아는 바이트열은
		// 임시 버퍼에 복사한 뒤 널을 붙여서 넘긴다.
		TArray<ANSICHAR> Temp;
		Temp.Reserve(Len + 1);
		Temp.Append(reinterpret_cast<const ANSICHAR*>(Src), Len);
		Temp.Add('\0');

		return FString(UTF8_TO_TCHAR(Temp.GetData()));
	}
}

void MOUChat::ReadHostCandidates(const MOU::HostCandidate* Src, int32 Count,
                                 TArray<FMOUHostCandidate>& OutCandidates)
{
	OutCandidates.Reset();

	if (Src == nullptr || Count <= 0)
	{
		return;
	}

	// 서버가 보낸 개수를 그대로 믿지 않는다. 배열 크기를 넘으면 그 앞까지만 읽는다.
	const int32 SafeCount = FMath::Min(Count, static_cast<int32>(MOU::kMaxHostCandidates));

	for (int32 Index = 0; Index < SafeCount; ++Index)
	{
		const MOU::HostCandidate& In = Src[Index];

		FMOUHostCandidate Out;
		Out.Address = ReadFixedString(In.Address, static_cast<int32>(MOU::kMaxAddressLen));
		Out.Port    = static_cast<int32>(In.Port);
		Out.Kind    = static_cast<EMOUHostAddrKindBP>(In.Kind);

		// 쓰레기 항목은 버린다. 이것이 없으면 빈 주소로 ClientTravel 을 시도하고,
		// 실패 사유가 "주소가 이상하다" 가 아니라 엉뚱한 네트워크 오류로 나온다.
		if (Out.IsValid())
		{
			OutCandidates.Add(MoveTemp(Out));
		}
	}
}

FMOUGameRelayRoute MOUChat::ReadRelayHostRoute(const MOU::RelayHostRoute& Src)
{
	FMOUGameRelayRoute Out;
	Out.Address = ReadFixedString(Src.Address, static_cast<int32>(MOU::kMaxAddressLen));
	Out.Port    = static_cast<int32>(Src.HostPort);
	Out.RouteId = Src.RouteId;
	Out.Token.Append(Src.HostToken, static_cast<int32>(MOU::kRelayTokenBytes));
	return Out;
}

FMOUGameRelayRoute MOUChat::ReadRelayGuestRoute(const MOU::RelayGuestRoute& Src)
{
	FMOUGameRelayRoute Out;
	Out.Address = ReadFixedString(Src.Address, static_cast<int32>(MOU::kMaxAddressLen));
	Out.Port    = static_cast<int32>(Src.GuestPort);
	Out.RouteId = Src.RouteId;
	Out.Token.Append(Src.GuestToken, static_cast<int32>(MOU::kRelayTokenBytes));
	return Out;
}
