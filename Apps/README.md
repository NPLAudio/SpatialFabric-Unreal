# Standalone applications

The shared [OSC Controller source](https://github.com/NPLAudio/SpatialFabric/tree/main/Apps/OSCController)
now lives in `Apps/OSCController` in the **SpatialFabric shared hub** (formerly
`SpatialFabric-Interop`), not in this Unreal repository. It provides independent
desktop cue transport, direct ADM renderer controls, a message composer, bounded
monitor and JSON profiles, and runs without Unreal.

Download **OSC Controller 1.4.0** from the
[hub 1.0.0 release](https://github.com/NPLAudio/SpatialFabric/releases/tag/v1.0.0).
The local [controller README](OSCController/README.md) is a redirect only.
Controller source and executables are not bundled in Unreal's default release
archives, including CompleteSource, and are not required to build or run the plugin.
