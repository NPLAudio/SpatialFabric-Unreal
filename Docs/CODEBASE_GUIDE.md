![SpatialFabric and AudioLoom, woven together](../assets/spatialfabric-unified.png)

# SpatialFabric — codebase guide

This document explains **how the plugin fits together** before you dive into C++ files. Read it once, then use file-top comments in the source as a map.

## What SpatialFabric does (one sentence)

It reads actor positions into a stable-identity snapshot, encodes/routes OSC for external spatial systems, and includes independent direct-device PCM playback. Spatial and playback output are separate pipelines.

## Included audio playback modules

The same `SpatialFabric.uplugin` also owns `Source/AudioLoom` (runtime device/channel
playback, SoundWave loading, OSC transport subsystem) and `Source/AudioLoomEditor`
(playback panel, Details customization, CSV routing). `SpatialFabricEditor` embeds
`SAudioLoomPanel` as its **Playback** tab. The legacy modules/class names are retained
to preserve serialized `/Script/AudioLoom` references and existing C++ dependencies.

This is one installable plugin, not one runtime pipeline: spatial snapshots drive
external positioning protocols; AudioLoom plays PCM directly to OS devices. The
OSC servers/settings are independent. See [Audio playback](AudioPlayback.md) for
migration, ports, platform support, and known limitations.

## Main ideas (no jargon)

| Idea | Meaning |
|------|--------|
| **Binding** | “Track *this* actor and treat it as spatial object #3 (for example).” |
| **Stage volume** | A box in the level: center = (0,0,0) in normalized space; edges = ±1. |
| **Snapshot** | One frame’s worth of all tracked positions + optional listener data. |
| **Adapter** | Code that turns snapshot data into a specific protocol (e.g. `/adm/obj/1/xyz`). |
| **Router** | Sends each adapter only the objects that are routed to that adapter. |

## Data flow (every frame)

1. **`ASpatialFabricManagerActor::Tick`** runs (or the editor tickable calls **`ProcessFrame`** on existing managers without PIE). Preview state capture is independent of networking permission; actors are never auto-spawned by the tickable.
2. **`USpatialObjectRegistry::BuildSnapshot`** walks **`ObjectBindings`**, resolves each **`TargetActor`**, and fills **`FSpatialFrameSnapshot`** using **`ASpatialStageVolume`** for normalization.
3. **`FProtocolRouter::ProcessFrame`** filters the snapshot per adapter type, then calls **`ISpatialProtocolAdapter::ProcessFrame`** on ADM-OSC and Custom adapters.
4. Each state carries its source `BindingIndex`; the router never infers association from compacted array position. Adapters' pure `BuildMessages` encoders produce the same messages for preview and transmission.
5. Adapters use a reusable **`USpatialOSCClientComponent`** to send to their configured endpoint. Complete encoded messages determine send-on-change behavior.
6. Listener following updates a virtual coordinate basis, never the authored stage transform. ADM xyz/aed share a radially limited unit-sphere position.

## Suggested reading order (C++)

1. **`SpatialFabricTypes.h`** — all structs/enums; everything else references these.
2. **`ISpatialProtocolAdapter.h`** — the “plugin point” for new protocols.
3. **`ProtocolRouter.h` / `.cpp`** — how objects are filtered per adapter.
4. **`SpatialObjectRegistry`** — actor → normalized state.
5. **`SpatialStageVolume`** — world position → [-1, 1] and meters.
6. **`SpatialFabricManagerActor`** — wires components, tick, adapters.
7. **`SpatialMath`** — polar/Cartesian helpers for OSC.
8. **`Adapters/ADMOSCAdapter`**, **`Adapters/CustomAdapter`** — wire format details.
9. **`SpatialOSCClientComponent` / `SpatialOSCServerComponent`** — reusable UE OSC clients and exact-bind server polling.
10. **`Source/Shared/SpatialFabricOSCReceiver.h`** — bounded OSC decoder and socket ownership shared by the independent spatial/playback modules; avoids UE 5.8's loopback-to-LAN rewrite.
11. **`SpatialFabricEditorModule`** + **`SSpatialFabricPanel`** — editor UI (large file; use section comments).
12. **`Tools/Unreal/`** — reproducible [SpatialSandbox showcase](SpatialSandboxDemo.md) and live/capture diagnostics, not runtime code.

## Unreal Engine concepts you’ll see

- **`UCLASS` / `USTRUCT` / `UPROPERTY`** — reflection; Editor and Blueprints can edit or read these.
- **`AActor`** — anything placed in a level (your stage volume, manager, tracked props).
- **`UActorComponent`** — logic attached to an actor (registry, OSC client/server).
- **`TSoftObjectPtr`** — reference that may not be loaded; used for picking actors in the editor.
- **Game thread** — spatial routing/UDP receive dispatch run here; adapters are not thread-safe for background use. PCM device output uses its separate worker/callback backend.

## Where settings live

- **`USpatialFabricSettings`** — project defaults (ports, IPs, debug flags): **Project Settings → Plugins → Spatial Fabric**, stored in config such as **`DefaultSpatialFabric.ini`**.
- **`FSpatialAdapterConfig`** on the manager — per-format IP, port, send rate, coordinate mode, etc.

## Adding a new protocol (high level)

1. Add a value to **`ESpatialAdapterType`** in **`SpatialFabricTypes.h`** (and any UI/panel wiring).
2. Implement **`ISpatialProtocolAdapter`** (see **`FADMOSCAdapter`** as a full example).
3. Register it in **`ASpatialFabricManagerActor::InitializeAdapters`**.
4. Ensure **`FilterSnapshotForAdapter`** in **`ProtocolRouter.cpp`** still matches binding **`Targets`** to your type (same pattern as Custom).

## Tests

Automation tests live in **`SpatialFabricTests.cpp`**. They validate math and address strings without loading a full level.

Run `python -m unittest discover -s Tools -p "test_*.py"` from the plugin root for
structural and native-report-runner checks. Run `Tools/validate_unreal.py --engine
<UE install>` for Editor/Development/Shipping compilation and actual native regressions.
See `VALIDATION.md` for the manual hardware/cooked-content release gates. Automation
results are read from JSON, not Unreal's sometimes-zero failure exit status.

---

For line-level explanations, see the comment blocks at the top of each source file, **inline comments** inside functions (especially OSC, router, registry, adapters), and notes above non-trivial blocks.
