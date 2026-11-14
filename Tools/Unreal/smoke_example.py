"""Unreal Editor Python: silent example smoke test, including a real walking-pawn fall/sweep.
For automated use in a disposable prepared example. Starts/stops PIE and quits the editor.
Never run in an editor with unsaved work. See Docs/Development.md.
"""
import json
from pathlib import Path
import time
import traceback
import unreal


class ExampleSmoke:
    def __init__(self):
        self.editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        self.levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        self.path = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / "example_smoke.json"
        if self.editor.get_game_world() or unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() or unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():
            raise RuntimeError("Smoke test requires a clean editor with no active PIE")
        self.report = {"status": "running", "checks": {}, "errors": [], "audio_played": False}
        self.started = time.monotonic()
        self.phase = 0
        self.levels.editor_request_begin_play()
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def check(self, name, condition, detail=None):
        self.report["checks"][name] = bool(condition)
        if detail is not None: self.report.setdefault("details", {})[name] = detail

    def finish(self):
        self.report["status"] = "passed" if all(self.report["checks"].values()) and not self.report["errors"] else "failed"
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self.report, indent=2), encoding="utf-8")
        unreal.unregister_slate_post_tick_callback(self.handle)
        self.levels.editor_request_end_play()
        unreal.SystemLibrary.quit_editor()

    def tick(self, delta):
        try:
            if time.monotonic() - self.started > 90: raise RuntimeError("Example PIE smoke test timed out")
            world = self.editor.get_game_world()
            if not world: return
            if self.phase == 0:
                self.pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
                if not self.pawn: return
                self.check("own_walking_character", self.pawn.get_class().get_name() == "SpatialFabricShowcaseCharacter", self.pawn.get_class().get_name())
                self.check("correct_map", "L_SpatialFabric_Showcase" in world.get_path_name())
                managers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SpatialFabricManagerActor)
                stages = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SpatialStageVolume)
                self.check("one_manager_and_stage", len(managers) == len(stages) == 1)
                self.check("four_object_bindings", len(managers[0].get_editor_property("object_bindings")) == 4)
                components = [a.get_component_by_class(unreal.AudioLoomComponent) for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.AudioLoomActor)]
                self.check("three_cues_decode", len(components) == 3 and all(c.prepare_audio() for c in components))
                self.check("no_audio_autoplay", not any(c.is_playing() for c in components))
                self.check("all_inline", all(c.get_editor_property("sound_wave").get_editor_property("loading_behavior") == unreal.SoundWaveLoadingBehavior.FORCE_INLINE for c in components))
                self.mover = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor) if a.get_actor_label() == "SF_Object_01")
                self.initial = self.mover.get_actor_location()
                self.pawn.set_actor_location(unreal.Vector(-4000, -4000, 300), False, True)
                self.wait = time.monotonic()
                self.phase = 1
            elif self.phase == 1 and time.monotonic() - self.wait > 0.25:
                pos = self.pawn.get_actor_location()
                falling = self.pawn.get_movement_component().is_falling()
                # Background/editor throttling decouples wall time from simulated fall time.
                # Wait for the physical condition, not an assumed two-second frame cadence.
                landed = 40 < pos.z < 100 and not falling
                # A newly spawned character may still report Walking before its first floor update.
                if not landed and time.monotonic() - self.wait < 20: return
                self.check("player_lands_on_outer_floor", landed, [pos.x, pos.y, pos.z])
                self.pawn.set_actor_location(unreal.Vector(6200, -4000, pos.z), True, False)
                pos = self.pawn.get_actor_location()
                self.check("perimeter_blocks_capsule", pos.x < 5980, [pos.x, pos.y, pos.z])
                self.check("sequence_animates", (self.mover.get_actor_location() - self.initial).length() > 5)
                self.finish()
        except Exception:
            self.report["errors"].append(traceback.format_exc())
            self.finish()


EXAMPLE_SMOKE = ExampleSmoke()
