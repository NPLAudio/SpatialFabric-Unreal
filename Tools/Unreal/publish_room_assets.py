"""Commandlet-only sanitizer for a disposable publication project, then silent PIE smoke.
Never run in the user's open sandbox. publish_room_example.py creates the marker.
"""
import json
from pathlib import Path
import traceback
import unreal

project=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
source_root=Path(__file__).resolve().parents[2]
marker=project/'Saved/room_publish_request.json'
try:
    if not marker.is_file() or source_root==project or source_root in project.parents:raise RuntimeError('Only a marked, disposable project outside the repository may be sanitized')
    request=json.loads(marker.read_text(encoding='utf-8'))
    if request.get('mode')!='four_rooms_v1':raise RuntimeError('Invalid publication marker')
    editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor.get_game_world():raise RuntimeError('Publication cleanup must run outside PIE')
    world=editor.get_editor_world();map_path='/Game/SpatialFabricDemo/Maps/L_SpatialFabric_Showcase'
    if not world or world.get_path_name().split('.')[0]!=map_path:raise RuntimeError('Unexpected map')
    actor_sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    all_actors=actor_sub.get_all_level_actors()
    bound=set()
    for manager in all_actors:
        if isinstance(manager,unreal.SpatialFabricManagerActor):
            bound.update(b.get_editor_property('target_actor') for b in manager.get_editor_property('object_bindings'))
    removed=[]
    for actor in all_actors:
        if actor.actor_has_tag('SF_DemoGenerated'):continue
        mesh=actor.get_component_by_class(unreal.StaticMeshComponent)
        safe=(type(actor)==unreal.StaticMeshActor and actor not in bound and mesh and mesh.get_editor_property('static_mesh').get_path_name()=='/Engine/BasicShapes/Plane.Plane' and actor.get_actor_location().x < -1800)
        if not safe:raise RuntimeError('Unowned non-demo content must be reviewed before publication: '+actor.get_actor_label())
        removed.append(actor.get_actor_label());actor_sub.destroy_actor(actor)
    unreal.SystemLibrary.collect_garbage()
    editor.set_level_viewport_camera_info(unreal.Vector(-1580,-650,190),unreal.Rotator(pitch=-3,yaw=55,roll=0))
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():raise RuntimeError('Clean publication map save failed')
    registry=unreal.AssetRegistryHelpers.get_asset_registry()
    filename=project/'Content/SpatialFabricDemo/Maps/L_SpatialFabric_Showcase.umap'
    registry.scan_modified_asset_files([str(filename)])
    options=unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,include_soft_package_references=True)
    queue=[map_path];seen=set()
    while queue:
        package=str(queue.pop()).split('.')[0]
        if package in seen:continue
        seen.add(package)
        if package.startswith('/Game/'):queue.extend(str(p) for p in registry.get_dependencies(package,options))
    project_deps=sorted(p for p in seen if p.startswith('/Game/'))
    if any(not p.startswith('/Game/SpatialFabricDemo/') for p in project_deps):raise RuntimeError('External project content dependency found: '+str(project_deps))
    generated=[a for a in actor_sub.get_all_level_actors() if a.actor_has_tag('SF_DemoGenerated')]
    report={'status':'prepared','layout':'four_rooms_v1','removed_from_disposable_copy_only':removed,'actor_count':len(generated),'text_count':sum(bool(a.get_component_by_class(unreal.TextRenderComponent)) for a in generated),'project_package_dependencies':project_deps,'engine_and_script_dependencies':sorted(seen-set(project_deps))}
    (project/'Saved/room_publish_inventory.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    script=Path(__file__).with_name('smoke_example.py')
    exec(compile(script.read_text(encoding='utf-8'),str(script),'exec'),{'__name__':'__main__'})
except Exception:
    (project/'Saved/room_publish_error.txt').write_text(traceback.format_exc(),encoding='utf-8')
    unreal.log_error(traceback.format_exc());unreal.SystemLibrary.quit_editor()
