// Copyright (c) 2026 SpatialFabric Contributors. Licensed under the MIT License.
#pragma once
#include "CoreMinimal.h"
#include "SceneObjectStatus.h"
#include "EditorUndoClient.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class IDetailsView;
class SVerticalBox;
class AActor;
class ASpatialFabricManagerActor;

/** Loaded editor actors, not a runtime object registry. */
class SSceneObjectsPanel : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SSceneObjectsPanel) {} SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual ~SSceneObjectsPanel() override;
	virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }
private:
	using FActorItem = TSharedPtr<TWeakObjectPtr<AActor>>;
	TArray<FActorItem> Items;
	TArray<TWeakObjectPtr<AActor>> Snapshot;
	TArray<FString> SnapshotState;
	SceneObjectStatus::FCache StatusCache;
	TSharedPtr<SListView<FActorItem>> List;
	TSharedPtr<SVerticalBox> DetailsBox;
	TArray<TSharedPtr<IDetailsView>> Details;
	TWeakObjectPtr<ASpatialFabricManagerActor> Manager;
	FString Search;
	FText Status;
	double NextRefresh = 0;
	bool bRefreshing = false;
	void Refresh(bool bForce = false);
	void RebuildDetails();
	TArray<TWeakObjectPtr<AActor>> Selected() const;
	bool SelectionEditable() const;
	FText ActorStatus(AActor* Actor) const;
	TSharedRef<ITableRow> Row(FActorItem Item, const TSharedRef<STableViewBase>& Owner);
	FReply Add(UClass* Class);
};
