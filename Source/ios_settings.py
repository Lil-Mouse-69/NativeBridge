"""Apply a reversible, fail-closed settings overlay to known upstream integration points."""
import hashlib, json
from pathlib import Path

VERSION = 1

def digest(data): return hashlib.sha256(data).hexdigest()

def once(text, anchor, replacement):
    if text.count(anchor) != 1:
        raise RuntimeError('iPhone settings integration no longer matches upstream. Refresh/update the adapter before building; no settings patch was applied.')
    return text.replace(anchor,replacement,1)

def apply(repo, backend):
    repo=Path(repo)
    header=Path(__file__).with_name('iOS').joinpath('NativeBridgeSettings.h').read_bytes()
    revision=digest(header+str(VERSION).encode())
    manifest=repo/'.nativebridge-settings.json'
    previous={}; source_data={}
    if manifest.exists():
        old=json.loads(manifest.read_text())
        for path,sha in old['patched'].items():
            if not (repo/path).is_file() or digest((repo/path).read_bytes())!=sha: raise RuntimeError('A patched iPhone source changed. Refresh the backend checkout before rebuilding.')
        if old['revision']==revision: return
        previous=old['patched']
        for path,sha in old['original'].items():
            data=(repo/'.nativebridge-originals'/path).read_bytes()
            if digest(data)!=sha: raise RuntimeError('Original settings backup changed; refusing to overwrite source.')
            source_data[path]=data
    edits={}; originals={}
    def read(path):
        raw=source_data[path] if path in source_data else (repo/path).read_bytes(); originals[path]=raw; return raw.decode()
    if backend=='nxnative':
        path='dm/ios/Sources/main.m'; text=read(path)
        edits[path]=once(text,'int main(int argc, char **argv) {','#define NB_BACKEND 0\n#import "NativeBridgeSettings.h"\n\nint main(int argc, char **argv) {').encode()
        path='dm/ios/Sources/front_ios.m';text=read(path)
        edits[path]=once(text,'- (void)startGame {\n    if (self.game) return;', '- (void)startGame {\n    if (self.game) return;\n    extern void nb_apply_settings(void); nb_apply_settings();').encode()
        edits['dm/ios/Sources/NativeBridgeSettings.h']=header
    elif backend=='macnative':
        path='games/totk/ios/Sources/main.m';text=read(path)
        text=once(text,'#import "touch_pad.h"','#import "touch_pad.h"\n#define NB_BACKEND 1\n#import "NativeBridgeSettings.h"')
        edits[path]=once(text,'    unsetenv("TOTK_AUTOLOAD");','    unsetenv("TOTK_AUTOLOAD");\n    nb_apply_settings();').encode()
        path='games/totk/ios/tools/build_ipa.py';text=read(path)
        # Compile the verified 1.4.3 render-size hook into the embedded game module.
        edits[path]=once(text,'    e=dict(env,TOTK_EXPORT_IOS=str(modules))','    e=dict(env,TOTK_EXPORT_IOS=str(modules))\n    if a.version == "v143": e["TOTK_RENDER_RES"] = "1280x720"').encode()
        edits['games/totk/ios/Sources/NativeBridgeSettings.h']=header
    elif backend=='mush8':
        edits['ios/NativeBridgeSettings.m']=b'#define NB_BACKEND 2\n'+header
        path='crates/nxhost/build.rs';text=read(path)
        anchor='    let sdk = xcrun(&["--sdk", "iphoneos", "--show-sdk-path"]);'
        extra='''
    println!("cargo:rerun-if-changed=../../ios/NativeBridgeSettings.m");
    let bridge = format!("{out}/nativebridge-settings.o");
    let bridge_status = std::process::Command::new("xcrun")
        .args(["--sdk", "iphoneos", "clang", "-fobjc-arc", "-fmodules", "-target", "arm64-apple-ios26.0", "-isysroot", &sdk, "-c", "../../ios/NativeBridgeSettings.m", "-o", &bridge])
        .status().expect("NativeBridge settings clang");
    assert!(bridge_status.success(), "NativeBridge settings compile failed");
    println!("cargo:rustc-link-arg={bridge}");
'''
        edits[path]=once(text,anchor,anchor+extra).encode()
        path='crates/nxhost/src/main.rs';text=read(path)
        anchor='''    if let Some(scale) = field("hud_scale") {
        unsafe { std::env::set_var("NX_HUD_SCALE", scale) };
    }
}'''
        edits[path]=once(text,anchor,anchor[:-1]+'''    unsafe extern "C" { fn nb_apply_settings(); }
    unsafe { nb_apply_settings() };
}''').encode()
    else: raise ValueError('Unknown settings backend')
    # Validate every integration point and destination before modifying anything.
    for path in edits:
        if path not in originals and path not in previous and (repo/path).exists(): raise RuntimeError('Settings source destination already exists: '+path)
    backup=repo/'.nativebridge-originals'
    for path,data in originals.items():
        dest=backup/path;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
    for path,data in edits.items():
        dest=repo/path; dest.parent.mkdir(parents=True,exist_ok=True)
        temp=dest.with_name(dest.name+'.nativebridge-tmp');temp.write_bytes(data);temp.replace(dest)
    manifest.write_text(json.dumps({'revision':revision,'backend':backend,'original':{p:digest(b) for p,b in originals.items()},'patched':{p:digest(b) for p,b in edits.items()}},indent=2))
