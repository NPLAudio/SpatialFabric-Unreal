// Copyright (c) 2026 SpatialFabric Contributors. MIT License.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "SpatialOSCClientComponent.h"
#include "SpatialOSCServerComponent.h"
#include "SpatialManagedOSCClient.h"
#include "UObject/UObjectIterator.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "HAL/PlatformProcess.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFNetworkLifecycle, "SpatialFabric.OSC.LoopbackAndClientReuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFNetworkLifecycle::RunTest(const FString&)
{
    ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!TestTrue(TEXT("Socket subsystem available"), Sockets != nullptr)) return false;
    FSocket* Receiver = Sockets->CreateSocket(NAME_DGram, TEXT("SFLoopbackRegression"), false);
    if (!TestTrue(TEXT("UDP receiver created"), Receiver != nullptr)) return false;
    auto Address = Sockets->CreateInternetAddr(); bool Valid = false;
    Address->SetIp(TEXT("127.0.0.1"), Valid); Address->SetPort(0);
    if (!Receiver->Bind(*Address)) { Sockets->DestroySocket(Receiver); AddError(TEXT("Cannot bind loopback test socket")); return false; }
    Receiver->SetNonBlocking(true); Receiver->GetAddress(*Address);
    auto* Client = NewObject<USpatialOSCClientComponent>();
    Client->Connect(TEXT("127.0.0.1"), Address->GetPort());
    TestTrue(TEXT("Client is active"), Client->IsConnected());
    auto CountClients = [] { int32 N = 0; for (TObjectIterator<USpatialManagedOSCClient> It; It; ++It) ++N; return N; };
    const int32 Before = CountClients();
    for (int32 I = 0; I < 20; ++I) Client->Connect(TEXT("127.0.0.1"), Address->GetPort());
    TestEqual(TEXT("Repeated frame retargeting does not allocate more client UObjects"), CountClients(), Before);
    Client->SendFloat(TEXT("/spatialfabric/test"), 0.5f);
    uint32 Pending = 0;
    const double Until = FPlatformTime::Seconds() + 1.0;
    while (!Receiver->HasPendingData(Pending) && FPlatformTime::Seconds() < Until) FPlatformProcess::Sleep(0.001f);
    TestTrue(TEXT("Actual OSC packet arrives over loopback UDP"), Pending > 0);
    Client->Disconnect();
    TestFalse(TEXT("Disconnect closes active socket"), Client->IsConnected());
    Sockets->DestroySocket(Receiver);
    auto* Server = NewObject<USpatialOSCServerComponent>();
    Server->StartListening(TEXT("127.0.0.1"), Address->GetPort());
    TestTrue(TEXT("Listening reflects an active server"), Server->IsListening());
    Server->StopListening();
    TestFalse(TEXT("Stopped server is not listening"), Server->IsListening());
    return true;
}
#endif
