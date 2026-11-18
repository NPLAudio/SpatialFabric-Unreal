// Temporary audit probe for a valid RIFF file; no malformed/memory-unsafe input is executed.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "AudioLoomPcmLoader.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "AudioLoomOscSettings.h"
#include "Misc/PackageName.h"
#include "Sound/SoundWave.h"
#include "UObject/SavePackage.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFLegacySidecarAndBudget, "SpatialFabric.Playback.LegacySidecarAndBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFLegacySidecarAndBudget::RunTest(const FString&)
{
    UAudioLoomOscSettings* Settings = GetMutableDefault<UAudioLoomOscSettings>();
    TGuardValue<int32> BudgetGuard(Settings->MaxDecodedAudioMiB, 256);
    TGuardValue<bool> SidecarGuard(Settings->bAllowLegacyWavSidecar, true);
    TestEqual(TEXT("Configured 256 MiB budget remains bounded"), FAudioLoomPcmLoader::GetMaxDecodedBytes(), 256ll * 1024 * 1024);
    TestTrue(TEXT("Boundary samples fit"), FAudioLoomPcmLoader::IsDecodedSizeAllowed(256ll * 1024 * 1024 / 4));
    TestFalse(TEXT("One extra sample fails"), FAudioLoomPcmLoader::IsDecodedSizeAllowed(256ll * 1024 * 1024 / 4 + 1));
    Settings->MaxDecodedAudioMiB = 1024;
    TestTrue(TEXT("Long cues can opt into a larger budget without allocating in this test"), FAudioLoomPcmLoader::IsDecodedSizeAllowed(512ll * 1024 * 1024 / 4));
    TestFalse(TEXT("Negative count is rejected"), FAudioLoomPcmLoader::IsDecodedSizeAllowed(-1));
    TestFalse(TEXT("Index overflow is rejected"), FAudioLoomPcmLoader::IsDecodedSizeAllowed(int64(MAX_int32) + 1));
    Settings->MaxDecodedAudioMiB = MAX_int32;
    TestEqual(TEXT("Huge config is clamped"), FAudioLoomPcmLoader::GetMaxDecodedBytes(), 8191ll * 1024 * 1024);
    Settings->MaxDecodedAudioMiB = -1;
    TestEqual(TEXT("Negative config is clamped"), FAudioLoomPcmLoader::GetMaxDecodedBytes(), 1024ll * 1024);

    TArray<uint8> Wav;
    auto Tag = [&](const char* S) { for (int i = 0; i < 4; ++i) Wav.Add(uint8(S[i])); };
    auto U16 = [&](uint16 V) { Wav.Add(V & 255); Wav.Add((V >> 8) & 255); };
    auto U32 = [&](uint32 V) { for (int i = 0; i < 4; ++i) Wav.Add((V >> (8*i)) & 255); };
    Tag("RIFF"); U32(38); Tag("WAVE");
    Tag("fmt "); U32(16); U16(1); U16(1); U32(48000); U32(96000); U16(2); U16(16);
    Tag("data"); U32(2); U16(16384);
    const FString Package = FPaths::CreateTempFilename(*FPaths::ProjectContentDir(), TEXT("SFLegacy"), TEXT(".uasset"));
    const FString Sidecar = FPaths::ChangeExtension(Package, TEXT("wav"));
    if (!FFileHelper::SaveArrayToFile(Wav, *Sidecar)) { AddError(TEXT("Cannot write sidecar fixture")); return false; }
    // Exercise the actual SoundWave -> package filename -> sibling resolution too.
    UPackage* FixturePackage = CreatePackage(*(TEXT("/Game/") + FPaths::GetBaseFilename(Package)));
    USoundWave* Wave = NewObject<USoundWave>(FixturePackage, TEXT("SidecarCue"), RF_Public | RF_Standalone);
    Wave->bStreaming = true;
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    const bool bSaved = UPackage::SavePackage(FixturePackage, Wave, *Package, SaveArgs);
    TestTrue(TEXT("Package fixture saved"), bSaved);
    if (bSaved)
        TestTrue(TEXT("SoundWave resolves the package sibling WAV"), FAudioLoomPcmLoader::LoadFromSoundWave(Wave).bSuccess);
    IFileManager::Get().Delete(*Package);
    const auto Result = FAudioLoomPcmLoader::LoadLegacySidecar(Package);
    TestTrue(TEXT("Sibling WAV loads"), Result.bSuccess);
    TestEqual(TEXT("Sibling samples are decoded"), Result.PCM.Num(), 1);
    Settings->bAllowLegacyWavSidecar = false;
    TestFalse(TEXT("Explicit opt-out prevents file fallback"), FAudioLoomPcmLoader::LoadLegacySidecar(Package).bSuccess);
    Settings->bAllowLegacyWavSidecar = true;
    IFileManager::Get().Delete(*Sidecar);
    TestFalse(TEXT("Missing sibling fails closed"), FAudioLoomPcmLoader::LoadLegacySidecar(Package).bSuccess);
    return true;
}
#endif
