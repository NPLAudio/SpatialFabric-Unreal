// Copyright (c) 2026 AudioLoom Contributors. MIT License.
// Engine-independent live input -> output bridge: bounded ring, drift-servoed linear resampler and the
// same channel mapping rules as the PCM renderer. Standard C++ only and exception-free so it builds under
// Unreal's no-exceptions rules. Mirrors SpatialFabric-Unity/Native/src/live.h in behavior.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

namespace SpatialFabricLive
{
struct FStats
{
    uint64_t Captured = 0, Rendered = 0, Underruns = 0, Overruns = 0;
    int FillFrames = 0;
    float Peak = 0;
    bool bPrimed = false;
};

class FBridge
{
public:
    /** InputChannel: 0 = all input channels, 1..N = that single input.
     *  OutputChannel: 0 = corresponding channels (a single selected input lands on output 1),
     *                 1..N = the first selected source channel to exactly one output. */
    static bool IsValidConfig(int InCh, int InRate, int OutCh, int OutRate, int InSel, int OutSel, int TargetMs)
    {
        return InCh >= 1 && InCh <= 64 && OutCh >= 1 && OutCh <= 64 && InRate >= 8000 && InRate <= 384000
            && OutRate >= 8000 && OutRate <= 384000 && InSel >= 0 && InSel <= InCh && OutSel >= 0 && OutSel <= OutCh
            && TargetMs >= 5 && TargetMs <= 1000;
    }

    FBridge(int InCh, int InRate, int OutCh, int OutRate, int InSel, int OutSel, int TargetMs)
        : IC(InCh), InRateV(InRate), OC(OutCh), OutRateV(OutRate)
    {
        bValid = IsValidConfig(InCh, InRate, OutCh, OutRate, InSel, OutSel, TargetMs);
        if (!bValid) { IC = OC = 1; InRateV = OutRateV = 48000; Target = 16; Capacity = 128; Ring.assign(size_t(Capacity), 0.f); Map.assign(1, -1); return; }
        Target = (std::max)(16, int(std::llround(double(InRateV) * TargetMs / 1000.0)));
        Capacity = Target * 8;
        Ring.assign(size_t(Capacity) * size_t(IC), 0.f);
        const int First = InSel ? InSel - 1 : 0;
        Map.assign(size_t(OC), -1);
        for (int O = 0; O < OC; ++O)
        {
            if (OutSel == 0) Map[size_t(O)] = InSel ? (O == 0 ? First : -1) : (O < IC ? O : -1);
            else Map[size_t(O)] = (O + 1 == OutSel) ? First : -1;
        }
    }

    bool IsValid() const { return bValid; }

    /** Appends interleaved input; overflow drops the OLDEST audio and is counted. False on bad arguments. */
    bool Push(const float* Data, int Frames)
    {
        if (!bValid || !Data || Frames < 0 || Frames > 1024 * 1024) return false;
        std::lock_guard<std::mutex> Guard(Lock);
        if (Frames > Capacity) { Data += size_t(Frames - Capacity) * size_t(IC); Frames = Capacity; ++Stats.Overruns; }
        const int Free = Capacity - Count;
        if (Frames > Free) { const int Drop = Frames - Free; Head = Advance(Head, Drop); Count -= Drop; ReadFraction = 0; ++Stats.Overruns; }
        for (int F = 0; F < Frames; ++F)
        {
            const size_t At = size_t(Advance(Head, Count + F)) * size_t(IC);
            for (int C = 0; C < IC; ++C)
            {
                float V = Data[size_t(F) * size_t(IC) + size_t(C)];
                V = std::isfinite(V) ? (std::min)(1.f, (std::max)(-1.f, V)) : 0.f;
                Ring[At + size_t(C)] = V;
                Stats.Peak = (std::max)(Stats.Peak, std::fabs(V));
            }
        }
        Count += Frames;
        Stats.Captured += uint64_t(Frames);
        return true;
    }

    /** Fills Frames interleaved output frames (silence where input is unavailable).
     *  Returns frames that were silence due to underrun after priming, or -1 on bad arguments. */
    int Pull(float* Out, int Frames, float Gain)
    {
        if (!bValid || !Out || Frames < 0 || Frames > 1024 * 1024 || !std::isfinite(Gain) || Gain < 0 || Gain > 4) return -1;
        std::lock_guard<std::mutex> Guard(Lock);
        std::fill(Out, Out + size_t(Frames) * size_t(OC), 0.f);
        if (!Stats.bPrimed) { if (Count < Target) return 0; Stats.bPrimed = true; }
        if (Count > Target * 4) { const int Drop = Count - Target; Head = Advance(Head, Drop); Count -= Drop; ReadFraction = 0; ++Stats.Overruns; }
        for (int F = 0; F < Frames; ++F)
        {
            if (Count < 2) { Stats.bPrimed = false; ++Stats.Underruns; return Frames - F; }
            // Proportional servo keeps the fill near target despite independent device clocks (+/-0.5%).
            const double Error = (double(Count) - Target) / Target;
            const double Ratio = double(InRateV) / OutRateV * (1.0 + (std::min)(0.005, (std::max)(-0.005, Error * 0.01)));
            const size_t A = size_t(Head) * size_t(IC), B = size_t(Advance(Head, 1)) * size_t(IC);
            const float T = float(ReadFraction);
            for (int O = 0; O < OC; ++O)
            {
                const int Src = Map[size_t(O)];
                if (Src < 0) continue;
                const float X = Ring[A + size_t(Src)], Y = Ring[B + size_t(Src)];
                Out[size_t(F) * size_t(OC) + size_t(O)] = (std::min)(1.f, (std::max)(-1.f, (X + (Y - X) * T) * Gain));
            }
            ReadFraction += Ratio;
            int Whole = int(ReadFraction);
            if (Whole > 0) { Whole = (std::min)(Whole, Count - 1); Head = Advance(Head, Whole); Count -= Whole; ReadFraction -= Whole; }
            ++Stats.Rendered;
        }
        return 0;
    }

    FStats Snapshot() { std::lock_guard<std::mutex> Guard(Lock); FStats S = Stats; S.FillFrames = Count; return S; }
    /** Peak since the previous call (0..1). */
    float TakePeak() { std::lock_guard<std::mutex> Guard(Lock); const float P = Stats.Peak; Stats.Peak = 0; return P; }
    int TargetFrames() const { return Target; }

private:
    int Advance(int I, int N) const { return (I + N) % Capacity; }
    bool bValid = false;
    int IC, InRateV, OC, OutRateV, Target = 16, Capacity = 128, Head = 0, Count = 0;
    double ReadFraction = 0;
    std::vector<int> Map;
    std::vector<float> Ring;
    FStats Stats;
    std::mutex Lock;
};
}
