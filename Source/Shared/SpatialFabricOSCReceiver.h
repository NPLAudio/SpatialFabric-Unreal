// Copyright (c) 2026 SpatialFabric Contributors. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Containers/StringConv.h"
#include "Containers/Ticker.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "IPAddress.h"
#include "OSCAddress.h"
#include "OSCManager.h"
#include "OSCMessage.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

/** Exact-bind, game-thread OSC receiver shared by the otherwise independent runtime modules.
 * UE 5.8's UOSCServer silently replaces loopback with a LAN address. Owning the socket avoids
 * that rewrite and reports real bind failures. No worker callbacks or UObject lifetime races.
 * IPv4 unicast only. Accepts OSC 1.0 i/f/s/b and h/T/F extensions; immediate bundles are bounded and atomic.
 * Unsupported tags, invalid padding, non-finite floats and malformed datagrams fail closed.
 */
class FSpatialFabricOSCReceiver : public TSharedFromThis<FSpatialFabricOSCReceiver>
{
public:
    ~FSpatialFabricOSCReceiver() { Stop(); }
    FSpatialFabricOSCReceiver() = default;
    FSpatialFabricOSCReceiver(const FSpatialFabricOSCReceiver&) = delete;
    FSpatialFabricOSCReceiver& operator=(const FSpatialFabricOSCReceiver&) = delete;

    bool Start(const FString& IP, int32 Port)
    {
        Stop();
        FIPv4Address Parsed;
        if (Port < 0 || Port > 65535 || !FIPv4Address::Parse(IP, Parsed) || Parsed.IsMulticastAddress()) return false;
        auto* System = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        if (!System) return false;
        auto Address = System->CreateInternetAddr();
        Address->SetIp(Parsed.Value); Address->SetPort(Port);
        Socket = System->CreateSocket(NAME_DGram, TEXT("SpatialFabricExactOSC"), false);
        if (!Socket) return false;
        if (!Socket->SetNonBlocking(true) || !Socket->SetReuseAddr(false) || !Socket->Bind(*Address))
        { Stop(); return false; }
        Socket->GetAddress(*Address);
        BoundIP = Address->ToString(false);
        BoundPort = Address->GetPort();
        // Verify the actual endpoint rather than trusting a requested/configured string.
        if (BoundIP != Parsed.ToString() || BoundPort <= 0) { Stop(); return false; }
        return true;
    }

    /** Core ticker preserves OSC control while a world is paused or its viewport is not realtime.
     * Weak ticker ownership plus a pinned receiver/copy of the callback make Stop during dispatch safe.
     * Optional: stack-owned test receivers can instead invoke Pump directly.
     */
    void SetDispatcher(TFunction<void(const FOSCMessage&, const FString&, uint16)> InDispatch)
    {
        FTSTicker::RemoveTicker(TickHandle); TickHandle.Reset();
        Dispatcher = MoveTemp(InDispatch);
        if (!Dispatcher || !IsListening()) return;
        const TWeakPtr<FSpatialFabricOSCReceiver> WeakSelf = AsShared();
        TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakSelf](float)
        {
            const auto Self = WeakSelf.Pin();
            if (!Self || !Self->IsListening()) return false;
            auto Dispatch = Self->Dispatcher;
            if (Dispatch) Self->Pump(Dispatch);
            return Self->IsListening();
        }));
    }

    void Stop()
    {
        FTSTicker::RemoveTicker(TickHandle); TickHandle.Reset();
        Dispatcher = nullptr;
        if (Socket)
        {
            Socket->Close();
            if (auto* System = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)) System->DestroySocket(Socket);
            Socket = nullptr;
        }
        BoundIP.Empty(); BoundPort = 0;
    }
    bool IsListening() const { return Socket != nullptr; }
    const FString& GetIP() const { return BoundIP; }
    int32 GetPort() const { return BoundPort; }

    void Pump(TFunctionRef<void(const FOSCMessage&, const FString&, uint16)> Dispatch)
    {
        auto* System = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        if (!Socket || !System) return;
        int32 Budget = 256;
        for (int32 PacketIndex = 0; Socket && PacketIndex < 64 && Budget > 0; ++PacketIndex)
        {
            uint32 Pending = 0;
            if (!Socket->HasPendingData(Pending)) break;
            TArray<uint8> Data;
            Data.SetNumUninitialized(FMath::Clamp<int32>(static_cast<int32>(FMath::Min(Pending, 65507u)), 1, 65507));
            auto Sender = System->CreateInternetAddr();
            int32 Read = 0;
            if (!Socket->RecvFrom(Data.GetData(), Data.Num(), Read, *Sender)) break;
            if (Read <= 0 || static_cast<uint32>(Read) != Pending) continue;
            Data.SetNum(Read, EAllowShrinking::No);
            TArray<FOSCMessage> Messages;
            if (!DecodeDatagram(Data, Messages)) continue;
            const FString SenderIP = Sender->ToString(false);
            const uint16 SenderPort = static_cast<uint16>(Sender->GetPort());
            for (const auto& Message : Messages)
            {
                if (!Socket || Budget-- <= 0) break;
                Dispatch(Message, SenderIP, SenderPort);
            }
        }
    }

    static bool DecodeDatagram(const TArray<uint8>& Data, TArray<FOSCMessage>& Messages)
    {
        Messages.Reset();
        if (Data.Num() > 65507 || !Parse(Data.GetData(), Data.Num(), Messages, 0))
        { Messages.Reset(); return false; }
        return true;
    }

