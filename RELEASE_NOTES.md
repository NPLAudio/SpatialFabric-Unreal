# SpatialFabric 2.1.0

Spatial control, direct-device cue playback, read-only scene discovery and a
standalone OSC Controller 1.2.0. The plugin retains its AudioLoom module/class
identities. This is a source plugin release; the descriptor remains marked beta.

## Choose a download

| Artifact | Use |
|---|---|
| `SpatialFabric-2.1.0.zip` | Source plugin only. Extract `SpatialFabric` into `YourProject/Plugins`, then build with your matching Unreal toolchain. |
| `SpatialFabric-Example-2.1.0.zip` | Standalone four-room C++ showcase project with assets, original WAV sources and its own project-local source plugin. |
| `SpatialFabric-2.1.0-CompleteSource.zip` | Full source tree including controller, Max/Node-RED/Python/Node scene-query examples, guides and tests. |
| `SpatialFabricOSCController-1.2.0-Windows-x64.zip` | Portable Windows desktop app; extract the entire ZIP and launch its EXE. Bundled Python/Tk and license notices; no Unreal/Python installation needed. |
| `RELEASE_MANIFEST.json`, `SHA256SUMS.txt` | Exact source commit, artifact sizes and SHA-256 checksums. |

No Unreal Binaries/Intermediate/Saved caches, local MCP settings/credentials or
private validation logs are included. The Windows app is unsigned; Windows may
show a SmartScreen warning. Preserve its included runtime notices when sharing.
The old presentation is **not** included; it remains downloadable from
[release 2.0.3](https://github.com/NPLAudio/SpatialFabric/releases/tag/v2.0.3).

## What's new

- **Read-only scene query:** `/spatialfabric/scene/query` on a running spatial input
  (default 8100) or Playback input (9000). Correlated, bounded, paginated JSON over
  OSC strings describes managers, stages/listeners, bindings, playback components,
  routing and observed software state. Blueprint/Python/C++ can obtain local JSON
  through `SpatialSceneQueryLibrary.GetSceneJson` without a UDP receiver.
- **Interoperability examples:** dependency-free Python/Node clients, Node for Max
  wrapper, importable Node-RED flow, sample JSON and a v1 JSON Schema. See
  `Docs/SceneQuery.md` and `Examples/OSCSceneQuery/README.md` in CompleteSource.
- **Controller 1.2:** simultaneous state/ADM/Custom receivers, same-PC diagnostics,
  top/front/side/isometric/polar/history tracking, JSON scene inventory/export and
  confirmed cue import. Querying/importing configuration never triggers audio.
- **Playback UI:** labelled incoming/outgoing sections, complete IPv4/five-digit
  port editors and responsive wrapping instead of compressed endpoint fields.
- **Slim room showcase:** spatial motion, mono percussion, stereo groove and replay
  each have their own room off a walkable hall. 90 → 66 generated actors, 17 → 11
  text actors. Dummy speakers/billboards/floating labels and unused branding/driver
  packages are removed. The published map excludes the unrelated outside plane.
- **Repository example discovery:** preserves the upstream parent-plugin-directory
  fix; prepared copies have their own plugin and need no repository-relative path.

## Plugin and example installation

1. Close Unreal and back up your project. If upgrading standalone AudioLoom, remove
   that duplicate plugin and its project entry before installing SpatialFabric.
2. Extract the **plugin ZIP** into `YourProject/Plugins/SpatialFabric`. Enable
   SpatialFabric and Unreal's built-in OSC plugin.
3. Build against **your installed engine**. UE **5.8 Win64** is the validated native
   combination, using a supported Visual Studio 2022 C++ toolchain, Windows SDK and
   .NET Framework SDK. These ZIPs are not universal precompiled Unreal DLLs.
4. Open **Window → Spatial Fabric**. In Playback, choose a safe output device/channel
   before starting a cue. Audio is manual-start and the example sources are quiet.

For the **example ZIP**, extract the entire `SpatialFabricShowcase` folder, build
its `SpatialFabricShowcaseEditor` target, then open its `.uproject`. Its native
walking pawn starts in the hallway. WASD/mouse, Space jump, Shift+F1 release mouse.
The level's room labels refer to real Playback/OSC controls, not fake world buttons.
Room walls organize visuals; they do not acoustically isolate direct PCM playback.

For the **controller**, close any older controller/capture program occupying its
receive ports. Use **Start all receivers** for 9001/50018/9002. Unreal must separately
**Start OSC** on Playback's input 9000 for Play/Stop/Loop. Object tracker → **Query
scene** defaults to 8100 and temporary reply port 9011; choose 9000 for a running
Playback-only input. Nothing auto-sends or starts audio on app launch.

## Important boundaries

- OSC positions are **control data**, not audio samples. Direct PCM bypasses Unreal
  attenuation/submix processing and is not automatically spatialized through ADM.
- Output channel 0 maps corresponding source/device channels. 1..N sends the **first
  source channel only** to the selected device output.
- UDP is unacknowledged and unauthenticated. Scene discovery defaults to loopback;
  remote queries require explicit trusted-network opt-in. Snapshot state is not a
  device-presence, packet-delivery or physical audio confirmation.
- For responsive unfocused Unreal updates, disable **Use Less CPU when in
  Background** in Editor Preferences. This costs more CPU/GPU/power.
- Stop All is not a safety-rated emergency mute. Keep an independent system mute.
- Mac/Linux hardware, manufacturer compatibility, physical channels/fidelity,
  exclusive-mode/latency measurements and actual cooked-media playback remain
  separate verification gates. Shipping compilation does not certify those.

## Validation

See `VALIDATION.md` in the source archives for exact evidence and exclusions.
UE 5.8 Win64 Editor/Development/Shipping builds and 29 native tests passed. The
room example passed 13 silent PIE checks, including actual capsule passage through
all doors and collision against hall/divider walls, plus 21 static layout traces.
Controller: 35 tests; Node clients/wrappers: 4 tests. Live discovery passed in Editor,
PIE and paused PIE through both inputs, with no audio started. Max/Node-RED host
APIs were stub-tested; their actual application UIs are not certified by Node tests.

## Verify a download

```powershell
Get-FileHash .\SpatialFabric-2.1.0.zip -Algorithm SHA256
```

Compare with `SHA256SUMS.txt` and the artifact entry in `RELEASE_MANIFEST.json`.
On Unix-like systems, `sha256sum -c SHA256SUMS.txt` verifies the full downloaded set.
Keep the exact archive/checksum and source commit with your show-system records.
