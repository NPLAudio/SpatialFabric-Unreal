# Combined SpatialFabric audit

> Historical pre-fix findings. Implementation remediation is tracked in [FIXES.md](FIXES.md).
> Do not treat this report's original verdict/line numbers as the current post-fix status.

## Scope and verdict

Audited the current working tree at `SpatialSandbox/Plugins/SpatialFabric`, including
all four modules, their integration, OSC, device playback, CSV tooling, packaging,
tests, and the sandbox plugin configuration. The base commit is `e7de9cb`; the
AudioLoom consolidation and build-compatibility changes are still uncommitted.

**Verdict: builds successfully, but is not ready for reliable show playback.**
Consolidation and relocation preserved the earlier runtime defects. Eight high-priority
findings are listed first. This review is not a guarantee that all defects have been found.

All source references below are relative to this plugin directory. No implementation
files were changed during this audit. This report is the only repository file added.

## Validation performed

- Structural smoke tests: **6 passed** (`python Tools/test_plugin_layout.py`).
- Existing Unreal automation tests: **5 passed**, UE 5.8 Win64, NullRHI.
- Temporary regression tests against the actual implementations: **5 failed**, each
  reproducing the defect it was designed to detect; see the table below.
- The temporary regression build successfully compiled all four editor/runtime modules.
- Earlier combined-plugin Win64 Editor, Development, and Shipping builds succeeded.
  All 64 current production source files were checked byte-for-byte against the isolated
  regression source copy; only the temporary regression files were added to that copy.
