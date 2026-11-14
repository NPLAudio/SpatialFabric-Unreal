// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AudioLoomOscSubsystem.h"
#include "OSCManager.h"
#include "OSCMessage.h"
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
#endif
