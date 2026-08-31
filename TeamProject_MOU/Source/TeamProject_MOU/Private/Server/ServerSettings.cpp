#include "Server/ServerSettings.h"

#include "Server/Chat/ChatTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"

namespace
{
	/**
	 * ini 가 통째로 없을 때만 쓰이는 최후의 값.
	 *
	 * 여기 127.0.0.1 이 남아 있는 이유는 "설정이 하나도 없어도 혼자 테스트는 되게" 하기
	 * 위해서다. 정상 경로에서는 DefaultGame.ini 의 값이 항상 이겨서 여기까지 오지 않는다.
	 */
	const TCHAR* const kFallbackHost = TEXT("127.0.0.1");
	constexpr int32    kFallbackPort = 9000;

	bool IsValidPort(int32 Port)
	{
		return Port > 0 && Port <= 65535;
	}

	/**
	 * GConfig 로 직접 읽고 쓸 때 필요한 섹션 이름.
	 * UObject 의 config 로딩이 쓰는 규칙(클래스 경로)과 같아서 항상 일치한다.
	 *   -> "/Script/TeamProject_MOU.MOUServerSettings"
	 */
	FString GetConfigSection()
	{
		return UMOUServerSettings::StaticClass()->GetPathName();
	}
}

UMOUServerSettings::UMOUServerSettings()
	: ServerHost(kFallbackHost)
	, ServerPort(kFallbackPort)
	// 기본값이 자체 서버인 이유: 외부 SDK 도 Epic 계정도 인터넷도 없이 바로 돌아간다.
	// EOS 는 붙이는 순간 팀 전원이 Dev Portal 설정을 공유해야 하므로 기본값이 될 수 없다.
	, LobbyBackend(EMOULobbyBackendType::CustomSocket)
	// 60초. 큰 맵을 저사양 PC 에서 여는 최악의 경우를 넉넉히 덮는 값이다.
	// 정상적인 경우 이 값은 쓰이지 않는다 — 리슨서버가 뜨는 즉시 신호가 나간다.
	, HostReadyTimeoutSeconds(60.f)
	// 기본이 true 인 이유는 헤더 주석 참고 — 끄면 다른 네트워크 방장의 7777 을
	// 아무도 열어주지 않아 참여자가 못 들어온다.
	, bUseUpnpPortMapping(true)
{
}

FName UMOUServerSettings::GetCategoryName() const
{
	return TEXT("Game");
}

void UMOUServerSettings::ResolveEndpoint(FString& OutHost, int32& OutPort, FString* OutSource)
{
	// 1) ini 계층(Base -> Default -> Saved) 이 이미 병합된 값. 개인 설정이 있으면 그것이 들어있다.
	const UMOUServerSettings* Settings = GetDefault<UMOUServerSettings>();
	OutHost = Settings->ServerHost;
	OutPort = Settings->ServerPort;

	FString Source = TEXT("설정 파일");

	// ini 가 없거나 값이 깨져 있어도 게임이 죽지 않게 막아둔다.
	if (OutHost.IsEmpty())
	{
		OutHost = kFallbackHost;
		Source  = TEXT("기본값");
	}
	if (!IsValidPort(OutPort))
	{
		OutPort = kFallbackPort;
	}

	// 2) 실행 인자가 가장 세다. 파일을 하나도 건드리지 않고 이번 실행만 바꾸는 수단.
	const TCHAR* CmdLine = FCommandLine::Get();

	FString Combined;
	if (FParse::Value(CmdLine, TEXT("MOUServer="), Combined))
	{
		Combined.TrimStartAndEndInline();

		// "호스트:포트" 와 "호스트" 둘 다 받는다.
		FString HostPart, PortPart;
		if (Combined.Split(TEXT(":"), &HostPart, &PortPart))
		{
			if (!HostPart.IsEmpty())
			{
				OutHost = HostPart;
				Source  = TEXT("실행 인자 -MOUServer");
			}
			const int32 ParsedPort = FCString::Atoi(*PortPart);
			if (IsValidPort(ParsedPort))
			{
				OutPort = ParsedPort;
			}
		}
		else if (!Combined.IsEmpty())
		{
			OutHost = Combined;
			Source  = TEXT("실행 인자 -MOUServer");
		}
	}

	FString HostSwitch;
	if (FParse::Value(CmdLine, TEXT("MOUChatHost="), HostSwitch))
	{
		HostSwitch.TrimStartAndEndInline();
		if (!HostSwitch.IsEmpty())
		{
			OutHost = HostSwitch;
			Source  = TEXT("실행 인자 -MOUChatHost");
		}
	}

	int32 PortSwitch = 0;
	if (FParse::Value(CmdLine, TEXT("MOUChatPort="), PortSwitch) && IsValidPort(PortSwitch))
	{
		OutPort = PortSwitch;
	}

	OutHost.TrimStartAndEndInline();

	if (OutSource != nullptr)
	{
		*OutSource = Source;
	}
}

