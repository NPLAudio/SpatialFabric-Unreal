# Scene query / configuration discovery — protocol v1

[OSC reference](OSCReference.md) · [Runnable examples](../Examples/OSCSceneQuery/README.md)

Scene discovery is **read-only**: inspect every supported SpatialFabric/AudioLoom
record in one loaded world, including disabled/unresolved bindings and stopped
playback components. It never starts audio, prepares/decodes media, loads assets,
changes routing, starts a receiver, enumerates hardware, or saves the scene.

## One command, existing OSC inputs

Send to a **running input receiver**:

- SpatialFabric Manager spatial input: default `127.0.0.1:8100`.
- AudioLoom Playback input after **Start OSC**: default `127.0.0.1:9000`.

Do not send queries to outgoing ADM 50018 or monitoring 9001. Both input handlers
use one discovery service. No extra Unreal socket/server/HTTP/MCP service is needed.
A project containing only playback components can query through its Playback input;
a placed Manager is not required for that path. The combined plugin must be loaded.

```text
Address: /spatialfabric/scene/query
Types:   ,siii
Args:    request_id, reply_port, offset, limit
Example: "max_001", 9011, 0, 16
```

The short form `,si request_id reply_port` defaults to offset 0, limit 64.

| Field | Contract |
|---|---|
| request_id | 1–64 ASCII letters/digits/underscore/hyphen; unique for each page/retry |
| reply_port | int32, 1024–65535; your application's **listening** port |
| offset | int32, 0–16384; start at 0 |
| limit | int32, 1–64 records per page; 16 is convenient for simple UDP hosts |

Types matter: numeric strings and float-valued ports are not int32. Invalid
requests are silently rejected. Reply IP is always the UDP sender's IPv4 address,
never a caller-selected third-party IP. The reply socket's **source port is not
fixed**: route/correlate by sender IP and request ID, not by source port 8100/9000.
Bind the reply socket before sending. The examples use an OS-assigned ephemeral
reply port to avoid colliding with other apps.

## OSC response and JSON reconstruction

```text
Address: /spatialfabric/scene/response
Types:   ,siis
Args:    request_id, chunk_index, chunk_count, json_chunk
```

`chunk_index` is **zero-based**. `chunk_count` is 1–128. Each string contains at most
900 UTF-8 bytes and ends on a UTF-8 boundary. Datagrams are roughly <=1.1 KiB to
avoid ordinary LAN MTU fragmentation; the JSON page is at most 96 KiB.

1. Ignore unexpected sender IPs and request IDs.
2. Validate argument types, counts and size bounds.
3. Store chunks by index, not arrival order. Ignore identical duplicates; reject
   conflicting duplicates or a changed count within the same request.
4. Only after all chunks `0 .. count-1` arrive, concatenate **the strings without
   adding spaces/newlines**, then parse the resulting JSON object.
5. Check `schema`, `schema_version` and `error` before using it.
6. If `next_offset >= 0`, request the next page with a **new request ID** after at
   least 300 ms. Stop at `next_offset == -1`. Never treat a partial page as a scene.

A single-chunk response is already a complete JSON string. Native JSON responses
have schema `org.nplaudio.spatialfabric.scene`, version `1`. If a page is too large,
its JSON contains `error: "page_too_large_reduce_limit"`: retry that offset with a
smaller limit. If one record itself exceeds the cap, reduce its oversized
Custom fields/templates; the service does not silently omit that record.
Other bounded errors include `invalid_page`, `scene_record_limit`
and `invalid_world_or_thread` for the local API. No object inventory is silently
truncated: a scene above the 16,384-record scan cap returns an explicit error.

OSC/UDP does **not** guarantee delivery/order. On timeout discard the partial page
and retry with a fresh ID. Supplied clients retry at most three times/page, wait
1.5 seconds/attempt, bound the complete inventory to 8 MiB, and fail visibly if
world/count/record identity changes during pagination. There is no unsolicited
scene broadcast or continuous polling. Server limits are one query per sender IP
per 250 ms, plus a global 100 ms floor. Rate-limited requests are dropped, not queued.

## JSON inventory

See [sample JSON](../Examples/OSCSceneQuery/sample_scene.json) and
[JSON Schema](../Examples/OSCSceneQuery/scene.schema.json).

Header fields:

- `schema`, `schema_version`: contract identity/version.
- `world`, `world_type`: receiver owner's world (`Editor`, `PIE`, or `Game`).
- `snapshot_utc`, `world_time_seconds`: capture time for **this page**.
- `offset`, `limit`, `returned`, `total`, `next_offset`: pagination.
- `coordinates`, `state_contract`: human-readable interpretation boundaries.
- `playback_network`: configured listen IP/port, actual `bound_ip`/`bound_port`
  and `listening` when its subsystem exists, plus monitoring destination IP/port.
- `objects`: records below. Ignore additional fields for forward compatibility;
  reject unsupported schema versions rather than guessing.

