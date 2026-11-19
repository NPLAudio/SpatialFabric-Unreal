# Audio routing crosspoints (Unreal)

Open **Window → Spatial Fabric → Audio Routing**. This is the default visible tab.
No SpatialFabric manager is needed: rows include every playback component on loaded,
placed editor-world actors, including hidden actors and inactive loaded levels.
Unloaded World Partition actors are not loaded for routing. Live input/capture is
not part of this grid. Stage/Objects/Radar/Output, Scene Objects and the existing
Playback transport UI remain separate tabs. Actor Details is retained unchanged.

## Configure, do not play

The rectangular matrix has component rows and interface-group/channel columns.
Click an intersection to assign one endpoint/channel pair. Green checks mean
**CONFIGURED, not live**, not a hardware or signal-health test. Clicking the
already-configured cell is a no-op. This is not a multi-send mixer or a Dante
subscription editor.

Select any row (its label or a cell) for **Interface** and **Output channel**
dropdowns above the matrix. Both dropdowns use the same route mutation as cells.
Large channel lists have lazy 64-output submenus, not thousands of Slate widgets.
Both matrix axes scroll independently with fixed headers; wheel scrolls rows,
Shift+wheel scrolls columns. Only visible cells are painted by one leaf widget;
ancestor clipping is also used for pointer and wheel input, including during mouse
capture. Wheel events propagate when an axis cannot move. Hover columns to see exact
IDs and channel semantics. Narrow headers may truncate: use the tooltip or the
full selected-interface label to disambiguate. Duplicate names always include IDs.

* **Auto / 0** routes source channels to corresponding destinations.
* **1+** is a 1-based destination receiving the first source channel, not the
  source channel index. Source width/fit is not inferred or guaranteed.
* `*` marks an explicitly stored positive destination outside cached capacity.
* The empty ID is a separate **Dynamic OS default** group. Its capacity is unknown;
  it is never replaced by today's default hardware ID or invented channel count.
* Unknown IDs expose Auto plus their explicitly stored positive channels only.
  Known cached interfaces expose their recorded channels even while unplugged.
* Switching interfaces preserves the channel. Neither refresh nor assignment
  silently clamps an out-of-range legacy route or removes an unknown ID.

**Refresh Interfaces** is the only discovery action in this new tab. It uses
`FAudioOutputDeviceEnumerator::GetOutputDevices(FString* OutError)` to query output
metadata/mix format; it does not initialize/start a stream or request capture.
The new optional error output reports incomplete/unavailable inventory so partial
results cannot replace cache history. No refresh occurs on construction, paint,
selection, periodic row discovery, or dropdown open. The legacy Playback panel is
now constructed lazily when its tab is explicitly opened; its existing inventory
and transport behavior is otherwise unchanged. Existing Actor Details customizations
also retain their legacy discovery behavior; the offline-first guarantee applies
to this central Audio Routing tab, not every historical device selector.

Assignments directly edit the real `UAudioLoomComponent::DeviceId` and
`UAudioLoomComponent::OutputChannel` reflected fields, using `FScopedTransaction`,
`Modify`, `PreEditChange`, `PostEditChangeProperty` and package dirtying. No proxy
routes are saved in the inventory. One edit is one Undo step for the pair. The
mutation rechecks the current target, not only a displayed enabled state. It
rejects PIE/SIE and queued play sessions, active/asynchronously starting or
undrained playback, pending replay, templates, transient components, noneditable
actors, locked/read-only level packages, and noneditable/inherited/construction-
script components. Blueprint construction-script components are conservatively
browse-only; add an editable instance component to configure centrally. The panel
never calls Stop to make an edit succeed. It never calls Play/PrepareAudio,
InitializeAdapters, Connect or an OSC listener API. If a prior property/route edit is
undone or redone during legacy editor playback, the component stops before native
property-restoration callbacks and cancels pending replay/start state. Undo/Redo
never restarts on the restored endpoint, including on a subsequent tick; an explicit
Play is required to resume.

## Offline cache

