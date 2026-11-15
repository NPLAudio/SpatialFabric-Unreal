// Copyright (c) 2026 SpatialFabric Contributors. MIT License.
#include "SpatialSceneQuery.h"
#include "SpatialSceneQueryLibrary.h"
#include "SpatialFabricSceneQueryBridge.h"
#include "SpatialFabricManagerActor.h"
#include "SpatialFabricSettings.h"
#include "SpatialStageVolume.h"
#include "SpatialObjectRegistry.h"
#include "SpatialOSCServerComponent.h"
#include "SpatialOSCClientComponent.h"
#include "AudioLoomComponent.h"
#include "AudioLoomOscSettings.h"
#include "AudioLoomOscSubsystem.h"
#include "Components/BoxComponent.h"
#include "Sound/SoundWave.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "OSCManager.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

namespace SpatialSceneQuery
{
namespace
{
using JObject = TSharedPtr<FJsonObject>;
JObject Object() { return MakeShared<FJsonObject>(); }
void Number(const JObject& O, const FString& Key, double Value)
{
    if (FMath::IsFinite(Value)) O->SetNumberField(Key, Value); else O->SetField(Key, MakeShared<FJsonValueNull>());
}
TArray<TSharedPtr<FJsonValue>> Vector(FVector V)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    for (double X : {V.X, V.Y, V.Z}) Values.Add(FMath::IsFinite(X) ? TSharedPtr<FJsonValue>(MakeShared<FJsonValueNumber>(X)) : TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>()));
    return Values;
}
JObject Transform(AActor* Actor)
{
    auto O = Object(); O->SetArrayField(TEXT("position_cm"), Vector(Actor->GetActorLocation()));
    const auto R = Actor->GetActorRotation(); O->SetArrayField(TEXT("rotation_pitch_yaw_roll_deg"), Vector(FVector(R.Pitch,R.Yaw,R.Roll)));
    O->SetArrayField(TEXT("scale"), Vector(Actor->GetActorScale3D())); return O;
}
FString Label(AActor* Actor)
{
    // GetActorLabel() can synthesize/cache a new label and broadcast a change.
    // Discovery must not do that, even for an actor with no authored label yet.
    return Actor->GetActorNameOrLabel();
}
JObject Record(const FString& Kind, UObject* Source, AActor* Owner)
{
    auto O = Object(); O->SetStringField(TEXT("kind"), Kind); O->SetStringField(TEXT("key"), Source->GetPathName());
    O->SetStringField(TEXT("name"), Source->GetName()); O->SetStringField(TEXT("class"), Source->GetClass()->GetPathName());
    if (Owner) { O->SetStringField(TEXT("actor_path"),Owner->GetPathName()); O->SetStringField(TEXT("actor_label"),Label(Owner)); O->SetObjectField(TEXT("world_transform"),Transform(Owner)); }
    return O;
}
FString Serialize(const JObject& O)
{
    FString Json; auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
    FJsonSerializer::Serialize(O.ToSharedRef(), Writer); return Json;
}
JObject Error(const FString& Code)
{
    auto O=Object(); O->SetStringField(TEXT("schema"),TEXT("org.nplaudio.spatialfabric.scene")); O->SetNumberField(TEXT("schema_version"),1); O->SetStringField(TEXT("error"),Code); return O;
}
JObject Adapter(const FSpatialAdapterConfig& C, uint8 Type)
{
    auto O=Object(); O->SetStringField(TEXT("protocol"),Type==0?TEXT("ADM"):TEXT("Custom"));
    O->SetStringField(TEXT("target_ip"),C.TargetIP); O->SetNumberField(TEXT("target_port"),C.TargetPort);
    O->SetBoolField(TEXT("enabled"),C.bEnabled); Number(O,TEXT("send_rate_hz"),C.SendRateHz); O->SetBoolField(TEXT("send_only_on_change"),C.bSendOnlyOnChange);
    O->SetNumberField(TEXT("adm_coordinate_mode"),static_cast<uint8>(C.ADMCoordinateMode));
    O->SetStringField(TEXT("custom_address_template"),C.CustomAddressTemplate); O->SetStringField(TEXT("custom_arg_template"),C.CustomArgTemplate);
    O->SetNumberField(TEXT("custom_coordinate_mode"),static_cast<uint8>(C.CustomCoordinateMode)); O->SetNumberField(TEXT("custom_send_mode"),static_cast<uint8>(C.CustomSendMode));
    auto PositionRange=Vector(FVector(C.CustomPositionRange.X,C.CustomPositionRange.Y,0)); PositionRange.Pop(); O->SetArrayField(TEXT("custom_position_range"),PositionRange);
    auto GainRange=Vector(FVector(C.CustomGainRange.X,C.CustomGainRange.Y,0)); GainRange.Pop(); O->SetArrayField(TEXT("custom_gain_range"),GainRange);
    auto WidthRange=Vector(FVector(C.CustomWidthRange.X,C.CustomWidthRange.Y,0)); WidthRange.Pop(); O->SetArrayField(TEXT("custom_width_range"),WidthRange);
    return O;
}
}

