"""Rebuild the generated four-room showcase. Destructive only to tagged demo actors.
Requires a clean editor; backs up existing generated content before rebuilding.
No third-party content, fake speaker cabinets or decorative information billboards.
"""
import importlib.util
import json
import math
from pathlib import Path
import shutil
import time
import traceback
import unreal

ROOT='/Game/SpatialFabricDemo'
MAP=ROOT+'/Maps/L_SpatialFabric_Showcase'
TAG='SF_DemoGenerated'
FOLDER=Path(__file__).resolve().parent
SAVED=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))/'SpatialFabricDemo'
SAVED.mkdir(parents=True,exist_ok=True)
REPORT={'map':MAP,'steps':[],'actors':{},'assets':[]}
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ASSETS=unreal.AssetToolsHelpers.get_asset_tools()


def module(name,filename):
    spec=importlib.util.spec_from_file_location(name,FOLDER/filename)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result


AUDIO=module('sf_demo_audio','demo_audio.py')
ROOMS=module('sf_demo_rooms','room_layout.py')


def enum(name,value):
    return next(getattr(unreal,n) for n in dir(unreal) if n.lower()==name.lower()).cast(value)


def setp(obj,name,value):obj.set_editor_property(name,value)


def spawn(cls,name,location,folder):
    actor=ACTORS.spawn_actor_from_class(cls,unreal.Vector(*location),unreal.Rotator(pitch=0,yaw=0,roll=0))
    if not actor:raise RuntimeError('Cannot spawn '+name)
    actor.set_actor_label(name);actor.set_folder_path('SpatialFabric Demo/'+folder);actor.set_editor_property('tags',[TAG])
    REPORT['actors'][name]=actor.get_path_name();return actor


def material(name,color,emissive=0):
    path=ROOT+'/Materials/'+name
    mat=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else ASSETS.create_asset(name,ROOT+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    edit=unreal.MaterialEditingLibrary;edit.delete_all_material_expressions(mat)
    base=edit.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector,-320,0)
    base.set_editor_property('constant',unreal.LinearColor(*color,1));edit.connect_material_property(base,'',unreal.MaterialProperty.MP_BASE_COLOR)
    rough=edit.create_material_expression(mat,unreal.MaterialExpressionConstant,-320,180);rough.set_editor_property('r',0.65)
    edit.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    if emissive:
        glow=edit.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector,-320,-160)
        glow.set_editor_property('constant',unreal.LinearColor(*(c*emissive for c in color),1))
        edit.connect_material_property(glow,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.recompile_material(mat);REPORT['assets'].append(path);return mat


def audio_asset(name,kind):
    folder=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))/'SourceAssets/SpatialFabricDemo/Audio';folder.mkdir(parents=True,exist_ok=True)
    filename=folder/(name+'.wav');REPORT.setdefault('audio_designs',[]).append(AUDIO.write_wav(filename,kind))
    task=unreal.AssetImportTask();task.filename=str(filename);task.destination_path=ROOT+'/Audio';task.automated=True;task.replace_existing=True;task.save=True
    ASSETS.import_asset_tasks([task]);paths=task.get_editor_property('imported_object_paths')
    if not paths:raise RuntimeError('Audio import failed: '+str(filename))
    wave=unreal.load_asset(paths[0]);wave.set_editor_property('loading_behavior',unreal.SoundWaveLoadingBehavior.FORCE_INLINE)
    unreal.EditorAssetLibrary.save_loaded_asset(wave);REPORT['assets'].extend(paths);return wave


