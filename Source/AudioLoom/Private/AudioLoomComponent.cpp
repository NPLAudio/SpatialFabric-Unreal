// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#include "AudioLoomComponent.h"
#include "AudioLoomPlaybackBackend.h"
#include "AudioLoomOscSubsystem.h"
#include "AudioLoomBlueprintLibrary.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundWave.h"
#include "UObject/UnrealType.h"

namespace
{
FString Segment(const FString& Raw)
{
    FString Out;
    for (TCHAR C : Raw.TrimStartAndEnd())
        Out += FChar::IsAlnum(C) && C < 128 || C == TEXT('-') || C == TEXT('_') ? C : TEXT('_');
    while (Out.StartsWith(TEXT("_"))) Out.RightChopInline(1);
    while (Out.EndsWith(TEXT("_"))) Out.LeftChopInline(1);
    return Out.IsEmpty() ? TEXT("unnamed") : Out;
}
}
UAudioLoomComponent::UAudioLoomComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    bTickInEditor = true;
    AudioBackend = new FAudioLoomPlaybackBackend();
}
void UAudioLoomComponent::OnRegister() { Super::OnRegister(); RefreshOscRegistry(); }
void UAudioLoomComponent::OnUnregister() { Stop(); Super::OnUnregister(); RefreshOscRegistry(); }
void UAudioLoomComponent::BeginPlay()
{
    Super::BeginPlay();
    RefreshOscRegistry();
    if (bPlayOnBeginPlay) Play();
}
void UAudioLoomComponent::EndPlay(const EEndPlayReason::Type Reason) { Stop(); Super::EndPlay(Reason); }
void UAudioLoomComponent::BeginDestroy()
{
    if (AudioBackend) { AudioBackend->Stop(); delete AudioBackend; AudioBackend = nullptr; }
    Super::BeginDestroy();
}
void UAudioLoomComponent::UpdateTickEnabled()
{
    if (!bAutoReplay) { bReplayPending = false; ReplayCountdown = 0; }
    SetComponentTickEnabled(IsPlaying() || bWasPlaying || bStartPending || bReplayPending);
}
void UAudioLoomComponent::PollPlaybackEvents()
{
    if (!AudioBackend) return;
    TArray<FAudioLoomPlaybackEvent> Events;
    AudioBackend->DrainEvents(Events);
    for (const auto& Event : Events) HandlePlaybackEvent(Event);
}
void UAudioLoomComponent::HandlePlaybackEvent(const FAudioLoomPlaybackEvent& Event)
{
    bStartPending = false;
    if (!Event.Error.IsEmpty())
        if (UWorld* W = GetWorld())
            if (auto* Sub = W->GetSubsystem<UAudioLoomOscSubsystem>()) Sub->SendPlayFailure(GetOscAddress(), Event.Error, Event.bPlaying);
    if (bWasPlaying != Event.bPlaying || !Event.Error.IsEmpty())
    {
        bWasPlaying = Event.bPlaying;
        if (UWorld* W = GetWorld())
            if (auto* Sub = W->GetSubsystem<UAudioLoomOscSubsystem>()) Sub->SendStateUpdate(this, Event.bPlaying);
    }
    if (Event.bNaturalCompletion && !bStopping && bAutoReplay && !bLoop && SoundWave)
    {
        bReplayPending = true;
        const float Min = FMath::Max(0.f, ReplayDelayMin);
        ReplayCountdown = bRandomReplayDelay ? FMath::RandRange(Min, FMath::Max(Min, ReplayDelayMax)) : FMath::Max(0.f, ReplayDelaySeconds);
    }
}
void UAudioLoomComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta, Type, Function);
    ApplyPlaybackSettings();
    PollPlaybackEvents();
    if (bReplayPending && bAutoReplay && !bLoop && SoundWave)
    {
        ReplayCountdown -= Delta;
        if (ReplayCountdown <= 0) { bReplayPending = false; Play(); }
    }
    UpdateTickEnabled();
}
void UAudioLoomComponent::Play()
{
    Stop(); // cancel a previous replay and consume the previous stream's stop event
    auto FailStart = [this](const FString& Reason)
    {
        DecodeError = Reason;
        if (UWorld* W = GetWorld())
            if (auto* Sub = W->GetSubsystem<UAudioLoomOscSubsystem>()) Sub->SendPlayFailure(GetOscAddress(), Reason, IsPlaying());
    };
    if (!AudioBackend) { FailStart(TEXT("Playback backend unavailable")); return; }
    if (!SoundWave) { FailStart(TEXT("No SoundWave assigned")); return; }
    if (!PrepareAudio())
    {
        UE_LOG(LogTemp, Warning, TEXT("SpatialFabric: Cannot decode %s for direct playback"), *GetNameSafe(SoundWave));
        FailStart(GetLastPlaybackError());
        return;
    }
    StartedWave = SoundWave; StartedDevice = DeviceId; StartedChannel = OutputChannel;
    StartedBuffer = BufferSizeMs; bStartedExclusive = bUseExclusiveMode;
    bStartPending = true;
    AudioBackend->StartPrepared(DeviceId, PreparedPCM, CachedPCM.NumChannels, OutputChannel, bLoop, bUseExclusiveMode, BufferSizeMs, CachedPCM.SampleRate);
    // Initialization is asynchronous; worker events, not this request, drive monitoring.
    SetComponentTickEnabled(true);
}
bool UAudioLoomComponent::PrepareAudio()
{
    DecodeError.Empty();
    if (!SoundWave) { DecodeError = TEXT("No SoundWave assigned"); return false; }
    if (CachedWave.Get() != SoundWave || !CachedPCM.bSuccess)
    {
        CachedPCM = FAudioLoomPcmLoader::LoadFromSoundWave(SoundWave);
        PreparedPCM.Reset();
        if (CachedPCM.bSuccess) PreparedPCM = MakeShared<TArray<float>, ESPMode::ThreadSafe>(MoveTemp(CachedPCM.PCM));
        CachedWave = SoundWave;
    }
    if (!CachedPCM.bSuccess) DecodeError = TEXT("SoundWave decode failed; packaged assets require Force Inline and a supported codec/channel order. See Output Log.");
    return CachedPCM.bSuccess;
}
void UAudioLoomComponent::Stop()
{
    bReplayPending = false; ReplayCountdown = 0;
    bStopping = true;
    if (AudioBackend) AudioBackend->Stop();
    PollPlaybackEvents();
    bStopping = false;
    bWasPlaying = false;
    bStartPending = false;
    SetComponentTickEnabled(false);
}
bool UAudioLoomComponent::IsPlaying() const { return AudioBackend && AudioBackend->IsPlaying(); }
float UAudioLoomComponent::GetOutputLatencyMs() const { return AudioBackend ? AudioBackend->GetOutputLatencyMs() : 0; }
FString UAudioLoomComponent::GetLastPlaybackError() const { return !DecodeError.IsEmpty() ? DecodeError : (AudioBackend ? AudioBackend->GetLastError() : TEXT("Backend destroyed")); }
void UAudioLoomComponent::SetLoop(bool Value)
{
    bLoop = Value;
    if (AudioBackend) AudioBackend->SetLoop(Value);
    if (Value) bReplayPending = false;
    UpdateTickEnabled();
}
void UAudioLoomComponent::SetDeviceId(const FString& Value) { DeviceId = Value; ApplyPlaybackSettings(); }
void UAudioLoomComponent::SetOutputChannel(int32 Value) { OutputChannel = FMath::Max(0, Value); ApplyPlaybackSettings(); }
void UAudioLoomComponent::InvalidatePcmCache() { CachedPCM = {}; CachedWave.Reset(); PreparedPCM.Reset(); }
void UAudioLoomComponent::ApplyPlaybackSettings()
{
    if (AudioBackend) AudioBackend->SetLoop(bLoop);
    if (!bAutoReplay || !SoundWave || bLoop) bReplayPending = false;
    if (IsPlaying() && (StartedWave.Get() != SoundWave || StartedDevice != DeviceId || StartedChannel != OutputChannel
        || StartedBuffer != BufferSizeMs || bStartedExclusive != bUseExclusiveMode))
    { if (SoundWave) Play(); else Stop(); }
}
#if WITH_EDITOR
void UAudioLoomComponent::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    Super::PostEditChangeProperty(Event);
    if (Event.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UAudioLoomComponent, SoundWave)) InvalidatePcmCache();
    ApplyPlaybackSettings(); RefreshOscRegistry();
}
void UAudioLoomComponent::PostEditUndo()
{
    Super::PostEditUndo();
    InvalidatePcmCache(); ApplyPlaybackSettings(); RefreshOscRegistry();
}
#endif
AActor* UAudioLoomComponent::GetOscHierarchyRootForActor(AActor* Start)
{
    TSet<AActor*> Visited;
    AActor* Current = Start;
    while (Current && !Visited.Contains(Current))
    {
        Visited.Add(Current);
        AActor* Parent = Current->GetAttachParentActor();
        if (!Parent) Parent = Current->GetParentActor();
        if (!Parent) break;
        Current = Parent;
    }
    return Current;
}
FString UAudioLoomComponent::GetOscDefaultInstanceSegmentForHierarchyRoot(AActor* Root)
{ return Root ? Segment(Root->GetActorNameOrLabel()) : TEXT("unnamed"); }
FString UAudioLoomComponent::GetOscAddress() const
{
    const FString Override = UAudioLoomBlueprintLibrary::NormalizeOscAddress(OscAddress);
    if (!Override.IsEmpty()) return Override;
    AActor* Owner = GetOwner();
    if (!Owner) return TEXT("/audioloom/unnamed");
    AActor* Root = GetOscHierarchyRootForActor(Owner);
    TArray<UAudioLoomComponent*> Components;
    Owner->GetComponents(Components);
    const int32 Index = FMath::Max(0, Components.IndexOfByKey(this)) + 1;
    FString Base = TEXT("/audioloom/") + GetOscDefaultInstanceSegmentForHierarchyRoot(Root);
    // Preserve legacy root-owner paths. Descendant owners need their own identity.
    if (Owner != Root) Base += TEXT("/") + Segment(Owner->GetName());
    return Base + TEXT("/") + FString::FromInt(Index);
}
bool UAudioLoomComponent::SetOscAddress(const FString& Address)
{
    const FString Normalized = UAudioLoomBlueprintLibrary::NormalizeOscAddress(Address);
    if (!Address.TrimStartAndEnd().IsEmpty() && Normalized.IsEmpty()) return false;
    OscAddress = Normalized;
    RefreshOscRegistry();
    return true;
}
void UAudioLoomComponent::RefreshOscRegistry()
{
    if (UWorld* W = GetWorld())
        if (auto* Sub = W->GetSubsystem<UAudioLoomOscSubsystem>()) Sub->RebuildComponentRegistry();
}
