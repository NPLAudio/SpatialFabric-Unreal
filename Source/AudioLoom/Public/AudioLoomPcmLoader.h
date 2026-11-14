// Copyright (c) 2026 AudioLoom Contributors.

/**
 * @file AudioLoomPcmLoader.h
 * @brief WAV parse + `USoundWave` extraction → **interleaved float** PCM for the backend.
 *
 * Editor builds use imported PCM. Cooked non-streaming (Force Inline) assets use Unreal's
 * registered codec factory. Streaming and unsupported channel orderings fail explicitly.
 */

#pragma once

#include "CoreMinimal.h"

class USoundWave;

/**
 * Result of loading PCM: interleaved floats, **NumChannels** interleaved (L,R,…).
 */
struct AUDIOLOOM_API FAudioLoomPcmResult
{
	TArray<float> PCM;       // interleaved samples; length = frameCount * NumChannels
	int32 NumChannels = 0;   // 1 = mono, 2 = stereo, etc.
	int32 SampleRate = 48000; // Hz from WAV fmt or imported asset metadata
	bool bSuccess = false;    // false if parse failed or no PCM could be obtained
};

/**
 * Loads PCM float data from WAV files or USoundWave assets.
 */
class AUDIOLOOM_API FAudioLoomPcmLoader
{
public:
	/**
	 * Load PCM from a WAV file path.
	 * Bounded RIFF parser for PCM16/24/32 and IEEE float32; validates whole frames.
	 */
	static FAudioLoomPcmResult LoadFromFile(const FString& FilePath);
	static FAudioLoomPcmResult LoadFromMemory(const TArray<uint8>& Bytes);

	/**
	 * Load PCM from a USoundWave asset.
	 * Imported PCM in editor, or native decoded/inline cooked data at runtime.
	 */
	static FAudioLoomPcmResult LoadFromSoundWave(USoundWave* SoundWave);
};
