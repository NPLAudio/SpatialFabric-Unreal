// Copyright (c) 2026 AudioLoom Contributors. MIT License.
// SILENT live-input tests: offline bridge/codec and intentionally invalid devices only. No microphone is opened.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AudioLoomLiveInputComponent.h"
#include "AudioLoomLiveInputActor.h"
#include "AudioLoomLiveBackend.h"
#include "AudioInputDeviceEnumerator.h"
#include "AudioLoomPcmRender.h"
#include "SpatialFabricLiveBridge.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"

namespace
{
TArray<float> Constant(int32 Frames, int32 Channels, std::initializer_list<float> Values)
{
    TArray<float> Out; Out.SetNumZeroed(Frames * Channels);
    for (int32 F = 0; F < Frames; ++F) { int32 C = 0; for (float V : Values) { if (C < Channels) Out[F * Channels + C] = V; ++C; } }
    return Out;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFLiveBridge, "SpatialFabric.LiveInput.BridgeMappingAndDrift", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFLiveBridge::RunTest(const FString&)
{
    using SpatialFabricLive::FBridge;
    TArray<float> In = Constant(2000, 2, {0.5f, -0.25f}), Out; Out.SetNumZeroed(2000 * 2);
    FBridge All(2, 48000, 2, 48000, 0, 0, 20);
    All.Push(In.GetData(), 100);
    TestEqual(TEXT("Not primed: silence, not an underrun"), All.Pull(Out.GetData(), 50, 1.f), 0);
    TestEqual(TEXT("Unprimed output is silent"), Out[0], 0.f);
    TestEqual(TEXT("No underrun before priming"), int32(All.Snapshot().Underruns), 0);
    All.Push(In.GetData(), 2000);
    TestEqual(TEXT("Primed pull succeeds"), All.Pull(Out.GetData(), 500, 1.f), 0);
    TestEqual(TEXT("All channels: left"), Out[0], 0.5f);
    TestEqual(TEXT("All channels: right"), Out[1], -0.25f);
    FBridge Routed(2, 48000, 2, 48000, 0, 2, 20);
    Routed.Push(In.GetData(), 2000); Routed.Pull(Out.GetData(), 10, 1.f);
    TestEqual(TEXT("Output 2 leaves output 1 silent"), Out[0], 0.f);
    TestEqual(TEXT("Output 2 receives the first source channel"), Out[1], 0.5f);
    FBridge Single(2, 48000, 2, 48000, 2, 0, 20);
    Single.Push(In.GetData(), 2000); Single.Pull(Out.GetData(), 10, 1.f);
    TestEqual(TEXT("Single input 2 lands on corresponding output 1"), Out[0], -0.25f);
    TestEqual(TEXT("Corresponding mode leaves output 2 silent"), Out[1], 0.f);
    FBridge Both(2, 48000, 3, 48000, 2, 3, 20);
    Both.Push(In.GetData(), 2000); Both.Pull(Out.GetData(), 4, 1.f);
    TestEqual(TEXT("Input 2 to output 3: output 1 silent"), Out[0], 0.f);
    TestEqual(TEXT("Input 2 to output 3: output 3"), Out[2], -0.25f);
    TArray<float> Ramp = Constant(1500, 1, {0.25f});
    FBridge Half(1, 24000, 1, 48000, 0, 0, 20);
    Half.Push(Ramp.GetData(), 1500); Half.Pull(Out.GetData(), 960, 1.f);
    const int32 Fill = Half.Snapshot().FillFrames;
    TestTrue(TEXT("24 kHz to 48 kHz consumes about half as many frames"), Fill >= 1000 && Fill <= 1040);
    TArray<float> Exact = Constant(960, 2, {0.1f, 0.1f});
    FBridge Under(2, 48000, 2, 48000, 0, 0, 20);
    Under.Push(Exact.GetData(), 960);
    const int32 Silent = Under.Pull(Out.GetData(), 2000, 1.f);
    TestTrue(TEXT("Underrun returns the silent tail"), Silent > 900 && Silent < 1200);
    TestEqual(TEXT("Underrun is counted once"), int32(Under.Snapshot().Underruns), 1);
    TestEqual(TEXT("Underrun tail is zero"), Out[2 * 1999], 0.f);
    TestFalse(TEXT("Underrun re-primes"), Under.Snapshot().bPrimed);
    TArray<float> Burst = Constant(1000, 1, {0.3f});
    FBridge Over(1, 48000, 1, 48000, 0, 0, 5);
    for (int32 I = 0; I < 3; ++I) Over.Push(Burst.GetData(), 1000);
    TestTrue(TEXT("Overflow drops oldest and is counted"), Over.Snapshot().Overruns >= 1 && Over.Snapshot().FillFrames <= 1920);
    TestEqual(TEXT("Captured frames are counted"), int32(Over.Snapshot().Captured), 3000);
    TArray<float> Loud = Constant(2000, 1, {0.5f});
    Loud[10] = NAN;
    FBridge Boost(1, 48000, 1, 48000, 0, 0, 20);
    Boost.Push(Loud.GetData(), 2000); Boost.Pull(Out.GetData(), 20, 4.f);
    TestEqual(TEXT("Gain clamps at full scale"), Out[0], 1.f);
    TestEqual(TEXT("Peak is observed"), Boost.TakePeak(), 0.5f);
    TestEqual(TEXT("Peak resets after being taken"), Boost.TakePeak(), 0.f);
    TestFalse(TEXT("Input channel beyond device rejected"), FBridge::IsValidConfig(2, 48000, 2, 48000, 3, 0, 20));
    TestFalse(TEXT("Output channel beyond device rejected"), FBridge::IsValidConfig(2, 48000, 2, 48000, 0, 3, 20));
    FBridge Invalid(2, 48000, 2, 48000, 3, 0, 20);
    TestFalse(TEXT("Invalid bridge reports invalid"), Invalid.IsValid());
    TestEqual(TEXT("Invalid bridge refuses to render"), Invalid.Pull(Out.GetData(), 1, 1.f), -1);
    TestEqual(TEXT("Out-of-range gain refused"), All.Pull(Out.GetData(), 1, 5.f), -1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFLiveDecode, "SpatialFabric.LiveInput.DecodeRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFLiveDecode::RunTest(const FString&)
{
    const float Values[] = {-1.f, 0.f, 0.5f, 0.999f};
    for (int32 Bits : {16, 24, 32})
    {
        uint8 Bytes[16]; float Back[4];
        TestTrue(TEXT("Encode"), AudioLoomEncodePCM(Values, 4, Bytes, false, Bits, Bits));
        TestTrue(TEXT("Decode"), AudioLoomDecodePCM(Bytes, 4, Back, false, Bits, Bits));
        TestTrue(TEXT("Negative full scale round trips"), FMath::Abs(Back[0] + 1.f) < 1e-3f);
        TestEqual(TEXT("Zero round trips"), Back[1], 0.f);
        TestTrue(TEXT("Mid value round trips"), FMath::Abs(Back[2] - 0.5f) < 1e-3f);
        TestTrue(TEXT("High value round trips"), FMath::Abs(Back[3] - 0.999f) < 2e-3f);
    }
    uint8 Bytes[16]; float Back[4];
    AudioLoomEncodePCM(Values, 4, Bytes, true, 32, 32);
    TestTrue(TEXT("Float decode"), AudioLoomDecodePCM(Bytes, 4, Back, true, 32, 32));
    TestEqual(TEXT("Float exact"), Back[2], 0.5f);
    AudioLoomEncodePCM(Values, 4, Bytes, false, 32, 24);
    TestTrue(TEXT("Valid-bit packing decodes"), AudioLoomDecodePCM(Bytes, 4, Back, false, 32, 24));
    TestTrue(TEXT("Valid-bit value"), FMath::Abs(Back[2] - 0.5f) < 1e-5f);
    TestFalse(TEXT("Unsupported width rejected"), AudioLoomDecodePCM(Bytes, 4, Back, false, 8, 8));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFLiveInputState, "SpatialFabric.LiveInput.ObservedStateAndOsc", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFLiveInputState::RunTest(const FString&)
{
    float Gain = -1.f;
    TestTrue(TEXT("Float gain"), UAudioLoomLiveInputComponent::ParseGainArguments({0.25f}, {}, Gain) && Gain == 0.25f);
    TestTrue(TEXT("Int gain"), UAudioLoomLiveInputComponent::ParseGainArguments({}, {2}, Gain) && Gain == 2.f);
    TestTrue(TEXT("First float beats first int"), UAudioLoomLiveInputComponent::ParseGainArguments({1.5f}, {3}, Gain) && Gain == 1.5f);
    TestFalse(TEXT("Over range ignored"), UAudioLoomLiveInputComponent::ParseGainArguments({4.1f}, {}, Gain));
    TestFalse(TEXT("Negative int ignored"), UAudioLoomLiveInputComponent::ParseGainArguments({}, {-1}, Gain));
    TestFalse(TEXT("No numeric argument ignored"), UAudioLoomLiveInputComponent::ParseGainArguments({}, {}, Gain));
    TestFalse(TEXT("NaN ignored"), UAudioLoomLiveInputComponent::ParseGainArguments({NAN}, {}, Gain));

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Actor = World->SpawnActor<AAudioLoomLiveInputActor>();
    auto* Live = Actor->LiveInputComponent.Get();
    TestFalse(TEXT("Live is off at startup"), Live->IsLive());
    TestFalse(TEXT("Gain above range refused"), Live->SetGain(5.f));
    TestFalse(TEXT("NaN gain refused"), Live->SetGain(NAN));
    TestEqual(TEXT("Refused gain leaves value"), Live->Gain, 1.f);
    TestTrue(TEXT("Valid gain accepted"), Live->SetGain(0.5f) && Live->Gain == 0.5f);
    TestTrue(TEXT("Default OSC base is under /audioloom and /live/"), Live->GetOscAddress().StartsWith(TEXT("/audioloom/")) && Live->GetOscAddress().Contains(TEXT("/live/")));
    TestFalse(TEXT("Invalid OSC override refused"), Live->SetOscAddress(TEXT("not valid")));
    TestTrue(TEXT("Valid OSC override accepted"), Live->SetOscAddress(TEXT("/test/live")) && Live->GetOscAddress() == TEXT("/test/live"));

    int32 TrueCount = 0, FalseCount = 0;
    Live->OnLiveStateChangedNative.AddLambda([&](bool bLive) { if (bLive) ++TrueCount; else ++FalseCount; });
    Live->HandleEvent({true, false});
    TestTrue(TEXT("Observed start becomes live"), Live->IsLive() && TrueCount == 1);
    Live->HandleEvent({true, false});
    TestEqual(TEXT("Duplicate observation does not re-announce"), TrueCount, 1);
    Live->HandleEvent({false, false});
    TestTrue(TEXT("Observed end clears live"), !Live->IsLive() && FalseCount == 1);

    // Intentionally invalid devices: the worker must fail without ever reporting live.
    // The backend logs each fail-closed refusal as an error: one for the bad device IDs, one for the bad channel.
    AddExpectedError(TEXT("SpatialFabric live input:"), EAutomationExpectedErrorFlags::Contains, 2);
    Live->InputDeviceId = TEXT("spatialfabric-intentionally-invalid-input-no-fallback");
    Live->OutputDeviceId = TEXT("spatialfabric-intentionally-invalid-output-no-fallback");
    TrueCount = 0;
    TestTrue(TEXT("Start is accepted as a request"), Live->StartLive());
    const double Deadline = FPlatformTime::Seconds() + 5.0;
    while (Live->GetLastLiveError().IsEmpty() && FPlatformTime::Seconds() < Deadline) { FPlatformProcess::Sleep(0.01f); Live->PollBackend(); }
    Live->PollBackend();
    TestFalse(TEXT("Invalid device never reports live"), Live->IsLive());
    TestEqual(TEXT("No live notification for a failed start"), TrueCount, 0);
#if PLATFORM_WINDOWS || PLATFORM_MAC
    TestTrue(TEXT("Failure explains the absence of a fallback"), Live->GetLastLiveError().Contains(TEXT("no default fallback")));
#endif
    Live->StopLive();
    TestFalse(TEXT("Stop after failure is not live"), Live->IsLive());
    TestFalse(TEXT("Polling stops once nothing is pending"), Live->IsComponentTickEnabled());

    // Invalid channel requests fail inside the backend before any device is touched.
    Live->InputChannel = 99;
    Live->InputDeviceId.Empty(); Live->OutputDeviceId.Empty();
    TestTrue(TEXT("Request accepted"), Live->StartLive());
    const double Deadline2 = FPlatformTime::Seconds() + 3.0;
    while (Live->GetLastLiveError().IsEmpty() && FPlatformTime::Seconds() < Deadline2) { FPlatformProcess::Sleep(0.01f); Live->PollBackend(); }
    TestFalse(TEXT("Out-of-range input channel never goes live"), Live->IsLive());
    Live->StopLive();
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFLiveInventory, "SpatialFabric.LiveInput.ExplicitInventory", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFLiveInventory::RunTest(const FString&)
{
    // Inventory only: no stream is opened and no microphone permission is requested.
    const TArray<FAudioOutputDeviceInfo> Inputs = FAudioInputDeviceEnumerator::GetInputDevices();
    for (const FAudioOutputDeviceInfo& Info : Inputs)
    {
        TestTrue(TEXT("Input has a stable ID"), !Info.DeviceId.IsEmpty());
        TestTrue(TEXT("Input has a name"), !Info.FriendlyName.IsEmpty());
        TestTrue(TEXT("Input has channels"), Info.NumChannels > 0);
    }
    TestFalse(TEXT("A missing configured ID never resolves to the default"), FAudioInputDeviceEnumerator::GetInputDeviceById(TEXT("spatialfabric-missing-input")).bIsValid);
    return true;
}
#endif
