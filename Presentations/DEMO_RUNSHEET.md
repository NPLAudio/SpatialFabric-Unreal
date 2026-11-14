# Ten-minute demo run sheet

[Deck and notes](README.md) · [Detailed tour](../Docs/SpatialSandboxDemo.md)

## Before the audience: do not spend the demo fixing a patch

- Build/open the example in the intended UE 5.8 installation. Let shaders finish.
- Open `L_SpatialFabric_Showcase`; fit Window → Spatial Fabric so rows are readable.
- Confirm an independent system mute, low programme level, intended OS endpoint
  and physical channels. The 2nd output must exist for the glass/replay station.
- Begin Play/autoplay stays off. Stop every cue and any pending replay.
- For the local packet demonstration, start the intended ADM/Custom/state receiver
  before the timer; do not bind a second collector on those same ports.
- Keep the stage fixed and its visibility checkbox in a known state. No need to
  regenerate the level or run scripts in front of the audience.
- Decide whether an external renderer/audio input patch is actually present.
  If it is not, explicitly distinguish packet inspection from spatial audio listening.
- Keep the patch card (slide 15), a saved baseline and a receiver screenshot handy.

## Live order — 10:00 total

| Time | Action | Say / observe |
|---|---|---|
| 0:00–0:45 | Show the gallery; press Play | Three moving sources, one fixed reference; audio has not started automatically. |
| 0:45–2:00 | Stage → Objects → Radar | The box is a measurement frame. Show an actor binding and its spatial ID, not an audio output channel. Mention the outer floor/barriers without a lengthy walking detour. |
| 2:00–3:15 | Open Output and the real receiver | Identify ADM 1–4 and Custom 101–104. The preview and the receiver are separate observations. If only preview is available, say so. |
| 3:15–4:00 | Point out the two signal paths | A moving dot is not yet audio panning. Show where the renderer's audio input patch would live. |
| 4:00–4:45 | Playback: percussion station Play, then let it finish | A prepared, short one-shot; verify the selected physical output at safe level. |
| 4:45–5:45 | Play stereo groove; toggle Loop off | Corresponding source/device channels. Let the queued tail finish rather than hitting Stop immediately. |
| 5:45–7:00 | Play glass/replay cue; wait for one replay; press Stop | Natural finish, delay, restart; Stop cancels pending replay. Do not leave it counting down while explaining the next tab. |
| 7:00–8:30 | Enable Follow Position & Orientation; move player briefly | Release mouse with Shift+F1 for the panel. Watch changed relative coordinates; the authored stage stays put. This follows the player actor, not verified HMD pose. |
| 8:30–9:00 | Return to fixed stage / show visibility control | The hide option affects game/PIE; the editor box remains visible even in Game View. Do not chase unrelated viewport show flags mid-talk. |
| 9:00–10:00 | Stop all cues, Stop OSC if started, stop PIE, recap | One control patch, one audio patch, measured timing and explicit failure. Leave no loop or replay running before Q&A. |

## If something fails

**No audio:** mute/stop first, verify the selected endpoint/physical patch, then use
one known-good short cue. Do not raise master level or blindly switch to the OS
default. If unresolved in 30 seconds, continue the control-data demonstration and
explain the unproven link honestly.

**No packets:** verify receiver destination port, adapter/target enabled state,
transmit-on-change and port ownership. Use the saved capture as a fallback and label
it recorded evidence, not a current live connection.

**Editor stalls/shaders:** pause the explanation on the patch diagram. Do not fill
time by repeatedly pressing Play or introducing exclusive-mode/driver changes.

**Unexpected sound/replay:** use the independent mute and component Stop. Stop PIE
as necessary. Explain the distinction between a cue request and a confirmed audio
route after the system is safe.

**Time is short:** drop the optional floor walk and detailed coordinate/OSC fields.
Keep the two-lane explanation, one cue, one loop exit and explicit safe shutdown.

## Q&A prompts

- What would you patch externally, and which device/renderer must be verified?
- Which source should be room-fixed versus listener-relative?
- What is the recovery plan if a device disappears or a network packet is lost?
- Do you need a full cue-list, redundancy or synchronization layer beyond this tool?

Avoid promising sample-accurate distributed GO, automatic binaural rendering,
all manufacturer compatibility, measured zero latency or untested Mac/cooked behavior.
