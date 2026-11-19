// Copyright (c) 2026 SpatialFabric Contributors. Licensed under the MIT License.
#include "SceneObjectAuthoring.h"
#include "AudioLoomComponent.h"
#include "AudioLoomLiveInputComponent.h"
#include "SpatialFabricManagerActor.h"
#include "SpatialStageVolume.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/Level.h"
#include "LevelUtils.h"
#include "ScopedTransaction.h"
#include "SubobjectDataSubsystem.h"
#include "SubobjectData.h"
#include "Kismet2/ComponentEditorUtils.h"

#define LOCTEXT_NAMESPACE "SceneObjectAuthoring"
namespace SceneObjectAuthoring
{
UWorld* EditorWorld()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	return World && World->WorldType == EWorldType::Editor ? World : nullptr;
}

void GatherActors(UWorld* World, TArray<TWeakObjectPtr<AActor>>& Out)
{
	Out.Reset();
	if (!World || World->WorldType != EWorldType::Editor) return;
	// No default iterator flags: include hidden actors and actors in inactive levels.
	for (TActorIterator<AActor> It(World, AActor::StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
	{
		AActor* Actor = *It;
		if (IsValid(Actor) && !Actor->IsTemplate() && !Actor->HasAnyFlags(RF_Transient)) Out.Add(Actor);
	}
	Out.Sort([](const TWeakObjectPtr<AActor>& A, const TWeakObjectPtr<AActor>& B)
	{
		return A->GetPathName() < B->GetPathName();
	});
}

bool CanEdit(const AActor* Actor, UWorld* World, FText& Reason)
{
	if (!GEditor || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor || !World || World != EditorWorld())
		Reason = LOCTEXT("EditorOnly", "Authoring requires the editor world, outside PIE/SIE.");
	else if (!IsValid(Actor) || Actor->IsTemplate() || Actor->HasAnyFlags(RF_Transient) || Actor->GetWorld() != World || !Actor->GetLevel())
		Reason = LOCTEXT("PlacedOnly", "Select loaded, placed editor actors (not templates).");
	else if (!Actor->IsEditable() || FLevelUtils::IsLevelLocked(Actor->GetLevel()))
		Reason = LOCTEXT("ReadOnly", "An actor is noneditable or its level is locked; nothing was changed.");
	else return true;
	return false;
}

bool CanEditComponent(const UActorComponent* Component)
{
	FText Reason;
	if (!IsValid(Component) || !CanEdit(Component->GetOwner(), EditorWorld(), Reason) ||
		!FComponentEditorUtils::CanEditComponentInstance(Component, nullptr, false)) return false;
	USubobjectDataSubsystem* Subsystem = USubobjectDataSubsystem::Get();
	if (!Subsystem) return false;
	TArray<FSubobjectDataHandle> Handles;
	Subsystem->GatherSubobjectData(Component->GetOwner(), Handles);
	for (const auto& Handle : Handles)
	{
		const FSubobjectData* Data = Handle.GetData();
		if (Data && Data->GetObject() == Component) return Data->CanEdit();
	}
	return false;
}

bool AddComponents(const TArray<TWeakObjectPtr<AActor>>& Actors, UClass* ComponentClass, FText& Result)
{
	if (ComponentClass != UAudioLoomComponent::StaticClass() && ComponentClass != UAudioLoomLiveInputComponent::StaticClass())
	{
		Result = LOCTEXT("Allowlist", "Only AudioLoom Playback and Live Input can be added here.");
		return false;
	}
	if (Actors.IsEmpty()) { Result = LOCTEXT("Select", "Select actors first."); return false; }
	USubobjectDataSubsystem* Subsystem = USubobjectDataSubsystem::Get();
	if (!Subsystem) { Result = LOCTEXT("NoSubsystem", "The editor subobject subsystem is unavailable."); return false; }
	TArray<FSubobjectDataHandle> Parents;
	TSet<AActor*> Seen;
	for (const auto& Weak : Actors)
	{
		AActor* Actor = Weak.Get();
		if (!CanEdit(Actor, EditorWorld(), Result)) return false;
		if (Seen.Contains(Actor)) continue;
		Seen.Add(Actor);
		if (Actor->FindComponentByClass(ComponentClass)) continue;
		TArray<FSubobjectDataHandle> Handles;
		Subsystem->GatherSubobjectData(Actor, Handles);
		const FSubobjectDataHandle* Parent = Handles.FindByPredicate([Actor](const FSubobjectDataHandle& Handle)
		{
			const FSubobjectData* Data = Handle.GetData();
			return Data && Data->IsActor() && Data->GetObject() == Actor;
		});
		if (!Parent) { Result = LOCTEXT("NoHandle", "Could not resolve an actor-instance subobject handle; nothing changed."); return false; }
		Parents.Add(*Parent);
	}
	if (Parents.IsEmpty()) { Result = LOCTEXT("Duplicates", "All selected actors already have this component."); return true; }
	FScopedTransaction Transaction(LOCTEXT("Add", "Add AudioLoom components to scene actors"));
	int32 Added = 0;
	for (const auto& Parent : Parents)
	{
		const FSubobjectData* Data = Parent.GetData();
		const AActor* Actor = Data ? Data->GetObject<AActor>() : nullptr;
		// A previous actor's construction script can alter another target after preflight.
		if (!CanEdit(Actor, EditorWorld(), Result))
		{
			Result = FText::Format(LOCTEXT("ChangedTarget", "A target changed during addition: {0}. Undo reverts any earlier additions."), Result);
			return false;
		}
		if (Actor->FindComponentByClass(ComponentClass)) continue;
		FAddNewSubobjectParams Params;
		Params.ParentHandle = Parent;
		Params.NewClass = ComponentClass;
		if (!Subsystem->AddNewSubobject(Params, Result).IsValid())
		{
			// Engine failures after preflight can leave earlier additions; keep them undoable.
			Result = FText::Format(LOCTEXT("Partial", "Addition failed: {0}. Undo reverts this entire operation."), Result);
			return false;
		}
		++Added;
	}
	if (!Added) Transaction.Cancel();
	Result = FText::Format(LOCTEXT("Added", "Added {0} component(s). Existing components were preserved."), FText::AsNumber(Added));
	return true;
}

bool BindActors(const TArray<TWeakObjectPtr<AActor>>& Actors, ASpatialFabricManagerActor* Manager, FText& Result)
{
	if (!CanEdit(Manager, EditorWorld(), Result)) return false;
	if (Actors.IsEmpty()) { Result = LOCTEXT("SelectBind", "Select actors first."); return false; }
	for (const auto& Actor : Actors)
	{
		if (!CanEdit(Actor.Get(), EditorWorld(), Result)) return false;
		if (Actor->IsA<ASpatialFabricManagerActor>() || Actor->IsA<ASpatialStageVolume>())
		{ Result = LOCTEXT("TrackingOnly", "Select tracked actors, not managers or stages."); return false; }
	}
	TSet<AActor*> Bound;
	int64 NextID = 1;
	for (const auto& Binding : Manager->ObjectBindings)
	{
		if (Binding.TargetActor.Get()) Bound.Add(Binding.TargetActor.Get());
		NextID = FMath::Max(NextID, static_cast<int64>(Binding.DefaultObjectID) + 1);
		for (const auto& Target : Binding.Targets) NextID = FMath::Max(NextID, static_cast<int64>(Target.ObjectIDOverride) + 1);
	}
	TSet<AActor*> NewActors;
	for (const auto& Actor : Actors) if (!Bound.Contains(Actor.Get())) NewActors.Add(Actor.Get());
	if (!NewActors.IsEmpty() && NextID + NewActors.Num() - 1 > MAX_int32)
	{
		Result = LOCTEXT("IDExhausted", "No unused higher object IDs remain; nothing was changed.");
		return false;
	}
	FScopedTransaction Transaction(LOCTEXT("Bind", "Bind scene actors to SpatialFabric manager"));
	Manager->Modify();
	int32 Added = 0;
	for (const auto& Weak : Actors)
	{
		AActor* Actor = Weak.Get();
		if (Bound.Contains(Actor)) continue;
		FSpatialObjectBinding Binding;
		Binding.TargetActor = Actor;
		Binding.Label = Actor->GetActorLabel();
		Binding.CachedActorLabel = Binding.Label;
		Binding.DefaultObjectID = static_cast<int32>(NextID++);
		FSpatialAdapterTargetEntry Target;
		Target.AdapterType = Manager->ActiveAdapterType;
		Binding.Targets.Add(Target);
		Manager->ObjectBindings.Add(MoveTemp(Binding));
		Bound.Add(Actor);
		++Added;
	}
	if (Added) Manager->MarkPackageDirty(); else Transaction.Cancel();
	// Deliberately no ProcessFrame, adapter initialization, or binding/network rebuild.
	Result = FText::Format(LOCTEXT("Bound", "Bound {0} actor(s). Edit ObjectBindings below by selecting the manager, or use Objects."), FText::AsNumber(Added));
	return true;
}
}
#undef LOCTEXT_NAMESPACE
