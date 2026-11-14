"""Write current editor/PIE showcase diagnostics without changing project settings."""
import json
from pathlib import Path
import traceback
import unreal

folder = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / "SpatialFabricDemo"
folder.mkdir(parents=True, exist_ok=True)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_game_world() or editor.get_editor_world()
report = {"world": world.get_path_name(), "checks": {}}


def xyz(v):
    return [v.x, v.y, v.z]


try:
    managers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SpatialFabricManagerActor)
    stages = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SpatialStageVolume)
    players = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Pawn)
    report["manager_count"] = len(managers)
    report["stage_count"] = len(stages)
    report["pawns"] = [{"class": p.get_class().get_name(), "position": xyz(p.get_actor_location())} for p in players]
    report["checks"]["one_manager_one_stage"] = len(managers) == len(stages) == 1
    manager = managers[0]
    stage = stages[0]
    bindings = manager.get_editor_property("object_bindings")
    report["bindings"] = [{"label": b.label, "id": b.default_object_id, "target": b.target_actor.get_path_name() if b.target_actor else None,
        "targets": [{"adapter": str(t.adapter_type), "id_override": t.object_id_override} for t in b.targets]} for b in bindings]
    report["checks"]["four_dual_target_bindings"] = len(bindings) == 4 and all(len(b.targets) == 2 for b in bindings)
    report["stage_position"] = xyz(stage.get_actor_location())
    report["stage_half_extent"] = xyz(stage.get_editor_property("stage_box").get_scaled_box_extent())
    report["listener_mode"] = str(stage.get_editor_property("listener_mode"))
    report["spatial_server_listening"] = manager.get_editor_property("server_component").is_listening()
    report["recent_log"] = [str(entry) for entry in manager.get_recent_log(3)]
    report["checks"]["bounded_log_api"] = len(manager.get_recent_log(-10)) == 0 and len(manager.get_recent_log(3)) <= 3
    components = []
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.AudioLoomActor):
        component = actor.get_component_by_class(unreal.AudioLoomComponent)
        sound = component.get_editor_property("sound_wave")
        components.append({"actor": actor.get_actor_label(), "address": component.get_osc_address(), "output_channel": component.get_output_channel(),
            "loop": component.get_loop(), "auto_replay": component.get_editor_property("auto_replay"), "autoplay": component.get_editor_property("play_on_begin_play"),
            "prepared": component.prepare_audio(), "playing": component.is_playing(), "error": component.get_last_playback_error(),
            "sound": sound.get_path_name(), "loading_behavior": str(sound.get_editor_property("loading_behavior"))})
    report["playback"] = sorted(components, key=lambda c: c["actor"])
    report["checks"]["three_prepared_manual_stations"] = len(components) == 3 and all(c["prepared"] and not c["autoplay"] for c in components)
    report["checks"]["unique_explicit_osc_addresses"] = len({c["address"] for c in components}) == 3
    subsystem = next(s for s in unreal.ObjectIterator(unreal.AudioLoomOscSubsystem) if s.get_outer() == world)
    report["playback_osc_listening"] = subsystem.is_listening()
    report["checks"]["force_inline_audio"] = all("FORCE_INLINE" in c["loading_behavior"] for c in components)
    report["status"] = "passed" if all(report["checks"].values()) else "failed"
except Exception as error:
    report["status"] = "failed"
    report["error"] = str(error)
    report["traceback"] = traceback.format_exc()
    unreal.log_error(report["traceback"])
name = "inspection_pie.json" if editor.get_game_world() else "inspection_editor.json"
(folder / name).write_text(json.dumps(report, indent=2), encoding="utf-8")
unreal.log("SpatialFabric inspection: " + json.dumps(report))
