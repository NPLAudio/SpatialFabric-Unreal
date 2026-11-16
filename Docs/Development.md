# Development, building and distribution

[Documentation index](README.md) · [Codebase guide](CODEBASE_GUIDE.md) · [Release gates](../VALIDATION.md)

## Supported evidence, not inferred support

The automated native evidence targets **UE 5.8 Win64** with Visual Studio 2022,
Windows SDK and .NET Framework 4.8 SDK. **macOS Apple Silicon is user-tested and
reported working**; exact engine/OS versions and detailed test logs were not supplied.
See [the user-reported validation record](../VALIDATION.md#macos-apple-silicon-user-reported-validation).
Intel Macs and other engine/OS/interface combinations need their own builds and integration tests. A successful plugin build is separate from
cooking actual show media and separate again from physical audio verification.

## Build in a host project

Install the repository as `Project/Plugins/SpatialFabric`, enable SpatialFabric
and OSC, regenerate project files and build the host's Editor target:

```powershell
& "C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" `
  YourProjectEditor Win64 Development `
  -Project="C:/dev/YourProject/YourProject.uproject" -WaitMutex -NoLiveCoding
```

Close an editor using the DLLs before replacing them. Live Coding is not a general
substitute for a clean rebuild/restart after reflected class/module/layout changes.
Do not delete authored Content/SourceAssets while clearing generated intermediates.
The repository itself is a plugin, not an executable game target.

## Prepare the example without recursive plugin copies

From the repository root:

```powershell
python Tools/prepare_example.py --destination C:/dev/SpatialFabricShowcase
```

The helper copies the checked-in example plus a bounded set of plugin folders into
`Plugins/SpatialFabric`. It omits Examples/Presentations from the nested plugin,
removes the repository-relative AdditionalPluginDirectories entry, and excludes
binaries/caches/MCP/user configuration. Destinations must be new/empty and outside
this repository. This avoids self-referential symlinks and recursive Git/source copies.

Build `SpatialFabricShowcaseEditor` against the prepared `.uproject`. The source
project's walking-character module is original and does not require the local
SpatialSandbox FirstPerson template content. Direct opening from the repository
uses its relative plugin-search entry; a prepared copy is the recommended portable
setup. Moving the example alone without the plugin is not sufficient.

## Fast, engine-free tests

```sh
python -m unittest discover -s Tools -p "test_*.py"
```

These check integration/layout, branding, authoritative result handling, demo
source syntax, procedural audio bounds/stereo, example inventory/preparation,
document links and presentation structure as applicable. They cannot validate
Unreal APIs, shader rendering, cooked codecs or hardware audio.

## Disposable native plugin validation

```powershell
python Tools/validate_unreal.py --engine "C:/Program Files/Epic Games/UE_5.8"
```

Builds Editor/Development/Shipping outside the source plugin, creates a disposable
project, runs `SpatialFabric*` native regressions with NullRHI/NoSound and validates
Unreal's JSON report, including required test names. An exit code of zero alone
is insufficient. `--editor-only` is an iteration option; `--output` must be a new
empty external evidence directory. Do not accidentally cite an old success report
from another source revision as proof of new code.

## Disposable example validation

```powershell
python Tools/validate_example.py --engine "C:/Program Files/Epic Games/UE_5.8"
```

Prepares/builds a new project-local plugin copy, disables spatial networking **only
in that disposable copy** to avoid another editor's ports, and silently starts PIE.
The smoke test checks the original walking pawn, map/manager/stage, four bindings,
three prepared Force Inline cues, no autoplay, animation, landing on the outer floor
and a capsule sweep blocked by the perimeter. It judges `example_smoke.json`, not
only process exit. No cue is played. This is not a packaged/cooked or hardware test.

`Tools/Unreal/smoke_example.py` deliberately starts/stops PIE and quits its editor.
Run it only through this disposable workflow, never in your working editor with
unsaved content. The normal live validator is a different tool and intentionally
plays quiet cues; see the [tour](SpatialSandboxDemo.md).

## Updating example assets

The checked-in packages retain `/Game/SpatialFabricDemo/...` paths. Do not move them
through the filesystem and expect Unreal references to repair themselves. Use
migration/rename tools and save redirectors deliberately when changing paths.

- `build_spatialfabric_demo.py`: regenerate the owned namespace/tagged actors.
- `refresh_spatialfabric_demo_audio.py`: refresh existing cues/signage only;
  preserves routing/geometry and invalidates prepared PCM.
- `extend_spatialfabric_demo_floor.py`: update safety geometry and test traces.
- `demo_audio.py`: standard-library, deterministic 48 kHz PCM16 source generation.

Save before export. Publish only the dedicated Content/SpatialFabricDemo packages,
matching SourceAssets WAVs, minimal configuration, original player source and inventory.
Do not copy SpatialSandbox's entire Config directory: it can contain machine tokens,
MCP configuration and unrelated template/project settings. Review hard/soft package
dependencies to ensure no `/Game/FirstPerson`, character or private show-media assets
are required. The checked-in manifest supplies file hashes/provenance for review.

## Distribution boundaries

GitHub contains the source plugin, published example, controller and query clients.
The plugin-only release ZIP excludes Examples/Apps so installation does not import
sample projects or unrelated desktop tools. The example release ZIP contains its
project-local source plugin; it still requires a matching engine/toolchain. The
Windows controller ZIP bundles Python/Tk and needs no Unreal installation.

After tests and committing a clean worktree, build release archives locally:

```sh
python Tools/package_release.py --version 2.1.0 --output dist/releases/2.1.0 --controller-zip Apps/OSCController/dist/1.2.0/SpatialFabricOSCController-Windows-x64.zip
```

The packager validates descriptor/version, excludes generated/private directories,
prepares the self-contained example and emits a commit manifest/checksums. Build
and smoke-test the controller first; do not package an arbitrary old executable.
Tagging `v2.1.0` runs source/Node/controller checks and release builds in CI. If a
release already exists, CI preserves its validated assets rather than overwriting
them with independently rebuilt binaries/checksums.

Do not version Binaries, Intermediate, Saved, DerivedDataCache, IDE state, device
credentials, local `.pi`/MCP data or raw validation logs with local identifiers.
Native artifacts stay in printed external evidence directories. The concise current
validation summary can be published without bundling generated caches/logs.

## Documentation maintenance

Current operator guides live in Docs; AUDIT.md remains historical. Keep wire types,
config file names, channel semantics, enable defaults and certification claims tied
to production code. Particularly avoid obsolete “negate Y only”, “channel 0 duplicates
mono”, “all adapters disabled”, and “one socket reconnect allocated each frame” claims.

The removed presentation is not required by current release tests or packaging.
Historical deck files are retained in Git tag/release `v2.0.3`. Legacy
`Tools/build_presentation.py` and `Tools/export_presentation.ps1` require those
archived inputs if deliberately restoring that presentation separately; they are
not part of the current release build. Do not restore deleted user files merely
to satisfy an obsolete presentation check.

## Extending the runtime

Maintain four existing module identities. Put shared transport/decoder utilities
in the shared private include area without creating a circular AudioLoom/SpatialFabric
module dependency. Separate pure message encoding from socket effects so preview
and output stay equivalent. Test actor identity after missing bindings/PIE-world
resolution, then test real packets as well as pure encoders. Keep control callbacks
bounded/game-thread-safe and device rendering off the game thread. See the detailed
[reading map](CODEBASE_GUIDE.md) and explicit [manual gates](../VALIDATION.md).
