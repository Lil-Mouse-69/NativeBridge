// NativeBridge in-game settings. Compiled into the game host, never into game data.
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <GameController/GameController.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <stdlib.h>
#include <math.h>
#ifndef NB_BACKEND
#error Define NB_BACKEND: 0=nxnative, 1=MacNativeGames, 2=Mush8
#endif
#import "NativeBridgeCheats.h"
static NSString *const NBOpacityKey = @"NativeBridge.controlsOpacity";
static NSString *const NBFPSKey = @"NativeBridge.fpsTarget";
static NSString *const NBRenderKey = @"NativeBridge.renderChoice";
static NSString *const NBKeepAwakeKey = @"NativeBridge.keepAwake";
static BOOL NBCustomSize(NSString *value) {
    unsigned w=0,h=0; char extra;
    return value && sscanf(value.UTF8String,"%ux%u%c",&w,&h,&extra)==2 && w>=64 && h>=64 && w<=4096 && h<=4096 && !(w%2) && !(h%2) && (uint64_t)w*h<=4194304;
}
static NSString *NBRenderLabel(void) {
    NSString *value=[NSUserDefaults.standardUserDefaults stringForKey:NBRenderKey] ?: @"default";
    return @{@"default":@"Game / launcher default",@"0.5":@"50% of base resolution",@"0.75":@"75% of base resolution",@"1.0":@"100% of base resolution",@"960x540":@"540p · 960 × 540",@"1280x720":@"Native 720p · 1280 × 720",@"720":@"720p · Handheld",@"1080":@"1080p · Docked"}[value] ?: (NBCustomSize(value) ? [@"Custom · " stringByAppendingString:value] : @"Game / launcher default");
}
static BOOL NBScaleSupported(void) {
#if NB_BACKEND == 0
    NSString *path = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"dm/launcher.json"];
    NSData *data = [NSData dataWithContentsOfFile:path];
    NSDictionary *json = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    for (NSDictionary *row in json[@"settings"]) if ([row[@"env"] isEqual:@"DM_RENDER_SCALE"]) return YES;
    return NO;
#elif NB_BACKEND == 1
#if defined(TOTK_GAME_V143) || defined(GAME_BOTW)
    return YES;
#else
    return NO;
#endif
#else
    return YES;
