// Regression coverage for the combined SpatialFabric pipeline.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "SpatialFabricManagerActor.h"
#include "SpatialObjectRegistry.h"
#include "SpatialStageVolume.h"
#include "ProtocolRouter.h"
#include "SpatialFabricSettings.h"
#include "Adapters/ADMOSCAdapter.h"
#include "Adapters/CustomAdapter.h"

namespace SFAudit
{
struct FWorldScope
{
    TGuardValue<bool> NetworkPermission;
    UWorld* World;
    explicit FWorldScope(EWorldType::Type Type = EWorldType::Game)
        : NetworkPermission(GetMutableDefault<USpatialFabricSettings>()->bEnableInEditor, false), World(UWorld::CreateWorld(Type, false)) {}
    ~FWorldScope() { World->DestroyWorld(false); }
};
static AActor* Point(UWorld* World)
{
    AActor* Actor = World->SpawnActor<AActor>();
    USceneComponent* Root = NewObject<USceneComponent>(Actor);
    Actor->SetRootComponent(Root);
    Root->RegisterComponent();
    Actor->SetActorLocation(FVector(100, 0, 0));
    return Actor;
}
struct FCaptureAdapter : ISpatialProtocolAdapter
{
    ESpatialAdapterType Type;
    FSpatialFrameSnapshot Received;
    explicit FCaptureAdapter(ESpatialAdapterType InType) : Type(InType) {}
    FName GetName() const override { return TEXT("AuditCapture"); }
    ESpatialAdapterType GetAdapterType() const override { return Type; }
    void Configure(const FSpatialAdapterConfig&) override {}
    bool IsEnabled() const override { return true; }
    void ProcessFrame(const FSpatialFrameSnapshot& Snapshot, float) override { Received = Snapshot; }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFAuditMissingActor, "SpatialFabricAudit.Routing.MissingActor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFAuditMissingActor::RunTest(const FString&)
{
    SFAudit::FWorldScope Scope;
    auto* Manager = Scope.World->SpawnActor<ASpatialFabricManagerActor>();
    FSpatialObjectBinding Missing;
    FSpatialAdapterTargetEntry CustomTarget;
    CustomTarget.AdapterType = ESpatialAdapterType::Custom;
    CustomTarget.ObjectIDOverride = 77;
    Missing.Targets.Add(CustomTarget);
    FSpatialObjectBinding Present;
    Present.TargetActor = SFAudit::Point(Scope.World);
    Present.DefaultObjectID = 2;
    FSpatialAdapterTargetEntry ADMTarget;
    ADMTarget.AdapterType = ESpatialAdapterType::ADMOSC;
    Present.Targets.Add(ADMTarget);
    TArray<FSpatialObjectBinding> Bindings = {Missing, Present};
    const auto Snapshot = Manager->RegistryComponent->BuildSnapshot(Bindings, nullptr);
    TestEqual(TEXT("Registry has one resolved object"), Snapshot.Objects.Num(), 1);
    FProtocolRouter Router;
    auto Custom = MakeShared<SFAudit::FCaptureAdapter>(ESpatialAdapterType::Custom);
    auto ADM = MakeShared<SFAudit::FCaptureAdapter>(ESpatialAdapterType::ADMOSC);
    Router.RegisterAdapter(Custom);
    Router.RegisterAdapter(ADM);
    Router.ProcessFrame(Snapshot, Bindings, 1.0f);
    TestEqual(TEXT("Missing Custom binding must not steal ADM object"), Custom->Received.Objects.Num(), 0);
    TestEqual(TEXT("Resolved ADM binding must receive its object"), ADM->Received.Objects.Num(), 1);
    if (Custom->Received.Objects.Num())
        AddInfo(FString::Printf(TEXT("Wrongly emitted Custom object ID: %d"), Custom->Received.Objects[0].ObjectID));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFAuditChildListener, "SpatialFabricAudit.Listener.AttachedChildDrift", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFAuditChildListener::RunTest(const FString&)
{
    SFAudit::FWorldScope Scope;
    auto* Stage = Scope.World->SpawnActor<ASpatialStageVolume>();
    AActor* Child = SFAudit::Point(Scope.World);
    Child->AttachToActor(Stage, FAttachmentTransformRules::KeepWorldTransform);
    Stage->ListenerMode = EListenerMode::FollowPositionAndOrientation;
    Stage->ResolveListenerThisFrame();
    const double FirstX = Stage->GetActorLocation().X;
    Stage->ResolveListenerThisFrame();
    TestEqual(TEXT("Static attached listener must not move the stage again every frame"), Stage->GetActorLocation().X, FirstX);
    TestEqual(TEXT("Authored stage transform is not moved"), FirstX, 0.0);
    TestTrue(TEXT("Virtual listener origin still maps to center"), Stage->WorldToNormalized(Child->GetActorLocation()).IsNearlyZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFAuditEditorPipeline, "SpatialFabricAudit.Editor.FrameWithoutBeginPlay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFAuditEditorPipeline::RunTest(const FString&)
{
    SFAudit::FWorldScope Scope(EWorldType::Editor);
    auto* Manager = Scope.World->SpawnActor<ASpatialFabricManagerActor>();
    FSpatialObjectBinding Binding;
    Binding.TargetActor = SFAudit::Point(Scope.World);
    Manager->ObjectBindings.Add(Binding);
    Manager->ProcessFrame(1.0f);
    TestEqual(TEXT("Editor ProcessFrame must populate live snapshot without PIE"), Manager->GetLastSnapshot().Objects.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFAuditNameOverride, "SpatialFabricAudit.Metadata.NameOverride", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFAuditNameOverride::RunTest(const FString&)
{
    SFAudit::FWorldScope Scope;
    auto* Manager = Scope.World->SpawnActor<ASpatialFabricManagerActor>();
    FSpatialObjectBinding Binding;
    Binding.TargetActor = SFAudit::Point(Scope.World);
    Binding.Label = TEXT("Binding label");
    Binding.ADMNameOverride = TEXT("Requested ADM name");
    Binding.bADMSendName = true;
    const auto Snapshot = Manager->RegistryComponent->BuildSnapshot({Binding}, nullptr);
    if (!TestEqual(TEXT("One object"), Snapshot.Objects.Num(), 1)) return false;
    FADMOSCAdapter Adapter;
    FSpatialAdapterConfig Config;
    Adapter.Configure(Config);
    const auto Messages = Adapter.BuildMessages(Snapshot);
    const auto* Name = Messages.FindByPredicate([](const FSpatialOSCMessage& M) { return M.Address.EndsWith(TEXT("/name")); });
    TestTrue(TEXT("Name message exists"), Name != nullptr);
    if (Name) TestEqual(TEXT("ADM name override reaches actual encoder"), Name->Strings[0], Binding.ADMNameOverride);
    TestEqual(TEXT("Display label is not overwritten"), Snapshot.Objects[0].Label, Binding.Label);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFEncoderRegression, "SpatialFabric.Regression.EncodedPackets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFEncoderRegression::RunTest(const FString&)
{
    FSpatialFrameSnapshot Snapshot;
    FSpatialNormalizedState State;
    State.ObjectID = 4; State.StageNormalized = FVector(1,1,1);
    State.ADMName = TEXT("Override"); State.bADMSendName = true; State.Dref = 0.4f; State.Dmax = 20;
    Snapshot.Objects.Add(State);
    FSpatialAdapterConfig Config; Config.ADMCoordinateMode = EADMCoordinateMode::Both;
    FADMOSCAdapter ADM; ADM.Configure(Config);
    const auto Packets = ADM.BuildMessages(Snapshot);
    auto Find = [&](const TCHAR* Suffix) { return Packets.FindByPredicate([&](const FSpatialOSCMessage& M) { return M.Address.EndsWith(Suffix); }); };
    const auto* XYZ = Find(TEXT("/xyz")); const auto* AED = Find(TEXT("/aed"));
    TestTrue(TEXT("Both position messages exist"), XYZ && AED);
    if (XYZ && AED) TestTrue(TEXT("Both encodings have the same radius including corners"), FMath::IsNearlyEqual(FVector(XYZ->Floats[0],XYZ->Floats[1],XYZ->Floats[2]).Size(), double(AED->Floats[2]), 1.e-5));
    TestTrue(TEXT("Distance reference encoded"), Find(TEXT("/dref")) != nullptr);
    TestTrue(TEXT("Max distance encoded"), Find(TEXT("/dmax")) != nullptr);
    Snapshot.Objects[0].bADMSendMute = true;
    TestFalse(TEXT("A send-flag change changes the cached packet representation"), ADM.BuildMessages(Snapshot) == Packets);
    Config.CustomSendMode = ECustomSendMode::Discrete; Config.CustomArgTemplate = TEXT("invalid x y"); Config.CustomAddressTemplate = TEXT("/object/{id}/{axis}");
    FCustomAdapter Custom; Custom.Configure(Config);
    TestEqual(TEXT("Unknown tokens rejected, never shift values to wrong axis"), Custom.BuildMessages(Snapshot).Num(), 0);
    Config.CustomArgTemplate = TEXT("x y"); Custom.Configure(Config);
    const auto Before = Custom.BuildMessages(Snapshot);
    TestEqual(TEXT("Discrete produces two messages"), Before.Num(), 2);
    if (Before.Num()) TestEqual(TEXT("Discrete first address"), Before[0].Address, FString(TEXT("/object/4/x")));
    Snapshot.Objects[0].CustomFields.Add(TEXT("slot"), TEXT("A"));
    Config.CustomAddressTemplate = TEXT("/{slot}/{axis}"); Custom.Configure(Config);
    const auto A = Custom.BuildMessages(Snapshot);
    Snapshot.Objects[0].CustomFields[TEXT("slot")] = TEXT("B");
    TestFalse(TEXT("Custom field changes invalidate packet cache"), Custom.BuildMessages(Snapshot) == A);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFStageRebinding, "SpatialFabric.Regression.StageRebindingAndLogBounds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFStageRebinding::RunTest(const FString&)
{
    SFAudit::FWorldScope Scope;
    auto* M = Scope.World->SpawnActor<ASpatialFabricManagerActor>();
    auto* A = Scope.World->SpawnActor<ASpatialStageVolume>();
    auto* B = Scope.World->SpawnActor<ASpatialStageVolume>();
    B->SetActorLocation(FVector(100,0,0));
    FSpatialObjectBinding Binding; Binding.TargetActor = SFAudit::Point(Scope.World); M->ObjectBindings.Add(Binding);
    M->StageVolume = A; M->ProcessFrame(0);
    TestTrue(TEXT("Stage A local position"), FMath::IsNearlyEqual(M->GetLastSnapshot().Objects[0].StageNormalized.X, 0.1));
    M->StageVolume = B; M->ProcessFrame(0);
    TestTrue(TEXT("Valid stage reassignment takes effect immediately"), M->GetLastSnapshot().Objects[0].StageNormalized.IsNearlyZero());
    M->StageVolume.Reset(); M->ProcessFrame(0);
    TestTrue(TEXT("Cleared assignment restores manager fallback"), FMath::IsNearlyEqual(M->GetLastSnapshot().Objects[0].StageNormalized.X, 0.1));
    FSpatialFabricLogEntry Entry; M->AppendLog(Entry);
    TestEqual(TEXT("Negative log count is safe"), M->GetRecentLog(-1).Num(), 0);
    TestEqual(TEXT("Oversized log count is clamped"), M->GetRecentLog(MAX_int32).Num(), 1);
    A->SetActorRotation(FRotator(0,90,0));
    TestTrue(TEXT("Production stage rotates world offsets"), FMath::IsNearlyEqual(A->WorldToNormalized(FVector(0,500,0)).X, 0.5));
    TestTrue(TEXT("Preview still works with networking disabled"), M->GetRouter() == nullptr && M->GetLastSnapshot().Objects.Num() == 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFWorldResolution, "SpatialFabric.Regression.WorldOwnedResolution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFWorldResolution::RunTest(const FString&)
{
    SFAudit::FWorldScope First, Second;
    FActorSpawnParameters Params; Params.Name = TEXT("SharedPoint");
    AActor* A = First.World->SpawnActor<AActor>(Params);
    AActor* B = Second.World->SpawnActor<AActor>(Params);
    for (AActor* Actor : {A,B}) { auto* Root = NewObject<USceneComponent>(Actor); Actor->SetRootComponent(Root); Root->RegisterComponent(); }
    A->SetActorLocation(FVector(100,0,0)); B->SetActorLocation(FVector(600,0,0));
    auto* M = Second.World->SpawnActor<ASpatialFabricManagerActor>();
    FSpatialObjectBinding Binding; Binding.TargetActor = A;
    const auto Snapshot = M->RegistryComponent->BuildSnapshot({Binding}, nullptr);
    TestEqual(TEXT("Cross-world soft pointer resolved in owning world"), Snapshot.Objects.Num(), 1);
    if (Snapshot.Objects.Num()) TestTrue(TEXT("Uses second world actor, not editor/source transform"), FMath::IsNearlyEqual(Snapshot.Objects[0].StageNormalized.X, 0.6));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFStageVisibility, "SpatialFabric.Regression.StageVisibilityEditorAndGame", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFStageVisibility::RunTest(const FString&)
{
    for (const auto Type : {EWorldType::Editor, EWorldType::Game})
    {
        SFAudit::FWorldScope Scope(Type);
        auto* Stage = Scope.World->SpawnActor<ASpatialStageVolume>();
        auto* Manager = Scope.World->SpawnActor<ASpatialFabricManagerActor>();
        Manager->StageVolume = Stage;
        const FTransform Authored = Stage->GetActorTransform();
        // Simulate a legacy asset or an editor Game View-hidden box, including selection-only state.
        Stage->StageBox->SetHiddenInGame(true);
        Stage->StageBox->SetVisibility(false);
        Stage->StageBox->bDrawOnlyIfSelected = true;
        Manager->bHideStageInPIE = false;
        Manager->ApplyStageVisibility();
        TestFalse(TEXT("Visible actor is not hidden"), Stage->IsHidden());
        TestFalse(TEXT("Visible box is not suppressed in Game View"), Stage->StageBox->bHiddenInGame);
        TestTrue(TEXT("Box component visibility is restored"), Stage->StageBox->IsVisible());
        TestFalse(TEXT("Unselected volumes remain visible"), Stage->StageBox->bDrawOnlyIfSelected);
        Manager->bHideStageInPIE = true;
        Manager->ApplyStageVisibility();
        const bool Game = Scope.World->IsGameWorld();
        TestEqual(TEXT("PIE/game hide option does not hide the editor volume"), Stage->IsHidden(), Game);
        TestEqual(TEXT("Box hidden flag follows world-specific policy"), bool(Stage->StageBox->bHiddenInGame), Game);
        TestEqual(TEXT("Box visibility follows world-specific policy"), Stage->StageBox->IsVisible(), !Game);
        Manager->bHideStageInPIE = false;
        Manager->ApplyStageVisibility();
        TestTrue(TEXT("Toggle restores visibility without selection"), Stage->StageBox->IsVisible() && !Stage->StageBox->bHiddenInGame && !Stage->IsHidden());
        TestTrue(TEXT("Visibility never moves the authored stage"), Stage->GetActorTransform().Equals(Authored));
    }
    return true;
}
#endif
