# Validation and release gates

## Cue acknowledgement/status feedback (2026-11-17)

UE 5.8 Win64 Editor build passed, followed by **35 native tests**, zero failed/skipped/in-progress, in an isolated `-NoSound`/NullRHI project. Includes shared acknowledgement fixtures (34 decisions, seven golden packets), observed start/failure/stop/status lifecycle, nested unknown-suffix rejection and live teardown restart protection. The validation gate now requires both acknowledgement tests. Evidence: `C:/Users/davo_/AppData/Local/Temp/SpatialFabricAckFinalGate/`. Validated Editor binaries were copied into this plugin's local `Binaries/` directory. Development/Shipping, hardware playback/capture and other platforms were not revalidated in this pass.

## Fast checks (no Unreal installation)

```sh
python -m unittest discover -s Tools -p "test_*.py"
```

Checks descriptor/module integration, migration/distribution files, and the native
report parser (including failed-test reports accompanied by process exit code zero).

## Live input (silent, Windows)

UE 5.8 Win64: `validate_unreal.py` passed all builds (Editor, Development, Shipping) and **33 native
tests** (the previous 29 plus 4 live-input tests): bridge mapping/resampling/underrun/overrun/gain/NaN,
PCM decode round trip, observed-state/OSC-gain/invalid-device behavior (failures declared as expected
errors) and explicit read-only input inventory. **Not measured:** any real microphone or line input,
physical latency, long-run drift, feedback handling, the Details/Playback window by interactive
automation, and macOS Core Audio capture (source written, never compiled). The Apple Silicon report
below covers playback only and says nothing about live input.

## macOS Apple Silicon: user-reported validation

The user confirms that the **Unreal SpatialFabric plugin has been tested and works
on macOS Apple Silicon**. This is an operator-reported functional result, not an
inference from Unity's separate native backend or from Windows builds.

Exact Unreal/macOS versions, Mac model, audio interface and detailed OSC/playback
checks were not supplied. No automated Unreal Mac build/test report is attached;
Intel Macs and the full Core Audio format/device/lifecycle matrix remain unverified.
The detailed hardware/show-release checks below still apply to each target setup.

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
Release 2.1.0 UE 5.8 Win64 evidence: `SpatialFabricValidation-qedke7lh`, **29 tests passed**,
zero failed/skipped; Editor/Development/Shipping builds passed. This includes
read-only scene inventory/pagination/Unicode chunks and Playback endpoint widths
at 1×/1.5×/2× layout scales. Earlier evidence: `7d4mvdvu` (26, full builds) and
`c9brhrzz` (27, editor-only UI iteration).

The controller has **35 passing tests** (30 core + 5 desktop GUI); the Node scene
client/examples have **4 passing tests**, including actual UDP paging and host-API
stubs for Node-RED/Max wrappers. Max/Node-RED application UIs were not launched.
Release Tools checks: **29 pass**, including room geometry/navigation, asset and
document links, retired-presentation/release contract, and safe archive/checksum
packaging. The removed presentation was deliberately not restored; the obsolete
deck-content test is replaced with the current release contract and archival link.
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
- Stage visibility in editor/Game View versus game worlds, restored component visibility,
  unselected-box display, hide/show round-trip, and unchanged authored stage transforms.
- Actual ADM metadata/coordinates, coherent XYZ/AED radius, Custom token validation,
  discrete axis/value pairing, and packet changes when send flags/custom fields change.
- Descendant OSC address separation, explicit Stop tick cancellation, typed play triggers.
- Real inbound loopback UDP on the exact configured interface, occupied-endpoint failure,
  immediate unregister cleanup, atomic malformed-bundle rejection, depth/blob bounds,
  finite floats, strict padding/truncation handling, and actual core-ticker dispatch
  with safe stop/owner release inside a callback (no component/world tick dependency).
- CSV multiline/quoted round-trip, invalid number/quote rejection, and actual import undo.

## SpatialSandbox integration evidence

The [showcase guide](Docs/SpatialSandboxDemo.md) documents repeatable live editor/PIE
checks and an independent packet capture, separate from the headless suite. The
observed run passed **24 live checks**, including OSC play/stop while paused, and **five post-PIE restoration checks**;
15,458 captured messages had no decode or XYZ/AED consistency failures. Spatial
objects animated, actual playback worker states changed, replay cancellation and
missing-device failure were exercised, and no authored stage motion occurred.
Physical listening/capture, latency, the full editor workflow matrix, and packaged
codec playback remain unverified. Evidence stays in the sandbox's
`Saved/SpatialFabricDemo/` directory and is not plugin Git content.

