// NativeBridge in-game settings. Compiled into the game host, never into game data.
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <GameController/GameController.h>
#include <stdlib.h>
#include <math.h>
#ifndef NB_BACKEND
#error Define NB_BACKEND: 0=nxnative, 1=MacNativeGames, 2=Mush8
#endif
static NSString *const NBOpacityKey = @"NativeBridge.controlsOpacity";
static NSString *const NBFPSKey = @"NativeBridge.fpsTarget";
static NSString *const NBRenderKey = @"NativeBridge.renderChoice";
static NSString *const NBKeepAwakeKey = @"NativeBridge.keepAwake";
static NSString *NBRenderLabel(void) {
    NSString *value=[NSUserDefaults.standardUserDefaults stringForKey:NBRenderKey] ?: @"default";
    return @{@"default":@"Game / launcher default",@"0.5":@"50% of base resolution",@"0.75":@"75% of base resolution",@"1.0":@"100% of base resolution",@"960x540":@"540p · 960 × 540",@"1280x720":@"Native 720p · 1280 × 720",@"720":@"720p · Handheld",@"1080":@"1080p · Docked"}[value] ?: @"Game / launcher default";
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
void nb_apply_settings(void) {
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
#else
    if ([choice isEqual:@"720"]) setenv("NX_HANDHELD","1",1);
    if ([choice isEqual:@"1080"]) unsetenv("NX_HANDHELD");
#endif
}
@interface NBSettingsPanel : UITableViewController
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
- (void)done { [self dismissViewControllerAnimated:YES completion:nil]; }
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView { return 3; }
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section { return section==2 ? 1 : (section==1 ? 3 : 2); }
- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section { return @[@"On-screen controls",@"Graphics",@"Restore defaults"][section]; }
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
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
    if(indexPath.section==0 && indexPath.row==0) {
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
    if(indexPath.section==2) {
        for(NSString *key in @[NBOpacityKey,NBKeepAwakeKey,NBRenderKey,NBFPSKey]) [NSUserDefaults.standardUserDefaults removeObjectForKey:key];
        [[NBGameSettings shared] refresh]; [tableView reloadData]; return;
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
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    alert.popoverPresentationController.sourceView=[tableView cellForRowAtIndexPath:indexPath]; alert.popoverPresentationController.sourceRect=alert.popoverPresentationController.sourceView.bounds;
    [self presentViewController:alert animated:YES completion:nil];
}
@end
