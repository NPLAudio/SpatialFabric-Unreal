"""Structural smoke tests for the combined plugin; no Unreal installation required.

Run: python Tools/test_plugin_layout.py
These do not replace Unreal compilation or runtime/audio tests.
"""
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class UnifiedPluginLayoutTests(unittest.TestCase):
    def setUp(self):
        self.descriptor = json.loads((ROOT / "SpatialFabric.uplugin").read_text(encoding="utf-8"))

    def test_single_plugin_descriptor(self):
        self.assertEqual(
            sorted(p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*.uplugin")
                   if not {"Intermediate", "Binaries", "dist"}.intersection(p.relative_to(ROOT).parts)),
            ["SpatialFabric.uplugin"],
        )

    def test_all_modules_declared_with_correct_host_type(self):
        expected = {
            "SpatialFabric": "Runtime", "SpatialFabricEditor": "Editor",
            "AudioLoom": "Runtime", "AudioLoomEditor": "Editor",
        }
        modules = self.descriptor["Modules"]
        self.assertEqual(len(modules), len(expected))
        self.assertEqual({m["Name"]: m["Type"] for m in modules}, expected)
        for name in expected:
            self.assertTrue((ROOT / "Source" / name / f"{name}.Build.cs").is_file(), name)

    def test_only_external_plugin_dependency_is_osc(self):
        self.assertEqual(self.descriptor["Plugins"], [{"Name": "OSC", "Enabled": True}])

    def test_editor_embeds_exported_playback_panel(self):
        rules = (ROOT / "Source/SpatialFabricEditor/SpatialFabricEditor.Build.cs").read_text()
        self.assertIn('"AudioLoomEditor"', rules)
        source = (ROOT / "Source/SpatialFabricEditor/Private/UI/SSpatialFabricPanel.cpp").read_text()
        self.assertIn('SNew(SAudioLoomPanel)', source)
        header = (ROOT / "Source/AudioLoomEditor/Public/UI/SAudioLoomPanel.h").read_text()
        self.assertIn('class AUDIOLOOMEDITOR_API SAudioLoomPanel', header)

    def test_legacy_runtime_class_and_config_identity(self):
        source = ROOT / "Source/AudioLoom"
        for header in ["AudioLoomActor.h", "AudioLoomComponent.h", "AudioLoomOscSettings.h"]:
            self.assertTrue((source / "Public" / header).is_file())
        module = (source / "Private/AudioLoom.cpp").read_text()
        self.assertIn('IMPLEMENT_MODULE(FAudioLoomModule, AudioLoom)', module)
        settings = (source / "Public/AudioLoomOscSettings.h").read_text()
        self.assertIn('UCLASS(Config = Engine, DefaultConfig', settings)

    def test_unified_branding(self):
        import struct
        for name in ("assets/spatialfabric-unified.png", "assets/spatialfabric-mark.png", "Resources/Icon128.png"):
            data = (ROOT / name).read_bytes()
            self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
            width, height = struct.unpack(">II", data[16:24])
            self.assertGreater(width, 0)
            self.assertGreater(height, 0)
            if name.startswith("Resources"):
                self.assertEqual((width, height), (128, 128))
        self.assertIn("assets/spatialfabric-unified.png", (ROOT / "README.md").read_text())

    def test_distribution_includes_notice_and_migration_guide(self):
        notice = (ROOT / "ThirdParty/AudioLoom/LICENSE").read_text()
        self.assertIn('Copyright (c) 2026 AudioLoom Contributors', notice)
        self.assertIn('Permission is hereby granted', notice)
        self.assertTrue((ROOT / "Docs/AudioPlayback.md").is_file())
        self.assertTrue((ROOT / "Resources/Icon128.png").is_file())


if __name__ == "__main__":
    unittest.main()