#endif
}
static BOOL NBFPSIsSupported(void) {
#if NB_BACKEND == 0
    NSData *data=[NSData dataWithContentsOfFile:[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"dm/launcher.json"]];
    NSDictionary *json=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    for(NSDictionary *row in json[@"settings"]) {
        if(![row[@"env"] isEqual:@"DM_FPS_TARGET"]) continue;
        NSMutableSet *values=[NSMutableSet set];
        for(NSArray *option in row[@"options"]) if(option.count) [values addObject:option[0]];
        return [values containsObject:@"30"] && [values containsObject:@"60"];
    }
    return NO;
#elif NB_BACKEND == 1
    return YES;
#else
    return NO; // Mush8 has no verified 30/60 pacing control; don't slow the game's simulation.
#endif
}
static void NBApplyFPS(void) {
    NSString *fps=[NSUserDefaults.standardUserDefaults stringForKey:NBFPSKey];
    if(!NBFPSIsSupported() || ![@[@"30",@"60"] containsObject:fps]) return;
#if NB_BACKEND == 0
    NSString *locked=@(getenv("DM_ENV_FILE_KEYS") ?: "");
    if(![[locked componentsSeparatedByString:@" "] containsObject:@"DM_FPS_TARGET"]) setenv("DM_FPS_TARGET",fps.UTF8String,1);
#elif NB_BACKEND == 1
    setenv("TOTK_PHONE_FPS",fps.UTF8String,1);
#endif
}
static NSString *const NBOverlayKey = @"NativeBridge.performanceOverlay";
static BOOL NBOverlaySupported(void) { return NB_BACKEND != 2; }
void nb_apply_overlay(void) {
    NSString *mode=[NSUserDefaults.standardUserDefaults stringForKey:NBOverlayKey];
    if (!NBOverlaySupported() || !mode || [mode isEqual:@"default"]) return;
    BOOL full=[mode isEqual:@"full"], visible=![mode isEqual:@"off"];
#if NB_BACKEND == 0
    setenv("DM_METAL_HUD",full ? "1" : "0",1);
    setenv("MTL_HUD_ENABLED",full ? "1" : "0",1);
    setenv("DM_SHOW_FPS",visible && !full ? "1" : "0",1);
#elif NB_BACKEND == 1
    extern void fps_overlay_set_hud_mode(int mode) __attribute__((weak_import));
    if (fps_overlay_set_hud_mode) fps_overlay_set_hud_mode(!visible ? 0 : full ? 1 : 2);
    setenv("NATIVEBRIDGE_OVERLAY",mode.UTF8String,1);
    setenv("TOTK_FPS_OVERLAY",visible ? "1" : "0",1);
    setenv("TOTK_SHADER_OVERLAY",full ? "1" : "0",1);
    if (!full) setenv("MTL_HUD_ENABLED","0",1);
#endif
}
static NSString *const NBAspectKey = @"NativeBridge.screenFit";
static BOOL NBAspectSupported(void) {
#if NB_BACKEND == 1 && !defined(GAME_BOTW) && !defined(TOTK_GAME_V121) && !defined(TOTK_GAME_V143)
    return NO;
#else
    return NB_BACKEND == 1 || NB_BACKEND == 2;
#endif
}
static void NBImportSave(void) {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSString *docs=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
    NSString *source=[docs stringByAppendingPathComponent:@"NativeBridgeImport"];
    if (![fm fileExistsAtPath:[source stringByAppendingPathComponent:@"manifest.json"]]) source=[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"NativeBridgeImport"];
    NSData *data=[NSData dataWithContentsOfFile:[source stringByAppendingPathComponent:@"manifest.json"]];
    if (!data) return;
    NSDictionary *manifest=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    NSString *relative=manifest[@"saveDestination"];
    if (![@[@"save",@"zelda-user",@"work/save",@"work/save-3.0.5"] containsObject:relative]) return;
    NSString *seed=[source stringByAppendingPathComponent:@"save"];
    if (![fm fileExistsAtPath:seed]) return;
#if NB_BACKEND == 1
    if ([relative isEqual:@"zelda-user"]) {
        extern void runtime_account_uid(uint64_t out[2]);
        uint64_t uid[2]; runtime_account_uid(uid);
        relative=[NSString stringWithFormat:@"save/save/%016llx%016llx",(unsigned long long)uid[1],(unsigned long long)uid[0]];
    }
#else
    if ([relative isEqual:@"zelda-user"]) return;
#endif
    NSString *destination=[docs stringByAppendingPathComponent:relative];
    NSError *error=nil;
    BOOL replace=[manifest[@"replaceExisting"] boolValue] && [source hasPrefix:[docs stringByAppendingString:@"/"]];
    if ([fm fileExistsAtPath:destination]) {
        NSArray *existing=[fm contentsOfDirectoryAtPath:destination error:&error];
        if (!existing || (existing.count && !replace)) { NSLog(@"NativeBridge: existing save preserved; import skipped."); return; }
    }
    NSString *staged=[destination stringByAppendingFormat:@".import-%@",NSUUID.UUID.UUIDString];
    [fm createDirectoryAtPath:destination.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&error];
    if (![fm copyItemAtPath:seed toPath:staged error:&error]) { NSLog(@"NativeBridge save import: %@",error); [fm removeItemAtPath:staged error:nil]; return; }
    NSString *backup=[destination stringByAppendingFormat:@".backup-%@",NSUUID.UUID.UUIDString];
    BOOL backedUp=NO;
    if ([fm fileExistsAtPath:destination]) {
        NSArray *existing=[fm contentsOfDirectoryAtPath:destination error:&error];
        if (!existing || (existing.count && !replace)) { [fm removeItemAtPath:staged error:nil]; return; }
        if (![fm moveItemAtPath:destination toPath:backup error:&error]) { [fm removeItemAtPath:staged error:nil]; return; }
        backedUp=YES;
    }
    if (![fm moveItemAtPath:staged toPath:destination error:&error]) {
        if (backedUp) [fm moveItemAtPath:backup toPath:destination error:nil];
        NSLog(@"NativeBridge save import: %@",error);
    } else {
        [NSUserDefaults.standardUserDefaults setObject:@"Save imported successfully. Previous progress was backed up when present." forKey:@"NativeBridge.lastSaveImport"];
        if (replace) [fm removeItemAtPath:seed error:nil]; // consume explicit import once
        NSLog(@"NativeBridge: imported save into %@; backup %@",relative,backedUp ? backup : @"not needed");
    }
}
static void NBApplyAspect(void) {
    NSString *fit=[NSUserDefaults.standardUserDefaults stringForKey:NBAspectKey];
    if (!NBAspectSupported() || !fit || [fit isEqual:@"default"]) return;
#if NB_BACKEND == 1
    if ([fit isEqual:@"fill"]) unsetenv("TOTK_ASPECT"); // phone default follows the drawable; "display" is not a valid ratio
    else setenv("TOTK_ASPECT","16:9",1);
#elif NB_BACKEND == 2
    setenv("NX_ASPECT",[fit isEqual:@"fill"] ? "auto" : "16:9",1);
#endif
}

