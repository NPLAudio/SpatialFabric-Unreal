# Central Scene Object Manager (Unreal)

Open **Window → Spatial Fabric → Scene Objects**. Stage, Objects, Radar, Output,
and Playback remain available in the same window. Scene Objects requires **no
manager actor**. The normal Actor Details panel is unchanged.

The window now opens on **[Audio Routing](AudioRoutingMatrix.md)** by default.
That manager-free crosspoint tab lists all loaded editor-world playback components,
with offline-aware interface/channel dropdowns and explicit Refresh Interfaces.
Scene Objects retains its native embedded Details rather than duplicating routing
logic. Route safety and native test coverage are documented in the linked guide.

## Find and configure actors

The list contains **loaded, placed editor-world actors**, including ordinary,
unconfigured, hidden, inactive-level and rootless actors. It does not load World
Partition descriptors, levels, assets, or actors. Transient editor helpers,
templates, CDOs and dead actors are excluded. Names need not be unique: selection
uses weak actor identity, never a label or an array index.

Search labels, class names, paths or configuration status. Use Ctrl/Shift for
multi-selection. This selection is local to the tab; it does not replace the Level
Editor's selection. Filtering drops selection of rows no longer visible. Rows
show Playback/Live Input component counts, SpatialFabric binding counts across
loaded managers, and read-only / missing configuration status.

* **Add Playback** adds `UAudioLoomComponent` to each eligible selected actor.
* **Add Live Input** adds `UAudioLoomLiveInputComponent`, without starting capture.
* Each action preflights **all** selected actors. PIE/SIE, noneditable actors,
  locked levels, templates, transient or foreign-world targets block the action.
* Already-present types (including subclasses) are skipped, not replaced or
  duplicated. Existing multiple components are kept and all appear in Details.
* Components are created through `USubobjectDataSubsystem` using actor-instance
  handles, not Blueprint/template handles. Instance ownership, registration and
  serialization follow Unreal's native editor path. These non-scene components
  do not require attachment or a root. Unreal's own Undo/Redo reconstruction may
  introduce editor helper components on otherwise rootless actors.
* One bulk action is one Undo step; Redo restores it. An unexpected engine add
  failure after preflight is reported, and any earlier additions remain undoable
  together. There is no silent partial success.

## Spatial tracking and bindings

SpatialFabric tracks actors through **manager `ObjectBindings`**, not through a
per-actor tracking component. OSC utility components are deliberately not offered
as substitutes for tracking.

1. Select an **existing** manager in the list and click **Use selected manager**.
2. Select the actors to track, then **Bind selected to existing manager**.
3. The target manager's real native Details remain below the selected actors'
   settings. Expand **ObjectBindings** to edit actor references, IDs, gain, width,
   flags and adapter targets. Each entry's TargetActor links it to the list actor;
   row binding counts reflect these entries. The old **Objects** tab is another
   editor for the same array, not a second data store.

Binding never spawns a manager. It preflights target manager and actors, rejects
manager/stage actors as tracking targets, skips actors already bound to that
manager, and preserves all existing IDs and adapter target entries. New bindings
use a fresh ID above existing IDs/overrides and the manager's active adapter type.
The action is transactional. Selecting another manager replaces the binding
target; its name is always shown in the status line and Details group.

## Native values and mixed editing

Details groups contain real AudioLoom components, SpatialFabric managers and
stage actors. They edit the **same stored properties as individual Actor Details**.
All instances of a supported type participate, including multiple components on
one actor. Group captions identify each instance; unequal values use Unreal's
standard mixed-value display and property transactions. No proxy settings are
copied back later. Generic inherited Actor / ActorComponent properties are
filtered out of this tab; edit transforms and other engine settings in native
Actor Details instead.

Custom detail layouts are disabled **only in these embedded views**. This avoids
existing AudioLoom layouts' first-instance assumptions and transport buttons.
Consequently device/channel properties use ordinary native property rows rather
than the custom transport/device UI. Read-only actor/level and inherited/native
component restrictions disable a whole affected group rather than editing only
its first eligible instance. During PIE/SIE all authoring is disabled; the list
continues to describe the editor world, never PlayWorld.

Discovery polls loaded actor/component identity, labels, status and binding
membership every 0.5 seconds. Each refresh indexes loaded managers' binding records
once, then caches each actor's component counts, identity signature and status text.
Status work is O(actors + bindings + components), not an actor-by-actor manager scan;
search, row text and change detection consume the cache. Duplicate binding records
count separately, including records across multiple managers. Actor replacement
replaces cache identities even when actor counts are unchanged. The Slate list
remains virtualized. Undo/Redo and explicit Refresh rebuild immediately.
Refresh also rebinds native views after component reconstruction. The tab
registers only an editor Undo client, unregistering it on destruction; it creates
no runtime listeners, audio streams, capture, networking or processing loops.