Project-local, per-OS-user editor metadata lives at
`Saved/Config/SpatialFabricAudioInterfaces-<username-hash>-v1.json`.
Schema 1 stores exact case-sensitive endpoint ID, friendly name, channel count,
sample rate and UTC last-seen timestamp only. No cue/component/asset references,
routes or online flags are serialized. Loaded entries are always offline; online
means only "seen by the last explicit successful refresh", not continuous health.
Unplugging retains order, labels, capacity and timestamp. Reconnection matches IDs,
never friendly names. Bad refreshes mark entries offline but preserve history and
all authored routes, and do not save. Cache corruption is ignored without writing.

Limits: 256 retained interfaces, 4096 channels/interface, 1024 characters/text
field, 2 MiB JSON, rates 0–768000 Hz. Empty/duplicate IDs, control characters,
invalid schema/bounds, incomplete inventory, history overflow and write failures
reject the refresh atomically. History is not evicted to make room. A validated
explicit refresh writes a temporary file then replaces the cache; no route action
writes it. Cache loads and route configuration work without devices or preparation.

## APIs and validation

Editor-only seams in `AudioRoutingAuthoring.h`:

* `FCache::Load`, `FCache::Accept(Fresh, Error, Save)` inject hardware-free inventory
  and persistence; `LoadDisk`/`Refresh` are the production wrappers.
* `FMatrix::Capture` builds compact interface spans and stored-channel exceptions;
  `Column` maps rectangular column indices to exact endpoint/channel pairs.
* `Visible` bounds painting to the viewport; the widget additionally intersects
  ancestor culling and uses that same clip for pointer input.
* `CanAssign` / `Assign` provide fresh guarded, transactional offline mutation.
* Runtime `UAudioLoomComponent::IsRouteEditStopped()` is a read-only state query;
  it does not poll events, create audio, stop replay or change worker state.

`SpatialFabric.AudioRouting.*` native tests cover cache round trips/offline loads,
explicit valid refresh, unplug retention, errors/duplicates/bounds/schema/write
failure, compact 4096-channel mapping, visible ranges, native DeviceId/channel
mutation, one-step Undo/Redo, unrelated values, unknown/out-of-range preservation,
locked/PIE/inherited/missing guards, actual panel construction, actual bounded
Slate paint for 10,000 rows × 4096 channels, ancestor-clipped pointer rejection and
real per-user mouse capture, down/up route mutation and one-step Undo, configured
no-ops, capture loss, movement/scroll/refresh cancellation, replaced/expired targets,
fresh authoring guards, clipped captured clicks/wheels and both-axis wheel boundaries.
Tests deliver native widget events and apply Slate replies in a disposable virtual
window; they do not inject OS input. Existing transport tests additionally exercise
the pending-replay/asynchronous-start/undrained-state query and actual Undo followed
by a registered-component tick, proving that restoring an endpoint cannot auto-restart.
`Tools/validate_unreal.py` requires the three routing tests in addition to the
previous network/scene-object regressions.

Fresh UE **5.8 Win64** verification passed:

```sh
python Tools/validate_unreal.py --engine "C:/Program Files/Epic Games/UE_5.8" --output C:/Temp/SFUnrealRouting-final-clipping
```

- BuildPlugin: **Editor Development, Game Development and Game Shipping** passed.
- Native automation: **44 passed**, zero failed/not-run/in-process; 43 clean and
  one existing expected raw-SoundWave-data warning in the sidecar test.
- Evidence: `C:/Temp/SFUnrealRouting-final-clipping/Report/index.json`, `build.log`
  and `tests.log`. This gate includes the final capture/clipping and Undo fixes.
- Production and focused follow-up reviews passed. Tests used synthetic inventory
  and `-NoSound -NullRHI`; no physical output, capture or hotplug was performed.

## Manual follow-up (not automated certification)

Check the docked window at mixed DPI/narrow widths, pointer down/up across scroll
and refresh, both-axis scrollbar dragging, long/duplicate IDs, tooltip readability,
Undo after component reconstruction, and save/reopen scene serialization. Exercise
mixed Blueprint/native/inherited components, filesystem and source-control locks,
PIE transition timing and actual unplug/reconnect separately. The NullRHI test
covers native paint/hit boundaries, not a graphics-enabled visual review or a full
OS-delivered click/dropdown interaction. Source-control checkout policy beyond
filesystem read-only detection remains native-editor workflow. The new tab does
not promise physical stream compatibility or source-width validation. No assets,
installed native binaries, release archives or user project settings were changed
by this routing UI implementation.
