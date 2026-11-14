"""Asynchronous ~20-second live PIE validation (allows background-editor throttling). Quiet demo sounds will play.
Start the showcase in PIE first, then run this file from Unreal's console.
Tests software/device-open behavior, not physical channel mapping or audible fidelity.
"""
import json
from pathlib import Path
import socket
import struct
import time
import traceback
import unreal


class DemoValidation:
    def __init__(self):
        self.editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        self.world = self.editor.get_game_world()
        if not self.world or "L_SpatialFabric_Showcase" not in self.world.get_path_name():
            raise RuntimeError("Start PIE in the showcase before validation")
        self.components = {}
        for actor in unreal.GameplayStatics.get_all_actors_of_class(self.world, unreal.AudioLoomActor):
            self.components[actor.get_actor_label()] = actor.get_component_by_class(unreal.AudioLoomComponent)
        self.mono, self.stereo, self.replay = [self.components["SF_Playback_" + str(i)] for i in (1, 2, 3)]
        self.stage = unreal.GameplayStatics.get_all_actors_of_class(self.world, unreal.SpatialStageVolume)[0]
        self.original_stage = self.xyz(self.stage.get_actor_location())
        self.movers = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(self.world, unreal.StaticMeshActor) if a.get_actor_label() in ["SF_Object_01", "SF_Object_02", "SF_Object_03"]}
        self.initial_positions = {n: self.xyz(a.get_actor_location()) for n, a in self.movers.items()}
        self.changed = set()
        self.report = {"status": "running", "world": self.world.get_path_name(), "checks": {}, "details": {}, "errors": [], "physical_audio_verified": False}
        folder = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / "SpatialFabricDemo"
        self.path = folder / "runtime_validation.json"
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.replay_edges = 0
        self.replay_previous = False
        self.replay_edges_at_stop = None
        self.started = time.monotonic()
        self.events = [
            (0.0, "preflight", self.preflight),
            (0.05, "ignored triggers", self.ignored_triggers),
            (0.35, "reject false/string play", lambda: self.check("typed_play_ignores_false_and_string", not self.mono.is_playing())),
            (0.4, "OSC mono play", lambda: self.send(self.mono, "play", "f", 1.0)),
            (1.1, "mono running", self.mono_running),
            (2.6, "mono natural finish", lambda: self.check("mono_natural_eof", self.report["checks"].get("wasapi_device_open_and_play", False) and not self.mono.is_playing() and not self.mono.get_last_playback_error())),
            (2.8, "address-only stereo play", lambda: self.send(self.stereo, "play")),
            (3.5, "stereo running", self.stereo_running),
            (3.6, "live loop off", lambda: self.send(self.stereo, "loop", "F")),
            (4.2, "live loop applied", lambda: self.check("osc_bool_live_loop_off", not self.stereo.get_loop())),
            (8.0, "stereo drained", lambda: self.check("stereo_drains_after_loop_off", self.report["checks"].get("address_only_play_and_loop", False) and not self.stereo.is_playing() and not self.stereo.get_last_playback_error())),
            (8.3, "replay play", lambda: self.send(self.replay, "play", "i", 1)),
            (9.0, "replay running", lambda: self.check("integer_play_trigger", self.replay_edges >= 1)),
            (9.1, "listener follow", self.follow_listener),
            (9.8, "replay gap", lambda: self.check("replay_waits_between_cues", self.replay_edges >= 1 and not self.replay.is_playing())),
            (10.7, "listener basis", self.check_listener),
            (13.7, "cancel replay", self.stop_replay),
            (14.3, "replay stopped", lambda: self.check("osc_stop_applies", not self.replay.is_playing())),
            (14.5, "explicit missing device", self.missing_device),
            (15.2, "missing device fails closed", self.check_missing_device),
            (15.4, "reset device", self.reset_device),
            (16.0, "pause world", lambda: self.set_pause(True)),
            (16.2, "paused loop on", lambda: self.send(self.stereo, "loop", "f", 1.0)),
            (16.3, "paused play", lambda: self.send(self.stereo, "play")),
            (16.9, "paused OSC play applies", self.check_paused_play),
            (17.0, "paused OSC stop", lambda: self.send(self.stereo, "stop")),
            (17.6, "paused OSC stop applies", lambda: self.check("osc_stop_while_world_paused", unreal.GameplayStatics.is_game_paused(self.world) and not self.stereo.is_playing())),
            (17.8, "unpause world", lambda: self.set_pause(False)),
            (18.7, "finish", self.finish),
        ]
        self.flush()
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    @staticmethod
    def xyz(v): return [v.x, v.y, v.z]

    def flush(self): self.path.write_text(json.dumps(self.report, indent=2), encoding="utf-8")

    def check(self, name, condition, detail=None):
        self.report["checks"][name] = bool(condition)
        if detail is not None: self.report["details"][name] = detail
        self.flush()

    @staticmethod
    def string(value):
        data = value.encode("utf-8") + b"\0"
        return data + b"\0" * (-len(data) % 4)

    def send(self, component, operation, tag="", value=None):
        data = self.string(component.get_osc_address() + "/" + operation) + self.string("," + tag)
        if tag == "f": data += struct.pack(">f", value)
        elif tag == "i": data += struct.pack(">i", value)
        elif tag == "s": data += self.string(value)
        self.socket.sendto(data, ("127.0.0.1", 9000))

    def preflight(self):
        sub = next(s for s in unreal.ObjectIterator(unreal.AudioLoomOscSubsystem) if s.get_outer() == self.world)
        editor_sub = next(s for s in unreal.ObjectIterator(unreal.AudioLoomOscSubsystem) if "L_SpatialFabric_Showcase" in s.get_outer().get_path_name() and "UEDPIE_" not in s.get_outer().get_path_name())
        self.check("editor_to_pie_osc_handoff", sub.is_listening() and not editor_sub.is_listening())
        self.check("prepared_assets", all(c.prepare_audio() for c in self.components.values()))
        self.check("no_audio_autoplay", not any(c.is_playing() for c in self.components.values()))
        # Repeatable even after a previous validation toggled loop or attempted a missing device.
        for component in self.components.values(): component.stop()
        self.mono.set_device_id("")
        self.stereo.set_loop(True)
        self.replay.set_loop(False)

    def ignored_triggers(self):
        self.send(self.mono, "play", "s", "not-a-play-trigger")
        self.send(self.mono, "play", "f", 0.0)
        self.send(self.mono, "play", "F")

    def mono_running(self):
        self.check("wasapi_device_open_and_play", self.mono.is_playing() and not self.mono.get_last_playback_error(), {
            "error": self.mono.get_last_playback_error(), "estimated_output_latency_ms": self.mono.get_output_latency_ms()})

    def stereo_running(self):
        detail = {"playing": self.stereo.is_playing(), "loop": self.stereo.get_loop(), "error": self.stereo.get_last_playback_error()}
        self.check("address_only_play_and_loop", detail["playing"] and detail["loop"] and not detail["error"], detail)

    def follow_listener(self):
        mode = self.stage.get_editor_property("listener_mode")
        self.stage.set_editor_property("listener_mode", type(mode).cast(1))
        pawn = unreal.GameplayStatics.get_player_pawn(self.world, 0)
        pawn.set_actor_location(unreal.Vector(200, 100, 110), False, False)
        controller = unreal.GameplayStatics.get_player_controller(self.world, 0)
        controller.set_control_rotation(unreal.Rotator(pitch=-5, yaw=35, roll=0))

    def check_listener(self):
        controller = unreal.GameplayStatics.get_player_controller(self.world, 0)
        listener = controller.get_view_target()
        value = self.xyz(self.stage.world_to_normalized(listener.get_actor_location()))
        self.check("listener_view_target_is_virtual_origin", max(abs(v) for v in value) < 0.02, value)
        self.check("listener_does_not_move_authored_stage", self.xyz(self.stage.get_actor_location()) == self.original_stage)

    def stop_replay(self):
        self.check("natural_completion_auto_replays", self.replay_edges >= 2, self.replay_edges)
        self.send(self.replay, "stop")
        self.replay_edges_at_stop = self.replay_edges

    def missing_device(self):
        self.mono.set_device_id("SpatialFabricDemo-INTENTIONALLY-MISSING-DEVICE")
        self.mono.play()

    def check_missing_device(self):
        error = self.mono.get_last_playback_error()
        self.check("explicit_missing_device_fails_closed", not self.mono.is_playing() and bool(error), error)

    def reset_device(self):
        self.mono.stop()
        self.mono.set_device_id("")

    def check_paused_play(self):
        detail = {"paused": unreal.GameplayStatics.is_game_paused(self.world), "playing": self.stereo.is_playing(), "loop": self.stereo.get_loop(), "error": self.stereo.get_last_playback_error()}
        self.check("osc_play_while_world_paused", detail["paused"] and detail["playing"] and detail["loop"] and not detail["error"], detail)

    def set_pause(self, paused):
        unreal.GameplayStatics.set_game_paused(self.world, paused)
        self.check("game_paused_for_osc_check" if paused else "game_unpaused_after_osc_check", unreal.GameplayStatics.is_game_paused(self.world) == paused)

    def finish(self):
        unreal.GameplayStatics.set_game_paused(self.world, False)
        self.check("stop_cancels_pending_replay", not self.replay.is_playing() and self.replay_edges == self.replay_edges_at_stop)
        self.check("all_three_objects_animate", len(self.changed) == 3, sorted(self.changed))
        self.check("authored_stage_still_fixed", self.xyz(self.stage.get_actor_location()) == self.original_stage)
        for component in self.components.values(): component.stop()
        self.check("all_streams_stopped", not any(c.is_playing() for c in self.components.values()))
        self.report["status"] = "passed" if all(self.report["checks"].values()) and not self.report["errors"] else "failed"
        self.report["seconds"] = round(time.monotonic() - self.started, 2)
        self.flush()
        self.socket.close()
        unreal.unregister_slate_post_tick_callback(self.handle)
        unreal.log("SpatialFabric live validation: " + json.dumps(self.report))

    def tick(self, delta):
        try:
            if self.editor.get_game_world() != self.world:
                raise RuntimeError("PIE ended during validation")
            playing = self.replay.is_playing()
            if playing and not self.replay_previous: self.replay_edges += 1
            self.replay_previous = playing
            for name, actor in self.movers.items():
                if any(abs(a - b) > 5 for a, b in zip(self.xyz(actor.get_actor_location()), self.initial_positions[name])): self.changed.add(name)
            elapsed = time.monotonic() - self.started
            # One scheduled action per frame: background throttling must not collapse
            # a UDP send and its subsequent assertion into the same engine frame.
            if self.events and elapsed >= self.events[0][0]:
                _, name, action = self.events.pop(0)
                self.report.setdefault("event_times", []).append({"step": name, "seconds": round(elapsed, 3)})
                try: action()
                except Exception:
                    self.report["errors"].append({"step": name, "traceback": traceback.format_exc()})
                    self.flush()
        except Exception:
            self.report["status"] = "failed"
            self.report["errors"].append({"step": "callback", "traceback": traceback.format_exc()})
            self.flush()
            try: unreal.GameplayStatics.set_game_paused(self.world, False)
            except Exception: pass
            for component in self.components.values():
                try: component.stop()
                except Exception: pass
            self.socket.close()
            unreal.unregister_slate_post_tick_callback(self.handle)


SF_DEMO_VALIDATION = DemoValidation()
