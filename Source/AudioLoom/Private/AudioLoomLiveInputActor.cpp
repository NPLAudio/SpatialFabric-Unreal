// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#include "AudioLoomLiveInputActor.h"
#include "AudioLoomLiveInputComponent.h"
#include "Components/SceneComponent.h"

AAudioLoomLiveInputActor::AAudioLoomLiveInputActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	LiveInputComponent = CreateDefaultSubobject<UAudioLoomLiveInputComponent>(TEXT("LiveInputComponent"));
}

#if WITH_EDITOR
FString AAudioLoomLiveInputActor::GetDefaultActorLabel() const
{
	return TEXT("Audio Loom Live Input");
}
#endif