FString BuildJson(UWorld* World, int32 Offset, int32 Limit)
{
    if (!IsInGameThread() || !IsValid(World)) return Serialize(Error(TEXT("invalid_world_or_thread")));
    if (Offset<0 || Limit<1 || Limit>MaxPageSize) return Serialize(Error(TEXT("invalid_page")));
    TArray<JObject> Records;
    constexpr int32 MaxRecords=16384;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor=*It; if (!IsValid(Actor)) continue;
        if (auto* Stage=Cast<ASpatialStageVolume>(Actor))
        {
            auto O=Record(TEXT("stage"),Stage,Stage);
            O->SetArrayField(TEXT("physical_dimensions_m"),Vector(FVector(Stage->PhysicalWidthMeters,Stage->PhysicalDepthMeters,Stage->PhysicalHeightMeters)));
            if (Stage->StageBox) O->SetArrayField(TEXT("box_unscaled_extent_cm"),Vector(Stage->StageBox->GetUnscaledBoxExtent()));
            O->SetBoolField(TEXT("flip_front"),Stage->bFlipX); O->SetBoolField(TEXT("flip_right"),Stage->bFlipY); O->SetBoolField(TEXT("flip_up"),Stage->bFlipZ);
            O->SetNumberField(TEXT("listener_mode"),static_cast<uint8>(Stage->ListenerMode)); O->SetNumberField(TEXT("listener_player_index"),Stage->ListenerPlayerIndex);
            O->SetStringField(TEXT("listener_actor_path"),Stage->ListenerActor.ToSoftObjectPath().ToString()); O->SetBoolField(TEXT("listener_resolved"),Stage->HasListener());
            O->SetArrayField(TEXT("last_listener_position_cm"),Vector(Stage->GetListenerWorldPosition()));
            const auto R=Stage->GetListenerRotation(); O->SetArrayField(TEXT("last_listener_pitch_yaw_roll_deg"),Vector(FVector(R.Pitch,R.Yaw,R.Roll)));
            Records.Add(O);
            if (AActor* Listener=Stage->ListenerActor.Get(); IsValid(Listener) && Listener->GetWorld()==World)
            {
                auto L=Record(TEXT("listener"),Listener,Listener); L->SetStringField(TEXT("stage_path"),Stage->GetPathName());
                L->SetStringField(TEXT("key"),Stage->GetPathName()+TEXT("/listener")); Records.Add(L);
            }
        }
        if (auto* Manager=Cast<ASpatialFabricManagerActor>(Actor))
        {
            auto O=Record(TEXT("manager"),Manager,Manager); O->SetStringField(TEXT("stage_path"),Manager->StageVolume.ToSoftObjectPath().ToString());
            O->SetBoolField(TEXT("networking_allowed"),Manager->IsNetworkingAllowed()); O->SetBoolField(TEXT("hide_stage_in_pie"),Manager->bHideStageInPIE);
            O->SetStringField(TEXT("custom_target_ip"),Manager->CustomTargetIP); O->SetNumberField(TEXT("custom_target_port"),Manager->CustomTargetPort);
            TArray<TSharedPtr<FJsonValue>> Adapters;
            for (const auto& Pair:Manager->AdapterConfigs) Adapters.Add(MakeShared<FJsonValueObject>(Adapter(Pair.Value,Pair.Key)));
            O->SetArrayField(TEXT("adapters"),Adapters); Records.Add(O);
            const auto Snapshot=Manager->RegistryComponent ? Manager->RegistryComponent->BuildSnapshot(Manager->ObjectBindings,Manager->StageVolume.Get()) : FSpatialFrameSnapshot();
            for (int32 Index=0; Index<Manager->ObjectBindings.Num(); ++Index)
            {
                const auto& B=Manager->ObjectBindings[Index]; auto Item=Object();
                Item->SetStringField(TEXT("kind"),TEXT("spatial_binding")); Item->SetStringField(TEXT("key"),Manager->GetPathName()+TEXT("/bindings/")+FString::FromInt(Index));
                Item->SetStringField(TEXT("manager_path"),Manager->GetPathName()); Item->SetStringField(TEXT("label"),B.Label); Item->SetStringField(TEXT("actor_path"),B.TargetActor.ToSoftObjectPath().ToString());
                Item->SetStringField(TEXT("cached_actor_label"),B.CachedActorLabel); Item->SetNumberField(TEXT("binding_index"),Index);
                Item->SetNumberField(TEXT("default_object_id"),B.DefaultObjectID);
                Item->SetBoolField(TEXT("enabled"),B.bEnabled); Item->SetBoolField(TEXT("mute"),B.bMuted); Number(Item,TEXT("gain"),B.GainLinear); Number(Item,TEXT("width"),B.Width01);
                Item->SetBoolField(TEXT("adm_send_gain"),B.bADMSendGain); Item->SetBoolField(TEXT("adm_send_mute"),B.bADMSendMute); Item->SetBoolField(TEXT("adm_send_name"),B.bADMSendName);
                Item->SetStringField(TEXT("adm_name_override"),B.ADMNameOverride); Number(Item,TEXT("adm_dref"),B.ADMDref); Number(Item,TEXT("adm_dmax"),B.ADMDmax);
                const auto* State=Snapshot.Objects.FindByPredicate([Index](const auto& S){return S.BindingIndex==Index;});
                Item->SetBoolField(TEXT("snapshot_resolved"),State!=nullptr);
                AActor* Resolved=Manager->RegistryComponent ? Manager->RegistryComponent->ResolveBoundActor(B) : nullptr;
                if (IsValid(Resolved)) { Item->SetStringField(TEXT("resolved_actor_path"),Resolved->GetPathName()); Item->SetStringField(TEXT("actor_label"),Label(Resolved)); Item->SetObjectField(TEXT("world_transform"),Transform(Resolved)); }
                if (State) { Item->SetStringField(TEXT("spatial_guid"),State->SpatialGuid.ToString()); Item->SetArrayField(TEXT("stage_normalized"),Vector(State->StageNormalized)); Item->SetArrayField(TEXT("stage_meters"),Vector(State->StageMeters)); }
                TArray<TSharedPtr<FJsonValue>> Targets;
                for (const auto& Target:B.Targets)
                {
                    auto T=Object(); const uint8 Type=static_cast<uint8>(Target.AdapterType);
                    T->SetStringField(TEXT("protocol"),Type==0?TEXT("ADM"):TEXT("Custom")); T->SetBoolField(TEXT("enabled"),Target.bEnabled);
                    const int32 ID=Target.ObjectIDOverride>=0?Target.ObjectIDOverride:B.DefaultObjectID;
                    T->SetNumberField(TEXT("object_id"),ID); T->SetNumberField(TEXT("object_id_override"),Target.ObjectIDOverride);
                    auto Fields=Object(); for (const auto& Field:Target.CustomFields) Fields->SetStringField(Field.Key,Field.Value); T->SetObjectField(TEXT("custom_fields"),Fields);
                    if (const auto* Config=Manager->AdapterConfigs.Find(Type)) T->SetObjectField(TEXT("adapter"),Adapter(*Config,Type));
                    Targets.Add(MakeShared<FJsonValueObject>(T));
                }
                Item->SetArrayField(TEXT("targets"),Targets); Records.Add(Item);
                if (Records.Num()>MaxRecords) return Serialize(Error(TEXT("scene_record_limit")));
            }
        }
        TArray<UActorComponent*> Components; Actor->GetComponents(Components);
        for (auto* Component:Components)
        {
            if (!IsValid(Component)) continue;
            if (auto* C=Cast<UAudioLoomComponent>(Component))
            {
                auto O=Record(TEXT("playback"),C,Actor); O->SetBoolField(TEXT("registered"),C->IsRegistered());
                O->SetStringField(TEXT("osc_base"),C->GetOscAddress()); auto Controls=Object();
                for (const FString& Suffix:{FString(TEXT("play")),FString(TEXT("stop")),FString(TEXT("loop"))}) Controls->SetStringField(Suffix,C->GetOscAddress()+TEXT("/")+Suffix);
                O->SetObjectField(TEXT("controls"),Controls); O->SetStringField(TEXT("state_address"),C->GetOscAddress()+TEXT("/state"));
                O->SetStringField(TEXT("sound_path"),C->SoundWave?C->SoundWave->GetPathName():TEXT(""));
                if (C->SoundWave) { Number(O,TEXT("duration_seconds"),C->SoundWave->Duration); O->SetNumberField(TEXT("source_channels"),C->SoundWave->NumChannels); }
                O->SetStringField(TEXT("configured_device_id"),C->DeviceId); O->SetNumberField(TEXT("output_channel"),C->OutputChannel);
                O->SetStringField(TEXT("channel_policy"),C->OutputChannel==0?TEXT("corresponding_source_device_channels"):TEXT("first_source_channel_to_selected_output"));
                O->SetBoolField(TEXT("exclusive_requested"),C->bUseExclusiveMode); O->SetNumberField(TEXT("buffer_ms_requested"),C->BufferSizeMs);
                O->SetBoolField(TEXT("playing"),C->IsPlaying()); O->SetStringField(TEXT("last_error"),C->GetLastPlaybackError());
                O->SetBoolField(TEXT("loop"),C->GetLoop()); O->SetBoolField(TEXT("play_on_begin_play"),C->bPlayOnBeginPlay); O->SetBoolField(TEXT("auto_replay"),C->bAutoReplay);
                O->SetBoolField(TEXT("random_replay_delay"),C->bRandomReplayDelay); Number(O,TEXT("replay_delay_seconds"),C->ReplayDelaySeconds);
                Number(O,TEXT("replay_delay_min"),C->ReplayDelayMin); Number(O,TEXT("replay_delay_max"),C->ReplayDelayMax);
                Records.Add(O);
            }
            else if (auto* Server=Cast<USpatialOSCServerComponent>(Component))
            {
                auto O=Record(TEXT("spatial_osc_receiver"),Server,Actor); O->SetBoolField(TEXT("listening"),Server->IsListening());
                O->SetStringField(TEXT("bound_ip"),Server->GetListenIP()); O->SetNumberField(TEXT("bound_port"),Server->GetListenPort()); Records.Add(O);
            }
            else if (Component->IsA<USpatialOSCClientComponent>() || Component->IsA<USpatialObjectRegistry>())
                Records.Add(Record(Component->IsA<USpatialObjectRegistry>()?TEXT("spatial_registry"):TEXT("spatial_osc_client"),Component,Actor));
        }
        if (Records.Num()>MaxRecords) return Serialize(Error(TEXT("scene_record_limit")));
    }
    Records.Sort([](const JObject& A,const JObject& B){return A->GetStringField(TEXT("key"))<B->GetStringField(TEXT("key"));});
    auto Root=Object(); Root->SetStringField(TEXT("schema"),TEXT("org.nplaudio.spatialfabric.scene")); Root->SetNumberField(TEXT("schema_version"),1);
    Root->SetStringField(TEXT("world"),World->GetPathName()); Root->SetStringField(TEXT("world_type"),World->WorldType==EWorldType::PIE?TEXT("PIE"):World->WorldType==EWorldType::Editor?TEXT("Editor"):TEXT("Game"));
    Root->SetStringField(TEXT("snapshot_utc"),FDateTime::UtcNow().ToIso8601()); Number(Root,TEXT("world_time_seconds"),World->GetTimeSeconds());
    Root->SetStringField(TEXT("coordinates"),TEXT("World: UE cm; stage: front/right/up [-1,1]; ADM wire xyz: right/front/up, sphere-limited; Custom depends on adapter templates/ranges"));
    Root->SetStringField(TEXT("state_contract"),TEXT("Configured routing and last observed runtime state; not device enumeration, delivery acknowledgement or physical audio certification"));
    auto Playback=Object(); const auto* Settings=GetDefault<UAudioLoomOscSettings>();
    Playback->SetStringField(TEXT("configured_listen_ip"),Settings->ListenIP); Playback->SetNumberField(TEXT("configured_listen_port"),Settings->ListenPort);
    Playback->SetStringField(TEXT("monitor_ip"),Settings->SendIP); Playback->SetNumberField(TEXT("monitor_port"),Settings->SendPort);
    if (auto* Sub=World->GetSubsystem<UAudioLoomOscSubsystem>())
    {
        Playback->SetBoolField(TEXT("listening"),Sub->IsListening()); Playback->SetStringField(TEXT("bound_ip"),Sub->GetListenIP()); Playback->SetNumberField(TEXT("bound_port"),Sub->GetListenPort());
    }
    Root->SetObjectField(TEXT("playback_network"),Playback);
    Root->SetNumberField(TEXT("offset"),Offset); Root->SetNumberField(TEXT("limit"),Limit); Root->SetNumberField(TEXT("total"),Records.Num());
    TArray<TSharedPtr<FJsonValue>> Page;
    for (int32 I=Offset; I<Records.Num() && Page.Num()<Limit; ++I) Page.Add(MakeShared<FJsonValueObject>(Records[I]));
    Root->SetNumberField(TEXT("returned"),Page.Num()); Root->SetNumberField(TEXT("next_offset"),Offset+Page.Num()<Records.Num()?Offset+Page.Num():-1);
    Root->SetArrayField(TEXT("objects"),Page);
    FString Json=Serialize(Root); FTCHARToUTF8 Bytes(*Json);
    return Bytes.Length()<=MaxJsonBytes?Json:Serialize(Error(TEXT("page_too_large_reduce_limit")));
}

