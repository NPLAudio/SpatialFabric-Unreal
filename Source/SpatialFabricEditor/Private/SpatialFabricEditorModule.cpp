// Copyright (c) 2026 SpatialFabric Contributors. Licensed under the MIT License.

// Registers Window → Spatial Fabric tab; FSpatialFabricEditorTickable drives ProcessFrame in editor.

#include "SpatialFabricEditorModule.h"
#include "UI/SSpatialFabricPanel.h"
#include "SpatialFabricManagerActor.h"
#include "AudioLoomOscSubsystem.h"

#include "Editor.h"
#include "LevelEditor.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SBoxPanel.h"
#include "TickableEditorObject.h"
#include "EngineUtils.h"

#define LOCTEXT_NAMESPACE "SpatialFabricEditor"

const FName FSpatialFabricEditorModule::SpatialFabricTabName = TEXT("SpatialFabricPanel");
static bool GRestorePlaybackOSC = false;

// ─── Editor tickable ──────────────────────────────────────────────────────

/**
 * Drives ProcessFrame when in the Editor (not PIE) so the editor panel
 * shows live data as actor positions change, without needing to press Play.
 */
class FSpatialFabricEditorTickable : public FTickableEditorObject
{
public:
	virtual void Tick(float DeltaTime) override
	{
		if (!GEditor || GEditor->PlayWorld) { return; }
		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		if (!EditorWorld) { return; }
		if (GRestorePlaybackOSC)
		{
			if (auto* Sub = EditorWorld->GetSubsystem<UAudioLoomOscSubsystem>()) Sub->StartListening();
			GRestorePlaybackOSC = false;
		}

		// Never mutate a level just because the editor/module is ticking.
		for (TActorIterator<ASpatialFabricManagerActor> It(EditorWorld); It; ++It)
			It->ProcessFrame(DeltaTime);
	}

	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FSpatialFabricEditorTickable, STATGROUP_Tickables);
	}

	virtual bool IsTickable() const override
	{
		return GEditor != nullptr && !GEditor->PlayWorld;
	}
};

static TSharedPtr<FSpatialFabricEditorTickable> GEditorTickable;

// ─── Module lifecycle ─────────────────────────────────────────────────────

void FSpatialFabricEditorModule::StartupModule()
{
	GEditorTickable = MakeShared<FSpatialFabricEditorTickable>();

	RegisterTab();

	// Release editor sockets before PIE duplicates try to bind the same ports.
	PIEStartedHandle = FEditorDelegates::PreBeginPIE.AddLambda([](bool)
	{
		if (!GEditor) return;
		if (UWorld* World = GEditor->GetEditorWorldContext().World())
		{
			for (TActorIterator<ASpatialFabricManagerActor> It(World); It; ++It) It->ShutdownNetworking();
			if (auto* Sub = World->GetSubsystem<UAudioLoomOscSubsystem>())
			{ GRestorePlaybackOSC = Sub->IsListening(); Sub->StopListening(); }
		}
	});
	PostPIEHandle = FEditorDelegates::PostPIEStarted.AddLambda([](bool)
	{
		if (GRestorePlaybackOSC && GEditor && GEditor->PlayWorld)
			if (auto* Sub = GEditor->PlayWorld->GetSubsystem<UAudioLoomOscSubsystem>()) Sub->StartListening();
	});
	EndPIEHandle = FEditorDelegates::EndPIE.AddLambda([](bool)
	{
		if (GEditor && GEditor->PlayWorld)
			if (auto* Sub = GEditor->PlayWorld->GetSubsystem<UAudioLoomOscSubsystem>())
			{ GRestorePlaybackOSC = Sub->IsListening(); Sub->StopListening(); }
	});
}

void FSpatialFabricEditorModule::ShutdownModule()
{
	GEditorTickable.Reset();
	FEditorDelegates::PreBeginPIE.Remove(PIEStartedHandle);
	FEditorDelegates::PostPIEStarted.Remove(PostPIEHandle);
	FEditorDelegates::EndPIE.Remove(EndPIEHandle);

	if (FGlobalTabmanager::Get()->HasTabSpawner(SpatialFabricTabName))
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SpatialFabricTabName);
	}
}

void FSpatialFabricEditorModule::RegisterTab()
{
	const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		SpatialFabricTabName,
		FOnSpawnTab::CreateRaw(this, &FSpatialFabricEditorModule::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Spatial Fabric"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Spatial audio show-control — multi-protocol spatial object routing"))
		.SetGroup(MenuStructure.GetLevelEditorCategory());
}

TSharedRef<SDockTab> FSpatialFabricEditorModule::SpawnTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SSpatialFabricPanel)
		];
}

void FSpatialFabricEditorModule::OpenPanel()
{
	FGlobalTabmanager::Get()->TryInvokeTab(SpatialFabricTabName);
}

IMPLEMENT_MODULE(FSpatialFabricEditorModule, SpatialFabricEditor)

#undef LOCTEXT_NAMESPACE
