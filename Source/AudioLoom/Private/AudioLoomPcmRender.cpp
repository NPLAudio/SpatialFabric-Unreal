// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#include "AudioLoomPcmRender.h"
FAudioLoomPcmCursor::FAudioLoomPcmCursor(const TArray<float>& InPCM, int32 Channels, int32 SourceRate, double OutputRate)
    : PCM(InPCM), SourceChannels(FMath::Max(1, Channels)), TotalFrames(InPCM.Num() / FMath::Max(1, Channels)),
      Step(double(FMath::Max(1, SourceRate)) / FMath::Max(1.0, OutputRate)) {}
bool FAudioLoomPcmCursor::AtEnd() const { return TotalFrames <= 0 || Position >= TotalFrames; }
bool FAudioLoomPcmCursor::Render(float* Out, int32 Frames, int32 OutputChannels, int32 OutputChannel, bool bLoop)
{
    if (!Out || Frames < 0 || OutputChannels <= 0) return false;
    for (int32 F = 0; F < Frames; ++F)
    {
        if (bLoop && TotalFrames > 0) Position = FMath::Fmod(Position, double(TotalFrames));
        const bool bValid = !AtEnd();
        const int32 A = bValid ? FMath::FloorToInt(Position) : 0;
        const int32 B = bLoop && TotalFrames > 0 ? (A + 1) % TotalFrames : FMath::Min(A + 1, TotalFrames - 1);
        const float T = float(Position - A);
        for (int32 Ch = 0; Ch < OutputChannels; ++Ch)
        {
            const int32 Src = OutputChannel == 0 ? Ch : (Ch + 1 == OutputChannel ? 0 : SourceChannels);
            float V = 0;
            if (bValid && Src < SourceChannels) V = FMath::Lerp(PCM[A * SourceChannels + Src], PCM[B * SourceChannels + Src], T);
            Out[int64(F) * OutputChannels + Ch] = FMath::IsFinite(V) ? FMath::Clamp(V, -1.f, 1.f) : 0.f;
        }
        if (bValid) Position += Step;
    }
    return !bLoop && AtEnd();
}
bool AudioLoomEncodePCM(const float* Source, int32 Samples, uint8* Dest, bool bFloat, int32 Bits, int32 ValidBits)
{
    if (!Source || !Dest || Samples < 0 || (bFloat ? Bits != 32 : (Bits != 16 && Bits != 24 && Bits != 32))
        || ValidBits < 1 || ValidBits > Bits) return false;
    for (int32 i = 0; i < Samples; ++i)
    {
        const float V = FMath::IsFinite(Source[i]) ? FMath::Clamp(Source[i], -1.f, 1.f) : 0;
        uint32 Word;
        if (bFloat) FMemory::Memcpy(&Word, &V, 4);
        else
        {
            const int64 Scale = int64(1) << (ValidBits - 1);
            const int64 Q = FMath::Clamp<int64>(int64(FMath::RoundToDouble(double(V) * (Scale - 1))), -Scale, Scale - 1);
            Word = uint32(Q) << (Bits - ValidBits);
        }
        for (int32 B = 0; B < Bits / 8; ++B) *Dest++ = uint8(Word >> (8 * B));
    }
    return true;
}
