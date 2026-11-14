# Current work

## Completed

- [x] Consolidate AudioLoom runtime/editor modules into SpatialFabric while preserving class/module identity.
- [x] Move the independent plugin Git repository into `SpatialSandbox/Plugins/SpatialFabric`.
- [x] Implement audit remediation and promote real regressions into the repository.
- [x] Validate UE 5.8 Win64 Editor, Development, Shipping; 26 native tests passed (`SpatialFabricValidation-7d4mvdvu`).
- [x] Validate 22 Python layout/branding/result-runner/audio/example/docs/presentation checks.
- [x] Fix live-demo discoveries: exact loopback binding, real bind-failure status, bounded OSC decoding, immediate editor unregister cleanup; version 2.0.2.

## Requested follow-up

- [x] Configure Pi for Unreal's existing built-in MCP endpoint and verify it targets SpatialSandbox (`/Game/FirstPerson/Lvl_FirstPerson`).
- [x] Merge SpatialFabric and AudioLoom branding in documentation and the plugin icon; retain source artwork, attribution, and a reproduction script.
- [x] Final review of plugin changes, migration guidance, build/test evidence, and generated-file exclusions.
- [x] Commit/push the unified plugin/branding and follow-up receiver/demo tooling to `origin/main` (NPLAudio/SpatialFabric); initial push `5624f34`.
- [x] Build `/Game/SpatialFabricDemo/Maps/L_SpatialFabric_Showcase`: four spatial objects, listener-relative coordinates, three direct-playback/channel stations, simultaneous ADM/Custom OSC, and management-panel workflow.
- [x] Use engine/project assets and original procedural sound designs; no third-party downloads needed. Record provenance/terms in `Docs/SpatialSandboxDemo.md`.
- [x] Save the separate sandbox and publish a reviewed example snapshot under `Examples/SpatialFabricShowcase`, including assets, source audio, manifest, minimal config and original walking-player source.
- [x] Exercise 24 live PIE checks (including paused-world OSC play/stop), five post-PIE restoration checks, and independent UDP capture (15,458 messages; zero decode/XYZ-AED failures). Document operation and remaining external hardware/software gates.

- [x] Replace calibration tones with original warm percussion hit, stereo musical groove and glass/mechanical cues; preserve asset names, durations, channels and manual-start behavior. Refresh existing assets without rebuilding the stage.

- [x] Fix stage visibility in editor Game View and while unselected; keep PIE/game hide control separate and add a native regression.
- [x] Add a 120 × 120 m safety floor and 3.5 m perimeter barriers; verify five floor and four boundary collision traces.

## Publication and presentation

- [x] Publish the playable example without Epic FirstPerson/character-package dependencies or local MCP/token configuration.
- [x] Build the standalone example and pass ten silent PIE checks, including real pawn landing/perimeter collision.
- [x] Expand/correct operator and developer documentation, including channels, coordinates, config files, migration, troubleshooting and validation limits.
- [x] Create an editable 15-minute talk + 10-minute demo + Q&A deck for sound designers, embedded speaker notes, presenter guide, PDF and run sheet; render-check all 16 slides in PowerPoint.
- [x] Configure separate plugin/source-example/presentation release artifacts and commit/push the requested publication to origin/main.

## Explicit validation gates

- [ ] Physical Windows audio-device/rate/format/latency/disconnect testing.
- [ ] Actual cooked Force Inline SoundWave/codec playback verification in a packaged project.
- [ ] Native macOS/Linux and older-engine builds before claiming support for those combinations.
- [ ] Full interactive editor/PIE hand-off, undo/redo, and hardware/receiver capture matrix in `VALIDATION.md`.

Plugin commits must not accidentally include sandbox Content/Saved/Intermediate or local MCP credentials/configuration.
