# OSC reference and network patch

[Documentation index](README.md) · [Spatial conventions](SpatialRouting.md)

## Standalone controller

The independent [OSC Controller desktop app](https://github.com/NPLAudio/SpatialFabric/tree/main/Apps/OSCController) provides
cue Play/Stop/Loop, worker-state monitoring, direct ADM renderer controls and a typed
message composer, plus **live-input cues** (confirmed Start, Stop, bounded Gain; see the controller README), and a **Debugger / test bed** that checks what a sender app emits against the OSC contract and a running engine's real inventory, with an engine simulator. It runs without Unreal and never auto-sends on startup. Its spatial
pad controls a renderer directly; it does not remotely move Unreal actors.

## Read-only scene discovery

Send `/spatialfabric/scene/query` with OSC `,siii` (request ID, reply port, offset,
limit) to a running spatial input (8100) or Playback input (9000). Replies use
`/spatialfabric/scene/response ,siis`: ID, zero-based chunk index, chunk count,
JSON fragment. Reassemble a full page before parsing, then follow `next_offset`.
Loopback is enabled by default; remote discovery requires explicit opt-in.
See the [complete v1 contract](SceneQuery.md) and
[Python / Node.js / Max / Node-RED examples](../Examples/OSCSceneQuery/README.md).

## Ports: inputs and destinations are different

| Path | Default IP:port | Direction relative to Unreal |
|---|---|---|
| ADM spatial data | `127.0.0.1:50018` | Outgoing destination |
| Spatial numeric feedback | `127.0.0.1:8100` | Incoming listener |
| Playback control | `127.0.0.1:9000` | Incoming, starts on request |
| Playback worker state | `127.0.0.1:9001` | Outgoing destination |
| Example Custom position path | `127.0.0.1:9002` | Outgoing example-only destination |

127.0.0.1 means this computer. For another machine, use its reachable address.
0.0.0.0 is an all-interface **listen binding**, not a receiver destination.
OSC/UDP is unauthenticated; cue acknowledgements provide feedback, not reliable delivery. Do not expose these
ports or an editor MCP endpoint to untrusted networks. Multiple editors can collide
on the input ports; close the other owner rather than hiding the error with reuse.

## Configuration files and permissions

The two settings classes use **different config files**. Prefer the named Project
Settings pages, then inspect their generated configuration.

`Config/DefaultSpatialFabric.ini` — spatial Manager defaults:

```ini
[/Script/SpatialFabric.SpatialFabricSettings]
bEnableInEditor=True
bEnableInPackagedBuilds=False
DefaultOSCListenIP=127.0.0.1
DefaultOSCListenPort=8100
DefaultADMOSTargetIP=127.0.0.1
DefaultCustomTargetIP=127.0.0.1
DefaultADMOSCPort=50018
DefaultSendRateHz=50.0
bEnableDebugMessages=False
```

`Config/DefaultEngine.ini` — retained AudioLoom/playback configuration:

```ini
[/Script/AudioLoom.AudioLoomOscSettings]
ListenIP=127.0.0.1
ListenPort=9000
SendIP=127.0.0.1
SendPort=9001
bEnableInEditor=True
bEnableInPackagedBuilds=False
```

Enable packaged networking **separately** for each path that should operate in a
packaged build. Do not assume enabling spatial output enables playback control.
Placed Managers serialize AdapterConfigs; changing project defaults is not a bulk
rewrite of existing actor endpoints. CustomTargetIP/Port for on-demand custom sends
is also distinct from the continuous Custom adapter's config.

## ADM messages

`{n}` is a positive spatial object ID resolved for that adapter target.

| Address | Arguments | Emission policy |
|---|---|---|
| `/adm/obj/{n}/xyz` | float x, y, z | Cartesian/Both mode |
| `/adm/obj/{n}/aed` | float azimuth°, elevation°, radius | Polar/Both mode |
| `/adm/obj/{n}/w` | float width 0..1 | Included with object state |
| `/adm/obj/{n}/gain` | float nonnegative linear gain | ADM gain flag enabled |
| `/adm/obj/{n}/mute` | int32 0/1 | ADM mute flag enabled |
| `/adm/obj/{n}/name` | string | ADM name flag enabled |
| `/adm/obj/{n}/dref` | float 0..1 | Included with object state |
| `/adm/obj/{n}/dmax` | float | Only when positive |
| `/adm/lis/xyz` | float x, y, z | Listener resolved |
| `/adm/lis/ypr` | float yaw°, pitch°, roll° | Listener resolved |

Actual Cartesian mapping is `(internal right, internal front, internal up)`.
Polar azimuth is positive left; Cartesian/AED share the same radius-limited point.
In relative mode listener pose is identity to avoid transforming relative positions
twice. See [the coordinate examples](SpatialRouting.md#adm-osc-wire-behavior).
No specific processor's renderer/firmware behavior is certified by these messages.

The spatial input delegate forwards only the first float (or first int32 converted
to float) to the router. A numeric `/adm/...` reply can update ADM reply-status
indicators; an open UDP socket or outgoing packet does not prove a renderer reply.

## Custom messages

Custom uses address/arg/range templates. Every arg emitted by the current Custom
adapter is a float, even `id`. The UI's “Bundled” option means one message with
multiple args, **not** an OSC `#bundle` envelope. Discrete means one message per
argument with `{axis}` following that argument's name. Unsupported/unresolved
address or argument tokens fail rather than silently generating a different patch.
See [working recipes](SpatialRouting.md#custom-template-recipes).

## Live input transport

A live-input component answers on its own base (default `/audioloom/<root>/live/<n>`; **Get OSC Address**):

| Suffix | Meaning |
|---|---|
| `/play` | Address-only, float >=0.5 or int32 >=1 submits a **start request** (may prompt for microphone access) |
| `/stop` | Stops the live bridge |
| `/gain` | First float anywhere, else first int32; finite 0..4 only, anything else is rejected |
| `/state` | Outgoing float 1/0: the worker really started / is not live. Not a command acknowledgement |

See [Audio playback](AudioPlayback.md#live-input-microphone--line-in) for feedback and permission notes.

## Playback transport

Resolve the component's effective base with **Get OSC Address**. The example uses:

| Station | Base | Channel |
|---|---|---|
| Warm percussion | `/demo/playback/mono` | 1 |
| Stereo groove | `/demo/playback/stereo` | 0, corresponding channels |
| Glass/mechanical replay cue | `/demo/playback/replay` | 2 |

| Suffix | Meaning |
|---|---|
| `/play` | Address-only, float >=0.5, or int32 >=1 starts/restarts playback |
| `/stop` | Stops and cancels pending replay; arguments are not a confirmation protocol |
| `/loop` | First float anywhere, else first int32; >0.5 enables, address-only disables; nonnumeric payload ignored |
| `/state` | Outgoing float 1/0 from worker lifecycle, not a command acknowledgement |

A string `"1"` is not numeric 1. False/zero play messages do not fire. Supported
wire tags do not imply every tag has a transport interpretation: prefer the numeric
forms above; do not rely on Boolean True as a Play/Loop command. `/state` can show
natural completion or stop after a successful stream start. A request that fails
before starting must not be treated as audible success; watch `/ack` failure feedback and playback error/logs.

### Command acknowledgement and status (both cue kinds)

Every command suffix addressed to a known cue base answers on **SendIP:SendPort**
(default `127.0.0.1:9001`), the same destination as `/state`, not the sender's source port:

`<base>/ack ,ssis command result playing detail`

- `command`: suffix, sanitized to ASCII letters/digits/underscore/hyphen, at most 32 characters.
- `result`: `received` (acted on), `ignored` (payload says no effect or engine-only message),
  `rejected` (invalid arguments/unsupported command), or `failed` (start/worker failure).
- `playing`: int32 0/1, observed worker state, **not** a pending start request.
- `detail`: short UTF-8 human-readable reason (up to 256 characters); may be empty.

`play received` is a start request only. It is followed by observed `/state 1`, or
`ack play failed 0 <reason>` for missing/undecodable sources, unavailable backends,
or asynchronous device/worker refusal. Worker failures after starting also report
`play failed` and `/state 0`; normal stops and natural endings report `/state 0`.
Failures from local/Blueprint starts are also sent to the monitor while it is active.

Address-only `<base>/status` answers `ack status received <playing> ""` then
`<base>/state <playing>` (float 0/1), so late controllers can synchronize without
starting audio. Status with arguments is rejected. `/ack` and `/state` sent **to**
the engine are ignored with acknowledgement; unknown suffixes are rejected.
`/loop` on live cues and `/gain` on playback cues are rejected. Invalid live gain
never changes the previous value. A nonnumeric `/loop` never disables an existing loop.
Float arguments take precedence over ints anywhere in the list. Stop ignores payload.
Unknown bases are silent; duplicate bases retain fan-out (one ack per component).
Do not connect monitor output back to the command input: engine-only feedback is
ignored but still acknowledged and a loopback patch can create a feedback loop.

Lifecycle polling uses the core ticker while listening, including paused worlds
and non-realtime editor viewports. UDP can still lose/reorder replies: use `/status`
to reconcile observed state; there are no request IDs, retries or exactly-once guarantees.

Silent conformance tests: `SpatialFabric.OSC.AckFixtures` checks all 34 shared fixture
decisions and seven golden wire packets (including UTF-8 failure text);
`SpatialFabric.OSC.AckLifecycle` injects commands/worker events without opening devices
or a listener. Refresh/check the generated native cases with
`python -B Tools/generate_osc_ack_tests.py [--check]`; use `--fixture` for a non-sibling
hub checkout. The shared [contract](https://github.com/NPLAudio/SpatialFabric/blob/main/Specs/OSC-v1.md)
and [fixtures](https://github.com/NPLAudio/SpatialFabric/blob/main/Fixtures/ack-v1.json)
now live in the `SpatialFabric` shared hub (formerly `SpatialFabric-Interop`).
The default local sibling paths remain `SpatialFabric-Interop/Specs/OSC-v1.md`
and `SpatialFabric-Interop/Fixtures/ack-v1.json` for existing script checkouts.

### Default versus explicit paths

Root-owned components retain `/audioloom/<sanitized-root-label>/<component-index>`.
Descendants add their owner-name segment to avoid hierarchy collisions. Attachment,
label or owner changes can change defaults. For persistent show control, set a unique
explicit base and export it with routing. Duplicate explicit bases can deliberately
fan out to multiple components, but accidental duplicates can make several cues fire.

### A minimal standard-library sender

**This example intentionally plays the mono station if its listener is running.**
Choose a safe device/level first. Use a real OSC library for a larger controller.

```python
import socket
import struct

def osc_string(value):
    data = value.encode('utf-8') + b'\0'
    return data + b'\0' * (-len(data) % 4)

packet = (osc_string('/demo/playback/mono/play')
          + osc_string(',f') + struct.pack('>f', 1.0))
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
    sender.sendto(packet, ('127.0.0.1', 9000))
```

An address-only Stop packet is `osc_string('/demo/playback/mono/stop') +
osc_string(',')`. Watch monitoring separately on port 9001; it is not a reply to
the sender's ephemeral source port.

## Receiver contract

The plugin uses its own exact-bind socket rather than UE 5.8's stock receiver,
which can rewrite loopback to a LAN address and report proxy existence as active.
The bound address/socket result is checked. Unregister/teardown releases it promptly.
Core-ticker dispatch stays independent of paused-world/component ticking and handles
safe stop/release during a callback.

Supported input: IPv4 unicast, OSC `i/f/s/b/h/T/F`, zero-padded strings/blobs,
finite floats, bounded immediate bundles. Unsupported tags, malformed lengths,
truncations, bad padding and non-finite floats reject the datagram atomically.
Bundle timetags are not scheduled; immediate dispatch matches this control use case.
Limits: UDP payload <=65,507 bytes; nesting depth <=8; <=128 decoded messages and
128 args/message; bounded per-tick packet/message processing. These are bounded
controls, not a high-bandwidth audio transport or a security/authentication service.

## Capture and handoff

The provided collector listens to outgoing ADM/Custom/state ports and records JSONL,
IDs, distinct XYZs, state edges and XYZ/AED radius consistency. It refuses occupied
ports. Close another receiver first or capture in that receiver instead.

On PIE entry the editor releases input sockets; an active playback listener is
transferred to PIE. On exit, the editor restores its listening state. Stop the
listener explicitly when the demonstration is over. See [live-check procedure](SpatialSandboxDemo.md#repeat-the-live-checks).