EMOULobbyBackendType UMOUServerSettings::ResolveBackendType(FString* OutSource)
{
	const UMOUServerSettings* Settings = GetDefault<UMOUServerSettings>();
	EMOULobbyBackendType Type = Settings->LobbyBackend;
	FString Source = TEXT("설정 파일");

	// 실행 인자가 가장 세다. 팀원 한 명만 EOS 로 테스트해보는 상황을 위해 열어둔다.
	FString Switch;
	if (FParse::Value(FCommandLine::Get(), TEXT("MOULobbyBackend="), Switch))
	{
		Switch.TrimStartAndEndInline();
		if (Switch.Equals(TEXT("EOS"), ESearchCase::IgnoreCase))
		{
			Type   = EMOULobbyBackendType::EOS;
			Source = TEXT("실행 인자 -MOULobbyBackend");
		}
		else if (Switch.Equals(TEXT("Socket"), ESearchCase::IgnoreCase)
			|| Switch.Equals(TEXT("CustomSocket"), ESearchCase::IgnoreCase))
		{
			Type   = EMOULobbyBackendType::CustomSocket;
			Source = TEXT("실행 인자 -MOULobbyBackend");
		}
		else
		{
			// 오타를 조용히 넘기면 "왜 EOS 로 안 바뀌지" 를 한참 헤매게 된다.
			UE_LOG(LogMOUServer, Warning,
				TEXT("-MOULobbyBackend=%s 를 알아듣지 못했다. 쓸 수 있는 값: EOS, Socket"), *Switch);
		}
	}

	if (OutSource != nullptr)
	{
		*OutSource = Source;
	}
	return Type;
}

float UMOUServerSettings::GetHostReadyTimeoutSeconds()
{
	const float Configured = GetDefault<UMOUServerSettings>()->HostReadyTimeoutSeconds;

	// ini 를 손으로 고치다 0 이나 음수가 들어가면 시작하자마자 시간 초과가 된다.
	// 그런 값은 설정이 아니라 사고이므로 무시한다.
	return (Configured > 0.f) ? Configured : 60.f;
}

bool UMOUServerSettings::ShouldUseUpnp()
{
	// 실행 인자가 설정 파일을 이긴다. 주소·백엔드와 같은 규칙이다 —
	// "내 PC 에서 이번 한 번만" 을 파일을 건드리지 않고 할 수 있어야 한다.
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("MOUUseUpnp="), Override))
	{
		return Override != TEXT("0") && !Override.Equals(TEXT("false"), ESearchCase::IgnoreCase);
	}

	return GetDefault<UMOUServerSettings>()->bUseUpnpPortMapping;
}

FString UMOUServerSettings::GetResolvedServerHost()
{
	FString Host;
	int32   Port = 0;
	ResolveEndpoint(Host, Port);
	return Host;
}

int32 UMOUServerSettings::GetResolvedServerPort()
{
	FString Host;
	int32   Port = 0;
	ResolveEndpoint(Host, Port);
	return Port;
}

FString UMOUServerSettings::GetResolvedEndpointText()
{
	FString Host;
	int32   Port = 0;
	FString Source;
	ResolveEndpoint(Host, Port, &Source);
	return FString::Printf(TEXT("%s:%d (%s)"), *Host, Port, *Source);
}

void UMOUServerSettings::SaveEndpointOverrideForThisMachine(const FString& InHost, int32 InPort)
{
	if (GConfig == nullptr)
	{
		return;
	}

	const FString Section = GetConfigSection();

	FString Host = InHost;
	Host.TrimStartAndEndInline();
	if (!Host.IsEmpty())
	{
		GConfig->SetString(*Section, TEXT("ServerHost"), *Host, GGameIni);
	}
	if (IsValidPort(InPort))
	{
		GConfig->SetInt(*Section, TEXT("ServerPort"), InPort, GGameIni);
	}

	// GGameIni 는 Saved/Config/<플랫폼>/Game.ini 로 흘러간다. Saved/ 는 .gitignore 대상이라
	// 여기에 쓴 값은 이 PC 밖으로 나가지 않는다.
	GConfig->Flush(false, GGameIni);

	// 지금 돌고 있는 세션에도 즉시 반영한다. (다음 접속부터 적용)
	GetMutableDefault<UMOUServerSettings>()->ReloadConfig();

	UE_LOG(LogMOUServer, Log, TEXT("서버 주소를 이 PC 에만 저장했다: %s"), *GetResolvedEndpointText());
}

void UMOUServerSettings::ClearEndpointOverrideForThisMachine()
{
	if (GConfig == nullptr)
	{
		return;
	}

	const FString Section = GetConfigSection();
	GConfig->RemoveKey(*Section, TEXT("ServerHost"), GGameIni);
	GConfig->RemoveKey(*Section, TEXT("ServerPort"), GGameIni);
	GConfig->Flush(false, GGameIni);

	GetMutableDefault<UMOUServerSettings>()->ReloadConfig();

	UE_LOG(LogMOUServer, Log, TEXT("개인 서버 설정을 지웠다. 팀 공유 설정으로 돌아간다: %s"),
		*GetResolvedEndpointText());
}
