// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#include "AudioInputDeviceEnumerator.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include "Windows/HideWindowsPlatformTypes.h"
#elif PLATFORM_MAC
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace
{
#if PLATFORM_WINDOWS
bool ReadEndpoint(IMMDevice* Device, FAudioOutputDeviceInfo& Info)
{
	LPWSTR Id = nullptr;
	if (FAILED(Device->GetId(&Id)) || !Id) return false;
	Info.DeviceId = FString(Id);
	CoTaskMemFree(Id);
	Info.FriendlyName = Info.DeviceId;
	IPropertyStore* Store = nullptr;
	if (SUCCEEDED(Device->OpenPropertyStore(STGM_READ, &Store)) && Store)
	{
		PROPVARIANT Name;
		PropVariantInit(&Name);
		if (SUCCEEDED(Store->GetValue(PKEY_Device_FriendlyName, &Name)) && Name.vt == VT_LPWSTR && Name.pwszVal) Info.FriendlyName = FString(Name.pwszVal);
		PropVariantClear(&Name);
		Store->Release();
	}
	IAudioClient* Client = nullptr;
	if (SUCCEEDED(Device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&Client)) && Client)
	{
		WAVEFORMATEX* Mix = nullptr;
		if (SUCCEEDED(Client->GetMixFormat(&Mix)) && Mix)
		{
			Info.NumChannels = Mix->nChannels;
			Info.SampleRate = int32(Mix->nSamplesPerSec);
			Info.BytesPerSample = Mix->wBitsPerSample / 8;
			CoTaskMemFree(Mix);
		}
		Client->Release();
	}
	Info.bIsValid = !Info.DeviceId.IsEmpty() && Info.NumChannels > 0;
	return Info.bIsValid;
}
#elif PLATFORM_MAC
FString CFToString(CFStringRef Ref)
{
	if (!Ref) return FString();
	const CFIndex Len = CFStringGetMaximumSizeForEncoding(CFStringGetLength(Ref), kCFStringEncodingUTF8) + 1;
	if (Len < 1 || Len > 32768) return FString();
	TArray<char> Tmp;
	Tmp.SetNumZeroed(int32(Len));
	return CFStringGetCString(Ref, Tmp.GetData(), Len, kCFStringEncodingUTF8) ? FString(UTF8_TO_TCHAR(Tmp.GetData())) : FString();
}
int32 InputChannels(AudioDeviceID Device)
{
	AudioObjectPropertyAddress Addr{kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeInput, kAudioObjectPropertyElementMain};
	UInt32 Size = 0;
	if (AudioObjectGetPropertyDataSize(Device, &Addr, 0, nullptr, &Size) != noErr || Size < sizeof(AudioBufferList) - sizeof(AudioBuffer) || Size > 65536) return 0;
	TArray<uint8> Buffer;
	Buffer.SetNumZeroed(int32(Size));
	if (AudioObjectGetPropertyData(Device, &Addr, 0, nullptr, &Size, Buffer.GetData()) != noErr) return 0;
	const AudioBufferList* List = reinterpret_cast<const AudioBufferList*>(Buffer.GetData());
	int32 Channels = 0;
	for (UInt32 i = 0; i < List->mNumberBuffers; ++i) Channels += int32(List->mBuffers[i].mNumberChannels);
	return Channels > 64 ? 0 : Channels;
}
bool ReadDevice(AudioDeviceID Device, FAudioOutputDeviceInfo& Info)
{
	Info.NumChannels = InputChannels(Device);
	if (Info.NumChannels < 1) return false;
	AudioObjectPropertyAddress Addr{kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
	CFStringRef Ref = nullptr;
	UInt32 Size = sizeof(Ref);
	if (AudioObjectGetPropertyData(Device, &Addr, 0, nullptr, &Size, &Ref) == noErr && Ref) { Info.DeviceId = CFToString(Ref); CFRelease(Ref); }
	if (Info.DeviceId.IsEmpty()) return false;
	Addr.mSelector = kAudioDevicePropertyDeviceNameCFString;
	Ref = nullptr; Size = sizeof(Ref);
	if (AudioObjectGetPropertyData(Device, &Addr, 0, nullptr, &Size, &Ref) == noErr && Ref) { Info.FriendlyName = CFToString(Ref); CFRelease(Ref); }
	if (Info.FriendlyName.IsEmpty()) Info.FriendlyName = Info.DeviceId;
	Addr.mSelector = kAudioDevicePropertyNominalSampleRate;
	Float64 Rate = 0; Size = sizeof(Rate);
	if (AudioObjectGetPropertyData(Device, &Addr, 0, nullptr, &Size, &Rate) == noErr && FMath::IsFinite(Rate) && Rate >= 8000 && Rate <= 384000) Info.SampleRate = int32(Rate);
	Info.BytesPerSample = 4;
	Info.bIsValid = true;
	return true;
}
#endif
}

