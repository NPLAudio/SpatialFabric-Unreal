# Examples

## [SpatialFabricShowcase](SpatialFabricShowcase/README.md)

A UE 5.8 source project with the actual showcase map, 15 Unreal asset packages,
three original WAV sources, a minimal original C++ walking character, safe perimeter
geometry and explicit local OSC routes. No Epic FirstPerson character pack, external
sample download, local sandbox path or editor MCP service is needed.

Prepare a standalone copy from the repository root:

```sh
python Tools/prepare_example.py --destination C:/dev/SpatialFabricShowcase
```

Build/open its `.uproject` with the matching engine/toolchain. Read the example
README before playback. Source packages are engine-versioned assets, not a promise
of backward compatibility with older Unreal installations.
