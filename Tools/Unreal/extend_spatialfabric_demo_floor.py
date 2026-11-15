"""Unreal Editor Python: add/update only the showcase safety floor and perimeter.
Preserves existing actors, player start, routing and the current camera. Saves this demo map only.
"""
import json
from pathlib import Path
import traceback
import unreal

report = {"status": "running", "pieces": [], "traces": []}
folder = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / "SpatialFabricDemo"
folder.mkdir(parents=True, exist_ok=True)
try:
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor.get_game_world(): raise RuntimeError("Stop PIE before changing collision geometry")
    world = editor.get_editor_world()
    if not world.get_path_name().startswith("/Game/SpatialFabricDemo/Maps/L_SpatialFabric_Showcase."):
        raise RuntimeError("Open the showcase first")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing = {a.get_actor_label(): a for a in actors.get_all_level_actors() if a.actor_has_tag("SF_DemoGenerated")}
    if 'Gallery foundation' in existing:
        raise RuntimeError('Room layout already has a collision floor and enclosing walls; use rearrange_spatialfabric_demo_rooms.py, not the legacy outdoor-floor extender')
    cube = unreal.load_asset("/Engine/BasicShapes/Cube")
    panel = unreal.load_asset("/Game/SpatialFabricDemo/Materials/M_SF_Panel")
    charcoal = unreal.load_asset("/Game/SpatialFabricDemo/Materials/M_SF_Charcoal")
    pieces = [("Safety Floor 120x120m", (0, 0, -50), (120, 120, 0.6), panel)]
    for edge in (-5980, 5980):
        pieces.extend([("Safety Wall X " + str(edge), (edge, 0, 155), (0.3, 120, 3.5), charcoal),
                       ("Safety Wall Y " + str(edge), (0, edge, 155), (120, 0.3, 3.5), charcoal)])
    with unreal.ScopedEditorTransaction("Add SpatialFabric safety floor"):
        for name, position, scale, material in pieces:
            actor = existing.get(name)
            if actor is None:
                actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*position))
                actor.set_actor_label(name)
                actor.set_editor_property("tags", ["SF_DemoGenerated"])
            actor.modify()
            actor.set_folder_path("SpatialFabric Demo/Environment")
            actor.set_actor_location(unreal.Vector(*position), False, False)
            actor.set_actor_scale3d(unreal.Vector(*scale))
            component = actor.static_mesh_component
            component.modify()
            component.set_static_mesh(cube)
            component.set_material(0, material)
            component.set_collision_profile_name("BlockAll")
            component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
            report["pieces"].append({"name": name, "collision": str(component.get_collision_enabled())})
    for x, y in [(-5000, -5000), (-3000, 0), (3000, 0), (0, 3000), (5000, 5000)]:
        hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 1000), unreal.Vector(x, y, -100),
            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
        if isinstance(hit, tuple): hit = next((h for h in hit if isinstance(h, unreal.HitResult)), None)
        info = hit.to_tuple() if hit else None
        if not info or not info[0]: raise RuntimeError("Safety floor collision trace missed at " + str((x, y)))
        report["traces"].append({"xy": [x, y], "blocking": bool(info[0]), "location": str(info[4])})
    for start, end in [((5900, 0, 100), (6050, 0, 100)), ((-5900, 0, 100), (-6050, 0, 100)),
                       ((0, 5900, 100), (0, 6050, 100)), ((0, -5900, 100), (0, -6050, 100))]:
        hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(*start), unreal.Vector(*end),
            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
        if isinstance(hit, tuple): hit = next((h for h in hit if isinstance(h, unreal.HitResult)), None)
        info = hit.to_tuple() if hit else None
        if not info or not info[0]: raise RuntimeError("Perimeter collision trace missed at " + str(start))
        report["traces"].append({"boundary_start": start, "blocking": bool(info[0])})
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
        raise RuntimeError("Could not save safety floor")
    report["status"] = "passed"
except Exception as error:
    report["status"] = "failed"
    report["error"] = str(error)
    report["traceback"] = traceback.format_exc()
    unreal.log_error(report["traceback"])
finally:
    (folder / "safety_floor.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log("SpatialFabric safety floor: " + json.dumps(report))
