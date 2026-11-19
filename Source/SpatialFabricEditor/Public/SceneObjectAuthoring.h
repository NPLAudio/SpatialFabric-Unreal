// Copyright (c) 2026 SpatialFabric Contributors. Licensed under the MIT License.
#pragma once

#include "CoreMinimal.h"

class AActor;
class ASpatialFabricManagerActor;
class UWorld;
class UActorComponent;

/** Editor-only, side-effect-free discovery and explicit transactional authoring. */
namespace SceneObjectAuthoring
{
	SPATIALFABRICEDITOR_API UWorld* EditorWorld();
	SPATIALFABRICEDITOR_API void GatherActors(UWorld* World, TArray<TWeakObjectPtr<AActor>>& Out);
	SPATIALFABRICEDITOR_API bool CanEdit(const AActor* Actor, UWorld* World, FText& Reason);
	SPATIALFABRICEDITOR_API bool CanEditComponent(const UActorComponent* Component);
	/** Preflights every target before modifying any; existing components are skipped. */
	SPATIALFABRICEDITOR_API bool AddComponents(const TArray<TWeakObjectPtr<AActor>>& Actors, UClass* ComponentClass, FText& Result);
	SPATIALFABRICEDITOR_API bool BindActors(const TArray<TWeakObjectPtr<AActor>>& Actors, ASpatialFabricManagerActor* Manager, FText& Result);
}
