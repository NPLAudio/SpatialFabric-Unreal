# Spatial routing and coordinate reference

[Documentation index](README.md) · [OSC messages](OSCReference.md)

## Mental model: a moving control surface

An actor supplies a position. A **binding** says which source it represents. A
**Stage Volume** supplies the measurement frame. A **protocol adapter** formats
that result for a destination. The Manager assembles a stable-identity snapshot
and sends each adapter its relevant objects/IDs. A missing actor must not shift
all later IDs onto different rows.

This path does not contain audio samples. A spatial ID belongs to the renderer's
object patch, not automatically to an OS sound-device channel.

## Stage sizing

The box uses half-extents internally: with a full 20 × 15 × 8 m box, faces are
±10 m, ±7.5 m and ±4 m from center. At center the normalized coordinate is zero;
at a positive face its corresponding axis is +1. World points are unrotated into
the box frame and divided by the scaled half-extents; each axis is clamped to
[-1, 1]. `WorldToMeters` multiplies the normalized values by physical half-size.
It therefore also inherits clamping; it is not an unbounded world-distance query.

Match **both** the geometric box and physical-dimension fields. Rotating the box
rotates its fixed measurement axes. Axis flips are explicit output remaps; do not
use an arbitrary flip to conceal a mismatch you have not identified.

### Internal axes

- **+X:** front/forward; **-X:** back.
- **+Y:** right; **-Y:** left.
- **+Z:** up; **-Z:** down.

Treat theatre labels such as upstage/downstage as your own set orientation;
place/rotate the stage to match the real room rather than assuming all venues
use the same world direction.

### Visibility is not movement

The box remains visible in the editor, including Game View and when unselected.
**Hide stage volume when game is running** hides it only in PIE/game. Uncheck to
show it there. The control updates actor/component display, not authored transforms.
If still invisible, check Outliner eye/temporary hiding and viewport show flags.

## Bindings, IDs and metadata

A binding has a target actor, stable row identity, display label, enabled flag,
default positive object ID, gain/width/mute and ADM fields. Each target entry can
override the ID for one protocol and provide CustomFields. In the example the
same four actors use ADM IDs 1–4 and Custom IDs 101–104.

- **Enabled off:** omit this binding from snapshots/output.
- **Muted:** retain its state; send ADM mute metadata only if that send flag is on.
  It does not stop direct PCM playback or inherently suppress all position packets.
- **Gain:** linear renderer metadata; ADM gain is not dB and is clamped nonnegative.
- **Width:** emitted as a clamped 0..1 ADM value.
- **ADM name override:** separate from UI/Custom label so names do not overwrite
  unrelated routing fields.
- **Dref:** clamped 0..1; **Dmax:** emitted only when positive. Confirm their meaning
  against the renderer; a number on the wire is not proof of a physical calibration.

Enable the ADM gain/mute/name send flags when those fields should be transmitted.
Assign stable explicit IDs and unique target overrides. Avoid relying on actor
iteration order or labels alone as a durable show-file patch.

## ADM-OSC wire behavior

Only ADM-OSC and Custom adapters are implemented here. Other manufacturers may
support ADM-OSC, but that does not establish tested compatibility with a particular
processor, firmware or object model. No dedicated DS100/other vendor adapter or
manufacturer certification is implied.

The **actual implementation** first limits the normalized point to radius 1, then
uses the same point for Cartesian and polar messages:

| Internal point | ADM `xyz` payload |
|---|---|
| `(front, right, up)` | `(right, front, up)` |
| Internal +X/front | `(0, +1, 0)` |
| Internal +Y/right | `(+1, 0, 0)` |
| Internal +Z/up | `(0, 0, +1)` |

Polar azimuth is `atan2(-right, front)` in degrees: front 0°, left +90°, right
-90°. Elevation is degrees above the horizontal; distance is unit-sphere radius.
A box corner outside radius 1 is **radially limited**, not merely divided by
sqrt(3) in one format. Do not use an older description that simply says “negate Y”
for Cartesian output: the implemented mapping swaps front/right components.

Cartesian, Polar and Both modes choose `/xyz`, `/aed` or both. Test cardinal points
with the receiver before adopting its channel/axis labels.

## Listener-relative mode

Off uses the authored box center and orientation. Follow Position & Orientation
uses a **virtual** origin/orientation; it never translates or rotates the authored
stage, including when the listener is attached beneath it.

Resolution priority is local player view-target (pawn fallback), then the explicit
ListenerActor in the same world, then an attached child. The player **actor origin
and actor rotation** are used, not necessarily camera-eye offset/HMD pose. Explicit
listener assignment does not override an available higher-priority local player.
This distinction matters for VR, leaning cameras and off-axis views: do not claim
validated head tracking from ordinary actor-following behavior.

Because object coordinates are already listener-relative, emitted listener XYZ/YPR
is identity in that mode; the receiver must not transform them a second time.

## Custom template recipes

**One message, three float arguments** (“Bundled” in the UI):

```text
Address: /source/{id}/position
Args:    x y z
Mode:    XYZ, Bundled
Range:   -1 .. 1
```

This is one OSC message with multiple arguments, not an OSC `#bundle` packet.

**One message per value:**

```text
Address: /source/{id}/{axis}
Args:    z gain x
Mode:    XYZ, Discrete
```

Produces `/source/1/z`, `/source/1/gain`, `/source/1/x` in that argument order.
The axis token follows the actual argument, not a hardcoded X/Y/Z position.

**Per-target grouping:** `/demo/{zone}/{id}/xyz` with CustomFields `zone=main-stage`.
Custom fields substitute address tokens; they do not add arbitrary argument types.
Supported numeric args: `x y z gain width id azimuth a elevation e distance d`.
All Custom args, including `id`, are floats. An empty arg list sends address-only.

Built-in address tokens: `{id}`, `{x}`, `{y}`, `{z}`, `{gain}`, `{width}`, `{label}`;
`{axis}` applies in Discrete mode. Unknown/unresolved tokens or invalid addresses
are rejected rather than sent. Avoid spaces/pattern metacharacters in label/field
values inserted into paths. Do not expect `{a}` as an address token merely because
`a` is an argument alias.

XYZ ranges map each -1..1 component. In AED mode azimuth -180..180°, elevation
-90..90°, and clamped distance 0..1 each map to CustomPositionRange. Gain is mapped
from clamped 0..1 and width from 0..1 to their respective ranges. Custom AED is not
an unscaled degrees/meters preset; choose receiver ranges deliberately.

## Multi-protocol and update rate

The router can send enabled ADM and Custom targets simultaneously using reusable
per-endpoint clients. You do not need duplicate Managers merely to send to two IPs.
The panel's format selector is a convenience that enables its chosen adapter and
disables the others; it preserves saved target entries/custom fields. For concurrent
output, configure enabled adapter entries/targets explicitly on the Manager.

SendRateHz (1–120, default 50) is a **maximum update-batch rate**, not a guarantee
of packets per second or sample-accurate scheduling. One batch contains many
messages. Slow editor/game frames reduce achievable updates. Transmit-on-change
compares the complete encoded message list including metadata/templates; a changed
object can cause a full adapter batch to be resent. Static send-on-change output
is not a heartbeat. UDP delivery is not acknowledged or retried by this path.
