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
#include "SpatialFabricOSCReceiver.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFExactOSCBind, "SpatialFabric.OSC.ExactBindAndReceive", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFExactOSCBind::RunTest(const FString&)
{
    FSpatialFabricOSCReceiver Receiver;
    if (!TestTrue(TEXT("Bind exact loopback on a kernel-assigned port"), Receiver.Start(TEXT("127.0.0.1"), 0))) return false;
    TestEqual(TEXT("Actual socket address stays loopback, not LAN"), Receiver.GetIP(), FString(TEXT("127.0.0.1")));
    FSpatialFabricOSCReceiver Collision;
    TestFalse(TEXT("Occupied endpoint fails rather than reporting an active proxy"), Collision.Start(Receiver.GetIP(), Receiver.GetPort()));
    auto* System = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    FSocket* Sender = System->CreateSocket(NAME_DGram, TEXT("SFExactBindSender"), false);
    if (!TestNotNull(TEXT("Sender created"), Sender)) return false;
    auto Destination = System->CreateInternetAddr(); bool Valid = false;
    Destination->SetIp(TEXT("127.0.0.1"), Valid); Destination->SetPort(Receiver.GetPort());
    const uint8 Wire[] = {'/', 's', 'f', 0, ',', 'f', 0, 0, 0x3f, 0x80, 0, 0};
    int32 Sent = 0;
    TestTrue(TEXT("Send actual local OSC datagram"), Sender->SendTo(Wire, UE_ARRAY_COUNT(Wire), Sent, *Destination));
    int32 Received = 0;
    const double Until = FPlatformTime::Seconds() + 1.0;
    while (!Received && FPlatformTime::Seconds() < Until)
    {
        Receiver.Pump([&](const FOSCMessage& Message, const FString& IP, uint16)
        {
            ++Received;
            TestEqual(TEXT("Received address"), Message.GetAddress().GetFullPath(), FString(TEXT("/sf")));
            TArray<float> Values; UOSCManager::GetAllFloats(Message, Values);
            TestTrue(TEXT("Received float 1"), Values.Num() == 1 && Values[0] == 1.f);
            TestEqual(TEXT("Sender is loopback"), IP, FString(TEXT("127.0.0.1")));
        });
        if (!Received) FPlatformProcess::Sleep(0.001f);
    }
    TestEqual(TEXT("An inbound datagram actually arrives"), Received, 1);
    System->DestroySocket(Sender);
    Receiver.Stop();
    TestFalse(TEXT("Stop releases socket"), Receiver.IsListening());
    TestFalse(TEXT("Invalid IP fails closed"), Receiver.Start(TEXT("not-an-ip"), 9000));
    TestFalse(TEXT("Unsupported multicast cannot report a working unicast listener"), Receiver.Start(TEXT("239.1.2.3"), 9000));
    return true;
}

