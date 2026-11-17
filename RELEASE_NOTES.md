# SpatialFabric 2.2.0 — Unreal

Consistent command acknowledgements and observed playback feedback across Unreal,
Unity and Godot. The Unreal plugin retains its AudioLoom module/class identities
and beta descriptor. [Unity](https://github.com/NPLAudio/SpatialFabric-Unity) and
[Godot](https://github.com/NPLAudio/SpatialFabric-Godot) are released from their own
repositories. The former Unreal `SpatialFabric` repository is now
[SpatialFabric-Unreal](https://github.com/NPLAudio/SpatialFabric-Unreal); the former
`SpatialFabric-Interop` is now the [SpatialFabric shared hub](https://github.com/NPLAudio/SpatialFabric).

**OSC Controller 1.4.0** is released separately in the
[hub 1.0.0 release](https://github.com/NPLAudio/SpatialFabric/releases/tag/v1.0.0):
`SpatialFabricOSCController-1.4.0-Windows-x64.zip` is the portable unsigned Windows
controller, debugger and silent engine simulator, with runtime license notices.
No Python installation is required. Its source now lives in the hub's
[`Apps/OSCController`](https://github.com/NPLAudio/SpatialFabric/tree/main/Apps/OSCController),
not in the Unreal complete-source archive.

## Unreal downloads

Download these from the [Unreal 2.2.0 release](https://github.com/NPLAudio/SpatialFabric-Unreal/releases/tag/v2.2.0).

| Artifact | Use |
|---|---|
| `SpatialFabric-2.2.0.zip` | Source plugin. Extract into `YourProject/Plugins/SpatialFabric` and build with your matching Unreal toolchain. |
| `SpatialFabric-Example-2.2.0.zip` | Standalone four-room C++ showcase with assets, original quiet WAVs and a project-local source plugin. |
| `SpatialFabric-2.2.0-CompleteSource.zip` | Unreal-specific source, example, OSC query clients, documentation and tests; no controller source or executable. |
| `RELEASE_MANIFEST.json`, `SHA256SUMS.txt` | Source commit, artifact sizes and SHA-256 checksums. |

Source archives exclude Unreal build caches, private logs and local MCP settings.
The historical presentation remains in [Unreal release 2.0.3](https://github.com/NPLAudio/SpatialFabric-Unreal/releases/tag/v2.0.3); it is not restored here.

## Changes

- `<base>/ack ,ssis command result playing detail`: received/ignored/rejected/failed
  feedback to the configured monitor destination (default 9001), usable by any OSC client.
- Address-only `<base>/status` returns an acknowledgement and current observed state.
  A received play request is not proof the device started; real starts, failures,
  stops and natural endings report their observed outcome separately.
- Unreal: truthful starting-versus-playing state, asynchronous start failures,
  paused/non-realtime-world polling, UTF-8 monitor packets, nested unknown suffix
  rejection, safe registry snapshots and live teardown restart protection.
- Controller: acknowledgement and observed-state columns, Status button, readonly
  playback/live cue Type dropdown, pending-send timeout tracking and improved debugger
  and simulator support. Monitor startup and unrelated feedback cannot erase or
  resurrect unresolved-command warnings.
- Shared default ports: spatial input 8100, playback input 9000, monitor 9001,
  ADM 50018 and Custom 9002. Configure distinct ports when running multiple engines.

## Use

Close Unreal and back up the project before upgrading. Remove a duplicate standalone
AudioLoom plugin if present. Enable SpatialFabric and Unreal's built-in OSC plugin,
then compile against the installed engine. These are not universal precompiled DLLs.

In Unreal, explicitly start OSC; in the controller, load the matching engine profile
or query/import its cues, then start monitoring on 9001. Send commands to the exact
cue base plus `/play`, `/stop`, `/loop`, `/gain` (live only), or `/status`.
Unknown cue bases receive no reply. Example sources are quiet and audio is manual-start.

## Validation and boundaries

- UE 5.8 Win64 Editor build and **35 native tests** passed with zero failed/skipped,
  in an isolated NullRHI/NoSound process. Includes 34 shared command decisions,
  seven golden packets, lifecycle, failure, status and teardown regressions.
- Controller: **79 tests**, 77 passed and two optional real-engine skips, with GUI
  tests enabled. Packaged startup and silent simulator UDP round trips passed.
- Release tooling: **33 Python tests** passed. See `VALIDATION.md` for full scope.
- Earlier Development/Shipping and example checks are historical evidence, not
  revalidation of this release's hardware, cooked media or other platforms.
- No audible playback or real capture was initiated during automated verification.
  The operator subsequently reported the interactive playback issues resolved.
- UDP acknowledgements remain best effort, not reliable transport. OSC is
  unauthenticated: use trusted networks. Observed software state is not proof of
  physical sound, device presence or packet delivery.
- Direct PCM bypasses Unreal attenuation/submixes; OSC positions are control data,
  not spatialized audio. Channel 0 maps corresponding channels; 1..N routes the
  first source channel to the chosen output. Keep an independent hardware mute.
- macOS/Linux builds, physical channel/fidelity, latency and device compatibility
  have not been validated for this release. The Windows executables are unsigned.

## Verify

```powershell
Get-FileHash .\SpatialFabric-2.2.0.zip -Algorithm SHA256
```

Compare with `SHA256SUMS.txt` and `RELEASE_MANIFEST.json`. Preserve all runtime
license notices when redistributing the controller.