bool ParseRequest(const FOSCMessage& M,FString& ID,int32& Port,int32& Offset,int32& Limit)
{
    const int32 Count=M.GetArgumentsChecked().Num(); Offset=0; Limit=MaxPageSize;
    if ((Count!=2 && Count!=4) || !UOSCManager::GetString(M,0,ID) || !UOSCManager::GetInt32(M,1,Port)) return false;
    if (ID.IsEmpty() || ID.Len()>64 || Port<1024 || Port>65535) return false;
    for (TCHAR C:ID) if (!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='_'||C=='-')) return false;
    if (Count==4 && (!UOSCManager::GetInt32(M,2,Offset)||!UOSCManager::GetInt32(M,3,Limit))) return false;
    return Offset>=0 && Offset<=16384 && Limit>=1 && Limit<=MaxPageSize;
}

TArray<FString> SplitUTF8(const FString& Json)
{
    TArray<FString> Result; FTCHARToUTF8 Text(*Json); const int32 Length=Text.Length();
    if (Length>MaxJsonBytes) return Result;
    for (int32 Begin=0; Begin<Length;)
    {
        int32 End=FMath::Min(Begin+ChunkBytes,Length);
        while (End<Length && End>Begin && (static_cast<uint8>(Text.Get()[End])&0xC0)==0x80) --End;
        FUTF8ToTCHAR Chunk(Text.Get()+Begin,End-Begin); Result.Add(FString(Chunk.Length(),Chunk.Get())); Begin=End;
    }
    return Result;
}

