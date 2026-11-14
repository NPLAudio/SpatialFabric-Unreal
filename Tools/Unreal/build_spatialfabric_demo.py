"""Build a self-contained SpatialFabric showcase in the open SpatialSandbox editor.
Run from Unreal's console using this file's absolute path (see Docs/SpatialSandboxDemo.md).
Writes only generated /Game/SpatialFabricDemo assets/tagged actors, project
SourceAssets/SpatialFabricDemo audio, and Saved/SpatialFabricDemo reports.
No third-party downloads: engine primitives + project template + locally generated audio.
"""
import importlib.util
import json
import math
from pathlib import Path
import traceback
import unreal

_audio_spec = importlib.util.spec_from_file_location("spatialfabric_demo_audio", Path(__file__).with_name("demo_audio.py"))
AUDIO = importlib.util.module_from_spec(_audio_spec)
_audio_spec.loader.exec_module(AUDIO)

ROOT = "/Game/SpatialFabricDemo"
MAP = ROOT + "/Maps/L_SpatialFabric_Showcase"
TAG = "SF_DemoGenerated"
SAVED = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / "SpatialFabricDemo"
SAVED.mkdir(parents=True, exist_ok=True)
REPORT = {"map": MAP, "steps": [], "actors": {}, "assets": []}
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ASSETS = unreal.AssetToolsHelpers.get_asset_tools()


def enum(name, value):
    cls = next(getattr(unreal, n) for n in dir(unreal) if n.lower() == name.lower())
    return cls.cast(value)


def setp(obj, name, value):
    variants = [name, "b_" + name, "b" + "".join(s.title() for s in name.split("_"))]
    last = None
    for candidate in variants:
        try:
            obj.set_editor_property(candidate, value)
            return
        except Exception as error:
            last = error
    raise RuntimeError("Cannot set %s.%s: %s" % (type(obj).__name__, name, last))


def spawn(cls, name, loc, scale=(1, 1, 1), rot=(0, 0, 0), folder="Environment"):
    actor = ACTORS.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(pitch=rot[0], yaw=rot[1], roll=rot[2]))
    if not actor:
        raise RuntimeError("Cannot spawn " + name)
    actor.set_actor_label(name)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    actor.set_folder_path("SpatialFabric Demo/" + folder)
    actor.set_editor_property("tags", [unreal.Name(TAG)])
    REPORT["actors"][name] = actor.get_path_name()
    return actor


def mesh(name, shape, loc, scale, material, rot=(0, 0, 0), collision=False, folder="Environment", movable=False):
    actor = spawn(unreal.StaticMeshActor, name, loc, scale, rot, folder)
    component = actor.static_mesh_component
    component.set_mobility(unreal.ComponentMobility.MOVABLE if movable else unreal.ComponentMobility.STATIC)
    component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/" + shape))
    component.set_material(0, material)
    if not collision:
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    return actor


def material(name, color, metallic=0.0, roughness=0.5, emissive=0.0):
    path = ROOT + "/Materials/" + name
    mat = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else ASSETS.create_asset(name, ROOT + "/Materials", unreal.Material, unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)
    base = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -320, 0)
    base.set_editor_property("constant", unreal.LinearColor(*color, 1))
    unreal.MaterialEditingLibrary.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for prop, value, y in [(unreal.MaterialProperty.MP_METALLIC, metallic, 170), (unreal.MaterialProperty.MP_ROUGHNESS, roughness, 280)]:
        node = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant, -320, y)
        node.set_editor_property("r", value)
        unreal.MaterialEditingLibrary.connect_material_property(node, "", prop)
    if emissive:
        glow = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -320, -160)
        glow.set_editor_property("constant", unreal.LinearColor(*(c * emissive for c in color), 1))
        unreal.MaterialEditingLibrary.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    REPORT["assets"].append(path)
    return mat


def text(name, value, loc, size=35, color=(220, 233, 255), rot=(0, 180, 0), folder="Signage"):
    actor = spawn(unreal.TextRenderActor, name, loc, rot=rot, folder=folder)
    comp = actor.get_component_by_class(unreal.TextRenderComponent)
    comp.set_text(value)
    comp.set_world_size(size)
    comp.set_text_render_color(unreal.Color(r=color[0], g=color[1], b=color[2], a=255))
    comp.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    return actor


