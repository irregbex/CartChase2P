#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <GameController/GameController.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "ios_audio_ring.h"
#include "ios_viewport.h"
#include "ios_touch_button.h"
#import "ios_graphics.h"
#include "dkc1_game.h"
#include "dkc1_video.h"
#include "desktop_sram.h"
#include "verified_rom.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "dkc1_script.h"
#include <unistd.h>
#include <math.h>

static const double kGameHz = 60.098811862;
static const int kAudioRate = 32040;
static uint8_t s_pixels[kDkc1VideoMaxWidth * kDkc1VideoHeight * 4];
static int s_renderWidth = kDkc1VideoNativeWidth;
static uint8_t *s_rom;
static Dkc1SramStore s_sram;
static Dkc1IOSAudioRing s_audio;

@interface DKCButton : UIButton {
    Dkc1IOSTouchButton _input;
}
@property(nonatomic) uint32_t mask;
@property(nonatomic, strong) UIColor *accent;
@property(nonatomic, strong) UIImpactFeedbackGenerator *feedback;
@property(nonatomic) BOOL overlay;
@property(nonatomic, strong) NSMutableSet<UITouch *> *activeTouches;
@property(nonatomic, strong) NSMutableSet<UITouch *> *rolledTouches;
@property(nonatomic, weak) DKCButton *rollTarget;
@property(nonatomic) CGPoint touchOrigin;
@property(nonatomic, readonly) BOOL inputPressed;
- (void)resetInput;
- (void)setRolledTouch:(UITouch *)touch pressed:(BOOL)pressed;
@end
@implementation DKCButton
- (BOOL)inputPressed { return Dkc1IOSTouchPressed(&_input); }
- (void)refreshInputAppearance {
    self.highlighted = self.inputPressed;
    self.accessibilityValue = _input.latched ? @"Hold on" : @"Hold off";
    [self setNeedsDisplay];
}
- (void)resetInput {
    for (UITouch *touch in self.activeTouches)
        [self.rollTarget setRolledTouch:touch pressed:NO];
    [self.activeTouches removeAllObjects];
    [self.rolledTouches removeAllObjects];
    Dkc1IOSTouchCancel(&_input);
    [self refreshInputAppearance];
}
- (void)setRolledTouch:(UITouch *)touch pressed:(BOOL)pressed {
    BOOL wasPressed = [self.rolledTouches containsObject:touch];
    if (wasPressed == pressed) return;
    if (pressed) {
        if (!self.rolledTouches) self.rolledTouches = [NSMutableSet set];
        [self.rolledTouches addObject:touch];
        Dkc1IOSTouchRollBegin(&_input);
    } else {
        [self.rolledTouches removeObject:touch];
        Dkc1IOSTouchRollEnd(&_input);
    }
    [self refreshInputAppearance];
}
- (void)updateRollingTouch:(UITouch *)touch {
    DKCButton *target = self.rollTarget;
    if (!target || target.hidden || !target.enabled) return;
    CGPoint point = [touch locationInView:target.superview];
    BOOL pressed = Dkc1IOSTouchRollHits(point.x-target.center.x, point.y-target.center.y,
        target.bounds.size.width, target.bounds.size.height, touch.majorRadius,
        [target.rolledTouches containsObject:touch]);
    [target setRolledTouch:touch pressed:pressed];
    if (pressed) _input.tap_candidate = false;
}
#if DKC1_IOS_DIAGNOSTICS
- (void)latchForQA {
    Dkc1IOSTouchBegin(&_input, 1);
    Dkc1IOSTouchEnd(&_input, 1.05, true);
    Dkc1IOSTouchBegin(&_input, 1.2);
    Dkc1IOSTouchEnd(&_input, 1.25, true);
    [self refreshInputAppearance];
}
- (void)rollForQA {
    Dkc1IOSTouchBegin(&_input, 1);
    _input.tap_candidate = false;
    Dkc1IOSTouchRollBegin(&self.rollTarget->_input);
    [self refreshInputAppearance];
    [self.rollTarget refreshInputAppearance];
}
#endif
// Track actual touch lifetimes, independently for every button. UIButton's
// highlight is visual state and must not be the emulated controller state.
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!self.activeTouches) self.activeTouches = [NSMutableSet set];
    for (UITouch *touch in touches) {
        if ([self.activeTouches containsObject:touch]) continue;
        if (!self.activeTouches.count) self.touchOrigin = [touch locationInView:self];
        [self.activeTouches addObject:touch];
        Dkc1IOSTouchBegin(&_input, touch.timestamp);
        [self updateRollingTouch:touch];
    }
    [self refreshInputAppearance];
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        if (![self.activeTouches containsObject:touch]) continue;
        CGPoint point = [touch locationInView:self];
        if (hypot(point.x-self.touchOrigin.x, point.y-self.touchOrigin.y) > 18)
            _input.tap_candidate = false;
        [self updateRollingTouch:touch];
    }
    // A held thumb may drift while another finger jumps. Release only when
    // that touch ends or is cancelled, not when its visual highlight changes.
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    BOOL wasLatched = _input.latched;
    for (UITouch *touch in touches) {
        if (![self.activeTouches containsObject:touch]) continue;
        [self.rollTarget setRolledTouch:touch pressed:NO];
        [self.activeTouches removeObject:touch];
        Dkc1IOSTouchEnd(&_input, touch.timestamp, self.mask == 2);
    }
    [self refreshInputAppearance];
    if (wasLatched != _input.latched &&
        ![NSUserDefaults.standardUserDefaults boolForKey:@"DisableTouchHaptics"]) {
        UINotificationFeedbackGenerator *feedback = [[UINotificationFeedbackGenerator alloc] init];
        [feedback notificationOccurred:UINotificationFeedbackTypeSuccess];
    }
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self resetInput];
}
- (void)setHighlighted:(BOOL)highlighted {
    BOOL began = highlighted && !self.highlighted;
    [super setHighlighted:highlighted];
    if (began && ![NSUserDefaults.standardUserDefaults boolForKey:@"DisableTouchHaptics"]) {
        if (!self.feedback) self.feedback = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleSoft];
        [self.feedback impactOccurredWithIntensity:0.65];
    }
    self.transform = highlighted ? CGAffineTransformMakeScale(0.94, 0.94) : CGAffineTransformIdentity;
    [self setNeedsDisplay];
}
- (void)drawRect:(CGRect)rect {
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGRect inset = CGRectInset(self.bounds, 3, 3);
    CGFloat radius = MIN(inset.size.width, inset.size.height)/2;
    UIBezierPath *shape = [UIBezierPath bezierPathWithRoundedRect:inset cornerRadius:radius];
    if (self.overlay) {
        UIColor *hold = [UIColor colorWithRed:0.6 green:0.95 blue:0.65 alpha:1];
        [(_input.latched ? [hold colorWithAlphaComponent:0.24] :
            [UIColor colorWithWhite:1 alpha:self.highlighted ? 0.33 : 0.09]) setFill];
        [shape fill];
        [(_input.latched ? hold : [UIColor colorWithWhite:1 alpha:self.highlighted ? 0.8 : 0.32]) setStroke];
        shape.lineWidth = _input.latched ? 2 : 1; [shape stroke];
        if (_input.latched) {
            NSString *label = @"HOLD";
            NSDictionary *style = @{NSFontAttributeName:[UIFont systemFontOfSize:7 weight:UIFontWeightBold],
                                    NSForegroundColorAttributeName:hold};
            CGSize size = [label sizeWithAttributes:style];
            [label drawAtPoint:CGPointMake((self.bounds.size.width-size.width)/2,
                                           self.bounds.size.height*0.73) withAttributes:style];
        }
        return;
    }
    CGContextSaveGState(context);
    [shape addClip];
    UIColor *accent = self.accent ?: [UIColor colorWithWhite:0.6 alpha:1];
    NSArray *colors = self.highlighted ? @[(id)[accent colorWithAlphaComponent:0.5].CGColor, (id)[UIColor colorWithWhite:0.09 alpha:1].CGColor]
        : @[(id)[UIColor colorWithWhite:0.22 alpha:1].CGColor, (id)[UIColor colorWithWhite:0.09 alpha:1].CGColor];
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGGradientRef gradient = CGGradientCreateWithColors(space, (__bridge CFArrayRef)colors, NULL);
    CGContextDrawLinearGradient(context, gradient, CGPointMake(0,0), CGPointMake(0,self.bounds.size.height), 0);
    CGGradientRelease(gradient); CGColorSpaceRelease(space);
    CGContextRestoreGState(context);
    [[accent colorWithAlphaComponent:self.highlighted ? 0.95 : 0.36] setStroke];
    shape.lineWidth = 1.2; [shape stroke];
}
@end

