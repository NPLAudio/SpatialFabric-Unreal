# Troubleshooting: prove one link at a time

[Documentation index](README.md) · [OSC reference](OSCReference.md)

## I see objects moving, but hear nothing

Movement/OSC is **control data**, not audio. Check which path you intended to hear.
For AudioLoom, assign a SoundWave, choose a valid OS output, press Play manually,
and verify its physical patch. For an external renderer, confirm that audio reaches
its inputs and that the received object IDs address those inputs. Watching Radar
or Output is not a test of either audio route.

## Playback says stopped / refuses Play

- Read **Get Last Playback Error** and Output Log. No assigned SoundWave, decode
  policy, unavailable device, invalid channel or unsupported format are different faults.
- Empty DeviceId chooses the OS default. A stale explicit ID fails closed; reselect
  the intended interface after an installation/driver change. Do not blindly choose
  default when that could send sound into an unsafe route.
- Check OS device activity/permissions and the actual mix layout/channel count.
- Keep the test cue short/low-level; use Prepare Audio and confirm decode success.
- Exclusive requests can fall back to shared. A running worker alone does not prove
  exclusive operation, intended physical output or a particular measured latency.

## It plays on the wrong channel / only one side

Channel 0 maps corresponding source channels. A mono asset therefore uses the first
output, not every speaker. Channel 1..N routes **source channel 1 only** to that
one output; selecting output 2 on a stereo file does not extract source channel 2.
Use a suitable mono asset or corresponding-channel mapping for the desired content.
Check the device's channel numbering and physical patch independently. No automatic
stereo downmix, channel broadcast, external renderer bus patch or Unreal submix
routing is created.

## My Unreal attenuation/effects do nothing

Direct playback bypasses Unreal's AudioComponent mixer graph. Treat its PCM/device
route independently of spatial Metadata, submixes, attenuation and SoundClass controls.
ADM gain/mute/width do not automatically control an AudioLoom component on the same actor.

## A one-shot loops or starts again later

Distinguish Loop from Auto Replay. Loop repeats in the device stream; Auto Replay
waits after natural completion and starts another cue. Turn them off separately.
Explicit Stop cancels pending replay. A new failed-device request is not a natural
completion. Do not use a delayed event elsewhere in your Blueprint to restart a
cue and then attribute that event to the component's replay mechanism.

## Packaged project is silent but editor works

Set each relevant SoundWave to **Force Inline**, ensure it is referenced/included,
recook, and inspect codec availability and channel-order restrictions. Streaming
content is rejected; a sibling source WAV is not a packaged fallback. Plugin builds
in Development/Shipping are not a test of your cooked media. Check [release gates](../VALIDATION.md).

## Output preview looks correct but the receiver sees no packets

1. Check actual Manager/adapter/target enable state and the placed actor's endpoint.
2. Confirm spatial networking permission. Editor and packaged behavior differ.
3. Put the receiver on the **destination** UDP port (default ADM 50018), not 8100.
4. Use 127.0.0.1 only for the same computer; 0.0.0.0 is a listen wildcard, not a
   meaningful remote destination. Check IP, firewall, subnet and interface routing.
5. Disable transmit-on-change temporarily or move an actor; a static scene may
   correctly emit no new packets.
6. A successful UDP send is not a receiver acknowledgement. Confirm address/type
   support and object IDs in the destination's own monitor.

Spatial permissions/defaults belong in **DefaultSpatialFabric.ini**, even if an
older diagnostic text suggests DefaultEngine.ini. Playback OSC intentionally keeps
its separate AudioLoom section in **DefaultEngine.ini**.

## “Port unavailable” / receiver will not start

Only one owner should bind a given endpoint. Close another example/editor or OSC
application using 8100/9000. Do not use the same port for spatial feedback and
playback commands. The standalone example's editor MCP plugins are disabled.
If capturing outgoing packets with the provided collector, close another receiver
on 50018/9002/9001 first: the collector deliberately refuses occupied ports.

Actual exact binding determines server status; a configured IP string alone is not
proof of a socket. IPv4 multicast, hostnames/IPv6 and unsupported wire tags are not
part of the exact-bind unicast control receiver's supported policy.

## Playback OSC receives packets but does nothing

Press **Start OSC** in Playback and check the active editor/PIE world. Use the
component's resolved address: `/base/play`, `/base/stop`, `/base/loop`. Play accepts
no args, float >=0.5 or int32 >=1; a string "1" is not that trigger. Prefer numeric
0/1 for Loop; do not assume all supported wire types have identical transport meaning.
Inspect duplicate/default paths after renaming/attaching actors; explicit stable
addresses are preferable for show files. Monitor `/base/state` on 9001.

## Stage box is invisible

The stage is a wireframe measurement volume, not the decorative floor. Current
visibility logic restores it in editor Game View and without selection. In PIE,
uncheck **Hide stage volume when game is running**. Check Outliner eye/temporary
actor hiding, component visibility and viewport show flags as well. Old binaries
may still exhibit the inherited HiddenInGame bug: rebuild and restart cleanly;
changing source while an old DLL is loaded does not apply the fix.

## My player used to fall into the void

The shipped example includes a 120 × 120 m solid outer floor and 3.5 m barriers.
Use the floor updater for an older generated level. Its report must show nine
blocking traces. Do not delete the foundation/collision or place the PlayerStart
outside the enclosure. Engine fly/teleport cheats or custom movement can bypass
ordinary walking constraints and are not blocked as a security boundary.

## Listener movement looks doubled / stage drifts

Use fixed mode to establish a baseline. Follow changes a virtual basis, not the
stage actor. Do not add a Blueprint that physically moves its ancestor toward the
listener every frame. Already-relative object positions are accompanied by identity
listener pose; do not apply a second listener transform downstream. The default
player follow uses view-target actor origin/rotation, not camera-eye/HMD offsets.

## Updates feel slow or cues start late

Editor background throttling, frame time, first decode, device initialization,
output buffers, driver/converter delay and downstream systems are separate causes.
Prewarm media; measure the entire route with appropriate audio capture. A send-rate
setting is a ceiling, not a metronome; an output-latency diagnostic of zero means
unavailable data here, not measured zero delay. Check foreground/background behavior
before interpreting a low editor-FPS display as an audio sample-rate problem.

## Missing modules, classes or example assets

- Build with matching UE 5.8/toolchain; do not reuse arbitrary old DLLs.
- Remove duplicate standalone AudioLoom copies, including engine-level ones.
- For the prepared example, open its `.uproject`, not the parent plugin descriptor.
- Keep `/Game/SpatialFabricDemo` paths intact; copy into the **Content/** folder,
  not an extra nested Content directory. Use Unreal migration tools for relocation.
- The published project uses its own C++ walking character; the local SpatialSandbox
  may instead use its pre-existing FirstPerson Blueprint. Do not copy that local
  project's entire config (tokens/MCP settings are intentionally not distributed).

## What to include in a useful bug report

Engine version/build, OS/interface/driver, plugin commit, editor or packaged mode,
asset format/channel count/loading behavior, selected device/channel, exact OSC
address/types/IP/ports, relevant log excerpts and the smallest reproducible sequence.
Never publish tokens, device-account secrets, local MCP credentials or proprietary
show media. [Development](Development.md) explains safe evidence locations.
