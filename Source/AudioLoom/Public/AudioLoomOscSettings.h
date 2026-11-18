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

	/** Per-source decoded float PCM budget. Raise deliberately for long installation cues.
	 * The default remains 256 MiB; array/index limits are enforced even when increased. */
	UPROPERTY(Config, EditAnywhere, Category = "Playback", meta = (ClampMin = "1", ClampMax = "8191", UIMin = "1", UIMax = "8191"))
	int32 MaxDecodedAudioMiB = 256;

	/** Compatibility with standalone SoundWave deployments containing a sibling .wav/.WAV.
	 * Files are only looked up beside the resolved package; they must be staged separately. */
	UPROPERTY(Config, EditAnywhere, Category = "Playback")
	bool bAllowLegacyWavSidecar = true;

	virtual FName GetCategoryName() const override;
};
