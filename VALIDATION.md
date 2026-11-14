# Validation and release gates

## Fast checks (no Unreal installation)

```sh
python -m unittest discover -s Tools -p "test_*.py"
```

Checks descriptor/module integration, migration/distribution files, and the native
report parser (including failed-test reports accompanied by process exit code zero).

## Native Windows validation

Install UE 5.8 and its supported Visual Studio C++ toolchain, Windows SDK, and .NET
Framework SDK (4.8 works). Python must be available on PATH.

```powershell
python Tools/validate_unreal.py --engine "C:/Program Files/Epic Games/UE_5.8"
```

This builds the plugin for **Editor, Development, and Shipping**, creates a temporary
project outside the plugin, loads the built plugin, runs all `SpatialFabric*` native
regressions with NullRHI/NoSound, and checks the JSON report for failures, skipped tests,
or an empty test suite. It never opens or saves SpatialSandbox's level/config files.
Logs, the built plugin, and the test report remain in the printed temporary directory.
Use `--editor-only` for iteration, or `--output` with a new empty directory to retain
named evidence. Output directories inside the source plugin are rejected.

The manual **Native Unreal validation** GitHub workflow runs the same commands on a
trusted self-hosted Windows runner labeled `Unreal`, with `UNREAL_ENGINE_ROOT` set.
Do not run untrusted PR code on that host. Source ZIP release automation is deliberately
labeled source-only; it is not a substitute for this native validation job.

## Native regression coverage

The repository tests invoke production code for:

- RIFF chunk alignment, truncation/bounds/shape checks, invalid bit depths, and a small
  deterministic mutation corpus; real WAV sample conversion.
- Fractional device-rate rendering, channel isolation, loop exit, valid final samples,
  zero-filled EOF tails, and packed float/int16/int24/int32 sample widths.
- Registry/router association after a missing actor, cross-world actor resolution,
  editor snapshots without BeginPlay, stage reassignment/clear, and negative log counts.
- Attached listener feedback and production stage rotation/normalization.
- Actual ADM metadata/coordinates, coherent XYZ/AED radius, Custom token validation,
  discrete axis/value pairing, and packet changes when send flags/custom fields change.
- Descendant OSC address separation, explicit Stop tick cancellation, typed play triggers.
- CSV multiline/quoted round-trip, invalid number/quote rejection, and actual import undo.

## Required manual/integration gates before a show release

A passing NullRHI test run is **not** audio hardware certification or cooked-content
certification. Record these against each supported engine, OS, and audio interface:

1. Play mono/stereo/multichannel assets at 44.1/48/96 kHz output rates. Verify duration,
   pitch, endpoint, physical channel isolation, waveform amplitude, and clean EOF.
2. Windows: float32/PCM16/PCM24/PCM32 mix formats, exclusive success/fallback, short
   one-shots, tail drain, Stop responsiveness, unplug/stall/error reporting, and no
   fallback to a different endpoint. Compare requested versus actual latency.
3. macOS: native compilation plus interleaved/non-interleaved stream layouts, matched
   virtual stream rates, unsupported-format rejection, device disconnect, and callback
   silence/stop timing. Core Audio code cannot be validated on a Windows build host.
4. Package an actual project with referenced **Force Inline** SoundWaves for the chosen
   codecs; verify decoder availability and sample duration/channel order in Development
   and Shipping. Verify streaming assets are rejected with the documented message.
   Multichannel Vorbis is explicitly unsupported rather than silently reordered.
5. In the editor and PIE, verify loop toggles, zero/fixed/random replay delays, Stop
   cancellation, natural/failed playback state messages, and EndPlay/unregister cleanup.
6. Close all panels and verify registry freshness, disabled-network policies, active
   target changes, socket status/failure, and editor/PIE socket hand-off. Test remote
   control only after explicitly setting Listen IP and firewall permissions.
7. Exercise editor undo/redo for property edits, add/remove bindings, format switching,
   CSV import, level save/reload, and existing Blueprint/asset migration. Confirm
   format switching preserves saved CustomFields and ID overrides.
8. Verify live packets with a receiver capture (not only the preview), including ADM
   dref/dmax/name/mute/gain, unit-sphere corner limiting, listener-relative identity
   pose, Custom ranges/templates, duplicate-path warnings, and high-object-count load.
9. Call Prepare Audio during loading and measure first-decode cost, cached cue latency,
   memory use, and sustained OSC/device load for representative show assets.

Mac/Linux builds, older UE versions, physical audio output, packaged asset cooking,
and full interactive UI workflows remain separate validation gates. Do not advertise
those combinations as tested based solely on the Windows headless suite.
