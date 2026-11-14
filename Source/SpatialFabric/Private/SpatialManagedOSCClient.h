// Copyright (c) 2026 SpatialFabric Contributors. MIT License.
#pragma once
#include "OSCClient.h"
#include "SpatialManagedOSCClient.generated.h"
UCLASS()
class USpatialManagedOSCClient : public UOSCClient
{
    GENERATED_BODY()
public:
    void Shutdown() { Stop(); }
};
