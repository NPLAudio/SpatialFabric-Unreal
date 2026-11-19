// Copyright (c) 2026 SpatialFabric Contributors. Licensed under the MIT License.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "SceneObjectAuthoring.h"
#include "SceneObjectStatus.h"
#include "SpatialFabricSettings.h"
#include "Misc/ScopeExit.h"
#include "AudioLoomComponent.h"
#include "AudioLoomLiveInputComponent.h"
#include "SpatialFabricManagerActor.h"
#include "SpatialStageVolume.h"
#include "SpatialOSCServerComponent.h"
#include "SpatialOSCClientComponent.h"
#include "UI/SSceneObjectsPanel.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "ScopedTransaction.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/ObjectReader.h"
#include "UObject/UnrealType.h"

namespace
{
void SetEditable(AActor* Actor, bool bEditable)
{
	FBoolProperty* Property = FindFProperty<FBoolProperty>(AActor::StaticClass(), TEXT("bEditable"));
	check(Property);
	Property->SetPropertyValue_InContainer(Actor, bEditable);
}
struct FSceneAuthoringFixture
{
	UWorld* Previous;
	UWorld* World;
	FSceneAuthoringFixture()
	{
		Previous = GEditor->GetEditorWorldContext().World();
		World = UWorld::CreateWorld(EWorldType::Editor, false);
		GEditor->GetEditorWorldContext().SetCurrentWorld(World);
	}
	~FSceneAuthoringFixture()
	{
		GEditor->GetEditorWorldContext().SetCurrentWorld(Previous);
		World->DestroyWorld(false);
	}
	AActor* Actor()
	{
		AActor* A = World->SpawnActor<AActor>();
		A->SetFlags(RF_Transactional);
		return A;
	}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSceneObjectsDiscovery, "SpatialFabric.SceneObjects.LoadedOrdinaryAndRestrictions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSceneObjectsDiscovery::RunTest(const FString&)
{
	if (!GEditor) return false;
	FSceneAuthoringFixture Fixture;
	AActor* Ordinary = Fixture.Actor();
	AActor* Hidden = Fixture.Actor();
	Hidden->SetIsTemporarilyHiddenInEditor(true);
	TArray<TWeakObjectPtr<AActor>> Found;
	SceneObjectAuthoring::GatherActors(Fixture.World, Found);
	TestTrue(TEXT("Ordinary rootless actors are listed"), Found.Contains(Ordinary));
	TestTrue(TEXT("Hidden actors are listed"), Found.Contains(Hidden));
	TestNull(TEXT("Listing does not attach components"), Ordinary->GetRootComponent());
	FText Reason;
	TestTrue(TEXT("Ordinary placed actor is editable"), SceneObjectAuthoring::CanEdit(Ordinary, Fixture.World, Reason));
	TestFalse(TEXT("CDO is rejected"), SceneObjectAuthoring::CanEdit(GetMutableDefault<AActor>(), Fixture.World, Reason));
	SetEditable(Ordinary, false);
	TestFalse(TEXT("Noneditable target fails bulk preflight"), SceneObjectAuthoring::AddComponents({Hidden, Ordinary}, UAudioLoomComponent::StaticClass(), Reason));
	TestNull(TEXT("No partial addition on failed preflight"), Hidden->FindComponentByClass<UAudioLoomComponent>());
	SetEditable(Ordinary, true);
	Fixture.World->PersistentLevel->bLocked = true;
	TestFalse(TEXT("Locked level is rejected"), SceneObjectAuthoring::AddComponents({Ordinary}, UAudioLoomComponent::StaticClass(), Reason));
	Fixture.World->PersistentLevel->bLocked = false;
	UWorld* OldPlayWorld = GEditor->PlayWorld;
	GEditor->PlayWorld = Fixture.World;
	TestFalse(TEXT("PIE prevents component authoring"), SceneObjectAuthoring::AddComponents({Ordinary}, UAudioLoomComponent::StaticClass(), Reason));
	GEditor->PlayWorld = OldPlayWorld;
	TestFalse(TEXT("Component allowlist rejects arbitrary classes"), SceneObjectAuthoring::AddComponents({Ordinary}, USceneComponent::StaticClass(), Reason));
	TestTrue(TEXT("No playback component created by rejected actions"), Ordinary->GetInstanceComponents().IsEmpty());
	const TWeakObjectPtr<AActor> OldIdentity = Hidden;
	Fixture.World->DestroyActor(Hidden);
	AActor* Replacement = Fixture.Actor();
	SceneObjectAuthoring::GatherActors(Fixture.World, Found);
	TestFalse(TEXT("Deleted actors disappear without relying on count changes"), Found.Contains(OldIdentity));
	TestTrue(TEXT("Replacement has its own stable weak identity"), Found.Contains(Replacement));
	// Construct the production tab headlessly: browsing attaches no functionality.
	{
		TSharedRef<SSceneObjectsPanel> Panel = SNew(SSceneObjectsPanel);
		TestTrue(TEXT("Constructing the tab does not configure ordinary actors"), Replacement->GetInstanceComponents().IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSceneObjectsBulkUndo, "SpatialFabric.SceneObjects.BulkAddUndoRedoAndValues", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSceneObjectsBulkUndo::RunTest(const FString&)
{
	if (!GEditor) return false;
	FSceneAuthoringFixture Fixture;
	AActor* A = Fixture.Actor();
	AActor* B = Fixture.Actor();
	const FTransform Transform = A->GetActorTransform();
	FText Result;
	TestTrue(TEXT("Production native-subobject bulk add succeeds"), SceneObjectAuthoring::AddComponents({A, B, A}, UAudioLoomComponent::StaticClass(), Result));
	TestNotNull(TEXT("First actor has playback"), A->FindComponentByClass<UAudioLoomComponent>());
	TestNotNull(TEXT("Second actor has playback"), B->FindComponentByClass<UAudioLoomComponent>());
	TestEqual(TEXT("Repeated actor not duplicated"), A->GetInstanceComponents().Num(), 1);
	GEditor->UndoTransaction();
	TestNull(TEXT("One undo removes first addition"), A->FindComponentByClass<UAudioLoomComponent>());
	TestNull(TEXT("One undo removes second addition"), B->FindComponentByClass<UAudioLoomComponent>());
	GEditor->RedoTransaction();
	TestNotNull(TEXT("Redo restores first addition"), A->FindComponentByClass<UAudioLoomComponent>());
	TestNotNull(TEXT("Redo restores second addition"), B->FindComponentByClass<UAudioLoomComponent>());
	// Native Undo/Redo may introduce editor helper components on rootless actors.
	// Assert the requested type and that the duplicate command changes nothing.
	const int32 InstancesBeforeDuplicate = A->GetInstanceComponents().Num();
	TestTrue(TEXT("Duplicate add is safe"), SceneObjectAuthoring::AddComponents({A, B}, UAudioLoomComponent::StaticClass(), Result));
	TestEqual(TEXT("Duplicate add preserves instance count"), A->GetInstanceComponents().Num(), InstancesBeforeDuplicate);
	TArray<UAudioLoomComponent*> Playback;
	A->GetComponents(Playback);
	TestEqual(TEXT("Exactly one active playback component after redo and duplicate action"), Playback.Num(), 1);
	TestTrue(TEXT("Live input is independently addable"), SceneObjectAuthoring::AddComponents({A, B}, UAudioLoomLiveInputComponent::StaticClass(), Result));
	UAudioLoomLiveInputComponent* Live = A->FindComponentByClass<UAudioLoomLiveInputComponent>();
	TestNotNull(TEXT("Live input exists without transport invocation"), Live);
	if (Live) TestFalse(TEXT("Adding live input does not start capture"), Live->IsLive());
	TestTrue(TEXT("Rootless actor transform unchanged"), Transform.Equals(A->GetActorTransform()));
	UAudioLoomComponent* CA = A->FindComponentByClass<UAudioLoomComponent>();
	UAudioLoomComponent* CB = B->FindComponentByClass<UAudioLoomComponent>();
	if (CA && CB)
	{
		TestFalse(TEXT("Adding playback does not start playback"), CA->IsPlaying());
		TestTrue(TEXT("Instance-added components are centrally editable"), SceneObjectAuthoring::CanEditComponent(CA));
		SetEditable(A, false);
		TestFalse(TEXT("Component editing respects owner restrictions"), SceneObjectAuthoring::CanEditComponent(CA));
		SetEditable(A, true);
		CA->OutputChannel = 2;
		CB->OutputChannel = 5;
		{
			FScopedTransaction Edit(NSLOCTEXT("SceneObjectsTests", "Values", "Edit native component values"));
			CA->Modify(); CB->Modify(); CA->OutputChannel = CB->OutputChannel = 7;
		}
		GEditor->UndoTransaction();
		TestEqual(TEXT("Undo restores first mixed value"), CA->OutputChannel, 2);
		TestEqual(TEXT("Undo restores second mixed value"), CB->OutputChannel, 5);
		GEditor->RedoTransaction();
		TestEqual(TEXT("Redo applies shared value"), CA->OutputChannel, 7);
		TArray<uint8> Bytes;
		FObjectWriter Writer(CA, Bytes);
		CA->OutputChannel = 0;
		FObjectReader Reader(CA, Bytes);
		TestEqual(TEXT("Real component property serializes"), CA->OutputChannel, 7);
	}
	TArray<TWeakObjectPtr<AActor>> Actors;
	SceneObjectAuthoring::GatherActors(Fixture.World, Actors);
	for (const auto& Actor : Actors) TestFalse(TEXT("No manager auto-created"), Actor->IsA<ASpatialFabricManagerActor>());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSceneObjectsBindings, "SpatialFabric.SceneObjects.BindingPreservesExistingAndUndo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSceneObjectsBindings::RunTest(const FString&)
{
	if (!GEditor) return false;
	FSceneAuthoringFixture Fixture;
	AActor* A = Fixture.Actor();
	AActor* B = Fixture.Actor();
	auto* Manager = Fixture.World->SpawnActor<ASpatialFabricManagerActor>();
	Manager->SetFlags(RF_Transactional);
	FSpatialObjectBinding Existing;
	Existing.TargetActor = A;
	Existing.DefaultObjectID = 47;
	Existing.Label = TEXT("Preserved");
	FSpatialAdapterTargetEntry Target;
	Target.ObjectIDOverride = 91;
	Existing.Targets.Add(Target);
	Manager->ObjectBindings.Add(Existing);
	FText Result;
	TestTrue(TEXT("Production explicit binding succeeds"), SceneObjectAuthoring::BindActors({A, B, B}, Manager, Result));
	TestEqual(TEXT("Existing and repeated actors not duplicated"), Manager->ObjectBindings.Num(), 2);
	TestEqual(TEXT("Existing ID unchanged"), Manager->ObjectBindings[0].DefaultObjectID, 47);
	TestEqual(TEXT("Existing adapter target unchanged"), Manager->ObjectBindings[0].Targets[0].ObjectIDOverride, 91);
	TestEqual(TEXT("New ID avoids existing override"), Manager->ObjectBindings[1].DefaultObjectID, 92);
	GEditor->UndoTransaction();
	TestEqual(TEXT("Binding undo restores original array"), Manager->ObjectBindings.Num(), 1);
	GEditor->RedoTransaction();
	TestEqual(TEXT("Binding redo restores new entry"), Manager->ObjectBindings.Num(), 2);
	TestFalse(TEXT("Binding and its Undo/Redo never start a server"), Manager->ServerComponent->IsListening());
	TestFalse(TEXT("Binding and its Undo/Redo never connect a client"), Manager->ClientComponent->IsConnected());
	Manager->ObjectBindings[0].DefaultObjectID = MAX_int32;
	AActor* C = Fixture.Actor();
	TestFalse(TEXT("ID exhaustion rejects without overflow"), SceneObjectAuthoring::BindActors({C}, Manager, Result));
	TestEqual(TEXT("ID exhaustion did not append binding"), Manager->ObjectBindings.Num(), 2);
	SetEditable(B, false);
	TestFalse(TEXT("Read-only actor cannot be bound"), SceneObjectAuthoring::BindActors({B}, Manager, Result));
	SetEditable(B, true);
	SetEditable(Manager, false);
	TestFalse(TEXT("Read-only manager cannot be changed"), SceneObjectAuthoring::BindActors({A}, Manager, Result));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSceneObjectsInitializedCallbacks, "SpatialFabric.SceneObjects.InitializedCallbacksPreserveTransport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSceneObjectsInitializedCallbacks::RunTest(const FString&)
{
	if (!GEditor) return false;
	FSceneAuthoringFixture Fixture;
	auto* Manager = Fixture.World->SpawnActor<ASpatialFabricManagerActor>();
	Manager->SetFlags(RF_Transactional);
	auto* Client = Manager->ClientComponent;
	auto* Server = Manager->ServerComponent;
	auto* Settings = GetMutableDefault<USpatialFabricSettings>();
	const bool bPrevious = Settings->bEnableInEditor;
	Settings->bEnableInEditor = true; // Exercise the allowed production path, not its policy early-out.
	int32 Connects = 0, Disconnects = 0, Starts = 0, Stops = 0;
	bool bConnected = false, bListening = false;
	FString Endpoint;
	FString Phase;
	Client->TestConnect = [&](const FString& IP, int32 Port) { ++Connects; bConnected = true; Endpoint = IP + FString::FromInt(Port); };
	Client->TestDisconnect = [&] { ++Disconnects; bConnected = false; };
	Client->TestIsConnected = [&] { return bConnected; };
	Server->TestStart = [&](const FString&, int32) { ++Starts; bListening = true; };
	Server->TestStop = [&]
	{
		++Stops; bListening = false;
		if (!Phase.IsEmpty()) AddError(TEXT("Unexpected server Stop during ") + Phase);
	};
	Server->TestIsListening = [&] { return bListening; };
	ON_SCOPE_EXIT
	{
		Client->TestConnect = nullptr; Client->TestDisconnect = nullptr; Client->TestIsConnected = nullptr;
		Server->TestStart = nullptr; Server->TestStop = nullptr; Server->TestIsListening = nullptr;
		Settings->bEnableInEditor = bPrevious;
	};
	Manager->Router = MakeUnique<FProtocolRouter>();
	Manager->InitializeAdapters();
	Manager->StartServer();
	TestEqual(TEXT("Explicit initialization still connects through the real client entry point"), Connects, 1);
	TestEqual(TEXT("Explicit Start still starts through the server entry point"), Starts, 1);
	FProtocolRouter* Router = Manager->GetRouter();
	for (int32 Mode = 0; Mode < 3; ++Mode)
	{
		if (Mode == 1) Manager->DisconnectClient();
		if (Mode == 2) Manager->StopServer();
		const bool bExpectedConnected = Client->IsConnected(), bExpectedListening = Server->IsListening();
		const FString ExpectedEndpoint = Endpoint;
		Connects = Disconnects = Starts = Stops = 0;
		Phase = TEXT("unrelated property Pre/Post");
		FPropertyChangedEvent Unrelated(FindFProperty<FProperty>(Manager->GetClass(), TEXT("bHideStageInPIE")));
		Manager->PreEditChange(Unrelated.Property);
		Manager->bHideStageInPIE = !Manager->bHideStageInPIE;
		Manager->PostEditChangeProperty(Unrelated);
		AActor* Actor = Fixture.Actor();
		FText Result;
		const int32 Before = Manager->ObjectBindings.Num();
		Phase = TEXT("BindActors");
		TestTrue(TEXT("Binding initialized manager succeeds"), SceneObjectAuthoring::BindActors({Actor}, Manager, Result));
		Phase = TEXT("binding Undo");
		GEditor->UndoTransaction();
		TestEqual(TEXT("Initialized binding undo"), Manager->ObjectBindings.Num(), Before);
		Phase = TEXT("binding Redo");
		GEditor->RedoTransaction();
		TestEqual(TEXT("Initialized binding redo"), Manager->ObjectBindings.Num(), Before + 1);
		const uint8 Key = (uint8)ESpatialAdapterType::ADMOSC;
		const bool bWasEnabled = Manager->AdapterConfigs.FindChecked(Key).bEnabled;
		Phase = TEXT("config property Pre/Post");
		{
			FScopedTransaction Transaction(NSLOCTEXT("SceneObjectsTests", "Config", "Edit adapter configuration"));
			Manager->Modify();
			Manager->PreEditChange(FindFProperty<FProperty>(Manager->GetClass(), TEXT("AdapterConfigs")));
			auto& Config = Manager->AdapterConfigs.FindChecked(Key);
			Config.bEnabled = !bWasEnabled;
			Config.TargetPort = 19234 + Mode;
			FPropertyChangedEvent Changed(FindFProperty<FProperty>(Manager->GetClass(), TEXT("AdapterConfigs")));
			Manager->PostEditChangeProperty(Changed);
		}
		TestEqual(TEXT("Config edit synchronizes real router adapter"), Router->FindAdapter(ESpatialAdapterType::ADMOSC)->IsEnabled(), !bWasEnabled);
		Phase = TEXT("config Undo");
		GEditor->UndoTransaction();
		TestEqual(TEXT("Config undo synchronizes real router adapter"), Router->FindAdapter(ESpatialAdapterType::ADMOSC)->IsEnabled(), bWasEnabled);
		Phase = TEXT("config Redo");
		GEditor->RedoTransaction();
		TestEqual(TEXT("Config redo synchronizes real router adapter"), Router->FindAdapter(ESpatialAdapterType::ADMOSC)->IsEnabled(), !bWasEnabled);
		TestTrue(TEXT("Callbacks retain router identity"), Manager->GetRouter() == Router);
		TestEqual(TEXT("Callbacks never attempt Connect"), Connects, 0);
		TestEqual(TEXT("Callbacks never close client"), Disconnects, 0);
		TestEqual(TEXT("Callbacks never start server"), Starts, 0);
		TestEqual(TEXT("Callbacks never stop server"), Stops, 0);
		TestEqual(TEXT("Client state preserved"), Client->IsConnected(), bExpectedConnected);
		TestEqual(TEXT("Server state preserved"), Server->IsListening(), bExpectedListening);
		TestEqual(TEXT("Connected endpoint is not retargeted by config edit"), Endpoint, ExpectedEndpoint);
		Phase.Empty();
	}
	Manager->ShutdownNetworking();
	Connects = Disconnects = Starts = Stops = 0;
	FPropertyChangedEvent Changed(FindFProperty<FProperty>(Manager->GetClass(), TEXT("AdapterConfigs")));
	Manager->PreEditChange(Changed.Property);
	Manager->PostEditChangeProperty(Changed);
	Manager->PostEditUndo();
	TestNull(TEXT("Fully stopped manager never recreates router from callbacks"), Manager->GetRouter());
	TestEqual(TEXT("Fully stopped callbacks never touch transports"), Connects + Disconnects + Starts + Stops, 0);
	Manager->Router = MakeUnique<FProtocolRouter>();
	Manager->InitializeAdapters();
	Manager->StartServer();
	TestEqual(TEXT("Explicit reinit after stop can still connect"), Connects, 1);
	TestEqual(TEXT("Explicit Start after stop can still listen"), Starts, 1);
	Server->UnregisterComponent();
	TestFalse(TEXT("Ordinary unregister outside callbacks still stops server"), Server->IsListening());
	TestEqual(TEXT("Editor preservation guard does not leak"), Stops, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSceneObjectsStatusIndex, "SpatialFabric.SceneObjects.StatusIndexCountsAndIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSceneObjectsStatusIndex::RunTest(const FString&)
{
	if (!GEditor) return false;
	FSceneAuthoringFixture Fixture;
	AActor* A = Fixture.Actor();
	AActor* B = Fixture.Actor();
	auto* M1 = Fixture.World->SpawnActor<ASpatialFabricManagerActor>();
	auto* M2 = Fixture.World->SpawnActor<ASpatialFabricManagerActor>();
	FSpatialObjectBinding Binding;
	Binding.TargetActor = A;
	M1->ObjectBindings = {Binding, Binding}; // Count actual records, including duplicates.
	M2->ObjectBindings = {Binding};
	Binding.TargetActor = B;
	M2->ObjectBindings.Add(Binding);
	FText Result;
	SceneObjectAuthoring::AddComponents({A}, UAudioLoomComponent::StaticClass(), Result);
	SceneObjectAuthoring::AddComponents({A}, UAudioLoomLiveInputComponent::StaticClass(), Result);
	TArray<TWeakObjectPtr<AActor>> Actors = {A, B, M1, M2};
	auto Cache = SceneObjectStatus::Build(Actors, Fixture.World);
	TestEqual(TEXT("All manager binding records indexed, including duplicates"), Cache.FindChecked(A).Bindings, 3);
	TestEqual(TEXT("Other actor gets only its bindings"), Cache.FindChecked(B).Bindings, 1);
	TestEqual(TEXT("Playback cached"), Cache.FindChecked(A).Playback, 1);
	TestEqual(TEXT("Live input cached without starting capture"), Cache.FindChecked(A).LiveInput, 1);
	TestTrue(TEXT("Cached searchable status includes indexed counts"), Cache.FindChecked(A).Text.ToString().Contains(TEXT("SF bindings: 3")));
	TestTrue(TEXT("Component identity signature cached"), !Cache.FindChecked(A).ComponentSignature.IsEmpty());
	const TWeakObjectPtr<AActor> OldIdentity = B;
	Fixture.World->DestroyActor(B);
	AActor* Replacement = Fixture.Actor();
	Actors[1] = Replacement; // Same count; no stale entry inherited by replacement.
	Cache = SceneObjectStatus::Build(Actors, Fixture.World);
	TestFalse(TEXT("Old actor identity evicted"), Cache.Contains(OldIdentity));
	TestEqual(TEXT("Replacement starts unbound"), Cache.FindChecked(Replacement).Bindings, 0);
	TestTrue(TEXT("Replacement status reflects missing configuration"), Cache.FindChecked(Replacement).Text.ToString().Contains(TEXT("Missing configuration")));
	M1->ObjectBindings.Reset();
	Cache = SceneObjectStatus::Build(Actors, Fixture.World);
	TestEqual(TEXT("Refresh replaces counts instead of accumulating"), Cache.FindChecked(A).Bindings, 1);
	return true;
}
#endif
