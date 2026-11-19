// Copyright (c) 2026 SpatialFabric Contributors. Licensed under the MIT License.
#include "SceneObjectStatus.h"
#include "SceneObjectAuthoring.h"
#include "AudioLoomComponent.h"
#include "AudioLoomLiveInputComponent.h"
#include "SpatialFabricManagerActor.h"
#include "SpatialStageVolume.h"

SceneObjectStatus::FCache SceneObjectStatus::Build(const TArray<TWeakObjectPtr<AActor>>& Actors, UWorld* World)
{
	TMap<TWeakObjectPtr<AActor>, int32> BindingCounts;
	TSet<TWeakObjectPtr<AActor>> Managers;
	for (const auto& Weak : Actors)
	{
		const auto* Manager = Cast<ASpatialFabricManagerActor>(Weak.Get());
		if (!Manager || Managers.Contains(Weak)) continue;
		Managers.Add(Weak);
		for (const auto& Binding : Manager->ObjectBindings)
			if (AActor* Target = Binding.TargetActor.Get()) ++BindingCounts.FindOrAdd(Target);
	}
	FCache Cache;
	for (const auto& Weak : Actors)
	{
		AActor* Actor = Weak.Get();
		if (!Actor) continue;
		FActorStatus& Status = Cache.Add(Weak);
		Status.Bindings = BindingCounts.FindRef(Weak);
		TInlineComponentArray<UActorComponent*> Components(Actor);
		for (const auto* Component : Components)
		{
			Status.Playback += Component->IsA<UAudioLoomComponent>();
			Status.LiveInput += Component->IsA<UAudioLoomLiveInputComponent>();
			Status.ComponentSignature += FString::Printf(TEXT("%p"), Component);
		}
		FText Reason;
		const bool bEditable = SceneObjectAuthoring::CanEdit(Actor, World, Reason);
		Status.Text = FText::FromString(FString::Printf(TEXT("Playback: %d | Live Input: %d | SF bindings: %d%s%s"), Status.Playback, Status.LiveInput, Status.Bindings,
			Actor->IsA<ASpatialFabricManagerActor>() || Actor->IsA<ASpatialStageVolume>() ? TEXT(" | SF settings") : !Status.Playback && !Status.LiveInput && !Status.Bindings ? TEXT(" | Missing configuration") : TEXT(" | Configured"),
			bEditable ? TEXT("") : TEXT(" | Read-only")));
	}
	return Cache;
}
