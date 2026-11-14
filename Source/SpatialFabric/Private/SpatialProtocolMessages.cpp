// Copyright (c) 2026 SpatialFabric Contributors. MIT License.
#include "ISpatialProtocolAdapter.h"
#include "SpatialOSCClientComponent.h"
#include "OSCManager.h"
#include "OSCAddress.h"

bool FSpatialOSCMessage::IsValid() const
{
    if (!UOSCManager::OSCAddressIsValidPath(UOSCManager::ConvertStringToOSCAddress(Address))) return false;
    for (float Value : Floats) if (!FMath::IsFinite(Value)) return false;
    return true;
}


FString FSpatialOSCMessage::ToString() const
{
    FString Result = Address;
    for (float V : Floats) Result += FString::Printf(TEXT(" %.6g"), V);
    for (int32 V : Ints) Result += FString::Printf(TEXT(" %d"), V);
    for (const FString& V : Strings) Result += TEXT(" \"") + V + TEXT("\"");
    return Result;
}

void ISpatialProtocolAdapter::Transmit(USpatialOSCClientComponent* Client, const TArray<FSpatialOSCMessage>& Messages)
{
    if (!Client || !Client->IsConnected()) return;
    for (const auto& Message : Messages)
    {
        if (!Message.IsValid()) continue;
        Client->SendMessage(Message.Address, Message.Floats, Message.Ints, Message.Strings);
        if (OnLog)
        {
            FSpatialFabricLogEntry Entry;
            Entry.Adapter = GetName().ToString();
            Entry.Direction = TEXT("OUT");
            Entry.Address = Message.Address;
            Entry.ValueStr = Message.ToString().Mid(Message.Address.Len()).TrimStart();
            Entry.Timestamp = FDateTime::Now().ToString(TEXT("%H:%M:%S"));
            OnLog(Entry);
        }
    }
}
