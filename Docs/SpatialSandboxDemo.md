# SpatialSandbox showcase

Open **`/Game/SpatialFabricDemo/Maps/L_SpatialFabric_Showcase`** in SpatialSandbox.
The local map/assets live outside this plugin's Git repository. The reproducible
builder and inspection tools **are** distributed in `Tools/Unreal/`.

## Tour

1. Open **Window → Spatial Fabric**; give the panel enough vertical space.
2. Press **Play**. Use **WASD + mouse** to walk; **Shift+F1** releases the mouse
   so you can use the editor panel while PIE continues.
3. **Objects / Radar / Output:** three colored objects animate on a 30-second
   repeating sequence; a fourth is a stationary reference. Their independent
   routes use ADM IDs **1–4** and Custom IDs **101–104**.
4. **Stage:** switch **Follow Position & Orientation** on to express coordinates
   relative to the player/view-target actor. The authored stage does not move.
   With no local player, the explicit `SF_Listener_Rig` is the fallback listener.
   This mode follows the view-target **actor origin**, not the camera-eye offset.
5. **Playback:** expand `SF_Playback_1`, `_2`, or `_3` and press **Play / Stop**.
   Audio is deliberately **not** started by entering PIE. Quiet generated tones
   peak around -24 dBFS or below; choose a safe device/channel before listening.

| Station | Content | Route | Behavior |
|---|---|---|---|
| `SF_Playback_1` | 1.2 s mono cue | Output channel 1 | One-shot |
| `SF_Playback_2` | 4 s stereo, alternating 440/660 Hz emphasis | All output channels (`0`) | Loop; toggle Loop off to drain naturally |
| `SF_Playback_3` | 0.6 s mono cue | Output channel 2 | Auto Replay, 2.5 s delay; Stop cancels replay |

Device IDs are empty, meaning the OS default output. That endpoint must expose
channel 2 for station 3. Direct PCM playback is **not spatialized by ADM-OSC**:
connect an external renderer/audio routing system if you want those pipelines linked.
Use Refresh, device/channel controls, and CSV import/export in the Playback panel.
The full hardware and undo/redo matrix remains a release gate in `VALIDATION.md`.

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
local SpatialSandbox already has these enabled and uses its FirstPerson game mode.
The builder does not change project defaults or the existing FirstPerson map.
Other projects need their own playable pawn/game mode for the walking tour.

Save unrelated edits first. In **Output Log → Cmd**, run this file's absolute path:

```text
py "C:/dev/UnrealSpatial/SpatialSandbox/Plugins/SpatialFabric/Tools/Unreal/build_spatialfabric_demo.py"
```

The builder replaces generated assets under `/Game/SpatialFabricDemo`, regenerates
actors tagged `SF_DemoGenerated`, and preserves untagged actors. Treat the generated
namespace as owned by the builder: copy assets elsewhere before customizing them.
It writes raw WAV sources to `SpatialSandbox/SourceAssets/SpatialFabricDemo/Audio/`
and a build report to `SpatialSandbox/Saved/SpatialFabricDemo/build_report.json`.
The saved level contains 85 generated actors and 15 Unreal assets, including nine
materials, three Force Inline SoundWaves, the brand texture, sequence, and map.

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

- 24 live PIE checks passed: prepared/manual-start assets, numeric/address-only
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
- FirstPerson pawn/input/game mode: existing SpatialSandbox template content;
  retain Epic's applicable template/content terms.
- Materials, animation, labels and test tones: generated locally by the builder;
  no third-party audio recordings or sample-library licenses.
- Unified brand texture: this repository's `assets/spatialfabric-unified.png`;
  original SpatialFabric/AudioLoom artwork and attribution are documented in
  [`assets/README.md`](../assets/README.md).
- Builder/tool source: repository MIT license. Generated `.umap`/`.uasset` files,
  raw WAVs, Saved reports, and local MCP configuration are not plugin Git content.
