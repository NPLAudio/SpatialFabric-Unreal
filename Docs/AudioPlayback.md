![SpatialFabric / AudioLoom](../assets/spatialfabric-unified.png)

# Direct audio playback, routing and migration

[Documentation index](README.md) · [OSC reference](OSCReference.md) · [Troubleshooting](Troubleshooting.md)

## A cue player, not Unreal's spatial audio mixer

Place an **Audio Loom** actor or add its component, assign a SoundWave, and use
**Window → Spatial Fabric → Playback**. Legacy AudioLoom class/display names are
intentional: serialized `/Script/AudioLoom` references remain valid.

PCM goes directly to WASAPI on Windows or the implemented Core Audio backend on
macOS. It does not automatically traverse Unreal AudioComponent attenuation,
SoundClass/submix processing or the external ADM renderer. A spatial Manager,
Stage Volume, or OSC server is not required to play a cue locally.

Mac code requires its own native/device validation; Windows results are not Mac
certification. Linux direct-device playback is not certified.

## Patch channels deliberately

| Setting | Actual behavior |
|---|---|
| Device ID empty | Open the OS default output endpoint |
| Explicit Device ID | Open that endpoint or fail; no silent default fallback |
| OutputChannel `0` | Source channel 1 → device channel 1, source 2 → device 2, etc. |
| OutputChannel `1..N` | Route **source channel 1 only** to that one device channel |

Unmapped device channels are zeroed. If the device exposes fewer channels than
the source, corresponding mapping does not create extra physical outputs. There
is no automatic downmix or mono broadcast. Selecting output 2 on a stereo source
routes its **left/first channel to output 2**, not its right channel. Use an
appropriate mono file when extracting one source for an isolated speaker.

The dropdown describes the opened/mix layout, not an unrelated exclusive-mode
capability list. Confirm numbering against real interface outputs at low level.
A device ID is host/driver specific; a routing CSV copied between machines needs
review. Default output can change with an OS preference or newly connected device.

## Transport and preparation

- **Play** stops a prior instance/pending replay, prepares/caches PCM, then requests
  asynchronous stream initialization. Check actual playing state/error, not merely
  that the request function returned.
- **Prepare Audio** decodes before a timing-critical cue. First decode is synchronous;
  repeats share immutable cached PCM. Reimport/asset edits invalidate the cache.
- **Stop** cancels playback and any pending auto-replay, including zero-delay cases.
- **Loop** updates the active stream. Turning it off allows natural completion and
  queued-tail drain rather than requesting an abrupt transport Stop.
- **Auto Replay** applies after natural completion when Loop is off. Delay can be
  fixed or random; random min/max are sanitized. Delays use component/world ticking,
  not a sample-clock cue scheduler. World pause can pause replay timing even though
  OS audio and core-ticker OSC control are separate.
- **Begin Play**/autoplay is opt-in. It is off on all example stations.
- **EndPlay / unregister** stop and join the backend; deleting actors/ending PIE
  should not leave an orphaned playback worker.

Useful reflected functions: `Play`, `Stop`, `PrepareAudio`, `SetLoop`, `GetLoop`,
`IsPlaying`, `GetLastPlaybackError`, `GetOutputLatencyMs`, `SetDeviceId`,
`SetOutputChannel`, `GetOscAddress`, `SetOscAddress`, `UpdateTickEnabled`.
Routing/playback properties are also exposed for editor/Blueprint use. Do not infer
Blueprint availability for every public C++ helper in the source.

## Format and memory policy

Editor imports are decoded from the SoundWave's imported data. WAV parsing supports
little-endian PCM16/24/32 and float32, including validated extensible full-width
formats, chunk padding, complete interleaved frames, finite samples, 8–384 kHz and
1–64 channels. Unsupported encodings such as arbitrary float64 are not silently
reinterpreted. Decoded float PCM is capped at **256 MiB per component**; this is an
in-memory cue player, not unlimited long-form streaming.

PCM is interpolated at the actual output-device rate to preserve duration/pitch
across source/device-rate differences. Windows writes negotiated float32 or
integer16/24/32, including valid-bit packing, and rejects unsupported formats.
Mac code checks float32 virtual layouts/consistent rates and pre-zeroes callback
buffers; native Mac compilation/device/layout testing is still a release gate.

## Packaged/cooked SoundWaves

1. Reference/include each required SoundWave in the packaged content.
2. Set **Loading Behavior → Force Inline** and recook after changing it.
3. Confirm the chosen cooked codec has a decoder registered in that build.
4. Run the actual packaged project and verify duration, channel order and error handling.

