"""After Stop PIE, verify listener ownership returned to editor and restore initial state."""
import json
from pathlib import Path
import unreal
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
if editor.get_game_world():
    raise RuntimeError("Stop PIE first")
world = editor.get_editor_world()
folder = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / "SpatialFabricDemo"
before = json.loads((folder / "handoff_before.json").read_text(encoding="utf-8"))
subsystem = next(s for s in unreal.ObjectIterator(unreal.AudioLoomOscSubsystem) if s.get_outer() == world)
manager = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SpatialFabricManagerActor)[0]
stage = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SpatialStageVolume)[0]
report = {"checks": {"pie_to_editor_osc_handoff": subsystem.is_listening(),
    "editor_spatial_server_restored": manager.get_editor_property("server_component").is_listening(),
    "pie_listener_changes_not_saved_into_editor": stage.get_editor_property("listener_mode") == type(stage.get_editor_property("listener_mode")).cast(0)}}
for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.AudioLoomActor):
    actor.get_component_by_class(unreal.AudioLoomComponent).stop()
if not before["initial_listening"]:
    subsystem.stop_listening()
report["playback_osc_final_listening"] = subsystem.is_listening()
report["spatial_bind_ip"] = manager.get_editor_property("server_component").get_listen_ip()
report["checks"]["exact_loopback_reported"] = report["spatial_bind_ip"] == "127.0.0.1"
report["checks"]["initial_playback_osc_state_restored"] = report["playback_osc_final_listening"] == before["initial_listening"]
report["status"] = "passed" if all(report["checks"].values()) else "failed"
(folder / "handoff_after.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
unreal.log("SpatialFabric handoff after: " + json.dumps(report))
