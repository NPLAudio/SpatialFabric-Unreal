"""Unreal Editor Python: replace only the showcase audio/signage, not its geometry/routing.
Refuses PIE, unsaved edits and active playback. Does not start any sound.
"""
import importlib.util
import json
from pathlib import Path
import traceback
import unreal

ROOT = "/Game/SpatialFabricDemo"
folder = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / "SpatialFabricDemo"
folder.mkdir(parents=True, exist_ok=True)
report = {"status": "running", "sounds": [], "components": []}

try:
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor.get_game_world():
        raise RuntimeError("Stop PIE before refreshing audio")
    world = editor.get_editor_world()
    if not world.get_path_name().startswith(ROOT + "/Maps/L_SpatialFabric_Showcase."):
        raise RuntimeError("Open the SpatialFabric showcase first")
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() or unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():
        raise RuntimeError("Save existing edits before refreshing demo audio")
    components = [actor.get_component_by_class(unreal.AudioLoomComponent)
                  for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.AudioLoomActor)]
    if any(c.is_playing() for c in components):
        raise RuntimeError("Stop playback before refreshing demo audio")
    spec = importlib.util.spec_from_file_location("spatialfabric_demo_audio", Path(__file__).with_name("demo_audio.py"))
    audio = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audio)
    source = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / "SourceAssets/SpatialFabricDemo/Audio"
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    for kind, (name, channels, duration, description) in audio.SPECS.items():
        path = ROOT + "/Audio/" + name
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            raise RuntimeError("Missing demo sound asset: " + path)
        filename = source / (name + ".wav")
        metadata = audio.write_wav(filename, kind)
        task = unreal.AssetImportTask()
        task.filename = str(filename)
        task.destination_path = ROOT + "/Audio"
        task.automated = True
        task.replace_existing = True
        task.replace_existing_settings = False
        task.save = True
        tools.import_asset_tasks([task])
        if not task.get_editor_property("imported_object_paths"):
            raise RuntimeError("Import failed: " + name)
        sound = unreal.load_asset(path)
        sound.set_editor_property("loading_behavior", unreal.SoundWaveLoadingBehavior.FORCE_INLINE)
        if not unreal.EditorAssetLibrary.save_loaded_asset(sound):
            raise RuntimeError("Could not save " + path)
        metadata["asset"] = sound.get_path_name()
        report["sounds"].append(metadata)
    # Explicitly invalidate prepared PCM even for an import-overwrite rather than a Reimport action.
    # Final references, channels, loop/replay flags and OSC addresses remain unchanged.
    with unreal.ScopedEditorTransaction("Refresh SpatialFabric demo sounds"):
        for component in components:
            sound = component.get_editor_property("sound_wave")
            if not sound or not sound.get_path_name().startswith(ROOT + "/Audio/"):
                continue
            component.modify()
            component.set_editor_property("sound_wave", None)
            component.set_editor_property("sound_wave", sound)
            prepared = component.prepare_audio()
            report["components"].append({"actor": component.get_owner().get_actor_label(),
                "prepared": prepared, "channel": component.get_output_channel(), "osc": component.get_osc_address()})
            if not prepared:
                raise RuntimeError("Cannot prepare updated audio: " + component.get_last_playback_error())
        titles = {"Station title 0": "IMPACT / CH 1", "Station title 1": "STEREO / MATCHING CHANNELS", "Station title 2": "GLASS CUE / CH 2"}
        room_spec = importlib.util.spec_from_file_location('sf_audio_room_labels', Path(__file__).with_name('room_layout.py'))
        rooms = importlib.util.module_from_spec(room_spec); room_spec.loader.exec_module(rooms)
        titles.update({name: value for name, value, *_ in rooms.signs() if name in ('Mono guide', 'Stereo guide', 'Replay guide')})
        for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
            if actor.actor_has_tag("SF_DemoGenerated") and actor.get_actor_label() in titles:
                actor.modify()
                actor.get_component_by_class(unreal.TextRenderComponent).set_text(titles[actor.get_actor_label()])
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
        raise RuntimeError("Could not save updated station signage/references")
    report["status"] = "passed"
    report["audio_started"] = False
except Exception as error:
    report["status"] = "failed"
    report["error"] = str(error)
    report["traceback"] = traceback.format_exc()
    unreal.log_error(report["traceback"])
finally:
    (folder / "audio_refresh.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log("SpatialFabric audio refresh: " + json.dumps(report))