Streaming chunks are rejected instead of blocking/spinning while waiting for IO.
The plugin does not silently search for a sibling source WAV in a packaged build.
Multichannel Vorbis ordering is explicitly rejected rather than assigning channels
incorrectly. A supported device-order codec such as PCM/ADPCM is preferable for
that use, subject to your real codec/package test. Compilation of Shipping code
alone is not proof that your media cooked/decoded correctly.

## Latency, exclusive mode and diagnostics

Exclusive mode is a Windows request, not a universal promise; initialization can
fall back to shared mode. A buffer-size request is not the entire end-to-end delay.
First decode, OS device start, output queue, driver/converter and downstream routing
all contribute. The reported estimate can be **0 when unavailable**; never advertise
that as measured zero latency. Use appropriate audio-loopback/capture measurement.

`GetLastPlaybackError` distinguishes decode-policy/backend failures. Monitoring
comes from actual worker start/stop events, not unconditional Play requests.
Natural EOF drains queued frames; a failed explicit-device request must not quietly
play elsewhere. Independent hardware listening/capture, disconnect, exclusive-mode,
rate/layout and latency verification remain in [VALIDATION.md](../VALIDATION.md).

## OSC control and monitoring

Playback has its own **Start OSC / Stop OSC**, listen port and monitoring destination.
Defaults: incoming `127.0.0.1:9000`, state destination `127.0.0.1:9001`.
Configuration remains in `DefaultEngine.ini` under
`[/Script/AudioLoom.AudioLoomOscSettings]`.

Use `<base>/play` (no args, float >=0.5 or int32 >=1), `<base>/stop`, and numeric
`<base>/loop` (0/1). `<base>/state` is an outgoing float 1/0 lifecycle message,
not an acknowledgement that an audience heard a sound. Active listener ownership
is transferred between editor and PIE, then restored. Core-ticker command reception
works while the game is paused; this does not turn replay timing into a sample clock.
See [complete wire types and default-address rules](OSCReference.md#playback-transport).

## Routing CSV: export first, then edit

The current export header is:

```csv
ActorName,ActorLabel,ComponentName,ComponentIndex,SoundWave,DeviceId,OutputChannel,OscAddress,OscBase,OscPlay,OscStop,OscLoop,OscState
```

- Export the **actual current world** first; internal ActorName and ComponentName
  are not necessarily the labels displayed in the Outliner.
- ActorName is required; ActorLabel further disambiguates. ComponentName selects
  a specific component; a blank name falls back to the zero-based ComponentIndex
  in name-sorted component order. Legacy 6/7-column layouts remain supported.
- SoundWave is an Unreal object path, not a filesystem WAV path. Empty clears it.
  A bad nonempty asset path skips the row rather than substituting another file.
- Empty DeviceId chooses default; OutputChannel must be an integer 0..64 and still
  must exist on the real device. Strict numeric parsing rejects malformed values.
- OscAddress is the **override**. A blank override resets to a derived default;
  OscBase/Play/Stop/Loop/State are resolved reference columns, not separate endpoints.
- Quoted commas, doubled quotes and multiline fields are supported. Preserve
  proper CSV quoting instead of manually splitting on commas/newlines.
- Import is transactional for valid edits, but **not an all-or-nothing file commit**:
  invalid records can be skipped; an invalid OSC override can be reported while
  valid routing columns apply. Read every warning and re-export to verify.
- CSV is a routing snapshot, not a copy of audio media, drivers, device calibration,
  every transport property, or the spatial object-binding table.

Back up/export before bulk changes; test undo/redo on a disposable copy and inspect
the active world after PIE retargeting. Do not use a CSV as a substitute for a show
patch sheet and asset/version inventory.

## Migration from standalone AudioLoom / SpatialFabric 1.x

1. Close Unreal and back up the project. Remove standalone `Plugins/AudioLoom`
   and engine-level duplicates, including its `.uproject` plugin entry.
2. Install only the combined SpatialFabric plugin and enable OSC.
3. Remove stale generated binaries/intermediates only as appropriate, regenerate
   project files and rebuild against the chosen engine. Do not delete source media.
4. Inspect default receive IP, packaged permissions, explicit device routes,
   cooked Loading Behavior, resolved OSC paths and physical channel assignments.

Retained: `AudioLoom`/`AudioLoomEditor` module names, `/Script/AudioLoom` classes,
Engine config section, CSV columns, legacy hidden `AudioLoomPanel` tab. No class
redirect is needed for that packaging consolidation.

Intentional changes: descendants gain an owner-name OSC path segment; default
receivers bind loopback; packaged networking requires separate opt-ins; missing
devices fail closed; ADM XYZ/AED share a unit-sphere point; listener following
uses a virtual basis; format switching retains target/custom-field data. These
changes can require updating external show-control files even when assets load.

Original notices: [AudioLoom MIT license](../ThirdParty/AudioLoom/LICENSE).
