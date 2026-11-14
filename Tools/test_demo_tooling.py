"""Fast source/capture checks; Unreal API behavior is covered by the live demo run."""
import ast
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parent / "Unreal"
SPEC = importlib.util.spec_from_file_location("spatialfabric_capture", ROOT / "capture_spatialfabric_osc.py")
CAPTURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CAPTURE)


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


if __name__ == "__main__":
    unittest.main()