/* Continuous direction tracking allows diagonals and sliding without lifting
 * a thumb. UITouch identity keeps simultaneous buttons independent. */
@interface DKCDirectionPad : UIView
@property(nonatomic) uint32_t mask;
@property(nonatomic, strong) UISelectionFeedbackGenerator *feedback;
@property(nonatomic) BOOL overlay;
@end
@implementation DKCDirectionPad
- (void)drawRect:(CGRect)rect {
    CGFloat w = CGRectGetWidth(self.bounds), h = CGRectGetHeight(self.bounds);
    UIBezierPath *rim = [UIBezierPath bezierPathWithOvalInRect:CGRectInset(self.bounds, 1, 1)];
    [[UIColor colorWithWhite:self.overlay ? 1 : 0.025 alpha:self.overlay ? 0.025 : 0.7] setFill]; [rim fill];
    [[UIColor colorWithWhite:0.7 alpha:0.12] setStroke]; [rim stroke];
    UIBezierPath *cross = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(w*.34, h*.08, w*.32, h*.84) cornerRadius:9];
    [cross appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake(w*.08, h*.34, w*.84, h*.32) cornerRadius:9]];
    [(self.overlay ? [UIColor colorWithWhite:1 alpha:0.12] : [UIColor colorWithRed:0.15 green:0.18 blue:0.17 alpha:1]) setFill]; [cross fill];
    const uint32_t masks[] = {0x10,0x20,0x40,0x80};
    NSArray *arrows = @[@"▲",@"▼",@"◀",@"▶"];
    CGPoint positions[] = {{w/2-8,h*.11},{w/2-8,h*.74},{w*.11,h/2-10},{w*.77,h/2-10}};
    for (int i=0;i<4;i++) {
        UIColor *ink = self.mask & masks[i] ? [UIColor colorWithRed:0.63 green:0.92 blue:0.65 alpha:1] : [UIColor colorWithWhite:self.overlay ? 1 : 0.65 alpha:self.overlay ? 0.7 : 1];
        [arrows[i] drawAtPoint:positions[i] withAttributes:@{NSFontAttributeName:[UIFont systemFontOfSize:17],NSForegroundColorAttributeName:ink}];
    }
    [[UIColor colorWithWhite:self.overlay ? 1 : 0.07 alpha:self.overlay ? 0.2 : 1] setFill];
    [[UIBezierPath bezierPathWithOvalInRect:CGRectMake(w*.45,h*.45,w*.1,h*.1)] fill];
}
- (void)updateTouch:(UITouch *)touch {
    CGPoint p = [touch locationInView:self];
    CGFloat x = (p.x / self.bounds.size.width - 0.5) * 2;
    CGFloat y = (p.y / self.bounds.size.height - 0.5) * 2;
    uint32_t previous = self.mask;
    self.mask = (x < -0.25 ? 0x40 : x > 0.25 ? 0x80 : 0) |
                (y < -0.25 ? 0x10 : y > 0.25 ? 0x20 : 0);
    if (self.mask != previous && ![NSUserDefaults.standardUserDefaults boolForKey:@"DisableTouchHaptics"]) {
        if (!self.feedback) self.feedback = [[UISelectionFeedbackGenerator alloc] init];
        [self.feedback selectionChanged];
    }
    [self setNeedsDisplay];
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self updateTouch:touches.anyObject];
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self updateTouch:touches.anyObject];
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { self.mask = 0; [self setNeedsDisplay]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { self.mask = 0; [self setNeedsDisplay]; }
@end

@interface DKCPauseController : UIViewController
@property(nonatomic, copy) void (^action)(NSInteger);
@property(nonatomic, copy) void (^upscaling)(UIViewController *);
@property(nonatomic) BOOL hasSave;
@property(nonatomic) BOOL failed;
@end
@implementation DKCPauseController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.06 green:0.09 blue:0.08 alpha:1];
    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    UIScrollView *scroll = [[UIScrollView alloc] init];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:scroll];
    [NSLayoutConstraint activateConstraints:@[
        [scroll.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor],
        [scroll.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor]]];
    UIStackView *stack = [[UIStackView alloc] init];
    stack.axis = UILayoutConstraintAxisVertical; stack.spacing = 14;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [scroll addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:24],
        [stack.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:26],
        [stack.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-26],
        [stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-24],
        [stack.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-52]]];
    UILabel *eyebrow = [[UILabel alloc] init];
    eyebrow.text = @"D K C 1  /  P O C K E T";
    eyebrow.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightMedium];
    eyebrow.textColor = [UIColor colorWithRed:0.58 green:0.8 blue:0.63 alpha:1];
    [stack addArrangedSubview:eyebrow];
    UILabel *title = [[UILabel alloc] init]; title.text = @"Take a breather.";
    title.font = [UIFont systemFontOfSize:32 weight:UIFontWeightBold]; title.textColor = UIColor.whiteColor;
    [stack addArrangedSubview:title];
    UILabel *hint = [[UILabel alloc] init];
    hint.text = @"Hold Y and press B to run and jump. Double-tap Y to toggle Hold. Swap with A.";
    hint.font = [UIFont systemFontOfSize:14]; hint.textColor = [UIColor colorWithWhite:0.65 alpha:1];
    hint.numberOfLines = 0; [stack addArrangedSubview:hint];
    NSArray *titles = @[@"Continue playing", @"Save your place", @"Load saved state", @"Upscaling"];
    NSArray *symbols = @[@"play.fill", @"square.and.arrow.down", @"arrow.counterclockwise", @"sparkles.tv"];
    for (NSInteger i=0;i<4;i++) {
        UIButtonConfiguration *config = [UIButtonConfiguration filledButtonConfiguration];
        config.title = titles[i]; config.image = [UIImage systemImageNamed:symbols[i]];
        config.imagePadding = 14; config.contentInsets = NSDirectionalEdgeInsetsMake(17,20,17,20);
        config.cornerStyle = UIButtonConfigurationCornerStyleLarge;
        config.baseBackgroundColor = i == 0 ? [UIColor colorWithRed:0.65 green:0.9 blue:0.58 alpha:1] : [UIColor colorWithWhite:0.15 alpha:1];
        config.baseForegroundColor = i == 0 ? [UIColor colorWithWhite:0.06 alpha:1] : UIColor.whiteColor;
        UIButton *button = [UIButton buttonWithConfiguration:config primaryAction:nil];
        button.tag = i; button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
        button.enabled = !self.failed && (i != 2 || self.hasSave);
        [button addTarget:self action:@selector(selected:) forControlEvents:UIControlEventTouchUpInside];
        [stack addArrangedSubview:button];
    }
    UIStackView *row = [[UIStackView alloc] init]; row.axis = UILayoutConstraintAxisHorizontal;
    UILabel *label = [[UILabel alloc] init]; label.text = @"Touch feedback"; label.textColor = UIColor.whiteColor;
    label.font = [UIFont systemFontOfSize:15];
    UISwitch *toggle = [[UISwitch alloc] init]; toggle.on = ![NSUserDefaults.standardUserDefaults boolForKey:@"DisableTouchHaptics"];
    toggle.onTintColor = [UIColor colorWithRed:0.38 green:0.65 blue:0.43 alpha:1];
    [toggle addTarget:self action:@selector(hapticsChanged:) forControlEvents:UIControlEventValueChanged];
    [row addArrangedSubview:label]; [row addArrangedSubview:toggle]; [stack addArrangedSubview:row];
    UILabel *footer = [[UILabel alloc] init]; footer.text = @"Candy’s saves are kept automatically.\nSave State keeps your exact place in the game.";
    footer.numberOfLines = 0; footer.font = [UIFont systemFontOfSize:12];
    footer.textColor = [UIColor colorWithWhite:0.5 alpha:1]; [stack addArrangedSubview:footer];
}
- (void)selected:(UIButton *)sender {
    if (sender.tag == 3) { if (self.upscaling) self.upscaling(self); }
    else self.action(sender.tag);
}
- (void)hapticsChanged:(UISwitch *)sender { [NSUserDefaults.standardUserDefaults setBool:!sender.on forKey:@"DisableTouchHaptics"]; }
@end

