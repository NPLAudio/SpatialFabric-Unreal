"""Engine-free checks for the published project, portable copy, docs and presentation."""
import hashlib
import json
from pathlib import Path
import re
import tempfile
import unittest
from urllib.parse import unquote
import xml.etree.ElementTree as ET
from zipfile import ZipFile
from prepare_example import ROOT, EXAMPLE, prepare


class ExampleDistributionTests(unittest.TestCase):
    def test_example_asset_inventory_and_dependency_closure(self):
        manifest = json.loads((EXAMPLE / 'ASSET_MANIFEST.json').read_text())
        self.assertEqual(len(manifest['files']), 16)
        self.assertFalse((EXAMPLE / 'Content/SpatialFabricDemo/Textures').exists())
        self.assertFalse((EXAMPLE / 'Content/SpatialFabricDemo/Materials/M_SF_Banner.uasset').exists())
        self.assertFalse((EXAMPLE / 'Content/SpatialFabricDemo/Materials/M_SF_Driver.uasset').exists())
        actual = {p.relative_to(EXAMPLE).as_posix() for folder in ('Content', 'SourceAssets') for p in (EXAMPLE / folder).rglob('*') if p.is_file()}
        self.assertEqual(actual, {item['path'] for item in manifest['files']})
        for item in manifest['files']:
            path = EXAMPLE / item['path']
            self.assertEqual(path.stat().st_size, item['bytes'], item['path'])
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), item['sha256'], item['path'])
            self.assertLess(item['bytes'], 100 * 1024 * 1024)
        self.assertTrue(all(p.startswith('/Game/SpatialFabricDemo/') for p in manifest['project_package_dependencies']))
        self.assertFalse((EXAMPLE / 'Content/Characters').exists())
        self.assertFalse((EXAMPLE / 'Content/FirstPerson').exists())

    def test_example_has_own_player_and_no_enabled_mcp(self):
        descriptor = json.loads((EXAMPLE / 'SpatialFabricShowcase.uproject').read_text())
        plugins = {p['Name']: p['Enabled'] for p in descriptor['Plugins']}
        self.assertTrue(plugins['SpatialFabric']); self.assertTrue(plugins['OSC'])
        self.assertFalse(plugins['ModelContextProtocol'])
        self.assertFalse(plugins['AllToolsets'])
        self.assertEqual(descriptor['Modules'][0]['Name'], 'SpatialFabricShowcase')
        config = (EXAMPLE / 'Config/DefaultEngine.ini').read_text()
        self.assertIn('/Script/SpatialFabricShowcase.SpatialFabricShowcaseGameMode', config)
        self.assertNotIn('SecurityToken', config)
        self.assertNotIn('/Game/FirstPerson', config)
        self.assertIn('[/Script/SpatialFabric.SpatialFabricSettings]', (EXAMPLE / 'Config/DefaultSpatialFabric.ini').read_text())

    def test_prepare_is_self_contained_and_excludes_generated_and_recursive_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            project = prepare(Path(tmp) / 'Example')
            data = json.loads(project.read_text())
            self.assertNotIn('AdditionalPluginDirectories', data)
            plugin = project.parent / 'Plugins/SpatialFabric'
            self.assertTrue((plugin / 'SpatialFabric.uplugin').is_file())
            self.assertTrue((plugin / 'Source/Shared/SpatialFabricOSCReceiver.h').is_file())
            for folder in ('Examples', 'Presentations', 'Binaries', 'Saved', 'Intermediate', '.pi'):
                self.assertFalse((plugin / folder).exists(), folder)
            with self.assertRaises(ValueError): prepare(project.parent)
        with self.assertRaises(ValueError): prepare(ROOT / 'Examples/DoNotCreate')

    def test_current_documentation_links_resolve(self):
        paths = [ROOT / p for p in ('README.md', 'VALIDATION.md', 'FIXES.md')]
        paths += list((ROOT / 'Docs').glob('*.md'))
        paths += list((ROOT / 'Presentations').glob('*.md'))
        paths += list((ROOT / 'Examples').rglob('*.md'))
        for path in paths:
            for target in re.findall(r'\]\(([^)]+)\)', path.read_text(encoding='utf-8')):
                if '://' in target or target.startswith(('#', 'mailto:')): continue
                target = unquote(target.split('#', 1)[0])
                if not target: continue
                self.assertTrue((path.parent / target).exists(), f'{path.relative_to(ROOT)} -> {target}')

    def test_deck_timing_notes_and_delivered_documents(self):
        folder = ROOT / 'Presentations'
        spec = json.loads((folder / 'slides.json').read_text(encoding='utf-8'))
        self.assertEqual(sum(s['seconds'] for s in spec['slides'] if s['section'] == 'talk'), 900)
        self.assertEqual(sum(s['seconds'] for s in spec['slides'] if s['section'] == 'demo'), 600)
        self.assertEqual(len(spec['slides']), 16)
        with ZipFile(folder / 'SpatialFabric_From_Cue_to_Space.pptx') as archive:
            slides = [n for n in archive.namelist() if re.fullmatch(r'ppt/slides/slide\d+\.xml', n)]
            notes = [n for n in archive.namelist() if re.fullmatch(r'ppt/notesSlides/notesSlide\d+\.xml', n)]
            self.assertEqual(len(slides), 16); self.assertEqual(len(notes), 16)
            for name in notes: ET.fromstring(archive.read(name))
        self.assertTrue((folder / 'SpatialFabric_From_Cue_to_Space.pdf').read_bytes().startswith(b'%PDF'))
        self.assertTrue((folder / 'DEMO_RUNSHEET.md').is_file())
        self.assertTrue((folder / 'PRESENTER_GUIDE.md').is_file())


if __name__ == '__main__': unittest.main()
