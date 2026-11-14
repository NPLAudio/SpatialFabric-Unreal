# Current work

## Completed

- [x] Consolidate AudioLoom runtime/editor modules into SpatialFabric while preserving class/module identity.
- [x] Move the independent plugin Git repository into `SpatialSandbox/Plugins/SpatialFabric`.
- [x] Implement audit remediation and promote real regressions into the repository.
- [x] Validate UE 5.8 Win64 Editor, Development, Shipping; 21 native tests passed.
- [x] Validate 12 Python layout/branding/result-runner checks.

## In progress — execute in this order

- [x] Configure Pi for Unreal's existing built-in MCP endpoint and verify it targets SpatialSandbox (`/Game/FirstPerson/Lvl_FirstPerson`).
- [x] Merge SpatialFabric and AudioLoom branding in documentation and the plugin icon; retain source artwork, attribution, and a reproduction script.
- [x] Final review of plugin changes, migration guidance, build/test evidence, and generated-file exclusions.
- [ ] Commit plugin changes to its SpatialFabric Git repository and push to `origin` (NPLAudio/SpatialFabric).
- [ ] Build a demonstration level in SpatialSandbox covering spatial routing, listener-relative coordinates, direct playback/channel routing, OSC, and management tools.
- [ ] Import free assets only if useful; record source URLs, license terms, and attribution. Prefer existing project/engine assets where possible.
- [ ] Exercise the demonstration in Unreal, save the map/assets, and document how to run it and what still needs external audio hardware/software.

## Explicit validation gates

- [ ] Physical Windows audio-device/rate/format/latency/disconnect testing.
- [ ] Actual cooked Force Inline SoundWave/codec playback verification in a packaged project.
- [ ] Native macOS/Linux and older-engine builds before claiming support for those combinations.
- [ ] Full interactive editor/PIE hand-off, undo/redo, and hardware/receiver capture matrix in `VALIDATION.md`.

Plugin commits must not accidentally include sandbox Content/Saved/Intermediate or local MCP credentials/configuration.