@interface DKCViewController : UIViewController <UIDocumentPickerDelegate>
@property(nonatomic, strong) DKCMetalView *screen;
@property(nonatomic, strong) UILabel *welcome;
@property(nonatomic, strong) UILabel *brand;
@property(nonatomic, strong) UILabel *subtitle;
@property(nonatomic, strong) CAGradientLayer *background;
@property(nonatomic, strong) UIButton *importButton;
@property(nonatomic, strong) UIView *controlDeck;
@property(nonatomic, strong) UIButton *menuButton;
@property(nonatomic, strong) DKCDirectionPad *pad;
@property(nonatomic, strong) NSMutableArray<DKCButton *> *buttons;
@property(nonatomic, strong) CADisplayLink *displayLink;
@property(nonatomic, strong) AVAudioEngine *audioEngine;
@property(nonatomic, strong) AVAudioSourceNode *audioSource;
@property(nonatomic, strong) NSString *documents;
@property(nonatomic) BOOL loaded;
@property(nonatomic) BOOL paused;
@property(nonatomic) BOOL failed;
@property(nonatomic) BOOL saveErrorReported;
@property(nonatomic) BOOL wasPaused;
@property(nonatomic) BOOL controllerMenuPressed;
@property(nonatomic) BOOL startupActive;
@property(nonatomic) NSUInteger startupFrames;
@property(nonatomic) double previousTime;
@property(nonatomic) double elapsed;
@property(nonatomic) double audioFraction;
@end

@implementation DKCViewController
- (BOOL)prefersStatusBarHidden { return YES; }
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
#if DKC1_IOS_DIAGNOSTICS
    if (getenv("DKC1_IOS_QA_LANDSCAPE")) return UIInterfaceOrientationMaskLandscapeRight;