## Side-effect boundary

Browsing, searching, selecting, and adding functionality do not call transport,
`ProcessFrame`, adapter initialization, or network binding rebuild APIs. Binding
only modifies the manager's authored array.

Native manager property/Undo callbacks now use configuration-only adapter
initialization when a router already exists: configurations synchronize, but the
callbacks do **not** call Connect, open/close a transport, or create a router.
Connected clients keep their existing endpoint; disconnected clients and stopped
listeners stay stopped. This includes endpoint/configuration edits: they do not
retarget an existing connection during the callback. The public no-argument
`InitializeAdapters()` retains its previous connecting behavior; its private bool
overload supplies the configuration-only path.

Unreal's base actor property/Undo callbacks temporarily unregister components.
The editor-only manager Pre/Post change pair preserves the existing server during
that reconstruction. Component PreEditUndo also guards its temporary unregister
because Unreal can notify the component before its manager. Ordinary unregister,
component/owner deletion, EndPlay, BeginDestroy and explicit Stop still close the
server. These narrowly scoped editor lifecycle changes do not alter normal runtime
start/reinitialization or sending behavior.

This is a **callback boundary, not a networking mute switch**. Source inspection
confirmed that `BeginPlay` / router-less `ProcessFrame` initialize networking;
ADM/Custom adapter frame sends call Connect to retarget the shared client, and
explicit `SendCustomOSC` connects and restores its target. Existing explicit
Start/Connect/reinitialization and the editor ticker's ProcessFrame path remain
unchanged. An already-running manager can therefore send/reconnect on a later
frame, independently of authoring callbacks. Use a non-running authoring scene
when network isolation is required.

## Validation

Run from the plugin directory:

```text
python Tools/validate_unreal.py --engine "C:/Program Files/Epic Games/UE_5.8" --editor-only
```

The isolated project uses NullRHI, NoSound and no live microphone. Five required
`SpatialFabric.SceneObjects.*` tests exercise production discovery/preflight,
transactional bulk component creation, duplicate prevention, one-step Undo/Redo,
read-only/locked/PIE rejection, real property transactions and binary property
serialization, and binding ID/adapter preservation and Undo/Redo. Fixtures use
disposable editor worlds and never tick a manager or open a real transport.
They also construct the production tab headlessly and check actor replacement.

The two followup regressions are:

* `InitializedCallbacksPreserveTransport`: an initialized production router with
  private, per-instance fake transport hooks under `WITH_DEV_AUTOMATION_TESTS`.
  With networking policy enabled, real Pre/Post property callbacks and binding /
  configuration Undo/Redo must make zero client Connect/Disconnect or server
  Start/Stop attempts, retain the router identity and preserve connected,
  disconnected and stopped states. Real router adapter enablement follows config
  edits and Undo/Redo. A fully shut-down manager stays router-less; explicit
  reinit/Start still reaches transport entry points. Ordinary unregister after
  the callbacks still stops the server.
* `StatusIndexCountsAndIdentity`: calls the same cache builder used by refresh,
  checking multiple managers, duplicate binding records, independent actor and
  component counts, cached status/signatures, same-count actor replacement and
  count replacement on the next refresh.

Initial scene-manager UE 5.8 editor-only validation: **40 successes + 1 success with warnings =
41 tests (39 baseline + 2 new), zero failures/not-run/in-process**. Evidence:
`C:/Users/davo_/AppData/Local/Temp/SpatialFabricValidation-followup-20261118-0934/Report/index.json`
with `build.log` and `tests.log` in that same evidence directory. The sole warning
is the baseline `SpatialFabric.Playback.LegacySidecarAndBudget` missing imported
SoundWave raw-data warning. Earlier failed runs exposed Unreal's temporary
unregister during property edits and component-first Undo; the passing run includes
both preservation fixes. The full baseline suite retains its existing local
loopback UDP tests; the new authoring regressions use only fake transports. No
external network destination, hardware audio or capture was exercised.

The subsequent [Audio Routing](AudioRoutingMatrix.md) gate includes all these
regressions: **44 native passed** (43 clean, one existing warning), plus Editor
Development / Game Development / Game Shipping builds. Latest evidence:
`C:/Temp/SFUnrealRouting-final-clipping/Report/index.json`.

Binary serialization coverage is not a full map-save/reload integration test.

Manual acceptance still needed on a graphical editor: search and Ctrl/Shift
selection, native mixed-value display, inherited Blueprint component restrictions,
map save/reopen, actor deletion/replacement, loaded inactive/World Partition
levels and native Actor Details parity. Do not activate audio/microphone or a
network adapter for this authoring verification.
