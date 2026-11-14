"""Build the plugin and run native tests in a disposable project (Windows UE 5.x).

python Tools/validate_unreal.py --engine "C:/Program Files/Epic Games/UE_5.8"
Use --editor-only for a faster iteration. Default also compiles Development/Shipping.
Results are judged from Unreal's JSON report, NOT its potentially-zero failure exit.
Requires an installed Unreal engine and its supported VS/Windows/.NET Framework SDKs.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REQUIRED_TESTS = {
    "SpatialFabricAudit.Routing.MissingActor",
    "SpatialFabricAudit.Listener.AttachedChildDrift",
    "SpatialFabricAudit.Editor.FrameWithoutBeginPlay",
    "SpatialFabric.Playback.WavValidation",
    "SpatialFabric.Playback.RateRoutingAndEOF",
    "SpatialFabric.Playback.TransportReplayCancellation",
    "SpatialFabric.OSC.TypedPlayTriggers",
    "SpatialFabric.OSC.LoopbackAndClientReuse",
    "SpatialFabric.CSV.ValidationMultilineAndUndo",
}


def run(command, log, timeout=1200):
    print("Running:", subprocess.list2cmdline(command), flush=True)
    with log.open("w", encoding="utf-8") as output:
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
    if result.returncode:
        print("\n".join(log.read_text(errors="replace").splitlines()[-100:]))
        raise RuntimeError(f"Command failed ({result.returncode}); see {log}")


def read_results(path):
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    summary = {k: data.get(k, 0) for k in ("succeeded", "failed", "notRun", "inProcess")}
    if summary["failed"] or summary["notRun"] or summary["inProcess"] or summary["succeeded"] < 1:
        raise RuntimeError(f"Native tests failed/incomplete: {summary}; report: {path}")
    passed = {test.get("fullTestPath") for test in data.get("tests", []) if test.get("state") == "Success"}
    if REQUIRED_TESTS - passed:
        raise RuntimeError(f"Required regressions did not pass: {sorted(REQUIRED_TESTS - passed)}")
    return summary


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--engine", default=os.environ.get("UNREAL_ENGINE_ROOT"), required=not os.environ.get("UNREAL_ENGINE_ROOT"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--editor-only", action="store_true")
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("This runner currently validates Windows only. Mac/Linux require separate native builds.")
    engine = Path(args.engine).resolve()
    output = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix="SpatialFabricValidation-"))
    if output == ROOT or ROOT in output.parents:
        raise RuntimeError("Validation output must be outside the source plugin.")
    if args.output and output.exists() and any(output.iterdir()):
        raise RuntimeError("Choose an empty output directory; existing evidence is never deleted.")
    output.mkdir(parents=True, exist_ok=True)
    print("Validation evidence:", output, flush=True)
    build = ["cmd.exe", "/c", str(engine / "Engine/Build/BatchFiles/RunUAT.bat"), "BuildPlugin",
             "-Plugin=" + str(ROOT / "SpatialFabric.uplugin"), "-Package=" + str(output / "Plugin"), "-NoP4"]
    build.append("-NoTargetPlatforms" if args.editor_only else "-TargetPlatforms=Win64")
    run(build, output / "build.log")
    project = output / "Project"
    (project / "Plugins").mkdir(parents=True)
    (project / "Validation.uproject").write_text(json.dumps({"FileVersion": 3, "Plugins": [
        {"Name": "SpatialFabric", "Enabled": True}, {"Name": "OSC", "Enabled": True}]}), encoding="utf-8")
    run(["cmd.exe", "/c", "mklink", "/J", str(project / "Plugins/SpatialFabric"), str(output / "Plugin")], output / "junction.log")
    run([str(engine / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"), str(project / "Validation.uproject"),
         "-NullRHI", "-Unattended", "-NoSplash", "-NoSound", "-NoLiveCoding", "-stdout", "-FullStdOutLogOutput",
         "-ExecCmds=Automation RunTests SpatialFabric", "-TestExit=Automation Test Queue Empty",
         "-ReportExportPath=" + str(output / "Report")], output / "tests.log", timeout=300)
    summary = read_results(output / "Report/index.json")
    print("Native validation passed:", summary, flush=True)


if __name__ == "__main__":
    main()