TArray<FAudioOutputDeviceInfo> FAudioInputDeviceEnumerator::GetInputDevices()
{
	TArray<FAudioOutputDeviceInfo> Result;
#if PLATFORM_WINDOWS
	const HRESULT Init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(Init) && Init != RPC_E_CHANGED_MODE) return Result;
	const bool bUninitialize = SUCCEEDED(Init);
	IMMDeviceEnumerator* Enumerator = nullptr;
	if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&Enumerator)) && Enumerator)
	{
		IMMDeviceCollection* Devices = nullptr;
		UINT Count = 0;
		if (SUCCEEDED(Enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &Devices)) && Devices && SUCCEEDED(Devices->GetCount(&Count)) && Count <= 256)
		{
			for (UINT i = 0; i < Count; ++i)
			{
				IMMDevice* Device = nullptr;
				if (FAILED(Devices->Item(i, &Device)) || !Device) continue;
				FAudioOutputDeviceInfo Info;
				if (ReadEndpoint(Device, Info)) Result.Add(Info);
				Device->Release();
			}
		}
		if (Devices) Devices->Release();
		Enumerator->Release();
	}
	if (bUninitialize) CoUninitialize();
#elif PLATFORM_MAC
	AudioObjectPropertyAddress Addr{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
	UInt32 Size = 0;
	if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &Addr, 0, nullptr, &Size) != noErr || Size == 0 || Size > 256 * sizeof(AudioDeviceID) || Size % sizeof(AudioDeviceID)) return Result;
	TArray<AudioDeviceID> Devices;
	Devices.SetNum(int32(Size / sizeof(AudioDeviceID)));
	if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &Addr, 0, nullptr, &Size, Devices.GetData()) != noErr) return Result;
	for (AudioDeviceID Device : Devices)
	{
		FAudioOutputDeviceInfo Info;
		if (ReadDevice(Device, Info)) Result.Add(Info);
	}
#endif
	return Result;
}

FAudioOutputDeviceInfo FAudioInputDeviceEnumerator::GetDefaultInputDevice()
{
	FAudioOutputDeviceInfo Result;
#if PLATFORM_WINDOWS
	const HRESULT Init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(Init) && Init != RPC_E_CHANGED_MODE) return Result;
	const bool bUninitialize = SUCCEEDED(Init);
	IMMDeviceEnumerator* Enumerator = nullptr;
	if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&Enumerator)) && Enumerator)
	{
		IMMDevice* Device = nullptr;
		if (SUCCEEDED(Enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &Device)) && Device)
		{
			ReadEndpoint(Device, Result);
			Device->Release();
		}
		Enumerator->Release();
	}
	if (bUninitialize) CoUninitialize();
#elif PLATFORM_MAC
	AudioObjectPropertyAddress Addr{kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
	AudioDeviceID Device = kAudioObjectUnknown;
	UInt32 Size = sizeof(Device);
	if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &Addr, 0, nullptr, &Size, &Device) == noErr && Device != kAudioObjectUnknown) ReadDevice(Device, Result);
#endif
	return Result;
}

FAudioOutputDeviceInfo FAudioInputDeviceEnumerator::GetInputDeviceById(const FString& DeviceId)
{
	if (DeviceId.IsEmpty()) return GetDefaultInputDevice();
	for (const FAudioOutputDeviceInfo& Info : GetInputDevices())
		if (Info.DeviceId == DeviceId) return Info;
	return FAudioOutputDeviceInfo();
}
