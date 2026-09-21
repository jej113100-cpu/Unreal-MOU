// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "MOU_GameUserSettings.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnMOUAudioSettingsChanged);
DECLARE_MULTICAST_DELEGATE(FOnMOUControlSettingsChanged);

/**
 * 프로젝트 전용 게임 사용자 설정 클래스.
 * 엔진의 UGameUserSettings를 확장하여 그래픽(스케일러빌리티/해상도/화면모드/VSync/FPS제한),
 * 오디오(Master, BGM, SFX, Voice, 마이크 감도), 조작(마우스 감도, Y축 반전, FOV, 키 리매핑)을
 * GameUserSettings.ini 한 곳에 일괄 영구 저장 및 관리합니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API UMOU_GameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	UMOU_GameUserSettings();

	/** 싱글톤 인스턴스 헬퍼 */
	UFUNCTION(BlueprintPure, Category = "Settings")
	static UMOU_GameUserSettings* GetMOUGameUserSettings();

	virtual void SetToDefaults() override;
	virtual void ApplyNonResolutionSettings() override;

	// =========================================================================
	// [오디오 설정]
	// =========================================================================

	UFUNCTION(BlueprintPure, Category = "Settings|Audio")
	float GetMasterVolume() const { return MasterVolume; }

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetMasterVolume(float InVolume);

	UFUNCTION(BlueprintPure, Category = "Settings|Audio")
	float GetBGMVolume() const { return BGMVolume; }

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetBGMVolume(float InVolume);

	UFUNCTION(BlueprintPure, Category = "Settings|Audio")
	float GetSFXVolume() const { return SFXVolume; }

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetSFXVolume(float InVolume);

	UFUNCTION(BlueprintPure, Category = "Settings|Audio")
	float GetVoiceVolume() const { return VoiceVolume; }

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetVoiceVolume(float InVolume);

	UFUNCTION(BlueprintPure, Category = "Settings|Audio")
	float GetMicSensitivity() const { return MicSensitivity; }

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetMicSensitivity(float InSensitivity);

	/** 오디오 시스템 및 서브시스템에 볼륨 설정 적용 */
	UFUNCTION(BlueprintCallable, Category = "Settings|Audio", meta = (WorldContext = "WorldContextObject"))
	void ApplyAudioSettings(UObject* WorldContextObject);

	FOnMOUAudioSettingsChanged OnAudioSettingsChanged;

	// =========================================================================
	// [조작 및 화면 설정]
	// =========================================================================

	UFUNCTION(BlueprintPure, Category = "Settings|Controls")
	float GetMouseSensitivity() const { return MouseSensitivity; }

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetMouseSensitivity(float InSensitivity);

	UFUNCTION(BlueprintPure, Category = "Settings|Controls")
	bool GetInvertY() const { return bInvertY; }

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetInvertY(bool bInInvert);

	UFUNCTION(BlueprintPure, Category = "Settings|Controls")
	float GetFieldOfView() const { return FieldOfView; }

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetFieldOfView(float InFOV);

	FOnMOUControlSettingsChanged OnControlSettingsChanged;

	// =========================================================================
	// [키 바인딩 설정]
	// =========================================================================

	UFUNCTION(BlueprintCallable, Category = "Settings|Input")
	void SetCustomKeyBinding(FName ActionName, const FKey& Key);

	UFUNCTION(BlueprintPure, Category = "Settings|Input")
	FKey GetCustomKeyBinding(FName ActionName, const FKey& DefaultKey) const;

	UFUNCTION(BlueprintPure, Category = "Settings|Input")
	const TMap<FName, FKey>& GetAllCustomKeyBindings() const { return CustomKeyBindings; }

	UFUNCTION(BlueprintCallable, Category = "Settings|Input")
	void ClearAllCustomKeyBindings();

protected:
	// --- 오디오 저장 변수 ---
	UPROPERTY(Config)
	float MasterVolume = 1.0f;

	UPROPERTY(Config)
	float BGMVolume = 0.8f;

	UPROPERTY(Config)
	float SFXVolume = 1.0f;

	UPROPERTY(Config)
	float VoiceVolume = 1.0f;

	UPROPERTY(Config)
	float MicSensitivity = 0.05f;

	// --- 조작 저장 변수 ---
	UPROPERTY(Config)
	float MouseSensitivity = 1.0f;

	UPROPERTY(Config)
	bool bInvertY = false;

	UPROPERTY(Config)
	float FieldOfView = 90.0f;

	// --- 키 바인딩 저장 (ActionName -> Key) ---
	UPROPERTY(Config)
	TMap<FName, FKey> CustomKeyBindings;
};
