// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#include "AudioLoomLiveBackend.h"
#include "AudioLoomPcmRender.h"
#include "SpatialFabricLiveBridge.h"
#include "Containers/Queue.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
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
class FAudioLoomLiveBackendImpl
{
public:
    std::atomic<bool> StopRequested{false}, Running{false};
    std::atomic<float> Gain{1.f};
    TQueue<FAudioLoomPlaybackEvent, EQueueMode::Spsc> Events;
    FString InputId, OutputId;
    int32 InputChannel = 0, OutputChannel = 0, BufferMs = 0;
    bool Exclusive = false;
    mutable FCriticalSection ErrorLock;
    FString Error;
    std::thread Thread;
    mutable FCriticalSection StatusLock;
    FAudioLoomLiveStatus Status;
    std::unique_ptr<SpatialFabricLive::FBridge> Bridge;

    void Fail(const FString& Reason)
    {
        FScopeLock Lock(&ErrorLock);
        Error = Reason;
        UE_LOG(LogTemp, Error, TEXT("SpatialFabric live input: %s"), *Reason);
    }
    void Run()
    {
        const bool bOk = RunPlatform();
        {
            FScopeLock Lock(&StatusLock);
            Status = FAudioLoomLiveStatus();
        }
        Running = false;
        FScopeLock Lock(&ErrorLock);
        if (!bOk && !StopRequested && Error.IsEmpty()) Error = TEXT("Live worker could not start or continue");
        Events.Enqueue({false, false, Error});
    }
    void Publish(int32 InCh, int32 InRate, int32 OutCh, int32 OutRate)
    {
        const SpatialFabricLive::FStats Stats = Bridge->Snapshot();
        const float Peak = Bridge->TakePeak();
        FScopeLock Lock(&StatusLock);
        Status.bRunning = true;
        Status.InputChannels = InCh; Status.InputRate = InRate; Status.OutputChannels = OutCh; Status.OutputRate = OutRate;
        Status.InputPeak = FMath::Max(Status.InputPeak, Peak);
        Status.LatencyMs = InRate > 0 ? float(Stats.FillFrames) * 1000.f / float(InRate) : 0.f;
        Status.CapturedFrames = Stats.Captured; Status.RenderedFrames = Stats.Rendered; Status.Underruns = Stats.Underruns; Status.Overruns = Stats.Overruns;
    }
    bool RunPlatform();
#if PLATFORM_MAC
    TArray<float> InScratch, OutScratch;
    std::atomic<bool> CallbackError{false};
    std::atomic<double> LastInputTime{0}, LastOutputTime{0};
    int32 InDeviceChannels = 0, OutDeviceChannels = 0;
    static OSStatus InputCallback(AudioObjectID, const AudioTimeStamp*, const AudioBufferList* In, const AudioTimeStamp*, AudioBufferList*, const AudioTimeStamp*, void* Context);
    static OSStatus OutputCallback(AudioObjectID, const AudioTimeStamp*, const AudioBufferList*, const AudioTimeStamp*, AudioBufferList* Out, const AudioTimeStamp*, void* Context);
#endif
};

