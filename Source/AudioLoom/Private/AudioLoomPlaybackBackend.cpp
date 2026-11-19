// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#include "AudioLoomPlaybackBackend.h"
#include "AudioLoomPcmRender.h"
#include "Containers/Queue.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"
#include <atomic>
#include <thread>
#include <chrono>
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include "Windows/HideWindowsPlatformTypes.h"
#elif PLATFORM_MAC
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#if PLATFORM_WINDOWS || PLATFORM_MAC
class FAudioLoomPlaybackBackendImpl
{
public:
    std::atomic<bool> StopRequested{false}, Running{false}, Playing{false}, Loop{false};
    std::atomic<float> Latency{0};
    TQueue<FAudioLoomPlaybackEvent, EQueueMode::Spsc> Events;
    TSharedPtr<const TArray<float>, ESPMode::ThreadSafe> PCM;
    int32 Channels = 1, SourceRate = 48000, OutputChannel = 0, BufferMs = 0;
    bool Exclusive = false;
    FString DeviceId;
    mutable FCriticalSection ErrorLock;
    FString Error;
    std::thread Thread;
    void Fail(const FString& Reason)
    {
        FScopeLock Lock(&ErrorLock);
        Error = Reason;
        UE_LOG(LogTemp, Error, TEXT("SpatialFabric playback: %s"), *Reason);
    }
    void Run()
    {
        const bool Completed = RunPlatform();
        Latency = 0;
        Playing = false;
        Running = false;
        FScopeLock Lock(&ErrorLock);
        if (!Completed && !StopRequested && Error.IsEmpty()) Error = TEXT("Playback worker could not start or continue");
        Events.Enqueue({false, Completed && !StopRequested.load(), Error});
    }
    bool RunPlatform();
#if PLATFORM_MAC
    TUniquePtr<FAudioLoomPcmCursor> Cursor;
    TArray<float> Scratch;
    std::atomic<bool> Finished{false}, CallbackError{false};
    std::atomic<UInt32> LastBufferFrames{0};
    std::atomic<double> LastCallbackTime{0};
    int32 DeviceChannels = 0;
    static OSStatus Callback(AudioObjectID, const AudioTimeStamp*, const AudioBufferList*, const AudioTimeStamp*, AudioBufferList* Out, const AudioTimeStamp*, void* Context);
#endif
};

