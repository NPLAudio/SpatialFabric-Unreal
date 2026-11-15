# Showcase tour, media refresh and live checks

Open **`/Game/SpatialFabricDemo/Maps/L_SpatialFabric_Showcase`** in the
[published example project](../Examples/SpatialFabricShowcase/README.md) or the
local development SpatialSandbox. The actual project/assets and original WAVs are
now versioned under `Examples/SpatialFabricShowcase`; prepare a portable copy with
`Tools/prepare_example.py`. The local sandbox remains independent and is not copied
wholesale. Reproducible generators/inspection tools are in `Tools/Unreal/`.

For setup, start with [Getting started](GettingStarted.md). Older talk/deck materials
remain in the [2.0.3 archive](https://github.com/NPLAudio/SpatialFabric/releases/tag/v2.0.3);
this room tour supersedes their open-gallery walkthrough.

## Tour

1. Open **Window → Spatial Fabric**; give the panel enough vertical space.
2. Press **Play**. Use **WASD + mouse** to walk (**Space** to jump); **Shift+F1** releases the mouse
   so you can use the editor panel while PIE continues.
3. Start in the hallway. **Room 1** contains spatial motion; **rooms 2 / 3 / 4**
   contain mono percussion, stereo groove and replay. Each has its own doorway and
   solid partitions; no large instruction panel stands in front of another exhibit.
   **Objects / Radar / Output:** three colored objects animate on a 30-second
   repeating sequence; a fourth is a stationary reference. Their independent
   routes use ADM IDs **1–4** and Custom IDs **101–104**.
4. **Stage:** defaults to **Off / stage-fixed**, so hallway walking does not clamp
   the room's objects against the front edge. Optionally switch **Follow Position & Orientation** on to express coordinates
   relative to the player/view-target actor. The authored stage does not move.
   With no local player, the explicit `SF_Listener_Rig` is the fallback listener.
   This mode follows the view-target **actor origin**, not the camera-eye offset.
5. **Playback:** expand `SF_Playback_1`, `_2`, or `_3` and press **Play / Stop**.
   Audio is deliberately **not** started by entering PIE. Original procedural sounds
   peak around -24 dBFS or below; choose a safe device/channel before listening.

| Station | Content | Route | Behavior |
|---|---|---|---|
| `SF_Playback_1` | 1.2 s warm wood/percussion hit with short room reflections | Output channel 1 | One-shot |
| `SF_Playback_2` | 4 s / 120 BPM stereo groove: drums, bass, chords and ping-pong plucks | Corresponding source/device channels (`0`) | Loop; toggle Loop off to drain naturally |
| `SF_Playback_3` | 0.6 s double glass/mechanical tap with a resonant tail | Output channel 2 | Auto Replay, 2.5 s delay; Stop cancels replay |

Device IDs are empty, meaning the OS default output. That endpoint must expose
channel 2 for station 3. Direct PCM playback is **not spatialized by ADM-OSC**:
connect an external renderer/audio routing system if you want those pipelines linked.
Use Refresh, device/channel controls, and CSV import/export in the Playback panel.
The full hardware and undo/redo matrix remains a release gate in `VALIDATION.md`.

The compact building has a continuous collision foundation, four enclosed rooms,
a **5.5 m wide hallway**, clear 2–3 m doorways and blocking outer/divider walls.
The room layout verifies **10 floor, 4 doorway and 7 wall traces** before saving.
Tracked spheres are visual control objects, not moving collision obstacles.

Decorative speaker cabinets/plinths, the billboard/logo screen, floating object
labels, duplicate axis labels and the oversized 120 m outdoor floor are removed.
Only room names, short real-control instructions and two useful stage axes remain.
Walls provide **visual separation**, not acoustic isolation: direct PCM playback
still bypasses Unreal attenuation/occlusion and does not become room-spatialized.

`Tools/Unreal/rearrange_spatialfabric_demo_rooms.py` updates only the room layout,
preserves cue config and ADM/Custom routes/sequence, refuses unsaved edits/PIE/active
audio, and backs up the map. The old outdoor-floor extender now refuses room maps.

The stage box stays visible in the editor even in **Game View** and without
selection. **Hide stage volume when game is running** only hides it in PIE/game;
uncheck it to show the stage during play. This setting never moves the stage.

## OSC endpoints

All demo destinations are local; nothing needs a LAN receiver to run the level.

| Purpose | Endpoint / address |
|---|---|
| ADM spatial output | `127.0.0.1:50018`, `/adm/obj/1..4/...` (XYZ, AED and metadata) |
| Custom spatial output | `127.0.0.1:9002`, `/demo/main-stage/101..104/xyz` |
| Spatial feedback input | `127.0.0.1:8100` |
| Playback commands | `127.0.0.1:9000`; press **Start OSC** in Playback first |
| Playback monitoring | `127.0.0.1:9001`, `/demo/playback/<station>/state` |

Playback bases are `/demo/playback/mono`, `/demo/playback/stereo`, and
`/demo/playback/replay`. Append `/play`, `/stop`, or `/loop`.
Play accepts an address-only message, float >= 0.5, or int32 >= 1; `/loop` is
normally controlled with numeric 0/1. Monitoring emits float 1/0 for actual
worker start/stop. Socket handoff transfers a running editor listener to PIE
and restores it after Stop PIE.

**2.0.1 receiver policy:** exact IPv4 unicast binding, including real loopback.
Unlike UE 5.8's stock receiver, `127.0.0.1` is not rewritten to a LAN interface.
Supported input tags are `i/f/s/b/h/T/F`; malformed/unsupported datagrams are
rejected, and nested bundles are bounded and dispatched immediately (not scheduled).
Core-ticker reception remains responsive while the game is paused or a viewport
is not realtime; it does not require a ticking actor/component.
OSC is unauthenticated. Do not expose receive ports or the editor MCP endpoint
to untrusted networks.

## Rebuild the level

Requirements: SpatialFabric and OSC enabled in the project, UE 5.8, the **Python
Editor Script Plugin**, and editor scripting/import/sequencer facilities. The
published example enables these and supplies its own original C++ walking game mode.
The local SpatialSandbox instead uses its pre-existing FirstPerson game mode.
The builder does not change project defaults or the existing FirstPerson map.
Other projects need a playable pawn/game mode for the walking tour.

Save unrelated edits first. In **Output Log → Cmd**, run this file's absolute path:

```text
py "C:/dev/SpatialFabricShowcase/Plugins/SpatialFabric/Tools/Unreal/build_spatialfabric_demo.py"
```

The builder replaces generated assets under `/Game/SpatialFabricDemo`, regenerates
actors tagged `SF_DemoGenerated`, and preserves untagged actors. Treat the generated
namespace as owned by the builder: copy assets elsewhere before customizing them.
Replace the example absolute path with your actual prepared project/plugin location.
It writes raw WAV sources to `<project>/SourceAssets/SpatialFabricDemo/Audio/`
and a build report to `<project>/Saved/SpatialFabricDemo/build_report.json`.
The room scene contains **66 generated actors and 11 text actors** (previously
90/17). The published dependency-closed example contains **13 Unreal packages**:
eight materials, three Force Inline SoundWaves, sequence and map, plus three WAV
sources. Unused banner/brand-texture/driver-material packages are not distributed.
Unowned sandbox actors are not deleted locally or silently included in the published
project; publication reviews/removes the unrelated outside plane in a disposable copy.

To publish a saved room map safely from a development sandbox, run ordinary Python:

```powershell
python Tools/publish_room_example.py --engine "C:/Program Files/Epic Games/UE_5.8" --sandbox C:/dev/UnrealSpatial/SpatialSandbox
```

This builds a disposable copy, removes only the reviewed unbound outside primitive,
checks dependency closure and performs the silent walking-pawn smoke before replacing
versioned example assets and their manifest. It never edits the original sandbox.

### Refresh just the sounds

The original procedural sound designs are in `Tools/Unreal/demo_audio.py`.
The first station uses a warmer, damped percussion hit rather than a synthetic
rising cinematic sweep. The sounds use no downloaded recordings, retain the existing asset names/durations/channels,
and are peak-limited to 0.06 (about -24.4 dBFS). The stereo loop has deliberately
different left/right instrumentation and short seam fades.

To regenerate/reimport audio without rebuilding the stage or changing routing,
stop PIE/playback, save edits, and run `Tools/Unreal/refresh_spatialfabric_demo_audio.py`
from Unreal's Cmd console using its absolute path. It refreshes the three existing
SoundWaves, invalidates prepared PCM caches, updates station titles, saves the
level, and writes `Saved/SpatialFabricDemo/audio_refresh.json`. No sound is started.

## Repeat the live checks

The following are Unreal Python files; run them from the same Cmd console using
absolute paths. **`validate_spatialfabric_demo.py` intentionally plays quiet cues.**

1. Run `Tools/Unreal/inspect_spatialfabric_demo.py` in the editor.
2. Run `Tools/Unreal/begin_spatialfabric_handoff_check.py` to temporarily start
   playback OSC and record its initial state.
3. Start PIE, then run `Tools/Unreal/validate_spatialfabric_demo.py`. Wait roughly
   20–30 seconds and check `Saved/SpatialFabricDemo/runtime_validation.json` says
   `"status": "passed"`. Background editor throttling is allowed for in the timing.
4. Optionally run the inspector again while PIE is active.
5. Stop PIE, wait a tick, then run `Tools/Unreal/finish_spatialfabric_handoff_check.py`
   to check editor restoration, stop streams, and restore the original OSC state.

The validator intentionally requests a nonexistent device once; the associated
Output Log error is expected and is checked for fail-closed behavior.

For independent packet evidence, start this **ordinary Python** command from the
project root **before** starting PIE:

```sh
python Plugins/SpatialFabric/Tools/Unreal/capture_spatialfabric_osc.py --output Saved/SpatialFabricDemo --duration 90
```

It refuses occupied UDP ports rather than sharing/stealing a receiver's socket.
Close another receiver on ports 50018, 9002 and 9001 first, or inspect packets in
that receiver instead. Creating `Saved/SpatialFabricDemo/stop_capture.flag` ends
capture early. JSONL packets and a summary record coordinates, IDs, state edges,
non-finite/malformed data, and XYZ/AED radius consistency. Repeated captures
replace these output files; copy evidence elsewhere if it must be retained.

### Observed validation

- The room layout passed 21 static floor/door/wall traces, preserved all three cue
  configurations, four bindings and adapter configs, and retained the same motion sequence.
- The published room example's silent PIE smoke covers its own walking pawn, decoding,
  animation, hallway landing, all four doorway sweeps, divider and outer/end-wall blocks.
  No audio is played by that test; see `VALIDATION.md` for current evidence.
- Earlier open-gallery nine floor/boundary traces and the editor/Game View stage check passed.
- 24 live PIE checks passed in the development sandbox: prepared/manual-start assets, numeric/address-only
  OSC triggers, actual WASAPI worker playback, natural EOF, live loop exit,
  replay/cancellation, missing-device fail-closed behavior, moving objects,
  virtual listener coordinates, no authored stage motion, and OSC play/stop while
  the game world is paused (then restored to unpaused).
- Five post-PIE checks passed, including editor socket restoration and exact
  loopback. No audio remained playing; the initially stopped playback OSC
  listener was stopped again after validation.
- A local UDP capture received **15,458 messages** across ADM, Custom and playback
  monitoring, with no decode or XYZ/AED consistency failures. ADM IDs 1–4 and
  Custom IDs 101–104 were distinct; replay monitoring showed two start/stop pairs.
- These are software/device-lifecycle checks, **not listening/capture verification**
  of physical channels, audio fidelity, latency, exclusive mode, other devices,
  other platforms, or packaged/cooked playback. The backend reported 0 ms in this
  run; that is unavailable diagnostic data, not a measured zero-latency result.

## Asset provenance

No third-party asset downloads were needed.

- Geometry: references to `/Engine/BasicShapes`, supplied with Unreal Engine;
  governed by Epic's applicable Unreal Engine terms, not relicensed as MIT here.
- Published example pawn/game mode: original minimal C++ source, repository MIT.
  The development SpatialSandbox's pre-existing Epic FirstPerson template content
  is not copied into the published example.
- Materials, animation, labels and original sound designs: generated locally;
  no third-party audio recordings or sample-library licenses.
- Unified brand texture: this repository's `assets/spatialfabric-unified.png`;
  original SpatialFabric/AudioLoom artwork and attribution are documented in
  [`assets/README.md`](../assets/README.md).
- Builder/tool source: repository MIT license. The published `.umap`/`.uasset`
  snapshot and matching raw WAVs are included under `Examples/SpatialFabricShowcase`;
  see its inventory and asset-license notice. Saved reports, caches, binaries and
  local MCP configuration are not committed. Do not treat Unreal engine references
  as standalone MIT-licensed engine content.
