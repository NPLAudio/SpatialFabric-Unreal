// Copyright (c) 2026 SpatialFabric Contributors. MIT License.
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SpatialSceneQueryLibrary.generated.h"

/** Read-only, paginated configuration inventory. Never loads assets, prepares PCM or starts playback. */
UCLASS()
class SPATIALFABRIC_API USpatialSceneQueryLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Query the supplied context's world, not all editor/PIE worlds. Offset >=0; limit 1..64. */
    UFUNCTION(BlueprintCallable, Category="SpatialFabric|Discovery", meta=(WorldContext="WorldContextObject"))
    static FString GetSceneJson(const UObject* WorldContextObject, int32 Offset = 0, int32 Limit = 64);
};
