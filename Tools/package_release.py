"""Create source-only plugin/example/complete-source release ZIPs and SHA-256 inventory.
Optional --controller-zip adds an independently built portable Windows controller.
Requires a clean committed repository; never includes Unreal build caches or local settings.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from zipfile import ZipFile, ZIP_DEFLATED, ZipInfo
from prepare_example import ROOT, prepare, PLUGIN_FOLDERS, PLUGIN_FILES

EXCLUDED = {'.git','.pi','.claude','.venv','.vs','.idea','binaries','intermediate','saved','deriveddatacache','__pycache__','dist','build'}
ROOT_FILES = tuple(dict.fromkeys(PLUGIN_FILES + ('AUDIT.md','TODO.md','RELEASE_NOTES.md')))


def included(path):
    return not any(part.casefold() in EXCLUDED for part in path.parts) and path.suffix.lower() not in ('.pyc','.pyo','.pdb','.sln','.spec') and path.name not in ('.DS_Store','Thumbs.db') and not path.name.startswith('~$')


def selected(root, folders, files):
    result=[]
    for name in folders:
        base=root/name
        if not base.is_dir():raise FileNotFoundError(base)
        result.extend(p for p in base.rglob('*') if p.is_file() and not p.is_symlink() and included(p.relative_to(root)))
    for name in files:
        p=root/name
        if not p.is_file():raise FileNotFoundError(p)
        result.append(p)
    return sorted(set(result))


def archive(destination,root,files,prefix,epoch):
    stamp=datetime.fromtimestamp(max(epoch,315532800),timezone.utc).timetuple()[:6]
    with ZipFile(destination,'w',ZIP_DEFLATED,compresslevel=9) as output:
        for p in files:
            name=prefix+'/'+p.relative_to(root).as_posix()
            info=ZipInfo(name,stamp);info.compress_type=ZIP_DEFLATED;info.external_attr=0o100644<<16
            output.writestr(info,p.read_bytes())


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def finalize(folder):
    folder=Path(folder)
    path=folder/'RELEASE_MANIFEST.json';manifest=json.loads(path.read_text(encoding='utf-8'))
    manifest['artifacts']=[{'name':p.name,'bytes':p.stat().st_size,'sha256':digest(p)} for p in sorted(folder.glob('*.zip'))]
    path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    files=sorted([*folder.glob('*.zip'),folder/'RELEASE_NOTES.md',path])
    (folder/'SHA256SUMS.txt').write_text(''.join(digest(p)+'  '+p.name+'\n' for p in files),encoding='utf-8')
    return manifest


def package(version,output,controller_zip=None):
    descriptor=json.loads((ROOT/'SpatialFabric.uplugin').read_text(encoding='utf-8'))
    if not re.fullmatch(r'\d+\.\d+\.\d+',version) or descriptor['VersionName']!=version:raise ValueError('Release version must exactly match the descriptor semantic version')
    status=subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True)
    if status.strip():raise RuntimeError('Commit/review the worktree before packaging; generated outputs must be ignored')
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    epoch=int(subprocess.check_output(['git','show','-s','--format=%ct','HEAD'],cwd=ROOT,text=True))
    output=Path(output).resolve()
    if output.exists() and any(output.iterdir()):raise ValueError('Use a new/empty release output directory')
    output.mkdir(parents=True,exist_ok=True)
    archive(output/f'SpatialFabric-{version}.zip',ROOT,selected(ROOT,PLUGIN_FOLDERS,ROOT_FILES),'SpatialFabric',epoch)
    complete_files=ROOT_FILES+('SpatialFabric.uplugin','.gitignore','.gitattributes')
    archive(output/f'SpatialFabric-{version}-CompleteSource.zip',ROOT,selected(ROOT,PLUGIN_FOLDERS+('Apps','Examples','.github'),complete_files),'SpatialFabric',epoch)
    with tempfile.TemporaryDirectory(prefix='SpatialFabricReleaseExample-') as temp:
        project=prepare(Path(temp)/'SpatialFabricShowcase')
        files=[p for p in project.parent.rglob('*') if p.is_file() and not p.is_symlink() and included(p.relative_to(project.parent))]
        archive(output/f'SpatialFabric-Example-{version}.zip',project.parent,sorted(files),'SpatialFabricShowcase',epoch)
    controller_version=re.search(r"__version__ = '([^']+)'",(ROOT/'Apps/OSCController/osc_controller/__init__.py').read_text()).group(1)
    if controller_zip:
        with ZipFile(controller_zip) as zipped:
            names=zipped.namelist()
            if not any(n.endswith('/SpatialFabricOSCController.exe') for n in names) or not any('/runtime-licenses/' in n for n in names):raise ValueError('Controller ZIP must contain the executable and runtime license notices')
        shutil.copy2(controller_zip,output/f'SpatialFabricOSCController-{controller_version}-Windows-x64.zip')
    shutil.copy2(ROOT/'RELEASE_NOTES.md',output/'RELEASE_NOTES.md')
    manifest={'version':version,'tag':'v'+version,'git_commit':commit,'controller_version':controller_version,'plugin_distribution':'source only; compile with matching Unreal engine/toolchain','validated_engine_platform':'UE 5.8 / Win64; see VALIDATION.md for exact scope','presentation':'not included; historical files remain in release v2.0.3'}
    (output/'RELEASE_MANIFEST.json').write_text(json.dumps(manifest),encoding='utf-8')
    return finalize(output)


if __name__=='__main__':
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('--version');parser.add_argument('--output',type=Path);parser.add_argument('--controller-zip',type=Path)
    parser.add_argument('--finalize-directory',type=Path,help='Rebuild manifest/checksums after CI merges source and Windows artifacts')
    args=parser.parse_args()
    if args.finalize_directory:result=finalize(args.finalize_directory)
    else:
        version=args.version or json.loads((ROOT/'SpatialFabric.uplugin').read_text())['VersionName']
        result=package(version,args.output or ROOT/'dist/releases'/version,args.controller_zip)
    print(json.dumps(result,indent=2))