void nb_apply_settings(void) {
    NBImportSave(); NBApplyAspect(); nb_apply_overlay();
    NBApplyFPS();
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    NSString *choice = [d stringForKey:NBRenderKey];
    if (!NBScaleSupported() || !choice.length || [choice isEqual:@"default"]) return;
#if NB_BACKEND == 0
    // Never override explicit per-game env.txt settings.
    NSString *locked = @(getenv("DM_ENV_FILE_KEYS") ?: "");
    if ([[locked componentsSeparatedByString:@" "] containsObject:@"DM_RENDER_SCALE"]) return;
    if ([@[@"0.5",@"0.75",@"1.0"] containsObject:choice]) setenv("DM_RENDER_SCALE",choice.UTF8String,1);
#elif NB_BACKEND == 1
    if ([@[@"960x540",@"1280x720"] containsObject:choice]) setenv("TOTK_RENDER_RES",choice.UTF8String,1);
#if defined(TOTK_GAME_V143)
    else if (NBCustomSize(choice)) setenv("TOTK_RENDER_RES",choice.UTF8String,1);
#endif
#else
    if ([choice isEqual:@"720"]) setenv("NX_HANDHELD","1",1);
    if ([choice isEqual:@"1080"]) unsetenv("NX_HANDHELD");
#endif
}
@interface NBSettingsPanel : UITableViewController <UIDocumentPickerDelegate>
@property(nonatomic) BOOL importingCheats;
@property(nonatomic) BOOL importBusy;
@property(nonatomic,copy) NSString *saveDestination;
@end
@protocol NBTouchVisibility
- (BOOL)nb_controlsHidden;
@end
@interface NBGameSettings : NSObject
@property(nonatomic,strong) UIButton *button;
@property(nonatomic,strong) NSTimer *timer;
@property(nonatomic,weak) UIWindow *window;
@property(nonatomic) BOOL sawTouchControls;
@property(nonatomic) BOOL touchControlsVisible;
@property(nonatomic,copy) NSString *appliedScreenShape;
+ (instancetype)shared;
- (void)refresh;
- (void)show;
@end
@implementation NBGameSettings
+ (instancetype)shared { static NBGameSettings *s; static dispatch_once_t once; dispatch_once(&once, ^{ s=[NBGameSettings new]; }); return s; }
+ (void)load {
    @autoreleasepool {
        [NSUserDefaults.standardUserDefaults registerDefaults:@{NBOpacityKey:@1.0,NBKeepAwakeKey:@YES,NBRenderKey:@"default",NBFPSKey:@"default"}];
        [NSNotificationCenter.defaultCenter addObserverForName:UIApplicationDidFinishLaunchingNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) {
            NBGameSettings *s=[NBGameSettings shared];
            s.timer=[NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *t) { [s refresh]; }];
            [s refresh];
        }];
    }
}
- (void)applyToView:(UIView *)view {
    NSString *name=NSStringFromClass(view.class);
    if ([@[@"TOTKTouchPad",@"DmGamepad",@"NXTouchView"] containsObject:name]) {
        self.sawTouchControls=YES;
        BOOL concealed=view.hidden || view.alpha < 0.01;
        if ([view respondsToSelector:@selector(nb_controlsHidden)]) concealed=[(id<NBTouchVisibility>)view nb_controlsHidden];
        else for (GCController *controller in GCController.controllers) if (controller.extendedGamepad) concealed=YES;
        if (!concealed) self.touchControlsVisible=YES;
        // Preserve hit testing at zero opacity; the gear follows controller concealment.
        double value=[NSUserDefaults.standardUserDefaults doubleForKey:NBOpacityKey];
        view.layer.opacity=(float)(isfinite(value) ? fmax(0,fmin(1,value)) : 1);
        return;
    }
    for (UIView *child in view.subviews) [self applyToView:child];
}
- (void)refresh {
    if (UIApplication.sharedApplication.applicationState != UIApplicationStateActive) return;
    NBCheatTick();
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:UIWindowScene.class] || scene.activationState!=UISceneActivationStateForegroundActive) continue;
        for (UIWindow *w in ((UIWindowScene *)scene).windows) {
            if (!w.isKeyWindow || !w.rootViewController) continue;
            if (self.window!=w || !self.button) {
                [self.button removeFromSuperview]; self.window=w;
                self.button=[UIButton buttonWithType:UIButtonTypeSystem];
                [self.button setImage:[UIImage systemImageNamed:@"gearshape.fill"] forState:UIControlStateNormal];
                self.button.tintColor=UIColor.whiteColor; self.button.backgroundColor=[UIColor colorWithWhite:0.08 alpha:0.85]; self.button.layer.cornerRadius=22;
                self.button.accessibilityLabel=@"Game settings";
                [self.button addTarget:self action:@selector(show) forControlEvents:UIControlEventTouchUpInside];
                [w addSubview:self.button];
            }
            self.button.frame=CGRectMake(CGRectGetMidX(w.bounds)-22,CGRectGetMaxY(w.bounds)-w.safeAreaInsets.bottom-52,44,44);
            [w bringSubviewToFront:self.button];
#if NB_BACKEND == 1
            NSString *fit=[NSUserDefaults.standardUserDefaults stringForKey:NBAspectKey];
            if (fit && ![fit isEqual:@"default"]) {
                NSString *shape=[NSString stringWithFormat:@"%@-%g-%g",fit,w.bounds.size.width,w.bounds.size.height];
                if (![shape isEqual:self.appliedScreenShape] && w.bounds.size.width>w.bounds.size.height && w.bounds.size.height>0) {
                    extern int runtime_aspect_set_ratio(float ratio) __attribute__((weak_import));
                    NBApplyAspect();
                    float ratio=[fit isEqual:@"fill"] ? w.bounds.size.width/w.bounds.size.height : 16.0f/9.0f;
                    if (runtime_aspect_set_ratio && runtime_aspect_set_ratio(ratio)) self.appliedScreenShape=shape;
                }
            } else self.appliedScreenShape=nil;
#endif

            self.sawTouchControls=NO; self.touchControlsVisible=NO;
            [self applyToView:w.rootViewController.view];
            self.button.hidden=self.sawTouchControls && !self.touchControlsVisible;
            UIApplication.sharedApplication.idleTimerDisabled=[NSUserDefaults.standardUserDefaults boolForKey:NBKeepAwakeKey];
            return;
        }
    }
}
- (void)show {
    UIViewController *root=self.window.rootViewController;
    while(root.presentedViewController && !root.presentedViewController.isBeingDismissed) root=root.presentedViewController;
    if (!root || [root isKindOfClass:NBSettingsPanel.class] || ([root isKindOfClass:UINavigationController.class] && [((UINavigationController *)root).topViewController isKindOfClass:NBSettingsPanel.class])) return;
    NBSettingsPanel *panel=[[NBSettingsPanel alloc] initWithStyle:UITableViewStyleInsetGrouped];
    UINavigationController *nav=[[UINavigationController alloc] initWithRootViewController:panel];
    nav.modalPresentationStyle=UIModalPresentationFormSheet;
    [root presentViewController:nav animated:YES completion:nil];
}
@end
@implementation NBSettingsPanel
- (void)viewDidLoad {
    [super viewDidLoad]; self.title=@"Game settings";
    self.navigationItem.rightBarButtonItem=[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(done)];
}
- (void)notice:(NSString *)title text:(NSString *)text {
    UIAlertController *a=[UIAlertController alertControllerWithTitle:title message:text preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:a animated:YES completion:nil];
}
- (void)pickImport:(BOOL)cheats destination:(NSString *)destination {
    if (self.importBusy) return;
    self.importBusy=YES; self.importingCheats=cheats; self.saveDestination=destination;
    UIDocumentPickerViewController *picker=[[UIDocumentPickerViewController alloc] initForOpeningContentTypes:cheats ? @[UTTypeFolder,UTTypePlainText] : @[UTTypeFolder,UTTypeData] asCopy:NO];
    picker.delegate=self; picker.allowsMultipleSelection=NO;
    [self presentViewController:picker animated:YES completion:nil];
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller { self.importBusy=NO; }
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSURL *url=urls.firstObject; if (!url) { self.importBusy=NO; return; }
    BOOL cheats=self.importingCheats; NSString *saveDestination=self.saveDestination;
    [controller dismissViewControllerAnimated:YES completion:^{
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        BOOL access=[url startAccessingSecurityScopedResource];
        NSFileManager *fm=NSFileManager.defaultManager; __block NSError *error=nil;
        NSString *docs=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
        NSString *temporary=[docs stringByAppendingPathComponent:[@".nativebridge-import-" stringByAppendingString:NSUUID.UUID.UUIDString]];
        BOOL directory=NO; [fm fileExistsAtPath:url.path isDirectory:&directory];
        NSArray *files=directory ? [[fm enumeratorAtURL:url includingPropertiesForKeys:@[NSURLIsRegularFileKey,NSURLIsSymbolicLinkKey,NSURLFileSizeKey] options:0 errorHandler:^BOOL(NSURL *u,NSError *e){ error=e; return NO; }] allObjects] : @[url];
        unsigned long long total=0; NSUInteger count=0;
        NSMutableArray *regular=[NSMutableArray array], *importedCheats=[NSMutableArray array];
        for (NSURL *file in files) {
            NSDictionary *v=[file resourceValuesForKeys:@[NSURLIsRegularFileKey,NSURLIsSymbolicLinkKey,NSURLFileSizeKey] error:&error];
            if (!v || [v[NSURLIsSymbolicLinkKey] boolValue]) { error=[NSError errorWithDomain:@"NativeBridge" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Symbolic links cannot be imported."}]; break; }
            if (![v[NSURLIsRegularFileKey] boolValue] || [file.lastPathComponent isEqual:@".DS_Store"]) continue;
            total+=[v[NSURLFileSizeKey] unsignedLongLongValue]; count++;
            if (total>256ull*1024*1024 || count>10000) { error=[NSError errorWithDomain:@"NativeBridge" code:2 userInfo:@{NSLocalizedDescriptionKey:@"Import is limited to 256 MiB and 10,000 files."}]; break; }
            if (cheats) {
                if (NB_BACKEND!=1 || total>1024*1024) { error=[NSError errorWithDomain:@"NativeBridge" code:10 userInfo:@{NSLocalizedDescriptionKey:@"Runtime cheat import supports Zelda only, with files totaling at most 1 MiB."}]; break; }
                NSPredicate *valid=[NSPredicate predicateWithFormat:@"SELF MATCHES %@",@"[0-9a-fA-F]{16}\\.([tT][xX][tT])"];
                NSString *text=[NSString stringWithContentsOfURL:file encoding:NSUTF8StringEncoding error:&error];
                if (![valid evaluateWithObject:file.lastPathComponent] || ![text containsString:@"["]) { error=[NSError errorWithDomain:@"NativeBridge" code:3 userInfo:@{NSLocalizedDescriptionKey:@"Choose BUILD_ID.txt cheat files or their folder. Names must contain a 16-digit hexadecimal build ID."}]; break; }
                NSString *failure=nil; NSArray *groups=NBParseCheats(text,&failure);
                if (groups) {
                NSString *buildID=[[NSString stringWithContentsOfFile:[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"nativebridge-build-id.txt"] encoding:NSUTF8StringEncoding error:nil] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
                if (buildID.length!=16 || ![file.lastPathComponent.stringByDeletingPathExtension.uppercaseString isEqual:buildID.uppercaseString]) { error=[NSError errorWithDomain:@"NativeBridge" code:11 userInfo:@{NSLocalizedDescriptionKey:@"Cheat build ID does not match this game. Select the matching BUILD_ID.txt file. Rebuild with NativeBridge 0.6.0 if this app has no build-ID metadata."}]; break; }

                    NSMutableArray *checked=[NSMutableArray array]; NSUInteger compatible=0;
                    for(NSDictionary *group in groups) {
                        NSMutableDictionary *entry=[group mutableCopy];
                        NSString *problem=entry[@"error"] ?: NBCheckCheatWrites(@[entry]);
                        if(problem) entry[@"error"]=problem; else compatible++;
                        [checked addObject:entry];
                    }
                    groups=checked;
                    (void)compatible; // keep unsupported entries visible with their individual reasons
                }
                if (failure) { error=[NSError errorWithDomain:@"NativeBridge" code:12 userInfo:@{NSLocalizedDescriptionKey:failure}]; break; }
                [importedCheats addObjectsFromArray:groups];
            } else if ([@[@"zip",@"7z",@"nsp",@"xci",@"ipa"] containsObject:file.pathExtension.lowercaseString]) { error=[NSError errorWithDomain:@"NativeBridge" code:4 userInfo:@{NSLocalizedDescriptionKey:@"Unpack the save first, then select its actual contents."}]; break; }
            [regular addObject:file];
        }
        if (!count && !error) error=[NSError errorWithDomain:@"NativeBridge" code:5 userInfo:@{NSLocalizedDescriptionKey:@"The folder has no save or cheat files."}];
        if (!error) {
            NSString *sub=cheats ? @"cheats-archive" : @"save";
            for (NSURL *file in regular) {
                NSString *relative=directory ? [file.path substringFromIndex:url.path.length+1] : file.lastPathComponent;
                NSString *target=[[temporary stringByAppendingPathComponent:sub] stringByAppendingPathComponent:relative];
                if (![fm createDirectoryAtPath:target.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&error] || ![fm copyItemAtPath:file.path toPath:target error:&error]) break;
            }
#if NB_BACKEND == 2
            if (!error && !cheats) {
                BOOL native=NO;
                for (NSString *child in [fm contentsOfDirectoryAtPath:[temporary stringByAppendingPathComponent:@"save"] error:&error]) if ([child hasPrefix:@"type"] && [child containsString:@"-index"]) native=YES;
                if (!native) error=[NSError errorWithDomain:@"NativeBridge" code:13 userInfo:@{NSLocalizedDescriptionKey:@"Mush8 requires its native save folder containing typeN-indexN directories. Raw emulator saves need conversion."}];
            }
#endif
            if (!error && !cheats) {
                NSData *manifest=[NSJSONSerialization dataWithJSONObject:@{@"saveDestination":saveDestination,@"replaceExisting":@YES} options:0 error:&error];
                [manifest writeToFile:[temporary stringByAppendingPathComponent:@"manifest.json"] options:NSDataWritingAtomic error:&error];
            }
            if (!error) {
                NSString *target=[docs stringByAppendingPathComponent:cheats ? [@"NativeBridgeCheats-" stringByAppendingString:NSUUID.UUID.UUIDString] : @"NativeBridgeImport"];
                NSString *old=[target stringByAppendingFormat:@".previous-%@",NSUUID.UUID.UUIDString];
                BOOL moved=NO;
                if ([fm fileExistsAtPath:target]) moved=[fm moveItemAtPath:target toPath:old error:&error];
                if (!error && ![fm moveItemAtPath:temporary toPath:target error:&error] && moved) [fm moveItemAtPath:old toPath:target error:nil];
            }
        }
        [fm removeItemAtPath:temporary error:nil]; if(access) [url stopAccessingSecurityScopedResource];
        dispatch_async(dispatch_get_main_queue(), ^{
            self.importBusy=NO;
            if (!error && cheats) { [NSUserDefaults.standardUserDefaults setObject:importedCheats forKey:NBCheatsKey]; [NSUserDefaults.standardUserDefaults setObject:NBCheatBuildID() forKey:@"NativeBridge.cheatBuildID"]; }
            [self notice:error ? @"Import failed" : cheats ? @"Cheats imported · disabled" : @"Save ready to import" text:error ? error.localizedDescription : cheats ? @"Open Enable / disable cheats to review each entry. Compatible cheats start OFF; unsupported entries show their reason when tapped. A successful import does not mean every cheat can execute." : @"Close the game from the app switcher and reopen it. Your current save will be backed up before the imported save replaces it. Do not keep playing before restarting."];
        });
    });
    }];
}

- (void)done { [self dismissViewControllerAnimated:YES completion:nil]; }
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView { return 4; }
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section { return section==3 ? 3 : section==2 ? 1 : (section==1 ? 5 : 2); }
- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section { return @[@"On-screen controls",@"Graphics",@"Restore defaults",@"Import files"][section]; }
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    if(section==3) return @"Saves apply after restarting and keep a backup. Zelda supports a restricted set of static main-data cheats with a matching build ID. Code patches, pointers, conditions and button cheats are rejected.";
    if(section==0) return @"Opacity changes immediately and is saved for this game. At 0%, touch input still works. The bottom-center gear follows the touch controls when a controller connects or disconnects. Opening settings does not pause the game.";
    if(section==1) {
        if(!NBScaleSupported()) return @"This game/version does not expose a supported runtime render-resolution control. Other settings still work. Use the original launcher for additional game-specific options.";
#if NB_BACKEND == 0
        return @"Render scale affects the internal scene only where this game supports it. 75% of a 720p base is 540p; other base resolutions differ. Explicit env.txt overrides take priority. Close and reopen the app to apply graphics changes.";
#elif NB_BACKEND == 1
        return @"Internal scene resolution: 540p is upscaled to the 720p canvas; 720p renders at the native canvas size. Close and reopen the app to apply graphics changes. Higher resolution uses more GPU and memory.";
#else
        return @"Mush8 supports handheld 720p and docked 1080p. 540p is not supported by this backend. Close and reopen the app to apply graphics changes.";
#endif
    }
    return @"Resets only NativeBridge settings. Saves and the original launcher settings are kept. Graphics reset takes effect after restarting.";
}
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell=[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    cell.selectionStyle=UITableViewCellSelectionStyleNone;
    NSUserDefaults *d=NSUserDefaults.standardUserDefaults;
    if(indexPath.section==3) {
        cell.textLabel.text=indexPath.row==0 ? @"Import save file or folder…" : indexPath.row==1 ? @"Import cheat files…" : @"Enable / disable cheats…";
        cell.detailTextLabel.text=indexPath.row==0 ? ([d stringForKey:@"NativeBridge.lastSaveImport"] ?: @"Back up and replace current save on restart") : indexPath.row==1 ? @"Zelda: compatible main-data writes only" : @"Imported cheats start disabled";
        cell.accessoryType=UITableViewCellAccessoryDisclosureIndicator;
    } else if(indexPath.section==0 && indexPath.row==0) {
        cell.textLabel.text=@"Controls opacity";
        cell.detailTextLabel.text=[NSString stringWithFormat:@"%ld%%",(long)lround([d doubleForKey:NBOpacityKey]*100)];
        UISlider *slider=[[UISlider alloc] initWithFrame:CGRectMake(0,0,180,36)]; slider.minimumValue=0; slider.maximumValue=1; slider.value=[d doubleForKey:NBOpacityKey]; slider.accessibilityLabel=@"On-screen controls opacity";
        [slider addTarget:self action:@selector(opacityChanged:) forControlEvents:UIControlEventValueChanged]; cell.accessoryView=slider;
    } else if(indexPath.section==0) {
        cell.textLabel.text=@"Keep screen awake"; UISwitch *toggle=[UISwitch new]; toggle.on=[d boolForKey:NBKeepAwakeKey];
        [toggle addTarget:self action:@selector(awakeChanged:) forControlEvents:UIControlEventValueChanged]; cell.accessoryView=toggle;
    } else if(indexPath.section==1 && indexPath.row==0) {
        cell.textLabel.text=@"Rendering resolution"; cell.detailTextLabel.text=NBScaleSupported() ? NBRenderLabel() : @"Unavailable for this game/version";
        cell.selectionStyle=NBScaleSupported() ? UITableViewCellSelectionStyleDefault : UITableViewCellSelectionStyleNone;
        cell.accessoryType=NBScaleSupported() ? UITableViewCellAccessoryDisclosureIndicator : UITableViewCellAccessoryNone;
    } else if(indexPath.section==1 && indexPath.row==1) {
        cell.textLabel.text=@"Frame-rate target";
        NSString *fps=[d stringForKey:NBFPSKey] ?: @"default";
        cell.detailTextLabel.text=NBFPSIsSupported() ? ([fps isEqual:@"default"] ? @"Game / launcher default" : [fps stringByAppendingString:@" FPS · restart to apply"]) : @"30/60 switching unavailable for this game";
        cell.accessoryType=NBFPSIsSupported() ? UITableViewCellAccessoryDisclosureIndicator : UITableViewCellAccessoryNone;
        cell.selectionStyle=NBFPSIsSupported() ? UITableViewCellSelectionStyleDefault : UITableViewCellSelectionStyleNone;
    } else if(indexPath.section==1 && indexPath.row==2) {
        cell.textLabel.text=@"Fit screen without stretching";
        NSString *fit=[d stringForKey:NBAspectKey] ?: @"default";
        cell.detailTextLabel.text=NBAspectSupported() ? (@{@"fill":@"Match screen · restart to apply",@"16:9":@"Original 16:9",@"default":@"Game default"}[fit]) : @"This game/version has no verified camera-aspect patch";
        cell.accessoryType=NBAspectSupported() ? UITableViewCellAccessoryDisclosureIndicator : UITableViewCellAccessoryNone;
        cell.selectionStyle=NBAspectSupported() ? UITableViewCellSelectionStyleDefault : UITableViewCellSelectionStyleNone;
    } else if(indexPath.section==1 && indexPath.row==3) {
        cell.textLabel.text=@"Performance overlay";
        NSString *mode=[d stringForKey:NBOverlayKey] ?: @"default";
        cell.detailTextLabel.text=NBOverlaySupported() ? @{@"default":@"Game default",@"full":@"Full details",@"fps":@"FPS only",@"off":@"Off"}[mode] : @"Not exposed by this backend";
        cell.accessoryType=NBOverlaySupported() ? UITableViewCellAccessoryDisclosureIndicator : UITableViewCellAccessoryNone;
    } else if(indexPath.section==1) {
        cell.textLabel.text=@"Apply graphics changes"; cell.detailTextLabel.text=@"Close the game from the app switcher, then reopen.";
    } else { cell.textLabel.text=@"Reset NativeBridge settings"; cell.textLabel.textColor=UIColor.systemRedColor; cell.selectionStyle=UITableViewCellSelectionStyleDefault; }
    return cell;
}
- (void)opacityChanged:(UISlider *)slider {
    [NSUserDefaults.standardUserDefaults setDouble:slider.value forKey:NBOpacityKey];
    UITableViewCell *cell=[self.tableView cellForRowAtIndexPath:[NSIndexPath indexPathForRow:0 inSection:0]];
    cell.detailTextLabel.text=[NSString stringWithFormat:@"%ld%%",(long)lround(slider.value*100)]; [[NBGameSettings shared] refresh];
}
- (void)awakeChanged:(UISwitch *)toggle { [NSUserDefaults.standardUserDefaults setBool:toggle.on forKey:NBKeepAwakeKey]; [[NBGameSettings shared] refresh]; }
- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    if(indexPath.section==3) {
        if(indexPath.row==2) {
            NSArray *groups=[NSUserDefaults.standardUserDefaults arrayForKey:NBCheatsKey];
            if (!groups.count) { [self notice:@"No active-compatible cheats imported" text:@"Import a matching BUILD_ID.txt file first. Supported cheats start disabled. Unsupported codes are rejected with a reason."]; return; }
            UIAlertController *menu=[UIAlertController alertControllerWithTitle:@"Cheats" message:@"Toggle one cheat at a time. Disabling stops future writes; restart or reload your save to reset values already changed." preferredStyle:UIAlertControllerStyleActionSheet];
            for (NSUInteger i=0;i<groups.count;i++) {
                NSDictionary *group=groups[i];
                NSString *title=[NSString stringWithFormat:@"%@ · %@",group[@"name"],group[@"error"] ? @"UNSUPPORTED" : [group[@"enabled"] boolValue] ? @"ON" : @"OFF"];
                [menu addAction:[UIAlertAction actionWithTitle:title style:UIAlertActionStyleDefault handler:^(UIAlertAction *a) {
                    if(group[@"error"]) { [self notice:@"Unsupported cheat" text:group[@"error"]]; return; }
                    NSMutableArray *updated=[groups mutableCopy]; NSMutableDictionary *entry=[group mutableCopy]; entry[@"enabled"]=@(![group[@"enabled"] boolValue]); updated[i]=entry;
                    NSString *failure=NBCheckCheatWrites(updated);
                    if (failure) [self notice:@"Cannot enable cheat" text:failure]; else [NSUserDefaults.standardUserDefaults setObject:updated forKey:NBCheatsKey];
                }]];
            }
            [menu addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
            menu.popoverPresentationController.sourceView=[tableView cellForRowAtIndexPath:indexPath]; menu.popoverPresentationController.sourceRect=menu.popoverPresentationController.sourceView.bounds;
            [self presentViewController:menu animated:YES completion:nil]; return;
        }
        if(indexPath.row==1) { [self pickImport:YES destination:@""]; return; }
        UIAlertController *confirm=[UIAlertController alertControllerWithTitle:@"Import a save" message:@"Choose unpacked save contents for this game/version. On restart, the current save will be moved to a backup and replaced. The game continues running until you close it." preferredStyle:UIAlertControllerStyleAlert];
#if NB_BACKEND == 2
        for (NSString *version in @[@"4.0.1",@"3.0.5"]) [confirm addAction:[UIAlertAction actionWithTitle:[@"Mush8 native save " stringByAppendingString:version] style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){ [self pickImport:NO destination:[version isEqual:@"4.0.1"] ? @"work/save" : @"work/save-3.0.5"]; }]];
#else
        [confirm addAction:[UIAlertAction actionWithTitle:@"Choose save…" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){ [self pickImport:NO destination:NB_BACKEND==1 ? @"zelda-user" : @"save"]; }]];
