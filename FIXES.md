# SpatialFabric 2.0 — audit remediation

`AUDIT.md` is the **historical pre-fix report**. This document tracks implementation
changes, not a claim of hardware or packaged-content certification. Changes are
confined to this plugin; AudioLoom's module/class identities remain compatible.

## Findings addressed in code

| Audit IDs | Implementation | Automated evidence / remaining gate |
|---|---|---|
| H01, M21 | Bounded RIFF walking, padding, format/rate/channel/alignment validation, exact sample conversion, finite-sample and allocation limits | Real parser tests: valid padded WAV, truncations, zero bit depth/channels, incomplete float frames, NaN, mutation corpus |
| H02 | Snapshot rows carry source BindingIndex; routing never walks compacted rows in lockstep with bindings | Missing-actor regression now routes only the valid object to its actual target |
| H03, L02 | Windows polling renderer uses RAII COM ownership, checked calls, normal valid-buffer submission, explicit EOF zeros and padding drain; no event HANDLEs | Production cursor EOF/format tests; actual WASAPI drain/stall/device behavior still a hardware gate |
| H04, H05 | Source-rate PCM cursor interpolates at device rate; validated float32/int16/int24/int32 writers include valid-bit packing | Rate/channel/loop/EOF and packed-width tests; actual interfaces/rates still a hardware gate |
| H06 | Decode Unreal inline cooked data through the registered codec factory; explicitly reject streaming and unsupported multichannel Vorbis ordering | Native compilation; actual Force Inline cooking/codec deployment remains a package integration gate. Loose sibling-WAV fallback removed |
| H07 | Listener following changes a virtual basis, never an ancestor transform; outgoing listener pose is identity when objects are already listener-relative | Attached-child drift and production stage rotation tests |
| H08, M08 | Editor snapshots initialize without BeginPlay, independent of networking. Existing managers are ticked, never auto-spawned. Networking permissions guard manager entry points | Editor no-PIE snapshot and disabled-network regression; interactive editor/PIE gate documented |
| M01, M02, M12 | EndPlay/unregister join streams; live loop atomics; natural versus cancelled completion events; cancellable zero/fixed/random replay; actual worker events drive monitoring | Renderer loop-exit, zero/random replay state, explicit Stop and failure cancellation tests; actual device event timing remains a hardware gate |
| M03 | Dedicated ADMName plus dref/dmax flow through the actual encoder | Metadata/name regression inspects encoded packets, not a copied address string |
| M04 | Re-resolve stage assignment/clear, update prerequisites; PIE path fixup; enforce actor world ownership and reject ambiguous fallbacks | Stage rebind/clear, cross-world actor and negative-log regression |
| M05 | ADM XYZ/AED share the same unit-sphere point; corners radially limited consistently | Encoded Cartesian/polar radius equivalence test |
| M06, M16 | Compare complete encoded packet arrays; reject unknown Custom tokens rather than shifting discrete axis/value pairing | Send-flag/custom-field packet changes, discrete templates, and unknown-token tests |
| M07, M17 | Reuse/retarget OSC clients, expose deterministic shutdown through owned subclasses, use actual server activity | Real loopback UDP, repeated-connect object count, socket stop/listening state tests |
| M09, M10 | Runtime/editor tickable OSC registry, immediate register/address invalidation, descendant-owner path segment, warnings from final resolved paths | Descendant address regression; editor/PIE listener hand-off is centralized independently of the panel |
| M11 | Address-only versus typed numeric triggers; integer zero rejected, inclusive 0.5 float threshold | Actual FOSCMessage typed-trigger tests |
| M13, M14 | Central active-playback settings application; undo-safe playback/spatial edit scopes; undo notifications; preserve per-format targets/overrides/CustomFields | Actual CSV import undo plus editor controls compiled; full interactive undo/redo and format UI remains a manual gate |
| M15, L01 | Record-aware quoted/multiline CSV parser, strict bounded numeric fields, malformed quote/short-row rejection, sound-clear applies Stop | Multiline/quoted round-trip, invalid channel/quotes, and actual undo tests |
| M18, M19 | Dropdown uses opened mix layout; channel validation; explicit missing device routes fail closed | Renderer channel isolation tests; actual multichannel/default/disconnected endpoints remain a hardware gate |
| M20 | Clamp log count before pointer/count construction; zero count returns empty | Negative and oversized count tests |
| M22 | Active monitoring target refresh and safe client retargeting; rejected endpoints do not silently retain a previous destination | Code path and client reuse tests; packet destination changes remain in manual OSC matrix |
| M23 | Preview uses the exact router/encoder messages, including enabled routes, IDs, templates, modes, and metadata | Actual encoder regression coverage; no duplicated preview serializer |

## Gaps addressed

- The temporary regressions are now repository tests, alongside parser/render/OSC/CSV
  cases. Legacy formula/string-only tests were replaced with production function calls.
- `Tools/validate_unreal.py` builds Editor/Development/Shipping and runs native tests in
  a disposable project. It checks JSON failures/incomplete results **and required
  regression names**, so exit-code zero or an old green smoke suite cannot hide failures.
- Fast Python tests cover plugin layout and the native-result validator itself.
- `.github/workflows/native-validation.yml` provides an opt-in trusted self-hosted
  Unreal job with evidence artifacts. Source ZIP releases are labeled source-only.
- `PrepareAudio` caches immutable decoded PCM, and workers share that storage without
  copying an entire clip on each cue. Asset edits/reimport invalidate the cache. The
  first decode remains synchronous and should be prewarmed before time-critical cues.
- Receive defaults are loopback, and spatial/playback packaged networking each require
  opt-in. Remote control is an explicit Listen IP/firewall decision; OSC is not authenticated.
- Windows resource/error handling is explicit. macOS callbacks validate float32 layouts,
  pre-zero all output, resample at the stream rate, avoid callback allocation/logging,
  bound buffer capacity, detect stalls, and allow Stop to interrupt the final drain.
- Version is now 2.0.0. `Docs/AudioPlayback.md` documents changed descendant addresses,
  virtual listener following, ADM unit-sphere coordinates, cooked Force Inline policy,
  fail-closed device routes, loopback defaults, and independent network permissions.

## Validation status and limits

See `VALIDATION.md` for exact commands and the required release checklist. Windows
native compilation and headless tests do not certify physical audio, real codec cooking,
macOS/Linux, older Unreal releases, or the full interactive editor workflow. Those gates
are intentionally explicit rather than marked complete based on a successful compile.

Latest native run: **UE 5.8 Win64 Editor, Development, Shipping builds succeeded;
21 native tests passed, zero failed/skipped**. The Python layout/branding/result-runner
suite also passes (12 checks). Native evidence is retained locally in the validation
runner's `SpatialFabricValidation-rgyc3onw` output directory. The production source
matches that native build; subsequent changes are documentation/branding/task tracking.
Hardware and packaged-content gates above remain explicitly unverified.
