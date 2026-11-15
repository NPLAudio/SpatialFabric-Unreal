"""Rearrange only the generated showcase into four rooms and a hallway.
Preserves bound actors/sequence and cue routing; refuses PIE, dirty edits or active audio.
Backs up the map before the first change. No audio is played or reimported.
"""
import importlib.util
import json
from pathlib import Path
import shutil
import time
import traceback
import unreal

_spec=importlib.util.spec_from_file_location('sf_room_layout',Path(__file__).with_name('room_layout.py'))
PLAN=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(PLAN)
ROOT='/Game/SpatialFabricDemo'
MAP=ROOT+'/Maps/L_SpatialFabric_Showcase'
TAG='SF_DemoGenerated'


def _state(component):
    keys=('device_id','output_channel','use_exclusive_mode','buffer_size_ms','loop','auto_replay','random_replay_delay','replay_delay_seconds','replay_delay_min','replay_delay_max','play_on_begin_play','osc_address')
    result={key:component.get_editor_property(key) for key in keys}
    sound=component.get_editor_property('sound_wave');result['sound']=sound.get_path_name() if sound else None
    result['effective_base']=component.get_osc_address()
    return result


def _trace(world,start,end):
    result=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(*start),unreal.Vector(*end),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,[],unreal.DrawDebugTrace.NONE,True)
    if isinstance(result,tuple):result=next((x for x in result if isinstance(x,unreal.HitResult)),None)
    info=result.to_tuple() if result else None
    return bool(info and info[0]),info


def _wall_material():
    path=ROOT+'/Materials/M_SF_RoomWall'
    existing=unreal.load_asset(path)
    if existing:return existing
    tools=unreal.AssetToolsHelpers.get_asset_tools()
    mat=tools.create_asset('M_SF_RoomWall',ROOT+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    edit=unreal.MaterialEditingLibrary
    base=edit.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector,-250,0)
    base.set_editor_property('constant',unreal.LinearColor(0.24,0.29,0.36,1))
    edit.connect_material_property(base,'',unreal.MaterialProperty.MP_BASE_COLOR)
    rough=edit.create_material_expression(mat,unreal.MaterialExpressionConstant,-250,150);rough.set_editor_property('r',0.85)
    edit.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(mat)
    return mat


