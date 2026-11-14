// Copyright (c) 2026 SpatialFabric Contributors. MIT License.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "SpatialMath.h"
#include "Adapters/ADMOSCAdapter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpatialMathCenterTest, "SpatialFabric.Math.Center", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)
bool FSpatialMathCenterTest::RunTest(const FString&)
{
    TestTrue(TEXT("Production polar conversion at origin"), FSpatialMath::CartesianToPolar(FVector::ZeroVector).IsNearlyZero());
    TestEqual(TEXT("Production gain conversion"), FSpatialMath::DbToLinear(0), 1.f);
    TestEqual(TEXT("Production log gain"), FSpatialMath::LinearToDb(1), 0.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpatialMathCornerTest, "SpatialFabric.Math.Corner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)
bool FSpatialMathCornerTest::RunTest(const FString&)
{
    const FVector P = FSpatialMath::CartesianToPolar(FVector(1,1,1));
    TestTrue(TEXT("Actual Euclidean radius"), FMath::IsNearlyEqual(P.Z, FMath::Sqrt(3.0), 1.e-5));
    TestTrue(TEXT("ADM positive azimuth is left"), FMath::IsNearlyEqual(P.X, -45.0, 1.e-5));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADMOSCAddressTest, "SpatialFabric.Adapters.ADMOSCAddress", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)
bool FADMOSCAddressTest::RunTest(const FString&)
{
    FADMOSCAdapter Adapter;
    FSpatialAdapterConfig Config; Adapter.Configure(Config);
    FSpatialFrameSnapshot Snapshot;
    FSpatialNormalizedState State; State.ObjectID = 4; State.StageNormalized = FVector(1,0,0);
    Snapshot.Objects.Add(State);
    const auto Packets = Adapter.BuildMessages(Snapshot);
    TestTrue(TEXT("Production encoder emits xyz"), Packets.Num() > 0);
    if (Packets.Num()) { TestEqual(TEXT("Production address"), Packets[0].Address, FString(TEXT("/adm/obj/4/xyz"))); TestEqual(TEXT("Front maps to ADM Y"), Packets[0].Floats[1], 1.f); }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpatialMathListenerRelativeTest, "SpatialFabric.Math.ListenerRelative", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)
bool FSpatialMathListenerRelativeTest::RunTest(const FString&)
{
    const FVector P = FSpatialMath::CartesianToPolar(FVector(1,0,0));
    TestTrue(TEXT("Front has zero azimuth and elevation"), FMath::IsNearlyZero(P.X) && FMath::IsNearlyZero(P.Y));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpatialMathListenerRotatedTest, "SpatialFabric.Math.ListenerRelativeRotated", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)
bool FSpatialMathListenerRotatedTest::RunTest(const FString&)
{
    const FVector Right = FSpatialMath::CartesianToPolar(FVector(0,1,0));
    TestTrue(TEXT("Right is -90 degrees in production"), FMath::IsNearlyEqual(Right.X, -90.0));
    return true;
}
#endif