#endif
    return self.loaded ? UIInterfaceOrientationMaskLandscape : UIInterfaceOrientationMaskAllButUpsideDown;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.035 green:0.055 blue:0.065 alpha:1];
    self.view.multipleTouchEnabled = YES;
    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    self.background = [CAGradientLayer layer];
    self.background.colors = @[(id)[UIColor colorWithRed:0.09 green:0.16 blue:0.12 alpha:1].CGColor,
                               (id)[UIColor colorWithRed:0.025 green:0.035 blue:0.04 alpha:1].CGColor];
    [self.view.layer addSublayer:self.background];
    self.controlDeck = [[UIView alloc] init];
    self.controlDeck.backgroundColor = [UIColor colorWithWhite:0.12 alpha:0.25];
    self.controlDeck.layer.cornerRadius = 40;
    self.controlDeck.layer.borderWidth = 1;
    self.controlDeck.layer.borderColor = [UIColor colorWithWhite:0.7 alpha:0.08].CGColor;
    self.controlDeck.userInteractionEnabled = NO;
    [self.view addSubview:self.controlDeck];
    self.brand = [[UILabel alloc] init];
    self.brand.attributedText = [[NSAttributedString alloc] initWithString:@"DKC1  /  POCKET" attributes:@{
        NSFontAttributeName:[UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightSemibold],
        NSForegroundColorAttributeName:[UIColor colorWithRed:0.72 green:0.85 blue:0.75 alpha:1],
        NSKernAttributeName:@2}];
    [self.view addSubview:self.brand];
    self.documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    [NSFileManager.defaultManager createDirectoryAtPath:self.documents withIntermediateDirectories:YES attributes:nil error:nil];
    chdir(self.documents.fileSystemRepresentation);
    setenv("SNESRECOMP_TIER2_DIR", NSTemporaryDirectory().fileSystemRepresentation, 1);
    self.screen = [[DKCMetalView alloc] init];
    self.screen.settings = Dkc1IOSLoadGraphicsSettings();
#if DKC1_IOS_DIAGNOSTICS
    const char *upscaler = getenv("DKC1_IOS_QA_UPSCALER");
    if (upscaler) {
        Dkc1GraphicsSettings settings = self.screen.settings;
        settings.upscaler = atoi(upscaler); self.screen.settings = settings;
    }