## Scene discovery live evidence

Python and dependency-free Node clients queried the real Editor through **8100 and
9000**, assembling **13 records over four pages**: 3 playback components, 4 spatial
bindings, Manager/stage/listener and spatial components. The tracker GUI received
20 chunks for a default-size page, displayed 4 explicitly labelled Scene snapshot
positions, kept the configured cues unchanged and released its temporary reply
listener. The confirmed-import test sends no transport commands.

Live PIE and paused PIE queries also passed through both inputs. Paused responses
reported identical frozen world time and the PIE world path; all cues remained
stopped. Reports are local under `SpatialSandbox/Saved/SpatialFabricDemo/`:
`scene_query_live_validation.json`, `scene_query_tracker_gui.json`. Sample JSON in
Examples is illustrative and contains no local hardware identifiers. Native tests
also check that querying does not synthesize/cache actor labels or start servers/audio.
These checks do not certify physical devices, packaged runtime networking, Mac/Linux
hardware, or every Max/Node-RED version. See [the command contract](Docs/SceneQuery.md).

## Published example and historical presentation evidence

`python Tools/validate_example.py --engine "C:/Program Files/Epic Games/UE_5.8"`
prepares a new standalone source project, builds its Editor target and runs a
silent disposable PIE test. Current room publication evidence:
`SpatialFabricRoomPublication-7wo9j1_w`, **13 checks passed**. Release 2.1.0 was
independently prepared/built again as `SpatialFabricExampleValidation-y7g7iehc`;
all **13 silent PIE checks passed** with the merged plugin-discovery fix. Its original C++ walking
character landed in the hallway, passed all four doorways, and was blocked by a
hall wall, room divider and hall end cap. Animation and three Force Inline decodes
succeeded without playing audio. The focused layout update also passed 21 static
floor/door/wall traces and preserved cue settings, routes and sequence identity.
The full rewritten generator was exercised in the disposable project and recreated
66 generated actors. The dependency-closed example has 13 packages plus 3 WAVs;
unused banner/brand-texture/driver packages and the unowned outside plane are excluded.
Earlier open-gallery evidence `SpatialFabricExampleValidation-4iee9ojt` passed ten checks. No Epic FirstPerson content or enabled MCP
server was needed. Networking is disabled only in the disposable copy to avoid
another working editor's input ports. Run Unreal build jobs sequentially: installed
engine UBT instances share a global mutex.

The historical 16-slide presentation was opened/rendered in Microsoft PowerPoint;
its PDF and contact sheet came from that export. It is no longer in the current
working/source release; downloads remain in GitHub release `v2.0.3`. Speaker notes are embedded. Source timings
sum to 900 seconds of talk plus 600 seconds of demo, excluding open Q&A/reference
slides. This validates document structure/rendering, not a venue's display/audio setup.

Native artifacts/reports stay outside the repository. The example Content/SourceAssets
snapshot is intentionally versioned under `Examples/SpatialFabricShowcase`.
Current release archives exclude retired presentation files and generated Unreal
artifacts; the separate portable controller includes its own runtime notices.

## Required manual/integration gates before a show release

A passing NullRHI test run is **not** audio hardware certification or cooked-content
certification. Record these against each supported engine, OS, and audio interface:

1. Play mono/stereo/multichannel assets at 44.1/48/96 kHz output rates. Verify duration,
   pitch, endpoint, physical channel isolation, waveform amplitude, and clean EOF.
2. Windows: float32/PCM16/PCM24/PCM32 mix formats, exclusive success/fallback, short
   one-shots, tail drain, Stop responsiveness, unplug/stall/error reporting, and no
   fallback to a different endpoint. Compare requested versus actual latency.
3. macOS: Apple Silicon is user-tested and reported working (see above). For detailed
   show acceptance, record native build configuration, interleaved/non-interleaved
   stream layouts, matched virtual stream rates, unsupported-format rejection, device
   disconnect, and callback silence/stop timing on the actual Mac/interface. The user
   report does not establish this complete matrix or Intel Mac compatibility.
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
