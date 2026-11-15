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

`rearrange_spatialfabric_demo_rooms.py` replaces only generated environment/signage
with four enclosed rooms and a hallway, preserving bound objects, animation and cue
routing. It backs up the map, refuses dirty/active work, and verifies 21 collision traces.
`room_layout.py` is the shared engine-free plan, also covered by geometry tests.

`extend_spatialfabric_demo_floor.py` is only for the old outdoor gallery. It refuses
the room map rather than reintroducing a 120 m exterior that serves no demo.

Run `capture_spatialfabric_osc.py` with **ordinary Python** for independent local
UDP capture. It refuses occupied ports and never sends commands.

The tools are source-only, but a checked-in project/map/media snapshot is now
available in `Examples/SpatialFabricShowcase`. Generated working content and Saved
reports remain in the active host project. No external sample pack is bundled.

`smoke_example.py` is for the disposable example-validation runner only: it starts
PIE, checks walking collision/animation/decoding, stops PIE and quits the editor.
It plays no audio. Never run that tool in a working editor with unsaved work.

`publish_room_assets.py` is commandlet-only and requires a marked disposable
publication project. Use ordinary Python `Tools/publish_room_example.py` to build,
sanitize, walk-test and publish only required room assets from a saved sandbox.
The untagged outside plane remains untouched in the sandbox, not in the shipped map.
