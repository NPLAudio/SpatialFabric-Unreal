// Offline-first editor routing. No stream or OSC operations. MIT.
#pragma once
#include "CoreMinimal.h"
#include "Misc/Crc.h"
#include "AudioOutputDeviceInfo.h"
class UAudioLoomComponent;
namespace AudioRouting
{
// FString's default equality/hash are case-insensitive; native endpoint IDs are not.
inline bool SameId(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::CaseSensitive); }
struct FExactIdKeys : DefaultKeyFuncs<FString>
{
    static bool Matches(const FString& A, const FString& B) { return SameId(A,B); }
    static uint32 GetKeyHash(const FString& Key) { return FCrc::StrCrc32(*Key); }
};
template<typename Value> struct TExactIdMapKeys : TDefaultMapHashableKeyFuncs<FString, Value, false>
{
    static bool Matches(const FString& A, const FString& B) { return SameId(A,B); }
    static uint32 GetKeyHash(const FString& Key) { return FCrc::StrCrc32(*Key); }
};
constexpr int32 MaxDevices = 256, MaxChannels = 4096, MaxText = 1024, MaxJson = 2097152;
struct FInterface
{
    FString Id, Name, LastSeen;
    int32 Channels = 0, SampleRate = 0;
    bool Online = false;
};
class SPATIALFABRICEDITOR_API FCache
{
public:
    TArray<FInterface> Devices;
    FString Status = TEXT("Not refreshed this session; cached interfaces are offline.");
    bool Load(const FString& Json);
    bool Accept(const TArray<FAudioOutputDeviceInfo>& Fresh, const FString& Error,
        TFunctionRef<bool(const FString&)> Save);
    static FString Path();
    void LoadDisk();
    bool Refresh(); // only UI Refresh Interfaces calls this
};
struct FGroup
{
    FString Id, Title;
    int32 Capacity = 0, Start = 0;
    TArray<int32> Stored;
    int32 Count() const { return 1 + Capacity + Stored.Num(); }
    int32 ChannelAt(int32 Offset) const { return Offset <= Capacity ? Offset : Stored[Offset - Capacity - 1]; }
};
struct FRow
{
    TWeakObjectPtr<UAudioLoomComponent> Component;
    FString Name, Id;
    int32 Channel = 0;
};
struct SPATIALFABRICEDITOR_API FMatrix
{
    TArray<FRow> Rows;
    TArray<FGroup> Groups;
    int32 Columns = 0;
    void Capture(const TArray<UAudioLoomComponent*>& Components, const TArray<FInterface>& Devices);
    const FGroup* Column(int32 Index, int32& Channel) const;
};
struct FRange { int32 First = 0, End = 0; };
SPATIALFABRICEDITOR_API FRange Visible(float Offset, float Extent, float Stride, int32 Count);
SPATIALFABRICEDITOR_API bool ValidText(const FString& Text, bool Required = false);
SPATIALFABRICEDITOR_API bool CanAssign(const UAudioLoomComponent* Component, FText& Reason);
SPATIALFABRICEDITOR_API bool Assign(UAudioLoomComponent* Component, const FString& Id, int32 Channel, FText& Reason);
}
