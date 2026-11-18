# AudioLoom playback compatibility

SpatialFabric retains the AudioLoom runtime/editor modules and `/Script/AudioLoom`
serialized classes. Do not delete or rename those modules when retiring the
standalone distribution. Install only one plugin; see [migration](AudioPlayback.md#migration-from-standalone-audioloom--spatialfabric-1x).

The integrated feature surface includes SoundWave/validated WAV loading, direct
output device/channel routing, loop, fixed/random replay, opt-in BeginPlay playback,
derived/overridden OSC paths, editor management and legacy 13-column routing CSV.
The shared [playback parity contract](https://github.com/NPLAudio/SpatialFabric/blob/main/Specs/Playback-Parity-v1.md)
defines the cross-engine baseline, not a promise of interchangeable asset formats.

## Restored content compatibility

- The default decoded-float budget remains 256 MiB. `MaxDecodedAudioMiB` permits an
  explicit 1–8191 MiB budget (config values are clamped); int32 indexing, file-array
  capacity and available memory impose additional limits. This is not streaming.
- `bAllowLegacyWavSidecar` defaults true. When imported/raw/inline data cannot be
  used, a resolved package's sibling `.wav`/`.WAV` can be loaded with the same rate,
  format, finiteness and memory validation. Disable it for embedded-only deployments.
- Streaming compressed chunks are never passed to an unsafe full-file expansion.
  Stage sidecars explicitly or Force Inline and recook compatible content.
- Networking defaults, exact OSC paths, strict trigger typing and missing-device
  failure remain safer than the standalone plugin. Verify existing controllers and
  routes rather than reinstating accidental legacy behavior.

## Current local software evidence

UE **5.8**, Windows x64, disposable plugin/project validation:
**Editor Development, Game Development and Game Shipping compilation passed**;
BuildPlugin distributable packaging succeeded. **36/36 native automation tests passed**, including
`SpatialFabric.Playback.LegacySidecarAndBudget`. The new test checks budget bounds
without huge allocations, sibling loading/opt-out/missing-file behavior and a
persisted `/Game` SoundWave's production package-to-sidecar path. Its empty synthetic
SoundWave emits the expected imported-raw-data warning before successful fallback.
Python tool suite: **33 passed**.

Command:

```text
python Tools/validate_unreal.py --engine "C:/Program Files/Epic Games/UE_5.8"
```

Evidence from this run:
`C:/Users/davo_/AppData/Local/Temp/SpatialFabricValidation-9vxp0dra/Report/index.json`.
This is machine-local ephemeral evidence, not a portable release artifact.

Development compilation exposed a missing direct `UObject/Package.h` include in
the loader; it was fixed before the successful full rerun.

## Actual cooked/staged Development game

A separate disposable project subsequently passed UAT BuildCookRun: Editor/Game
build, cook (0 errors/warnings), IoStore, stage, package and archive. All 13 published
assets are in the UFS manifest. Packaged startup/content checks passed, including
legacy AudioLoom class resolution, three serialized cues and inactive startup/network.

**The silent runtime smoke is partial: 16/22 checks passed; six loader/PrepareAudio
assertions failed, player exit 7.** Under `-NoSound -NullRHI`, UE 5.8 suppresses
runtime-format initialization, leaving `FSoundWaveProxy_InvalidFormat`. This is not
a proved bad cook, but the ordinary audio-enabled loader remains unverified.
A separate bounded, device-free BINKA diagnostic decoded all three actual cooked
payloads (48 kHz: 1.2 s mono, 4 s stereo, 0.6 s mono); it did not change or validate
the loader's failed path. Explicit sidecar lookup also decoded all three WAVs in a
separate archive copy with manually deployed loose siblings. UAT did not stage them.

Evidence/report:
`C:/Users/davo_/AppData/Local/Temp/SpatialFabricCookValidation-012t2fc_/VALIDATION_REPORT.md`.
The unchanged source snapshot covered 246 files. No physical output/microphone,
Shipping-game runtime, macOS or hardware latency/channel qualification occurred.
Do not treat this partial smoke as full packaged playback/migration acceptance.
Retain backups/legacy releases until the target deployment migration is accepted.
