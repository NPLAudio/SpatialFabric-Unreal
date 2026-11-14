"""Build and silently smoke-test a disposable published example on Windows UE 5.8.
python Tools/validate_example.py --engine "C:/Program Files/Epic Games/UE_5.8"
The source example/plugin and local open sandbox are never modified. No cue is played.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from prepare_example import ROOT, prepare


def run(command, log, timeout):
    print("Running:", subprocess.list2cmdline(command), flush=True)
    with log.open("w", encoding="utf-8") as stream:
        subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=timeout)


def validate(engine, output):
    if os.name != "nt": raise RuntimeError("This runner currently validates Win64 only")
    engine, output = Path(engine).resolve(), Path(output).resolve()
    if output == ROOT or ROOT in output.parents: raise ValueError("Use evidence outside the repository")
    if output.exists() and any(output.iterdir()): raise ValueError("Use a new or empty evidence directory")
    output.mkdir(parents=True, exist_ok=True)
    project = prepare(output / "Project")
    # Only the disposable copy disables networking, avoiding another running editor's ports.
    config = project.parent / "Config/DefaultSpatialFabric.ini"
    config.write_text(config.read_text() + "\n[/Script/SpatialFabric.SpatialFabricSettings]\nbEnableInEditor=False\n")
    run(["cmd.exe", "/c", str(engine / "Engine/Build/BatchFiles/Build.bat"),
         "SpatialFabricShowcaseEditor", "Win64", "Development", "-Project=" + str(project),
         "-WaitMutex", "-NoHotReloadFromIDE", "-NoLiveCoding"], output / "build.log", 1200)
    script = ROOT / "Tools/Unreal/smoke_example.py"
    # Single-quoted Python string supports spaces without nested UE command-line double quotes.
    command = "py exec(open(" + repr(script.as_posix()) + ").read())"
    if ',' in command or '"' in command: raise ValueError("Smoke script path cannot contain comma/double quote")
    run([str(engine / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"), str(project), "-NullRHI",
         "-Unattended", "-NoSound", "-NoSplash", "-NoLiveCoding", "-stdout", "-FullStdOutLogOutput",
         "-ExecCmds=" + command], output / "smoke.log", 180)
    report = json.loads((project.parent / "Saved/example_smoke.json").read_text(encoding="utf-8"))
    checks = report.get("checks", {})
    if report.get("status") != "passed" or len(checks) < 10 or not all(checks.values()) or report.get("errors"):
        raise RuntimeError("Example smoke test failed: " + json.dumps(report))
    (output / "example_result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output or Path(tempfile.mkdtemp(prefix="SpatialFabricExampleValidation-"))
    print("Example evidence:", output, flush=True)
    result = validate(args.engine, output)
    print("Example passed:", len(result["checks"]), "checks; no audio played")
