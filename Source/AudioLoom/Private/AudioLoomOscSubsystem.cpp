// Copyright (c) 2026 AudioLoom Contributors.

/**
 * @file AudioLoomOscSubsystem.cpp
 * @brief OSC message handling: float/int args → play/stop/loop; `SendStateUpdate` sends float to Send IP:Port.
 *
 * `IsPortAvailable` binds a UDP socket briefly to detect collisions before `CreateOSCServer`.
 */

#include "AudioLoomOscSubsystem.h"
#include "AudioLoomManagedOSCClient.h"
#include "AudioLoomComponent.h"
#include "AudioLoomOscSettings.h"
#include "OSCManager.h"
#include "OSCServer.h"
#include "OSCClient.h"
#include "OSCMessage.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

#define LOCTEXT_NAMESPACE "AudioLoomOsc"

static FString NormalizeOscAddress(const FString& Addr)
{
	FString S = Addr.TrimStartAndEnd();
	if (!S.StartsWith(TEXT("/")))
	{
		S = TEXT("/") + S; // OSC 1.0 paths always start with /
	}
	return S;
}

bool UAudioLoomOscSubsystem::IsPortAvailable(int32 Port)
{
	if (Port <= 0 || Port > 65535) return false;

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem) return false;

	FSocket* TestSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("AudioLoomPortCheck"), true);
	if (!TestSocket) return false;

	TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
	bool bValid = false;
	Addr->SetIp(TEXT("0.0.0.0"), bValid); // bind all interfaces — same as server will use
	Addr->SetPort(static_cast<uint16>(Port));

	bool bBound = TestSocket->Bind(*Addr); // if this fails, something else already owns the UDP port
	TestSocket->Close();
	ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(TestSocket);

	return bBound;
}

void UAudioLoomOscSubsystem::Tick(float DeltaTime)
{
	const auto* Permissions = GetDefault<UAudioLoomOscSettings>();
	if (!(GIsEditor ? Permissions->bEnableInEditor : Permissions->bEnableInPackagedBuilds)) { StopListening(); return; }
	if ((OSCClient || IsListening()) && (Permissions->SendIP != MonitoringIP || Permissions->SendPort != MonitoringPort)) UpdateMonitoringTarget();
	if (!IsListening()) return;
	RegistryElapsed += DeltaTime;
	if (RegistryElapsed >= 0.25f) { RegistryElapsed = 0; RebuildComponentRegistry(); }
}

void UAudioLoomOscSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Server is off until StartListening (panel or Blueprint)
}

void UAudioLoomOscSubsystem::Deinitialize()
{
	StopListening(); // release UDP sockets before world dies
	Super::Deinitialize();
}

void UAudioLoomOscSubsystem::RebuildComponentRegistry()
{
	PlayTagToComponents.Empty();
	StopTagToComponents.Empty();
	LoopTagToComponents.Empty();

	UWorld* World = GetWorld();
	if (!World) return;

	// Full world scan: each component registers three full OSC addresses under its base
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TArray<UAudioLoomComponent*> Comps;
		It->GetComponents(Comps);
		for (UAudioLoomComponent* Comp : Comps)
		{
			if (!IsValid(Comp) || !Comp->IsRegistered()) continue;

			FString Base = NormalizeOscAddress(Comp->GetOscAddress());
			if (Base.IsEmpty()) continue;

			FString PlayTag = Base + TEXT("/play");
			FString StopTag = Base + TEXT("/stop");
			FString LoopTag = Base + TEXT("/loop");

			PlayTagToComponents.FindOrAdd(PlayTag).Add(Comp);   // one address → possibly multiple comps if duplicated
			StopTagToComponents.FindOrAdd(StopTag).Add(Comp);
			LoopTagToComponents.FindOrAdd(LoopTag).Add(Comp);
		}
	}
}

bool UAudioLoomOscSubsystem::StartListening()
{
	StopListening();

	const UAudioLoomOscSettings* Settings = GetDefault<UAudioLoomOscSettings>();
	const int32 Port = Settings->ListenPort;
	if (!(GIsEditor ? Settings->bEnableInEditor : Settings->bEnableInPackagedBuilds)) return false;

	if (!IsPortAvailable(Port))
	{
		UE_LOG(LogTemp, Warning, TEXT("AudioLoom OSC: Port %d is not available"), Port);
		return false;
	}

	OSCServer = UOSCManager::CreateOSCServer(Settings->ListenIP, Port, false, true, TEXT("AudioLoom"), this); // UDP, receive enabled
	if (!OSCServer || !OSCServer->IsActive())
	{
		UE_LOG(LogTemp, Error, TEXT("AudioLoom OSC: Failed to create server on port %d"), Port);
		return false;
	}

	OSCServer->OnOscMessageReceivedNative.AddUObject(this, &UAudioLoomOscSubsystem::HandleOSCMessage);

	RebuildComponentRegistry();

	UpdateMonitoringTarget();

	UE_LOG(LogTemp, Log, TEXT("AudioLoom OSC: Listening on port %d"), Port);
	return true;
}

