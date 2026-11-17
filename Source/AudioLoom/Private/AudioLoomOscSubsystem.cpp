// Copyright (c) 2026 AudioLoom Contributors.

/**
 * @file AudioLoomOscSubsystem.cpp
 * @brief OSC message handling: float/int args → play/stop/loop; `SendStateUpdate` sends float to Send IP:Port.
 *
 * `IsPortAvailable` binds a UDP socket briefly to detect collisions before `CreateOSCServer`.
 */

#include "AudioLoomOscSubsystem.h"
#include "AudioLoomLiveInputComponent.h"
#include "AudioLoomManagedOSCClient.h"
#include "AudioLoomComponent.h"
#include "AudioLoomOscSettings.h"
#include "OSCManager.h"
#include "SpatialFabricOSCReceiver.h"
#include "SpatialFabricSceneQueryBridge.h"
#include "OSCClient.h"
#include "OSCMessage.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

#define LOCTEXT_NAMESPACE "AudioLoomOsc"

static void AppendOscString(TArray<uint8>& Bytes, const FString& Value)
{
	FTCHARToUTF8 UTF8(*Value);
	Bytes.Append(reinterpret_cast<const uint8*>(UTF8.Get()), UTF8.Length());
	Bytes.Add(0);
	while (Bytes.Num() % 4) Bytes.Add(0);
}

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
	Addr->SetIp(TEXT("0.0.0.0"), bValid); // Conservative UI probe; actual StartListening binds the configured IP directly.
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
	PlaybackBases.Empty();
	LiveBases.Empty();

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

			PlaybackBases.FindOrAdd(Base).Add(Comp); // duplicate bases retain fan-out semantics
		}
		TArray<UAudioLoomLiveInputComponent*> LiveComps;
		It->GetComponents(LiveComps);
		for (UAudioLoomLiveInputComponent* Live : LiveComps)
		{
			if (!IsValid(Live) || !Live->IsRegistered()) continue;
			const FString LiveBase = NormalizeOscAddress(Live->GetOscAddress());
			if (LiveBase.IsEmpty()) continue;
			LiveBases.FindOrAdd(LiveBase).Add(Live);
		}
	}
}

bool UAudioLoomOscSubsystem::StartListening()
{
	StopListening();

	const UAudioLoomOscSettings* Settings = GetDefault<UAudioLoomOscSettings>();
	const int32 Port = Settings->ListenPort;
	if (!(GIsEditor ? Settings->bEnableInEditor : Settings->bEnableInPackagedBuilds)) return false;

	Receiver = MakeShared<FSpatialFabricOSCReceiver>();
	if (Port <= 0 || !Receiver->Start(Settings->ListenIP, Port))
	{
		Receiver.Reset();
		UE_LOG(LogTemp, Warning, TEXT("AudioLoom OSC: Failed to bind %s:%d"), *Settings->ListenIP, Port);
		return false;
	}

	RebuildComponentRegistry();

	UpdateMonitoringTarget();
	Receiver->SetDispatcher([Owner = TWeakObjectPtr<UAudioLoomOscSubsystem>(this)](const FOSCMessage& Message, const FString& IP, uint16 SenderPort)
	{ if (Owner.IsValid()) Owner->HandleOSCMessage(Message, IP, SenderPort); });

	// Core ticker also runs while the world is paused / editor viewports are not realtime.
	MonitoringTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float Delta)
	{
		Tick(Delta); // also refresh registry, permissions and monitor target in suspended worlds
		if (!IsListening()) return false;
		PollCueEvents();
		return true;
	}), 0.02f);
	UE_LOG(LogTemp, Log, TEXT("AudioLoom OSC: Listening on port %d"), Port);
	return true;
}

void UAudioLoomOscSubsystem::StopListening()
{
	FTSTicker::GetCoreTicker().RemoveTicker(MonitoringTicker);
	MonitoringTicker.Reset();
	if (Receiver) Receiver->Stop();
	Receiver.Reset();
	if (OSCClient)
	{
		CastChecked<UAudioLoomManagedOSCClient>(OSCClient)->Shutdown();
		OSCClient = nullptr;
	}
}

bool UAudioLoomOscSubsystem::IsListening() const
{
	return Receiver && Receiver->IsListening();
}

FString UAudioLoomOscSubsystem::GetListenIP() const
{
	return Receiver && Receiver->IsListening() ? Receiver->GetIP() : FString();
}

