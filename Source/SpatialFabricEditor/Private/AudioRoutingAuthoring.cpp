// Copyright (c) 2026 SpatialFabric Contributors. MIT.
#include "AudioRoutingAuthoring.h"
#include "AudioLoomComponent.h"
#include "AudioOutputDeviceEnumerator.h"
#include "SceneObjectAuthoring.h"
#include "Editor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "Misc/PackageName.h"
namespace AudioRouting
{
bool ValidText(const FString& Text, bool Required)
{
    if (Text.Len() > MaxText || (Required && Text.IsEmpty())) return false;
    for (TCHAR C : Text) if (C < 32 || C == 127) return false;
    return true;
}
static bool Valid(const FInterface& D)
{
    return ValidText(D.Id, true) && ValidText(D.Name) && ValidText(D.LastSeen) &&
        D.Channels > 0 && D.Channels <= MaxChannels && D.SampleRate >= 0 && D.SampleRate <= 768000;
}
static bool BoundedJson(const FString& Json)
{
    return Json.Len() <= MaxJson && FTCHARToUTF8(*Json).Length() <= MaxJson;
}
static bool IntegerField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, int32& Out)
{
    double Value = 0;
    if (!Object->TryGetNumberField(Name, Value) || !FMath::IsFinite(Value) || Value < 0 || Value > MAX_int32 || Value != FMath::FloorToDouble(Value)) return false;
    Out = static_cast<int32>(Value); return true;
}
bool FCache::Load(const FString& Json)
{
    Devices.Reset();
    if (Json.IsEmpty()) return true;
    TSharedPtr<FJsonObject> Root;
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    int32 Version = 0;
    bool Good = BoundedJson(Json) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid() &&
        IntegerField(Root, TEXT("version"), Version) && Version == 1 && Root->TryGetArrayField(TEXT("devices"), Array) && Array->Num() <= MaxDevices;
    TArray<FInterface> Loaded;
    TSet<FString, FExactIdKeys> Ids;
    if (Good) for (const auto& Value : *Array)
    {
        const TSharedPtr<FJsonObject>* Obj = nullptr;
        FInterface D;
        if (!Value.IsValid() || !Value->TryGetObject(Obj) || !Obj->IsValid() ||
            !(*Obj)->TryGetStringField(TEXT("id"), D.Id) || !(*Obj)->TryGetStringField(TEXT("name"), D.Name) ||
            !(*Obj)->TryGetStringField(TEXT("seen"), D.LastSeen) || !IntegerField(*Obj, TEXT("channels"), D.Channels) ||
            !IntegerField(*Obj, TEXT("rate"), D.SampleRate) || !Valid(D) || Ids.Contains(D.Id)) { Good = false; break; }
        Ids.Add(D.Id); Loaded.Add(D);
    }
    if (Good) Devices = MoveTemp(Loaded); else Status = TEXT("Invalid/unsupported audio cache ignored.");
    return Good;
}
bool FCache::Accept(const TArray<FAudioOutputDeviceInfo>& Fresh, const FString& Error, TFunctionRef<bool(const FString&)> Save)
{
    for (auto& D : Devices) D.Online = false;
    Status = Error;
    if (!Error.IsEmpty()) return false;
    if (Fresh.Num() > MaxDevices) { Status = TEXT("Inventory exceeds device limit."); return false; }
    TArray<FInterface> Merged = Devices;
    TSet<FString, FExactIdKeys> Seen;
    for (const auto& F : Fresh)
    {
        FInterface D; D.Id = F.DeviceId; D.Name = F.FriendlyName; D.Channels = F.NumChannels;
        D.SampleRate = F.SampleRate; D.LastSeen = FDateTime::UtcNow().ToIso8601(); D.Online = true;
        if (!F.bIsValid || !Valid(D) || Seen.Contains(D.Id)) { Status = TEXT("Invalid inventory or duplicate exact ID; history preserved."); return false; }
        Seen.Add(D.Id);
        auto* Old = Merged.FindByPredicate([&](const FInterface& Item) { return SameId(Item.Id,D.Id); });
        if (Old) *Old = D; else Merged.Add(D);
    }
    if (Merged.Num() > MaxDevices) { Status = TEXT("Interface history full; no offline interfaces evicted."); return false; }
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("version"), 1);
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const auto& D : Merged)
    {
        auto O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("id"), D.Id); O->SetStringField(TEXT("name"), D.Name); O->SetStringField(TEXT("seen"), D.LastSeen);
        O->SetNumberField(TEXT("channels"), D.Channels); O->SetNumberField(TEXT("rate"), D.SampleRate);
        Values.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("devices"), Values);
    FString Json; FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
    if (!BoundedJson(Json) || !Save(Json)) { Status = TEXT("Cache save failed; history preserved."); return false; }
    Devices = MoveTemp(Merged); Status = TEXT("Last explicit refresh succeeded. Checks are configured, not live."); return true;
}
FString FCache::Path()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Config"), FString::Printf(TEXT("SpatialFabricAudioInterfaces-%08x-v1.json"), GetTypeHash(FString(FPlatformProcess::UserName()))));
}
void FCache::LoadDisk()
{
    const int64 Size = IFileManager::Get().FileSize(*Path());
    if (Size > MaxJson) { Status = TEXT("Oversized audio cache ignored."); return; }
    FString Json; if (Size >= 0 && FFileHelper::LoadFileToString(Json, *Path())) Load(Json);
}
bool FCache::Refresh()
{
    FString Error;
    const auto Fresh = FAudioOutputDeviceEnumerator::GetOutputDevices(&Error);
    return Accept(Fresh, Error, [](const FString& Json)
    {
        const FString File = Path(), Temp = File + TEXT(".tmp");
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        if (!FFileHelper::SaveStringToFile(Json, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
        return IFileManager::Get().Move(*File, *Temp, true, true) == COPY_OK;
    });
}
FRange Visible(float Offset, float Extent, float Stride, int32 Count)
{
    if (Stride <= 0 || Extent <= 0 || Count <= 0) return {};
    return { FMath::Clamp(FMath::FloorToInt(FMath::Max(0.f, Offset) / Stride), 0, Count),
        FMath::Clamp(FMath::CeilToInt((FMath::Max(0.f, Offset) + Extent) / Stride), 0, Count) };
}
void FMatrix::Capture(const TArray<UAudioLoomComponent*>& Components, const TArray<FInterface>& Devices)
{
    Rows.Reset(); Groups.Reset(); Columns = 0;
    TMap<FString, TSet<int32>, FDefaultSetAllocator, TExactIdMapKeys<TSet<int32>>> Authored;
    TSet<FString, FExactIdKeys> Added;
    for (auto* C : Components) if (IsValid(C) && IsValid(C->GetOwner()))
    {
        FRow R; R.Component = C; R.Name = C->GetOwner()->GetActorLabel() + TEXT(" / ") + C->GetName(); R.Id = C->DeviceId; R.Channel = C->OutputChannel;
        Rows.Add(R); Authored.FindOrAdd(R.Id).Add(R.Channel);
    }
    auto Add = [&](const FString& Id, const FInterface* D)
    {
        if (Added.Contains(Id)) return;
        Added.Add(Id);
        FGroup G; G.Id = Id; G.Start = Columns; G.Capacity = D ? D->Channels : 0;
        G.Title = Id.IsEmpty() ? TEXT("Dynamic OS default (unknown capacity)") : D ? D->Name + TEXT(" [") + Id + (D->Online ? TEXT("]") : TEXT("] OFFLINE / cached")) : Id + TEXT(" UNKNOWN / offline");
        if (const auto* Channels = Authored.Find(Id)) for (int32 Ch : *Channels) if (Ch > G.Capacity) G.Stored.Add(Ch);
        G.Stored.Sort(); Columns += G.Count(); Groups.Add(MoveTemp(G));
    };
    Add(TEXT(""), nullptr);
    for (const auto& D : Devices) Add(D.Id, &D);
    for (const auto& R : Rows) Add(R.Id, nullptr);
}
const FGroup* FMatrix::Column(int32 Index, int32& Channel) const
{
    for (const auto& G : Groups) if (Index >= G.Start && Index < G.Start + G.Count()) { Channel = G.ChannelAt(Index - G.Start); return &G; }
    return nullptr;
}
bool CanAssign(const UAudioLoomComponent* C, FText& Reason)
{
    if (!IsValid(C) || !SceneObjectAuthoring::CanEdit(C->GetOwner(), SceneObjectAuthoring::EditorWorld(), Reason)) return false;
    if (GEditor->IsPlaySessionInProgress() || GEditor->IsPlayingSessionInEditor() || C->IsTemplate() ||
        C->HasAnyFlags(RF_Transient) || !SceneObjectAuthoring::CanEditComponent(C) || C->CreationMethod == EComponentCreationMethod::SimpleConstructionScript ||
        C->CreationMethod == EComponentCreationMethod::UserConstructionScript || !C->IsRouteEditStopped())
    { Reason = FText::FromString(TEXT("Requires stopped editable instance (no active/start/replay, PIE, inherited or construction-script component).")); return false; }
    FString File;
    if (FPackageName::DoesPackageExist(C->GetOutermost()->GetName(), &File) && IFileManager::Get().IsReadOnly(*File))
    { Reason = FText::FromString(TEXT("Level package is read-only.")); return false; }
    return true;
}
bool Assign(UAudioLoomComponent* C, const FString& Id, int32 Channel, FText& Reason)
{
    Reason = FText::GetEmpty();
    if (!CanAssign(C, Reason)) return false;
    if (!ValidText(Id) || Channel < 0) { Reason = FText::FromString(TEXT("Invalid route ID/channel.")); return false; }
    if (SameId(C->DeviceId,Id) && C->OutputChannel == Channel) return false;
    const FScopedTransaction Transaction(NSLOCTEXT("AudioRouting", "Assign", "Assign audio output route"));
    C->Modify();
    // Direct reflected fields, not transport setters. One notification after the complete pair is set.
    FProperty* Property = FindFProperty<FProperty>(C->GetClass(), !SameId(C->DeviceId,Id) ? GET_MEMBER_NAME_CHECKED(UAudioLoomComponent, DeviceId) : GET_MEMBER_NAME_CHECKED(UAudioLoomComponent, OutputChannel));
    C->PreEditChange(Property); C->DeviceId = Id; C->OutputChannel = Channel;
    FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet); static_cast<UObject*>(C)->PostEditChangeProperty(Event);
    C->MarkPackageDirty(); return true;
}
}
