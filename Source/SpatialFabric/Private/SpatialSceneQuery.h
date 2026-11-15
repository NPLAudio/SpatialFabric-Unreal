// Copyright (c) 2026 SpatialFabric Contributors. MIT License.
#pragma once
#include "CoreMinimal.h"
class UWorld;
struct FOSCMessage;
namespace SpatialSceneQuery
{
    constexpr int32 MaxPageSize = 64;
    constexpr int32 MaxJsonBytes = 96 * 1024;
    constexpr int32 ChunkBytes = 900;
    constexpr int32 MaxChunks = 128;
    FString BuildJson(UWorld* World, int32 Offset, int32 Limit);
    bool ParseRequest(const FOSCMessage& Message, FString& RequestID, int32& ReplyPort, int32& Offset, int32& Limit);
    TArray<FString> SplitUTF8(const FString& Json);
    void Register();
    void Unregister();
}
