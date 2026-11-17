// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AudioLoomOscSubsystem.h"
#include "OSCManager.h"
#include "OSCMessage.h"
#include "AudioLoomComponent.h"
#include "AudioLoomLiveInputComponent.h"
#include "AudioLoomPlaybackBackend.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFOSCTrigger, "SpatialFabric.OSC.TypedPlayTriggers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFOSCTrigger::RunTest(const FString&)
{
    FOSCMessage Empty;
    TestTrue(TEXT("Address only plays"), UAudioLoomOscSubsystem::IsPlayTrigger(Empty));
    FOSCMessage Zero; UOSCManager::AddInt32(Zero, 0);
    TestFalse(TEXT("Integer zero does not play"), UAudioLoomOscSubsystem::IsPlayTrigger(Zero));
    FOSCMessage One; UOSCManager::AddInt32(One, 1);
    TestTrue(TEXT("Integer one plays"), UAudioLoomOscSubsystem::IsPlayTrigger(One));
    FOSCMessage Half; UOSCManager::AddFloat(Half, 0.5f);
    TestTrue(TEXT("Float threshold is inclusive"), UAudioLoomOscSubsystem::IsPlayTrigger(Half));
    FOSCMessage String; UOSCManager::AddString(String, TEXT("0"));
    TestFalse(TEXT("String is not an address-only trigger"), UAudioLoomOscSubsystem::IsPlayTrigger(String));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFOscAckFixtures, "SpatialFabric.OSC.AckFixtures", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFOscAckFixtures::RunTest(const FString&)
{
    auto CheckDecision = [this](bool bLive, const TCHAR* Command, TFunction<void(FOSCMessage&)> Fill, const TCHAR* Result, bool bActs)
    {
        FOSCMessage M; Fill(M);
        const auto D = UAudioLoomOscSubsystem::DecideCommand(Command, M, bLive);
        TestEqual(FString(Command) + TEXT(" result"), D.Result, FString(Result));
        TestEqual(FString(Command) + TEXT(" acts"), D.bAct, bActs);
    };
    auto CheckPacket = [this](const TCHAR* Base, const TCHAR* Command, const TCHAR* Result, bool bPlaying, const TCHAR* Detail, const TCHAR* Hex)
    {
        const auto Bytes = UAudioLoomOscSubsystem::MakeAcknowledgement(Base, Command, Result, bPlaying, Detail);
        FString Actual;
        for (uint8 Byte : Bytes) Actual += FString::Printf(TEXT("%02x"), Byte);
        TestEqual(FString(Command) + TEXT(" golden wire bytes"), Actual, FString(Hex));
    };
#include "AudioLoomAckFixtures.inl"
    FOSCMessage Mixed; UOSCManager::AddInt32(Mixed, 1); UOSCManager::AddFloat(Mixed, 0.1f);
    TestFalse(TEXT("Low float beats high int"), UAudioLoomOscSubsystem::DecideCommand(TEXT("play"), Mixed, false).bAct);
    FOSCMessage Half; UOSCManager::AddFloat(Half, 0.5f);
    const auto Loop = UAudioLoomOscSubsystem::DecideCommand(TEXT("loop"), Half, false);
    TestTrue(TEXT("Half disables loop but is acted on"), Loop.bAct && !(Loop.Value > 0.5f));
    TestEqual(TEXT("Status rejects payload"), UAudioLoomOscSubsystem::DecideCommand(TEXT("status"), Half, true).Result, FString(TEXT("rejected")));
    const auto Sanitized = UAudioLoomOscSubsystem::MakeAcknowledgement(TEXT("/test"), FString::ChrN(40, TEXT('.')), TEXT("rejected"), false, TEXT(""));
    const auto Expected = UAudioLoomOscSubsystem::MakeAcknowledgement(TEXT("/test"), FString::ChrN(32, TEXT('_')), TEXT("rejected"), false, TEXT(""));
    TestTrue(TEXT("Command sanitized and limited to 32 chars"), Sanitized == Expected);
    return true;
}

// No listener, device or source is opened: incoming commands and worker observations are injected.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFOscAckLifecycle, "SpatialFabric.OSC.AckLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFOscAckLifecycle::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Sub = World->GetSubsystem<UAudioLoomOscSubsystem>();
    if (!TestNotNull(TEXT("OSC world subsystem"), Sub)) { World->DestroyWorld(false); return false; }
    AActor* Actor = World->SpawnActor<AActor>();
    auto* Playback = NewObject<UAudioLoomComponent>(Actor);
    Actor->AddInstanceComponent(Playback); Playback->RegisterComponent(); Playback->SetOscAddress(TEXT("/test/playback"));
    auto* Live = NewObject<UAudioLoomLiveInputComponent>(Actor);
    Actor->AddInstanceComponent(Live); Live->RegisterComponent(); Live->SetOscAddress(TEXT("/test/live"));
    Sub->RebuildComponentRegistry();
    TArray<FString> Replies;
    Sub->AckObserver = [&Replies](const FString& Command, const FString& Result, bool bPlaying, const FString& Detail)
    { Replies.Add(Command + TEXT(" ") + Result + (bPlaying ? TEXT(" 1") : TEXT(" 0")) + (Detail.IsEmpty() ? TEXT("") : TEXT(" detail"))); };
    Sub->StateObserver = [&Replies](bool bPlaying) { Replies.Add(bPlaying ? TEXT("state 1") : TEXT("state 0")); };
    auto Dispatch = [Sub](const TCHAR* Path)
    {
        FOSCMessage M; M.SetAddress(UOSCManager::ConvertStringToOSCAddress(Path));
        Sub->HandleOSCMessage(M, TEXT("127.0.0.1"), 9999);
    };
    Dispatch(TEXT("/test/playback/play")); // missing source is a synchronous failure, never opens a device
    TestEqual(TEXT("Received then synchronous failure"), FString::Join(Replies, TEXT("|")), FString(TEXT("play received 0|play failed 0 detail")));
    Replies.Reset();
    Playback->bStartPending = true;
    Playback->UpdateTickEnabled();
    TestTrue(TEXT("Pending worker keeps polling"), Playback->IsComponentTickEnabled());
    TestFalse(TEXT("Request is not observed playing"), Playback->IsPlaying());
    Playback->HandlePlaybackEvent({false, false, TEXT("device refused")});
    TestEqual(TEXT("Async failure even without prior state 1"), FString::Join(Replies, TEXT("|")), FString(TEXT("play failed 0 detail|state 0")));
    TestFalse(TEXT("Failure clears pending"), Playback->bStartPending);
    Replies.Reset();
    Live->bStartPending = true;
    Dispatch(TEXT("/test/live/status"));
    TestEqual(TEXT("Pending status is not live"), FString::Join(Replies, TEXT("|")), FString(TEXT("status received 0|state 0")));
    Replies.Reset();
    Live->HandleEvent({false, false, TEXT("microphone unavailable")});
    TestEqual(TEXT("Live async refusal"), FString::Join(Replies, TEXT("|")), FString(TEXT("play failed 0 detail|state 0")));
    Replies.Reset();
    Live->HandleEvent({true, false});
    Dispatch(TEXT("/test/live/status"));
    TestEqual(TEXT("Observed live state and late controller status"), FString::Join(Replies, TEXT("|")), FString(TEXT("state 1|status received 1|state 1")));
    Replies.Reset();
    Dispatch(TEXT("/test/live/bogus"));
    Dispatch(TEXT("/test/live/foo/bar"));
    Dispatch(TEXT("/test/live/state"));
    Dispatch(TEXT("/unknown/status"));
    TestEqual(TEXT("Known unknown suffix rejected, engine-only ignored, unknown base silent"), FString::Join(Replies, TEXT("|")), FString(TEXT("bogus rejected 1 detail|foo/bar rejected 1 detail|state ignored 1 detail")));
    Replies.Reset();
    Live->HandleEvent({false, false});
    Dispatch(TEXT("/test/live/stop"));
    TestTrue(TEXT("Ending and idle stop report stopped"), Replies.Contains(TEXT("state 0")) && Replies.Contains(TEXT("stop received 0")));
    // A stop delegate may rebuild the registry or try to restart during unregister.
    // Teardown must reject that request before opening any device.
    Live->HandleEvent({true, false});
    bool bRestartAccepted = true;
    Live->OnLiveStateChangedNative.AddLambda([&](bool bLive)
    {
        if (!bLive) { Live->SetOscAddress(TEXT("/test/changed")); bRestartAccepted = Live->StartLive(); }
    });
    Live->UnregisterComponent();
    TestFalse(TEXT("State callback cannot restart capture during teardown"), bRestartAccepted);
    TestFalse(TEXT("Unregistered component has no pending start"), Live->bStartPending);
    Sub->AckObserver = nullptr; Sub->StateObserver = nullptr;
    World->DestroyWorld(false);
    return true;
}
#endif
