"""Fast source/capture checks; Unreal API behavior is covered by the live demo run."""
import ast
import importlib.util
import math
from pathlib import Path
import struct
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parent / "Unreal"
SPEC = importlib.util.spec_from_file_location("spatialfabric_capture", ROOT / "capture_spatialfabric_osc.py")
CAPTURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CAPTURE)
AUDIO_SPEC = importlib.util.spec_from_file_location("spatialfabric_demo_audio", ROOT / "demo_audio.py")
AUDIO = importlib.util.module_from_spec(AUDIO_SPEC)
AUDIO_SPEC.loader.exec_module(AUDIO)


class DemoToolingTests(unittest.TestCase):
    def test_all_unreal_scripts_are_distributed_and_parse(self):
        plugin = ROOT.parent.parent
        self.assertTrue((plugin / "Docs/SpatialSandboxDemo.md").is_file())
        self.assertIn("!Docs/SpatialSandboxDemo.md", (plugin / ".gitignore").read_text())
        self.assertIn("/Tools/Unreal/...", (plugin / "Config/FilterPlugin.ini").read_text())
        for name in (
            "build_spatialfabric_demo.py", "inspect_spatialfabric_demo.py",
            "validate_spatialfabric_demo.py", "capture_spatialfabric_osc.py",
            "begin_spatialfabric_handoff_check.py", "finish_spatialfabric_handoff_check.py",
            "demo_audio.py", "refresh_spatialfabric_demo_audio.py", "extend_spatialfabric_demo_floor.py",
            "smoke_example.py",
        ):
            with self.subTest(script=name):
                path = ROOT / name
                self.assertTrue(path.is_file())
                ast.parse(path.read_text(encoding="utf-8"), filename=str(path))

    def test_independent_osc_capture_decoder(self):
        packet = b"/sf\0,f\0\0" + struct.pack(">f", 1.0)
        expected = {"address": "/sf", "tags": ",f", "args": [1.0]}
        self.assertEqual(list(CAPTURE.decode(packet)), [expected])
        bundle = b"#bundle\0" + struct.pack(">Q", 1) + struct.pack(">i", len(packet)) + packet
        self.assertEqual(list(CAPTURE.decode(bundle)), [expected])
        self.assertEqual(list(CAPTURE.decode(b"/sf\0,T\0\0"))[0]["args"], [True])
        for bad in (packet[:7], b"/sf\0,q\0\0", b"/sf\0,f\0\0" + struct.pack(">f", float("nan"))):
            with self.subTest(packet=bad):
                with self.assertRaises((ValueError, struct.error)):
                    list(CAPTURE.decode(bad))


class DemoSoundDesignTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.buffers = {kind: AUDIO.render(kind, 12000) for kind in AUDIO.SPECS}

    def test_shapes_levels_and_declicking(self):
        for kind, channels in self.buffers.items():
            with self.subTest(design=kind):
                spec = AUDIO.SPECS[kind]
                self.assertEqual(len(channels), spec[1])
                for channel in channels:
                    self.assertEqual(len(channel), round(spec[2] * 12000))
                    self.assertTrue(all(math.isfinite(value) for value in channel))
                    self.assertLessEqual(max(abs(value) for value in channel), AUDIO.PEAK + 1e-12)
                    self.assertEqual(channel[0], 0)
                    self.assertEqual(channel[-1], 0)
                    rms = math.sqrt(sum(v * v for v in channel) / len(channel))
                    self.assertGreater(rms, 0.001)
                    self.assertLess(rms, 0.04)

    def test_stereo_is_not_duplicated_mono_and_sources_are_deterministic(self):
        left, right = self.buffers["groove"]
        energy = sum(a * a + b * b for a, b in zip(left, right))
        difference = sum((a - b) ** 2 for a, b in zip(left, right))
        self.assertGreater(difference / energy, 0.05)
        self.assertEqual(self.buffers["glass"], AUDIO.render("glass", 12000))

    def test_pcm16_wav_and_invalid_input(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "glass.wav"
            AUDIO.write_wav(path, "glass", 12000)
            with wave.open(str(path), "rb") as sound:
                self.assertEqual((sound.getnchannels(), sound.getsampwidth(), sound.getframerate(), sound.getnframes()), (1, 2, 12000, 7200))
        with self.assertRaises(ValueError): AUDIO.render("unknown")
        with self.assertRaises(ValueError): AUDIO.render("glass", 0)


if __name__ == "__main__":
    unittest.main()
