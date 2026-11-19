# SpatialFabric Unreal documentation

This is [SpatialFabric-Unreal](https://github.com/NPLAudio/SpatialFabric-Unreal).
Shared OSC contracts, fixtures and controller source live in the
[SpatialFabric hub](https://github.com/NPLAudio/SpatialFabric) (formerly `SpatialFabric-Interop`).
Download OSC Controller 1.4.0 from the [hub 1.0.0 release](https://github.com/NPLAudio/SpatialFabric/releases/tag/v1.0.0).
[Unity](https://github.com/NPLAudio/SpatialFabric-Unity) and
[Godot](https://github.com/NPLAudio/SpatialFabric-Godot) have separate repositories and validation.

## Pick a workflow

1. **[Getting started](GettingStarted.md)** — installation, first launch, first
   position patch, first cue, and a pre-show checklist.
2. **[Spatial routing](SpatialRouting.md)** — stage sizing, object identity,
   coordinate conversion, ADM/Custom, metadata and listener following.
3. **[Direct audio](AudioPlayback.md)** — channel patching, devices, preparation,
   loops/replay, CSV, cooked media and migration. See also
   [AudioLoom playback compatibility and fresh software checks](PlaybackParity.md).
4. **[OSC reference](OSCReference.md)** — exact addresses, types, ports, config
   files, network permissions and an executable sender example.
5. **[Showcase tour](SpatialSandboxDemo.md)** — prepared example, ten-minute
   walkthrough, asset refresh and live validation tools.
6. **[Troubleshooting](Troubleshooting.md)** — silence, wrong channels, missing
   packets, invisible stage, slow updates, duplicate paths and missing modules.
7. **[Development](Development.md)** — source builds, portable example creation,
   automated validation, distribution, editing generated assets and documentation.
8. **[Codebase guide](CODEBASE_GUIDE.md)** — four modules, runtime data flow,
   threading/lifetime, source reading order and extension points.

**[Audio Routing crosspoint grid](AudioRoutingMatrix.md)** — default editor tab,
offline interfaces, exact channels, Refresh Interfaces and guarded Undo.

**[Scene Objects manager](SceneObjectManager.md)** — editor-world inventory and
safe authoring without a manager actor.

**[Scene query / discovery](SceneQuery.md)** — read-only JSON/OSC inventory,
pagination and parsing, plus Python, Node.js, Max and Node-RED examples.

## Ready-made material

- [Playable UE 5.8 source project](../Examples/SpatialFabricShowcase/README.md)
- [Current release contents and installation](../RELEASE_NOTES.md)
- [Historical presentation downloads (2.0.3)](https://github.com/NPLAudio/SpatialFabric-Unreal/releases/tag/v2.0.3)
- [Current validation evidence and outstanding release gates](../VALIDATION.md)
- [Remediation/change summary](../FIXES.md)
- [Task tracking](../TODO.md)

`AUDIT.md` is a historical pre-remediation report, not a current list of unfixed
issues and not evidence of platform/hardware certification. Documentation describes
current implemented behavior; the validation page distinguishes implementation
from combinations actually exercised.

## Three distinctions to keep on the patch sheet

- A **spatial object ID** is not an **audio output channel**.
- OSC **position data** is not a stream of PCM **audio samples**.
- A **successful send/preview** is not proof that the receiver rendered sound.

Write both signal paths down before a rehearsal. Keep levels low while confirming
physical output assignments and always retain an independent system mute.
