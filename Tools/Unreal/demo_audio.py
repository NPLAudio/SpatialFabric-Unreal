"""Original procedural demo sound design; standard-library Python, no sample downloads.
Generate sources: python demo_audio.py --output <project>/SourceAssets/SpatialFabricDemo/Audio
PCM16 / 48 kHz by default, conservatively normalized to 0.06 peak (-24.4 dBFS).
"""
from array import array
import argparse
import math
from pathlib import Path
import random
import sys
import wave

SPECS = {
    "impact": ("SW_SF_MonoCue", 1, 1.2, "Warm wood/percussion hit with a short room-reflection tail"),
    "groove": ("SW_SF_StereoLoop", 2, 4.0, "120 BPM stereo groove: drums, bass, chords and ping-pong plucks"),
    "glass": ("SW_SF_ReplayCue", 1, 0.6, "Double glass/machinery tap with a short resonant tail"),
}
PEAK = 0.06
TAU = 2 * math.pi


def _note(midi):
    return 440.0 * 2 ** ((midi - 69) / 12)


def _add(buffers, start, duration, rate, voice, amplitude=1.0, pan=0.0, wrap=False):
    frames = len(buffers[0])
    offset = round(start * rate)
    left = math.cos((pan + 1) * math.pi / 4)
    right = math.sin((pan + 1) * math.pi / 4)
    for i in range(round(duration * rate)):
        frame = offset + i
        if wrap:
            frame %= frames
        elif frame >= frames:
            break
        value = voice(i / rate, i) * amplitude
        if len(buffers) == 1:
            buffers[0][frame] += value
        else:
            buffers[0][frame] += value * left
            buffers[1][frame] += value * right


def _pitched(buffers, midi, start, duration, rate, amplitude, pan=0, decay=5, bright=0.3, wrap=False):
    frequency = _note(midi)
    def voice(t, i):
        envelope = min(1.0, t / 0.006, (duration - t) / 0.035) * math.exp(-decay * t)
        phase = TAU * frequency * t
        return envelope * (math.sin(phase) + bright * math.sin(2 * phase + 0.2) + bright * 0.35 * math.sin(3 * phase))
    _add(buffers, start, duration, rate, voice, amplitude, pan, wrap)


def _impact(buffers, rate):
    # A damped, inharmonic soundboard rather than a rising sci-fi oscillator sweep.
    rng = random.Random(1408)
    modes = [(83, 7.5, 0.75), (139, 12, 0.35), (227, 17, 0.22), (353, 24, 0.12), (587, 34, 0.06)]
    phases = [rng.uniform(-0.3, 0.3) for _ in modes]
    dry = array("d", [0.0]) * len(buffers[0])
    low = 0.0
    onset = round(0.025 * rate)
    for i in range(onset, len(dry)):
        t = (i - onset) / rate
        noise = rng.uniform(-1, 1)
        low += 0.16 * (noise - low)
        attack = min(1, t / 0.002)
        body = sum(level * math.sin(TAU * frequency * t + phase) * math.exp(-decay * t)
                   for (frequency, decay, level), phase in zip(modes, phases))
        contact = (noise - low) * math.exp(-95 * t) * 0.22
        dry[i] = attack * (body + contact + 0.25 * low * math.exp(-30 * t))
    for delay, gain in [(0, 1), (0.023, 0.18), (0.047, -0.12), (0.079, 0.085), (0.127, -0.055), (0.191, 0.035)]:
        offset = round(delay * rate)
        for i in range(offset, len(dry)):
            buffers[0][i] += dry[i - offset] * gain


