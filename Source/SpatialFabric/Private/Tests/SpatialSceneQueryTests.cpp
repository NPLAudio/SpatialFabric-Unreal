// Copyright (c) 2026 SpatialFabric Contributors. MIT License.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "SpatialSceneQuery.h"
#include "SpatialSceneQueryLibrary.h"
#include "SpatialFabricManagerActor.h"
#include "SpatialStageVolume.h"
#include "SpatialOSCServerComponent.h"
#include "AudioLoomComponent.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "OSCManager.h"
#include "OSCMessage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSceneInventoryTest,"SpatialFabric.SceneQuery.InventoryPagingAndNoPlayback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSceneInventoryTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Editor,false);
    auto* Actor=World->SpawnActor<AActor>();
    auto* Cue=NewObject<UAudioLoomComponent>(Actor); Actor->AddInstanceComponent(Cue); Cue->RegisterComponent();
    Cue->SetOscAddress(TEXT("/test/discovery/cue")); Cue->DeviceId=TEXT("Never-open-this-device"); Cue->OutputChannel=2;
    auto* Manager=World->SpawnActor<ASpatialFabricManagerActor>(); auto* Stage=World->SpawnActor<ASpatialStageVolume>(); Manager->StageVolume=Stage;
    FSpatialObjectBinding B; B.Label=TEXT("Quoted \"object\" / 日本語"); B.TargetActor=Actor; B.DefaultObjectID=7;
    FSpatialAdapterTargetEntry Target; Target.ObjectIDOverride=42; B.Targets.Add(Target); Manager->ObjectBindings.Add(B);
#if WITH_EDITOR
    const FString OriginalLabel=Actor->GetActorLabel(false);
#endif
    const FString Json=USpatialSceneQueryLibrary::GetSceneJson(World,0,64); TSharedPtr<FJsonObject> Root;
    TestTrue(TEXT("Inventory is parseable JSON"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root));
    if (Root)
    {
        TestEqual(TEXT("Versioned schema"),Root->GetIntegerField(TEXT("schema_version")),1);
        const auto& Objects=Root->GetArrayField(TEXT("objects")); bool FoundCue=false,FoundBinding=false;
        for (const auto& Entry:Objects)
        {
            const auto O=Entry->AsObject(); const auto Kind=O->GetStringField(TEXT("kind"));
            if (Kind==TEXT("playback")) { FoundCue=true; TestEqual(TEXT("Exact explicit base"),O->GetStringField(TEXT("osc_base")),FString(TEXT("/test/discovery/cue"))); TestEqual(TEXT("Output channel retained"),O->GetIntegerField(TEXT("output_channel")),2); }
            if (Kind==TEXT("spatial_binding")) { FoundBinding=true; TestEqual(TEXT("Resolved target override"),O->GetArrayField(TEXT("targets"))[0]->AsObject()->GetIntegerField(TEXT("object_id")),42); }
        }
        TestTrue(TEXT("AudioLoom component discovered"),FoundCue); TestTrue(TEXT("Spatial binding discovered"),FoundBinding);
        TSharedPtr<FJsonObject> Page; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SpatialSceneQuery::BuildJson(World,0,2)),Page);
        TestEqual(TEXT("Page limit enforced"),Page->GetIntegerField(TEXT("returned")),2); TestEqual(TEXT("Next offset"),Page->GetIntegerField(TEXT("next_offset")),2);
        TestEqual(TEXT("Total independent of page size"),Page->GetIntegerField(TEXT("total")),Root->GetIntegerField(TEXT("total")));
    }
#if WITH_EDITOR
    TestEqual(TEXT("Query never synthesizes/caches an actor label"),Actor->GetActorLabel(false),OriginalLabel);
#endif
    TestFalse(TEXT("Query never started audio"),Cue->IsPlaying()); TestFalse(TEXT("Query never started server"),Manager->ServerComponent->IsListening());
    TestEqual(TEXT("Configured device unchanged"),Cue->DeviceId,FString(TEXT("Never-open-this-device")));
    World->DestroyWorld(false); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSceneWireTest,"SpatialFabric.SceneQuery.RequestBoundsAndUnicodeChunks",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSceneWireTest::RunTest(const FString&)
{
    FOSCMessage Message; UOSCManager::AddString(Message,TEXT("max_001")); UOSCManager::AddInt32(Message,9011); UOSCManager::AddInt32(Message,0); UOSCManager::AddInt32(Message,16);
    FString ID; int32 Port=0,Offset=0,Limit=0;
    TestTrue(TEXT("Typed request accepted"),SpatialSceneQuery::ParseRequest(Message,ID,Port,Offset,Limit)); TestEqual(TEXT("Reply port"),Port,9011);
    UOSCManager::AddInt32(Message,1); TestFalse(TEXT("Extra arguments rejected"),SpatialSceneQuery::ParseRequest(Message,ID,Port,Offset,Limit));
    FOSCMessage Bad; UOSCManager::AddString(Bad,TEXT("bad id")); UOSCManager::AddInt32(Bad,9011); TestFalse(TEXT("Unsafe request IDs rejected"),SpatialSceneQuery::ParseRequest(Bad,ID,Port,Offset,Limit));
    FString Text; for (int32 I=0;I<1600;++I) Text+=TEXT("日本語🎧");
    const auto Chunks=SpatialSceneQuery::SplitUTF8(Text); FString Joined;
    for (const auto& Chunk:Chunks) { FTCHARToUTF8 Bytes(*Chunk); TestTrue(TEXT("UTF8 chunk fits safe payload"),Bytes.Length()<=SpatialSceneQuery::ChunkBytes); Joined+=Chunk; }
    TestEqual(TEXT("Unicode reassembles losslessly"),Joined,Text); TestTrue(TEXT("Chunk count bounded"),Chunks.Num()<=SpatialSceneQuery::MaxChunks);
    return true;
}
#endif