#endif
    self.screen.backgroundColor = UIColor.blackColor;
    self.screen.userInteractionEnabled = NO;
    self.view.clipsToBounds = YES;
    self.screen.layer.magnificationFilter = kCAFilterNearest;
    self.screen.layer.minificationFilter = kCAFilterNearest;
    self.screen.layer.borderWidth = 1;
    self.screen.layer.borderColor = [UIColor colorWithWhite:0.8 alpha:0.15].CGColor;
    self.screen.layer.shadowOpacity = 0.7;
    self.screen.layer.shadowRadius = 22;
    self.screen.layer.shadowOffset = CGSizeMake(0,12);
    [self.view addSubview:self.screen];
    self.welcome = [[UILabel alloc] init];
    self.welcome.numberOfLines = 0;
    self.welcome.textAlignment = NSTextAlignmentCenter;
    self.welcome.textColor = UIColor.whiteColor;
    self.welcome.font = [UIFont systemFontOfSize:31 weight:UIFontWeightBold];
    self.welcome.text = @"A little adventure.\nAlways with you.";
    [self.view addSubview:self.welcome];
    self.subtitle = [[UILabel alloc] init]; self.subtitle.numberOfLines = 0;
    self.subtitle.text = @"Import your DKC USA v1.0 ROM from Files.\nPlay with touch or a paired controller.";
    self.subtitle.font = [UIFont systemFontOfSize:13];
    self.subtitle.textColor = [UIColor colorWithWhite:0.65 alpha:1]; self.subtitle.textAlignment = NSTextAlignmentCenter;
    [self.view addSubview:self.subtitle];
    UIButtonConfiguration *importConfig = [UIButtonConfiguration filledButtonConfiguration];
    importConfig.title = @"Open game"; importConfig.image = [UIImage systemImageNamed:@"folder.fill"];
    importConfig.imagePadding = 12; importConfig.cornerStyle = UIButtonConfigurationCornerStyleCapsule;
    importConfig.baseBackgroundColor = [UIColor colorWithRed:0.65 green:0.9 blue:0.58 alpha:1];
    importConfig.baseForegroundColor = [UIColor colorWithWhite:0.05 alpha:1];
    self.importButton = [UIButton buttonWithConfiguration:importConfig primaryAction:nil];
    [self.importButton addTarget:self action:@selector(openROM) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:self.importButton];
    self.pad = [[DKCDirectionPad alloc] init];
    self.pad.backgroundColor = UIColor.clearColor;
    self.pad.accessibilityLabel = @"Directional pad";
    [self.view addSubview:self.pad];
    self.buttons = [NSMutableArray array];
    NSArray *names = @[@"Y", @"B", @"X", @"A", @"SELECT", @"START", @"L", @"R"];
    const uint32_t masks[] = {2, 1, 0x200, 0x100, 4, 8, 0x400, 0x800};
    for (NSUInteger i = 0; i < names.count; ++i) {
        DKCButton *button = [DKCButton buttonWithType:UIButtonTypeCustom];
        button.mask = masks[i];
        button.exclusiveTouch = NO;
        button.multipleTouchEnabled = YES;
        [button setTitle:names[i] forState:UIControlStateNormal];
        button.accessibilityLabel = names[i];
        if (i == 0) button.accessibilityHint = @"Double tap to toggle holding Y. Press B at the same time to jump.";
        button.titleLabel.font = [UIFont systemFontOfSize:i < 4 ? 23 : 12 weight:UIFontWeightSemibold];
        button.backgroundColor = UIColor.clearColor;
        button.accent = i == 1 ? [UIColor colorWithRed:0.75 green:0.91 blue:0.51 alpha:1] :
            i == 0 ? [UIColor colorWithRed:0.96 green:0.76 blue:0.42 alpha:1] : [UIColor colorWithWhite:0.7 alpha:1];
        [button setTitleColor:button.accent forState:UIControlStateNormal];
        button.layer.shadowOpacity = 0.5; button.layer.shadowRadius = 4;
        button.layer.shadowOffset = CGSizeMake(0,3);
        button.opaque = NO;
        [self.view addSubview:button];
        [self.buttons addObject:button];
    }
    // UIKit keeps a touch with its starting view. Explicitly let a thumb
    // that starts on Y add/release B as its contact rolls across the diamond.
    self.buttons[0].rollTarget = self.buttons[1];
    self.menuButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [self.menuButton setTitle:@"Open ROM" forState:UIControlStateNormal];
    self.menuButton.titleLabel.font = [UIFont systemFontOfSize:17 weight:UIFontWeightSemibold];
    self.menuButton.tintColor = [UIColor colorWithRed:0.6 green:0.9 blue:0.65 alpha:1];
    [self.menuButton addTarget:self action:@selector(showMenu) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:self.menuButton];
    NSNotificationCenter *notifications = NSNotificationCenter.defaultCenter;
    [notifications addObserver:self selector:@selector(deactivate:) name:UIApplicationWillResignActiveNotification object:nil];
    [notifications addObserver:self selector:@selector(activate:) name:UIApplicationDidBecomeActiveNotification object:nil];
    [notifications addObserver:self selector:@selector(audioInterrupted:) name:AVAudioSessionInterruptionNotification object:nil];
    [notifications addObserver:self selector:@selector(audioConfigurationChanged:) name:AVAudioEngineConfigurationChangeNotification object:nil];
    [notifications addObserver:self selector:@selector(controllerDisconnected:) name:GCControllerDidDisconnectNotification object:nil];
    self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    self.displayLink.preferredFrameRateRange = CAFrameRateRangeMake(60, 120, 60);
    [self.displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    dispatch_async(dispatch_get_main_queue(), ^{
#if DKC1_IOS_DIAGNOSTICS
        if ([NSProcessInfo.processInfo.arguments containsObject:@"--verify-graphics"]) {
            [self runGraphicsVerification];
            return;
        }
        if ([NSProcessInfo.processInfo.arguments containsObject:@"--verify"]) {
            [self runVerification];
            return;
        }
#endif
        NSString *rom = [self.documents stringByAppendingPathComponent:@"game.sfc"];
        if ([NSFileManager.defaultManager fileExistsAtPath:rom]) [self loadROM:rom];
    });
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    self.background.frame = self.view.bounds;
    CGRect safe = UIEdgeInsetsInsetRect(self.view.bounds, self.view.safeAreaInsets);
    CGFloat w = safe.size.width, h = safe.size.height;
    BOOL landscape = w > h;
    BOOL overlay = landscape && self.loaded;
    self.brand.frame = CGRectMake(safe.origin.x+16, safe.origin.y+7, 250, 34);
    self.brand.hidden = overlay;
    CGFloat controls = MIN(158, landscape ? h * 0.51 : w * 0.38);
    CGRect area = landscape ? CGRectMake(safe.origin.x + controls + 12, safe.origin.y + 18,
                                          w - 2 * controls - 24, h - 36)
                            : CGRectMake(safe.origin.x + 10, safe.origin.y + 55, w - 20, h - controls - 140);
    CGFloat width = MIN(area.size.width, area.size.height * 4.0 / 3.0);
    self.screen.frame = CGRectMake(CGRectGetMidX(area) - width/2, CGRectGetMidY(area) - width*3/8,
                                   width, width*3/4);
    CGRect gameArea = overlay ? self.view.bounds : area;
    Dkc1IOSViewport viewport = Dkc1IOSMakeViewport(gameArea.size.width, gameArea.size.height, overlay);
    self.screen.frame = CGRectMake(CGRectGetMidX(gameArea)-viewport.display_width/2,
                                   CGRectGetMidY(gameArea)-viewport.display_height/2,
                                   viewport.display_width, viewport.display_height);
    [self.screen setNeedsLayout];
    if (self.loaded && s_renderWidth != viewport.source_width) {
        // UIKit layout and emulation both run on the main thread. Rebind before
        // rendering; the existing host resets width-dependent shadow history.
        Dkc1VideoSetRenderWidth(viewport.source_width);
        s_renderWidth = viewport.source_width;
        Dkc1BeginDrawing(s_pixels, s_renderWidth*4);
        Dkc1DrawPpuFrame();
        [self presentFrame];
    }
    self.screen.layer.borderWidth = overlay ? 0 : 1;
    self.screen.layer.shadowOpacity = overlay ? 0 : 0.7;
    self.welcome.frame = CGRectMake(area.origin.x, CGRectGetMidY(area)-100, area.size.width, 86);
    self.subtitle.frame = CGRectMake(area.origin.x, CGRectGetMidY(area)-12, area.size.width, 45);
    self.importButton.frame = CGRectMake(CGRectGetMidX(area)-95, CGRectGetMidY(area)+49, 190, 48);
    self.screen.hidden = !self.loaded;
    CGFloat bottom = CGRectGetMaxY(safe) - (landscape ? 25 : 65);
    self.controlDeck.hidden = landscape;
    self.controlDeck.frame = CGRectMake(safe.origin.x, bottom-controls-59, w, controls+110);
    self.pad.frame = CGRectMake(safe.origin.x + 8, bottom-controls, controls, controls);
    self.pad.overlay = overlay;
    [self.pad setNeedsDisplay];
    CGFloat button = MIN(58, controls * 0.37), cx = CGRectGetMaxX(safe)-controls/2-8;
    CGFloat cy = bottom-controls/2, spread = controls * 0.29;
    const CGFloat offsets[4][2] = {{-1,0},{0,1},{0,-1},{1,0}};
    for (NSUInteger i=0; i<4; ++i)
        self.buttons[i].frame = CGRectMake(cx+offsets[i][0]*spread-button/2,
                                           cy+offsets[i][1]*spread-button/2,button,button);
    CGFloat center = CGRectGetMidX(safe);
    self.buttons[4].frame = CGRectMake(center-80, CGRectGetMaxY(safe)-44, 72, 34);
    self.buttons[5].frame = CGRectMake(center+8, CGRectGetMaxY(safe)-44, 72, 34);
    if (landscape) {
        self.buttons[4].frame = CGRectMake(center-86, safe.origin.y+8, 76, 36);
        self.buttons[5].frame = CGRectMake(center+10, safe.origin.y+8, 76, 36);
    }
    self.buttons[6].frame = CGRectMake(safe.origin.x+12, bottom-controls-47, 62, 34);
    self.buttons[7].frame = CGRectMake(CGRectGetMaxX(safe)-74, bottom-controls-47, 62, 34);
    self.menuButton.frame = CGRectMake(CGRectGetMaxX(safe)-116, safe.origin.y+3, 106, 42);
    self.menuButton.backgroundColor = overlay ? [UIColor colorWithWhite:1 alpha:0.1] : UIColor.clearColor;
    self.menuButton.tintColor = overlay ? [UIColor colorWithWhite:1 alpha:0.8] : [UIColor colorWithRed:0.6 green:0.9 blue:0.65 alpha:1];
    self.menuButton.layer.cornerRadius = 21;
    for (DKCButton *buttonView in self.buttons) {
        buttonView.overlay = overlay;
        [buttonView setTitleColor:overlay ? [UIColor colorWithWhite:1 alpha:0.75] : buttonView.accent forState:UIControlStateNormal];
        [buttonView setNeedsDisplay];
    }
}

