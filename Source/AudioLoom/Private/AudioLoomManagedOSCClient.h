// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#pragma once
#include "OSCClient.h"
#include "AudioLoomManagedOSCClient.generated.h"

/** UE exposes Stop as protected; owned clients need deterministic socket shutdown. */
UCLASS()
class UAudioLoomManagedOSCClient : public UOSCClient
{
    GENERATED_BODY()
public:
    void Shutdown() { Stop(); }
};
