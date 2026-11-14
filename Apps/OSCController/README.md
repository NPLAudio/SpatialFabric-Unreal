# SpatialFabric OSC Controller

A **separate desktop application**, not an Unreal plugin or editor panel. It sends
OSC/UDP to SpatialFabric or another OSC device, and monitors incoming messages.
It never plays audio itself. No Unreal installation, MCP server, web server,
account, or third-party OSC library is required.

## Run the Windows app

After building, double-click **`dist/SpatialFabricOSCController.exe`**. The companion
`dist/SpatialFabricOSCController-Windows-x64.zip` is the portable distribution:
extract it and run the executable inside. Python and Tcl/Tk are bundled; recipients
do not need to install Python. Keep the included license notices when sharing.

This is an unsigned local executable: Windows may show an unsigned-app warning.
There is no installer, elevation requirement, telemetry, or automatic network
connection. On startup **live sending is off, no packets are sent, and no listener
is bound**. Loading a profile does not send or start listening either.

## Run from source

Use Python 3.10+ with Tk (validated here on Windows/Python 3.13). From this folder:

```sh
python launch.py
# or
python -m osc_controller
python launch.py --profile profiles/spatialfabric-showcase.json
```

On Windows with Python 3.13 installed, `run.cmd` is a double-clickable source
launcher. On Linux, install the distribution's Tk package if necessary. Source
uses only the Python standard library; Mac/Linux GUI/device behavior has not been
validated. There is no audio driver/backend in the controller.

## Connect to the showcase

1. Open the SpatialFabric showcase in Unreal and select a safe output device/channel.
   Begin with a low hardware level. The app cannot inspect your physical patch.
2. In **Spatial Fabric → Playback**, start the OSC receiver. Its default input is
   **127.0.0.1:9000**. This app's playback destination must match.
3. In the app, select **Monitor / log → Start monitor**. The default listen endpoint
   is **127.0.0.1:9001**. Unreal's playback Send IP/Port must point here.
4. Return to **Cue transport**, select a cue and press **Play**. Play sends numeric
   float `1`; Stop is address-only; Loop ON/OFF sends float `1`/`0`.
5. Press **Stop All Configured Cues** before leaving a show. It sends one Stop per
   configured base and disables local spatial live changes. **Closing the app does
   not stop remote audio**, and UDP delivery is not guaranteed. Keep an independent
   system mute; this is not a safety-rated emergency stop.

| Cue | OSC base | Example audio patch |
|---|---|---|
| Warm percussion | `/demo/playback/mono` | First source channel → output 1 |
| Stereo groove | `/demo/playback/stereo` | Channel 0: corresponding source/device channels |
| Glass/mechanical | `/demo/playback/replay` | First source channel → output 2; example replay behavior |

Only configured cue bases receive Stop All. Add/Edit a cue to match the actual
component's **Get OSC Address**. Bases must be unique; fan-out belongs on the
receiver side. Removing an entry does not stop that remote cue.

### What the state column means

`/base/state` float/int `1` or `0` updates the last reported worker state and age.
It is **not a command acknowledgement, heartbeat, audio meter, physical listening
test, or proof of correct channels**. Startup is Unknown. No packets after a send
may mean the stream never started, monitoring is mispatched, or UDP was lost.
Historic state remains labelled with its age even after monitoring stops.

## Spatial / renderer

The pad is **direct ADM renderer control**. It does **not** move Unreal actors,
change stage transforms, or spatialize AudioLoom PCM. Unreal's spatial input on
8100 handles numeric feedback, not remote XYZ actor movement.

- Patch the spatial destination to the actual renderer. Default `127.0.0.1:50018`
  is only a local test destination, not a guaranteed listening device.
- Enter object ID, Front, Right, Up, linear Gain, Width and optional mute metadata.
  Click **Send once**, or deliberately enable **live changes** before dragging.
- Live mode sends only changed values, limited by Maximum Hz (1–60, default 20).
  GUI timers are best-effort, not sample-accurate scheduling. No stationary refresh
  or timetag scheduling is promised.
- Each update sends five independent datagrams: `/adm/obj/{id}/xyz`, `/aed`,
  `/gain`, `/w`, `/mute`. Device support must be checked; XYZ/AED are equivalent
  descriptions, not separate positions. These datagrams are not an atomic bundle.
