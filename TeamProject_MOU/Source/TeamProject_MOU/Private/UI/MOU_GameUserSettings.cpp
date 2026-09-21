// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/MOU_GameUserSettings.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Voice/VoiceSubsystem.h"

UMOU_GameUserSettings::UMOU_GameUserSettings()
{
	MasterVolume = 1.0f;
	BGMVolume = 0.8f;
	SFXVolume = 1.0f;
	VoiceVolume = 1.0f;
	MicSensitivity = 0.05f;

	MouseSensitivity = 1.0f;
	bInvertY = false;
	FieldOfView = 90.0f;
}

UMOU_GameUserSettings* UMOU_GameUserSettings::GetMOUGameUserSettings()
{
	return Cast<UMOU_GameUserSettings>(UGameUserSettings::GetGameUserSettings());
}

void UMOU_GameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();

	MasterVolume = 1.0f;
	BGMVolume = 0.8f;
	SFXVolume = 1.0f;
	VoiceVolume = 1.0f;
	MicSensitivity = 0.05f;

	MouseSensitivity = 1.0f;
	bInvertY = false;
	FieldOfView = 90.0f;

	CustomKeyBindings.Empty();
}

void UMOU_GameUserSettings::ApplyNonResolutionSettings()
{
	Super::ApplyNonResolutionSettings();

	if (GEngine)
	{
		ApplyAudioSettings(GEngine->GetWorld());
	}

	OnAudioSettingsChanged.Broadcast();
	OnControlSettingsChanged.Broadcast();
}

void UMOU_GameUserSettings::SetMasterVolume(float InVolume)
{
	MasterVolume = FMath::Clamp(InVolume, 0.0f, 1.0f);
	if (GEngine)
	{
		ApplyAudioSettings(GEngine->GetWorld());
	}
	OnAudioSettingsChanged.Broadcast();
}

void UMOU_GameUserSettings::SetBGMVolume(float InVolume)
{
	BGMVolume = FMath::Clamp(InVolume, 0.0f, 1.0f);
	OnAudioSettingsChanged.Broadcast();
}

void UMOU_GameUserSettings::SetSFXVolume(float InVolume)
{
	SFXVolume = FMath::Clamp(InVolume, 0.0f, 1.0f);
	OnAudioSettingsChanged.Broadcast();
}

void UMOU_GameUserSettings::SetVoiceVolume(float InVolume)
{
	VoiceVolume = FMath::Clamp(InVolume, 0.0f, 1.0f);
	OnAudioSettingsChanged.Broadcast();
}

void UMOU_GameUserSettings::SetMicSensitivity(float InSensitivity)
{
	MicSensitivity = FMath::Clamp(InSensitivity, 0.0f, 1.0f);
	if (GEngine)
	{
		if (UVoiceSubsystem* VoiceSubsystem = UVoiceSubsystem::Get(GEngine->GetWorld()))
		{
			VoiceSubsystem->SetMicSensitivity(MicSensitivity);
		}
	}
	OnAudioSettingsChanged.Broadcast();
}

void UMOU_GameUserSettings::ApplyAudioSettings(UObject* WorldContextObject)
{
	// 1. 엔진 메인 오디오 디바이스 마스터 볼륨 반영
	if (GEngine)
	{
		if (FAudioDeviceManager* DeviceManager = GEngine->GetAudioDeviceManager())
		{
			if (FAudioDevice* AudioDevice = DeviceManager->GetActiveAudioDevice().GetAudioDevice())
			{
				AudioDevice->SetTransientPrimaryVolume(MasterVolume);
			}
		}
	}

	// 2. 보이스 서브시스템 마이크 감도 반영
	if (WorldContextObject)
	{
		if (UVoiceSubsystem* VoiceSubsystem = UVoiceSubsystem::Get(WorldContextObject))
		{
			VoiceSubsystem->SetMicSensitivity(MicSensitivity);
		}
	}
}

void UMOU_GameUserSettings::SetMouseSensitivity(float InSensitivity)
{
	MouseSensitivity = FMath::Clamp(InSensitivity, 0.1f, 3.0f);
	OnControlSettingsChanged.Broadcast();
}

void UMOU_GameUserSettings::SetInvertY(bool bInInvert)
{
	bInvertY = bInInvert;
	OnControlSettingsChanged.Broadcast();
}

void UMOU_GameUserSettings::SetFieldOfView(float InFOV)
{
	FieldOfView = FMath::Clamp(InFOV, 70.0f, 110.0f);
	OnControlSettingsChanged.Broadcast();
}

void UMOU_GameUserSettings::SetCustomKeyBinding(FName ActionName, const FKey& Key)
{
	if (!ActionName.IsNone())
	{
		CustomKeyBindings.FindOrAdd(ActionName) = Key;
		OnControlSettingsChanged.Broadcast();
	}
}

FKey UMOU_GameUserSettings::GetCustomKeyBinding(FName ActionName, const FKey& DefaultKey) const
{
	if (const FKey* Found = CustomKeyBindings.Find(ActionName))
	{
		return *Found;
	}
	return DefaultKey;
}

void UMOU_GameUserSettings::ClearAllCustomKeyBindings()
{
	CustomKeyBindings.Empty();
	OnControlSettingsChanged.Broadcast();
}
