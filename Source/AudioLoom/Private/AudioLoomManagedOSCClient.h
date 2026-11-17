// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#pragma once
#include "OSCClient.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "AudioLoomManagedOSCClient.generated.h"

/** UE exposes Stop as protected; owned clients need deterministic socket shutdown. */
UCLASS()
class UAudioLoomManagedOSCClient : public UOSCClient
{
    GENERATED_BODY()
public:
    /** Raw UTF-8 acknowledgement packets, to exactly the current /state destination. */
    void SendBytes(const TArray<uint8>& Bytes)
    {
        if (!IsActive()) return;
        ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        if (!Sockets) return;
        if (!AckSocket) AckSocket = Sockets->CreateSocket(NAME_DGram, TEXT("AudioLoomAck"), false);
        if (!AckSocket) return;
        FString IP; int32 Port = 0;
        GetSendIPAddress(IP, Port);
        TSharedRef<FInternetAddr> Target = Sockets->CreateInternetAddr();
        bool bValid = false; Target->SetIp(*IP, bValid); Target->SetPort(Port);
        int32 Sent = 0;
        if (bValid) AckSocket->SendTo(Bytes.GetData(), Bytes.Num(), Sent, *Target);
    }
    void Shutdown()
    {
        if (AckSocket)
        {
            AckSocket->Close();
            if (auto* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)) Sockets->DestroySocket(AckSocket);
            AckSocket = nullptr;
        }
        Stop();
    }
protected:
    virtual void BeginDestroy() override { Shutdown(); Super::BeginDestroy(); }
private:
    FSocket* AckSocket = nullptr;
};