- Pad axes are internal Front/+X and Right/+Y. Wire XYZ is **right, front, up**;
  AED azimuth is positive left, in degrees. Values are limited coherently to a
  unit sphere before transmission; the cyan point shows the limited XY preview.
- **Center pad** disables live and centers locally. It deliberately sends nothing
  until Send once or live is re-enabled. Live is never persisted in profiles.
- Monitor port 50018 to see received ADM objects as purple outlines (expire after
  10 seconds). Monitoring 9001 instead receives playback state. One receiver port
  is active at a time. Receiving positions does not automatically retransmit them.

**Avoid competing writers:** Unreal and this app can both send to a renderer.
Disable the unwanted sender or give each a separate object-ID range. If you listen
on the same endpoint you send to, the monitor may display your own datagrams;
that is not a renderer reply.

## Message composer

Choose an independent destination and absolute OSC path. Arguments are **one per
line**, with literal typed values:

```text
f 0.5
i 1
s Main stage
```

Standalone `T`, `F`, `N` are supported for generic OSC equipment. Prefer numeric
forms for AudioLoom Play/Loop; supported OSC tags do not imply transport semantics.
Leave the argument box empty for an address-only message. Nothing is evaluated
as Python or shell code. There is no automatic repeat.

## Monitor and network safety

Default listen address is literal loopback. Binding verifies the real socket and
fails if another receiver owns the endpoint; address reuse is never enabled.
Stop other capture/controller programs if port 9001/50018/9002 is already occupied.
Changing the listen fields does not rebind an active socket: press Start monitor
to restart. The displayed bound endpoint remains authoritative.

For a second computer, use its reachable IPv4 address, patch both directions and
allow only the required UDP ports on trusted networks. The app accepts literal
IPv4 unicast destinations, not DNS names, IPv6 or multicast. `0.0.0.0` is allowed
only for an explicit all-interface listener, after confirmation. UDP/OSC has no
authentication, encryption or guaranteed delivery. Never expose it to the Internet.

Incoming messages support `i/f/s/b/h/d/T/F/N` and bounded bundles; timetags are
**not scheduled**. Unsupported tags, malformed padding/lengths, non-finite numbers,
overdeep bundles and oversized packets reject the whole datagram. The event queue
is bounded to 2048; the log retains only the latest 1000 records. Excess events
are dropped and counted rather than growing memory indefinitely. Export CSV is
explicit; logs are not written automatically. The monitor does not authenticate
senders: reported state may be spoofed on an untrusted network.

## Profiles

Load/Save JSON explicitly; see `profiles/spatialfabric-showcase.json`. Profiles
store endpoints, cue names/bases and spatial settings. They contain no credentials
and never store live-enabled or listener-running state. Validation occurs before
application; invalid profiles do not replace settings. Saving uses atomic replace.
The composer and last-reported state are intentionally not persisted.

## Build the Windows executable

On Windows, use a regular **64-bit Python 3.13 with Tk**:

```powershell
py -3.13 -m venv .venv
.venv\Scripts\python -m pip install -r requirements-build.txt
.venv\Scripts\python build_windows.py
```

Builds run the protocol/profile/UDP tests, use PyInstaller, bundle Python/Tk and
collect runtime license notices. One upstream Tcl license download may be needed
if the Python distribution omits it. No Unreal build or native SDK is involved.
Build artifacts stay under ignored `dist/`; source is independently runnable.
Read `THIRD_PARTY_NOTICES.md` and **About / licenses** in the app.

## Test and limits

```powershell
python -m unittest discover -s tests -v
# Optional on an interactive desktop; sends only to ephemeral loopback sockets:
$env:OSC_CONTROLLER_GUI_TESTS = '1'
python -m unittest discover -s tests -v
# Check packaged startup without sending or binding:
dist\SpatialFabricOSCController.exe --smoke-test "$env:TEMP\osc-smoke.json"
```

Local validation: Windows 11 x64, Python 3.13.14 / Tk 8.6; **14 core tests and
3 interactive GUI tests passed**. The bundled Windows executable also passed its
no-network startup smoke check. Physical hardware integration remains unverified.

Tests cover wire bytes, bounded/atomic parsing, profiles, socket ownership,
loopback sending/receiving and opt-in UI transport/composer behavior. Startup smoke
checks verify no implicit network activity. This does not certify a renderer,
physical audio, latency, channel fidelity, packet delivery, or every OS. No network
operations or audio are required merely to open the UI.