private:
    FSocket* Socket = nullptr;
    FTSTicker::FDelegateHandle TickHandle;
    TFunction<void(const FOSCMessage&, const FString&, uint16)> Dispatcher;
    FString BoundIP;
    int32 BoundPort = 0;

    static bool Word(const uint8* Data, int32 Size, int32& Offset, uint32& Value)
    {
        if (Offset > Size - 4) return false;
        Value = (uint32(Data[Offset]) << 24) | (uint32(Data[Offset + 1]) << 16)
            | (uint32(Data[Offset + 2]) << 8) | uint32(Data[Offset + 3]);
        Offset += 4; return true;
    }
    static bool String(const uint8* Data, int32 Size, int32& Offset, FString& Value)
    {
        const int32 Start = Offset;
        while (Offset < Size && Data[Offset]) ++Offset;
        if (Offset >= Size) return false;
        const int32 End = Offset;
        const int32 Next = Align(Offset + 1, 4);
        if (Next > Size) return false;
        while (Offset < Next) if (Data[Offset++] != 0) return false;
        FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Data + Start), End - Start);
        Value = FString(Converted.Length(), Converted.Get());
        return true;
    }
    static bool Parse(const uint8* Data, int32 Size, TArray<FOSCMessage>& Messages, int32 Depth)
    {
        if (Size < 8 || Size % 4 || Depth > 8 || Messages.Num() >= 128) return false;
        if (FMemory::Memcmp(Data, "#bundle\0", 8) == 0)
        {
            if (Size < 16) return false;
            int32 Offset = 16; // Same immediate-dispatch timetag policy as Unreal's OSC server.
            while (Offset < Size)
            {
                uint32 Length = 0;
                if (!Word(Data, Size, Offset, Length) || Length == 0 || Length > uint32(Size - Offset)) return false;
                if (!Parse(Data + Offset, static_cast<int32>(Length), Messages, Depth + 1)) return false;
                Offset += static_cast<int32>(Length);
            }
            return true;
        }
        int32 Offset = 0;
        FString Address, Tags;
        if (!String(Data, Size, Offset, Address) || !String(Data, Size, Offset, Tags)
            || !Tags.StartsWith(TEXT(",")) || Tags.Len() > 129) return false;
        FOSCMessage Message;
        if (!Message.SetAddress(FOSCAddress(Address))) return false;
        for (int32 Index = 1; Index < Tags.Len(); ++Index)
        {
            uint32 Bits = 0;
            switch (Tags[Index])
            {
            case TEXT('i'):
                if (!Word(Data, Size, Offset, Bits)) return false;
                UOSCManager::AddInt32(Message, static_cast<int32>(Bits)); break;
            case TEXT('f'):
            {
                if (!Word(Data, Size, Offset, Bits)) return false;
                float Value = 0; FMemory::Memcpy(&Value, &Bits, sizeof(Value));
                if (!FMath::IsFinite(Value)) return false;
                UOSCManager::AddFloat(Message, Value); break;
            }
            case TEXT('h'):
            {
                uint32 Low = 0;
                if (!Word(Data, Size, Offset, Bits) || !Word(Data, Size, Offset, Low)) return false;
                UOSCManager::AddInt64(Message, static_cast<int64>((uint64(Bits) << 32) | Low)); break;
            }
            case TEXT('s'):
            {
                FString Value;
                if (!String(Data, Size, Offset, Value)) return false;
                UOSCManager::AddString(Message, Value); break;
            }
            case TEXT('b'):
            {
                if (!Word(Data, Size, Offset, Bits) || Bits > uint32(Size - Offset)) return false;
                const int32 End = Offset + static_cast<int32>(Bits), Next = Align(End, 4);
                if (Next > Size) return false;
                TArray<uint8> Blob; Blob.Append(Data + Offset, static_cast<int32>(Bits));
                Offset = End;
                while (Offset < Next) if (Data[Offset++] != 0) return false;
                UOSCManager::AddBlob(Message, Blob); break;
            }
            case TEXT('T'): UOSCManager::AddBool(Message, true); break;
            case TEXT('F'): UOSCManager::AddBool(Message, false); break;
            default: return false;
            }
        }
        if (Offset != Size) return false;
        Messages.Add(MoveTemp(Message));
        return true;
    }
};
