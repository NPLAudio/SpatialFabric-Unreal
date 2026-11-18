// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#include "AudioLoomPcmLoader.h"
#include "Sound/SoundWave.h"
#include "AudioDecompress.h"
#include "Interfaces/IAudioFormat.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "AudioLoomOscSettings.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Misc/Paths.h"

namespace
{
int64 MaxFileBytes() { return FMath::Min<int64>(FAudioLoomPcmLoader::GetMaxDecodedBytes(), MAX_int32 - 2); }
FAudioLoomPcmResult SidecarFor(USoundWave* Wave)
{
    if (!GetDefault<UAudioLoomOscSettings>()->bAllowLegacyWavSidecar) return {};
    FString Filename;
    if (!FPackageName::DoesPackageExist(Wave->GetOutermost()->GetName(), &Filename)) return {};
    return FAudioLoomPcmLoader::LoadLegacySidecar(Filename);
}
uint16 U16(const uint8* P) { return uint16(P[0]) | uint16(P[1]) << 8; }
uint32 U32(const uint8* P) { return uint32(P[0]) | uint32(P[1]) << 8 | uint32(P[2]) << 16 | uint32(P[3]) << 24; }
bool ValidShape(int32 Rate, int32 Channels) { return Rate >= 8000 && Rate <= 384000 && Channels > 0 && Channels <= 64; }
FAudioLoomPcmResult FromInt16(const uint8* Data, int64 Bytes, int32 Rate, int32 Channels)
{
    FAudioLoomPcmResult R;
    if (!Data || !ValidShape(Rate, Channels) || Bytes <= 0 || Bytes % (2 * Channels)
        || !FAudioLoomPcmLoader::IsDecodedSizeAllowed(Bytes / 2)) return R;
    R.NumChannels = Channels; R.SampleRate = Rate;
    R.PCM.SetNumUninitialized(int32(Bytes / 2));
    for (int32 i = 0; i < R.PCM.Num(); ++i) R.PCM[i] = int16(U16(Data + i * 2)) / 32768.f;
    R.bSuccess = true;
    return R;
}
}

int64 FAudioLoomPcmLoader::GetMaxDecodedBytes()
{
    return int64(FMath::Clamp(GetDefault<UAudioLoomOscSettings>()->MaxDecodedAudioMiB, 1, 8191)) * 1024 * 1024;
}

bool FAudioLoomPcmLoader::IsDecodedSizeAllowed(int64 SampleCount)
{
    return SampleCount > 0 && SampleCount <= MAX_int32 && SampleCount <= GetMaxDecodedBytes() / int64(sizeof(float));
}

FAudioLoomPcmResult FAudioLoomPcmLoader::LoadLegacySidecar(const FString& PackageFilename)
{
    if (!GetDefault<UAudioLoomOscSettings>()->bAllowLegacyWavSidecar || PackageFilename.IsEmpty()) return {};
    const FString Base = FPaths::GetPath(PackageFilename) / FPaths::GetBaseFilename(PackageFilename);
    for (const TCHAR* Extension : { TEXT(".wav"), TEXT(".WAV") })
    {
        FAudioLoomPcmResult Result = LoadFromFile(Base + Extension);
        if (Result.bSuccess) return Result;
    }
    return {};
}

FAudioLoomPcmResult FAudioLoomPcmLoader::LoadFromMemory(const TArray<uint8>& Bytes)
{
    FAudioLoomPcmResult R;
    const uint8* D = Bytes.GetData();
    const int64 Size = Bytes.Num();
    if (Size < 12 || Size > MaxFileBytes() || FMemory::Memcmp(D, "RIFF", 4) || FMemory::Memcmp(D + 8, "WAVE", 4)) return R;
    const int64 End = int64(U32(D + 4)) + 8;
    if (End < 12 || End > Size) return R;
    int32 Format = 0, Channels = 0, Rate = 0, Bits = 0, Align = 0;
    uint32 ByteRate = 0;
    const uint8* PCM = nullptr;
    int64 PCMBytes = 0;
    bool HasFmt = false;
    for (int64 Offset = 12; Offset < End;)
    {
        if (End - Offset < 8) return R;
        const uint8* Header = D + Offset;
        const int64 Count = U32(Header + 4);
        Offset += 8;
        if (Count > End - Offset) return R;
        const uint8* Chunk = D + Offset;
        if (!FMemory::Memcmp(Header, "fmt ", 4))
        {
            if (HasFmt || Count < 16) return R;
            HasFmt = true;
            Format = U16(Chunk); Channels = U16(Chunk + 2); Rate = U32(Chunk + 4);
            ByteRate = U32(Chunk + 8); Align = U16(Chunk + 12); Bits = U16(Chunk + 14);
            if (Format == 0xfffe)
            {
                static const uint8 GuidTail[12] = {0,0,16,0,128,0,0,170,0,56,155,113};
                if (Count < 40 || U16(Chunk + 16) < 22 || FMemory::Memcmp(Chunk + 28, GuidTail, 12)) return R;
                Format = U32(Chunk + 24);
                if (U16(Chunk + 18) != Bits) return R; // reject packed-valid-bit variants rather than misdecode
            }
        }
        else if (!FMemory::Memcmp(Header, "data", 4))
        {
            if (PCM) return R;
            PCM = Chunk; PCMBytes = Count;
        }
        Offset += Count;
        if (Count & 1) { if (Offset >= End) return R; ++Offset; }
    }
    if (!HasFmt || !PCM || !ValidShape(Rate, Channels) || PCMBytes <= 0) return R;
    if (!((Format == 1 && (Bits == 16 || Bits == 24 || Bits == 32)) || (Format == 3 && Bits == 32))) return R;
    const int32 SampleBytes = Bits / 8;
    if (Align != SampleBytes * Channels || ByteRate != uint32(Rate * Align) || PCMBytes % Align) return R;
    const int64 Samples = PCMBytes / SampleBytes;
    if (!IsDecodedSizeAllowed(Samples)) return R;
    R.PCM.SetNumUninitialized(int32(Samples));
    for (int32 i = 0; i < R.PCM.Num(); ++i)
    {
        const uint8* P = PCM + int64(i) * SampleBytes;
        float Value;
        if (Format == 3) { const uint32 V = U32(P); FMemory::Memcpy(&Value, &V, 4); }
        else if (Bits == 16) Value = int16(U16(P)) / 32768.f;
        else if (Bits == 24) { int32 V = int32(P[0]) | int32(P[1]) << 8 | int32(P[2]) << 16; if (V & 0x800000) V |= int32(0xff000000); Value = V / 8388608.f; }
        else Value = int32(U32(P)) / 2147483648.f;
        if (!FMath::IsFinite(Value)) return {};
        R.PCM[i] = FMath::Clamp(Value, -1.f, 1.f);
    }
    R.NumChannels = Channels; R.SampleRate = Rate; R.bSuccess = true;
    return R;
}

