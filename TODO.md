# Current work

## Completed

- [x] Consolidate AudioLoom runtime/editor modules into SpatialFabric while preserving class/module identity.
- [x] Move the independent plugin Git repository into `SpatialSandbox/Plugins/SpatialFabric`.
- [x] Implement audit remediation and promote real regressions into the repository.
- [x] Validate UE 5.8 Win64 Editor, Development, Shipping; 25 native tests passed (`SpatialFabricValidation-5gg8mmdf`).
- [x] Validate 14 Python layout/branding/result-runner/demo-tooling checks.
- [x] Fix live-demo discoveries: exact loopback binding, real bind-failure status, bounded OSC decoding, immediate editor unregister cleanup; version 2.0.1.

## Requested follow-up

- [x] Configure Pi for Unreal's existing built-in MCP endpoint and verify it targets SpatialSandbox (`/Game/FirstPerson/Lvl_FirstPerson`).
- [x] Merge SpatialFabric and AudioLoom branding in documentation and the plugin icon; retain source artwork, attribution, and a reproduction script.
- [x] Final review of plugin changes, migration guidance, build/test evidence, and generated-file exclusions.
- [x] Commit/push the unified plugin/branding and follow-up receiver/demo tooling to `origin/main` (NPLAudio/SpatialFabric); initial push `5624f34`.
- [x] Build `/Game/SpatialFabricDemo/Maps/L_SpatialFabric_Showcase`: four spatial objects, listener-relative coordinates, three direct-playback/channel stations, simultaneous ADM/Custom OSC, and management-panel workflow.
- [x] Use engine/project assets and generated tones; no third-party downloads needed. Record provenance/terms in `Docs/SpatialSandboxDemo.md`.
- [x] Save the separate sandbox map/assets; publish a reproducible source builder without committing sandbox binary content.
- [x] Exercise 24 live PIE checks (including paused-world OSC play/stop), five post-PIE restoration checks, and independent UDP capture (15,458 messages; zero decode/XYZ-AED failures). Document operation and remaining external hardware/software gates.

## Explicit validation gates

- [ ] Physical Windows audio-device/rate/format/latency/disconnect testing.
- [ ] Actual cooked Force Inline SoundWave/codec playback verification in a packaged project.
- [ ] Native macOS/Linux and older-engine builds before claiming support for those combinations.
- [ ] Full interactive editor/PIE hand-off, undo/redo, and hardware/receiver capture matrix in `VALIDATION.md`.

Plugin commits must not accidentally include sandbox Content/Saved/Intermediate or local MCP credentials/configuration.