def apply_layout(allow_generated_dirty=False,backup=True):
    editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    folder=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))/'SpatialFabricDemo'
    folder.mkdir(parents=True,exist_ok=True)
    report={'status':'running','layout':'four_rooms_v1','removed':[],'created':[],'floor_traces':[],'wall_traces':[],'door_traces':[]}
    try:
        if editor.get_game_world():raise RuntimeError('Stop PIE before rearranging the showcase')
        world=editor.get_editor_world()
        if not world or world.get_path_name().split('.')[0]!=MAP:raise RuntimeError('Open the showcase map first')
        dirty=list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
        if dirty and (not allow_generated_dirty or any(not p.get_name().startswith(ROOT+'/') for p in dirty)):
            raise RuntimeError('Unsaved edits; save or revert before rearranging: '+str([p.get_name() for p in dirty]))
        all_actors=actors.get_all_level_actors()
        generated=[a for a in all_actors if a.actor_has_tag(TAG)]
        report['before_actor_count']=len(generated)
        report['before_text_count']=sum(bool(a.get_component_by_class(unreal.TextRenderComponent)) for a in generated)
        by_name={a.get_actor_label():a for a in generated}
        required=['SF_Manager_ADM_and_Custom','SF_Stage_20x15x8m','SF_Autoplay_Motion']+['SF_Object_%02d'%(i+1) for i in range(4)]+['SF_Playback_%d'%(i+1) for i in range(3)]
        if any(name not in by_name for name in required):raise RuntimeError('Expected demo objects missing; not guessing a replacement')
        manager=by_name['SF_Manager_ADM_and_Custom'];stage=by_name['SF_Stage_20x15x8m']
        cues=[by_name['SF_Playback_%d'%(i+1)].get_component_by_class(unreal.AudioLoomComponent) for i in range(3)]
        if any(c.is_playing() for c in cues):raise RuntimeError('Stop active cue playback before rearranging')
        before_cues=[_state(c) for c in cues]
        before_bindings=[b.export_text() for b in manager.get_editor_property('object_bindings')]
        before_adapters={int(k):v.export_text() for k,v in manager.get_editor_property('adapter_configs').items()}
        before_sequence=by_name['SF_Autoplay_Motion'].get_sequence().get_path_name()
        report['previous_listener_mode']=str(stage.get_editor_property('listener_mode'))
        if backup:
            destination=folder/('BeforeRooms-'+str(time.time_ns()));destination.mkdir()
            content=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()))/'SpatialFabricDemo'
            for relative in ('Maps/L_SpatialFabric_Showcase.umap','Sequences/LS_SpatialFabric_Motion.uasset'):
                target=destination/Path(relative).name;shutil.copy2(content/relative,target)
            report['backup']=str(destination)
            (destination/'cue_config.json').write_text(json.dumps(before_cues,indent=2),encoding='utf-8')
        materials={
            'floor':unreal.load_asset(ROOT+'/Materials/M_SF_Panel'),
            'ceiling':unreal.load_asset(ROOT+'/Materials/M_SF_Charcoal'),
            'wall':_wall_material(),'grid':unreal.load_asset(ROOT+'/Materials/M_SF_Grid')}
        for i in range(4):materials['accent'+str(i)]=unreal.load_asset(ROOT+'/Materials/M_SF_Glow_'+str(i+1))
        cube=unreal.load_asset('/Engine/BasicShapes/Cube')
        if not cube or any(not m for m in materials.values()):raise RuntimeError('Required geometry/material assets unavailable')
        keep={by_name[n] for n in required}
        listener=stage.get_editor_property('listener_actor')
        if listener:keep.add(listener)
        starts=[a for a in generated if isinstance(a,unreal.PlayerStart)]
        if len(starts)>1:raise RuntimeError('Multiple generated PlayerStarts; choose one before rearranging')
        if starts:keep.add(starts[0])
        exposures=[a for a in generated if isinstance(a,unreal.PostProcessVolume)]
        if len(exposures)>1:raise RuntimeError('Multiple generated exposure volumes; refusing ambiguous changes')
        if exposures:keep.add(exposures[0])
        report['unowned_actors_preserved']=[a.get_actor_label() for a in all_actors if a not in generated]
        with unreal.ScopedEditorTransaction('SpatialFabric: four demo rooms and hallway'):
            for actor in generated:
                if actor not in keep:
                    report['removed'].append(actor.get_actor_label());actors.destroy_actor(actor)
            def spawn(cls,name,location,rotation=(0,0,0),section='Rooms'):
                actor=actors.spawn_actor_from_class(cls,unreal.Vector(*location),unreal.Rotator(pitch=rotation[0],yaw=rotation[1],roll=rotation[2]))
                if not actor:raise RuntimeError('Could not spawn '+name)
                actor.set_actor_label(name);actor.set_folder_path('SpatialFabric Demo/'+section)
                actor.set_editor_property('tags',[TAG,'SF_DemoRoomLayout']);report['created'].append(name)
                return actor
            for piece in PLAN.geometry():
                actor=spawn(unreal.StaticMeshActor,piece['name'],piece['location'])
                actor.set_actor_scale3d(unreal.Vector(*(v/100 for v in piece['size'])))
                component=actor.static_mesh_component;component.set_static_mesh(cube);component.set_material(0,materials[piece['material']])
                component.set_collision_profile_name('BlockAll' if piece['collision'] else 'NoCollision')
                component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS if piece['collision'] else unreal.CollisionEnabled.NO_COLLISION)
            for name,location,intensity,width,height,radius in PLAN.lights():
                light=spawn(unreal.RectLight,name,location,(-90,0,0),'Lighting')
                component=light.get_component_by_class(unreal.RectLightComponent);component.set_mobility(unreal.ComponentMobility.MOVABLE)
                for key,value in [('intensity_units',unreal.LightUnits.LUMENS),('intensity',float(intensity)),('source_width',float(width)),('source_height',float(height)),('attenuation_radius',float(radius))]:component.set_editor_property(key,value)
            for name,value,location,size,rotation in PLAN.signs():
                actor=spawn(unreal.TextRenderActor,name,location,rotation,'Room labels')
                component=actor.get_component_by_class(unreal.TextRenderComponent);component.set_text(value);component.set_world_size(size)
                component.set_text_render_color(unreal.Color(r=220,g=233,b=255,a=255));component.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
                component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            for i in range(4):
                # Tracked spheres are visual control objects, not moving physics obstacles.
                by_name['SF_Object_%02d'%(i+1)].static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            for i,(component,position) in enumerate(zip(cues,PLAN.CUE_POSITIONS)):
                component.get_owner().modify();component.get_owner().set_actor_location(unreal.Vector(*position),False,True)
                component.get_owner().set_folder_path('SpatialFabric Demo/Room '+str(i+2)+' Playback')
            start=starts[0] if starts else spawn(unreal.PlayerStart,'Visitor Start',PLAN.PLAYER_START,section='Navigation')
            start.modify();start.set_actor_location(unreal.Vector(*PLAN.PLAYER_START),False,True)
            start.set_actor_rotation(unreal.Rotator(pitch=0,yaw=PLAN.PLAYER_YAW,roll=0),True)
            # A fixed stage makes the spatial room readable even while the visitor is in the hall.
            # Follow remains available in the plugin, with the existing fallback rig preserved.
            stage.modify();stage.set_editor_property('listener_mode',unreal.ListenerMode.cast(0))
            manager.modify();manager.set_editor_property('hide_stage_in_pie',True);manager.apply_stage_visibility()
            if not exposures:
                post=spawn(unreal.PostProcessVolume,'Room exposure',(0,0,0),section='Lighting');post.set_editor_property('unbound',True)
                settings=post.get_editor_property('settings')
                for key,value in [('override_auto_exposure_method',True),('auto_exposure_method',unreal.AutoExposureMethod.AEM_MANUAL),('override_auto_exposure_apply_physical_camera_exposure',True),('auto_exposure_apply_physical_camera_exposure',False),('override_auto_exposure_bias',True),('auto_exposure_bias',-1.5),('override_motion_blur_amount',True),('motion_blur_amount',0.0)]:settings.set_editor_property(key,value)
                post.set_editor_property('settings',settings)
            after_cues=[_state(c) for c in cues]
            if after_cues!=before_cues:raise RuntimeError('Cue configuration changed unexpectedly')
            if [b.export_text() for b in manager.get_editor_property('object_bindings')]!=before_bindings or {int(k):v.export_text() for k,v in manager.get_editor_property('adapter_configs').items()}!=before_adapters:raise RuntimeError('Spatial routing changed unexpectedly')
            if by_name['SF_Autoplay_Motion'].get_sequence().get_path_name()!=before_sequence:raise RuntimeError('Motion sequence changed unexpectedly')
            for name,(x,y) in PLAN.floor_samples():
                blocking,info=_trace(world,(x,y,200),(x,y,-100))
                if not blocking or abs(info[4].z)>3:raise RuntimeError('Floor trace missed at '+name)
                report['floor_traces'].append(name)
            for name,start_pos,end_pos in PLAN.doorway_segments():
                blocking,_=_trace(world,start_pos,end_pos)
                if blocking:raise RuntimeError('Doorway blocked: '+name)
                report['door_traces'].append(name)
            for name,start_pos,end_pos in PLAN.wall_segments():
                blocking,_=_trace(world,start_pos,end_pos)
                if not blocking:raise RuntimeError('Wall/divider trace missed: '+name)
                report['wall_traces'].append(name)
        unreal.EditorAssetLibrary.save_loaded_asset(materials['wall'])
        loc,rot=PLAN.EDITOR_CAMERA
        editor.set_level_viewport_camera_info(unreal.Vector(*loc),unreal.Rotator(pitch=rot[0],yaw=rot[1],roll=rot[2]))
        levels.editor_set_game_view(True)
        if not levels.save_current_level():raise RuntimeError('Room layout map save failed')
        current=[a for a in actors.get_all_level_actors() if a.actor_has_tag(TAG)]
        report.update(status='passed',after_actor_count=len(current),after_text_count=sum(bool(a.get_component_by_class(unreal.TextRenderComponent)) for a in current),cue_config_preserved=True,spatial_routing_preserved=True,sequence_preserved=True,audio_started=False,listener_default='Off / stage-fixed',rooms=list(PLAN.ROOMS))
    except Exception as error:
        report.update(status='failed',error=str(error),traceback=traceback.format_exc());unreal.log_error(report['traceback'])
    (folder/'room_layout.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    if report['status']!='passed':raise RuntimeError(report['error'])
    unreal.log('SpatialFabric room layout: '+json.dumps(report))
    return report


if __name__=='__main__':apply_layout()