def _groove(buffers, rate):
    rng = random.Random(120)
    noise = [rng.uniform(-1, 1) for _ in range(round(rate * 0.22))]
    # Eight quarter notes, with backbeat and off-beat hats.
    for beat in range(8):
        def kick(t, i):
            phase = TAU * (49 * t + 95 / 32 * (1 - math.exp(-32 * t)))
            return min(1, t / 0.003) * math.exp(-19 * t) * math.sin(phase)
        _add(buffers, beat * 0.5, 0.35, rate, kick, 0.65, wrap=True)
        if beat % 2:
            def snare(t, i):
                return min(1, t / 0.002) * math.exp(-24 * t) * (0.65 * noise[i] + 0.3 * math.sin(TAU * 185 * t))
            _add(buffers, beat * 0.5, 0.21, rate, snare, 0.32, pan=0.12, wrap=True)
        def hat(t, i):
            previous = noise[i - 1] if i else 0
            return min(1, t / 0.001) * math.exp(-70 * t) * (noise[i] - previous)
        _add(buffers, beat * 0.5 + 0.25, 0.08, rate, hat, 0.10, pan=(-0.55 if beat % 2 else 0.55), wrap=True)
    bass = [36, 43, 32, 39, 34, 41, 31, 38]
    for beat, midi in enumerate(bass):
        _pitched(buffers, midi, beat * 0.5 + 0.03, 0.42, rate, 0.28, decay=7, bright=0.35, wrap=True)
    chords = [(60, 63, 67, 70), (56, 60, 63, 67), (58, 62, 65, 72), (55, 58, 62, 65)]
    for chord_index, chord in enumerate(chords):
        for index, midi in enumerate(chord):
            frequency = _note(midi)
            def pad(t, i, f=frequency):
                envelope = min(1, t / 0.14, (1.45 - t) / 0.35) * math.exp(-1.5 * t)
                return envelope * (math.sin(TAU * f * 0.997 * t) + math.sin(TAU * f * 1.003 * t))
            _add(buffers, chord_index, 1.45, rate, pad, 0.045, pan=(-0.7 if index % 2 else 0.7), wrap=True)
    melody = [72, 79, 75, 82, 80, 75, 72, 79, 70, 77, 74, 72, 67, 74, 70, 77]
    for step, midi in enumerate(melody):
        pan = -0.65 if step % 2 else 0.65
        _pitched(buffers, midi, step * 0.25 + 0.02, 0.65, rate, 0.10, pan, decay=10, bright=0.45, wrap=True)
        _pitched(buffers, midi, step * 0.25 + 0.395, 0.6, rate, 0.028, -pan, decay=11, bright=0.3, wrap=True)


def _glass(buffers, rate):
    rng = random.Random(602)
    clicks = [rng.uniform(-1, 1) for _ in range(round(rate * 0.035))]
    for onset, level in [(0.015, 0.7), (0.17, 0.4)]:
        def voice(t, i):
            attack = min(1, t / 0.0015)
            ringing = sum(math.sin(TAU * f * t) * math.exp(-(10 + j * 3) * t) / (j + 1)
                          for j, f in enumerate((1173, 1687, 2399, 3251)))
            click = clicks[i] * math.exp(-110 * t) if i < len(clicks) else 0
            body = 0.35 * math.sin(TAU * 285 * t) * math.exp(-25 * t)
            return attack * (ringing * 0.35 + click * 0.35 + body)
        _add(buffers, onset, 0.42, rate, voice, level)


def render(kind, rate=48000):
    if kind not in SPECS:
        raise ValueError("Unknown sound design: " + str(kind))
    if not isinstance(rate, int) or not 8000 <= rate <= 192000:
        raise ValueError("Expected an integer sample rate between 8000 and 192000")
    _, channels, duration, _ = SPECS[kind]
    buffers = [array("d", [0.0]) * round(duration * rate) for _ in range(channels)]
    {"impact": _impact, "groove": _groove, "glass": _glass}[kind](buffers, rate)
    # Very short seam fades on the periodic groove; longer declick/tail fades on cues.
    fade_in = round(rate * (0.002 if kind == "groove" else 0.006))
    fade_out = round(rate * (0.002 if kind == "groove" else 0.045))
    frames = len(buffers[0])
    for channel in buffers:
        for i in range(fade_in): channel[i] *= i / fade_in
        for i in range(fade_out): channel[frames - 1 - i] *= i / fade_out
    maximum = max(abs(value) for channel in buffers for value in channel)
    scale = PEAK / maximum if maximum else 1.0
    for channel in buffers:
        for i in range(frames): channel[i] *= scale
    return buffers


def write_wav(path, kind, rate=48000):
    buffers = render(kind, rate)
    pcm = array("h")
    for frame in zip(*buffers):
        pcm.extend(round(value * 32767) for value in frame)
    if sys.byteorder != "little": pcm.byteswap()
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as out:
        out.setnchannels(len(buffers))
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(pcm.tobytes())
    return {"file": str(path), "design": kind, "channels": len(buffers), "duration": len(buffers[0]) / rate, "peak_dbfs": 20 * math.log10(PEAK), "description": SPECS[kind][3]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    for kind, spec in SPECS.items():
        print(write_wav(args.output / (spec[0] + ".wav"), kind))
