// Copyright (c) 2026 SpatialFabric Contributors. Licensed under the MIT License.

// Exact-bind UDP receive; bounded game-thread polling → HandleOSCMessage → broadcast.

#include "SpatialOSCServerComponent.h"
#include "SpatialFabricSettings.h"
#include "SpatialFabricOSCReceiver.h"
#include "SpatialFabricSceneQueryBridge.h"
#include "OSCManager.h"
#include "OSCAddress.h"
#include "OSCTypes.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpatialOSCServer, Log, All);

#define SF_LOG_DEBUG(Fmt, ...) \
	do { if (GetDefault<USpatialFabricSettings>()->bEnableDebugMessages) { UE_LOG(LogSpatialOSCServer, Log, Fmt, ##__VA_ARGS__); } } while(0)

USpatialOSCServerComponent::USpatialOSCServerComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // Receiver dispatch uses the core ticker, including paused worlds.
}

void USpatialOSCServerComponent::BeginPlay()
{
	Super::BeginPlay();
}

void USpatialOSCServerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopListening();
	Super::EndPlay(EndPlayReason);
}

void USpatialOSCServerComponent::OnUnregister()
{
	// Property/Undo reconstruction is not an explicit transport Stop. Real deletion,
	// map reload and all other unregisters still release the socket immediately.
#if WITH_EDITOR
	if (!bPreserveListeningOnEditorReregister)
#endif
		StopListening();
	Super::OnUnregister();
}

#if WITH_EDITOR
void USpatialOSCServerComponent::PreEditUndo()
{
	// Component transactions may run before the owning manager's PreEditChange.
	TGuardValue<bool> PreserveListener(bPreserveListeningOnEditorReregister, true);
	Super::PreEditUndo();
}

void USpatialOSCServerComponent::PostEditUndo()
{
	Super::PostEditUndo();
	// Undo that actually deletes the component/owner must still release the listener.
	if (!IsValid(this) || !IsValid(GetOwner())) StopListening();
}
#endif

void USpatialOSCServerComponent::BeginDestroy()
{
	StopListening();
	Super::BeginDestroy();
}

void USpatialOSCServerComponent::StartListening(const FString& IP, int32 Port)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestStart) { TestStart(IP, Port); return; }
#endif
	StopListening();

	Receiver = MakeShared<FSpatialFabricOSCReceiver>();
	if (Port <= 0 || !Receiver->Start(IP, Port))
	{
		Receiver.Reset();
		UE_LOG(LogSpatialOSCServer, Error, TEXT("Failed to bind OSC Server on %s:%d"), *IP, Port);
		return;
	}

	CurrentIP = Receiver->GetIP();
	CurrentPort = Receiver->GetPort();
	Receiver->SetDispatcher([Owner = TWeakObjectPtr<USpatialOSCServerComponent>(this)](const FOSCMessage& Message, const FString& SenderIP, uint16 SenderPort)
	{ if (Owner.IsValid()) Owner->HandleOSCMessage(Message, SenderIP, SenderPort); });
	SF_LOG_DEBUG(TEXT("OSC Server listening on %s:%d"), *IP, Port);
}

void USpatialOSCServerComponent::StopListening()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestStop) { TestStop(); return; }
#endif
	if (Receiver) Receiver->Stop();
	Receiver.Reset();
	CurrentPort = 0;
	CurrentIP.Empty();
}

bool USpatialOSCServerComponent::IsListening() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestIsListening) return TestIsListening();
#endif
	return Receiver && Receiver->IsListening();
}

void USpatialOSCServerComponent::HandleOSCMessage(const FOSCMessage& Message, const FString& IPAddress, uint16 Port)
{
	if (ISpatialFabricSceneQuery::Dispatch(Message, GetWorld(), IPAddress, Port)) return;
	const FString AddressStr = UOSCManager::GetOSCAddressFullPath(Message.GetAddress());

	float Value = 0.f;
	bool bFoundFloat = false;

	// SpatialFabric's delegate is (Address, float) — take first numeric arg we can read
	TArray<float> Floats;
	UOSCManager::GetAllFloats(Message, Floats);
	if (Floats.Num() > 0)
	{
		Value = Floats[0];
		bFoundFloat = true;
	}
	if (!bFoundFloat)
	{
		TArray<int32> Ints;
		UOSCManager::GetAllInt32s(Message, Ints);
		if (Ints.Num() > 0)
		{
			Value = static_cast<float>(Ints[0]);
			bFoundFloat = true;
		}
	}

	if (!bFoundFloat) return;

	OnMessageReceived.Broadcast(AddressStr, Value);
}
