"""Create a standalone source example with its own project-local SpatialFabric plugin.
python Tools/prepare_example.py --destination C:/dev/SpatialFabricShowcase
Refuses nonempty destinations and destinations inside this repository. No symlinks,
MCP configuration, editor caches, binaries or Epic template character assets are copied.
"""
import argparse
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
EXAMPLE = ROOT / "Examples/SpatialFabricShowcase"
PLUGIN_FOLDERS = ("Source", "Config", "Resources", "ThirdParty", "assets", "Tools", "Docs")
PLUGIN_FILES = ("SpatialFabric.uplugin", "README.md", "LICENSE", "FIXES.md", "VALIDATION.md")
IGNORE = shutil.ignore_patterns(".git", ".pi", ".claude", "Binaries", "Intermediate", "Saved", "DerivedDataCache", ".vs", "__pycache__", "*.pyc")


def prepare(destination, root=ROOT, example=EXAMPLE):
    destination = Path(destination).resolve()
    root, example = Path(root).resolve(), Path(example).resolve()
    if not (example / "SpatialFabricShowcase.uproject").is_file():
        raise FileNotFoundError("Example source missing: use the complete repository or the separate Example ZIP, not a plugin-only ZIP")
    if destination == root or root in destination.parents:
        raise ValueError("Choose a destination outside the source repository")
    if destination.exists() and any(destination.iterdir()):
        raise ValueError("Destination must be new or empty; existing work is never replaced")
    shutil.copytree(example, destination, dirs_exist_ok=True, ignore=IGNORE)
    project = destination / "SpatialFabricShowcase.uproject"
    descriptor = json.loads(project.read_text(encoding="utf-8"))
    descriptor.pop("AdditionalPluginDirectories", None)
    project.write_text(json.dumps(descriptor, indent=2) + "\n", encoding="utf-8")
    plugin = destination / "Plugins/SpatialFabric"
    plugin.mkdir(parents=True)
    for name in PLUGIN_FOLDERS:
        shutil.copytree(root / name, plugin / name, ignore=IGNORE)
    for name in PLUGIN_FILES:
        shutil.copy2(root / name, plugin / name)
    return project


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", required=True, type=Path)
    args = parser.parse_args()
    print("Prepared source example:", prepare(args.destination))