FAudioLoomPcmResult FAudioLoomPcmLoader::LoadFromFile(const FString& Path)
{
    const int64 Size = IFileManager::Get().FileSize(*Path);
    if (Size < 12 || Size > MaxFileBytes()) return {};
    TArray<uint8> Bytes;
    return FFileHelper::LoadFileToArray(Bytes, *Path) ? LoadFromMemory(Bytes) : FAudioLoomPcmResult{};
}

FAudioLoomPcmResult FAudioLoomPcmLoader::LoadFromSoundWave(USoundWave* Wave)
{
    if (!IsValid(Wave)) return {};
#if WITH_EDITOR
    TArray<uint8> Imported;
    uint32 Rate = 0; uint16 Channels = 0;
    if (Wave->GetImportedSoundWaveData(Imported, Rate, Channels))
    {
        FAudioLoomPcmResult Result = FromInt16(Imported.GetData(), Imported.Num(), Rate, Channels);
        if (Result.bSuccess) return Result;
    }
#endif
    if (Wave->RawPCMData && Wave->RawPCMDataSize > 0)
    {
        FAudioLoomPcmResult Result = FromInt16(Wave->RawPCMData, Wave->RawPCMDataSize, Wave->GetSampleRateForCurrentPlatform(), Wave->NumChannels);
        if (Result.bSuccess) return Result;
    }
    // Full-file decoding deliberately requires inline cooked data. Streaming codecs
    // must not be read with ExpandFile (their chunks may not be resident).
    if (Wave->IsStreaming())
    {
        FAudioLoomPcmResult Result = SidecarFor(Wave);
        if (!Result.bSuccess) UE_LOG(LogTemp, Error, TEXT("SpatialFabric playback: %s uses streaming audio. Set Loading Behavior to Force Inline and recook, or stage a compatible sibling WAV."), *Wave->GetName());
        return Result;
    }
    const FName Format = Wave->GetRuntimeFormat();
    TUniquePtr<ICompressedAudioInfo> Decoder(IAudioInfoFactoryRegistry::Get().Create(Format));
    if (!Decoder || !Wave->InitAudioResource(Format) || !Wave->GetResourceData()) return SidecarFor(Wave);
    FSoundQualityInfo Info;
    if (!Decoder->ReadCompressedInfo(Wave->GetResourceData(), Wave->GetResourceSize(), &Info)
        || !ValidShape(Info.SampleRate, Info.NumChannels) || Info.SampleDataSize == 0
        || Info.SampleDataSize > MAX_int32 || !IsDecodedSizeAllowed(Info.SampleDataSize / 2)
        || Info.SampleDataSize % (2 * Info.NumChannels)) return SidecarFor(Wave);
    TArray<uint8> PCM;
    PCM.SetNumZeroed(Info.SampleDataSize);
    Decoder->ExpandFile(PCM.GetData(), &Info);
    if (Decoder->HasError()) return SidecarFor(Wave);
    if (Info.NumChannels > 2 && Decoder->UsesVorbisChannelOrdering())
    {
        UE_LOG(LogTemp, Error, TEXT("SpatialFabric playback: multichannel Vorbis ordering requires an explicit channel map; use PCM/ADPCM for %s."), *Wave->GetName());
        return SidecarFor(Wave);
    }
    return FromInt16(PCM.GetData(), PCM.Num(), Info.SampleRate, Info.NumChannels);
}
