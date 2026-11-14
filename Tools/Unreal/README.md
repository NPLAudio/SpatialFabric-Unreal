# Unreal showcase tools

See [the complete tour, setup and validation instructions](../../Docs/SpatialSandboxDemo.md).

Run `build_spatialfabric_demo.py`, `inspect_spatialfabric_demo.py`,
`begin_spatialfabric_handoff_check.py`, `validate_spatialfabric_demo.py`, and
`finish_spatialfabric_handoff_check.py` **inside Unreal Editor Python**, not system
Python. The live validator intentionally plays quiet generated tones during PIE.

Run `capture_spatialfabric_osc.py` with **ordinary Python** for independent local
UDP capture. It refuses occupied ports and never sends commands.

The scripts are source-only. They generate project content outside the plugin;
no Unreal `.umap`/`.uasset` files or external sample packs are distributed here.