def build():
    editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor.get_game_world():raise RuntimeError('Stop PIE before rebuilding')
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() or unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():raise RuntimeError('Save/revert unsaved work first; builder refuses dirty packages')
    if not hasattr(unreal.AudioLoomComponent,'prepare_audio'):raise RuntimeError('Validated plugin binaries are not loaded')
    content=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()))/'SpatialFabricDemo'
    if content.exists():
        backup=SAVED/('BeforeRebuild-'+str(time.time_ns()));shutil.copytree(content,backup);REPORT['backup']=str(backup)
    for folder in ('Maps','Materials','Audio','Sequences'):unreal.EditorAssetLibrary.make_directory(ROOT+'/'+folder)
    current=editor.get_editor_world().get_path_name().split('.')[0]
    if current!=MAP:
        if unreal.EditorAssetLibrary.does_asset_exist(MAP):LEVELS.load_level(MAP)
        elif not LEVELS.new_level(MAP):raise RuntimeError('Cannot create showcase level')
    for actor in ACTORS.get_all_level_actors():
        if actor.actor_has_tag(TAG):ACTORS.destroy_actor(actor)
    material('M_SF_Charcoal',(0.028,0.045,0.075));material('M_SF_Panel',(0.075,0.10,0.15));material('M_SF_Grid',(0.04,0.14,0.23),.8)
    colors=[(0.025,.48,1),(.55,.04,1),(1,.25,.025),(.1,.8,.35)]
    glows=[material('M_SF_Glow_'+str(i+1),color,2.2) for i,color in enumerate(colors)]
    stage=spawn(unreal.SpatialStageVolume,'SF_Stage_20x15x8m',(0,0,400),'Spatial Control')
    stage.set_editor_property('listener_mode',enum('ListenerMode',0))
    listener=spawn(unreal.TargetPoint,'SF_Listener_Rig',(0,0,170),'Spatial Control');stage.set_editor_property('listener_actor',listener)
    manager=spawn(unreal.SpatialFabricManagerActor,'SF_Manager_ADM_and_Custom',(0,0,0),'Spatial Control')
    manager.set_editor_property('stage_volume',stage);manager.set_editor_property('hide_stage_in_pie',True)
    configs={}
    for index,port in ((0,50018),(1,9002)):
        config=unreal.SpatialAdapterConfig()
        for name,value in [('target_ip','127.0.0.1'),('target_port',port),('enabled',True),('send_rate_hz',30.0),('send_only_on_change',True)]:setp(config,name,value)
        if index==0:setp(config,'adm_coordinate_mode',enum('ADMCoordinateMode',2))
        else:setp(config,'custom_address_template','/demo/{zone}/{id}/xyz');setp(config,'custom_arg_template','x y z')
        configs[index]=config
    setp(manager,'adapter_configs',configs)
    bindings=[];movers=[];starts=[(700,0,320),(0,0,220),(0,550,400),(500,-350,150)]
    sphere=unreal.load_asset('/Engine/BasicShapes/Sphere')
    for i,(position,glow) in enumerate(zip(starts,glows)):
        actor=spawn(unreal.StaticMeshActor,'SF_Object_%02d'%(i+1),position,'Spatial Objects');actor.set_actor_scale3d(unreal.Vector(.7,.7,.7))
        component=actor.static_mesh_component;component.set_mobility(unreal.ComponentMobility.MOVABLE);component.set_static_mesh(sphere);component.set_material(0,glow);component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        binding=unreal.SpatialObjectBinding()
        for name,value in [('label','Object %02d'%(i+1)),('target_actor',actor),('cached_actor_label',actor.get_actor_label()),('default_object_id',i+1),('enabled',True),('gain_linear',.7),('width01',.15+i*.1),('adm_send_gain',True),('adm_send_mute',True),('adm_send_name',True),('adm_name_override','Demo Object %02d'%(i+1)),('adm_dref',.8),('adm_dmax',20.0)]:setp(binding,name,value)
        targets=[]
        for index in (0,1):
            target=unreal.SpatialAdapterTargetEntry();setp(target,'adapter_type',enum('SpatialAdapterType',index));setp(target,'object_id_override',-1 if index==0 else 101+i)
            if index==1:setp(target,'custom_fields',{'zone':'main-stage'})
            targets.append(target)
        setp(binding,'targets',targets);bindings.append(binding)
        if i<3:movers.append(actor)
    setp(manager,'object_bindings',bindings)
    sequence_path=ROOT+'/Sequences/LS_SpatialFabric_Motion'
    sequence=unreal.load_asset(sequence_path) if unreal.EditorAssetLibrary.does_asset_exist(sequence_path) else ASSETS.create_asset('LS_SpatialFabric_Motion',ROOT+'/Sequences',unreal.LevelSequence,unreal.LevelSequenceFactoryNew())
    for binding in sequence.get_bindings():binding.remove()
    sequence.set_display_rate(unreal.FrameRate(30,1));sequence.set_playback_start(0);sequence.set_playback_end(901)
    for index,actor in enumerate(movers):
        binding=sequence.add_possessable(actor);track=binding.add_track(unreal.MovieScene3DTransformTrack);section=track.add_section();section.set_range(0,901);channels=section.get_all_channels()
        for c in range(3,9):channels[c].set_default(.7 if c>=6 else 0.0)
        for frame in range(0,901,10):
            a=frame/900*2*math.pi
            position=[(700*math.cos(a),500*math.sin(a),320+160*math.sin(2*a)),(650*math.sin(a),450*math.sin(2*a),220),(0,550*math.cos(a),400+220*math.sin(a))][index]
            for c,value in enumerate(position):channels[c].add_key(unreal.FrameNumber(frame),float(value),interpolation=unreal.MovieSceneKeyInterpolation.LINEAR)
    unreal.EditorAssetLibrary.save_loaded_asset(sequence)
    player=spawn(unreal.LevelSequenceActor,'SF_Autoplay_Motion',(0,0,0),'Animation');player.set_sequence(sequence)
    settings=player.get_editor_property('playback_settings');settings.set_editor_property('auto_play',True);settings.loop_count=unreal.MovieSceneSequenceLoopCount(-1);player.set_editor_property('playback_settings',settings)
    sounds=[audio_asset('SW_SF_MonoCue','impact'),audio_asset('SW_SF_StereoLoop','groove'),audio_asset('SW_SF_ReplayCue','glass')]
    bases=['/demo/playback/mono','/demo/playback/stereo','/demo/playback/replay']
    for i,position in enumerate(ROOMS.CUE_POSITIONS):
        actor=spawn(unreal.AudioLoomActor,'SF_Playback_%d'%(i+1),position,'Playback');component=actor.get_component_by_class(unreal.AudioLoomComponent)
        for name,value in [('sound_wave',sounds[i]),('output_channel',[1,0,2][i]),('loop',i==1),('auto_replay',i==2),('replay_delay_seconds',2.5),('play_on_begin_play',False),('osc_address',bases[i])]:setp(component,name,value)
    if not unreal.EditorAssetLibrary.save_directory(ROOT,only_if_is_dirty=True,recursive=True) or not LEVELS.save_current_level():raise RuntimeError('Could not save generated actors/assets')
    rooms=module('sf_apply_rooms','rearrange_spatialfabric_demo_rooms.py').apply_layout(backup=False)
    REPORT.update(status='created',actor_count=rooms['after_actor_count'],asset_count=len(unreal.EditorAssetLibrary.list_assets(ROOT,recursive=True,include_folder=False)),audio_autoplay=False,layout='four_rooms_v1')
    REPORT['steps']=['four bound actors, preserved ADM/Custom identities','30-second looping motion sequence','three quiet manual-start cue rooms','four enclosed rooms, clear doors, continuous hall/floor','no decorative cabinets, floating object labels or billboard']


try:build()
except Exception as error:REPORT.update(status='failed',error=str(error),traceback=traceback.format_exc());unreal.log_error(REPORT['traceback'])
finally:(SAVED/'build_report.json').write_text(json.dumps(REPORT,indent=2),encoding='utf-8');unreal.log('SpatialFabric showcase: '+REPORT['status'])