- (void)showError:(NSString *)message {
    [self setGamePaused:YES];
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"DKC1Recomp" message:message preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (BOOL)loadROM:(NSString *)path {
    char error[256] = {0};
    size_t size = 0;
    s_rom = Dkc1ReadVerifiedRom(path.fileSystemRepresentation, &size, error, sizeof error);
    if (!s_rom) { [self showError:@(error)]; return NO; }
    CGSize sizeInPoints = self.view.bounds.size;
    Dkc1IOSViewport viewport = Dkc1IOSMakeViewport(sizeInPoints.width, sizeInPoints.height,
                                                 sizeInPoints.width > sizeInPoints.height);
    s_renderWidth = viewport.source_width;
    Dkc1VideoSetRenderWidth(s_renderWidth);
    Dkc1VideoSetRom(s_rom, size);
    RtlRegisterGame(Dkc1GameInfo());
    if (!SnesInit(s_rom, (int)size)) {
        free(s_rom); s_rom = NULL;
        [self showError:@"The runtime could not initialize the verified ROM."];
        return NO;
    }
    Dkc1BeginDrawing(s_pixels, s_renderWidth*4);
    RtlSetAudioOutputRate(kAudioRate);
    self.loaded = YES;
    [self setNeedsUpdateOfSupportedInterfaceOrientations];
    UIWindowScene *scene = self.view.window.windowScene;
    UIInterfaceOrientationMask orientation = UIInterfaceOrientationIsLandscape(scene.interfaceOrientation)
        ? UIInterfaceOrientationMaskLandscape : UIInterfaceOrientationMaskLandscapeRight;
    [scene requestGeometryUpdateWithPreferences:
        [[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:orientation]
        errorHandler:^(NSError *error) { NSLog(@"Landscape orientation: %@", error); }];
    self.welcome.hidden = YES;
    self.subtitle.hidden = YES;
    self.importButton.hidden = YES;
    self.screen.hidden = NO;
    [self.view setNeedsLayout];
    [self.menuButton setTitle:@"Menu" forState:UIControlStateNormal];
    fprintf(stderr, "[ios] build=%s ROM=USA-v1.0 source=%dx224 documents=%s\n",
            DKC1_BUILD_COMMIT, s_renderWidth, self.documents.fileSystemRepresentation);
    if (!Dkc1SramLoad(&s_sram, g_sram, (size_t)g_sram_size, error, sizeof error)) {
        [self showError:@(error)];
        return YES;
    }
#if DKC1_IOS_DIAGNOSTICS
    const char *startup = getenv("DKC1_STARTUP_SCRIPT");
    if (startup && *startup) {
        if (!Dkc1ScriptLoad(startup, error, sizeof error)) { [self showError:@(error)]; return YES; }
        self.startupActive = YES;
    }
    const char *snapshot = getenv("DKC1_SAVESTATE_INPUT");
    if (snapshot && *snapshot && !RtlLoadSnapshot(snapshot)) {
        [self showError:@"Unable to load the requested diagnostic snapshot."];
        return YES;
    }
#endif
    [self setGamePaused:NO];
#if DKC1_IOS_DIAGNOSTICS
    if (getenv("DKC1_IOS_QA_PAUSED")) {
        Dkc1DrawPpuFrame();
        [self presentFrame];
        [self setGamePaused:YES];
        self.wasPaused = YES;
        if (getenv("DKC1_IOS_QA_HOLD_Y")) [self.buttons[0] latchForQA];
        if (getenv("DKC1_IOS_QA_ROLL_Y_B")) [self.buttons[0] rollForQA];
    }
    if (getenv("DKC1_IOS_QA_LANDSCAPE")) {
        [self setNeedsUpdateOfSupportedInterfaceOrientations];
        [self.view.window.windowScene requestGeometryUpdateWithPreferences:
            [[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:UIInterfaceOrientationMaskLandscapeRight]
            errorHandler:^(NSError *error) { NSLog(@"QA orientation: %@", error); }];
    }
    if (getenv("DKC1_IOS_QA_UPSCALING_MENU")) [self showUpscalingFrom:self];
    else if (getenv("DKC1_IOS_QA_MENU")) [self showMenu];
#endif
    return YES;
}

- (void)openROM {
    [self setGamePaused:YES];
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeData] asCopy:YES];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSURL *url = urls.firstObject;
    if (!url) return;
    BOOL scoped = [url startAccessingSecurityScopedResource];
    size_t size = 0; char error[256] = {0};
    uint8_t *rom = Dkc1ReadVerifiedRom(url.path.fileSystemRepresentation, &size, error, sizeof error);
    if (scoped) [url stopAccessingSecurityScopedResource];
    if (!rom) { [self showError:@(error)]; return; }
    NSData *data = [NSData dataWithBytesNoCopy:rom length:size freeWhenDone:YES];
    NSString *path = [self.documents stringByAppendingPathComponent:@"game.sfc"];
    NSError *writeError;
    if (![data writeToFile:path options:NSDataWritingAtomic error:&writeError]) {
        [self showError:writeError.localizedDescription]; return;
    }
    [self loadROM:path];
}

- (void)showMenu {
    if (!self.loaded) { [self openROM]; return; }
    [self setGamePaused:YES];
    DKCPauseController *menu = [[DKCPauseController alloc] init];
    menu.failed = self.failed;
    menu.hasSave = [NSFileManager.defaultManager fileExistsAtPath:[self.documents stringByAppendingPathComponent:@"quicksave.state"]];
    menu.modalPresentationStyle = UIModalPresentationPageSheet;
    menu.sheetPresentationController.detents = @[[UISheetPresentationControllerDetent largeDetent]];
    menu.sheetPresentationController.prefersGrabberVisible = YES;
    menu.sheetPresentationController.preferredCornerRadius = 32;
    __weak DKCViewController *weakSelf = self;
    menu.upscaling = ^(UIViewController *parent) { [weakSelf showUpscalingFrom:parent]; };
    menu.action = ^(NSInteger action) {
        [weakSelf dismissViewControllerAnimated:YES completion:^{
            if (action == 0) [weakSelf setGamePaused:NO];
            if (action == 1) [weakSelf saveState];
            if (action == 2) [weakSelf loadState];
        }];
    };
    [self presentViewController:menu animated:YES completion:nil];
}

- (void)showUpscalingFrom:(UIViewController *)parent {
    [self setGamePaused:YES];
    DKCUpscalingController *menu = [[DKCUpscalingController alloc] init];
    menu.settings = self.screen.settings;
    menu.pixels = [NSData dataWithBytes:s_pixels length:s_renderWidth*kDkc1VideoHeight*4];
    menu.sourceWidth = s_renderWidth; menu.sourceHeight = kDkc1VideoHeight;
    menu.modalPresentationStyle = UIModalPresentationPageSheet;
    menu.sheetPresentationController.detents = @[[UISheetPresentationControllerDetent largeDetent]];
    menu.sheetPresentationController.prefersGrabberVisible = YES;
    menu.sheetPresentationController.preferredCornerRadius = 28;
    __weak DKCViewController *weakSelf = self;
    menu.settingsChanged = ^(Dkc1GraphicsSettings settings) {
        Dkc1IOSSaveGraphicsSettings(settings);
        weakSelf.screen.settings = settings;
    };
    [parent presentViewController:menu animated:YES completion:nil];
}

#if DKC1_IOS_DIAGNOSTICS
- (void)runGraphicsVerification {
    // Run the existing native GPU/cache oracle unchanged inside the simulator.
    extern int Dkc1IOSGraphicsTestMain(int argc, char **argv);
    NSString *directory = [self.documents stringByAppendingPathComponent:@"graphics-qa"];
    [NSFileManager.defaultManager createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *shader = [NSBundle.mainBundle pathForResource:@"ios_upscaling" ofType:@"txt"];
    NSString *source = [self.documents stringByAppendingPathComponent:@"graphics-source.ppm"];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        char *args[] = {"graphics-test", (char *)shader.fileSystemRepresentation,
            (char *)directory.fileSystemRepresentation, (char *)source.fileSystemRepresentation};
        int count = [NSFileManager.defaultManager fileExistsAtPath:source] ? 4 : 3;
        int result = shader ? Dkc1IOSGraphicsTestMain(count,args) : 2;
        fflush(stdout);
        [@(result).stringValue writeToFile:[directory stringByAppendingPathComponent:@"result.txt"]
            atomically:YES encoding:NSUTF8StringEncoding error:nil];
        fprintf(stderr,"[ios graphics test] result=%d\n",result);
    });
}
#endif

