// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AudioLoomPcmLoader.h"
#include "AudioLoomPcmRender.h"
#include "AudioLoomPlaybackBackend.h"
#include "AudioLoomComponent.h"
#include "AudioLoomActor.h"
#include "Sound/SoundWave.h"
#include "Engine/World.h"

namespace
{
TArray<uint8> WavFixture(uint16 Format = 1, uint16 Bits = 16)
{
    TArray<uint8> B = {'R','I','F','F',38,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
        1,0,1,0,128,187,0,0,0,119,1,0,2,0,16,0,'d','a','t','a',2,0,0,0,0,64};
    B[20] = uint8(Format); B[21] = uint8(Format >> 8); B[34] = uint8(Bits); B[35] = uint8(Bits >> 8);
    return B;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFPCMValidation, "SpatialFabric.Playback.WavValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFPCMValidation::RunTest(const FString&)
{
    const auto Valid = WavFixture();
    const auto Decoded = FAudioLoomPcmLoader::LoadFromMemory(Valid);
    TestTrue(TEXT("Real parser decodes PCM16"), Decoded.bSuccess);
    if (Decoded.PCM.Num()) TestEqual(TEXT("PCM sample value"), Decoded.PCM[0], 0.5f);
    for (int32 N = 0; N < Valid.Num(); ++N)
    {
        auto Truncated = Valid; Truncated.SetNum(N);
        TestFalse(FString::Printf(TEXT("Reject truncation at byte %d"), N), FAudioLoomPcmLoader::LoadFromMemory(Truncated).bSuccess);
    }
    TestFalse(TEXT("Reject zero bit depth before dividing"), FAudioLoomPcmLoader::LoadFromMemory(WavFixture(1, 0)).bSuccess);
    auto Bad = Valid; Bad[22] = 0;
    TestFalse(TEXT("Reject zero channels"), FAudioLoomPcmLoader::LoadFromMemory(Bad).bSuccess);
    Bad = Valid; Bad[32] = 9;
    TestFalse(TEXT("Reject wrong block alignment"), FAudioLoomPcmLoader::LoadFromMemory(Bad).bSuccess);
    Bad = WavFixture(3, 32); Bad[32] = 4;
    TestFalse(TEXT("Reject incomplete float frame without copying"), FAudioLoomPcmLoader::LoadFromMemory(Bad).bSuccess);
    Bad = Valid; Bad[16] = 255; Bad[17] = 255; Bad[18] = 255; Bad[19] = 255;
    TestFalse(TEXT("Reject oversized fmt payload before reading"), FAudioLoomPcmLoader::LoadFromMemory(Bad).bSuccess);
    auto Float = WavFixture(3, 32); Float.SetNumZeroed(48); Float[4] = 40; Float[32] = 4; Float[40] = 4;
    Float[28] = 0; Float[29] = 238; Float[30] = 2; Float[31] = 0;
    Float[44] = 0; Float[45] = 0; Float[46] = 0; Float[47] = 63;
    const auto FloatPCM = FAudioLoomPcmLoader::LoadFromMemory(Float);
    TestTrue(TEXT("IEEE float WAV decodes"), FloatPCM.bSuccess);
    if (FloatPCM.PCM.Num()) TestEqual(TEXT("Float WAV sample"), FloatPCM.PCM[0], 0.5f);
    Float[46] = 192; Float[47] = 127;
    TestFalse(TEXT("Non-finite float samples rejected"), FAudioLoomPcmLoader::LoadFromMemory(Float).bSuccess);
    // Small deterministic corpus covers chunk sizes/format mutations without large allocations.
    for (int32 I = 0; I < 512; ++I)
    {
        Bad = Valid;
        Bad[(I * 17) % Bad.Num()] ^= uint8(I * 31);
        const auto R = FAudioLoomPcmLoader::LoadFromMemory(Bad);
        if (R.bSuccess) { TestTrue(TEXT("Successful parse has whole frames"), R.PCM.Num() > 0 && R.NumChannels > 0 && R.PCM.Num() % R.NumChannels == 0); }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFPCMRender, "SpatialFabric.Playback.RateRoutingAndEOF", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFPCMRender::RunTest(const FString&)
{
    TArray<float> Source = {0.f, 1.f};
    FAudioLoomPcmCursor Cursor(Source, 1, 48000, 96000);
    float Buffer[24];
    TestTrue(TEXT("Two source frames last four hardware frames at 96k"), Cursor.Render(Buffer, 4, 4, 3, false));
    TestEqual(TEXT("First interpolation sample"), Buffer[2], 0.f);
    TestEqual(TEXT("Fractional source interpolation"), Buffer[6], 0.5f);
    TestEqual(TEXT("Last valid source sample is not silenced"), Buffer[14], 1.f);
    for (int32 F = 0; F < 4; ++F) { TestEqual(TEXT("Unrouted left silent"), Buffer[F * 4], 0.f); TestEqual(TEXT("Unrouted right silent"), Buffer[F * 4 + 1], 0.f); }
    Cursor.Render(Buffer, 2, 4, 3, false);
    for (int32 I = 0; I < 8; ++I) TestEqual(TEXT("Only samples AFTER EOF are silent"), Buffer[I], 0.f);
    FAudioLoomPcmCursor Loop(Source, 1, 48000, 48000);
    TestFalse(TEXT("Loop has no completion"), Loop.Render(Buffer, 6, 1, 0, true));
    TestEqual(TEXT("Loop wraps"), Buffer[4], 0.f);
    TestTrue(TEXT("Turning loop off reaches natural EOF"), Loop.Render(Buffer, 6, 1, 0, false));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFPCMEncoding, "SpatialFabric.Playback.PackedOutputFormats", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFPCMEncoding::RunTest(const FString&)
{
    const float Values[] = {1.f, -1.f, 0.f};
    for (int32 Bits : {16,24,32})
    {
        uint8 Bytes[32]; FMemory::Memset(Bytes, 0xcc, sizeof(Bytes));
        TestTrue(TEXT("Supported integer encoding"), AudioLoomEncodePCM(Values, 3, Bytes, false, Bits, Bits));
        TestEqual(TEXT("Positive full-scale high byte"), Bytes[Bits / 8 - 1], uint8(0x7f));
        TestEqual(TEXT("No write beyond exact device sample width"), Bytes[3 * Bits / 8], uint8(0xcc));
    }
    uint8 Bytes[32];
    TestTrue(TEXT("24 valid bits in 32-bit output container"), AudioLoomEncodePCM(Values, 3, Bytes, false, 32, 24));
    TestEqual(TEXT("Valid-bit output is left aligned"), Bytes[0], uint8(0));
    TestEqual(TEXT("Valid-bit positive full scale"), Bytes[3], uint8(0x7f));
    TestTrue(TEXT("Float output"), AudioLoomEncodePCM(Values, 3, Bytes, true, 32, 32));
    float Decoded; FMemory::Memcpy(&Decoded, Bytes, 4);
    TestEqual(TEXT("IEEE float is not written as int16"), Decoded, 1.f);
    TestFalse(TEXT("Unsupported output rejected"), AudioLoomEncodePCM(Values, 3, Bytes, false, 8, 8));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFPlaybackHierarchy, "SpatialFabric.Playback.HierarchyAddresses", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFPlaybackHierarchy::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Root = World->SpawnActor<AAudioLoomActor>();
    auto* A = World->SpawnActor<AAudioLoomActor>();
    auto* B = World->SpawnActor<AAudioLoomActor>();
    A->AttachToActor(Root, FAttachmentTransformRules::KeepWorldTransform);
    B->AttachToActor(Root, FAttachmentTransformRules::KeepWorldTransform);
    TestNotEqual(TEXT("Distinct descendants never share root/1"), A->AudioLoomComponent->GetOscAddress(), B->AudioLoomComponent->GetOscAddress());
    Root->AudioLoomComponent->bAutoReplay = true;
    Root->AudioLoomComponent->ReplayDelaySeconds = 0;
    Root->AudioLoomComponent->Stop();
    TestFalse(TEXT("Explicit Stop does not keep replay ticking"), Root->AudioLoomComponent->IsComponentTickEnabled());
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFTransportState, "SpatialFabric.Playback.TransportReplayCancellation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFTransportState::RunTest(const FString&)
{
    auto* Component = NewObject<UAudioLoomComponent>();
    Component->SoundWave = NewObject<USoundWave>();
    Component->bAutoReplay = true; Component->ReplayDelaySeconds = 0;
    Component->HandlePlaybackEvent({false, true});
    TestTrue(TEXT("Zero delay still schedules natural replay"), Component->bReplayPending);
    TestEqual(TEXT("Zero delay is preserved"), Component->ReplayCountdown, 0.f);
    Component->Stop();
    TestFalse(TEXT("Explicit Stop cancels pending replay"), Component->bReplayPending);
    Component->HandlePlaybackEvent({false, false});
    TestFalse(TEXT("Failure/cancellation is not natural completion"), Component->bReplayPending);
    Component->SetLoop(true);
    Component->HandlePlaybackEvent({false, true});
    TestFalse(TEXT("Loop mode does not schedule a second replay path"), Component->bReplayPending);
    Component->SetLoop(false);
    Component->bRandomReplayDelay = true; Component->ReplayDelayMin = 0.2f; Component->ReplayDelayMax = 0.4f;
    Component->HandlePlaybackEvent({false, true});
    TestTrue(TEXT("Random delay is bounded"), Component->ReplayCountdown >= 0.2f && Component->ReplayCountdown <= 0.4f);
    Component->Stop();
    return true;
}
#endif