int32 UAudioLoomOscSubsystem::GetListenPort() const
{
	return Receiver && Receiver->IsListening() ? Receiver->GetPort() : 0;
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

FAudioLoomOscDecision UAudioLoomOscSubsystem::DecideCommand(const FString& Command, const FOSCMessage& Message, bool bLive)
{
	FAudioLoomOscDecision D;
	TArray<float> Floats; UOSCManager::GetAllFloats(Message, Floats);
	TArray<int32> Ints; UOSCManager::GetAllInt32s(Message, Ints);
	const bool bNumeric = Floats.Num() || Ints.Num();
	D.Value = Floats.Num() ? Floats[0] : (Ints.Num() ? float(Ints[0]) : 0.f);
	if (Command == TEXT("ack") || Command == TEXT("state"))
	{ D.Result = TEXT("ignored"); D.Detail = TEXT("engine-only message"); return D; }
	if (Command == TEXT("play"))
	{
		D.bAct = IsPlayTrigger(Message);
		D.Result = D.bAct ? TEXT("received") : TEXT("ignored");
		if (!D.bAct) D.Detail = bNumeric ? TEXT("below play threshold") : TEXT("non-numeric payload");
		return D;
	}
	if (Command == TEXT("stop")) D.bAct = true; // payload is intentionally ignored
	else if (Command == TEXT("status"))
	{
		D.bAct = Message.GetArgumentsChecked().Num() == 0;
		if (!D.bAct) D.Detail = TEXT("status takes no arguments");
	}
	else if (Command == TEXT("loop") && !bLive)
	{
		D.bAct = Message.GetArgumentsChecked().Num() == 0 || (bNumeric && FMath::IsFinite(D.Value));
		if (!D.bAct) { D.Result = TEXT("ignored"); D.Detail = TEXT("non-numeric payload"); return D; }
	}
	else if (Command == TEXT("gain") && bLive)
	{
		D.bAct = UAudioLoomLiveInputComponent::ParseGainArguments(Floats, Ints, D.Value);
		if (!D.bAct) D.Detail = TEXT("gain requires a number in 0..4");
	}
	else D.Detail = TEXT("unknown or unsupported command for cue kind");
	if (D.bAct) D.Result = TEXT("received");
	return D;
}

TArray<uint8> UAudioLoomOscSubsystem::MakeAcknowledgement(const FString& Base, const FString& Command, const FString& Result, bool bPlaying, const FString& Detail)
{
	FString SafeCommand;
	for (TCHAR C : Command.Left(32))
		SafeCommand += (C < 128 && FChar::IsAlnum(C)) || C == TEXT('_') || C == TEXT('-') ? C : TEXT('_');
	// UE's OSC FStream::WriteString truncates TCHARs to ANSI bytes; acknowledgements require UTF-8.
	TArray<uint8> Bytes;
	auto String = [&Bytes](const FString& Value)
	{
		AppendOscString(Bytes, Value);
	};
	String(NormalizeOscAddress(Base) + TEXT("/ack"));
	String(TEXT(",ssis"));
	String(SafeCommand);
	String(Result);
	Bytes.Append({0, 0, 0, uint8(bPlaying ? 1 : 0)});
	String(Detail.Left(256));
	return Bytes;
}

void UAudioLoomOscSubsystem::SendAcknowledgement(const FString& Base, const FString& Command, const FString& Result, bool bPlaying, const FString& Detail)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (AckObserver) { AckObserver(Command, Result, bPlaying, Detail); return; }
#endif
	if (!OSCClient || !OSCClient->IsActive()) return;
	const TArray<uint8> Bytes = MakeAcknowledgement(Base, Command, Result, bPlaying, Detail);
	CastChecked<UAudioLoomManagedOSCClient>(OSCClient)->SendBytes(Bytes);
}

void UAudioLoomOscSubsystem::SendPlayFailure(const FString& Base, const FString& Reason, bool bPlaying)
{
	SendAcknowledgement(Base, TEXT("play"), TEXT("failed"), bPlaying, Reason);
}

void UAudioLoomOscSubsystem::PollCueEvents()
{
	// State delegates may rebuild the registry. Never keep map/array iterators across callbacks.
	TArray<TWeakObjectPtr<UAudioLoomComponent>> Playback;
	TArray<TWeakObjectPtr<UAudioLoomLiveInputComponent>> Live;
	for (const auto& Pair : PlaybackBases) Playback.Append(Pair.Value);
	for (const auto& Pair : LiveBases) Live.Append(Pair.Value);
	for (const auto& Weak : Playback) if (auto* C = Weak.Get()) C->PollPlaybackEvents();
	for (const auto& Weak : Live) if (auto* C = Weak.Get()) C->PollBackend();
}