#if PLATFORM_WINDOWS
bool FAudioLoomPlaybackBackendImpl::RunPlatform()
{
    using Microsoft::WRL::ComPtr;
    const HRESULT Init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(Init)) { Fail(TEXT("COM initialization failed")); return false; }
    struct FComScope { ~FComScope() { CoUninitialize(); } } ComScope;
    ComPtr<IMMDeviceEnumerator> Enumerator;
    ComPtr<IMMDevice> Device;
    ComPtr<IAudioClient> Client;
    ComPtr<IAudioRenderClient> Render;
    auto Check = [&](HRESULT Result, const TCHAR* Operation)
    {
        if (SUCCEEDED(Result)) return true;
        Fail(FString::Printf(TEXT("%s failed (0x%08x)"), Operation, uint32(Result)));
        return false;
    };
    if (!Check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(Enumerator.GetAddressOf())), TEXT("Device enumeration"))) return false;
    // Explicit routes fail closed; never redirect a show cue to the default endpoint.
    HRESULT Hr = DeviceId.IsEmpty() ? Enumerator->GetDefaultAudioEndpoint(eRender, eConsole, Device.GetAddressOf())
        : Enumerator->GetDevice(*DeviceId, Device.GetAddressOf());
    if (!Check(Hr, TEXT("Resolve output device"))) return false;
    auto Activate = [&]() { Client.Reset(); return Check(Device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)Client.GetAddressOf()), TEXT("Activate stream")); };
    if (!Activate()) return false;
    WAVEFORMATEX* RawFormat = nullptr;
    if (!Check(Client->GetMixFormat(&RawFormat), TEXT("Get mix format"))) return false;
    struct FFormatScope { WAVEFORMATEX* Ptr; ~FFormatScope() { CoTaskMemFree(Ptr); } } FormatScope{RawFormat};
    const int32 OutChannels = RawFormat->nChannels;
    const int32 Rate = RawFormat->nSamplesPerSec;
    const int32 Bits = RawFormat->wBitsPerSample;
    int32 ValidBits = Bits;
    bool Float = RawFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool Integer = RawFormat->wFormatTag == WAVE_FORMAT_PCM;
    if (RawFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && RawFormat->cbSize >= 22)
    {
        auto* Ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(RawFormat);
        Float = Ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        Integer = Ext->SubFormat == KSDATAFORMAT_SUBTYPE_PCM;
        ValidBits = Ext->Samples.wValidBitsPerSample;
    }
    if (OutChannels <= 0 || OutChannels > 64 || OutputChannel > OutChannels || Rate < 8000 || Rate > 384000
        || (!Float && !Integer) || (Float ? Bits != 32 : (Bits != 16 && Bits != 24 && Bits != 32))
        || ValidBits <= 0 || ValidBits > Bits || RawFormat->nBlockAlign != OutChannels * Bits / 8)
    { Fail(TEXT("Unsupported device format or output channel exceeds the opened layout")); return false; }
    AUDCLNT_SHAREMODE Mode = Exclusive ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;
    REFERENCE_TIME DefaultPeriod = 0, MinimumPeriod = 0;
    Client->GetDevicePeriod(&DefaultPeriod, &MinimumPeriod);
    REFERENCE_TIME Duration = Exclusive ? (BufferMs > 0 ? int64(FMath::Clamp(BufferMs, 1, 100)) * 10000 : MinimumPeriod) : 1000000; // 100ms shared, bounded polling
    // Polling mode avoids exclusive event streams' exact-full-buffer restrictions.
    Hr = Client->Initialize(Mode, 0, Duration, Exclusive ? Duration : 0, RawFormat, nullptr);
    if (FAILED(Hr) && Exclusive)
    {
        if (!Activate()) return false;
        Mode = AUDCLNT_SHAREMODE_SHARED;
        Hr = Client->Initialize(Mode, 0, 1000000, 0, RawFormat, nullptr);
    }
    if (!Check(Hr, TEXT("Initialize audio stream"))) return false;
    UINT32 Capacity = 0;
    if (!Check(Client->GetBufferSize(&Capacity), TEXT("Get buffer size")) || Capacity == 0 || uint64(Capacity) * OutChannels > 16 * 1024 * 1024) return false;
    if (!Check(Client->GetService(IID_PPV_ARGS(Render.GetAddressOf())), TEXT("Get render service"))) return false;
    TArray<float> Buffer;
    Buffer.SetNumUninitialized(Capacity * OutChannels);
    FAudioLoomPcmCursor Cursor(*PCM, Channels, SourceRate, Rate);
    auto Fill = [&](UINT32 Frames)
    {
        BYTE* Data = nullptr;
        if (!Check(Render->GetBuffer(Frames, &Data), TEXT("Get render buffer"))) return false;
        Cursor.Render(Buffer.GetData(), Frames, OutChannels, OutputChannel, Loop.load());
        const bool Encoded = AudioLoomEncodePCM(Buffer.GetData(), Frames * OutChannels, Data, Float, Bits, ValidBits);
        const HRESULT Release = Render->ReleaseBuffer(Frames, Encoded ? 0 : AUDCLNT_BUFFERFLAGS_SILENT);
        return Encoded && Check(Release, TEXT("Release render buffer"));
    };
    if (StopRequested || !Fill(Capacity)) return false;
    if (!Check(Client->Start(), TEXT("Start stream"))) return false;
    struct FStopScope { IAudioClient* Client; ~FStopScope() { Client->Stop(); } } StopScope{Client.Get()};
    REFERENCE_TIME Delay = 0;
    if (SUCCEEDED(Client->GetStreamLatency(&Delay))) Latency = float(Delay / 10000.0);
    else Latency = Capacity * 1000.f / Rate;
    Playing = true;
    Events.Enqueue({true, false});
    const auto Sleep = std::chrono::milliseconds(FMath::Clamp(int32(Capacity * 250.0 / Rate), 1, 10));
    double LastProgress = FPlatformTime::Seconds();
    UINT32 LastPadding = Capacity;
    while (!StopRequested)
    {
        UINT32 Padding = 0;
        if (!Check(Client->GetCurrentPadding(&Padding), TEXT("Read output padding")) || Padding > Capacity) return false;
        if (Padding != LastPadding) LastProgress = FPlatformTime::Seconds();
        LastPadding = Padding;
        if (!Loop.load() && Cursor.AtEnd())
        {
            if (Padding == 0) return true; // natural completion only AFTER queued output drains
        }
        else if (Capacity > Padding)
        {
            if (!Fill(Capacity - Padding)) return false;
            LastProgress = FPlatformTime::Seconds();
        }
        if (FPlatformTime::Seconds() - LastProgress > 5.0) { Fail(TEXT("Output device stalled")); return false; }
        std::this_thread::sleep_for(Sleep);
    }
    return false;
}
#elif PLATFORM_MAC
OSStatus FAudioLoomPlaybackBackendImpl::Callback(AudioObjectID, const AudioTimeStamp*, const AudioBufferList*, const AudioTimeStamp*, AudioBufferList* Out, const AudioTimeStamp*, void* Context)
{
    auto* Self = static_cast<FAudioLoomPlaybackBackendImpl*>(Context);
    Self->LastCallbackTime = FPlatformTime::Seconds();
    UInt32 Frames = MAX_uint32;
    for (UInt32 i = 0; i < Out->mNumberBuffers; ++i)
    {
        auto& B = Out->mBuffers[i];
        if (B.mData) { FMemory::Memzero(B.mData, B.mDataByteSize); Frames = FMath::Min(Frames, B.mDataByteSize / (sizeof(float) * FMath::Max(1u, B.mNumberChannels))); }
    }
    if (Self->StopRequested || Self->Finished) return noErr;
    if (Frames == MAX_uint32 || Frames == 0 || uint64(Frames) * Self->DeviceChannels > uint64(Self->Scratch.Num())) { Self->CallbackError = true; return noErr; }
    Self->LastBufferFrames = Frames;
    const bool End = Self->Cursor->Render(Self->Scratch.GetData(), Frames, Self->DeviceChannels, Self->OutputChannel, Self->Loop.load());
    UInt32 Offset = 0;
    for (UInt32 i = 0; i < Out->mNumberBuffers; ++i)
    {
        auto& B = Out->mBuffers[i];
        if (B.mData)
            for (UInt32 F = 0; F < Frames; ++F)
                for (UInt32 C = 0; C < B.mNumberChannels && Offset + C < UInt32(Self->DeviceChannels); ++C)
                    static_cast<float*>(B.mData)[F * B.mNumberChannels + C] = Self->Scratch[F * Self->DeviceChannels + Offset + C];
        Offset += B.mNumberChannels;
    }
    Self->Finished = End;
    return noErr;
}
bool FAudioLoomPlaybackBackendImpl::RunPlatform()
{
    AudioDeviceID Device = kAudioObjectUnknown;
    AudioObjectPropertyAddress Address{kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 Size = sizeof(Device);
    if (DeviceId.IsEmpty())
    {
        if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &Address, 0, nullptr, &Size, &Device) != noErr) { Fail(TEXT("No default output device")); return false; }
    }
    else
    {
        Address.mSelector = kAudioHardwarePropertyDevices;
        Size = 0;
        if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &Address, 0, nullptr, &Size) != noErr) return false;
        TArray<AudioDeviceID> Devices; Devices.SetNum(Size / sizeof(AudioDeviceID));
        if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &Address, 0, nullptr, &Size, Devices.GetData()) != noErr) return false;
        for (auto ID : Devices)
        {
            AudioObjectPropertyAddress UIDAddress{kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
            CFStringRef UID = nullptr; UInt32 UIDSize = sizeof(UID);
            if (AudioObjectGetPropertyData(ID, &UIDAddress, 0, nullptr, &UIDSize, &UID) == noErr && UID)
            {
                char Text[4096];
                if (CFStringGetCString(UID, Text, sizeof(Text), kCFStringEncodingUTF8) && DeviceId.Equals(UTF8_TO_TCHAR(Text), ESearchCase::CaseSensitive)) Device = ID;
                CFRelease(UID);
            }
        }
    }
    if (Device == kAudioObjectUnknown) { Fail(TEXT("Selected output device is unavailable")); return false; }
    // Validate every stream; do not cast arbitrary hardware sample bytes to float.
    Address = {kAudioDevicePropertyStreams, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMain};
    Size = 0;
    if (AudioObjectGetPropertyDataSize(Device, &Address, 0, nullptr, &Size) != noErr || Size == 0) return false;
    TArray<AudioStreamID> Streams; Streams.SetNum(Size / sizeof(AudioStreamID));
    if (AudioObjectGetPropertyData(Device, &Address, 0, nullptr, &Size, Streams.GetData()) != noErr) return false;
    double Rate = 0; DeviceChannels = 0;
    for (auto Stream : Streams)
    {
        AudioObjectPropertyAddress FormatAddress{kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        AudioStreamBasicDescription Format{}; Size = sizeof(Format);
        if (AudioObjectGetPropertyData(Stream, &FormatAddress, 0, nullptr, &Size, &Format) != noErr
            || Format.mFormatID != kAudioFormatLinearPCM || !(Format.mFormatFlags & kAudioFormatFlagIsFloat)
            || (Format.mFormatFlags & kAudioFormatFlagIsBigEndian) || Format.mBitsPerChannel != 32 || Format.mFramesPerPacket != 1
            || Format.mBytesPerFrame != 4 * ((Format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) ? 1 : Format.mChannelsPerFrame)
            || !FMath::IsFinite(Format.mSampleRate) || (Rate != 0 && Rate != Format.mSampleRate)) { Fail(TEXT("Unsupported Core Audio stream format")); return false; }
        Rate = Format.mSampleRate; DeviceChannels += Format.mChannelsPerFrame;
    }
    if (Rate < 8000 || Rate > 384000 || DeviceChannels < 1 || DeviceChannels > 64 || OutputChannel > DeviceChannels) { Fail(TEXT("Invalid Core Audio rate/channel layout")); return false; }
    Address = {kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioValueRange Range{}; Size = sizeof(Range);
    if (AudioObjectGetPropertyData(Device, &Address, 0, nullptr, &Size, &Range) != noErr) return false;
    if (Range.mMaximum < 1 || Range.mMaximum > 65536) { Fail(TEXT("Unsupported Core Audio buffer capacity")); return false; }
    const int32 MaxFrames = int32(Range.mMaximum);
    Scratch.SetNumZeroed(MaxFrames * DeviceChannels);
    Cursor = MakeUnique<FAudioLoomPcmCursor>(*PCM, Channels, SourceRate, Rate);
    Finished = false; CallbackError = false; LastCallbackTime = FPlatformTime::Seconds(); LastBufferFrames = 0;
    AudioDeviceIOProcID Proc = nullptr;
    if (AudioDeviceCreateIOProcID(Device, Callback, this, &Proc) != noErr) return false;
    if (AudioDeviceStart(Device, Proc) != noErr) { AudioDeviceDestroyIOProcID(Device, Proc); Fail(TEXT("Core Audio start failed")); return false; }
    Address = {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 Frames = 0; Size = sizeof(Frames);
    AudioObjectGetPropertyData(Device, &Address, 0, nullptr, &Size, &Frames);
    Latency = float(Frames * 1000.0 / Rate);
    Playing = true;
    Events.Enqueue({true, false});
    while (!StopRequested && !Finished)
    {
        if (CallbackError || FPlatformTime::Seconds() - LastCallbackTime.load() > 2.0) { Fail(TEXT("Core Audio callback failed/stalled")); break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // Wait for the actual final callback buffer, but allow explicit Stop to interrupt.
    const double DrainUntil = FPlatformTime::Seconds() + double(LastBufferFrames.load()) / Rate + 0.002;
    while (Finished && !StopRequested && FPlatformTime::Seconds() < DrainUntil) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AudioDeviceStop(Device, Proc);
    AudioDeviceDestroyIOProcID(Device, Proc);
    return Finished.load() && !StopRequested.load();
}
#endif
#endif

FAudioLoomPlaybackBackend::FAudioLoomPlaybackBackend()
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    Impl = MakeUnique<FAudioLoomPlaybackBackendImpl>();
#endif
}
FAudioLoomPlaybackBackend::~FAudioLoomPlaybackBackend() { Stop(); }
void FAudioLoomPlaybackBackend::Start(const FString& Id, const TArray<float>& PCM, int32 Channels, int32 Channel, bool Loop, bool Exclusive, int32 BufferMs, int32 SourceRate)
{
    StartPrepared(Id, MakeShared<TArray<float>, ESPMode::ThreadSafe>(PCM), Channels, Channel, Loop, Exclusive, BufferMs, SourceRate);
}
void FAudioLoomPlaybackBackend::StartPrepared(const FString& Id, TSharedPtr<const TArray<float>, ESPMode::ThreadSafe> PCM, int32 Channels, int32 Channel, bool Loop, bool Exclusive, int32 BufferMs, int32 SourceRate)
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    Stop();
    { FScopeLock Lock(&Impl->ErrorLock); Impl->Error.Empty(); }
    if (Channels <= 0 || Channels > 64 || SourceRate < 8000 || SourceRate > 384000 || !PCM || PCM->Num() == 0 || PCM->Num() % Channels || Channel < 0)
    { Impl->Fail(TEXT("Invalid source PCM or routing")); Impl->Events.Enqueue({false, false, TEXT("Invalid source PCM or routing")}); return; }
    Impl->PCM = PCM; Impl->Channels = Channels; Impl->SourceRate = SourceRate; Impl->OutputChannel = Channel;
    Impl->DeviceId = Id; Impl->Exclusive = Exclusive; Impl->BufferMs = BufferMs;
    Impl->StopRequested = false; Impl->Loop = Loop; Impl->Running = true;
    Impl->Thread = std::thread([this] { Impl->Run(); });
#else
    bUnsupportedStartPending = true;
#endif
}
void FAudioLoomPlaybackBackend::Stop()
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    Impl->StopRequested = true;
    if (Impl->Thread.joinable()) Impl->Thread.join();
    Impl->Running = false; Impl->Playing = false; Impl->Latency = 0;
    Impl->PCM.Reset();
#if PLATFORM_MAC
    Impl->Cursor.Reset(); Impl->Scratch.Reset();
#endif
#endif
}
void FAudioLoomPlaybackBackend::SetLoop(bool Value)
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    Impl->Loop = Value;
#endif
}
void FAudioLoomPlaybackBackend::DrainEvents(TArray<FAudioLoomPlaybackEvent>& Events)
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    FAudioLoomPlaybackEvent Event;
    while (Impl->Events.Dequeue(Event)) Events.Add(Event);
#else
    if (bUnsupportedStartPending) { Events.Add({false, false, GetLastError()}); bUnsupportedStartPending = false; }
#endif
}
bool FAudioLoomPlaybackBackend::IsPlaying() const
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    return Impl->Playing.load();
#else
    return false;
#endif
}
float FAudioLoomPlaybackBackend::GetOutputLatencyMs() const
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    return Impl->Latency.load();
#else
    return 0;
#endif
}
FString FAudioLoomPlaybackBackend::GetLastError() const
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    FScopeLock Lock(&Impl->ErrorLock);
    return Impl->Error;
#else
    return TEXT("Direct playback is not supported on this platform");
#endif
}