namespace
{
void PutString(TArray<uint8>& Packet,const FString& Text)
{
    FTCHARToUTF8 UTF8(*Text); Packet.Append(reinterpret_cast<const uint8*>(UTF8.Get()),UTF8.Length()); Packet.Add(0); while (Packet.Num()%4) Packet.Add(0);
}
void PutInt(TArray<uint8>& Packet,int32 Value) { for (int32 Shift:{24,16,8,0}) Packet.Add(static_cast<uint8>((static_cast<uint32>(Value)>>Shift)&255)); }
class FService final : public ISpatialFabricSceneQuery
{
    FSocket* Socket=nullptr;
    TMap<FString,double> LastPeer;
    double LastGlobal=-1e9;
public:
    ~FService() { if (Socket) { Socket->Close(); ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket); } }
    bool HandleQuery(const FOSCMessage& Message,UWorld* World,const FString& IP,uint16 SenderPort) override
    {
        if (UOSCManager::GetOSCAddressFullPath(Message.GetAddress())!=TEXT("/spatialfabric/scene/query")) return false;
        const auto* Settings=GetDefault<USpatialFabricSettings>();
        if (!Settings->bEnableSceneQueries || (!IP.StartsWith(TEXT("127.")) && !Settings->bAllowRemoteSceneQueries)) return true;
        FString ID; int32 Port=0,Offset=0,Limit=0;
        if (!ParseRequest(Message,ID,Port,Offset,Limit)) return true;
        const double Now=FPlatformTime::Seconds();
        const double* Previous=LastPeer.Find(IP);
        if (Now-LastGlobal<0.1 || (Previous && Now-*Previous<0.25)) return true;
        if (LastPeer.Num()>=32 && !Previous) LastPeer.Empty();
        LastPeer.Add(IP,Now); LastGlobal=Now;
        auto* System=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM); if (!System) return true;
        if (!Socket) { Socket=System->CreateSocket(NAME_DGram,TEXT("SpatialFabricSceneReply"),true); if (Socket) Socket->SetNonBlocking(true); }
        if (!Socket) return true;
        auto Address=System->CreateInternetAddr(); bool Valid=false; Address->SetIp(*IP,Valid); Address->SetPort(Port); if (!Valid) return true;
        const auto Chunks=SplitUTF8(BuildJson(World,Offset,Limit));
        for (int32 Index=0; Index<Chunks.Num(); ++Index)
        {
            TArray<uint8> Packet; PutString(Packet,TEXT("/spatialfabric/scene/response")); PutString(Packet,TEXT(",siis"));
            PutString(Packet,ID); PutInt(Packet,Index); PutInt(Packet,Chunks.Num()); PutString(Packet,Chunks[Index]);
            int32 Sent=0; Socket->SendTo(Packet.GetData(),Packet.Num(),Sent,*Address); // UDP is intentionally unacknowledged; client retries a missing page.
        }
        return true;
    }
};
TUniquePtr<FService> Service;
}
void Register() { if (Service) return; Service=MakeUnique<FService>(); IModularFeatures::Get().RegisterModularFeature(ISpatialFabricSceneQuery::FeatureName(),Service.Get()); }
void Unregister() { if (Service) { IModularFeatures::Get().UnregisterModularFeature(ISpatialFabricSceneQuery::FeatureName(),Service.Get()); Service.Reset(); } }
}

FString USpatialSceneQueryLibrary::GetSceneJson(const UObject* Context,int32 Offset,int32 Limit)
{
    if (!IsInGameThread()) return SpatialSceneQuery::BuildJson(nullptr,Offset,Limit);
    UWorld* World=GEngine?GEngine->GetWorldFromContextObject(Context,EGetWorldErrorMode::ReturnNull):nullptr;
    return SpatialSceneQuery::BuildJson(World,Offset,Limit);
}
