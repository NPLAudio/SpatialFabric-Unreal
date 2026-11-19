// Copyright (c) 2026 SpatialFabric Contributors. Licensed under the MIT License.
#pragma once
#include "CoreMinimal.h"

class AActor;
class UWorld;

namespace SceneObjectStatus
{
struct FActorStatus
{
	int32 Playback = 0;
	int32 LiveInput = 0;
	int32 Bindings = 0;
	FText Text;
	FString ComponentSignature;
};
using FCache = TMap<TWeakObjectPtr<AActor>, FActorStatus>;

/** One binding-index pass, then one component/status pass. Replaces, never accumulates, cached identities. */
FCache Build(const TArray<TWeakObjectPtr<AActor>>& Actors, UWorld* World);
}
