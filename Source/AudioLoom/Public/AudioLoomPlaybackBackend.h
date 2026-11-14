// Copyright (c) 2026 AudioLoom Contributors.

/**
 * @file AudioLoomPlaybackBackend.h
 * @brief PIMPL facade for platform-specific output (`FAudioLoomPlaybackBackendImpl` in .cpp).
 *
 * **WASAPI** on Windows and **Core Audio** on macOS. See `AudioLoomPlaybackBackend.cpp` for device
 * open, format, render loop, and channel routing.
 */

#pragma once

#include "CoreMinimal.h"
#include "AudioOutputDeviceInfo.h"

class FAudioLoomPlaybackBackendImpl;

struct FAudioLoomPlaybackEvent
{
	bool bPlaying = false;
	bool bNaturalCompletion = false;
};

/**
 * Audio playback backend (Windows: WASAPI; macOS: Core Audio `AudioDeviceIOProc`).
 * Interleaved source-rate float PCM; maps channels and resamples at the negotiated device rate.
 */
class AUDIOLOOM_API FAudioLoomPlaybackBackend
{
public:
	FAudioLoomPlaybackBackend();
	~FAudioLoomPlaybackBackend();

	/**
	 * Initialize asynchronously. PCM is interleaved float at SourceSampleRate.
	 * bLoop: repeat until Stop(). bExclusive / BufferSizeMs: Windows low-latency path only.
	 * Thread: spawns worker; Stop() joins. Safe to call only from game thread.
	 */
	void Start(const FString& DeviceId, const TArray<float>& PCMData, int32 SourceChannels, int32 OutputChannel, bool bLoop = false, bool bExclusive = false, int32 InBufferSizeMs = 0, int32 SourceSampleRate = 48000);

	/** Zero-copy playback of immutable prepared PCM; ownership lasts through worker shutdown. */
	void StartPrepared(const FString& DeviceId, TSharedPtr<const TArray<float>, ESPMode::ThreadSafe> PCMData, int32 SourceChannels, int32 OutputChannel, bool bLoop, bool bExclusive, int32 BufferSizeMs, int32 SourceSampleRate);

	/** Stop playback and release resources. */
	void Stop();
	void SetLoop(bool bLoop);
	void DrainEvents(TArray<FAudioLoomPlaybackEvent>& Events);
	FString GetLastError() const;

	/** Whether currently playing. */
	bool IsPlaying() const;

	/**
	 * Estimated output path latency (ms) from the last started stream, or 0 if not playing / unknown.
	 * Safe to call from game thread; value is set on the playback thread after the device stream initializes.
	 */
	float GetOutputLatencyMs() const;

private:
#if PLATFORM_WINDOWS || PLATFORM_MAC
	TUniquePtr<FAudioLoomPlaybackBackendImpl> Impl; // platform impl defined only on supported playback platforms
#endif
};
