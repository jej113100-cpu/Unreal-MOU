// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TeamProject_MOUPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class ULoginWidgetBase;
class UVoiceComponent;

/**
 *  Basic PlayerController class for a third person game
 *  Manages input mappings
 */
UCLASS(abstract)
class ATeamProject_MOUPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATeamProject_MOUPlayerController();

protected:
	/**
	 * 음성 송수신 창구 (VOICE_INTEGRATION.md 6절).
	 *
	 * ★ 런타임에 붙이지 않고 **생성자에서** 만드는 것이 중요하다.
	 *   RPC 는 보내는 쪽과 받는 쪽에 같은 컴포넌트가 있어야 목적지를 찾는다.
	 *   생성자에서 만들면 서버와 모든 클라이언트가 똑같이 갖게 되지만,
	 *   나중에 붙이면 한쪽에만 있는 순간이 생겨 **RPC 가 조용히 사라진다.**
	 *
	 *   폰이 아니라 컨트롤러에 두는 이유는 VoiceComponent.h 상단 주석에 있다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MOU|Voice")
	TObjectPtr<UVoiceComponent> VoiceComponent;


	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/**
	 * PIE/게임 시작 시 채팅 로그인 화면을 자동으로 띄울지.
	 *
	 * 이미 로그인되어 있으면(예: 방장이 방을 만들고 리슨서버로 여행해온 경우)
	 * 다시 묻지 않는다 — ShowLoginWidgetIfNeeded() 가 UChatSubsystem 의 연결 상태로 판단한다.
	 */
	UPROPERTY(EditAnywhere, Category = "MOU|Chat")
	bool bAutoShowLoginWidget = true;

	/** 자동으로 띄울 로그인 위젯 클래스. 비워두면 ULoginWidgetBase 의 C++ 기본 레이아웃을 쓴다. */
	UPROPERTY(EditAnywhere, Category = "MOU|Chat")
	TSubclassOf<ULoginWidgetBase> LoginWidgetClass;

	/**
	 * 이 컨트롤러만 다른 채팅 서버를 보게 할 때 쓰는 **예외용** 값. 평소에는 비워둔다.
	 *
	 * 비어 있으면 Config/DefaultGame.ini 의 팀 공유 주소(UMOUServerSettings)를 쓴다.
	 * 예전에는 여기에 127.0.0.1 이 박혀 있었는데, 그 값은 "이 게임이 돌고 있는 PC" 라는
	 * 뜻이라 서버를 켜지 않은 팀원은 자기 자신에게 접속하려다 항상 실패했다.
	 * 그래서 기본값을 없애고, 주소를 아는 곳을 설정 한 군데로 모았다.
	 */
	UPROPERTY(EditAnywhere, Category = "MOU|Chat")
	FString ServerHostOverride;

	/** 0 이면 ServerHostOverride 와 마찬가지로 설정값을 쓴다. */
	UPROPERTY(EditAnywhere, Category = "MOU|Chat")
	int32 ServerPortOverride = 0;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

private:
	/** bAutoShowLoginWidget 이 켜져 있고 아직 로그인 전이면 로그인 위젯을 띄운다. */
	void ShowLoginWidgetIfNeeded();

};
