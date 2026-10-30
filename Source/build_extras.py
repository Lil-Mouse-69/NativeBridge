"""Validated optional build inputs; never consume originals or overwrite saves."""
import json, os, re, shutil, tempfile, zipfile
from pathlib import Path

LIMIT = 1024 * 1024 * 1024

def files_in(folder, cheats=False):
    root=Path(folder).expanduser()
    if not root.is_dir() or root.is_symlink(): raise ValueError('Choose a regular, unpacked folder.')
    files=[]; total=0
    for base, dirs, names in os.walk(root, followlinks=False):
        for name in dirs+names:
            p=Path(base)/name
            if p.is_symlink(): raise ValueError('Import folders cannot contain symbolic links.')
        for name in names:
            p=Path(base)/name
            if name=='.DS_Store': continue
            if not p.is_file(): raise ValueError('Only regular files can be imported.')
            relative=p.relative_to(root)
            if cheats:
                if p.suffix.lower()!='.txt' or not re.fullmatch(r'[0-9a-fA-F]{16}',p.stem):
                    raise ValueError('Cheats must be BUILD_ID.txt files (16 hexadecimal characters), inside the selected title folder.')
                text=p.read_text(encoding='utf-8-sig')
                if not re.search(r'^\[.+\]$',text,re.M): raise ValueError('Cheat text must contain a [Cheat name] section.')
            total+=p.stat().st_size
            if total>LIMIT or len(files)>=10000: raise ValueError('Import exceeds 1 GiB or 10,000 files.')
            files.append((p,relative))
    if not files: raise ValueError('The selected import folder is empty.')
    return files,total

def validate(c,backend):
    size=0
    for key in ('saveImport','cheatImport'):
        if c.get(key):
            if c.get('target')!='ios': raise ValueError('Save and cheat imports currently support iPhone builds only.')
            _,count=files_in(c[key],key=='cheatImport'); size+=count
    if c.get('saveImport') and backend=='mush8' and c.get('saveVersion') not in ('4.0.1','3.0.5'):
        raise ValueError('Select the Mario Kart version matching the save and update.')
    if c.get('saveImport') and backend=='mush8':
        children=list(Path(c['saveImport']).iterdir())
        if not any(p.is_dir() and re.fullmatch(r'type\d+-index\d+',p.name) for p in children):
            raise ValueError('Mush8 needs its native save folder containing typeN-indexN directories; raw emulator saves need conversion first.')
    return size

def save_destination(c,backend):
    if backend=='mush8': return 'work/save' if c.get('saveVersion')=='4.0.1' else 'work/save-3.0.5'
    return 'zelda-user' if backend=='macnative' else 'save'

def embed(ipa,c,backend):
    if not (c.get('saveImport') or c.get('cheatImport')): return
    validate(c,backend)
    ipa=Path(ipa)
    with zipfile.ZipFile(ipa) as source:
        apps={n.split('/')[1] for n in source.namelist() if n.startswith('Payload/') and len(n.split('/'))>2 and n.split('/')[1].endswith('.app')}
        if len(apps)!=1: raise ValueError('Expected exactly one app in the IPA.')
        prefix='Payload/'+apps.pop()+'/NativeBridgeImport/'
        with tempfile.NamedTemporaryFile(dir=ipa.parent,suffix='.ipa',delete=False) as f: temporary=Path(f.name)
        try:
            with zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED,allowZip64=True) as target:
                for item in source.infolist():
                    if item.filename.startswith(prefix): continue
                    with source.open(item) as src, target.open(item,'w',force_zip64=True) as dst: shutil.copyfileobj(src,dst,1024*1024)
                target.writestr(prefix+'manifest.json',json.dumps({'saveDestination':save_destination(c,backend)}))
                for key,subdir in (('saveImport','save'),('cheatImport','cheats-archive')):
                    if c.get(key):
                        for path,rel in files_in(c[key],key=='cheatImport')[0]: target.write(path,prefix+subdir+'/'+rel.as_posix())
            os.replace(temporary,ipa)
        finally:
            temporary.unlink(missing_ok=True)

def clean_job(root,identity):
    root=Path(root).resolve(); jobs=root/'jobs'; job=jobs/identity
    if not re.fullmatch(r'[0-9a-f]{12}',identity): raise ValueError('Invalid build identifier.')
    if jobs.is_symlink() or job.is_symlink() or not (job/'.nativebridge-complete').is_file():
        raise ValueError('Only a completed NativeBridge build cache can be deleted.')
    shutil.rmtree(job)  # symlinks inside are unlinked, not followed

def stage(directory,c,backend):
    destination=Path(directory)/'NativeBridgeImport'; destination.mkdir()
    (destination/'manifest.json').write_text(json.dumps({'saveDestination':save_destination(c,backend)}))
    for key,subdir in (('saveImport','save'),('cheatImport','cheats-archive')):
        if c.get(key):
            for source,rel in files_in(c[key],key=='cheatImport')[0]:
                target=destination/subdir/rel; target.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(source,target)
    return destination
