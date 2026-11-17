// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#include "AudioLoomLiveInputComponent.h"
#include "AudioLoomLiveBackend.h"
#include "AudioLoomComponent.h"
#include "AudioLoomBlueprintLibrary.h"
#include "AudioLoomOscSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UAudioLoomLiveInputComponent::UAudioLoomLiveInputComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false; // enabled only while a start is outstanding or live
    bTickInEditor = true;
    Backend = new FAudioLoomLiveBackend();
}
void UAudioLoomLiveInputComponent::OnRegister() { Super::OnRegister(); RefreshOscRegistry(); }
void UAudioLoomLiveInputComponent::OnUnregister() { StopLive(); Super::OnUnregister(); RefreshOscRegistry(); }
void UAudioLoomLiveInputComponent::EndPlay(const EEndPlayReason::Type Reason) { StopLive(); Super::EndPlay(Reason); }
void UAudioLoomLiveInputComponent::BeginDestroy()
{
    if (Backend) { Backend->Stop(); delete Backend; Backend = nullptr; }
    Super::BeginDestroy();
}
bool UAudioLoomLiveInputComponent::ParseGainArguments(const TArray<float>& Floats, const TArray<int32>& Ints, float& OutGain)
{
    OutGain = 0.f;
    if (Floats.Num()) { OutGain = Floats[0]; return FMath::IsFinite(OutGain) && OutGain >= 0.f && OutGain <= 4.f; }
    if (Ints.Num()) { OutGain = float(Ints[0]); return OutGain >= 0.f && OutGain <= 4.f; }
    return false;
}
bool UAudioLoomLiveInputComponent::StartLive()
{
    if (!Backend || !IsRegistered()) return false;
    if (InputChannel < 0 || OutputChannel < 0 || !FMath::IsFinite(Gain) || Gain < 0.f || Gain > 4.f) return false;
    bStartPending = true;
    Backend->Start(InputDeviceId, InputChannel, OutputDeviceId, OutputChannel, bUseExclusiveOutput, BufferSizeMs, Gain);
    SetComponentTickEnabled(true);
    PollBackend();
    return true; // a request, not an acknowledgement: IsLive follows the worker
}
void UAudioLoomLiveInputComponent::StopLive()
{
    if (!Backend) return;
    Backend->Stop();
    PollBackend();
    if (bLive) HandleEvent({false, false});
    bStartPending = false;
    InputPeak = 0.f; LatencyMs = 0.f;
    SetComponentTickEnabled(false);
}
bool UAudioLoomLiveInputComponent::SetGain(float InGain)
{
    if (!FMath::IsFinite(InGain) || InGain < 0.f || InGain > 4.f) return false;
    Gain = InGain;
    if (Backend) Backend->SetGain(InGain);
    return true;
}
void UAudioLoomLiveInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime, TickType, Function);
    PollBackend();
    // Keep ticking only while a worker may still report something.
    if (!bLive && !bStartPending && Backend && !Backend->IsRunning()) SetComponentTickEnabled(false);
}
void UAudioLoomLiveInputComponent::PollBackend()
{
    if (!Backend) return;
    TArray<FAudioLoomPlaybackEvent> Events;
    Backend->DrainEvents(Events);
    for (const FAudioLoomPlaybackEvent& Event : Events) HandleEvent(Event);
    const FAudioLoomLiveStatus Status = Backend->GetStatus();
    if (Status.bRunning)
    {
        InputPeak = FMath::Max(InputPeak * 0.9f, FMath::Clamp(Status.InputPeak, 0.f, 1.f));
        LatencyMs = Status.LatencyMs; Underruns = Status.Underruns; Overruns = Status.Overruns;
        ActualInputChannels = Status.InputChannels; ActualInputRate = Status.InputRate;
        ActualOutputChannels = Status.OutputChannels; ActualOutputRate = Status.OutputRate;
    }
}
void UAudioLoomLiveInputComponent::HandleEvent(const FAudioLoomPlaybackEvent& Event)
{
    bStartPending = false; // the worker has now reported (live or failed)
    if (bLive == Event.bPlaying) return;
    bLive = Event.bPlaying;
    if (UWorld* World = GetWorld())
        if (auto* Sub = World->GetSubsystem<UAudioLoomOscSubsystem>()) Sub->SendLiveStateUpdate(this, bLive);
    OnLiveStateChanged.Broadcast(bLive);
    OnLiveStateChangedNative.Broadcast(bLive);
}
FString UAudioLoomLiveInputComponent::GetLastLiveError() const { return Backend ? Backend->GetLastError() : FString(); }
FString UAudioLoomLiveInputComponent::GetOscAddress() const
{
    const FString Override = UAudioLoomBlueprintLibrary::NormalizeOscAddress(OscAddress);
    if (!Override.IsEmpty()) return Override;
    AActor* Owner = GetOwner();
    if (!Owner) return TEXT("/audioloom/unnamed/live/1");
    AActor* Root = UAudioLoomComponent::GetOscHierarchyRootForActor(Owner);
    TArray<UAudioLoomLiveInputComponent*> Components;
    Owner->GetComponents(Components);
    const int32 Index = FMath::Max(0, Components.IndexOfByKey(this)) + 1;
    FString Base = TEXT("/audioloom/") + UAudioLoomComponent::GetOscDefaultInstanceSegmentForHierarchyRoot(Root);
    if (Owner != Root) Base += TEXT("/") + UAudioLoomComponent::GetOscDefaultInstanceSegmentForHierarchyRoot(Owner);
    return Base + TEXT("/live/") + FString::FromInt(Index);
}
bool UAudioLoomLiveInputComponent::SetOscAddress(const FString& Address)
{
    const FString Normalized = UAudioLoomBlueprintLibrary::NormalizeOscAddress(Address);
    if (!Address.TrimStartAndEnd().IsEmpty() && Normalized.IsEmpty()) return false;
    OscAddress = Normalized;
    RefreshOscRegistry();
    return true;
}
void UAudioLoomLiveInputComponent::RefreshOscRegistry()
{
    if (UWorld* World = GetWorld())
        if (auto* Sub = World->GetSubsystem<UAudioLoomOscSubsystem>()) Sub->RebuildComponentRegistry();
}