- (void)saveState {
    NSString *temporary = [self.documents stringByAppendingPathComponent:@"quicksave.state.tmp"];
    NSString *final = [self.documents stringByAppendingPathComponent:@"quicksave.state"];
    if (!RtlSaveSnapshot(temporary.fileSystemRepresentation) ||
        rename(temporary.fileSystemRepresentation, final.fileSystemRepresentation) != 0) {
        [self showError:@"Could not save the state. The previous save has been kept."]; return;
    }
    [self setGamePaused:NO];
}
- (void)loadState {
    if (!RtlLoadSnapshot([self.documents stringByAppendingPathComponent:@"quicksave.state"].fileSystemRepresentation)) {
        [self showError:@"Could not load the saved state."]; return;
    }
    Dkc1BeginDrawing(s_pixels, s_renderWidth*4);
    self.audioFraction = 0;
    [self setGamePaused:NO];
}

- (void)startAudio {
    NSError *error = nil;
    AVAudioSession *session = AVAudioSession.sharedInstance;
    if (![session setCategory:AVAudioSessionCategoryPlayback error:&error] ||
        ![session setActive:YES error:&error]) {
        NSLog(@"Audio session: %@", error); return;
    }
    if (!self.audioEngine) {
        self.audioEngine = [[AVAudioEngine alloc] init];
        AVAudioFormat *format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:kAudioRate channels:2];
        self.audioSource = [[AVAudioSourceNode alloc] initWithFormat:format renderBlock:^OSStatus(BOOL *silence, const AudioTimeStamp *time, AVAudioFrameCount count, AudioBufferList *buffers) {
            Dkc1IOSAudioRead(&s_audio, buffers->mBuffers[0].mData, buffers->mBuffers[1].mData, count);
            return noErr;
        }];
        [self.audioEngine attachNode:self.audioSource];
        [self.audioEngine connect:self.audioSource to:self.audioEngine.mainMixerNode format:format];
    }
    if (![self.audioEngine startAndReturnError:&error]) NSLog(@"Audio engine: %@", error);
}

- (void)clearInput {
    self.pad.mask = 0;
    [self.pad setNeedsDisplay];
    for (DKCButton *button in self.buttons) {
        [button resetInput];
    }
}

- (void)setGamePaused:(BOOL)paused {
    self.paused = paused;
    self.previousTime = 0;
    self.elapsed = 0;
    [self clearInput];
    [self.audioEngine stop];
    Dkc1IOSAudioReset(&s_audio);
    if (paused) {
        char error[256];
        if (self.loaded && !Dkc1SramFlush(&s_sram, g_sram, (size_t)g_sram_size, true, error, sizeof error))
            NSLog(@"SRAM: %s", error);
    } else if (self.loaded && !self.failed) {
        /* A small prefill prevents an immediate device-callback underrun. */
        int16_t silence[1070*2] = {0};
        Dkc1IOSAudioWrite(&s_audio, silence, 1070);
        [self startAudio];
    }
    UIApplication.sharedApplication.idleTimerDisabled = self.loaded && !paused;
}
- (void)deactivate:(NSNotification *)notification {
    self.wasPaused = self.paused;
    [self setGamePaused:YES];
}
- (void)activate:(NSNotification *)notification {
    if (self.loaded && !self.wasPaused && !self.presentedViewController) [self setGamePaused:NO];
}
- (void)audioInterrupted:(NSNotification *)notification {
    if ([notification.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue] == AVAudioSessionInterruptionTypeBegan)
        [self setGamePaused:YES];
}
- (void)audioConfigurationChanged:(NSNotification *)notification {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self.loaded && !self.paused) [self setGamePaused:NO];
    });
}
- (void)controllerDisconnected:(NSNotification *)notification {
    self.controllerMenuPressed = NO;
    if (self.loaded) [self setGamePaused:YES];
}