class FSFOSCWait : public IAutomationLatentCommand
{
    TFunction<bool()> Poll;
public:
    explicit FSFOSCWait(TFunction<bool()> InPoll) : Poll(MoveTemp(InPoll)) {}
    virtual bool Update() override { return Poll(); }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFOSCCoreTicker, "SpatialFabric.OSC.CoreTickerLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFOSCCoreTicker::RunTest(const FString&)
{
    struct FState
    {
        TSharedPtr<FSpatialFabricOSCReceiver> Receiver;
        FSocket* Sender = nullptr;
        int32 Received = 0;
        double Deadline = 0;
    };
    auto State = MakeShared<FState>();
    State->Receiver = MakeShared<FSpatialFabricOSCReceiver>();
    if (!TestTrue(TEXT("Receiver starts without a ticking world"), State->Receiver->Start(TEXT("127.0.0.1"), 0))) return false;
    const TWeakPtr<FState> WeakState = State;
    State->Receiver->SetDispatcher([WeakState](const FOSCMessage&, const FString&, uint16)
    {
        if (const auto Live = WeakState.Pin())
        {
            ++Live->Received;
            Live->Receiver->Stop();
            Live->Receiver.Reset(); // Must be safe while its copied callback/core ticker is executing.
        }
    });
    auto* System = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    State->Sender = System->CreateSocket(NAME_DGram, TEXT("SFCoreTickerSender"), false);
    if (!TestNotNull(TEXT("Sender created"), State->Sender)) { State->Receiver->Stop(); return false; }
    auto Destination = System->CreateInternetAddr(); bool Valid = false;
    Destination->SetIp(TEXT("127.0.0.1"), Valid); Destination->SetPort(State->Receiver->GetPort());
    const uint8 Wire[] = {'/', 's', 'f', 0, ',', 'f', 0, 0, 0x3f, 0x80, 0, 0};
    int32 Sent = 0;
    TestTrue(TEXT("Send datagram for core-ticker dispatch"), State->Sender->SendTo(Wire, UE_ARRAY_COUNT(Wire), Sent, *Destination));
    State->Deadline = FPlatformTime::Seconds() + 2.0;
    ADD_LATENT_AUTOMATION_COMMAND(FSFOSCWait([this, State]()
    {
        if (!State->Received && FPlatformTime::Seconds() < State->Deadline) return false;
        TestEqual(TEXT("Core ticker dispatches without component/world ticking"), State->Received, 1);
        TestFalse(TEXT("Callback can release the receiver owner"), State->Receiver.IsValid());
        if (State->Receiver) State->Receiver->Stop();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(State->Sender);
        return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFBoundedOSCDecode, "SpatialFabric.OSC.BoundedDatagramDecoder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFBoundedOSCDecode::RunTest(const FString&)
{
    TArray<uint8> Wire = {'/', 's', 'f', 0, ',', 'f', 0, 0, 0x3f, 0x80, 0, 0};
    TArray<FOSCMessage> Messages;
    TestTrue(TEXT("Valid OSC 1.0 float"), FSpatialFabricOSCReceiver::DecodeDatagram(Wire, Messages));
    for (int32 Length = 0; Length < Wire.Num(); ++Length)
    {
        TArray<uint8> Short; Short.Append(Wire.GetData(), Length);
        TestFalse(TEXT("Every truncated prefix is rejected"), FSpatialFabricOSCReceiver::DecodeDatagram(Short, Messages));
        TestEqual(TEXT("Malformed input never partially dispatches"), Messages.Num(), 0);
    }
    auto Bad = Wire; Bad[7] = 1;
    TestFalse(TEXT("Nonzero string padding rejected"), FSpatialFabricOSCReceiver::DecodeDatagram(Bad, Messages));
    Bad = Wire; Bad[5] = 'd';
    TestFalse(TEXT("Unsupported tag rejected, not mistaken for address-only play"), FSpatialFabricOSCReceiver::DecodeDatagram(Bad, Messages));
    Bad = Wire; Bad[8] = 0x7f;
    TestFalse(TEXT("Infinity rejected"), FSpatialFabricOSCReceiver::DecodeDatagram(Bad, Messages));
    TArray<uint8> Bundle = {'#','b','u','n','d','l','e',0, 0,0,0,0,0,0,0,1, 0,0,0,12};
    Bundle.Append(Wire);
    TestTrue(TEXT("Immediate bundle supported"), FSpatialFabricOSCReceiver::DecodeDatagram(Bundle, Messages));
    TestEqual(TEXT("One bundled message"), Messages.Num(), 1);
    Bundle.Append({0,0,0,12}); Bundle.Append(Bad);
    TestFalse(TEXT("Bad second bundle element rejects the entire datagram"), FSpatialFabricOSCReceiver::DecodeDatagram(Bundle, Messages));
    TestEqual(TEXT("No partial bundle execution"), Messages.Num(), 0);
    TArray<uint8> Nested = Wire;
    for (int32 Depth = 0; Depth < 9; ++Depth)
    {
        const uint32 Length = Nested.Num();
        TArray<uint8> Outer = {'#','b','u','n','d','l','e',0, 0,0,0,0,0,0,0,1};
        Outer.Append({uint8(Length >> 24), uint8(Length >> 16), uint8(Length >> 8), uint8(Length)});
        Outer.Append(Nested); Nested = MoveTemp(Outer);
    }
    TestFalse(TEXT("Deep nesting is bounded"), FSpatialFabricOSCReceiver::DecodeDatagram(Nested, Messages));
    TArray<uint8> BadBlob = {'/', 's', 'f', 0, ',', 'b', 0, 0, 255,255,255,255};
    TestFalse(TEXT("Oversized blob cannot allocate from a forged length"), FSpatialFabricOSCReceiver::DecodeDatagram(BadBlob, Messages));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSFOSCUnregister, "SpatialFabric.OSC.ComponentUnregisterClosesSocket", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSFOSCUnregister::RunTest(const FString&)
{
    FSpatialFabricOSCReceiver Probe;
    if (!Probe.Start(TEXT("127.0.0.1"), 0)) return false;
    const int32 Port = Probe.GetPort(); Probe.Stop();
    UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
    AActor* Owner = World->SpawnActor<AActor>();
    auto* Server = NewObject<USpatialOSCServerComponent>(Owner);
    Owner->AddInstanceComponent(Server); Server->RegisterComponent();
    Server->StartListening(TEXT("127.0.0.1"), Port);
    TestTrue(TEXT("Registered component listens"), Server->IsListening());
    Server->UnregisterComponent();
    TestFalse(TEXT("Unregister closes immediately, before GC"), Server->IsListening());
    TestTrue(TEXT("A new receiver can immediately reclaim the port"), Probe.Start(TEXT("127.0.0.1"), Port));
    World->DestroyWorld(false);
    return true;
}
#endif
