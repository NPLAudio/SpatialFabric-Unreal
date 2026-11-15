# SpatialFabric Showcase — playable source example

## What is included

- `Content/SpatialFabricDemo/Maps/L_SpatialFabric_Showcase.umap` and its 14 companion
  packages: materials, brand texture, animation sequence and three SoundWaves.
- `SourceAssets/SpatialFabricDemo/Audio`: original PCM16/48 kHz WAV sources.
- A small original C++ walking character/game mode; **no FirstPerson Blueprint,
  character mesh, animation pack, input asset or private show-media dependency**.
- A 120 × 120 m outer floor, 3.5 m perimeter barriers, four spatial objects,
  listener rig and manual-start percussion/stereo groove/glass cue stations.
- Minimal reviewed configuration; no copied sandbox token, credentials or MCP server.

The asset inventory/hashes are in `ASSET_MANIFEST.json`; attribution and applicable
content terms are in `ASSET_LICENSES.md`.

## Recommended setup: prepare a portable copy

Clone/download the complete SpatialFabric repository. From its root:

```powershell
python Tools/prepare_example.py --destination C:/dev/SpatialFabricShowcase
```

The destination must be new/empty and outside the repository. It contains this
project and a project-local `Plugins/SpatialFabric`, without recursive Examples,
presentation files or generated caches. No symlink or local SpatialSandbox is used.

Use UE **5.8** and a supported Visual Studio 2022 C++ toolchain/Windows SDK/.NET
Framework SDK. Build the example Editor target, or generate project files and build
in Visual Studio:

```powershell
& "C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" `
  SpatialFabricShowcaseEditor Win64 Development `
  -Project="C:/dev/SpatialFabricShowcase/SpatialFabricShowcase.uproject" -WaitMutex -NoLiveCoding
```

Then open `SpatialFabricShowcase.uproject`. Let first shader compilation finish.
Its startup/default map is the showcase. This is source, not a universal prebuilt
application; do not reuse arbitrary DLLs from another engine build.

**Direct repository opening:** the checked-in descriptor searches `../../..`
(the repository's parent) for the plugin. Unreal discovers plugins in subfolders
of each search directory. Keep the full repository layout. If the engine/project
setup cannot resolve that path, prepare the portable copy instead. Moving only this project folder without
its plugin or changing the relative layout requires an explicit plugin installation.

## Operate it

- **Play**, click the viewport if needed; **WASD + mouse**, **Space** to jump.
- **Shift+F1** releases the mouse for editor controls. Stop PIE when finished.
- Open **Window → Spatial Fabric** and enlarge the panel if controls are clipped.
- **Objects / Radar / Output:** ADM IDs 1–4; Custom IDs 101–104. Three objects animate;
  the fourth is a fixed-world reference.
- **Stage:** Follow Position & Orientation changes a virtual basis, not the authored
  stage. It follows the player actor origin/rotation, not verified HMD pose.
- **Playback:** select a safe device/channel, then Play/Stop one station at a time.
  Audio is **off until requested**. The quiet sources peak near -24.4 dBFS.

| Station | Channel | Behavior |
|---|---|---|
| `SF_Playback_1` — warm percussion | 1 | 1.2 s one-shot |
| `SF_Playback_2` — stereo groove | 0 | 4 s / 120 BPM loop, corresponding source/device channels |
| `SF_Playback_3` — glass/mechanical | 2 | 0.6 s cue; 2.5 s auto-replay delay |

An empty device ID selects the OS default. Channel 2 must actually exist for the
third station. Channel 0 does not broadcast mono to all outputs; selected 1..N uses
the first source channel only. Stop cancels replay; Loop off allows natural drain.

The example does not patch audio into an external spatial renderer. Moving spheres
send control data; the separate groove does not automatically pan with them.

## Network defaults

All destinations/listeners use loopback: ADM output 50018, Custom output 9002,
spatial input 8100, playback commands 9000 (Start OSC on request), state output 9001.
Close another editor/listener that owns the same input endpoint. Remote OSC requires
explicit trusted-network/firewall configuration; it has no authentication.

Spatial settings: `Config/DefaultSpatialFabric.ini`. Playback OSC:
`Config/DefaultEngine.ini`. Packaged networking is disabled for each independently.
MCP/editor toolset plugins are explicitly disabled in this project. They are optional
local development tools, not a dependency of the playable example.

## Editing / rebuilding / verification

Treat `Content/SpatialFabricDemo` as a generated namespace. Copy assets elsewhere
before making custom show edits you do not want a generator to overwrite.
The plugin's `Tools/Unreal` scripts regenerate the level, refresh only audio, or
update just floor protection. Do not move Unreal packages with a file manager.

From the repository root, a silent, disposable build/PIE test is available:

```powershell
python Tools/validate_example.py --engine "C:/Program Files/Epic Games/UE_5.8"
```

It checks its original walking pawn, map/bindings, three decoded Force Inline cues,
no autoplay, animation, actual landing on the outer floor and a perimeter-blocked
capsule. It does not play audio. The runtime/plugin and physical/cooked-media tests
are separate; do not interpret the example smoke test as hardware certification.

Further guides in the full repository:

- [Operator tour and live checks](https://github.com/NPLAudio/SpatialFabric/blob/main/Docs/SpatialSandboxDemo.md)
- [Audio patching / CSV / migration](https://github.com/NPLAudio/SpatialFabric/blob/main/Docs/AudioPlayback.md)
- [OSC reference](https://github.com/NPLAudio/SpatialFabric/blob/main/Docs/OSCReference.md)
- [Troubleshooting](https://github.com/NPLAudio/SpatialFabric/blob/main/Docs/Troubleshooting.md)
- [15-minute talk + 10-minute demo deck](https://github.com/NPLAudio/SpatialFabric/tree/main/Presentations)

In a prepared copy, the same operator guides are also under
`Plugins/SpatialFabric/Docs`. Core code/tools are MIT; Unreal content references
remain subject to Epic's applicable terms. See `ASSET_LICENSES.md`.