| `kind` | Useful fields |
|---|---|
| `manager` | path/key, actor identity/transform, stage path, networking permission, hide-in-PIE, on-demand Custom destination, complete configured adapter list |
| `stage` | actor transform, physical dimensions in meters, unscaled box extent in cm, axis flips, listener mode/player index/actor path and last resolved listener pose |
| `listener` | explicitly assigned listener actor/transform and owning stage path; automatic player/view-target is described by the stage's resolved pose, not a fabricated actor |
| `spatial_binding` | manager path/index, label, authored and resolved actor paths/transform, default ID, enabled/mute/gain/width, ADM flags/name/dref/dmax, resolved stage-normalized/meters position when available, per-target ID overrides, adapter configuration and Custom fields |
| `playback` | component/class/actor identity, registered flag, exact `osc_base` and `controls`, state address, SoundWave path/duration/channel metadata when present, configured device ID/output channel/policy, exclusive/buffer requests, playing/error, loop/BeginPlay/replay settings |
| `spatial_osc_receiver` | component identity and actual bound IP/port/listening |
| `spatial_osc_client`, `spatial_registry` | component/class/actor identity; effective adapter destinations are on managers/targets |

Ordinary unrelated actors, meshes, materials, PCM bytes, arbitrary UObject internals,
project credentials and MCP settings are not dumped. Bound actors and listener
information are represented through their bindings/stages; a mesh with two bindings
has two configuration records. Subclasses are included through their native type.

`key` is unique inside a snapshot and useful for merging records/pages. Component
paths can change with renames and PIE. Binding keys include manager path/index and
can change when bindings are reordered. **These are not permanent cross-project
UUIDs.** Use explicit OSC bases and per-target object IDs for an authored show patch.
A `spatial_guid`, when present, is runtime registry metadata and may be the zero GUID;
it is not the discovery key or a promised persistent identifier.

`output_channel == 0` means corresponding source/device channels. `1..N` routes the
**first** source channel to that selected output. A device ID is configured state,
not a fresh hardware/device-presence check. Exclusive and buffer values are requests,
not certified actual stream mode/latency. `playing` is software runtime state,
not proof of audible sound, correct physical channels, or command acknowledgement.

The stage vectors are internal **front/right/up**. ADM wire XYZ swaps front/right
and sphere-limits; Custom output depends on templates/ranges. Snapshot positions
must not be treated as already-transformed renderer coordinates.

## World scope, timing and safety

Only the world owning the queried listener is scanned, not other loaded maps,
unloaded World Partition cells, Content Browser assets, or every editor/PIE world.
The direct API uses the supplied World Context. No asset is loaded to answer a query.

Each page is captured on the game thread. Pages are **not one atomic frozen snapshot**;
objects/configuration can change between them. Supplied clients detect world/count
changes and duplicate keys, but same-count edits can still occur between pages.
Query again after structural scene edits; use continuous OSC positions for motion.
The core-ticker receiver can answer while PIE is paused or Unreal is unfocused;
background throttling still affects how recently the world itself advanced.

Networking follows each input's editor/packaged permissions. Additionally, Project
Settings → Spatial Fabric contains:

```ini
; Config/DefaultSpatialFabric.ini
[/Script/SpatialFabric.SpatialFabricSettings]
bEnableSceneQueries=True
bAllowRemoteSceneQueries=False
```

Loopback queries work by default on an already-running receiver. Remote hosts are
**disabled by default**; explicitly allow only on trusted networks and configure
firewall restrictions. OSC has no authentication, encryption or sender verification.
The response exposes scene names, paths and device-routing configuration.
Custom routing fields are returned verbatim; do not put secrets in those fields. Queries
are bounded/rate-limited and replies target the sender, but this is not an
Internet-safe public discovery endpoint. Disable scene queries if not needed.

## Blueprint, Python and C++ local JSON function

**Blueprint:** `Get Scene Json` on `Spatial Scene Query Library`; pass World Context,
offset and limit. This requires no UDP listener and does not start one.

**Unreal Python:**

```python
import json, unreal
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
page = json.loads(unreal.SpatialSceneQueryLibrary.get_scene_json(world, 0, 64))
for record in page.get('objects', []):
    if record['kind'] == 'playback':
        print(record['actor_label'], record['osc_base'], record['output_channel'])
# Follow next_offset until -1 for scenes larger than one page.
```

**C++:** include `SpatialSceneQueryLibrary.h` and call
`USpatialSceneQueryLibrary::GetSceneJson(WorldContextObject, Offset, Limit)` on the
game thread. Off-thread/invalid-world calls return bounded JSON errors.

## In the standalone controller

Object tracker → **Query scene** defaults to input 8100, reply port 9011; use 9000
if querying through Playback. Reply binding uses the tracker's Listen IP. Discovery
uses a temporary shared receiver without disturbing live 9001/50018/9002 listeners.
Complete results appear under **Scene inventory**, can be exported as JSON, and add
clearly labelled **Scene** snapshot dots to the visualizations. These are not faked
received ADM/Custom motion packets. Filter by Scene to inspect only query positions.

**Import discovered cues…** requires confirmation before replacing the app cue list
and playback destination. It deduplicates shared bases and rejects >64 distinct
bases rather than silently truncating. It does not alter Unreal/device settings,
start audio, save a profile, or retarget Unreal monitoring. Verify the reported
monitor destination separately. Failed/incomplete queries leave the previous
complete inventory intact. Old UDP snapshots are never mixed into a new request.
