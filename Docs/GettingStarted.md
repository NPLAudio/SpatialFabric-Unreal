# Getting started

[Documentation index](README.md) · [Troubleshooting](Troubleshooting.md)

## Before opening a project

SpatialFabric is a **source plugin**. Use UE 5.8 and a supported C++ toolchain;
Win64 is the validated combination. Close Unreal and back up an existing project
before replacing plugin modules. Never install standalone AudioLoom alongside the
combined plugin: both define `/Script/AudioLoom` classes.

For the quickest tour, follow the [prepared example](../Examples/SpatialFabricShowcase/README.md).
It contains the map and a small original walking character, not an external
FirstPerson template dependency. Prepare a copy outside the repository, build its
Editor target, then open its `.uproject`. Initial shader compilation is normal.

For another project, copy the plugin to `Project/Plugins/SpatialFabric`; enable
SpatialFabric and the built-in OSC plugin. Regenerate project files/build with
Visual Studio's Unreal C++ toolchain. A precompiled DLL from a different engine
build is not interchangeable just because its filename matches.

## The panel is the main operating surface

Open **Window → Spatial Fabric**. If docked too small, enlarge the panel or move
its tab to a larger stack; controls are scrollable. The five tabs are:

| Tab | Use it for |
|---|---|
| Stage | Assign a measurement box; choose fixed or listener-relative coordinates |
| Objects | Patch actors to positive spatial IDs and protocol targets |
| Radar | Check relative movement and aspect/axis expectations |
| Output | Read encoded messages and a bounded recent message log |
| Playback | Patch SoundWaves to OS devices/channels; run transport and CSV tools |

A missing Manager is a setup condition, not a reason to silently add actors.
Explicitly use the spawn action when requested. Direct playback does not require
a Manager or stage at all.

## First spatial output: prove the control patch

1. Place/spawn a Spatial Fabric Manager. Assign a Spatial Stage Volume in Stage.
2. Fit the box to your intended space. The default full dimensions are 20 × 15 × 8 m;
   the origin is its center, not its floor. Match the physical-dimension fields
   to the box and your receiver's interpretation.
3. Select one visible actor. In Objects use **+ Add Selected Actors**, assign ID 1,
   and keep an ADM target enabled. Do not start with a whole complicated level.
4. Set the ADM destination to `127.0.0.1:50018` for a receiver on this computer.
   New managers enable ADM; the example also enables Custom on `9002`.
5. Watch Output, move the actor, and inspect actual packets in the receiver.
   Test center/front/right/up separately. Confirm object numbering and coordinate
   mode with the destination's own documentation.
6. Only after that works, change to a trusted remote IP, confirm firewall/ports,
   then add the rest of the bindings.

The actor's gain/mute/width fields are **renderer metadata**, not automatic gain,
mute or panning controls for a separate AudioLoom stream.

## First cue: prove the audio patch

1. Add an Audio Loom actor/component. Import/assign a short known SoundWave.
2. Pick an explicit output device if the endpoint must not change with the OS
   default. Start with a low-level cue and a known physical patch.
3. Use output `0` for corresponding source-channel mapping; use `1..N` to send the
   **first source channel only** to a particular device channel. A mono cue on 0
   does not automatically duplicate to every speaker.
4. Leave Begin Play/autoplay off. Press Play, verify the physical output, then Stop.
5. Use Prepare Audio during setup/loading before timing-sensitive cues. The initial
   decode is synchronous; this is not a guarantee of zero onset latency.
6. Test Loop and Auto Replay separately. Explicit Stop cancels pending replay.

Unreal attenuation, SoundClass/submix routing and spatial renderer configuration
are not automatically inserted into this OS-device path.

## Connecting spatial control and audio

Treat it as a real system patch: which audio inputs reach the external renderer,
which spatial IDs address those inputs, which hardware outputs feed the speakers,
and who owns overall mute/level? SpatialFabric does not infer that relationship.
Loopback/virtual devices may be useful, but their drivers, channel order and latency
must be tested like any other interface. The example's stereo groove is not proof
of external spatial rendering.

## Save a known-good baseline

Save the level, export Playback routing CSV, note device IDs and channel numbering,
and record endpoint IP/ports. Keep media sources and the engine/plugin revision.
Changing project defaults does not necessarily rewrite adapter settings already
serialized on a placed Manager. Inspect the actor's actual AdapterConfigs.

## Before a demo or show

- Confirm exact engine/plugin build and open the intended map.
- Close any second editor/OSC listener using ports 8100/9000.
- Confirm safe system level, output device and physical channel patch.
- Test Play → natural EOF → Stop, Loop off → drain, and replay cancellation.
- Check Output against a real UDP receiver, not just an animation or preview.
- Understand stage-fixed versus listener-relative mode before moving a player.
- Keep an independent emergency mute; OSC is not a safety-rated transport.
- For packaged shows, explicitly enable each required network path and perform
  the actual Force Inline codec/package tests in [VALIDATION.md](../VALIDATION.md).
