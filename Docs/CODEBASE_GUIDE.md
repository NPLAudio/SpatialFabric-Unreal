![SpatialFabric / AudioLoom](../assets/spatialfabric-unified.png)

# Codebase guide: responsibilities, lifetime and extension points

[Documentation index](README.md) · [Build/test workflow](Development.md)

## Repository and module boundaries

One descriptor owns four modules. Keep these names stable for serialized assets,
Blueprints, Engine configuration and existing C++ consumers:

| Module | Host | Responsibility |
|---|---|---|
| SpatialFabric | Runtime | Stage, registry, bindings, protocol router, ADM/Custom, spatial OSC |
| SpatialFabricEditor | Editor | Seven-tab Spatial Fabric panel: spatial controls, Scene Objects authoring, offline-first Audio Routing crosspoints/selectors and editor/PIE network ownership |
| AudioLoom | Runtime | SoundWave decode/cache, PCM cursor/encoding, device backend, playback OSC |
| AudioLoomEditor | Editor | Playback widgets/details, CSV, transactional edits and reimport handling |

The new UI embeds `SAudioLoomPanel` in Playback. Retaining module/class names is
not the same as automatically merging the two signal paths: spatial output is
control data; direct playback supplies samples to OS devices. There is no inferred
renderer-input/channel association between components on the same actor.

`Source/Shared/SpatialFabricOSCReceiver.h` is a shared private include, not another
module or reflected class. Both runtime modules use it without a dependency cycle.
Scene discovery now gives SpatialFabric a private, one-way dependency on AudioLoom
for typed read-only inventory. AudioLoom dispatches query requests through the
Core-only `SpatialFabricSceneQueryBridge.h` modular-feature interface, not a reverse
module dependency. The versioned command/local JSON API is implemented in
`SpatialSceneQuery.cpp` / `SpatialSceneQueryLibrary.h`; see [scene query](SceneQuery.md). `Examples/SpatialFabricShowcase/Source`
is a separate original host-project module, not part of the plugin's four modules.

## Spatial frame pipeline

1. Manager resolves its Stage Volume **in its own world** and applies visibility.
   Soft references/label fallback must not accidentally bind the editor copy while
   running in PIE. Stage reassignment/clear must take effect without stale cached state.
2. Stage resolves a virtual listener basis when enabled. It never moves an authored
   ancestor toward its own child. Fixed mode uses the box frame; followed mode uses
   the chosen actor's location/rotation.
3. Registry walks enabled bindings, resolves valid target actors and creates
   `FSpatialNormalizedState` values. `BindingIndex` ties a compact snapshot entry back
   to its original binding when another actor is missing. It is not inferred from
   the compacted array position. ADMName remains distinct from Label.
4. Router filters per adapter and resolves each target's ID override/CustomFields.
   The same actor can have different IDs in two protocol outputs.
5. Adapter `BuildMessages` is a pure encoding path shared by preview/output. Its
   complete message list includes metadata, tokens and routing-dependent addresses.
6. Rate limiting gates batches; transmit-on-change compares encoded messages.
   Transport reuses endpoint clients. UDP acceptance is not delivery confirmation.

Main types live in SpatialFabricTypes.h. The runtime produces normalized snapshots;
legacy/raw symmetry types are not evidence of an additional active capture pipeline.

## Coordinates and visibility

Stage-local values are axis-clamped [-1,1]; meter conversion multiplies physical
half-size. ADM limits the point radially to 1 before both XYZ/AED encoding. Its
Cartesian output is `(right, front, up)`; polar azimuth uses `atan2(-right, front)`.
Custom handles its own template/range conventions. Do not reintroduce the obsolete
“negate Y only” Cartesian description or inconsistent AED distance scaling.

Follow mode uses a virtual basis and identity listener output because objects are
already relative. Player view-target actor origin/rotation is not an HMD/camera-eye
pose API. See the [operator reference](SpatialRouting.md) before changing semantics.

Visibility policy is separate: editor stage display is restored even in Game View
and without selection; only game worlds obey the PIE/game hide flag. Component
visibility and inherited HiddenInGame/selection-only state must agree. Avoid marking
render state dirty every frame when a property has not actually changed. Visibility
must not change stage transforms or routing.

## Playback pipeline

1. `UAudioLoomComponent::Play` cancels a prior instance/replay and calls PrepareAudio.
2. Loader validates imported WAV or supported inline cooked data, then prepares
   interleaved float PCM. First decode is synchronous; immutable shared storage
   avoids a full clip copy for every replay. Streaming/certain channel-order policies
   fail explicitly instead of spinning or searching a source-file fallback.
3. Backend startup is asynchronous. The selected device/mix format supplies the
   actual render rate/layout. Explicit missing devices do not silently choose default.
