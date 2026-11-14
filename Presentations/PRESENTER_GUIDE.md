# Presenter guide — From Cue to Space

15-minute talk + 10-minute demo + open Q&A. See DEMO_RUNSHEET.md for the live procedure.

## 01. Make movement part of your sound design

Section: talk · planned duration: 45 seconds

0:00–0:45. Open with the creative aim: a performer, prop or virtual actor can become a moving source control. This is not a programming talk. We will follow the patch like a sound designer, then operate it live. Say clearly that the demo uses modest listening levels and that there will be time for questions after the ten-minute demo.

## 02. The problem: two patches to manage

Section: talk · planned duration: 60 seconds

0:45–1:45. Ask the room to imagine a moving prop with a playback cue. Point out the two questions on the slide. An object number on a spatial processor and an output number on an audio interface can look similar on a screen but mean different things. A good patch sheet connects them deliberately. Avoid suggesting that adding an actor automatically makes a sound spatial.

## 03. Two lanes, not one

Section: talk · planned duration: 90 seconds

1:45–3:15. Spend time on this editable diagram. The cyan lane is like automation/control on a console; it carries numbers, not programme audio. The purple lane carries actual samples to an operating-system device. They can work together in a larger system, but SpatialFabric does not guess the connection. If there is no renderer/audio patch at the venue, the demo can prove control packets and local cue routing; it must not pretend to prove spatial rendering.

## 04. Spatial lane: a moving control surface

Section: talk · planned duration: 75 seconds

3:15–4:30. Use console language: an actor is a source fader under automation; a binding is the patch/name/ID; an adapter is the language spoken at the destination. ADM-OSC and Custom are the implemented choices, not a list of separately certified manufacturer integrations. Show that IDs can differ per target: the example uses 1–4 for ADM and 101–104 for Custom. Preview is useful, but UDP receipt is a separate test.

## 05. A stage is your tape measure

Section: talk · planned duration: 75 seconds

4:30–5:45. Point to the box and cardinal positions, not a formula. Its center is not the floor. The default full box is 20 by 15 by 8 metres. Internal forward/right/up coordinates are translated at the protocol boundary. ADM Cartesian and polar share a radius-limited point; a box corner is not an unlimited renderer radius. Ask the audience to make a simple cardinal-point check part of calibration. The decorative floor is not the measurement volume.

## 06. Listener following changes the viewpoint

Section: talk · planned duration: 60 seconds

5:45–6:45. Analogy: changing the observer on a plan, not physically moving the venue. The implementation follows the player view-target actor origin/rotation; do not describe this as verified HMD/head tracking or exact camera-eye tracking. Already-relative objects are sent with an identity listener pose, so a downstream system must not apply the same movement twice. In the demo, watch Radar/Output rather than claiming that the separate stereo cue suddenly pans.

## 07. Audio lane: a separate cue player

Section: talk · planned duration: 90 seconds

6:45–8:15. Explain the backend as a second operator keeping the next tray of audio ready while the editor draws pictures. Preparing media pays the initial decode cost earlier; it does not promise zero onset delay. Samples are paced to the device rate to avoid assuming every interface is 48 kHz. Windows is the validated native platform; Core Audio implementation is not Mac device certification. Designers should not expect Unreal submix/attenuation or ADM metadata to control this separate stream automatically.

## 08. Channel 0 does not mean everywhere

Section: talk · planned duration: 75 seconds

8:15–9:30. Take a stereo cue: corresponding mapping sends left to output 1 and right to output 2. Selecting output 2 alone sends the first/left source channel to output 2, not the right channel. A mono cue in corresponding mode does not appear in every speaker. This is a patching rule, not a loudspeaker renderer. Confirm the interface's real channel ordering at low level before using it in a rig.

## 09. What happens when you press Play?

Section: talk · planned duration: 90 seconds

9:30–11:00. Walk the boxes from prepare to request to device start to finish. An invalid selected device should fail rather than quietly choose another. Natural finish drains queued samples; an explicit Stop is a cancellation. Loop and delayed Auto Replay are different behaviors. Monitoring comes from stream lifecycle events, not a fake acknowledgement on every button press, but even a running stream is not proof the physical loudspeaker is connected. OSC reception remains responsive while PIE is paused; replay delay is still not a sample-clock scheduler.

## 10. Timing: four clocks, not zero latency

Section: talk · planned duration: 75 seconds

11:00–12:15. Frame updates and audio samples are not the same clock. A send-rate setting is a ceiling on update batches, not guaranteed packets per second or synchronized GO across machines. Background editor throttling can make control look slow without changing the audio device's sample rate. The output-latency diagnostic reported zero when unavailable in one run: that is not measured zero delay. Prewarm, choose sensible buffers, and measure end to end with the intended interface/renderer.

## 11. Build a show-safe patch

Section: talk · planned duration: 90 seconds

12:15–13:45. Empty device means the OS default and can change when the computer changes. Explicit selection is preferable when accidental routing would be unsafe. OSC is unauthenticated; the example is local by default and does not enable an editor MCP server. Two applications cannot safely own the same receive endpoint. Cue limits/Force Inline cooking matter for deployment. Keep an independent system mute and rehearsed fallback; this is not a safety-rated show-control transport.

## 12. What is proven—and what needs a sound check

Section: talk · planned duration: 75 seconds

13:45–15:00. Close the talk by separating software evidence from system certification. The published example has a separate silent PIE smoke test, including landing on its outer floor and a capsule blocked by the perimeter. This is useful evidence but not listening/capture verification of the venue. Mac/Linux/other engines and actual packaged codecs still have their own gates. Transition: now we will operate the prepared scene for ten minutes, then discuss how this fits your workflow.

## 13. 10-minute demonstration

Section: demo · planned duration: 600 seconds

DEMO 0:00–10:00. Follow DEMO_RUNSHEET.md. Keep all cues manual-start and quiet. If no receiver is available, say that Output is a preview and do not claim a packet-delivery test. If no external spatial renderer/audio patch is present, explicitly explain that moving objects is not panning the separate PCM groove. Avoid the automated live validator on stage: it intentionally tries a missing device and produces an expected error. End by stopping every cue and restoring fixed stage/listener and OSC state. Allow a few seconds to demonstrate the safety floor only if the planned sequence is ahead of time.

## 14. Questions: where would this sit in your patch?

Section: qa · planned duration: 0 seconds

Q&A. Invite sound-design and systems questions before implementation details. Useful answers: this is not automatically a binaural renderer; it does not replace a full cue-list/redundancy workflow; a channel ID is not a spatial object ID; physical delay must be measured. Offer to inspect the patch with a sound designer and system engineer rather than promising untested processor or interface support.

## 15. Keep this patch card nearby

Section: reference · planned duration: 0 seconds

Reference only. Receive ports and outgoing destinations are different. The prepared example's names/addresses and exact numeric trigger semantics are in the demo guide/OSC reference. Do not change to a venue LAN until the local patch and firewall/ownership plan are understood.

## 16. Take the example, keep the questions

Section: reference · planned duration: 0 seconds

Reference/credits. Source and generated presentation are editable. No external sample library is bundled. The example uses original minimal walking-character source rather than Epic FirstPerson character/animation packages. Engine basic-shape references still require Unreal Engine and its applicable terms. No manufacturer endorsement, hardware approval or processor certification is implied.
