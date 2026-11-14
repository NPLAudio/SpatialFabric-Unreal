// Copyright (c) 2026 AudioLoom Contributors.

/**
 * @file AudioLoomOscSettings.h
 * @brief `UDeveloperSettings` persisted in **Config/Engine** — listen port and monitoring destination.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AudioLoomOscSettings.generated.h"

/**
 * Persistent playback OSC configuration (Project Settings → Plugins → Spatial Fabric Playback OSC).
 * Config remains Engine, under [/Script/AudioLoom.AudioLoomOscSettings], for compatibility.
 */
UCLASS(Config = Engine, DefaultConfig, meta = (DisplayName = "Spatial Fabric Playback OSC"))
class AUDIOLOOM_API UAudioLoomOscSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAudioLoomOscSettings();

	/** UDP port for OSC server (receiving triggers). Default 9000. */
	UPROPERTY(Config, EditAnywhere, Category = "OSC")
	int32 ListenPort = 9000;

	UPROPERTY(Config, EditAnywhere, Category = "OSC")
	FString ListenIP = TEXT("127.0.0.1");

	UPROPERTY(Config, EditAnywhere, Category = "OSC")
	bool bEnableInEditor = true;

	UPROPERTY(Config, EditAnywhere, Category = "OSC")
	bool bEnableInPackagedBuilds = false;

	/** Target IP for OSC monitoring (sending play/stop state). */
	UPROPERTY(Config, EditAnywhere, Category = "OSC")
	FString SendIP = TEXT("127.0.0.1");

	/** Target port for OSC monitoring. */
	UPROPERTY(Config, EditAnywhere, Category = "OSC")
	int32 SendPort = 9001;

	virtual FName GetCategoryName() const override;
};
