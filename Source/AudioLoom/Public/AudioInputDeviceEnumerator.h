// Copyright (c) 2026 AudioLoom Contributors. MIT License.

/**
 * @file AudioInputDeviceEnumerator.h
 * @brief Lists capture (microphone / line in) devices for `UAudioLoomLiveInputComponent`.
 *
 * Inventory only: it opens no stream and requests no microphone access. DeviceId strings use the same
 * conventions as outputs (Windows: IMMDevice::GetId, macOS: Core Audio device UID).
 */

#pragma once

#include "CoreMinimal.h"
#include "AudioOutputDeviceInfo.h"

class AUDIOLOOM_API FAudioInputDeviceEnumerator
{
public:
	/** All active capture endpoints. Call from the game thread; may block briefly. */
	static TArray<FAudioOutputDeviceInfo> GetInputDevices();

	/** The system default capture device, or bIsValid=false when none exists. */
	static FAudioOutputDeviceInfo GetDefaultInputDevice();

	/** Exact lookup; an empty ID resolves the default. Never falls back for a non-empty missing ID. */
	static FAudioOutputDeviceInfo GetInputDeviceById(const FString& DeviceId);
};
