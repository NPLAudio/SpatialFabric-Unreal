"""Publish only validated room-demo dependencies from a disposable copy of the sandbox.
The sandbox map/assets/config and unrelated actors are never edited by this tool.
The versioned example Content/SpatialFabricDemo and manifest are replaced only after
build, sanitization, dependency closure and silent walking-pawn smoke checks pass.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from prepare_example import ROOT,EXAMPLE,prepare
from validate_example import run


def publish(engine,sandbox,output,resume=False):
    engine,sandbox,output=Path(engine).resolve(),Path(sandbox).resolve(),Path(output).resolve()
    if output==ROOT or ROOT in output.parents:raise ValueError('Evidence/disposable project must be outside this repository')
    source=sandbox/'Content/SpatialFabricDemo'
    if not (source/'Maps/L_SpatialFabric_Showcase.umap').is_file():raise FileNotFoundError('Saved room showcase map missing')
    layout=json.loads((sandbox/'Saved/SpatialFabricDemo/room_layout.json').read_text(encoding='utf-8'))
    if layout.get('status')!='passed' or layout.get('layout')!='four_rooms_v1':raise ValueError('Apply and validate the room layout first')
    old=json.loads((EXAMPLE/'ASSET_MANIFEST.json').read_text(encoding='utf-8'))
    for entry in old['files']:
        path=EXAMPLE/entry['path']
        if hashlib.sha256(path.read_bytes()).hexdigest()!=entry['sha256']:raise ValueError('Example asset has uncommitted edits not matching its manifest: '+str(path))
    project=output/'Project/SpatialFabricShowcase.uproject'
    if resume:
        marker=project.parent/'Saved/room_publish_request.json'
        if not project.is_file() or not marker.is_file() or json.loads(marker.read_text()).get('mode')!='four_rooms_v1':raise ValueError('Resume requires an existing marked publication project')
        # Resume from the current saved sandbox, not a half-sanitized or generator-test map.
        disposable_content=project.parent/'Content/SpatialFabricDemo'
        shutil.rmtree(disposable_content);shutil.copytree(source,disposable_content)
    else:
        if output.exists() and any(output.iterdir()):raise ValueError('Choose a new/empty evidence directory')
        output.mkdir(parents=True,exist_ok=True)
        project=prepare(output/'Project')
        shutil.copytree(source,project.parent/'Content/SpatialFabricDemo',dirs_exist_ok=True)
        for config,section in [('DefaultSpatialFabric.ini','SpatialFabric.SpatialFabricSettings'),('DefaultEngine.ini','AudioLoom.AudioLoomOscSettings')]:
            path=project.parent/'Config'/config;path.write_text(path.read_text()+f'\n[/Script/{section}]\nbEnableInEditor=False\n',encoding='utf-8')
        (project.parent/'Saved').mkdir(exist_ok=True)
        (project.parent/'Saved/room_publish_request.json').write_text(json.dumps({'mode':'four_rooms_v1'}),encoding='utf-8')
    saved=project.parent/'Saved'
    for stale in ('room_publish_error.txt','room_publish_inventory.json','example_smoke.json'):
        path=saved/stale
        if path.exists():path.unlink()
    run(['cmd.exe','/c',str(engine/'Engine/Build/BatchFiles/Build.bat'),'SpatialFabricShowcaseEditor','Win64','Development','-Project='+str(project),'-WaitMutex','-NoHotReloadFromIDE','-NoLiveCoding'],output/'build.log',1200)
    script=ROOT/'Tools/Unreal/publish_room_assets.py'
    command='py import runpy; runpy.run_path('+repr(script.as_posix())+')'
    if ',' in command or '"' in command:raise ValueError('Unsupported script path quoting')
    run([str(engine/'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'),str(project),'-NullRHI','-Unattended','-NoSound','-NoSplash','-NoLiveCoding','-stdout','-FullStdOutLogOutput','-ExecCmds='+command],output/'publish_smoke.log',180)
    error=saved/'room_publish_error.txt'
    if error.exists():raise RuntimeError('Publication script failed; see '+str(error))
    smoke=json.loads((saved/'example_smoke.json').read_text(encoding='utf-8'))
    if smoke.get('status')!='passed' or len(smoke.get('checks',{}))<13 or not all(smoke['checks'].values()) or smoke.get('errors'):raise RuntimeError('Publication smoke failed: '+json.dumps(smoke))
    inventory=json.loads((saved/'room_publish_inventory.json').read_text(encoding='utf-8'))
    staging=output/'Published/SpatialFabricDemo'
    if staging.exists():shutil.rmtree(staging)
    staging.mkdir(parents=True)
    for package in inventory['project_package_dependencies']:
        relative=Path(package.removeprefix('/Game/'))
        paths=[project.parent/'Content'/relative.with_suffix(suffix) for suffix in ('.uasset','.umap')]
        existing=[p for p in paths if p.is_file()]
        if len(existing)!=1:raise RuntimeError('Expected one owned asset for dependency '+package)
        destination=staging/relative.relative_to('SpatialFabricDemo').with_suffix(existing[0].suffix)
        destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(existing[0],destination)
    if any(p.name in ('M_SF_Banner.uasset','M_SF_Driver.uasset','spatialfabric-unified.uasset') for p in staging.rglob('*')):raise RuntimeError('Unused decorative assets still referenced; inspect dependency report')
    # Builds may take minutes: do not overwrite asset edits made while validation ran.
    for entry in old['files']:
        path=EXAMPLE/entry['path']
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=entry['sha256']:raise ValueError('Example changed during publication; preserving current work: '+str(path))
    original=EXAMPLE/'Content/SpatialFabricDemo'
    if not (output/'PreviousExampleContent').exists():shutil.copytree(original,output/'PreviousExampleContent')
    shutil.rmtree(original);shutil.copytree(staging,original)
    files=[]
    for folder in ('Content','SourceAssets'):
        for path in sorted((EXAMPLE/folder).rglob('*')):
            if path.is_file():files.append({'path':path.relative_to(EXAMPLE).as_posix(),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    manifest=dict(old,files=files,project_package_dependencies=inventory['project_package_dependencies'],engine_and_script_dependencies=inventory['engine_and_script_dependencies'],layout='four_rooms_v1',actor_count=inventory['actor_count'],text_count=inventory['text_count'])
    (EXAMPLE/'ASSET_MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    result={'status':'published','files':len(files),'packages':len(inventory['project_package_dependencies']),'actors':inventory['actor_count'],'text_actors':inventory['text_count'],'smoke_checks':len(smoke['checks']),'removed_from_copy':inventory['removed_from_disposable_copy_only']}
    (output/'publication_result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('--engine',required=True,type=Path);parser.add_argument('--sandbox',required=True,type=Path);parser.add_argument('--output',type=Path)
    parser.add_argument('--resume',action='store_true',help='Resume only a marked disposable publication project after a failed script step')
    args=parser.parse_args();output=args.output or Path(tempfile.mkdtemp(prefix='SpatialFabricRoomPublication-'))
    print('Publication evidence:',output,flush=True)
    print(json.dumps(publish(args.engine,args.sandbox,output,args.resume),indent=2))
