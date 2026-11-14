![SpatialFabric and AudioLoom, woven together](../assets/spatialfabric-unified.png)

# SpatialFabric 2 audio playback

Install and enable **SpatialFabric** and the built-in **OSC** plugin only. Do not
also install standalone AudioLoom: it defines the same Unreal modules/classes.

## Setup

1. Open **Window → Spatial Fabric**, then select **Playback**.
2. Place an **Audio Loom** actor, or add an **Audio Loom** component to an actor.
   Legacy class/display names remain to preserve serialized assets and Blueprints.
3. Assign a SoundWave, output device, and channel. Channel `0` maps corresponding
   source/device channels; `1..N` routes the first source channel to one output.
4. Use Play/Stop, Blueprint transport functions, or OSC triggers.

Playback uses WASAPI on Windows and Core Audio on macOS, outside Unreal's mixer.
It does **not** automatically spatialize PCM when the same actor is routed to ADM.
A Spatial Fabric Manager and Stage Volume are not required for direct playback.

## Assets, limits, and packaged playback

- In editor, imported PCM is decoded and cached per component/asset. Call
  **Prepare Audio** before timing-critical cues to pay the first decode cost ahead
  of time. First decoding is synchronous; repeats reuse the cache. Reimport and
  sound-property changes invalidate it.
- Packaged SoundWaves must use **Loading Behavior → Force Inline**, then be recooked.
  Inline cooked data is decoded through Unreal's registered codec factory. Streaming
  assets are explicitly rejected; the plugin does not block waiting on streaming chunks
  or silently search for a sibling WAV. Keep a referenced asset in cooked content.
- Multichannel Vorbis-order data is rejected rather than silently assigning the wrong
  physical channels. Use PCM/ADPCM or another supported device-order codec for that case.
- WAV parsing supports little-endian PCM16/24/32 and float32, including validated
  extensible headers with full-width valid bits. It checks RIFF boundaries, padding,
  whole frames, finite samples, rates (8–384 kHz), and channels (1–64).
- Decoded float PCM is capped at 256 MiB per component. Large files should be split;
  this is not an unlimited streaming player. `Get Last Playback Error` reports backend
  failures; decode-policy failures are also logged with the asset name.

## Device behavior

- PCM is interpolated at the **actual device rate**, not assumed to be 48 kHz.
- Windows supports negotiated float32 and integer16/24/32 (including valid-bit packing).
  Unsupported encodings/layouts fail with a diagnostic.
- Output channel must exist in the opened device mix layout; the dropdown describes
  that layout rather than an unrelated exclusive-mode capability probe.
- Missing explicitly selected devices **fail closed**. An empty device ID alone selects
  the default endpoint. Do not depend on silent fallback after disconnecting an interface.
- Windows exclusive initialization may fall back to shared mode. Windows one-shots
  drain queued frames before natural completion. Stop is an explicit cancellation.
- Loop changes propagate to active streams. Explicit Stop cancels pending auto-replay;
  zero replay delay is supported. EndPlay and component unregistration stop/join workers.
- Play/stop monitoring comes from actual worker stream events, including natural
  completion/failure after a successful start, rather than unconditional Play requests.
- macOS validates float32 virtual stream formats and consistent stream rates. Other
  encodings are rejected. Real Mac device/layout testing remains required.

## OSC configuration

**Project Settings → Plugins → Spatial Fabric Playback OSC**:

- Listen IP: **127.0.0.1**, port `9000` by default; the server starts on request.
- Enable In Editor: true; Enable In Packaged Builds: **false** until explicitly enabled.
- Monitoring: `127.0.0.1:9001` by default. Active clients pick up destination changes.
- Set Listen IP to `0.0.0.0` only for a trusted/firewalled remote control network.
  OSC has no authentication. Do not share the spatial server's receive port.

Commands: `<base>/play` (address-only, float >=0.5, or integer >=1), `<base>/stop`,
and `<base>/loop` (1/0). Outgoing `<base>/state` is float 1/0 for actual playback.
Registries refresh without an editor panel; address changes also invalidate immediately.
The editor module transfers an active playback listener across editor/PIE transitions.

Spatial positioning remains independent: its default loopback listen port is `8100`,
and ADM output is `50018`. Its permissions are in Spatial Fabric project settings.

## Migration from standalone AudioLoom / SpatialFabric 1.x

1. Close Unreal, back up the project, and remove standalone `Plugins/AudioLoom`
   (including engine-level copies). Remove its `.uproject` plugin entry.
2. Install combined `Plugins/SpatialFabric`; enable SpatialFabric and OSC.
3. Clear stale generated Binaries/Intermediate as appropriate and rebuild.
4. Review receive IP/permissions, cooked Loading Behavior, and OSC addresses below.

`AudioLoom`/`AudioLoomEditor` module names, `/Script/AudioLoom` class paths, C++ module
references, CSV columns, and `[/Script/AudioLoom.AudioLoomOscSettings]` in Engine config
remain unchanged; no class redirects are needed. The hidden legacy `AudioLoomPanel`
tab is retained for saved layouts/C++ callers.

**Intentional v2 behavior changes:**

- Root-owned components keep `/audioloom/<root-label>/<index>` defaults. Descendant
  owners now use `/audioloom/<root-label>/<owner-name>/<index>` to avoid the old
  hierarchy collisions. Use explicit OSC overrides for persistent external show files.
- Default receive binding is loopback rather than all interfaces. Packaged networking
  requires opt-in in each subsystem's settings.
- Missing selected devices no longer fall back to the default output.
- ADM Cartesian/AED output uses the same unit-sphere position. Cube corners outside
  radius 1 are radially limited consistently in both encodings; no sqrt(3) distance
  scaling is applied only to AED.
- Listener following changes the coordinate basis, **not the authored stage transform**.
  Listener-relative object coordinates are accompanied by an identity listener pose to
  avoid applying orientation twice. The debug box follows the virtual basis.
- Switching spatial formats preserves existing target entries/overrides/custom fields.

AudioLoom's MIT notice is in `ThirdParty/AudioLoom/LICENSE`.
See `FIXES.md` and `Tools/validate_unreal.py` for validation scope and remaining gates.
