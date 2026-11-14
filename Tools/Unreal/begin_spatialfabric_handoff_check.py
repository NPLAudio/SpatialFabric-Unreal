"""Enable editor playback OSC temporarily so native PIE handoff can be exercised."""
import json
from pathlib import Path
import unreal
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if "L_SpatialFabric_Showcase" not in world.get_path_name():
    raise RuntimeError("Open the showcase first")
subsystem = next(s for s in unreal.ObjectIterator(unreal.AudioLoomOscSubsystem) if s.get_outer() == world)
initial = subsystem.is_listening()
report = {"initial_listening": initial, "editor_start_succeeded": subsystem.start_listening()}
path = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / "SpatialFabricDemo/handoff_before.json"
path.write_text(json.dumps(report, indent=2), encoding="utf-8")
unreal.log("SpatialFabric handoff before: " + json.dumps(report))
