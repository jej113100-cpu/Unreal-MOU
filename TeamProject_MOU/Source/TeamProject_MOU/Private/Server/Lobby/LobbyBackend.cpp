// MOU 로비 - 백엔드 팩토리.
//
// 백엔드 종류를 실제 클래스로 바꾸는 곳은 여기 한 군데뿐이다.
// 새 백엔드(예: Steam)를 붙일 때 고쳐야 할 파일도 여기 하나다.

#include "Server/Lobby/LobbyBackend.h"

#include "Server/Lobby/EOSLobbyBackend.h"
#include "Server/Lobby/SocketLobbyBackend.h"

namespace MOULobbyBackend
{
	TUniquePtr<ILobbyBackend> Create(EMOULobbyBackendType Type)
	{
		switch (Type)
		{
		case EMOULobbyBackendType::EOS:
			return MakeUnique<FEOSLobbyBackend>();

		case EMOULobbyBackendType::CustomSocket:
		default:
			return MakeUnique<FSocketLobbyBackend>();
		}
	}

	FString GetTypeName(EMOULobbyBackendType Type)
	{
		switch (Type)
		{
		case EMOULobbyBackendType::EOS: return TEXT("EOS");
		default:                        return TEXT("자체 서버(TCP)");
		}
	}
}