4. `FAudioLoomPcmCursor` advances at source-rate/device-rate, interpolates frames,
   performs channel mapping, wraps when looping and emits zeros beyond EOF.
5. `AudioLoomEncodePCM` writes negotiated float/integer packed samples. The Windows
   worker checks stream operations and drains queued one-shot audio. Mac callbacks
   validate/pre-zero layouts and avoid callback allocation/logging; Mac testing is separate.
6. Worker events drive actual playing/stopped monitoring. Natural completion can
   arm an auto-replay delay; explicit Stop cancels it. EndPlay/unregister teardown
   stops/joins workers and prevents stale callbacks/queues from leaking into a replay.

The component cache must be invalidated when its SoundWave content/routing changes
or on reimport. A tooling import-overwrite can differ from an ordinary Reimport
notification: the focused audio refresher explicitly invalidates prepared data by
reassigning the same asset through the reflected property after import.

## Threading and lifetime

- Actor resolution, snapshots, pure protocol encoding, Blueprint delegates and
  public transport requests are game-thread work.
- Exact-bind OSC input is bounded nonblocking receive on the **core ticker**, not a
  world/component tick dependency. This preserves receive behavior when paused or
  a viewport is not realtime.
- The receiver's ticker captures weak self; a pinned receiver and copied dispatcher
  survive a callback that stops/releases its owner. UObject owners are weakly captured.
  Unregister/BeginDestroy/deinitialize release sockets/ticker handles promptly.
- OS PCM rendering uses its platform worker/callback. Immutable PCM and synchronized
  state/events are the boundary; do not touch UObjects, allocate large buffers or
  log from a real-time callback.
- Replay timing is component/world ticking, not distributed/sample-clock scheduling.
- Editor delegates release editor input sockets before PIE duplication/start, transfer
  an active playback listener, and restore state after PIE. Centralized ownership
  avoids two panels/worlds binding the same endpoint.

## Public APIs: reflection is explicit

A C++ method's public visibility does not make it a Blueprint node. Check its
`UFUNCTION` annotation. For example Manager `ProcessFrame`, `InitializeAdapters`
and `BuildOutputPreview` are C++ helpers; reflected control includes manager
spawn/server/client/log functions and stage conversion/listener helpers. AudioLoom
transport/prepare/error/device/channel/path methods are reflected as documented.

`UPROPERTY`/`USTRUCT` fields are serialized/editor-visible according to their flags;
private non-UObject socket helpers are not saved runtime objects. Actor soft paths
are not filesystem paths. Weak references must be validated against the correct
world/lifetime, not merely checked for a non-null cached address.

## Editor / data operations

The panel is an operator surface, not an automatic actor spawner. Changes should
participate in transactions. Format changes preserve saved targets/custom data.
CSV parses records (including quoted multiline fields) and validates numbers before
applying each row; valid rows can apply while others report errors. Asset import
and editor undo behavior still need their full integration matrix, not only unit tests.
Logs are bounded and negative/oversized requests must not read invalid indices.

Config distinction: SpatialFabric settings use `DefaultSpatialFabric.ini`;
AudioLoom OSC retains `DefaultEngine.ini`. Adapter actor settings can differ from
project defaults. Do not document a non-existent independent EnableInPIE field.

## Reading order

1. SpatialFabricTypes.h and ISpatialProtocolAdapter.h.
2. SpatialStageVolume.cpp and SpatialObjectRegistry.cpp.
3. ProtocolRouter.cpp, ADMOSCAdapter.cpp and CustomAdapter.cpp.
4. SpatialFabricManagerActor.cpp and managed OSC client/server components.
5. Source/Shared/SpatialFabricOSCReceiver.h and its packet/lifecycle regressions.
6. AudioLoomPcmLoader.cpp, AudioLoomPcmRender.cpp and playback backend.
7. AudioLoomComponent.cpp and AudioLoomOscSubsystem.cpp.
8. SpatialFabricEditorModule.cpp, both panel implementations and CSV code.
9. Production regressions, native/example validation runners and operator guides.

## Add or change behavior safely

Separate encoding from effects, preserve stable binding identity and world ownership,
then test malformed input/teardown as carefully as the happy path. For a new protocol,
add its reflected enum/config entry, implement the adapter interface, wire factory/UI
registration, and verify actual packets plus preview equivalence. Do not count a UI
entry as renderer certification. For device changes, validate real format/rate/channel
behavior on the target OS/interface in addition to the pure cursor/encoding tests.

See [VALIDATION.md](../VALIDATION.md) for authoritative JSON checks, current evidence,
manual gates and what is intentionally not certified. Historical AUDIT.md explains
why some apparently defensive structure exists; it is not current test output.