- (uint32_t)readInput {
    uint32_t input = self.pad.mask;
    for (DKCButton *button in self.buttons) if (button.inputPressed) input |= button.mask;
    NSUInteger player = 0;
    BOOL menu = NO;
    for (GCController *controller in GCController.controllers) {
        GCExtendedGamepad *pad = controller.extendedGamepad;
        if (!pad || player >= 2) continue;
        float x = pad.dpad.xAxis.value, y = pad.dpad.yAxis.value;
        if (fabs(pad.leftThumbstick.xAxis.value) > 0.25) x = pad.leftThumbstick.xAxis.value;
        if (fabs(pad.leftThumbstick.yAxis.value) > 0.25) y = pad.leftThumbstick.yAxis.value;
        uint32_t mask = (x < -0.25 ? 0x40 : x > 0.25 ? 0x80 : 0) |
                        (y > 0.25 ? 0x10 : y < -0.25 ? 0x20 : 0) |
                        (pad.buttonA.pressed ? 1 : 0) | (pad.buttonX.pressed ? 2 : 0) |
                        (pad.buttonB.pressed ? 0x100 : 0) | (pad.buttonY.pressed ? 0x200 : 0) |
                        (pad.leftShoulder.pressed ? 0x400 : 0) | (pad.rightShoulder.pressed ? 0x800 : 0) |
                        (pad.buttonMenu.pressed ? 8 : 0) | (pad.buttonOptions.pressed ? 4 : 0);
        input |= mask << (player++ * 12);
        menu |= pad.buttonHome.pressed;
    }
    if (menu && !self.controllerMenuPressed) [self showMenu];
    self.controllerMenuPressed = menu;
    return input;
}

- (void)presentFrame {
    [self.screen presentPixels:s_pixels width:s_renderWidth height:kDkc1VideoHeight];
}

- (void)tick:(CADisplayLink *)link {
    if (!self.loaded || self.paused || self.failed) return;
    if (!self.previousTime) { self.previousTime = link.timestamp; return; }
    self.elapsed += MIN(link.timestamp-self.previousTime, 4.0/kGameHz);
    self.previousTime = link.timestamp;
    uint32_t input = [self readInput];
    if (self.paused) return;
    BOOL drew = NO;
    while (self.elapsed >= 1.0/kGameHz) {
        self.elapsed -= 1.0/kGameHz;
#if DKC1_IOS_DIAGNOSTICS
        if (self.startupActive) {
            Dkc1ScriptOps ops = {0}; bool failed = false;
            input = Dkc1ScriptNextInput(g_ram, &ops, &failed);
            if (failed || ops.checkpoint || ops.state_load || ops.state_save || ++self.startupFrames > 30000) {
                Dkc1ScriptFree(); self.startupActive = NO;
                [self showError:@"Startup route failed; only input/wait commands are accepted."]; return;
            }
            if (Dkc1ScriptFinished()) {
                Dkc1ScriptFree(); self.startupActive = NO;
                fprintf(stderr, "[ios] startup route completed frames=%lu\n", (unsigned long)self.startupFrames);
                [self setGamePaused:YES]; return;
            }
            if (!ops.run_frame) continue;
        }
#endif
        RtlRunFrame(input);
        if (g_fail || !Dkc1LastLleResult()) {
            self.failed = YES;
            [self showError:[NSString stringWithFormat:@"The runtime stopped at frame %d ($%06x).", snes_frame_counter, Dkc1ResumePc()]];
            return;
        }
        Dkc1DrawPpuFrame();
        self.audioFraction += kAudioRate / kGameHz;
        int frames = (int)self.audioFraction;
        self.audioFraction -= frames;
        int16_t samples[534*2];
        RtlRenderAudio(samples, frames, 2);
        Dkc1IOSAudioWrite(&s_audio, samples, frames);
        char error[256];
        if (!Dkc1SramFlush(&s_sram, g_sram, (size_t)g_sram_size, false, error, sizeof error) && !self.saveErrorReported) {
            self.saveErrorReported = YES;
            [self showError:@(error)]; return;
        }
        drew = YES;
    }
    if (drew) [self presentFrame];
}

#if DKC1_IOS_DIAGNOSTICS
- (void)runVerification {
    self.welcome.text = @"Running deterministic verification…";
    self.paused = YES;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        extern int Dkc1IOSHeadlessMain(int, char **);
        const char *frames = getenv("DKC1_IOS_VERIFY_FRAMES");
        char *args[] = {"dkc1-ios-verify", (char *)[self.documents stringByAppendingPathComponent:@"game.sfc"].fileSystemRepresentation, (char *)(frames ? frames : "600")};
        int result = Dkc1IOSHeadlessMain(3, args);
        NSString *status = [NSString stringWithFormat:@"%d\n", result];
        [status writeToFile:[self.documents stringByAppendingPathComponent:@"verification-result.txt"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
        fflush(stdout); fflush(stderr);
        dispatch_async(dispatch_get_main_queue(), ^{
            self.welcome.text = result ? @"Verification failed. See the captured log." : @"Verification passed.\nRelaunch to play.";
        });
    });
}
#endif
@end

@interface DKCSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation DKCSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    if (![scene isKindOfClass:UIWindowScene.class]) return;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController = [[DKCViewController alloc] init];
    [self.window makeKeyAndVisible];
}
@end

@interface DKCAppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation DKCAppDelegate
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    UISceneConfiguration *config = [[UISceneConfiguration alloc] initWithName:@"Game" sessionRole:session.role];
    config.delegateClass = DKCSceneDelegate.class;
    return config;
}
@end

int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(DKCAppDelegate.class)); }
}
