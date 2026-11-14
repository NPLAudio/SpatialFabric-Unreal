// Copyright (c) 2026 SpatialFabric Contributors. MIT License.
#pragma once
#include "CoreMinimal.h"
#include "ISpatialProtocolAdapter.h"

/** ADM encoder shared by preview and transmission. XYZ and AED use one unit sphere. */
class SPATIALFABRIC_API FADMOSCAdapter : public ISpatialProtocolAdapter
{
public:
    virtual FName GetName() const override { return TEXT("ADMOSC"); }
    virtual ESpatialAdapterType GetAdapterType() const override { return ESpatialAdapterType::ADMOSC; }
    virtual void Configure(const FSpatialAdapterConfig& Config) override;
    virtual void SetClientComponent(USpatialOSCClientComponent* Client) override;
    virtual void ProcessFrame(const FSpatialFrameSnapshot& Snapshot, float DeltaTime) override;
    virtual void HandleIncomingOSC(const FString& Address, float Value) override;
    virtual bool IsEnabled() const override { return Config.bEnabled; }
    virtual TArray<FSpatialOSCMessage> BuildMessages(const FSpatialFrameSnapshot& Snapshot) const override;
    bool IsConnectionConfirmed() const { return bConnectionConfirmed; }
    double GetSecondsSinceLastReply() const { return SecondsSinceLastReply; }
private:
    FSpatialAdapterConfig Config;
    EADMCoordinateMode CoordMode = EADMCoordinateMode::Cartesian;
    USpatialOSCClientComponent* Client = nullptr;
    bool bConnectionConfirmed = false;
    double LastReplyTime = -1, SecondsSinceLastReply = -1;
    TArray<FSpatialOSCMessage> LastMessages;
};
