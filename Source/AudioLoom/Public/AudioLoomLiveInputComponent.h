// Copyright (c) 2026 AudioLoom Contributors.

/**
 * @file AudioLoomLiveInputComponent.h
 * @brief Routes a live capture device (microphone / line in) to a chosen output device and channel.
 *
 * Uses the same direct-device path as `UAudioLoomComponent` (WASAPI on Windows, Core Audio on macOS), not
 * Unreal's AudioComponent graph or audio capture components. Nothing opens a microphone until `StartLive`
 * is called explicitly (the OS may prompt for permission). `IsLive` reflects the worker's observed state,
 * never a mere start request. Configured device IDs never fall back to defaults.
 *
 * Monitoring a microphone through nearby speakers causes acoustic feedback: use headphones or low gain.
 * OSC (same server as playback): `<base>/play`, `<base>/stop`, `<base>/gain` (float 0..4) and observed
 * `<base>/state` (1 = live).
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioLoomLiveInputComponent.generated.h"

class AActor;
struct FAudioLoomPlaybackEvent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAudioLoomLiveStateChanged, bool, bLive);
DECLARE_MULTICAST_DELEGATE_OneParam(FAudioLoomLiveStateChangedNative, bool);

UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent, DisplayName = "Audio Loom Live Input"))
class AUDIOLOOM_API UAudioLoomLiveInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAudioLoomLiveInputComponent();

	/** Exact capture endpoint ID. Empty = system default input. A missing configured ID fails (no fallback). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioLoom|Live Input")
	FString InputDeviceId;

	/** 0 = all input channels. 1..N = only that (1-based) input channel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioLoom|Live Input", meta = (ClampMin = "0", UIMin = "0"))
	int32 InputChannel = 0;

	/** Exact render endpoint ID. Empty = system default output. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioLoom|Live Input")
	FString OutputDeviceId;

	/** 0 = corresponding channels (a single selected input lands on output 1). 1..N = exactly that output. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioLoom|Live Input", meta = (ClampMin = "0", UIMin = "0"))
	int32 OutputChannel = 0;

	/** Windows: request exclusive output. If it cannot be granted the start FAILS (no shared fallback). Capture is always shared. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioLoom|Live Input")
	bool bUseExclusiveOutput = false;

	/** Output buffer request in ms (0 = driver default). Bridge latency is about twice this (40 ms default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioLoom|Live Input", meta = (ClampMin = "0", ClampMax = "100", UIMin = "0", UIMax = "50"))
	int32 BufferSizeMs = 0;

	/** Linear gain 0..4; the output is always clamped to full scale. Mind feedback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioLoom|Live Input", meta = (ClampMin = "0.0", ClampMax = "4.0", UIMin = "0.0", UIMax = "2.0"))
	float Gain = 1.0f;

	/** Base OSC address. Empty = /audioloom/<hierarchy root>/live/<index>. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioLoom|OSC", meta = (DisplayName = "OSC Address"))
	FString OscAddress;

	/** Fires when the OBSERVED live state changes (true only after the native worker really started). */
	UPROPERTY(BlueprintAssignable, Category = "AudioLoom|Live Input")
	FAudioLoomLiveStateChanged OnLiveStateChanged;

	/** C++ equivalent of OnLiveStateChanged (supports lambdas). */
	FAudioLoomLiveStateChangedNative OnLiveStateChangedNative;

	/** Submit a start request. Returns false only if the request itself was invalid; IsLive follows the worker. */
	UFUNCTION(BlueprintCallable, Category = "AudioLoom|Live Input")
	bool StartLive();

	UFUNCTION(BlueprintCallable, Category = "AudioLoom|Live Input")
	void StopLive();

	/** Validated 0..4. Applies immediately to a running stream. */
	UFUNCTION(BlueprintCallable, Category = "AudioLoom|Live Input")
	bool SetGain(float InGain);

	UFUNCTION(BlueprintPure, Category = "AudioLoom|Live Input")
	bool IsLive() const { return bLive; }

	/** Input peak 0..1, smoothed; observed while live. */
	UFUNCTION(BlueprintPure, Category = "AudioLoom|Diagnostics")
	float GetInputPeak() const { return InputPeak; }

	/** Buffered input awaiting output in ms. Not full round-trip hardware latency. */
	UFUNCTION(BlueprintPure, Category = "AudioLoom|Diagnostics")
	float GetLiveLatencyMs() const { return LatencyMs; }

	UFUNCTION(BlueprintPure, Category = "AudioLoom|Diagnostics")
	int32 GetUnderruns() const { return int32(FMath::Min<uint64>(Underruns, MAX_int32)); }

	UFUNCTION(BlueprintPure, Category = "AudioLoom|Diagnostics")
	int32 GetOverruns() const { return int32(FMath::Min<uint64>(Overruns, MAX_int32)); }

	UFUNCTION(BlueprintPure, Category = "AudioLoom|Diagnostics")
	FString GetLastLiveError() const;

	/** Effective OSC base (resolved default when OscAddress is empty). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "AudioLoom|OSC", meta = (DisplayName = "Get OSC Address"))
	FString GetOscAddress() const;

	UFUNCTION(BlueprintCallable, Category = "AudioLoom|OSC", meta = (DisplayName = "Set OSC Address"))
	bool SetOscAddress(const FString& InAddress);

	/** Observed worker details (valid while live). */
	int32 GetActualInputChannels() const { return ActualInputChannels; }
	int32 GetActualInputRate() const { return ActualInputRate; }
	int32 GetActualOutputChannels() const { return ActualOutputChannels; }
	int32 GetActualOutputRate() const { return ActualOutputRate; }

	/** Gain trigger semantics shared with the OSC subsystem and tests: first float anywhere, else first int; 0..4 only. */
	static bool ParseGainArguments(const TArray<float>& Floats, const TArray<int32>& Ints, float& OutGain);

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void BeginDestroy() override;

private:
	friend class FSFLiveInputState;
	friend class FSFOscAckLifecycle;
	friend class UAudioLoomOscSubsystem;
	void PollBackend();
	void HandleEvent(const FAudioLoomPlaybackEvent& Event);
	void RefreshOscRegistry();

	class FAudioLoomLiveBackend* Backend = nullptr;
	bool bLive = false;
	bool bTearingDown = false; // state delegates must not restart capture during teardown
	bool bStartPending = false; // a start request is outstanding; keep polling until the worker reports
	float InputPeak = 0.f, LatencyMs = 0.f;
	uint64 Underruns = 0, Overruns = 0;
	int32 ActualInputChannels = 0, ActualInputRate = 0, ActualOutputChannels = 0, ActualOutputRate = 0;
};