#endif
        [confirm addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
        [self presentViewController:confirm animated:YES completion:nil]; return;
    }
    if(indexPath.section==2) {
        for(NSString *key in @[NBOpacityKey,NBKeepAwakeKey,NBRenderKey,NBFPSKey,NBAspectKey,NBOverlayKey]) [NSUserDefaults.standardUserDefaults removeObjectForKey:key];
        [[NBGameSettings shared] refresh]; [tableView reloadData]; return;
    }
    if(indexPath.section==1 && indexPath.row==3 && NBOverlaySupported()) {
        UIAlertController *menu=[UIAlertController alertControllerWithTitle:@"Performance overlay" message:@"Close and reopen the game to apply." preferredStyle:UIAlertControllerStyleActionSheet];
        NSArray *values=@[@"default",@"full",@"fps",@"off"], *labels=@[@"Game default",@"Full details",@"FPS only",@"Off"];
        for (NSUInteger i=0;i<values.count;i++) { NSString *v=values[i]; [menu addAction:[UIAlertAction actionWithTitle:labels[i] style:UIAlertActionStyleDefault handler:^(UIAlertAction *a) { [NSUserDefaults.standardUserDefaults setObject:v forKey:NBOverlayKey]; [self.tableView reloadData]; }]]; }
        [menu addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
        menu.popoverPresentationController.sourceView=[tableView cellForRowAtIndexPath:indexPath]; menu.popoverPresentationController.sourceRect=menu.popoverPresentationController.sourceView.bounds;
        [self presentViewController:menu animated:YES completion:nil]; return;
    }
    if(indexPath.section==1 && indexPath.row==2 && NBAspectSupported()) {
        UIAlertController *menu=[UIAlertController alertControllerWithTitle:@"Screen shape" message:@"Uses this game's camera/aspect patch to match the screen without stretching. Some menus or cutscenes may retain borders. Restart to apply." preferredStyle:UIAlertControllerStyleActionSheet];
        NSArray *values=@[@"default",@"fill",@"16:9"], *labels=@[@"Game default",@"Match my screen",@"Original 16:9"];
        for (NSUInteger i=0;i<values.count;i++) { NSString *v=values[i]; [menu addAction:[UIAlertAction actionWithTitle:labels[i] style:UIAlertActionStyleDefault handler:^(UIAlertAction *a) { [NSUserDefaults.standardUserDefaults setObject:v forKey:NBAspectKey]; [[NBGameSettings shared] refresh]; [self.tableView reloadData]; }]]; }
        [menu addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
        menu.popoverPresentationController.sourceView=[tableView cellForRowAtIndexPath:indexPath]; menu.popoverPresentationController.sourceRect=menu.popoverPresentationController.sourceView.bounds;
        [self presentViewController:menu animated:YES completion:nil]; return;
    }
    if(indexPath.section==1 && indexPath.row==1 && NBFPSIsSupported()) {
        UIAlertController *fpsMenu=[UIAlertController alertControllerWithTitle:@"Frame-rate target" message:@"Close and reopen the game to apply. 60 FPS is a target, not a guarantee; performance depends on the game and device." preferredStyle:UIAlertControllerStyleActionSheet];
        for(NSString *value in @[@"default",@"30",@"60"]) {
            NSString *title=[value isEqual:@"default"] ? @"Game / launcher default" : [value stringByAppendingString:@" FPS"];
            [fpsMenu addAction:[UIAlertAction actionWithTitle:title style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) { [NSUserDefaults.standardUserDefaults setObject:value forKey:NBFPSKey]; [self.tableView reloadData]; }]];
        }
        [fpsMenu addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
        fpsMenu.popoverPresentationController.sourceView=[tableView cellForRowAtIndexPath:indexPath]; fpsMenu.popoverPresentationController.sourceRect=fpsMenu.popoverPresentationController.sourceView.bounds;
        [self presentViewController:fpsMenu animated:YES completion:nil]; return;
    }
    if(indexPath.section!=1 || indexPath.row!=0 || !NBScaleSupported()) return;
    NSArray *values; NSArray *labels;
#if NB_BACKEND == 0
    values=@[@"default",@"0.5",@"0.75",@"1.0"]; labels=@[@"Game / launcher default",@"50% of base resolution",@"75% (540p from a 720p base)",@"100% (720p from a 720p base)"];
#elif NB_BACKEND == 1
    values=@[@"default",@"960x540",@"1280x720"]; labels=@[@"Game default",@"540p · 960 × 540",@"Native 720p · 1280 × 720"];
#else
    values=@[@"default",@"720",@"1080"]; labels=@[@"Launcher default",@"720p · Handheld",@"1080p · Docked"];
#endif
    UIAlertController *alert=[UIAlertController alertControllerWithTitle:@"Rendering resolution" message:@"Saved for the next game launch." preferredStyle:UIAlertControllerStyleActionSheet];
    for(NSUInteger i=0;i<values.count;i++) {
        NSString *value=values[i];
        [alert addAction:[UIAlertAction actionWithTitle:labels[i] style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) { [NSUserDefaults.standardUserDefaults setObject:value forKey:NBRenderKey]; [self.tableView reloadData]; }]];
    }
#if NB_BACKEND == 1 && defined(TOTK_GAME_V143)
    [alert addAction:[UIAlertAction actionWithTitle:@"Custom resolution…" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        UIAlertController *custom=[UIAlertController alertControllerWithTitle:@"Custom render resolution" message:@"Enter WIDTHxHEIGHT, for example 1600x740. Even dimensions from 64 to 4096, at most 4 megapixels. Use Match my screen for the camera aspect ratio. Restart to apply." preferredStyle:UIAlertControllerStyleAlert];
        [custom addTextFieldWithConfigurationHandler:^(UITextField *field) { field.placeholder=@"1600x740"; field.autocorrectionType=UITextAutocorrectionTypeNo; }];
        [custom addAction:[UIAlertAction actionWithTitle:@"Save" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a) {
            NSString *value=[custom.textFields.firstObject.text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
            if (NBCustomSize(value)) { [NSUserDefaults.standardUserDefaults setObject:value forKey:NBRenderKey]; [self.tableView reloadData]; }
            else { UIAlertController *error=[UIAlertController alertControllerWithTitle:@"Invalid resolution" message:@"Use even WIDTHxHEIGHT values within the limits. Your previous setting was kept." preferredStyle:UIAlertControllerStyleAlert]; [error addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]]; [self presentViewController:error animated:YES completion:nil]; }
        }]];
        [custom addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
        [self presentViewController:custom animated:YES completion:nil];
    }]];
#endif
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    alert.popoverPresentationController.sourceView=[tableView cellForRowAtIndexPath:indexPath]; alert.popoverPresentationController.sourceRect=alert.popoverPresentationController.sourceView.bounds;
    [self presentViewController:alert animated:YES completion:nil];
}
@end
