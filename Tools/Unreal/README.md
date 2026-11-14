# Unreal showcase tools

See [the complete tour, setup and validation instructions](../../Docs/SpatialSandboxDemo.md).

Run `build_spatialfabric_demo.py`, `inspect_spatialfabric_demo.py`,
`begin_spatialfabric_handoff_check.py`, `validate_spatialfabric_demo.py`, and
`finish_spatialfabric_handoff_check.py` **inside Unreal Editor Python**, not system
Python. The live validator intentionally plays quiet demo sounds during PIE.

`refresh_spatialfabric_demo_audio.py` runs inside Unreal to replace just the
existing cues/signage while preserving geometry and routing. `demo_audio.py`
contains the original warm percussion, stereo groove and glass/mechanical sound designs;
it can also generate WAVs using ordinary Python with `--output <folder>`.

`extend_spatialfabric_demo_floor.py` adds/updates a continuous safety floor and
perimeter collision barriers without rebuilding the gallery, then checks actual
floor and wall traces.

Run `capture_spatialfabric_osc.py` with **ordinary Python** for independent local
UDP capture. It refuses occupied ports and never sends commands.

The tools are source-only, but a checked-in project/map/media snapshot is now
available in `Examples/SpatialFabricShowcase`. Generated working content and Saved
reports remain in the active host project. No external sample pack is bundled.

`smoke_example.py` is for the disposable example-validation runner only: it starts
PIE, checks walking collision/animation/decoding, stops PIE and quits the editor.
It plays no audio. Never run that tool in a working editor with unsaved work.
