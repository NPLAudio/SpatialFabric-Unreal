# Example asset provenance and terms

No external sample library, downloaded recording or Epic FirstPerson character/
animation package is bundled in this example.

| Material | Origin / applicable terms |
|---|---|
| C++ walking character/game mode, config and helper code | Original SpatialFabric example source; repository MIT license |
| Map layout, material graphs, labels, collision geometry setup and sequence keys | Generated/original SpatialFabric example content; repository MIT license for original contributions |
| WAV sources and imported SoundWaves | Original deterministic procedural sound designs in Tools/Unreal/demo_audio.py; repository MIT license; not third-party recordings |
| Unified brand texture | Derived from repository SpatialFabric/AudioLoom artwork; retain assets/README.md attribution and original AudioLoom MIT notice |
| `/Engine/BasicShapes` geometry and engine materials/editor-resource references | References to content supplied with Unreal Engine; Epic's applicable Unreal Engine terms, not relicensed as MIT here |
| Unreal engine/runtime dependencies | Require a valid Unreal Engine installation and applicable Epic terms |

Epic's Unreal Engine terms: https://www.unrealengine.com/eula/unreal
Repository license: https://github.com/NPLAudio/SpatialFabric/blob/main/LICENSE
Artwork provenance: https://github.com/NPLAudio/SpatialFabric/blob/main/assets/README.md
AudioLoom notice: https://github.com/NPLAudio/SpatialFabric/blob/main/ThirdParty/AudioLoom/LICENSE

The local development SpatialSandbox uses pre-existing FirstPerson template content;
that content is deliberately **not copied** into this published example. The example
has its own minimal native walking character instead. Engine basic assets are not
vendored as standalone mesh files: the packages reference the installed engine.

`ASSET_MANIFEST.json` lists the distributed Content/SourceAssets files and SHA-256
hashes. A dependency audit found no required `/Game` packages outside
`/Game/SpatialFabricDemo`; `/Engine` and `/Script` references are supplied by Unreal
and enabled modules. Refresh the inventory after intentionally re-exporting assets.
