// Copyright (c) 2026 AudioLoom Contributors.

/**
 * @file AudioLoomLiveInputActor.h
 * @brief Placeable actor wrapping one `UAudioLoomLiveInputComponent` (same as adding the component).
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AudioLoomLiveInputActor.generated.h"

class UAudioLoomLiveInputComponent;

UCLASS(BlueprintType, meta = (DisplayName = "Audio Loom Live Input"))
class AUDIOLOOM_API AAudioLoomLiveInputActor : public AActor
{
	GENERATED_BODY()

public:
	AAudioLoomLiveInputActor();

	/** Never starts on its own: call StartLive, press Start in the details panel, or send OSC /play. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AudioLoom")
	TObjectPtr<UAudioLoomLiveInputComponent> LiveInputComponent;

#if WITH_EDITOR
	virtual FString GetDefaultActorLabel() const override;
#endif
};