def import_file(filename, destination):
    task = unreal.AssetImportTask()
    task.filename = str(filename)
    task.destination_path = destination
    task.automated = True
    task.replace_existing = True
    task.save = True
    ASSETS.import_asset_tasks([task])
    paths = task.get_editor_property("imported_object_paths")
    if not paths:
        raise RuntimeError("Import produced no asset: " + str(filename))
    REPORT["assets"].extend(paths)
    return unreal.load_asset(paths[0])


def audio_asset(name, kind):
    folder = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / "SourceAssets/SpatialFabricDemo/Audio"
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / (name + ".wav")
    REPORT.setdefault("audio_designs", []).append(AUDIO.write_wav(path, kind))
    asset = import_file(path, ROOT + "/Audio")
    setp(asset, "loading_behavior", unreal.SoundWaveLoadingBehavior.FORCE_INLINE)
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    return asset


def build():
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
        raise RuntimeError("Stop PIE before rebuilding the showcase")
    if not hasattr(unreal.AudioLoomComponent, "prepare_audio"):
        raise RuntimeError("Validated SpatialFabric 2 binary is not loaded; rebuild/restart before creating the demo.")
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    if any(not p.startswith(MAP) for p in dirty):
        raise RuntimeError("Unrelated map has unsaved edits; save it before running builder: " + str(dirty))
    for sub in ("Maps", "Materials", "Audio", "Textures", "Sequences"):
        unreal.EditorAssetLibrary.make_directory(ROOT + "/" + sub)
    current = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name().split(".")[0]
    if current != MAP:
        if unreal.EditorAssetLibrary.does_asset_exist(MAP):
            LEVELS.load_level(MAP)
        elif not LEVELS.new_level(MAP):
            raise RuntimeError("Cannot create showcase level")
    for actor in ACTORS.get_all_level_actors():
        if actor.actor_has_tag(unreal.Name(TAG)):
            ACTORS.destroy_actor(actor)
    REPORT["steps"].append("new isolated level")

    charcoal = material("M_SF_Charcoal", (0.028, 0.045, 0.075), 0.5, 0.35)
    panel = material("M_SF_Panel", (0.075, 0.10, 0.15), 0.6, 0.45)
    black = material("M_SF_Driver", (0.008, 0.012, 0.02), 0.25, 0.55)
    colors = [(0.025, 0.48, 1.0), (0.55, 0.04, 1.0), (1.0, 0.25, 0.025), (0.1, 0.8, 0.35)]
    glows = [material("M_SF_Glow_" + str(i + 1), c, 0.25, 0.3, 2.2) for i, c in enumerate(colors)]
    grid = material("M_SF_Grid", (0.04, 0.14, 0.23), 0.2, 0.5, 0.8)
    # A continuous foundation catches players stepping/jumping off the display deck.
    mesh("Safety Floor 120x120m", "Cube", (0, 0, -50), (120, 120, 0.6), panel, collision=True)
    for edge in (-5980, 5980):
        mesh("Safety Wall X " + str(edge), "Cube", (edge, 0, 155), (0.3, 120, 3.5), charcoal, collision=True)
        mesh("Safety Wall Y " + str(edge), "Cube", (0, edge, 155), (120, 0.3, 3.5), charcoal, collision=True)
    mesh("Main Stage 20x15m", "Cube", (0, 0, -15), (20, 15, 0.3), charcoal, collision=True)
    mesh("Welcome Deck", "Cube", (-1750, 0, -15), (15, 24, 0.3), panel, collision=True)
    mesh("Brand Backwall", "Cube", (1260, 0, 550), (0.3, 24, 11), charcoal, collision=True)
    for y in (-1200, 1200):
        mesh("Gallery rail " + str(y), "Cube", (-500, y, 40), (40, 0.2, 0.8), panel, collision=True)
    for x in range(-1000, 1001, 250):
        mesh("Grid X " + str(x), "Cube", (x, 0, 1), (0.015, 15, 0.02), grid)
    for y in range(-750, 751, 250):
        mesh("Grid Y " + str(y), "Cube", (0, y, 1), (20, 0.015, 0.02), grid)
    for y in (-750, 750):
        mesh("Stage boundary Y " + str(y), "Cube", (0, y, 4), (20, 0.045, 0.06), glows[0])
    for x in (-1000, 1000):
        mesh("Stage boundary X " + str(x), "Cube", (x, 0, 4), (0.045, 15, 0.06), glows[0])
    for y in (-1050, 1050):
        mesh("Walkway accent " + str(y), "Cube", (-1750, y, 3), (15, 0.04, 0.04), glows[1])
    text("Front axis", "+X  FRONT", (920, 0, 8), 48, rot=(90, 0, 0))
    text("Right axis", "+Y  HOUSE RIGHT", (0, 670, 8), 32, rot=(90, 0, 0))
    text("Left axis", "-Y  HOUSE LEFT", (0, -670, 8), 32, rot=(90, 0, 0))

    banner_path = Path(__file__).resolve().parents[2] / "assets/spatialfabric-unified.png"
    banner = import_file(banner_path, ROOT + "/Textures")
    banner_mat_path = ROOT + "/Materials/M_SF_Banner"
    banner_mat = unreal.load_asset(banner_mat_path) if unreal.EditorAssetLibrary.does_asset_exist(banner_mat_path) else ASSETS.create_asset("M_SF_Banner", ROOT + "/Materials", unreal.Material, unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(banner_mat)
    banner_mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    banner_mat.set_editor_property("two_sided", True)
    sample = unreal.MaterialEditingLibrary.create_material_expression(banner_mat, unreal.MaterialExpressionTextureSample, -300, 0)
    sample.set_editor_property("texture", banner)
    unreal.MaterialEditingLibrary.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(banner_mat)
    mesh("Unified Brand Display", "Plane", (1240, 0, 700), (16, 6.4, 1), banner_mat, rot=(0, 90, 90))
    text("Brand Caption", "SPATIAL CONTROL + DIRECT DEVICE PLAYBACK", (1225, 0, 290), 40)

    key = spawn(unreal.DirectionalLight, "Key Light", (0, 0, 1000), rot=(-48, -25, 0), folder="Lighting")
    key.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    key.light_component.set_editor_property("intensity", 2.0)
    for i, (loc, tint) in enumerate([((-1200, -700, 800), (0.35, 0.65, 1.0)), ((350, 700, 900), (0.7, 0.4, 1.0)), ((900, -300, 800), (1.0, 0.55, 0.2))]):
        light = spawn(unreal.PointLight, "Gallery Fill " + str(i), loc, folder="Lighting")
        comp = light.light_component
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        comp.set_editor_property("intensity", 2500.0)
        comp.set_editor_property("attenuation_radius", 2000.0)
        comp.set_editor_property("light_color", unreal.Color(r=int(tint[0] * 255), g=int(tint[1] * 255), b=int(tint[2] * 255), a=255))
    post = spawn(unreal.PostProcessVolume, "Gallery Exposure", (0, 0, 0), folder="Lighting")
    setp(post, "unbound", True)
    settings = post.get_editor_property("settings")
    for name, value in [("override_auto_exposure_method", True), ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL), ("override_auto_exposure_apply_physical_camera_exposure", True), ("auto_exposure_apply_physical_camera_exposure", False), ("override_auto_exposure_bias", True), ("auto_exposure_bias", -1.5), ("override_motion_blur_amount", True), ("motion_blur_amount", 0.0), ("override_bloom_intensity", True), ("bloom_intensity", 0.35)]:
        setp(settings, name, value)
    post.set_editor_property("settings", settings)
    spawn(unreal.PlayerStart, "Visitor Start", (-2150, 0, 110), folder="Navigation")
    REPORT["steps"].append("gallery geometry, materials, lighting, branded display")

    stage = spawn(unreal.SpatialStageVolume, "SF_Stage_20x15x8m", (0, 0, 400), folder="Spatial Control")
    manager = spawn(unreal.SpatialFabricManagerActor, "SF_Manager_ADM_and_Custom", (0, 0, 0), folder="Spatial Control")
    setp(manager, "stage_volume", stage)
    setp(manager, "hide_stage_in_pie", True)
    listener = spawn(unreal.TargetPoint, "SF_Listener_Rig", (0, 0, 170), folder="Spatial Control")
    setp(stage, "listener_actor", listener)
    mesh("Listener marker", "Cylinder", (0, 0, 10), (1.1, 1.1, 0.1), glows[3])
    text("Listener label", "LISTENER\nFollow mode tracks player in PIE", (-80, 0, 160), 23, color=(85, 255, 160))
    configs = {}
    for index, port in [(0, 50018), (1, 9002)]:
        config = unreal.SpatialAdapterConfig()
        for name, value in [("target_ip", "127.0.0.1"), ("target_port", port), ("enabled", True), ("send_rate_hz", 30.0), ("send_only_on_change", True)]: setp(config, name, value)
        if index == 0:
            setp(config, "adm_coordinate_mode", enum("ADMCoordinateMode", 2))
        else:
            setp(config, "custom_address_template", "/demo/{zone}/{id}/xyz")
            setp(config, "custom_arg_template", "x y z")
        configs[index] = config
    setp(manager, "adapter_configs", configs)

    bindings = []
    movers = []
    starts = [(700, 0, 320), (0, 0, 220), (0, 550, 400), (500, -350, 150)]
    for i, (pos, glow) in enumerate(zip(starts, glows)):
        actor = mesh("SF_Object_%02d" % (i + 1), "Sphere", pos, (0.7, 0.7, 0.7), glow, folder="Spatial Objects", movable=True)
        label = text("Object %d label" % (i + 1), "OBJ %02d" % (i + 1), (pos[0], pos[1], pos[2] + 80), 29, folder="Spatial Objects")
        label.attach_to_actor(actor, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
        binding = unreal.SpatialObjectBinding()
        for name, value in [("label", "Object %02d" % (i + 1)), ("target_actor", actor), ("cached_actor_label", actor.get_actor_label()), ("default_object_id", i + 1), ("enabled", True), ("gain_linear", 0.7), ("width01", 0.15 + i * 0.1), ("adm_send_gain", True), ("adm_send_mute", True), ("adm_send_name", True), ("adm_name_override", "Demo Object %02d" % (i + 1)), ("adm_dref", 0.8), ("adm_dmax", 20.0)]: setp(binding, name, value)
        targets = []
        for index in (0, 1):
            target = unreal.SpatialAdapterTargetEntry()
            setp(target, "adapter_type", enum("SpatialAdapterType", index))
            setp(target, "object_id_override", -1 if index == 0 else 101 + i)
            if index == 1: setp(target, "custom_fields", {"zone": "main-stage"})
            targets.append(target)
        setp(binding, "targets", targets)
        bindings.append(binding)
        if i < 3: movers.append(actor)
    setp(manager, "object_bindings", bindings)
    REPORT["steps"].append("four bound objects, simultaneous ADM/Custom routes, listener rig")

    sequence_path = ROOT + "/Sequences/LS_SpatialFabric_Motion"
    sequence = unreal.load_asset(sequence_path) if unreal.EditorAssetLibrary.does_asset_exist(sequence_path) else ASSETS.create_asset("LS_SpatialFabric_Motion", ROOT + "/Sequences", unreal.LevelSequence, unreal.LevelSequenceFactoryNew())
    for binding in sequence.get_bindings(): binding.remove()
    sequence.set_display_rate(unreal.FrameRate(30, 1))
    sequence.set_playback_start(0); sequence.set_playback_end(901)
    for index, actor in enumerate(movers):
        binding = sequence.add_possessable(actor)
        track = binding.add_track(unreal.MovieScene3DTransformTrack)
        section = track.add_section(); section.set_range(0, 901)
        channels = section.get_all_channels()
        for c in range(3, 9): channels[c].set_default(0.7 if c >= 6 else 0.0)
        for frame in range(0, 901, 10):
            a = frame / 900 * 2 * math.pi
            position = [(700 * math.cos(a), 500 * math.sin(a), 320 + 160 * math.sin(2 * a)), (650 * math.sin(a), 450 * math.sin(2 * a), 220), (0, 550 * math.cos(a), 400 + 220 * math.sin(a))][index]
            for c, value in enumerate(position): channels[c].add_key(unreal.FrameNumber(frame), float(value), interpolation=unreal.MovieSceneKeyInterpolation.LINEAR)
    unreal.EditorAssetLibrary.save_loaded_asset(sequence)
    playback = spawn(unreal.LevelSequenceActor, "SF_Autoplay_Motion", (0, 0, 0), folder="Animation")
    playback.set_sequence(sequence)
    settings = playback.get_editor_property("playback_settings")
    setp(settings, "auto_play", True)
    settings.loop_count = unreal.MovieSceneSequenceLoopCount(-1)
    playback.set_editor_property("playback_settings", settings)
    REPORT["steps"].append("30-second looping motion sequence (autoplay in PIE)")

    sounds = [audio_asset("SW_SF_MonoCue", "impact"), audio_asset("SW_SF_StereoLoop", "groove"), audio_asset("SW_SF_ReplayCue", "glass")]
    names = ["IMPACT / CH 1", "STEREO GROOVE / ALL", "GLASS CUE / CH 2"]
    osc = ["/demo/playback/mono", "/demo/playback/stereo", "/demo/playback/replay"]
    for i, y in enumerate((-650, 0, 650)):
        x = -1640
        mesh("Playback plinth %d" % i, "Cylinder", (x, y, 16), (2.5, 2.5, 0.3), glows[i], folder="Playback Stations")
        mesh("Speaker cabinet %d" % i, "Cube", (x, y, 100), (0.7, 0.75, 1.45), charcoal, folder="Playback Stations")
        for z, size in [(70, 0.44), (135, 0.25)]:
            mesh("Speaker rim %d %d" % (i, z), "Cylinder", (x - 38, y, z), (size + 0.07, size + 0.07, 0.03), glows[i], rot=(90, 0, 0), folder="Playback Stations")
            mesh("Speaker driver %d %d" % (i, z), "Cylinder", (x - 41, y, z), (size, size, 0.04), black, rot=(90, 0, 0), folder="Playback Stations")
        actor = spawn(unreal.AudioLoomActor, "SF_Playback_%d" % (i + 1), (x, y, 120), folder="Playback Stations")
        component = actor.get_component_by_class(unreal.AudioLoomComponent)
        for name, value in [("sound_wave", sounds[i]), ("output_channel", [1, 0, 2][i]), ("loop", i == 1), ("auto_replay", i == 2), ("replay_delay_seconds", 2.5), ("play_on_begin_play", False), ("osc_address", osc[i])]: setp(component, name, value)
        text("Station title %d" % i, names[i], (x - 48, y, 245), 30, color=tuple(int(c * 255) for c in colors[i]))
        text("Station OSC %d" % i, osc[i] + "/play", (x - 48, y, 205), 19)
    text("Welcome instructions", "SPATIALFABRIC SHOWCASE\nWASD + MOUSE in Play\nOpen Window > Spatial Fabric\nRadar / Output: animated objects\nPlayback: Play a station to hear audio\nAudio is OFF until requested", (-2280, 0, 340), 31, rot=(0, 0, 0))
    mesh("OSC information panel", "Cube", (-990, 0, 670), (0.12, 8.2, 2.5), charcoal, folder="Signage")
    mesh("OSC panel accent", "Cube", (-1000, 0, 794), (0.04, 8.2, 0.04), glows[0], folder="Signage")
    for y in (-400, 400):
        mesh("OSC panel support " + str(y), "Cylinder", (-990, y, 300), (0.08, 0.08, 6), panel, folder="Signage")
    text("Spatial instructions", "ADM :50018  +  CUSTOM :9002\nFour object IDs / per-target overrides\nStage: toggle Follow for player-relative output\nPlayback OSC :9000 (Start OSC in panel)", (-1000, 0, 700), 31)
    REPORT["steps"].append("three quiet manual-start playback stations with explicit OSC paths")

    # Save only our namespace. The existing FirstPerson map and project defaults remain untouched.
    if not unreal.EditorAssetLibrary.save_directory(ROOT, only_if_is_dirty=True, recursive=True):
        raise RuntimeError("Could not save generated showcase assets")
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(unreal.Vector(-3700, -3100, 2600), unreal.Rotator(pitch=-27, yaw=44, roll=0))
    LEVELS.editor_set_game_view(True)
    if not LEVELS.save_current_level():
        raise RuntimeError("Could not save showcase map")
    REPORT["status"] = "created"
    REPORT["asset_count"] = len(unreal.EditorAssetLibrary.list_assets(ROOT, recursive=True, include_folder=False))
    REPORT["actor_count"] = len([a for a in ACTORS.get_all_level_actors() if a.actor_has_tag(unreal.Name(TAG))])
    REPORT["audio_autoplay"] = False

try:
    build()
except Exception as error:
    REPORT["status"] = "failed"
    REPORT["error"] = str(error)
    REPORT["traceback"] = traceback.format_exc()
    unreal.log_error(REPORT["traceback"])
finally:
    (SAVED / "build_report.json").write_text(json.dumps(REPORT, indent=2), encoding="utf-8")
    unreal.log("SpatialFabric showcase: " + json.dumps({k: v for k, v in REPORT.items() if k != "actors"}))
