// Copyright (c) 2026 SpatialFabric Contributors. MIT License.
#include "Adapters/ADMOSCAdapter.h"
#include "SpatialOSCClientComponent.h"
#include "SpatialMath.h"

void FADMOSCAdapter::Configure(const FSpatialAdapterConfig& InConfig)
{
    Config = InConfig;
    CoordMode = Config.ADMCoordinateMode;
    CachedSendRateHz = Config.SendRateHz;
    TimeSinceLastSend = 0.f;
    LastMessages.Reset();
    bConnectionConfirmed = false; LastReplyTime = -1; SecondsSinceLastReply = -1;
}
void FADMOSCAdapter::SetClientComponent(USpatialOSCClientComponent* InClient) { Client = InClient; }
void FADMOSCAdapter::HandleIncomingOSC(const FString& Address, float)
{
    if (Address.StartsWith(TEXT("/adm/"))) { bConnectionConfirmed = true; LastReplyTime = FPlatformTime::Seconds(); }
}
TArray<FSpatialOSCMessage> FADMOSCAdapter::BuildMessages(const FSpatialFrameSnapshot& Snapshot) const
{
    TArray<FSpatialOSCMessage> Result;
    for (const auto& State : Snapshot.Objects)
    {
        if (State.ObjectID < 1 || State.StageNormalized.ContainsNaN()) continue;
        // ADM distance is a unit-sphere radius. Both encodings use the SAME point.
        const FVector P = State.StageNormalized.GetClampedToMaxSize(1.0);
        const FString Base = FString::Printf(TEXT("/adm/obj/%d/"), State.ObjectID);
        if (CoordMode != EADMCoordinateMode::Polar)
            Result.Add({Base + TEXT("xyz"), {(float)P.Y, (float)P.X, (float)P.Z}, {}, {}});
        if (CoordMode != EADMCoordinateMode::Cartesian)
        {
            const FVector Polar = FSpatialMath::CartesianToPolar(P);
            Result.Add({Base + TEXT("aed"), {(float)Polar.X, (float)Polar.Y, (float)Polar.Z}, {}, {}});
        }
        Result.Add({Base + TEXT("w"), {FMath::Clamp(State.Width01, 0.f, 1.f)}, {}, {}});
        if (State.bADMSendGain) Result.Add({Base + TEXT("gain"), {FMath::Max(0.f, State.GainLinear)}, {}, {}});
        if (State.bADMSendMute) Result.Add({Base + TEXT("mute"), {}, {State.bMuted ? 1 : 0}, {}});
        if (State.bADMSendName) Result.Add({Base + TEXT("name"), {}, {}, {State.ADMName.IsEmpty() ? State.Label : State.ADMName}});
        Result.Add({Base + TEXT("dref"), {FMath::Clamp(State.Dref, 0.f, 1.f)}, {}, {}});
        if (State.Dmax > 0.f) Result.Add({Base + TEXT("dmax"), {State.Dmax}, {}, {}});
    }
    if (Snapshot.bHasListener)
    {
        const auto& P = Snapshot.ListenerNormalized;
        const auto& R = Snapshot.ListenerYPR;
        Result.Add({TEXT("/adm/lis/xyz"), {(float)P.Y, (float)P.X, (float)P.Z}, {}, {}});
        Result.Add({TEXT("/adm/lis/ypr"), {-(float)R.Yaw, (float)R.Pitch, -(float)R.Roll}, {}, {}});
    }
    return Result;
}
void FADMOSCAdapter::ProcessFrame(const FSpatialFrameSnapshot& Snapshot, float DeltaTime)
{
    if (LastReplyTime >= 0) SecondsSinceLastReply = FPlatformTime::Seconds() - LastReplyTime;
    if (!Config.bEnabled || !Client || !ShouldSendThisFrame(DeltaTime)) return;
    Client->Connect(Config.TargetIP, Config.TargetPort);
    if (!Client->IsConnected()) { LastMessages.Reset(); return; }
    const auto Messages = BuildMessages(Snapshot);
    if (Config.bSendOnlyOnChange && Messages == LastMessages) return;
    Transmit(Client, Messages);
    LastMessages = Messages;
}