void UAudioLoomOscSubsystem::StopListening()
{
	if (OSCServer)
	{
		OSCServer->Stop();
		OSCServer->OnOscMessageReceivedNative.RemoveAll(this); // don’t call back into a destroyed subsystem
		OSCServer = nullptr;
	}
	if (OSCClient)
	{
		CastChecked<UAudioLoomManagedOSCClient>(OSCClient)->Shutdown();
		OSCClient = nullptr;
	}
}

bool UAudioLoomOscSubsystem::IsListening() const
{
	return OSCServer && OSCServer->IsActive();
}

void UAudioLoomOscSubsystem::UpdateMonitoringTarget()
{
	const UAudioLoomOscSettings* Settings = GetDefault<UAudioLoomOscSettings>();
	if (!(GIsEditor ? Settings->bEnableInEditor : Settings->bEnableInPackagedBuilds))
	{
		if (OSCClient) { CastChecked<UAudioLoomManagedOSCClient>(OSCClient)->Shutdown(); OSCClient = nullptr; }
		return;
	}
	FString IP = Settings->SendIP;
	int32 Port = Settings->SendPort;
	if (IP.IsEmpty()) IP = TEXT("127.0.0.1"); // sane default for local Max/PD/TouchOSC bridges
	if (Port <= 0) Port = 9001;

	MonitoringIP = Settings->SendIP;
	MonitoringPort = Settings->SendPort;
	if (OSCClient && OSCClient->IsActive())
	{
		if (OSCClient->SetSendIPAddress(IP, Port)) return;
		CastChecked<UAudioLoomManagedOSCClient>(OSCClient)->Shutdown();
		OSCClient = nullptr;
		return;
	}
	if (Port > 65535) return;
	OSCClient = NewObject<UAudioLoomManagedOSCClient>(this);
	OSCClient->Connect();
	if (!OSCClient->SetSendIPAddress(IP, Port))
	{
		CastChecked<UAudioLoomManagedOSCClient>(OSCClient)->Shutdown();
		OSCClient = nullptr;
	}
}

bool UAudioLoomOscSubsystem::IsPlayTrigger(const FOSCMessage& Message)
{
	if (Message.GetArgumentsChecked().Num() == 0) return true;
	TArray<float> Floats; UOSCManager::GetAllFloats(Message, Floats);
	if (Floats.Num()) return FMath::IsFinite(Floats[0]) && Floats[0] >= 0.5f;
	TArray<int32> Ints; UOSCManager::GetAllInt32s(Message, Ints);
	return Ints.Num() && Ints[0] >= 1;
}

void UAudioLoomOscSubsystem::HandleOSCMessage(const FOSCMessage& Message, const FString& IPAddress, uint16 Port)
{
	const FString AddressStr = UOSCManager::GetOSCAddressFullPath(Message.GetAddress());
	FString Normalized = NormalizeOscAddress(AddressStr); // must match keys built in RebuildComponentRegistry

	float TriggerValue = 0.f;
	TArray<float> Floats;
	UOSCManager::GetAllFloats(Message, Floats);
	if (Floats.Num() > 0) TriggerValue = Floats[0];
	else
	{
		TArray<int32> Ints;
		UOSCManager::GetAllInt32s(Message, Ints);
		if (Ints.Num() > 0) TriggerValue = static_cast<float>(Ints[0]);
	}

	if (TArray<TWeakObjectPtr<UAudioLoomComponent>>* PlayComps = PlayTagToComponents.Find(Normalized))
	{
		// No args or high value → play; low float could mean “don’t play” in some rigs
		const bool bTriggerPlay = IsPlayTrigger(Message);
		for (TWeakObjectPtr<UAudioLoomComponent>& W : *PlayComps)
		{
			if (UAudioLoomComponent* C = W.Get())
			{
				if (bTriggerPlay) C->Play();
			}
		}
	}

	if (TArray<TWeakObjectPtr<UAudioLoomComponent>>* StopComps = StopTagToComponents.Find(Normalized))
	{
		for (TWeakObjectPtr<UAudioLoomComponent>& W : *StopComps)
		{
			if (UAudioLoomComponent* C = W.Get())
			{
				C->Stop();
			}
		}
	}

	if (TArray<TWeakObjectPtr<UAudioLoomComponent>>* LoopComps = LoopTagToComponents.Find(Normalized))
	{
		const bool bEnableLoop = (TriggerValue > 0.5f); // convention: >0.5 on, else off
		for (TWeakObjectPtr<UAudioLoomComponent>& W : *LoopComps)
		{
			if (UAudioLoomComponent* C = W.Get())
			{
				C->SetLoop(bEnableLoop);
			}
		}
	}
}

void UAudioLoomOscSubsystem::SendStateUpdate(UAudioLoomComponent* Component, bool bPlaying)
{
	if (!OSCClient || !OSCClient->IsActive() || !IsValid(Component)) return;

	FString Base = NormalizeOscAddress(Component->GetOscAddress());
	if (Base.IsEmpty()) return;

	FString Addr = Base + TEXT("/state"); // monitoring convention: one float 1=playing 0=stopped

	FOSCMessage Msg;
	Msg.SetAddress(UOSCManager::ConvertStringToOSCAddress(Addr));
	UOSCManager::AddFloat(Msg, bPlaying ? 1.f : 0.f);
	OSCClient->SendOSCMessage(Msg); // UDP unicast to SendIP:SendPort
}

#undef LOCTEXT_NAMESPACE
