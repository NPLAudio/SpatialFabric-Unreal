// Copyright (c) 2026 SpatialFabric Contributors. Licensed under the MIT License.
#include "UI/SSceneObjectsPanel.h"
#include "SceneObjectAuthoring.h"
#include "AudioLoomComponent.h"
#include "AudioLoomLiveInputComponent.h"
#include "SpatialFabricManagerActor.h"
#include "SpatialStageVolume.h"
#include "Editor.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "PropertyEditorDelegates.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SceneObjectsPanel"
void SSceneObjectsPanel::Construct(const FArguments& Args)
{
	if (GEditor) GEditor->RegisterForUndo(this);
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[ SNew(STextBlock).Text(LOCTEXT("Scope", "Scene Objects — all loaded, placed editor actors (including hidden/rootless). No actor loading or transport controls.")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[ SNew(SSearchBox).HintText(LOCTEXT("Search", "Search actor label, class, path, or status"))
			.OnTextChanged_Lambda([this](const FText& Text) { Search = Text.ToString(); Refresh(true); }) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[ SNew(SButton).Text(LOCTEXT("Playback", "Add Playback")).IsEnabled_Lambda([this] { return SelectionEditable(); })
				.OnClicked_Lambda([this] { return Add(UAudioLoomComponent::StaticClass()); }) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[ SNew(SButton).Text(LOCTEXT("Live", "Add Live Input")).IsEnabled_Lambda([this] { return SelectionEditable(); })
				.OnClicked_Lambda([this] { return Add(UAudioLoomLiveInputComponent::StaticClass()); }) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[ SNew(SButton).Text(LOCTEXT("Target", "Use selected manager"))
				.OnClicked_Lambda([this]
				{
					const auto Actors = Selected();
					Manager = Actors.Num() == 1 ? Cast<ASpatialFabricManagerActor>(Actors[0].Get()) : nullptr;
					Status = Manager.IsValid() ? LOCTEXT("Chosen", "Manager selected. Its real ObjectBindings are editable below; now select tracked actors.") : LOCTEXT("Choose", "Select exactly one existing manager in the list first.");
					RebuildDetails(); return FReply::Handled();
				}) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[ SNew(SButton).Text(LOCTEXT("Bind", "Bind selected to existing manager"))
				.IsEnabled_Lambda([this] { FText Reason; return SelectionEditable() && SceneObjectAuthoring::CanEdit(Manager.Get(), SceneObjectAuthoring::EditorWorld(), Reason); })
				.OnClicked_Lambda([this] { SceneObjectAuthoring::BindActors(Selected(), Manager.Get(), Status); Refresh(true); return FReply::Handled(); }) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[ SNew(SButton).Text(LOCTEXT("Refresh", "Refresh")).OnClicked_Lambda([this] { Refresh(true); return FReply::Handled(); }) ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[ SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]
			{ return FText::Format(LOCTEXT("Status", "{0} visible actor(s), {1} selected. Binding target: {2}. {3}"), FText::AsNumber(Items.Num()), FText::AsNumber(Selected().Num()), Manager.IsValid() ? FText::FromString(Manager->GetActorLabel()) : LOCTEXT("None", "none"), Status); }) ]
		+ SVerticalBox::Slot().FillHeight(1)
		[
			SNew(SSplitter)
			+ SSplitter::Slot().Value(0.4f)
			[
				SAssignNew(List, SListView<FActorItem>).ListItemsSource(&Items).SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SSceneObjectsPanel::Row)
				.OnSelectionChanged_Lambda([this](FActorItem, ESelectInfo::Type) { if (!bRefreshing) RebuildDetails(); })
			]
			+ SSplitter::Slot().Value(0.6f)
			[ SNew(SScrollBox) + SScrollBox::Slot() [ SAssignNew(DetailsBox, SVerticalBox) ] ]
		]
	];
	Refresh(true);
}

SSceneObjectsPanel::~SSceneObjectsPanel()
{
	if (GEditor) GEditor->UnregisterForUndo(this);
}
void SSceneObjectsPanel::Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime)
{
	SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
	if (CurrentTime >= NextRefresh) { NextRefresh = CurrentTime + 0.5; Refresh(); }
}
void SSceneObjectsPanel::PostUndo(bool bSuccess) { Refresh(true); }

TArray<TWeakObjectPtr<AActor>> SSceneObjectsPanel::Selected() const
{
	TArray<TWeakObjectPtr<AActor>> Actors;
	if (List) for (const auto& Item : List->GetSelectedItems()) if (Item.IsValid() && Item->IsValid()) Actors.Add(*Item);
	return Actors;
}
bool SSceneObjectsPanel::SelectionEditable() const
{
	const auto Actors = Selected();
	if (Actors.IsEmpty()) return false;
	FText Reason;
	for (const auto& Actor : Actors) if (!SceneObjectAuthoring::CanEdit(Actor.Get(), SceneObjectAuthoring::EditorWorld(), Reason)) return false;
	return true;
}
FText SSceneObjectsPanel::ActorStatus(AActor* Actor) const
{
	const auto* Cached = Actor ? StatusCache.Find(Actor) : nullptr;
	return Cached ? Cached->Text : FText::GetEmpty();
}
void SSceneObjectsPanel::Refresh(bool bForce)
{
	TArray<TWeakObjectPtr<AActor>> Actors;
	SceneObjectAuthoring::GatherActors(SceneObjectAuthoring::EditorWorld(), Actors);
	// Compare identities, labels, component identities and binding membership, not actor counts.
	TArray<FString> State;
	const bool bActorsChanged = Actors != Snapshot;
	Snapshot = Actors;
	StatusCache = SceneObjectStatus::Build(Actors, SceneObjectAuthoring::EditorWorld());
	for (const auto& Weak : Actors)
	{
		AActor* Actor = Weak.Get();
		const auto& Cached = StatusCache.FindChecked(Weak);
		FString Key = Actor->GetActorLabel() + Cached.Text.ToString() + Cached.ComponentSignature;
		State.Add(Key);
	}
	// Items may be filtered; compare full state keys including paths.
	for (int32 I = 0; I < Actors.Num(); ++I) State[I] += Actors[I]->GetPathName();
	if (!bForce && !bActorsChanged && State == SnapshotState) return;
	SnapshotState = MoveTemp(State);
	const TSet<TWeakObjectPtr<AActor>> Selection(Selected());
	TGuardValue<bool> Guard(bRefreshing, true);
	List->ClearSelection();
	Items.Reset();
	for (const auto& Actor : Actors)
	{
		const FString Haystack = Actor->GetActorLabel() + TEXT(" ") + Actor->GetPathName() + TEXT(" ") + Actor->GetClass()->GetName() + TEXT(" ") + ActorStatus(Actor.Get()).ToString();
		if (!Search.IsEmpty() && !Haystack.Contains(Search)) continue;
		auto Item = MakeShared<TWeakObjectPtr<AActor>>(Actor);
		Items.Add(Item);
		if (Selection.Contains(Actor)) List->SetItemSelection(Item, true);
	}
	List->RequestListRefresh();
	RebuildDetails();
}
TSharedRef<ITableRow> SSceneObjectsPanel::Row(FActorItem Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<FActorItem>, Owner)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[ SNew(STextBlock).Text_Lambda([Item] { return Item->IsValid() ? FText::FromString((*Item)->GetActorLabel() + TEXT(" [") + (*Item)->GetClass()->GetName() + TEXT("]")) : LOCTEXT("Deleted", "Deleted actor"); })
			.ToolTipText_Lambda([Item] { return Item->IsValid() ? FText::FromString((*Item)->GetPathName()) : FText::GetEmpty(); }) ]
		+ SVerticalBox::Slot().AutoHeight()
		[ SNew(STextBlock).AutoWrapText(true).Text_Lambda([this, Item] { return ActorStatus(Item->Get()); }) ]
	];
}
FReply SSceneObjectsPanel::Add(UClass* Class)
{
	SceneObjectAuthoring::AddComponents(Selected(), Class, Status);
	Refresh(true);
	return FReply::Handled();
}

void SSceneObjectsPanel::RebuildDetails()
{
	DetailsBox->ClearChildren();
	Details.Reset();
	TMap<UClass*, TArray<UObject*>> Groups;
	for (const auto& Actor : Selected())
	{
		if (Actor->IsA<ASpatialFabricManagerActor>()) Groups.FindOrAdd(ASpatialFabricManagerActor::StaticClass()).Add(Actor.Get());
		if (Actor->IsA<ASpatialStageVolume>()) Groups.FindOrAdd(ASpatialStageVolume::StaticClass()).Add(Actor.Get());
		TInlineComponentArray<UActorComponent*> Components(Actor.Get());
		for (auto* Component : Components)
		{
			if (Component->IsA<UAudioLoomComponent>()) Groups.FindOrAdd(UAudioLoomComponent::StaticClass()).Add(Component);
			else if (Component->IsA<UAudioLoomLiveInputComponent>()) Groups.FindOrAdd(UAudioLoomLiveInputComponent::StaticClass()).Add(Component);
		}
	}
	if (Manager.IsValid() && Manager->GetWorld() == SceneObjectAuthoring::EditorWorld())
		Groups.FindOrAdd(ASpatialFabricManagerActor::StaticClass()).AddUnique(Manager.Get());
	if (Groups.IsEmpty())
		DetailsBox->AddSlot().AutoHeight().Padding(6)[ SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("NoSettings", "Select actors to edit their AudioLoom / SpatialFabric settings. Ordinary actors remain listed; use an explicit Add action to configure them. Ctrl/Shift selects multiple actors. All matching components participate, including multiple components on one actor.")) ];
	for (auto& Group : Groups)
	{
		UClass* Class = Group.Key;
		FDetailsViewArgs Args;
		Args.bUpdatesFromSelection = false;
		Args.bLockable = false;
		Args.bAllowMultipleTopLevelObjects = false;
		Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		auto View = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(Args);
		View->SetDisableCustomDetailLayouts(true); // Existing AudioLoom transport layouts assume the first instance.
		View->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([Class](const FPropertyAndParent& Property)
		{
			if (const UClass* OwnerClass = Property.Property.GetOwnerClass()) return OwnerClass == Class;
			// Struct/array/map descendants inherit visibility, not generic nested UObject fields.
			for (const FProperty* Parent : Property.ParentProperties) if (Parent && Parent->GetOwnerClass() == Class) return true;
			return false;
		}));
		TArray<TWeakObjectPtr<UObject>> Objects;
		for (UObject* Object : Group.Value) Objects.Add(Object);
		View->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateLambda([Objects]
		{
			FText Reason;
			for (const auto& Weak : Objects)
			{
				UObject* Object = Weak.Get();
				if (!Object) return false;
				const UActorComponent* Component = Cast<UActorComponent>(Object);
				const AActor* Owner = Component ? Component->GetOwner() : Cast<AActor>(Object);
				if (!SceneObjectAuthoring::CanEdit(Owner, SceneObjectAuthoring::EditorWorld(), Reason)) return false;
				if (Component && !SceneObjectAuthoring::CanEditComponent(Component)) return false;
			}
			return true;
		}));
		View->SetObjects(Group.Value);
		Details.Add(View);
		FString Instances;
		for (UObject* Object : Group.Value)
		{
			const UActorComponent* Component = Cast<UActorComponent>(Object);
			const AActor* Owner = Component ? Component->GetOwner() : Cast<AActor>(Object);
			if (!Instances.IsEmpty()) Instances += TEXT("\n");
			Instances += Owner->GetActorLabel();
			if (Component) Instances += TEXT(" / ") + Component->GetName();
			if (Owner == Manager.Get()) Instances += TEXT(" (binding target)");
		}
		DetailsBox->AddSlot().AutoHeight().Padding(6)[ SNew(STextBlock).AutoWrapText(true).Text(FText::Format(LOCTEXT("Group", "{0} — {1} instance(s); native mixed-value editing\n{2}"), Class->GetDisplayNameText(), FText::AsNumber(Group.Value.Num()), FText::FromString(Instances))) ];
		DetailsBox->AddSlot().AutoHeight()[ View ];
	}
}
#undef LOCTEXT_NAMESPACE
