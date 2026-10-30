#!/usr/bin/env python3
"""Local build orchestration. No game data or key values are uploaded by this wrapper."""
import hashlib, json, os, platform, re, shutil, signal, subprocess, sys, time, plistlib, tempfile, threading
from pathlib import Path
ROOT = Path.home() / 'Library/Application Support/NativeBridge'
REPOS = {'macnative': 'https://github.com/ly05010419/MacNativeGames.git', 'mush8': 'https://github.com/c22dev/mush8.git', 'nxnative': 'https://github.com/seedds/nxnative.git'}
ENV = dict(os.environ, PATH='/opt/homebrew/bin:/opt/homebrew/opt/rustup/bin:/opt/homebrew/opt/python@3.14/bin:' + str(Path.home()/'.cargo/bin') + ':' + os.pathsep.join(filter(None, [os.environ.get('DOTNET_ROOT'), '/usr/local/share/dotnet', str(Path.home()/'.dotnet'), os.environ.get('PATH'), '/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin'])), PYTHONUNBUFFERED='1', GIT_TERMINAL_PROMPT='0', HOMEBREW_NO_AUTO_UPDATE='1', HOMEBREW_NO_ANALYTICS='1', DOTNET_CLI_TELEMETRY_OPTOUT='1', DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1')
child = None

def emit(kind, **kw):
    print(json.dumps(dict(type=kind, **kw)), flush=True)

def run(args, cwd=None, env=None, capture=False):
    global child
    args = list(map(str, args))
    child = subprocess.Popen(args, cwd=cwd, env=env or ENV, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace', start_new_session=True)
    lines = []
    stop=threading.Event(); disk_error=[]; proc=child
    def monitor():
        while not stop.wait(2):
            for path in WATCH_PATHS:
                try: low=shutil.disk_usage(ancestor(path)).free < 5*GIB
                except OSError: low=True
                if low:
                    disk_error.append('Build stopped: free disk space fell below the 5 GiB emergency reserve. Cached work is retained.')
                    try:
                        os.killpg(proc.pid,signal.SIGTERM)
                        if not stop.wait(3) and proc.poll() is None: os.killpg(proc.pid,signal.SIGKILL)
                    except ProcessLookupError: pass
                    return
    watcher=threading.Thread(target=monitor,daemon=True); watcher.start()
    for line in child.stdout:
        line = re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', line.rstrip())
        # Suppress key-looking assignments if a third-party tool prints one.
        line = re.sub(r'(?i)(\b\w*(?:key|secret)\w*\s*[=:]\s*)[0-9a-f]{32,}', r'\1[redacted]', line)
        if capture: lines.append(line)
        else: emit('log', text=line)
    status = child.wait(); stop.set(); watcher.join(timeout=4); child.stdout.close(); child = None
    if disk_error: raise RuntimeError(disk_error[0])
    if status: raise RuntimeError(f'{Path(args[0]).name} exited with code {status}. See the log above.')
    return '\n'.join(lines)

def cancel(*_):
    if child and child.poll() is None:
        os.killpg(child.pid, signal.SIGTERM)
        try: child.wait(timeout=4)
        except subprocess.TimeoutExpired: os.killpg(child.pid, signal.SIGKILL)
    emit('error', text='Cancelled. Cached build files are kept for the next attempt.')
    sys.exit(130)

signal.signal(signal.SIGTERM, cancel)
signal.signal(signal.SIGINT, cancel)

def scan(folder):
    root = Path(folder).expanduser().resolve()
    if not root.is_dir(): raise ValueError('Choose a game directory.')
    files = []
    for here, dirs, names in os.walk(root):
        dirs[:] = [d for d in dirs if not d.startswith('.') and not d.endswith(('_build', '.app')) and d not in ('node_modules', 'build', 'target')]
        for name in names:
            p = Path(here)/name
            if p.suffix.lower() in ('.nsp', '.xci'): files.append(p)
    if not files: raise ValueError('No NSP or XCI files found in this directory.')
    return sorted(files)

def role(p):
    n = p.stem.lower()
    ids = re.findall(r'(?<![0-9a-f])(010[0-9a-f]{13})(?![0-9a-f])', n)
    if any(i.endswith('800') for i in ids) or re.search(r'\b(update|upd|patch)\b', n): return 'update'
    if 'dlc' in n or 'booster' in n or any(not i.endswith(('000', '800')) for i in ids): return 'dlc'
    return 'base'

def backend_for(files, choice):
    if choice != 'auto': return choice
    n = ' '.join(str(f).lower() for f in files)
    if re.search(r'(?<![a-z0-9])(botw|totk)(?![a-z0-9])', n) or any(s in n for s in ('0100f2c0115b6000', 'tears of the kingdom', '01007ef00011e000', 'breath of the wild')): return 'macnative'
    if any(s in n for s in ('0100152000022000', 'mario kart', 'mariokart')): return 'mush8'
    return 'nxnative'

def single(files, kind, override=''):
    if override:
        p = Path(override).resolve()
        if p not in files: raise ValueError(f'Selected {kind} must be inside the game directory.')
        return p
    found = [f for f in files if role(f) == kind]
    if len(found)>1: raise ValueError(f'Multiple {kind} files found. Choose the intended {kind} in Advanced, or keep one version in the directory.')
    return found[0] if found else None

def prepare(c):
    files = scan(c['folder']); backend = backend_for(files, c['backend'])
    keys = Path(c['keys']).expanduser().resolve()
    if keys.is_dir(): keys = keys/'prod.keys'
    if not keys.is_file() or not keys.stat().st_size: raise ValueError('Choose your nonempty prod.keys file or the folder containing it.')
    out = Path(c['output']).expanduser().resolve()
    out.mkdir(parents=True, exist_ok=True)
    base = update = None; dlc = []
    if backend != 'nxnative':
        base = single(files, 'base', c.get('base','')); update = single(files, 'update', c.get('update',''))
        if not base: raise ValueError('No unambiguous base game found. Select it in Advanced.')
        if base == update: raise ValueError('Base and update cannot be the same file.')
        dlc = [f for f in files if f not in (base,update)]
        if any(role(f) != 'dlc' for f in dlc): raise ValueError('Unclassified packages remain. Name DLC files with “DLC”, or use nxnative to inspect package metadata.')
        if backend == 'mush8':
            if not update or base.suffix.lower()!='.nsp': raise ValueError('Mush8 requires a base NSP and update NSP (3.0.5 or 4.0.1). For XCI, select nxnative.')
            if len(dlc)>1: raise ValueError('Mush8 accepts one Booster Course Pass DLC NSP.')
            if not c.get('firmware'): raise ValueError('Mush8 requires your dumped Firmware folder. Choose it in Advanced.')
    if c.get('firmware') and not Path(c['firmware']).is_dir(): raise ValueError('Firmware must be an unpacked directory.')
    if c.get('install') and not c.get('device','').strip(): raise ValueError('Enter the target iPhone name or device ID before installing.')
    if c.get('install') and not re.fullmatch('[A-Z0-9]{10}', c.get('team','')): raise ValueError('Sign into Xcode, then refresh accounts and select your signing team.')
    return files, backend, keys, out, base, update, dlc

# Dependency planning, account discovery and disk checks are local-only.
GIB = 2**30
RESERVE = 10*GIB
WATCH_PATHS = []
PACKAGES = {'macnative':['opus','libvpx','python@3.14','xcodegen','cmake','ninja'], 'nxnative':['python@3.14','xcodegen','zstd','spirv-cross','glslang'], 'mush8':['rustup','xcodegen']}

def probe(args, timeout=20):
    try:
        p=subprocess.run(list(map(str,args)),env=ENV,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=timeout)
        return p.returncode==0, (p.stdout.strip() or p.stderr.strip())
    except (OSError,subprocess.TimeoutExpired): return False,''

def ancestor(path):
    p=Path(path).expanduser().resolve()
    while not p.exists() and p!=p.parent: p=p.parent
    return p

def disk_plan(requirements):
    volumes={}
    for path,amount,label in requirements:
        p=ancestor(path); key=p.stat().st_dev
        if key not in volumes: volumes[key]=dict(path=str(p),free=shutil.disk_usage(p).free,required=RESERVE,labels=[])
        volumes[key]['required']+=amount; volumes[key]['labels'].append(label)
    return [dict(v,ok=v['free']>=v['required']) for v in volumes.values()]

def require_space(plan):
    for v in plan:
        if not v['ok']: raise RuntimeError(f"Not enough disk space for {', '.join(v['labels'])}: {v['free']/GIB:.1f} GiB free, estimated {v['required']/GIB:.1f} GiB needed including 10 GiB reserve. Choose another output/cache location or free space.")

def accounts():
    found={}
    ok,data=probe(['defaults','export','com.apple.dt.Xcode','-'])
    if ok:
        try:
            obj=plistlib.loads(data.encode())
            def visit(x):
                if isinstance(x,dict):
                    tid=x.get('teamID') or x.get('teamIdentifier')
                    if isinstance(tid,str) and re.fullmatch('[A-Z0-9]{10}',tid): found[tid]={'id':tid,'name':str(x.get('teamName') or x.get('name') or 'Apple development team'),'free':bool(x.get('isFreeProvisioningTeam',False))}
                    for value in x.values(): visit(value)
                elif isinstance(x,list):
                    for value in x: visit(value)
            visit(obj)
        except Exception: pass
    ok,pems=probe(['security','find-certificate','-a','-c','Apple Development','-p'])
    if ok:
        for pem in re.findall(r'-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----',pems,re.S):
            try:
                subject=subprocess.run(['/usr/bin/openssl','x509','-noout','-subject'],input=pem,text=True,capture_output=True,timeout=5).stdout
                m=re.search(r'OU\s*=\s*([A-Z0-9]{10})',subject)
                if m and m[1] not in found: found[m[1]]={'id':m[1],'name':'Apple Development certificate','free':False}
            except (OSError,subprocess.TimeoutExpired): pass
    return sorted(found.values(),key=lambda x:x['name'])

def devices():
    with tempfile.TemporaryDirectory(prefix='nativebridge-devices-') as tmp:
        dest=Path(tmp)/'devices.json'
        ok,_=probe(['xcrun','devicectl','list','devices','--json-output',dest],timeout=20)
        if not ok or not dest.exists(): return []
        try:
            data=json.loads(dest.read_text())
            return [{'id':d['identifier'],'name':d.get('deviceProperties',{}).get('name','iPhone / iPad'),'state':d.get('connectionProperties',{}).get('tunnelState','paired')} for d in data.get('result',{}).get('devices',[]) if d.get('hardwareProperties',{}).get('platform')=='iOS']
        except (ValueError,KeyError): return []

def dotnet_sdk():
    # GUI apps do not inherit shell profile changes made by Microsoft's installer.
    candidates = [shutil.which('dotnet', path=ENV['PATH'])]
    candidates += [str(Path(folder)/'dotnet') for folder in ENV['PATH'].split(os.pathsep) if folder]
    # Search installer locations independently of PATH, including Homebrew's
    # versioned SDK and architecture-specific Microsoft installations.
    roots = [ENV.get('DOTNET_ROOT_ARM64'), ENV.get('DOTNET_ROOT'),
             '/usr/local/share/dotnet', str(Path.home()/'.dotnet'),
             '/opt/homebrew/opt/dotnet/libexec', '/opt/homebrew/opt/dotnet@10/libexec',
             '/usr/local/opt/dotnet/libexec', '/usr/local/opt/dotnet@10/libexec',
             '/usr/local/share/dotnet/arm64', '/usr/local/share/dotnet/x64']
    candidates += [str(Path(root)/'dotnet') for root in roots if root]
    for executable in dict.fromkeys(p for p in candidates if p):
        if not os.access(executable, os.X_OK): continue
        ok, versions = probe([executable, '--list-sdks'])
        if ok and any(re.match(r'10\.\d+\.\d+(?:\s|$)', line.strip()) for line in versions.splitlines()):
            ENV['PATH'] = str(Path(executable).parent) + os.pathsep + ENV['PATH']
            return executable
    return None

def tools_report(c):
    selected=c.get('backend','auto'); files=[]
    if c.get('folder'):
        try: files=scan(c['folder'])
        except ValueError: pass
    backends=list(REPOS) if c.get('scope','selected')=='all' else ([backend_for(files,selected)] if selected != 'auto' or files else [])
    ios=c.get('target')=='ios'
    items=[]
    def add(id,name,ok,detail,gb=0,installable=True):
        items.append(dict(id=id,name=name,ok=bool(ok),detail=detail,gb=gb,installable=installable))
    add('silicon','Apple silicon',platform.machine()=='arm64',platform.machine(),installable=False)
    version=platform.mac_ver()[0]
    minimum=26 if any(b in backends for b in ('macnative','nxnative')) else 14
    add('macos',f'macOS {minimum} or newer',int(version.split('.')[0] or 0)>=minimum,version,installable=False)
    ok,v=probe(['xcodebuild','-version']); m=re.search(r'Xcode (\d+)',v)
    xcode=bool(ok and m and int(m[1])>=26)
    add('xcode','Xcode 26 or newer',xcode,v or 'Install Xcode, open it and finish Apple’s setup.',installable=False)
    ok,_=probe(['xcodebuild','-checkFirstLaunchStatus']) if xcode else (False,'')
    add('xcode-ready','Xcode first-launch setup',ok,'Open Xcode to accept its license and finish components.' if not ok else 'Ready',installable=False)
    brew=shutil.which('brew',path=ENV['PATH'])
    if not backends:
        add('selection','Choose a game or backend',False,'Select your game folder or a specific backend to check only its required tools.',installable=False)
    packages=set(p for b in backends for p in PACKAGES[b])
    if not ios:
        packages -= {'xcodegen','cmake','ninja'}
    for package in sorted(packages):
        ok,v=probe([brew,'list','--versions',package]) if brew else (False,'')
        command={'python@3.14':'python3.14','rustup':'rustup','xcodegen':'xcodegen','cmake':'cmake','ninja':'ninja','glslang':'glslangValidator','spirv-cross':'spirv-cross'}.get(package)
        if not (ok and v) and command and shutil.which(command,path=ENV['PATH']):
            ok,v=probe([command,'--version'])
        add('brew:'+package,package,bool(ok and v),v or 'Missing — Homebrew formula',2 if package=='rustup' else 1)
    if 'nxnative' in backends:
        sdk=dotnet_sdk()
        add('dotnet','.NET SDK 10 (nxnative only)',bool(sdk),f'SDK 10 detected: {sdk}' if sdk else 'nxnative requires SDK 10 to build its .NET tools. Checked system, user, DOTNET_ROOT and PATH locations.',3)
    if 'mush8' in backends:
        ok,v=probe(['rustup','toolchain','list'])
        stable=ok and any(s.startswith('stable') for s in v.splitlines())
        add('rust-stable','Rust stable toolchain',stable,'Installed' if stable else 'Missing — rustup',3)
        pinned=ok and any(s.startswith('1.98.1') for s in v.splitlines())
        if ios: add('rust-ios','Rust 1.98.1 + iOS target',pinned and probe(['rustup','target','list','--installed','--toolchain','1.98.1'])[1].find('aarch64-apple-ios')>=0,'Required by Mush8 for iPhone builds',3)
    if backends and (ios or any(b != 'mush8' for b in backends)):
        ok,v=probe(['xcrun','metal','--version']) if xcode else (False,'')
        add('metal','Apple Metal toolchain',ok,v.splitlines()[0] if ok and v else 'Missing — Apple component download',5)
    needs_brew=any(not i['ok'] and (i['id'].startswith('brew:') or i['id']=='dotnet') for i in items)
    if needs_brew:
        add('homebrew','Homebrew (for missing-tool installation)',bool(brew),brew or 'Needed only to install the missing packages listed above. Existing working tools do not require Homebrew.',1,False)
    if ios:
        ok,v=probe(['xcrun','--sdk','iphoneos','--show-sdk-path']) if xcode else (False,'')
        add('ios-sdk','iOS SDK',ok,v or 'Open Xcode Settings → Components and install iOS support.',installable=False)
    missing=[i for i in items if not i['ok'] and i['installable']]
    budget=sum(i['gb'] for i in missing)*GIB
    requirements=[(Path.home()/'Library/Caches',budget//2,'temporary tool downloads')]
    for item in missing:
        dest=Path('/Library/Developer') if item['id']=='metal' else Path.home()/'.rustup' if item['id'].startswith('rust-') else Path('/usr/local/share/dotnet') if item['id']=='dotnet' else Path('/opt/homebrew')
        requirements.append((dest,int(item['gb']*GIB/2),item['name']))
    plan=disk_plan(requirements)
    return dict(items=items,missing=[i['id'] for i in missing],disks=plan,backends=backends,ready=all(i['ok'] for i in items),setupReady=all(i['ok'] for i in items if not i['installable']) and all(d['ok'] for d in plan),estimatedGB=budget/GIB)

def install_missing(c):
    report=tools_report(c)
    if not report['setupReady']: raise RuntimeError('Finish required Xcode/Homebrew/system setup and resolve disk space before installing tools.')
    missing=set(report['missing']); approved=set(c.get('approved',[]))
    if not missing.issubset(approved): raise RuntimeError('The missing-tools list changed. Check again and approve the new download plan.')
    require_space(report['disks'])
    WATCH_PATHS[:]=[d['path'] for d in report['disks']]
    brew=shutil.which('brew',path=ENV['PATH'])
    formulas=sorted(i[5:] for i in missing if i.startswith('brew:'))
    if formulas: run([brew,'install']+formulas)
    if 'rust-stable' in missing: run(['rustup','toolchain','install','stable'])
    if 'rust-ios' in missing:
        run(['rustup','toolchain','install','1.98.1']); run(['rustup','target','add','aarch64-apple-ios','--toolchain','1.98.1'])
    if 'metal' in missing: run(['xcodebuild','-downloadComponent','MetalToolchain'])
    if 'dotnet' in missing:
        # Verify the cask download using Homebrew, then let Apple's Installer handle admin authentication.
        run([brew,'fetch','--cask','dotnet-sdk'])
        ok,location=probe([brew,'--cache','--cask','dotnet-sdk'])
        package=Path(location)
        if not ok or not package.is_file() or package.suffix.lower()!='.pkg': raise RuntimeError('The .NET cask installer layout changed. Install SDK 10 from dotnet.microsoft.com, then check again.')
        run(['/usr/bin/open',package])
        emit('done',text='Finish the approved .NET SDK installation in Apple’s Installer, then click Check again. Setup is not yet confirmed complete.')
        return
    report=tools_report(c); emit('report',**report)
    if report['missing']: raise RuntimeError('Some tools are still missing after setup. See the checklist; no success was assumed.')
    emit('done',text='Required tools are installed and checked.')

def job_identity(folder, backend):
    return hashlib.sha256((str(Path(folder).resolve())+backend).encode()).hexdigest()[:12]

def cached_bytes(folder):
    total=0
    for here, dirs, names in os.walk(folder, followlinks=False):
        dirs[:]=[d for d in dirs if not (Path(here)/d).is_symlink()]
        for name in names:
            p=Path(here)/name
            try:
                if p.is_symlink() or not p.is_file(): continue
                st=p.stat(); total+=min(st.st_size,st.st_blocks*512)
            except OSError: pass
    return total

def shared_fonts(c):
    root=Path(c.get('folder') or '/nonexistent-nativebridge-game')
    names=('0-standard','1-chinese-simplified','2-chinese-simplified-extension','3-chinese-traditional','4-korean','5-nintendo-extension')
    for folder in (root/'shared-fonts',root/'BotW_build/totk/extracted/firmware/shared-fonts', ROOT/'jobs'/job_identity(root,'macnative')/'work/totk/extracted/firmware/shared-fonts'):
        if all((folder/(name+'.bfttf')).is_file() and (folder/(name+'.bfttf')).stat().st_size > 8 for name in names):
            return folder
    return None

def game_report(c):
    items=[]
    def add(name,ok,detail): items.append(dict(id=name,name=name,ok=ok,detail=detail,gb=0,installable=False))
    files=[]; backend=c.get('backend','auto')
    try:
        files=scan(c['folder']); backend=backend_for(files,backend)
        add('Game packages',True,f'{len(files)} package(s) found; backend: {backend}. Content and versions are verified by the backend during build.')
        for f in files:
            with f.open('rb') as stream:
                header=stream.read(4)
                if f.suffix.lower()=='.xci': stream.seek(0x100); header=stream.read(4)
            valid=header==(b'HEAD' if f.suffix.lower()=='.xci' else b'PFS0')
            add(str(f),valid,('Container header found' if valid else 'Invalid or truncated container header')+f' · {f.stat().st_size/GIB:.2f} GiB · {role(f)} filename hint')
    except (ValueError,OSError) as ex: add('Game packages',False,str(ex))
    key=Path(c.get('keys','')).expanduser()
    if key.is_dir(): key=key/'prod.keys'
    valid=False
    if key.is_file():
        with key.open(errors='replace') as stream:
            for line in stream:
                if re.match(r'^\s*[a-zA-Z0-9_]+\s*=\s*[0-9a-fA-F]{32,64}\s*$',line): valid=True; break
    add('prod.keys',valid,'Key-file format found; key values never shown. Game-specific keys are verified during extraction.' if valid else 'Select prod.keys with key-name = hexadecimal-value entries.')
    if backend in ('mush8','macnative'):
        try:
            b=single(files,'base',c.get('base','')); u=single(files,'update',c.get('update',''))
            add('Base game',b is not None,b.name if b else 'Choose the base game in Advanced.')
            if backend=='mush8': add('Mario Kart update',u is not None and u.suffix.lower()=='.nsp',u.name if u else 'Required: update NSP 3.0.5 or 4.0.1.')
            required=backend=='mush8' or (b is not None and b.suffix.lower()=='.nsp')
            fw=Path(c.get('firmware') or '/nonexistent-nativebridge-firmware')
            has_nca=fw.is_dir() and next(fw.rglob('*.nca'),None) is not None
            fonts=shared_fonts(c) if backend=='macnative' else None
            if required or c.get('firmware'):
                add('Firmware / shared fonts',has_nca or fonts is not None,'Existing shared fonts found; backend verifies their hashes.' if fonts else 'NCA files found; content validated by backend.' if has_nca else 'Select unpacked firmware, or place the six extracted .bfttf fonts in the game folder’s shared-fonts directory.')
        except ValueError as ex: add('Package selection',False,str(ex))
    from build_extras import validate
    extra_size=0
    try:
        extra_size=validate(c,backend)
        if c.get('saveImport'): add('Save import',True,'Imported only into an empty save folder. Existing progress is preserved. Choose the actual save contents, not the emulator user/title wrapper.')
        if c.get('cheatImport'): add('Cheat archive',True,'Text files will be included for storage only. These backends do not execute Eden cheat codes.')
    except (ValueError,OSError) as ex: add('Optional imports',False,str(ex))
    size=sum(f.stat().st_size for f in files)+extra_size
    cache=max(2*size+10*GIB,60*GIB if backend=='macnative' else 0)
    if c.get('folder') and backend in REPOS:
        used=cached_bytes(ROOT/'jobs'/job_identity(c['folder'],backend))
        cache=max(20*GIB,cache-used) if used else cache
    output=int(size*1.2)+2*GIB
    if extra_size: output*=2  # streaming IPA rewrite temporarily keeps both copies
    if c.get('install'): cache+=output
    requirements=[(ROOT,cache,'build cache'),(c.get('output') or ROOT,output,'output artifact')]
    plan=disk_plan(requirements)
    return dict(items=items,disks=plan,ready=all(i['ok'] for i in items) and all(d['ok'] for d in plan),backend=backend)


def checkout(backend):
    repo = ROOT/'backends'/backend
    if not (repo/'.git').is_dir():
        repo.parent.mkdir(parents=True, exist_ok=True)
        if repo.exists(): raise RuntimeError(f'Incomplete checkout at {repo}. Remove that folder and retry.')
        run(['git','clone','--depth','1','--recurse-submodules',REPOS[backend],repo])
    commit = run(['git','rev-parse','HEAD'],cwd=repo,capture=True)
    emit('log',text=f'Backend: {REPOS[backend]} @ {commit}. Cached checkout retained; no automatic pull.')
    return repo

def link(source, dest):
    if dest.is_symlink(): dest.unlink()
    elif dest.exists(): raise RuntimeError(f'Refusing to replace existing backend file: {dest}')
    dest.symlink_to(source, target_is_directory=source.is_dir())

def main(c):
    if c['action']=='cleanup':
        from build_extras import clean_job
        backend=backend_for(scan(c['folder']),c.get('backend','auto'))
        clean_job(ROOT,job_identity(c['folder'],backend))
        emit('done',text='Completed build cache deleted. Exported apps, installed games, original packages, keys and saves were kept.'); return
    if c['action']=='check':
        emit('report',**tools_report(c)); emit('done',text='Local setup checks finished. Nothing downloaded.'); return
    if c['action']=='accounts':
        emit('accounts',teams=accounts(),devices=devices()); emit('done',text='Accounts and paired devices refreshed.'); return
    if c['action']=='setup':
        install_missing(c); return
    if c['action']=='prebuild':
        emit('files',**game_report(c)); emit('report',**tools_report(dict(c,scope='selected')))
        emit('done',text='Build preflight complete. No downloads.'); return
    if c['action']=='inspect':
        report=game_report(c); emit('files',**report)
        emit('done',text='File and disk checks finished. No downloads.'); return
    if not c.get('downloadsApproved'): raise RuntimeError('Approve backend and build-dependency downloads before building.')
    report=game_report(c); emit('files',**report)
    if not report['ready']: raise RuntimeError('Resolve missing or invalid game files and disk-space issues in the checklist first.')
    tool_report=tools_report(dict(c,scope='selected')); emit('report',**tool_report)
    if not tool_report['ready']: raise RuntimeError('Required build tools are missing. Open Setup and approve installation first.')
    WATCH_PATHS[:]=[d['path'] for d in report['disks']]
    files, backend, keys, out, base, update, dlc = prepare(c)
    emit('selected',text=backend)
    if platform.machine()!='arm64': raise RuntimeError('Apple silicon is required.')
    run(['xcodebuild','-version'])
    started=time.time(); emit('stage',text='Downloading backend',fraction=0.03)
    repo=checkout(backend)
    if c["target"] == "ios":
        from ios_settings import apply
        apply(repo,backend)
        emit("log",text="Added in-game settings: control opacity, keep screen awake, and supported render-resolution options. Existing apps must be rebuilt to receive this menu.")
    py=shutil.which('python3.14',path=ENV['PATH'])
    if backend!='mush8' and not py: raise RuntimeError('Python 3.14 is missing. Click Install build tools first.')
    identity=job_identity(c['folder'],backend)
    job=ROOT/'jobs'/identity; job.mkdir(parents=True,exist_ok=True)
    stamp=time.strftime('%Y%m%d-%H%M%S')
    target=out/(Path(c['folder']).name+'-'+stamp+('.app' if c['target']=='mac' else '.ipa'))
    history=ROOT/'history.json'
    hist=json.loads(history.read_text()) if history.exists() else {}
    hkey=f"{identity}-{c['target']}"
    emit('estimate',seconds=hist.get(hkey,2700),text='Previous successful build' if hkey in hist else 'Rough first-build estimate; hardware, shaders and downloads vary')
    emit('stage',text='Building game',fraction=0.12)
    if backend=='macnative':
        args=[py,repo/'build_app.py',base,'-k',keys,'-o',target,'--work',job/'work','--platform','macos' if c['target']=='mac' else 'ios','--no-shader-reminder']
        if update: args+=['--update',update]
        for d in dlc: args+=['--dlc',d]
        if c.get('firmware'): args+=['--firmware',c['firmware']]
        elif shared_fonts(c): args+=['--shared-fonts',shared_fonts(c)]
        run(args,cwd=repo)
    elif backend=='nxnative':
        venv=repo/'.bridge-venv'
        if not (venv/'bin/python3').exists(): run([py,'-m','venv',venv])
        py=str(venv/'bin/python3')
        run([py,'-m','pip','install','lz4','capstone','fonttools'])
        inputs=job/'inputs'; inputs.mkdir(exist_ok=True)
        for old in inputs.iterdir():
            if old.is_symlink(): old.unlink()
        names=set()
        for f in files:
            if f.name.lower() in names: raise ValueError('Two packages share a filename. Rename one before building.')
            names.add(f.name.lower()); link(f,inputs/f.name)
        args=[py,repo/'scripts/build_ipa_from_nsp.py',inputs,'-k',keys,'-o',target,'--platform','mac-app' if c['target']=='mac' else 'ios','--progress','lines']
        run(args,cwd=repo)
    else:
        for name,p in [('base.nsp',base),('update.nsp',update),('prod.keys',keys),('Firmware',Path(c['firmware']))]: link(p,repo/name)
        if dlc: link(dlc[0],repo/'dlc.nsp')
        elif (repo/'dlc.nsp').is_symlink(): (repo/'dlc.nsp').unlink()
        run(['bash',repo/('make-app.sh' if c['target']=='mac' else 'make-ios.sh')]+([] if c['target']=='mac' else ['--ipa']),cwd=repo,env=dict(ENV,RUSTUP_TOOLCHAIN='stable'))
        built=repo/'build'/('Mush8.app' if c['target']=='mac' else 'Mush8.ipa')
        run(['/usr/bin/ditto',built,target])
        if c['target']=='mac': emit('log',text='Mush8: open the app, select your original game folder and press Prepare. Its launcher handles game-data preparation.')
    if not target.exists(): raise RuntimeError('Backend returned success but the expected artifact is missing.')
    from build_extras import embed, stage
    embed(target,c,backend)
    emit('artifact',path=str(target))
    if c.get('install') and c['target']=='ios':
        emit('stage',text='Signing and installing on iPhone',fraction=0.92)
        if backend=='macnative':
            args=[py,repo/'games/totk/ios/tools/install_device.py',target,'--device',c['device']]
            if c.get('team'): args+=['--team',c['team']]
            run(args,cwd=repo)
        elif backend=='mush8':
            env=dict(ENV,RUSTUP_TOOLCHAIN='stable',TEAM=c['team'],FREE='1' if c.get('free',True) else '0',BUNDLE_ID='org.nativebridge.mush8.'+c['team'].lower())
            run(['bash',repo/'make-ios.sh','--install',c['device']],cwd=repo,env=env)
            for name in ('prod.keys','base.nsp','update.nsp','dlc.nsp','Firmware'):
                p=repo/name
                if p.exists(): run(['xcrun','devicectl','device','copy','to','--device',c['device'],'--source',p.resolve(),'--destination','Documents/'+name,'--domain-type','appDataContainer','--domain-identifier',env['BUNDLE_ID']])
        else:
            run(args+['--prepare-xcode','--team-id',c['team'],'--bundle-id','org.nativebridge.game'+identity,'--xcode-workspace',job/'Games.xcworkspace'],cwd=repo)
            projects=list(inputs.rglob('*.xcodeproj'))
            if len(projects)!=1: raise RuntimeError(f'Expected one generated Xcode project, found {len(projects)}. Unsigned IPA is saved; open the generated workspace to sign.')
            project=projects[0]
            schemes=list((project/'xcshareddata/xcschemes').glob('*.xcscheme'))
            if len(schemes)!=1: raise RuntimeError('Cannot select an unambiguous generated Xcode scheme. Open the generated workspace to sign.')
            derived=job/'device-build'
            run(['xcodebuild','-project',project,'-scheme',schemes[0].stem,'-configuration','Release','-destination','generic/platform=iOS','-derivedDataPath',derived,'-allowProvisioningUpdates','DEVELOPMENT_TEAM='+c['team'],'CODE_SIGN_STYLE=Automatic','build'])
            apps=list((derived/'Build/Products/Release-iphoneos').glob('*.app'))
            if len(apps)!=1: raise RuntimeError('Signed device app was not found.')
            run(['xcrun','devicectl','device','install','app','--device',c['device'],apps[0]])
        if c.get('saveImport') or c.get('cheatImport'):
            if backend=='macnative':
                # The installer suffixes the base bundle ID with the signing team.
                import zipfile, plistlib
                with zipfile.ZipFile(target) as archive:
                    names=[n for n in archive.namelist() if re.fullmatch(r'Payload/[^/]+\.app/Info.plist',n)]
                    bundle=plistlib.loads(archive.read(names[0]))['CFBundleIdentifier']
                suffix='.'+c['team'].lower()
                bundle=(bundle[:-len(suffix)] if bundle.endswith(suffix) else bundle)+suffix
            elif backend=='mush8': bundle=env['BUNDLE_ID']
            else: bundle='org.nativebridge.game'+identity
            with tempfile.TemporaryDirectory(prefix='nativebridge-import-') as tmp:
                source=stage(tmp,c,backend)
                run(['xcrun','devicectl','device','copy','to','--device',c['device'],'--source',source,'--destination','Documents/NativeBridgeImport','--domain-type','appDataContainer','--domain-identifier',bundle])
            emit('log',text='Imported data staged. Restart the game to seed an empty save folder. Existing saves are never overwritten; cheat text is archived, not executed.')
    (job/'.nativebridge-complete').write_text('NativeBridge completed build cache\n')
    hist[hkey]=time.time()-started; history.write_text(json.dumps(hist))
    emit('stage',text='Complete',fraction=1)
    emit('done',text='Build complete'+(' and installed on iPhone.' if c.get('install') and c['target']=='ios' else '.'))

if __name__=='__main__':
    import fcntl
    config=json.loads(Path(sys.argv[1]).read_text())
    if config.get('cache'): ROOT=Path(config['cache']).expanduser().resolve()
    ROOT.mkdir(parents=True,exist_ok=True)
    lock=open(ROOT/'build.lock','w')
    try:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        main(config)
    except BlockingIOError: emit('error',text='Another NativeBridge operation is already running.'); sys.exit(1)
    except Exception as e: emit('error',text=str(e)); sys.exit(1)