#if PLATFORM_WINDOWS
bool FAudioLoomLiveBackendImpl::RunPlatform()
{
    using Microsoft::WRL::ComPtr;
    const HRESULT Init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(Init)) { Fail(TEXT("COM initialization failed")); return false; }
    struct FComScope { ~FComScope() { CoUninitialize(); } } ComScope;
    auto Check = [&](HRESULT Result, const TCHAR* Operation)
    {
        if (SUCCEEDED(Result)) return true;
        Fail(FString::Printf(TEXT("%s failed (0x%08x)%s"), Operation, uint32(Result),
            Result == E_ACCESSDENIED ? TEXT(" - check Windows Settings > Privacy > Microphone for desktop apps") : TEXT("")));
        return false;
    };
    ComPtr<IMMDeviceEnumerator> Enumerator;
    if (!Check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(Enumerator.GetAddressOf())), TEXT("Device enumeration"))) return false;
    // Explicit routes fail closed; never redirect a configured live route to the default endpoint.
    ComPtr<IMMDevice> InDevice, OutDevice;
    HRESULT Hr = InputId.IsEmpty() ? Enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, InDevice.GetAddressOf()) : Enumerator->GetDevice(*InputId, InDevice.GetAddressOf());
    if (!Check(Hr, TEXT("Resolve input device (no default fallback)"))) return false;
    Hr = OutputId.IsEmpty() ? Enumerator->GetDefaultAudioEndpoint(eRender, eConsole, OutDevice.GetAddressOf()) : Enumerator->GetDevice(*OutputId, OutDevice.GetAddressOf());
    if (!Check(Hr, TEXT("Resolve output device (no default fallback)"))) return false;
    ComPtr<IAudioClient> InClient, OutClient;
    if (!Check(InDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)InClient.GetAddressOf()), TEXT("Activate input stream"))) return false;
    if (!Check(OutDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)OutClient.GetAddressOf()), TEXT("Activate output stream"))) return false;
    WAVEFORMATEX* InRaw = nullptr; WAVEFORMATEX* OutRaw = nullptr;
    if (!Check(InClient->GetMixFormat(&InRaw), TEXT("Get input mix format"))) return false;
    struct FFormatScope { WAVEFORMATEX* Ptr; ~FFormatScope() { CoTaskMemFree(Ptr); } } InScope{InRaw};
    if (!Check(OutClient->GetMixFormat(&OutRaw), TEXT("Get output mix format"))) return false;
    FFormatScope OutScope{OutRaw};
    struct FFormat { bool Float = false, Ok = false; int32 Bits = 0, Valid = 0, Channels = 0, Rate = 0; };
    auto Describe = [](WAVEFORMATEX* Raw)
    {
        FFormat F;
        F.Bits = Raw->wBitsPerSample; F.Valid = F.Bits; F.Channels = Raw->nChannels; F.Rate = int32(Raw->nSamplesPerSec);
        F.Float = Raw->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        bool Integer = Raw->wFormatTag == WAVE_FORMAT_PCM;
        if (Raw->wFormatTag == WAVE_FORMAT_EXTENSIBLE && Raw->cbSize >= 22)
        {
            auto* Ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(Raw);
            F.Float = Ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            Integer = Ext->SubFormat == KSDATAFORMAT_SUBTYPE_PCM;
            F.Valid = Ext->Samples.wValidBitsPerSample;
        }
        F.Ok = F.Channels > 0 && F.Channels <= 64 && F.Rate >= 8000 && F.Rate <= 384000 && (F.Float || Integer)
            && (F.Float ? F.Bits == 32 : (F.Bits == 16 || F.Bits == 24 || F.Bits == 32)) && F.Valid > 0 && F.Valid <= F.Bits
            && Raw->nBlockAlign == F.Channels * F.Bits / 8;
        return F;
    };
    const FFormat In = Describe(InRaw), Out = Describe(OutRaw);
    if (!In.Ok || !Out.Ok) { Fail(TEXT("Unsupported input or output device format")); return false; }
    if (InputChannel > In.Channels) { Fail(TEXT("Selected input channel exceeds the actual device channels")); return false; }
    if (OutputChannel > Out.Channels) { Fail(TEXT("Selected output channel exceeds the actual device channels")); return false; }
    const int32 TargetMs = FMath::Clamp(BufferMs > 0 ? BufferMs * 2 : 40, 20, 500);
    if (!SpatialFabricLive::FBridge::IsValidConfig(In.Channels, In.Rate, Out.Channels, Out.Rate, InputChannel, OutputChannel, TargetMs)) { Fail(TEXT("Invalid live bridge configuration")); return false; }
    Bridge = std::make_unique<SpatialFabricLive::FBridge>(In.Channels, In.Rate, Out.Channels, Out.Rate, InputChannel, OutputChannel, TargetMs);
    // Capture: shared, polled (matches the playback backend's polling design).
    if (!Check(InClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, InRaw, nullptr), TEXT("Initialize capture stream"))) return false;
    // Render: exclusive is honored or the start fails; it never silently becomes shared.
    const AUDCLNT_SHAREMODE Mode = Exclusive ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;
    REFERENCE_TIME DefaultPeriod = 0, MinimumPeriod = 0;
    OutClient->GetDevicePeriod(&DefaultPeriod, &MinimumPeriod);
    const REFERENCE_TIME Duration = Exclusive ? (BufferMs > 0 ? int64(FMath::Clamp(BufferMs, 1, 100)) * 10000 : MinimumPeriod) : 1000000;
    if (!Check(OutClient->Initialize(Mode, 0, Duration, Exclusive ? Duration : 0, OutRaw, nullptr), Exclusive ? TEXT("Initialize exclusive output (no shared fallback)") : TEXT("Initialize output stream"))) return false;
    UINT32 InCapacity = 0, OutCapacity = 0;
    if (!Check(InClient->GetBufferSize(&InCapacity), TEXT("Get capture buffer size")) || InCapacity == 0 || uint64(InCapacity) * In.Channels > 16 * 1024 * 1024) return false;
    if (!Check(OutClient->GetBufferSize(&OutCapacity), TEXT("Get output buffer size")) || OutCapacity == 0 || uint64(OutCapacity) * Out.Channels > 16 * 1024 * 1024) return false;
    ComPtr<IAudioCaptureClient> Capture;
    ComPtr<IAudioRenderClient> Render;
    if (!Check(InClient->GetService(IID_PPV_ARGS(Capture.GetAddressOf())), TEXT("Get capture service"))) return false;
    if (!Check(OutClient->GetService(IID_PPV_ARGS(Render.GetAddressOf())), TEXT("Get render service"))) return false;
    TArray<float> InBuffer, OutBuffer;
    InBuffer.SetNumZeroed(int32(InCapacity) * In.Channels * 2);
    OutBuffer.SetNumZeroed(int32(OutCapacity) * Out.Channels);
    auto Submit = [&](UINT32 Frames)
    {
        BYTE* Data = nullptr;
        if (!Check(Render->GetBuffer(Frames, &Data), TEXT("Get render buffer"))) return false;
        const int32 Silent = Bridge->Pull(OutBuffer.GetData(), int32(Frames), Gain.load());
        const bool Encoded = Silent >= 0 && AudioLoomEncodePCM(OutBuffer.GetData(), int32(Frames) * Out.Channels, Data, Out.Float, Out.Bits, Out.Valid);
        const HRESULT Release = Render->ReleaseBuffer(Frames, Encoded ? 0 : AUDCLNT_BUFFERFLAGS_SILENT);
        return Encoded && Check(Release, TEXT("Release render buffer"));
    };
    auto DrainCapture = [&](bool& bGotPackets)
    {
        for (;;)
        {
            UINT32 Packet = 0;
            if (!Check(Capture->GetNextPacketSize(&Packet), TEXT("Get capture packet size"))) return false;
            if (Packet == 0) return true;
            BYTE* Data = nullptr; UINT32 Frames = 0; DWORD Flags = 0;
            if (!Check(Capture->GetBuffer(&Data, &Frames, &Flags, nullptr, nullptr), TEXT("Get capture buffer"))) return false;
            const int64 Samples = int64(Frames) * In.Channels;
            bool Good = Samples <= MAX_int32;
            if (Good)
            {
                if (Samples > InBuffer.Num()) InBuffer.SetNumZeroed(int32(Samples));
                if (Flags & AUDCLNT_BUFFERFLAGS_SILENT) FMemory::Memzero(InBuffer.GetData(), size_t(Samples) * sizeof(float));
                else Good = AudioLoomDecodePCM(Data, int32(Samples), InBuffer.GetData(), In.Float, In.Bits, In.Valid);
                Good = Good && Bridge->Push(InBuffer.GetData(), int32(Frames));
            }
            const HRESULT Release = Capture->ReleaseBuffer(Frames);
            if (!Good) { Fail(TEXT("Capture data could not be decoded")); return false; }
            if (!Check(Release, TEXT("Release capture buffer"))) return false;
            bGotPackets = true;
        }
    };
    if (StopRequested) return false;
    {
        BYTE* Data = nullptr;
        if (!Check(Render->GetBuffer(OutCapacity, &Data), TEXT("Prime output buffer"))) return false;
        if (!Check(Render->ReleaseBuffer(OutCapacity, AUDCLNT_BUFFERFLAGS_SILENT), TEXT("Submit silent prime"))) return false;
    }
    if (!Check(InClient->Start(), TEXT("Start capture"))) return false;
    struct FStopScope { IAudioClient* Client; ~FStopScope() { Client->Stop(); } } InStop{InClient.Get()};
    if (!Check(OutClient->Start(), TEXT("Start output"))) return false;
    FStopScope OutStop{OutClient.Get()};
    Running = true;
    Events.Enqueue({true, false});
    Publish(In.Channels, In.Rate, Out.Channels, Out.Rate);
    double LastInput = FPlatformTime::Seconds();
    const auto Sleep = std::chrono::milliseconds(2);
    while (!StopRequested)
    {
        bool bGot = false;
        if (!DrainCapture(bGot)) return false;
        if (bGot) LastInput = FPlatformTime::Seconds();
        UINT32 Padding = 0;
        if (!Check(OutClient->GetCurrentPadding(&Padding), TEXT("Read output padding")) || Padding > OutCapacity) return false;
        if (OutCapacity > Padding && !Submit(OutCapacity - Padding)) return false;
        Publish(In.Channels, In.Rate, Out.Channels, Out.Rate);
        if (FPlatformTime::Seconds() - LastInput > 3.0) { Fail(TEXT("No capture data for 3 seconds; the input device may be unavailable, disconnected or blocked by OS microphone privacy settings")); return false; }
        std::this_thread::sleep_for(Sleep);
    }
    return true;
}
#elif PLATFORM_MAC
namespace
{
bool QueryStreams(AudioDeviceID Device, AudioObjectPropertyScope Scope, double& Rate, int32& Channels)
{
    AudioObjectPropertyAddress Address{kAudioDevicePropertyStreams, Scope, kAudioObjectPropertyElementMain};
    UInt32 Size = 0;
    if (AudioObjectGetPropertyDataSize(Device, &Address, 0, nullptr, &Size) != noErr || Size == 0) return false;
    TArray<AudioStreamID> Streams; Streams.SetNum(Size / sizeof(AudioStreamID));
    if (AudioObjectGetPropertyData(Device, &Address, 0, nullptr, &Size, Streams.GetData()) != noErr) return false;
    Rate = 0; Channels = 0;
    for (auto Stream : Streams)
    {
        AudioObjectPropertyAddress FormatAddress{kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        AudioStreamBasicDescription Format{}; Size = sizeof(Format);
        if (AudioObjectGetPropertyData(Stream, &FormatAddress, 0, nullptr, &Size, &Format) != noErr
            || Format.mFormatID != kAudioFormatLinearPCM || !(Format.mFormatFlags & kAudioFormatFlagIsFloat)
            || (Format.mFormatFlags & kAudioFormatFlagIsBigEndian) || Format.mBitsPerChannel != 32 || Format.mFramesPerPacket != 1
            || Format.mBytesPerFrame != 4 * ((Format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) ? 1 : Format.mChannelsPerFrame)
            || !FMath::IsFinite(Format.mSampleRate) || (Rate != 0 && Rate != Format.mSampleRate)) return false;
        Rate = Format.mSampleRate; Channels += int32(Format.mChannelsPerFrame);
    }
    return Rate >= 8000 && Rate <= 384000 && Channels >= 1 && Channels <= 64;
}
AudioDeviceID ResolveDevice(const FString& Id, bool bCapture)
{
    AudioObjectPropertyAddress Address{bCapture ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioDeviceID Device = kAudioObjectUnknown; UInt32 Size = sizeof(Device);
    if (Id.IsEmpty())
    {
        if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &Address, 0, nullptr, &Size, &Device) != noErr) return kAudioObjectUnknown;
        return Device;
    }
    Address.mSelector = kAudioHardwarePropertyDevices; Size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &Address, 0, nullptr, &Size) != noErr || Size == 0 || Size > 256 * sizeof(AudioDeviceID)) return kAudioObjectUnknown;
    TArray<AudioDeviceID> Devices; Devices.SetNum(Size / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &Address, 0, nullptr, &Size, Devices.GetData()) != noErr) return kAudioObjectUnknown;
    for (AudioDeviceID Candidate : Devices)
    {
        AudioObjectPropertyAddress UIDAddress{kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        CFStringRef UID = nullptr; UInt32 UIDSize = sizeof(UID);
        if (AudioObjectGetPropertyData(Candidate, &UIDAddress, 0, nullptr, &UIDSize, &UID) == noErr && UID)
        {
            char Text[4096];
            const bool bMatch = CFStringGetCString(UID, Text, sizeof(Text), kCFStringEncodingUTF8) && Id == UTF8_TO_TCHAR(Text);
            CFRelease(UID);
            if (bMatch) return Candidate;
        }
    }
    return kAudioObjectUnknown;
}
}
OSStatus FAudioLoomLiveBackendImpl::InputCallback(AudioObjectID, const AudioTimeStamp*, const AudioBufferList* In, const AudioTimeStamp*, AudioBufferList*, const AudioTimeStamp*, void* Context)
{
    auto* Self = static_cast<FAudioLoomLiveBackendImpl*>(Context);
    Self->LastInputTime = FPlatformTime::Seconds();
    if (Self->StopRequested || !In) return noErr;
    UInt32 Frames = MAX_uint32;
    for (UInt32 i = 0; i < In->mNumberBuffers; ++i)
        if (In->mBuffers[i].mData) Frames = FMath::Min(Frames, In->mBuffers[i].mDataByteSize / (sizeof(float) * FMath::Max(1u, In->mBuffers[i].mNumberChannels)));
    if (Frames == MAX_uint32 || Frames == 0 || uint64(Frames) * Self->InDeviceChannels > uint64(Self->InScratch.Num())) { Self->CallbackError = true; return noErr; }
    UInt32 Offset = 0;
    for (UInt32 i = 0; i < In->mNumberBuffers; ++i)
    {
        const auto& B = In->mBuffers[i];
        if (B.mData)
            for (UInt32 F = 0; F < Frames; ++F)
                for (UInt32 C = 0; C < B.mNumberChannels && Offset + C < UInt32(Self->InDeviceChannels); ++C)
                    Self->InScratch[F * Self->InDeviceChannels + Offset + C] = static_cast<const float*>(B.mData)[F * B.mNumberChannels + C];
        Offset += B.mNumberChannels;
    }
    if (!Self->Bridge->Push(Self->InScratch.GetData(), int32(Frames))) Self->CallbackError = true;
    return noErr;
}
OSStatus FAudioLoomLiveBackendImpl::OutputCallback(AudioObjectID, const AudioTimeStamp*, const AudioBufferList*, const AudioTimeStamp*, AudioBufferList* Out, const AudioTimeStamp*, void* Context)
{
    auto* Self = static_cast<FAudioLoomLiveBackendImpl*>(Context);
    Self->LastOutputTime = FPlatformTime::Seconds();
    UInt32 Frames = MAX_uint32;
    for (UInt32 i = 0; i < Out->mNumberBuffers; ++i)
    {
        auto& B = Out->mBuffers[i];
        if (B.mData) { FMemory::Memzero(B.mData, B.mDataByteSize); Frames = FMath::Min(Frames, B.mDataByteSize / (sizeof(float) * FMath::Max(1u, B.mNumberChannels))); }
    }
    if (Self->StopRequested) return noErr;
    if (Frames == MAX_uint32 || Frames == 0 || uint64(Frames) * Self->OutDeviceChannels > uint64(Self->OutScratch.Num())) { Self->CallbackError = true; return noErr; }
    if (Self->Bridge->Pull(Self->OutScratch.GetData(), int32(Frames), Self->Gain.load()) < 0) { Self->CallbackError = true; return noErr; }
    UInt32 Offset = 0;
    for (UInt32 i = 0; i < Out->mNumberBuffers; ++i)
    {
        auto& B = Out->mBuffers[i];
        if (B.mData)
            for (UInt32 F = 0; F < Frames; ++F)
                for (UInt32 C = 0; C < B.mNumberChannels && Offset + C < UInt32(Self->OutDeviceChannels); ++C)
                    static_cast<float*>(B.mData)[F * B.mNumberChannels + C] = Self->OutScratch[F * Self->OutDeviceChannels + Offset + C];
        Offset += B.mNumberChannels;
    }
    return noErr;
}
bool FAudioLoomLiveBackendImpl::RunPlatform()
{
    const AudioDeviceID InDevice = ResolveDevice(InputId, true), OutDevice = ResolveDevice(OutputId, false);
    if (InDevice == kAudioObjectUnknown) { Fail(TEXT("Selected input device is unavailable (no default fallback)")); return false; }
    if (OutDevice == kAudioObjectUnknown) { Fail(TEXT("Selected output device is unavailable (no default fallback)")); return false; }
    double InRate = 0, OutRate = 0;
    if (!QueryStreams(InDevice, kAudioDevicePropertyScopeInput, InRate, InDeviceChannels)) { Fail(TEXT("Unsupported Core Audio input stream format")); return false; }
    if (!QueryStreams(OutDevice, kAudioDevicePropertyScopeOutput, OutRate, OutDeviceChannels)) { Fail(TEXT("Unsupported Core Audio output stream format")); return false; }
    if (InputChannel > InDeviceChannels || OutputChannel > OutDeviceChannels) { Fail(TEXT("Selected channel exceeds the opened device layout")); return false; }
    if (Exclusive) { Fail(TEXT("Exclusive output is not supported by the Core Audio live path (no shared fallback)")); return false; }
    const int32 TargetMs = FMath::Clamp(BufferMs > 0 ? BufferMs * 2 : 40, 20, 500);
    if (!SpatialFabricLive::FBridge::IsValidConfig(InDeviceChannels, int(InRate), OutDeviceChannels, int(OutRate), InputChannel, OutputChannel, TargetMs)) { Fail(TEXT("Invalid live bridge configuration")); return false; }
    Bridge = std::make_unique<SpatialFabricLive::FBridge>(InDeviceChannels, int(InRate), OutDeviceChannels, int(OutRate), InputChannel, OutputChannel, TargetMs);
    auto MaxFrames = [](AudioDeviceID Device, int32& Frames)
    {
        AudioObjectPropertyAddress Address{kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        AudioValueRange Range{}; UInt32 Size = sizeof(Range);
        if (AudioObjectGetPropertyData(Device, &Address, 0, nullptr, &Size, &Range) != noErr || Range.mMaximum < 1 || Range.mMaximum > 65536) return false;
        Frames = int32(Range.mMaximum);
        return true;
    };
    int32 InFrames = 0, OutFrames = 0;
    if (!MaxFrames(InDevice, InFrames) || !MaxFrames(OutDevice, OutFrames)) { Fail(TEXT("Unsupported Core Audio buffer capacity")); return false; }
    InScratch.SetNumZeroed(InFrames * InDeviceChannels);
    OutScratch.SetNumZeroed(OutFrames * OutDeviceChannels);
    CallbackError = false; LastInputTime = LastOutputTime = FPlatformTime::Seconds();
    AudioDeviceIOProcID InProc = nullptr, OutProc = nullptr;
    if (AudioDeviceCreateIOProcID(InDevice, InputCallback, this, &InProc) != noErr) { Fail(TEXT("Create Core Audio input callback failed")); return false; }
    if (AudioDeviceCreateIOProcID(OutDevice, OutputCallback, this, &OutProc) != noErr) { AudioDeviceDestroyIOProcID(InDevice, InProc); Fail(TEXT("Create Core Audio output callback failed")); return false; }
    auto Teardown = [&]() { AudioDeviceStop(InDevice, InProc); AudioDeviceStop(OutDevice, OutProc); AudioDeviceDestroyIOProcID(InDevice, InProc); AudioDeviceDestroyIOProcID(OutDevice, OutProc); };
    if (AudioDeviceStart(InDevice, InProc) != noErr) { Teardown(); Fail(TEXT("Core Audio input start failed (microphone access may be denied)")); return false; }
    if (AudioDeviceStart(OutDevice, OutProc) != noErr) { Teardown(); Fail(TEXT("Core Audio output start failed")); return false; }
    Running = true;
    Events.Enqueue({true, false});
    bool bOk = true;
    while (!StopRequested)
    {
        const double Now = FPlatformTime::Seconds();
        if (CallbackError) { Fail(TEXT("Core Audio live callback failed")); bOk = false; break; }
        if (Now - LastInputTime.load() > 3.0) { Fail(TEXT("No Core Audio input data for 3 seconds; device unavailable or microphone access denied")); bOk = false; break; }
        if (Now - LastOutputTime.load() > 3.0) { Fail(TEXT("Core Audio output stalled")); bOk = false; break; }
        Publish(InDeviceChannels, int32(InRate), OutDeviceChannels, int32(OutRate));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Teardown();
    return bOk;
}
#endif
#endif

FAudioLoomLiveBackend::FAudioLoomLiveBackend()
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    Impl = MakeUnique<FAudioLoomLiveBackendImpl>();
#endif
}
FAudioLoomLiveBackend::~FAudioLoomLiveBackend() { Stop(); }
void FAudioLoomLiveBackend::Start(const FString& InputDeviceId, int32 InputChannel, const FString& OutputDeviceId, int32 OutputChannel, bool bExclusive, int32 BufferSizeMs, float Gain)
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    Stop();
    { FScopeLock Lock(&Impl->ErrorLock); Impl->Error.Empty(); }
    if (InputChannel < 0 || InputChannel > 64 || OutputChannel < 0 || OutputChannel > 64 || BufferSizeMs < 0 || BufferSizeMs > 2000 || !FMath::IsFinite(Gain) || Gain < 0.f || Gain > 4.f)
    { Impl->Fail(TEXT("Invalid live input channel, buffer or gain request")); Impl->Events.Enqueue({false, false, TEXT("Invalid live input channel, buffer or gain request")}); return; }
    Impl->InputId = InputDeviceId; Impl->InputChannel = InputChannel; Impl->OutputId = OutputDeviceId; Impl->OutputChannel = OutputChannel;
    Impl->Exclusive = bExclusive; Impl->BufferMs = BufferSizeMs; Impl->Gain = Gain;
    Impl->StopRequested = false;
    Impl->Thread = std::thread([this] { Impl->Run(); });
#else
    (void)InputDeviceId; (void)InputChannel; (void)OutputDeviceId; (void)OutputChannel; (void)bExclusive; (void)BufferSizeMs; (void)Gain;
    bUnsupportedStartPending = true;
#endif
}
void FAudioLoomLiveBackend::Stop()
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    Impl->StopRequested = true;
    if (Impl->Thread.joinable()) Impl->Thread.join();
    Impl->Running = false;
    { FScopeLock Lock(&Impl->StatusLock); Impl->Status = FAudioLoomLiveStatus(); }
    Impl->Bridge.reset();
