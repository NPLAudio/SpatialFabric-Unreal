# OSC reference and network patch

[Documentation index](README.md) · [Spatial conventions](SpatialRouting.md)

## Standalone controller

The independent [OSC Controller desktop app](../Apps/OSCController/README.md) provides
cue Play/Stop/Loop, worker-state monitoring, direct ADM renderer controls and a typed
message composer. It runs without Unreal and never auto-sends on startup. Its spatial
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
OSC/UDP is unauthenticated and delivery is not acknowledged. Do not expose these
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
| `/gain` | First float anywhere, else first int32; finite 0..4 only, anything else is ignored |
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
| `/loop` | Use numeric 0/1 to change live loop state |
| `/state` | Outgoing float 1/0 from worker lifecycle, not a command acknowledgement |

A string `"1"` is not numeric 1. False/zero play messages do not fire. Supported
wire tags do not imply every tag has a transport interpretation: prefer the numeric
forms above; do not rely on Boolean True as a Play/Loop command. `/state` can show
natural completion or stop after a successful stream start. A request that fails
before starting must not be treated as audible success; inspect playback error/logs.

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
