// Copyright (c) 2026 AudioLoom Contributors. MIT License.

/**
 * @file AudioLoomLiveBackend.h
 * @brief PIMPL facade that routes a live capture device (mic / line in) to an output device.
 *
 * Windows: WASAPI capture + render. macOS: Core Audio `AudioDeviceIOProc` pair. A start request is
 * not an acknowledgement: `DrainEvents` reports the real worker start/end. Configured device IDs never
 * fall back to defaults and exclusive output that cannot be granted fails instead of degrading.
 */

#pragma once

#include "CoreMinimal.h"
#include "AudioLoomPlaybackBackend.h"

class FAudioLoomLiveBackendImpl;

/** Observed snapshot of the running bridge. All fields are zero while stopped. */
struct FAudioLoomLiveStatus
{
	bool bRunning = false;
	int32 InputChannels = 0, InputRate = 0, OutputChannels = 0, OutputRate = 0;
	float InputPeak = 0.f;       // 0..1 since the previous status read
	float LatencyMs = 0.f;       // buffered input awaiting output (not full round-trip hardware latency)
	uint64 CapturedFrames = 0, RenderedFrames = 0, Underruns = 0, Overruns = 0;
};

class AUDIOLOOM_API FAudioLoomLiveBackend
{
public:
	FAudioLoomLiveBackend();
	~FAudioLoomLiveBackend();

	/**
	 * Submit a start request; a worker thread opens both endpoints. InputChannel: 0 = all inputs,
	 * 1..N = that input. OutputChannel: 0 = corresponding channels, 1..N = exactly one output.
	 * bExclusive and BufferSizeMs apply to the output endpoint; capture is always shared.
	 * Game thread only. Invalid arguments enqueue a failed (not-live) event and set the error.
	 */
	void Start(const FString& InputDeviceId, int32 InputChannel, const FString& OutputDeviceId, int32 OutputChannel, bool bExclusive, int32 BufferSizeMs, float Gain);
	void Stop();
	bool SetGain(float Gain);
	void DrainEvents(TArray<FAudioLoomPlaybackEvent>& Events);
	FAudioLoomLiveStatus GetStatus();
	FString GetLastError() const;
	bool IsRunning() const;

private:
#if PLATFORM_WINDOWS || PLATFORM_MAC
	TUniquePtr<FAudioLoomLiveBackendImpl> Impl;
#endif
};