void UAudioLoomOscSubsystem::HandleOSCMessage(const FOSCMessage& Message, const FString& IPAddress, uint16 Port)
{
	if (ISpatialFabricSceneQuery::Dispatch(Message, GetWorld(), IPAddress, Port)) return;
	const FString Address = NormalizeOscAddress(UOSCManager::GetOSCAddressFullPath(Message.GetAddress()));
	// Match the longest registered base first, retaining the entire unknown suffix.
	// Splitting at the final slash silently drops known-base requests like /cue/foo/bar.
	FString Base;
	auto MatchBase = [&](const auto& Bases)
	{
		for (const auto& Pair : Bases)
			if (Pair.Key.Len() > Base.Len() && Address.StartsWith(Pair.Key + TEXT("/"), ESearchCase::CaseSensitive)) Base = Pair.Key;
	};
	MatchBase(PlaybackBases);
	MatchBase(LiveBases);
	if (Base.IsEmpty()) return;
	const FString Command = Address.Mid(Base.Len() + 1);

	PollCueEvents(); // status/ack reads observations even when component ticking is suspended
	const auto Playback = PlaybackBases.FindRef(Base);
	const auto Live = LiveBases.FindRef(Base);
	for (const auto& Weak : Playback)
			if (UAudioLoomComponent* C = Weak.Get())
			{
				const auto D = DecideCommand(Command, Message, false);
				// A received play is a request, emitted BEFORE any synchronous failure or worker observation.
				if (Command == TEXT("play")) SendAcknowledgement(Base, Command, D.Result, C->IsPlaying(), D.Detail);
				if (D.bAct)
				{
					if (Command == TEXT("play")) C->Play();
					else if (Command == TEXT("stop")) { C->Stop(); SendStateUpdate(C, false); }
					else if (Command == TEXT("loop")) C->SetLoop(D.Value > 0.5f);
				}
				if (!IsValid(C)) continue;
				if (Command != TEXT("play")) SendAcknowledgement(Base, Command, D.Result, C->IsPlaying(), D.Detail);
				if (Command == TEXT("status") && D.bAct) SendStateUpdate(C, C->IsPlaying());
			}
	for (const auto& Weak : Live)
			if (UAudioLoomLiveInputComponent* C = Weak.Get())
			{
				const auto D = DecideCommand(Command, Message, true);
				if (Command == TEXT("play")) SendAcknowledgement(Base, Command, D.Result, C->IsLive(), D.Detail);
				if (D.bAct)
				{
					if (Command == TEXT("play"))
					{
						if (!C->StartLive() && IsValid(C)) SendPlayFailure(Base, TEXT("Live backend unavailable, component unregistered or invalid live settings"), C->IsLive());
					}
					else if (Command == TEXT("stop")) { C->StopLive(); SendLiveStateUpdate(C, false); }
					else if (Command == TEXT("gain")) C->SetGain(D.Value);
				}
				if (!IsValid(C)) continue;
				if (Command != TEXT("play")) SendAcknowledgement(Base, Command, D.Result, C->IsLive(), D.Detail);
				if (Command == TEXT("status") && D.bAct) SendLiveStateUpdate(C, C->IsLive());
			}
}

void UAudioLoomOscSubsystem::SendStateUpdate(UAudioLoomComponent* Component, bool bPlaying)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (StateObserver) { StateObserver(bPlaying); return; }
#endif
	if (IsValid(Component)) SendState(Component->GetOscAddress(), bPlaying);
}

void UAudioLoomOscSubsystem::SendLiveStateUpdate(UAudioLoomLiveInputComponent* Component, bool bLive)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (StateObserver) { StateObserver(bLive); return; }
#endif
	if (IsValid(Component)) SendState(Component->GetOscAddress(), bLive);
}

void UAudioLoomOscSubsystem::SendState(const FString& Base, bool bPlaying)
{
	if (!OSCClient || !OSCClient->IsActive()) return;
	TArray<uint8> Bytes;
	AppendOscString(Bytes, NormalizeOscAddress(Base) + TEXT("/state"));
	AppendOscString(Bytes, TEXT(",f"));
	Bytes.Append({uint8(bPlaying ? 0x3f : 0), uint8(bPlaying ? 0x80 : 0), 0, 0});
	CastChecked<UAudioLoomManagedOSCClient>(OSCClient)->SendBytes(Bytes);
}

#undef LOCTEXT_NAMESPACE
