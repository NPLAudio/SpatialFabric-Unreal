// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AudioLoomRoutingCsv.h"
#include "AudioLoomActor.h"
#include "AudioLoomComponent.h"
#include "Engine/World.h"
#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFCSVRegression, "SpatialFabric.CSV.ValidationMultilineAndUndo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFCSVRegression::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
    auto* Actor = World->SpawnActor<AAudioLoomActor>();
    Actor->SetActorLabel(TEXT("Quoted, \"actor\"\nsecond line"));
    auto* Component = Actor->AudioLoomComponent.Get();
    Component->SetFlags(RF_Transactional);
    Component->OutputChannel = 3;
    const FString CSV = FAudioLoomRoutingCsv::BuildRoutingCsv(World);
    Component->OutputChannel = 0;
    int32 Applied = 0; FString Errors;
    TestTrue(TEXT("Exported multiline quoted CSV imports"), FAudioLoomRoutingCsv::ApplyRoutingCsv(World, CSV, Applied, Errors));
    TestEqual(TEXT("Exactly one record imported"), Applied, 1);
    TestEqual(TEXT("Routing round-trips"), Component->OutputChannel, 3);
    if (GEditor)
    {
        GEditor->UndoTransaction();
        TestEqual(TEXT("Import captures state BEFORE mutation"), Component->OutputChannel, 0);
    }
    FString Invalid = CSV.Replace(TEXT(",3,"), TEXT(",oops,"));
    TestFalse(TEXT("Invalid channel is rejected instead of silently routing all outputs"), FAudioLoomRoutingCsv::ApplyRoutingCsv(World, Invalid, Applied, Errors));
    TestEqual(TEXT("Invalid row did not mutate routing"), Component->OutputChannel, 0);
    TestFalse(TEXT("Unterminated CSV quote is rejected"), FAudioLoomRoutingCsv::ApplyRoutingCsv(World, CSV + TEXT("\"broken"), Applied, Errors));
    World->DestroyWorld(false);
    return true;
}
#endif
