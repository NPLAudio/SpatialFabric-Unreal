// Copyright (c) 2026 SpatialFabric Contributors. MIT License.
#pragma once
#include "CoreMinimal.h"
#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"
class UWorld;
struct FOSCMessage;

// Core-only bridge: AudioLoom keeps its legacy identity and has no dependency on SpatialFabric.
class ISpatialFabricSceneQuery : public IModularFeature
{
public:
    static FName FeatureName() { return FName(TEXT("SpatialFabric.SceneQuery.v1")); }
    virtual bool HandleQuery(const FOSCMessage&, UWorld*, const FString&, uint16) = 0;
    static bool Dispatch(const FOSCMessage& Message, UWorld* World, const FString& IP, uint16 Port)
    {
        for (auto* Service : IModularFeatures::Get().GetModularFeatureImplementations<ISpatialFabricSceneQuery>(FeatureName()))
            if (Service->HandleQuery(Message, World, IP, Port)) return true;
        return false;
    }
};
