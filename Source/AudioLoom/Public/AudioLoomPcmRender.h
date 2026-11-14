// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#pragma once
#include "CoreMinimal.h"

/** Hardware-independent fractional-frame renderer. The PCM owner must outlive this cursor. */
class AUDIOLOOM_API FAudioLoomPcmCursor
{
public:
    FAudioLoomPcmCursor(const TArray<float>& InPCM, int32 Channels, int32 SourceRate, double OutputRate);
    bool Render(float* Out, int32 Frames, int32 OutputChannels, int32 OutputChannel, bool bLoop);
    bool AtEnd() const;
private:
    const TArray<float>& PCM;
    int32 SourceChannels;
    int32 TotalFrames;
    double Position = 0;
    double Step;
};

/** Exact packed little-endian PCM output; false means unsupported format (writes nothing). */
AUDIOLOOM_API bool AudioLoomEncodePCM(const float* Source, int32 Samples, uint8* Dest, bool bFloat, int32 Bits, int32 ValidBits);