- The built distribution includes `Docs/AudioPlayback.md`, the AudioLoom MIT notice,
  and the plugin icon. Descriptor/module/dependency checks and whitespace checks passed
  (using `core.whitespace=cr-at-eol` for the repository's existing committed CRLF files).
- SpatialSandbox enables SpatialFabric and OSC. Its newly enabled MCP plugin was not
  exercised: no Unreal MCP tools were exposed to this assistant session.

Unreal was run in a temporary project, not against the user's open sandbox level.
No audio-device playback, malformed-WAV crash/exploit probes, macOS, Linux, packaged
asset cooking, or compatibility tests on UE 5.4–5.7 were performed.

### Runtime regression evidence

| Test | Expected | Observed |
|---|---|---|
| Missing actor routing | Only the valid ADM binding receives its object | Custom receives the ADM object's position with override ID `77`; ADM receives nothing |
| Attached child listener | No repeated parent movement for a static setup | Stage X moves from `100` to `200` on the next resolution |
| Editor frame without BeginPlay | One tracked object in the snapshot | Zero objects |
| ADM name override | `Requested ADM name` reaches the state used by ADM output | `Binding label` reaches that state |
| Padded odd-length RIFF chunk | Valid mono PCM WAV decodes to one sample | Load fails; zero samples |

Evidence and test-only code are outside the repository:

- `%TEMP%/SpatialFabric-Audit/TestReport/index.json` — original suite, 5 passed.
- `%TEMP%/SpatialFabric-Audit/RegressionReport/index.json` — regression suite, 5 failed.
- `%TEMP%/SpatialFabric-Audit/regression-console.log` — observed values/errors.
- `%TEMP%/SpatialFabric-Audit/regression-build.log` — successful regression build.
- `%TEMP%/SpatialFabric-AuditSource/Source/SpatialFabric/Private/Tests/AuditRegressionTests.cpp`.
- `%TEMP%/SpatialFabric-AuditSource/Source/AudioLoom/Private/Tests/AuditWavTests.cpp`.

Important: this headless Unreal invocation exited with status 0 even for failed tests.
Results above come from the JSON report and assertions, not process exit status alone.

## High-priority findings

### H01 — Unsafe WAV parsing permits invalid reads/writes and division by zero

**Source-confirmed; intentionally not crash-tested.**

`Source/AudioLoom/Private/AudioLoomPcmLoader.cpp:61–89,118–122`

The parser reads a claimed `fmt ` payload without first checking that the bytes exist.
It divides by `BitsPerSample / 8` before validating the format, permitting a zero divisor.
For float WAVs it allocates complete frames but copies the entire byte payload, including
an incomplete final frame. This can write beyond the valid destination or copy to an
empty allocation. Allocation size and sample-rate validity are also insufficiently checked.

**Fix:** validate chunk bounds, supported format, sample rate, channel count, frame
alignment, and allocation limits before calculating sizes or copying samples. Copy only
the validated destination byte count. Add bounded malformed-input regression tests.

### H02 — Missing actors shift all subsequent binding-to-state associations

**Runtime reproduced.**

`Source/SpatialFabric/Private/SpatialObjectRegistry.cpp:42–55`;
`Source/SpatialFabric/Private/ProtocolRouter.cpp:87–113`

The registry omits unresolved enabled bindings, but the router consumes one snapshot
row for every enabled binding. A missing first binding steals the following object's
state and applies the missing binding's adapter, ID override, and custom fields.
Subsequent objects can be dropped. The regression emitted object ID `77` on Custom
instead of delivering the valid object to ADM.

**Fix:** carry a stable binding identity/index in every snapshot row and route using it;
do not infer identity from compacted array positions.

### H03 — Windows one-shots silence their final buffer and stop before draining

**Source-confirmed; device playback not tested.**

`Source/AudioLoom/Private/AudioLoomPlaybackBackend.cpp:291–295,337–372`

`AUDCLNT_BUFFERFLAGS_SILENT` marks an entire released buffer as silent, not the remaining
samples after EOF. Setting it when the last source frame is written suppresses the final
buffer; a clip that fits in the initial buffer can be entirely silent. The worker also
stops the audio client without waiting for all previously queued audio to finish.

**Fix:** submit the valid samples normally, explicitly zero-fill only unused samples,
and track/drain queued frames before declaring natural completion.

### H04 — Fixed 48-kHz PCM is played at the device's unrelated rate

**Source-confirmed; non-48-kHz hardware not tested.**

`Source/AudioLoom/Private/AudioLoomComponent.cpp:218–244`;
`Source/AudioLoom/Private/AudioLoomPlaybackBackend.cpp:135,227–228,291,580–602`

The component resamples everything to 48 kHz, but neither backend converts to the actual
output rate. Each output frame consumes one source frame. A 96-kHz device therefore
plays 48-kHz PCM approximately twice as fast; 44.1-kHz devices also change speed/pitch.
The nominal Core Audio rate is queried only to calculate the latency display.

**Fix:** negotiate the device rate first and resample or configure a conversion-capable
stream to match it. Preserve source-rate information in the backend API.

### H05 — Windows output encoding is not validated against the negotiated format

**Source-confirmed; alternate output formats not tested.**

`Source/AudioLoom/Private/AudioLoomPlaybackBackend.cpp:229–232,279–289`

Only extensible IEEE float is recognized. Every other format is written as 16-bit PCM,
including 24/32-bit integer output and plain IEEE-float formats. Pointer advancement
then disagrees with the device block alignment and generates corrupted audio.

**Fix:** explicitly negotiate supported sample formats or handle each accepted
encoding, valid-bit count, and block alignment correctly; reject unsupported formats.

### H06 — Normal cooked SoundWave assets have no runtime decoding path

**Source-confirmed; packaged content cooking not tested.**

`Source/AudioLoom/Private/AudioLoomPcmLoader.cpp:162–200`

The asset PCM path is editor-only. Outside the editor, loading relies on finding a loose
WAV beside an asset package. Ordinary cooking/IoStore does not create or stage that
sibling WAV. Successful Shipping compilation therefore does not establish packaged
SoundWave playback support.

**Fix:** implement supported cooked-audio decoding, or define and stage a deliberate
external-file workflow with tested package-path handling.

### H07 — Following an attached child feeds the parent's motion back into itself

**Runtime reproduced.**

`Source/SpatialFabric/Private/SpatialStageVolume.cpp:130–150`

When there is no local player listener, a child of the stage can become its listener.
The stage moves to that child's world position, moving the child with it. The next
frame moves the stage by the same relative offset again. Relative rotations can also
accumulate. A 100-unit child offset produced stage X=100, then X=200.

**Fix:** avoid following descendants or compute a listener-relative basis without
moving the ancestor transform that owns the listener.

### H08 — Advertised non-PIE spatial preview never initializes the pipeline

**Runtime reproduced.**

`Source/SpatialFabricEditor/Private/SpatialFabricEditorModule.cpp:39–42`;
`Source/SpatialFabric/Private/SpatialFabricManagerActor.cpp:74,106–113,196`

The editor tickable calls `ProcessFrame`, but the router is created only in BeginPlay.
`ProcessFrame` returns immediately when it is null. `InitializeAdapters` also does
nothing with a null router. A valid editor binding consequently produces no snapshot,
radar state, or spatial OSC output. Listener/stage initialization also needs an editor path.

**Fix:** introduce explicit editor-safe initialization, update, and shutdown, with
network permission checks; keep preview snapshot generation independent of networking.

## Additional correctness findings

### M01 — EndPlay does not stop playback

`Source/AudioLoom/Private/AudioLoomComponent.cpp:129–134,189–198`

Playback is stopped only on explicit Stop or BeginDestroy. Actor destruction, level
unload, or ending PIE can leave a worker and audio output running until GC.
Stop/cancel playback during EndPlay and component unregistration as appropriate.

### M02 — Loop changes are disconnected from the backend; replay can override Stop

`Source/AudioLoom/Private/AudioLoomComponent.cpp:139–166,258–274,293–296`;
`Source/AudioLoomEditor/Private/UI/SAudioLoomExpandableRow.cpp:352–359`

`SetLoop` and the UI change the component value, not the active backend loop state.
OSC cannot turn an active loop off. SetLoop also does not update replay tick state.
Manual Stop can be interpreted as natural completion, and existing replay countdowns
are not cancelled. A zero replay delay never enters the positive-countdown branch.
Use an explicit transport/completion state machine with cancellation semantics.

### M03 — ADM name and distance controls are ignored

**Name override runtime reproduced; distance omission source-confirmed.**

`Source/SpatialFabric/Private/SpatialObjectRegistry.cpp:64–73`;
`Source/SpatialFabric/Private/Adapters/ADMOSCAdapter.cpp:98–137`

`ADMNameOverride` is never forwarded to the name consumed by ADM output. `Dref` and
`Dmax` are copied into state but no `/dref` or `/dmax` messages are emitted, despite
public API/docs promising them. Carry the complete metadata through the pipeline.

### M04 — Runtime stage reassignment and actor world resolution are unreliable

`Source/SpatialFabric/Private/SpatialFabricManagerActor.cpp:92–98,301–320`;
`Source/SpatialFabric/Private/SpatialObjectRegistry.cpp:109–125`

Changing a valid StageVolume assignment, or clearing it, leaves the cached stage in use.
Fallback stage resolution picks the first stage rather than matching a selected stage
across worlds. Actor resolution accepts a resolved soft pointer without verifying its
world; label fallback silently chooses the first duplicate label. This can produce the
wrong origin or editor-world positions in PIE. Track assignment changes, resolve stable
actor identities in the owning world, and reject ambiguous fallbacks.

### M05 — Cartesian and polar ADM modes disagree on distance

`Source/SpatialFabric/Private/Adapters/ADMOSCAdapter.cpp:139–179`

Cartesian coordinates retain their normalized radius, but polar distance is divided
by sqrt(3). A Cartesian radius of 1 becomes approximately 0.577 in AED. `Both` emits
conflicting representations of the same object. Define a consistent distance convention
and test Cartesian-to-polar-to-Cartesian equivalence against the target protocol.

### M06 — Send-on-change omits changes that alter actual packets

`Source/SpatialFabric/Private/Adapters/ADMOSCAdapter.cpp:54–73`;
`Source/SpatialFabric/Private/Adapters/CustomAdapter.cpp:189–205`

ADM caches omit send-gain/mute/name flags; enabling them for a stationary object can
send nothing. Custom caches omit label/custom fields even when they change the address.
Caches are also marked sent without confirmation that a usable client sent anything.
Compare/cache the complete emitted representation and invalidate it on reconnect/config changes.

### M07 — Spatial adapters recreate their client/socket for every send batch

`Source/SpatialFabric/Private/Adapters/ADMOSCAdapter.cpp:49`;
`Source/SpatialFabric/Private/Adapters/CustomAdapter.cpp:186`;
`Source/SpatialFabric/Private/SpatialOSCClientComponent.cpp:33–38,59–65`

Connect always drops the reference and creates another UOSCClient, even for an unchanged
endpoint. Disconnect does not explicitly Stop the old client. This adds UObject/socket
churn and makes release dependent on object replacement/GC. Reuse an endpoint-specific
client or change its send endpoint; stop clients explicitly on teardown.

### M08 — Editor networking permission is ignored and manager spawning is unconditional

`Source/SpatialFabric/Public/SpatialFabricSettings.h:33`;
`Source/SpatialFabric/Private/SpatialFabricManagerActor.cpp:53–74`;
`Source/SpatialFabricEditor/Private/SpatialFabricEditorModule.cpp:32–53`

`bEnableInEditor` is never read. PIE opens networking regardless of this setting.
The editor tickable also calls GetOrSpawnManager every tick, even with the panel closed;
users cannot keep a level free of the actor by deleting it while the plugin is enabled.
Honor permissions and make authoring-world mutations explicit and undoable.

### M09 — Playback OSC registries depend on an open editor widget for freshness

`Source/AudioLoom/Private/AudioLoomOscSubsystem.cpp:69–125`;
`Source/AudioLoomEditor/Private/UI/SAudioLoomPanel.cpp:472–488`

The runtime registry is built on StartListening; subsequent periodic rebuilds come
from the editor panel. Spawned/streamed components and address changes remain stale
without that widget or a manual Blueprint rebuild. Register/unregister components and
address changes at runtime independently of Slate.

### M10 — Default playback OSC paths collide inside a hierarchy

`Source/AudioLoom/Private/AudioLoomComponent.cpp:304–317`;
`Source/AudioLoomEditor/Private/UI/SAudioLoomPanel.cpp:365–432`

The path uses the hierarchy root label but a component index local to each owner.
Two child actors with one component each get the same `<root>/1`. The warning only
compares distinct roots, so it misses this case. Allocate hierarchy-wide identities,
or include owner identity; detect collisions using final resolved paths.

### M11 — Integer zero triggers Play and float 0.5 contradicts the UI contract

`Source/AudioLoom/Private/AudioLoomOscSubsystem.cpp:167–185`

The no-arguments condition tests only `Floats.Num()==0`, so an integer argument 0
is interpreted as no arguments and triggers playback. The strict `>0.5` also rejects
the documented inclusive float threshold. Distinguish empty messages from typed args.

### M12 — Monitoring reports requests instead of actual playback transitions

`Source/AudioLoom/Private/AudioLoomComponent.cpp:246–253,258–270`;
`Source/AudioLoom/Private/AudioLoomPlaybackBackend.cpp:70–79,369–379`

State 1 is emitted before asynchronous device initialization can succeed. Natural
completion and backend failures do not emit state 0. External monitors can remain
stuck at playing after failure/EOF. Deliver actual backend lifecycle events back to
the game thread and distinguish request acceptance from confirmed playback.

### M13 — Playback UI edits can disagree with active audio and are not undo-safe

`Source/AudioLoomEditor/Private/UI/SAudioLoomExpandableRow.cpp:222–227,256–317,399–421`;
`Source/AudioLoom/Private/AudioLoomComponent.cpp:169–187`;
`Source/AudioLoomEditor/Private/AudioLoomRoutingCsv.cpp:450–457`;
`Source/SpatialFabricEditor/Private/UI/SSpatialFabricPanel.cpp:2244–2372`

Playback row controls directly mutate device/channel/buffer/sound without invoking
the Details notification/restart path. They call Modify after mutation, without a
scoped transaction. Spatial panel edits mostly only MarkPackageDirty. CSV clearing
a playing SoundWave also avoids stopping the previous stream because restart is gated
on the new SoundWave being non-null. Use transactions and before-change snapshots,
centralized setters, and explicit stop/restart rules.

### M14 — Changing spatial format destroys per-target routing configuration

`Source/SpatialFabricEditor/Private/UI/SSpatialFabricPanel.cpp:2337–2366`

SetActiveFormat empties every binding's Targets and recreates defaults. Switching
ADM -> Custom -> ADM loses object-ID overrides and CustomFields instead of preserving
per-format configuration; no confirmation or undo transaction protects the operation.
Keep per-adapter entries and switch enablement without destructive replacement.

### M15 — Invalid CSV numbers silently become valid routing values

`Source/AudioLoomEditor/Private/AudioLoomRoutingCsv.cpp:325–328`

FCString::Atoi converts malformed/empty values to 0. A typo in a legacy ComponentIndex
can select the first component, and a typo in OutputChannel routes to channel 0 instead
of rejecting the row. Validate complete numeric strings and ranges before mutation.

### M16 — Unknown Custom argument tokens misalign discrete addresses and values

`Source/SpatialFabric/Private/Adapters/CustomAdapter.cpp:25–36,113–137`

BuildArgs skips unknown tokens, but discrete SendObject indexes the unfiltered token
array. For `invalid x y`, the X value is sent to `{axis}=invalid`, Y to `{axis}=x`.
Validate templates once, or build name/value pairs together so filtering cannot shift them.

### M17 — OSC listening status tests object existence, not socket state

`Source/SpatialFabric/Private/SpatialOSCServerComponent.cpp:39–69`;
`Source/AudioLoom/Private/AudioLoomOscSubsystem.cpp:114–147`

UE's CreateOSCServer can return a UObject even if address setup or Listen fails.
Both wrappers equate a non-null pointer with listening. The playback port precheck is
also subject to a bind race. Check actual server activity after startup, return failure,
and clean up failed instances. Multiple spatial managers competing for the default port
currently make the misleading status especially easy to encounter.

### M18 — UI can advertise output channels that playback never opens

`Source/AudioLoom/Private/AudioOutputDeviceEnumerator.cpp:33–127,229–230,258–259`;
`Source/AudioLoom/Private/AudioLoomPlaybackBackend.cpp:135,227–232`

The enumerator advertises the maximum supported/probed channel count, while playback
always starts from GetMixFormat and does not negotiate the requested wider layout.
If the mix format is stereo but probing reports eight channels, selecting channel 8
produces silence because the writer only iterates the two opened channels. Validate
against the actual stream layout or negotiate a format supporting the chosen channel.

### M19 — Missing explicitly selected devices silently fall back to default output

`Source/AudioLoom/Private/AudioLoomPlaybackBackend.cpp:83–119,492–539`

Both backends begin with the default device and keep it when a non-empty saved device
ID cannot be matched. Disconnecting an interface can unexpectedly send show audio to
a different endpoint without warning. Fail closed for explicit routes, or expose an
opt-in fallback policy and report the resolved device.

### M20 — Negative log counts can construct an invalid array range

`Source/SpatialFabric/Private/SpatialFabricManagerActor.cpp:167–172`

GetRecentLog is Blueprint-callable with an unrestricted signed Count. A negative
value makes Start exceed MessageLog.Num(), then passes a negative element count and
out-of-range pointer to the array constructor. Clamp Count to [0, Num] and return an
empty array explicitly for zero. This was source-reviewed, not crash-tested.

### M21 — Valid odd-sized RIFF chunks are not padded while walking

**Runtime reproduced.**

`Source/AudioLoom/Private/AudioLoomPcmLoader.cpp:61–65`

RIFF advances by chunk size plus an alignment byte for odd payloads. The parser skips
only chunk size, misreads the following header, and rejects valid WAVs. Advance using
validated size plus `(size & 1)`, and retain a real parser regression test.

### M22 — Changing playback monitoring IP/port does not retarget an active client

`Source/AudioLoomEditor/Private/UI/SAudioLoomPanel.cpp:198–224`;
`Source/AudioLoom/Private/AudioLoomOscSubsystem.cpp:125,150–159`

The widgets save SendIP/SendPort but never call UpdateMonitoringTarget. The UI shows
the new destination while state messages keep going to the previous one until restart
or a manual API call. Apply the change to active world clients or clearly mark it pending.

### M23 — Output preview is not a preview of routed/configured packets

`Source/SpatialFabricEditor/Private/UI/SSpatialFabricPanel.cpp:2521–2558,2617–2634`

Custom preview hardcodes `/source/{id}/position` and XYZ regardless of Custom templates,
ranges, coordinate mode, and send mode. It also reads the full snapshot rather than
per-adapter filtered states, so target ID overrides and disabled routes are not reflected.
Generate previews from the same adapter encoding/routing path used for transmission.

## Lower-priority and robustness findings

### L01 — CSV export supports multiline fields that import cannot reconstruct

`Source/AudioLoomEditor/Private/AudioLoomRoutingCsv.cpp:38–62,273–282`

EscapeField quotes newline-containing fields, but import splits physical lines before
parsing CSV records. Such an exported actor label cannot round-trip. Use record-aware
CSV parsing and reject unterminated quoted fields.

### L02 — Windows error paths leak event handles and can hide stream failures

`Source/AudioLoom/Private/AudioLoomPlaybackBackend.cpp:182–240,313–333`

After creating an event, early returns for GetService failure or empty PCM do not close
it. Start/GetBufferSize/GetCurrentPadding results are also incompletely checked, so
stream failure/disconnection can leave misleading playing state or retry loops.
Use RAII for HANDLE/COM ownership and propagate meaningful failure reasons.

## Test, packaging, and integration assessment

### Good / verified

- One installable `SpatialFabric.uplugin`, four declared modules, one external OSC dependency.
- AudioLoom class/module identity is retained; migration does not require class redirects.
- Playback UI is embedded via an exported Slate class and editor-only dependency.
- Runtime builds do not link editor modules. Unsupported playback platforms avoid owning
  the incomplete platform PIMPL type; non-Windows builds still need actual compilation.
- AudioLoom license preservation and RunUAT distribution filters are present.
- Release copy excludes its own `dist` output, avoiding recursive self-copy.
- Git history, branch, origin remote, and uncommitted work survived the move.

### Remaining gaps / deployment cautions

- Existing Unreal tests largely duplicate math and format strings, rather than invoking
  production normalization, routing, device writers, or lifecycle code. Their green
  result does not cover the failures reproduced here.
- Temporary regression tests are not yet part of the repository suite. Preserve and
  adapt them while fixing these issues; add WAV safety, transport, stream-format,
  editor/PIE transition, CSV, reconnect, and packaged-asset tests.
- Release CI only runs structural Python checks and creates a source ZIP; it does not
  compile Unreal, run native tests, verify SDK/engine versions, or test audio devices.
- WAV/device decoding and full-buffer copies occur synchronously from Play on the game
  thread. Large assets or repeated OSC Play commands can cause frame/control latency.
  Benchmark caching/asynchronous decoding before using this for timing-critical cues.
- OSC listeners bind all interfaces without authentication. That is normal for this
  protocol but requires a trusted control network/firewall. Spatial and playback
  servers have independent settings/permissions and must not share a receive port.
- Editor and PIE worlds own different playback OSC subsystems. Current PIE callbacks
  rebuild lists, not transfer an active editor listener; test port ownership and which
  world's objects receive commands when entering/leaving PIE.
- Core Audio output assumes float data without negotiating/querying stream encoding;
  callback shutdown also stops filling output before the device is stopped. Validate
  format assumptions, channel layouts, and silence initialization on real Mac hardware.
- Listener-relative object coordinates combined with listener-orientation messages
  need receiver-side semantic testing to rule out double application of orientation.
- The code retains separate spatial and playback pipelines: routing an actor through
  ADM does not automatically spatialize that actor's AudioLoom PCM.
- SpatialSandbox's `.uproject` configuration is outside the plugin's independent Git
  repository; committing the plugin alone will not version project enablement/settings.
- Version remains 1.0.4 despite the combined feature set. Choose a new release version
  and publish explicit migration guidance before distribution.

## Recommended implementation order

1. WAV bounds/size validation and RIFF parsing; Windows one-shot drain/flags.
2. Stable binding identity and descendant-safe listener basis.
3. Device format/rate/channel negotiation and cooked-asset decoding policy.
4. Separate preview state capture from network lifecycle; implement editor initialization.
5. Explicit transport lifecycle, loop propagation, replay cancellation, and real state events.
6. Correct metadata, cache invalidation, OSC status/registry behavior, and route setters.
7. Undo-safe editor workflows, non-destructive format switching, CSV validation, and
   previews generated from the actual encoders.
8. Promote regression tests into CI, then exercise real devices and packaged content.
