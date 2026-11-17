// Copyright (c) 2026 AudioLoom Contributors.

/**
 * @file AudioLoomOscSubsystem.h
 * @brief Per-world OSC **UDP server** (incoming triggers) and **client** (outgoing `/state`).
 *
 * **Registry**: `RebuildComponentRegistry` maps cue bases (including unknown command feedback)
 * to `UAudioLoomComponent` weak pointers. Call `RebuildComponentRegistry` when levels or addresses change.
 *
 * Settings: `UAudioLoomOscSettings` (Project Settings). Requires Engine OSC plugin.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Containers/Ticker.h"
#include "AudioLoomOscSubsystem.generated.h"

class UAudioLoomComponent;
class UAudioLoomLiveInputComponent;
class UOSCClient;
struct FOSCMessage;

/** Pure command decision; no devices or sockets are touched. */
struct FAudioLoomOscDecision
{
	FString Result = TEXT("rejected");
	FString Detail;
	bool bAct = false;
	float Value = 0.f;
};

/**
 * World subsystem: OSC UDP listener + monitoring client; editor and PIE worlds each have an instance.
 */
UCLASS()
class AUDIOLOOM_API UAudioLoomOscSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Start the OSC server on the configured listen port. Returns true if listening. */
	UFUNCTION(BlueprintCallable, Category = "AudioLoom|OSC")
	bool StartListening();

	/** Numeric play trigger or an address-only message; shared with regression tests. */
	static bool IsPlayTrigger(const FOSCMessage& Message);
	static FAudioLoomOscDecision DecideCommand(const FString& Command, const FOSCMessage& Message, bool bLive);
	static TArray<uint8> MakeAcknowledgement(const FString& Base, const FString& Command, const FString& Result, bool bPlaying, const FString& Detail);

	/** Worker/start failures use the same monitor destination as state updates. */
	void SendPlayFailure(const FString& Base, const FString& Reason, bool bPlaying);

	/** Stop the OSC server. */
	UFUNCTION(BlueprintCallable, Category = "AudioLoom|OSC")
	void StopListening();

	/** Returns true if the OSC server is currently listening. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "AudioLoom|OSC")
	bool IsListening() const;

	/** Actual bound endpoint, not settings that may have changed since StartListening. */
	UFUNCTION(BlueprintPure, Category = "AudioLoom|OSC")
	FString GetListenIP() const;

	UFUNCTION(BlueprintPure, Category = "AudioLoom|OSC")
	int32 GetListenPort() const;

	/** Conservative all-interface UI probe. StartListening's exact configured-IP bind is authoritative. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "AudioLoom|OSC")
	static bool IsPortAvailable(int32 Port);

	/** Update monitoring client target (IP and port). Call after changing settings. */
	UFUNCTION(BlueprintCallable, Category = "AudioLoom|OSC")
	void UpdateMonitoringTarget();

	/** Rebuild component-to-address registry. Call when addresses change. */
	UFUNCTION(BlueprintCallable, Category = "AudioLoom|OSC")
	void RebuildComponentRegistry();

	/** Send a play/stop state message for a component. Address format: BaseAddress/state, value 1=playing 0=stopped. */
	void SendStateUpdate(UAudioLoomComponent* Component, bool bPlaying);

	/** Observed live-input state: BaseAddress/state, 1 = live (worker really started), 0 = not live. */
	void SendLiveStateUpdate(UAudioLoomLiveInputComponent* Component, bool bLive);

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(AudioLoomOSC, STATGROUP_Tickables); }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FSFOscAckLifecycle;
	TFunction<void(const FString&, const FString&, bool, const FString&)> AckObserver;
	TFunction<void(bool)> StateObserver;
#endif
	/** Exact-bind receiver dispatches validated messages here on the game thread. */
	void HandleOSCMessage(const FOSCMessage& Message, const FString& IPAddress, uint16 Port);

	TSharedPtr<class FSpatialFabricOSCReceiver> Receiver; // Exact-bind, game-thread UDP listener

	UPROPERTY()
	UOSCClient* OSCClient; // UDP sender (outgoing /state); recreated when Send IP/Port changes

	void PollCueEvents();
	void SendState(const FString& Base, bool bPlaying);
	FTSTicker::FDelegateHandle MonitoringTicker;
	void SendAcknowledgement(const FString& Base, const FString& Command, const FString& Result, bool bPlaying, const FString& Detail = FString());

	/** Cue bases, rather than recognized commands: unknown suffixes also need feedback. */
	TMap<FString, TArray<TWeakObjectPtr<UAudioLoomComponent>>> PlaybackBases;
	TMap<FString, TArray<TWeakObjectPtr<UAudioLoomLiveInputComponent>>> LiveBases;

	/** Reserved for future debounced registry rebuilds (currently timer drives refresh from panel). */
	FTimerHandle RebuildRegistryTimer;
	float RegistryElapsed = 0.f;
	FString MonitoringIP;
	int32 MonitoringPort = 0;
};
