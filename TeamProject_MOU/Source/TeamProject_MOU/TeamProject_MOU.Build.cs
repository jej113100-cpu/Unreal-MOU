// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class TeamProject_MOU : ModuleRules
{
    public TeamProject_MOU(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            "AIModule",
            "NavigationSystem",
            "StateTreeModule",
            "GameplayStateTreeModule",
            "UMG",
            "Slate",
            "SlateCore",
            "GameplayAbilities",
            "GameplayTags",
            "GameplayTasks",
            "Niagara",
            "Sockets",
            "Networking",

            // UDP 홀펀칭 (v10) — UIpNetDriver 를 상속해 클라이언트 바인드 포트를 고정한다.
            // 기본 구현은 GetClientPort() 가 0(임시 포트)을 돌려주는데, 그러면 그 번호를
            // 아무도 미리 알 수 없어서 방장이 punch 할 대상을 정할 수 없다.
            "OnlineSubsystemUtils",

            // 채팅 서버 주소를 Project Settings 에 노출한다 (Server/ServerSettings.h).
            "DeveloperSettings",

            // --- 음성 시스템 (VOICE_INTEGRATION.md 4절) ------------------
            // 마이크 캡처와 Opus 코덱은 엔진 것을 그대로 쓴다.
            // 직접 만드는 것은 그 위의 라우팅/재생뿐이다.
            "Voice",             // IVoiceCapture / IVoiceEncoder / IVoiceDecoder
            "AudioMixer",        // USynthComponent (음성 재생의 출구)
            "SignalProcessing",  // Audio::TCircularAudioBuffer (게임 스레드 -> 오디오 렌더 스레드)
            "AudioExtensions"    // 소스 이펙트 체인 (무전기 필터)
        });

        PrivateDependencyModuleNames.AddRange(new string[] { });

        PublicIncludePaths.AddRange(new string[] {
            "TeamProject_MOU"
        });

        PublicIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "MOU_Server", "Shared")));

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}
