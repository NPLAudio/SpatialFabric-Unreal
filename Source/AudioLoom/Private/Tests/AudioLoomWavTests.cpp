// Temporary audit probe for a valid RIFF file; no malformed/memory-unsafe input is executed.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "AudioLoomPcmLoader.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFAuditOddRiffChunk, "SpatialFabricAudit.Playback.OddPaddedRiffChunk", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFAuditOddRiffChunk::RunTest(const FString&)
{
    TArray<uint8> Wav;
    auto Tag = [&](const char* S) { for (int i = 0; i < 4; ++i) Wav.Add((uint8)S[i]); };
    auto U16 = [&](uint16 V) { Wav.Add(V & 255); Wav.Add((V >> 8) & 255); };
    auto U32 = [&](uint32 V) { for (int i = 0; i < 4; ++i) Wav.Add((V >> (8*i)) & 255); };
    Tag("RIFF"); U32(48); Tag("WAVE");
    Tag("JUNK"); U32(1); Wav.Add(42); Wav.Add(0); // mandatory even-byte padding
    Tag("fmt "); U32(16); U16(1); U16(1); U32(48000); U32(96000); U16(2); U16(16);
    Tag("data"); U32(2); U16(16384);
    const FString Filename = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("SFAudit"), TEXT(".wav"));
    if (!FFileHelper::SaveArrayToFile(Wav, *Filename)) { AddError(TEXT("Cannot write WAV fixture")); return false; }
    const auto Result = FAudioLoomPcmLoader::LoadFromFile(Filename);
    IFileManager::Get().Delete(*Filename);
    TestTrue(TEXT("Valid WAV with odd-length padded JUNK must decode"), Result.bSuccess);
    TestEqual(TEXT("Expected one PCM sample"), Result.PCM.Num(), 1);
    return true;
}
#endif