#endif
}
bool FAudioLoomLiveBackend::SetGain(float Gain)
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    if (!FMath::IsFinite(Gain) || Gain < 0.f || Gain > 4.f) return false;
    Impl->Gain = Gain;
    return true;
#else
    (void)Gain;
    return false;
#endif
}
void FAudioLoomLiveBackend::DrainEvents(TArray<FAudioLoomPlaybackEvent>& Events)
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    FAudioLoomPlaybackEvent Event;
    while (Impl->Events.Dequeue(Event)) Events.Add(Event);
#else
    if (bUnsupportedStartPending) { Events.Add({false, false, GetLastError()}); bUnsupportedStartPending = false; }
#endif
}
FAudioLoomLiveStatus FAudioLoomLiveBackend::GetStatus()
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    FScopeLock Lock(&Impl->StatusLock);
    FAudioLoomLiveStatus Copy = Impl->Status;
    Impl->Status.InputPeak = 0.f; // peak is "since the previous read"
    return Copy;
#else
    return FAudioLoomLiveStatus();
#endif
}
bool FAudioLoomLiveBackend::IsRunning() const
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    return Impl->Running.load();
#else
    return false;
#endif
}
FString FAudioLoomLiveBackend::GetLastError() const
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
    FScopeLock Lock(&Impl->ErrorLock);
    return Impl->Error;
#else
    return TEXT("Live input is not supported on this platform");
#endif
}
