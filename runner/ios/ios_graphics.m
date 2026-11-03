#import "ios_graphics.h"
#import "macos_graphics.h"
#import <QuartzCore/CAMetalLayer.h>
#include <math.h>

static NSString *const kSettingsKey = @"IOSUpscalingV1";
static UIColor *Accent(void) { return [UIColor colorWithRed:0.65 green:0.9 blue:0.58 alpha:1]; }
NSString *Dkc1IOSUpscalerName(int upscaler) {
    return @[@"Original pixels", @"Bilinear", @"Reconstruct", @"Sharp Bilinear"][MAX(0,MIN(3,upscaler))];
}
Dkc1GraphicsSettings Dkc1IOSLoadGraphicsSettings(void) {
    Dkc1GraphicsSettings s; Dkc1GraphicsDefault(&s);
    NSDictionary *saved = [NSUserDefaults.standardUserDefaults dictionaryForKey:kSettingsKey];
    NSArray *keys = @[@"upscaler", @"mode", @"strength", @"softness", @"shading"];
    int *values[] = {&s.upscaler, &s.reconstruct_mode, &s.strength, &s.softness, &s.shading};
    for (NSUInteger i=0;i<keys.count;i++) {
        id value = saved[keys[i]];
        if ([value isKindOfClass:NSNumber.class]) *values[i] = [value intValue];
    }
    Dkc1GraphicsClamp(&s);
    return s;
}
void Dkc1IOSSaveGraphicsSettings(Dkc1GraphicsSettings s) {
    Dkc1GraphicsClamp(&s);
    [NSUserDefaults.standardUserDefaults setObject:@{
        @"upscaler":@(s.upscaler), @"mode":@(s.reconstruct_mode),
        @"strength":@(s.strength), @"softness":@(s.softness), @"shading":@(s.shading)
    } forKey:kSettingsKey];
}

@implementation DKCMetalView {
    CAMetalLayer *_metalLayer;
    Dkc1MetalGraphics *_renderer;
    id<MTLCommandQueue> _queue;
    dispatch_semaphore_t _inFlight;
    NSData *_pixels;
    int _width, _height;
    BOOL _retryPending;
}
- (instancetype)initWithFrame:(CGRect)frame {
    if (!(self = [super initWithFrame:frame])) return nil;
    self.backgroundColor = UIColor.blackColor;
    self.userInteractionEnabled = NO;
    self.layer.magnificationFilter = kCAFilterNearest;
    self.layer.minificationFilter = kCAFilterNearest;
    Dkc1GraphicsDefault(&_settings);
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    NSError *error = nil;
    NSString *shader = [NSBundle.mainBundle pathForResource:@"ios_upscaling" ofType:@"txt"];
    if (device && shader) _renderer = [[Dkc1MetalGraphics alloc] initWithDevice:device shaderPath:shader error:&error];
    _queue = [device newCommandQueue];
    if (_renderer && _queue) {
        _inFlight = dispatch_semaphore_create(2);
        _metalLayer = [CAMetalLayer layer];
        _metalLayer.device = device;
        _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        _metalLayer.framebufferOnly = YES;
        _metalLayer.opaque = YES;
        _metalLayer.maximumDrawableCount = 3;
        [self.layer addSublayer:_metalLayer];
    } else {
        _renderer = nil;
        NSLog(@"[ios graphics] Metal unavailable; original pixels: %@", error);
    }
    return self;
}
- (BOOL)upscalingAvailable { return _renderer != nil; }
- (void)didMoveToWindow {
    [super didMoveToWindow];
    [self setNeedsLayout];
}
- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat scale = self.window.screen.nativeScale ?: UIScreen.mainScreen.nativeScale;
    CGSize drawable = CGSizeMake(MAX(1,lround(self.bounds.size.width*scale)),
                                 MAX(1,lround(self.bounds.size.height*scale)));
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _metalLayer.frame = self.bounds;
    _metalLayer.contentsScale = scale;
    if (!CGSizeEqualToSize(_metalLayer.drawableSize, drawable)) {
        _metalLayer.drawableSize = drawable;
        NSLog(@"[ios graphics] drawable=%.0fx%.0f scale=%.2f", drawable.width, drawable.height, scale);
    }
    [CATransaction commit];
    [self drawFrame];
}
- (void)setSettings:(Dkc1GraphicsSettings)settings {
    Dkc1GraphicsClamp(&settings);
    _settings = settings;
    [self drawFrame];
}
- (void)presentPixels:(const void *)pixels width:(int)width height:(int)height {
    if (!pixels || width<1 || height<1) return;
    _pixels = [NSData dataWithBytes:pixels length:(size_t)width*height*4];
    _width = width; _height = height;
    [self drawFrame];
}
- (void)retryFrame {
    // A paused frame/settings change must eventually draw after GPU backpressure.
    if (_retryPending) return;
    _retryPending = YES;
    __weak DKCMetalView *weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC/60), dispatch_get_main_queue(), ^{
        DKCMetalView *view = weakSelf;
        if (!view) return;
        view->_retryPending = NO;
        [view drawFrame];
    });
}
- (void)drawFrame {
    if (!_pixels || !self.window || self.bounds.size.width<1 || self.bounds.size.height<1 ||
        UIApplication.sharedApplication.applicationState == UIApplicationStateBackground) return;
    if (!_renderer) {
        CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)_pixels);
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGImageRef image = CGImageCreate(_width, _height, 8, 32, _width*4, space,
            kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst, provider, NULL, false, kCGRenderingIntentDefault);
        [CATransaction begin]; [CATransaction setDisableActions:YES];
        _metalLayer.hidden = YES;
        self.layer.contents = (__bridge id)image;
        [CATransaction commit];
        CGImageRelease(image); CGColorSpaceRelease(space); CGDataProviderRelease(provider);
        return;
    }
    if (_metalLayer.drawableSize.width<1 || _metalLayer.drawableSize.height<1) return;
    if (dispatch_semaphore_wait(_inFlight, DISPATCH_TIME_NOW)) { [self retryFrame]; return; }
    id<CAMetalDrawable> drawable = [_metalLayer nextDrawable];
    id<MTLCommandBuffer> command = drawable ? [_queue commandBuffer] : nil;
    if (!command) { dispatch_semaphore_signal(_inFlight); [self retryFrame]; return; }
    MTLViewport viewport = {0,0,drawable.texture.width,drawable.texture.height,0,1};
    BOOL encoded = [_renderer encodePixels:_pixels.bytes width:_width height:_height
        target:drawable.texture viewport:viewport settings:_settings commandBuffer:command];
    dispatch_semaphore_t gate = _inFlight;
    __weak DKCMetalView *weakSelf = self;
    [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        dispatch_semaphore_signal(gate);
        if (completed.status == MTLCommandBufferStatusError) {
            NSLog(@"[ios graphics] GPU error; original pixels: %@", completed.error);
            dispatch_async(dispatch_get_main_queue(), ^{
                DKCMetalView *view = weakSelf;
                if (!view) return;
                view->_renderer = nil;
                [view drawFrame];
            });
        }
    }];
    if (encoded) [command presentDrawable:drawable];
    [command commit]; // Also retires any input ownership on an incomplete encode.
    if (!encoded) [self retryFrame];
}
@end

@implementation DKCUpscalingController {
    DKCMetalView *_preview;
    UIStackView *_reconstruct;
    UIButton *_filterButton, *_modeButton;
    UILabel *_description;
    NSMutableArray<UILabel *> *_values;
    NSMutableArray<UISlider *> *_sliders;
    UIStackView *_columns;
}
- (UILabel *)label:(NSString *)text size:(CGFloat)size color:(UIColor *)color {
    UILabel *label = [[UILabel alloc] init]; label.text = text;
    label.font = [UIFont systemFontOfSize:size]; label.textColor = color; label.numberOfLines = 0;
    return label;
}
- (UIButton *)choiceButton {
    UIButtonConfiguration *config = [UIButtonConfiguration tintedButtonConfiguration];
    config.baseBackgroundColor = Accent(); config.baseForegroundColor = Accent();
    config.cornerStyle = UIButtonConfigurationCornerStyleLarge;
    config.contentInsets = NSDirectionalEdgeInsetsMake(13,16,13,16);
    config.image = [UIImage systemImageNamed:@"chevron.up.chevron.down"];
    config.imagePlacement = NSDirectionalRectEdgeTrailing; config.imagePadding = 12;
    UIButton *button = [UIButton buttonWithConfiguration:config primaryAction:nil];
    button.showsMenuAsPrimaryAction = YES;
    button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
    return button;
}
- (void)viewDidLoad {
    [super viewDidLoad];
    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    self.view.backgroundColor = [UIColor colorWithRed:0.06 green:0.09 blue:0.08 alpha:1];
    UIScrollView *scroll = [[UIScrollView alloc] init]; scroll.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:scroll];
    UIStackView *stack = [[UIStackView alloc] init]; stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 16; stack.translatesAutoresizingMaskIntoConstraints = NO;
    [scroll addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [scroll.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor],
        [scroll.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:20],
        [stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-24],
        [stack.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:24],
        [stack.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-24],
        [stack.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-48]]];
    UIStackView *heading = [[UIStackView alloc] init]; heading.alignment = UIStackViewAlignmentCenter;
    UILabel *title = [self label:@"Upscaling" size:27 color:UIColor.whiteColor];
    title.font = [UIFont systemFontOfSize:27 weight:UIFontWeightBold];
    UIButtonConfiguration *doneConfig = [UIButtonConfiguration plainButtonConfiguration];
    doneConfig.title = @"Done"; doneConfig.baseForegroundColor = Accent();
    UIButton *done = [UIButton buttonWithConfiguration:doneConfig primaryAction:nil];
    [done addTarget:self action:@selector(done) forControlEvents:UIControlEventTouchUpInside];
    [done setContentHuggingPriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
    [heading addArrangedSubview:title]; [heading addArrangedSubview:done]; [stack addArrangedSubview:heading];
    // Two columns keep the current frame visible beside settings in landscape.
    UIStackView *columns = [[UIStackView alloc] init]; columns.spacing = 22;
    _columns = columns;
    columns.axis = self.view.bounds.size.width>self.view.bounds.size.height ? UILayoutConstraintAxisHorizontal : UILayoutConstraintAxisVertical;
    columns.alignment = UIStackViewAlignmentTop; columns.distribution = UIStackViewDistributionFillEqually;
    UIStackView *left = [[UIStackView alloc] init]; left.axis = UILayoutConstraintAxisVertical; left.spacing = 12;
    _preview = [[DKCMetalView alloc] init]; _preview.settings = self.settings;
    _preview.layer.cornerRadius = 16; _preview.clipsToBounds = YES;
    [_preview.heightAnchor constraintEqualToAnchor:_preview.widthAnchor multiplier:(CGFloat)self.sourceHeight/(self.sourceWidth*7.0/6.0)].active = YES;
    [left addArrangedSubview:_preview];
    [left addArrangedSubview:[self label:@"LIVE PREVIEW" size:10 color:Accent()]];
    _description = [self label:@"" size:13 color:[UIColor colorWithWhite:0.65 alpha:1]];
    [left addArrangedSubview:_description];
    UIStackView *right = [[UIStackView alloc] init]; right.axis = UILayoutConstraintAxisVertical; right.spacing = 12;
    _filterButton = [self choiceButton]; _filterButton.accessibilityLabel = @"Upscaling filter";
    [right addArrangedSubview:_filterButton];
    _reconstruct = [[UIStackView alloc] init]; _reconstruct.axis = UILayoutConstraintAxisVertical; _reconstruct.spacing = 8;
    _modeButton = [self choiceButton]; _modeButton.accessibilityLabel = @"Reconstruct detail";
    [_reconstruct addArrangedSubview:_modeButton];
    _values = [NSMutableArray array]; _sliders = [NSMutableArray array];
    NSArray *names = @[@"Strength", @"Softness", @"Shading"];
    for (NSInteger i=0;i<3;i++) {
        UILabel *label = [self label:@"" size:13 color:UIColor.whiteColor];
        UISlider *slider = [[UISlider alloc] init]; slider.minimumValue = 0; slider.maximumValue = 100;
        slider.minimumTrackTintColor = Accent(); slider.tag = i; slider.accessibilityLabel = names[i];
        [slider addTarget:self action:@selector(sliderChanged:) forControlEvents:UIControlEventValueChanged];
        [_values addObject:label]; [_sliders addObject:slider];
        [_reconstruct addArrangedSubview:label]; [_reconstruct addArrangedSubview:slider];
    }
    [right addArrangedSubview:_reconstruct]; [columns addArrangedSubview:left]; [columns addArrangedSubview:right];
    [stack addArrangedSubview:columns];
    [self refresh];
    [_preview presentPixels:self.pixels.bytes width:self.sourceWidth height:self.sourceHeight];
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    _columns.axis = self.view.bounds.size.width>self.view.bounds.size.height ? UILayoutConstraintAxisHorizontal : UILayoutConstraintAxisVertical;
}
- (void)refresh {
    Dkc1GraphicsSettings s = self.settings; Dkc1GraphicsClamp(&s); self.settings = s;
    __weak DKCUpscalingController *weakSelf = self;
    NSMutableArray *filters = [NSMutableArray array];
    for (NSNumber *choice in @[@0,@3,@1,@2]) {
        int value = choice.intValue;
        UIAction *action = [UIAction actionWithTitle:Dkc1IOSUpscalerName(value) image:nil identifier:nil handler:^(UIAction *action) {
            DKCUpscalingController *owner = weakSelf; if (!owner) return;
            Dkc1GraphicsSettings changed = owner.settings; changed.upscaler = value;
            owner.settings = changed; [owner apply];
        }];
        action.state = s.upscaler == value ? UIMenuElementStateOn : UIMenuElementStateOff;
        if (!_preview.upscalingAvailable && value) action.attributes = UIMenuElementAttributesDisabled;
        [filters addObject:action];
    }
    _filterButton.menu = [UIMenu menuWithTitle:@"Upscaling" children:filters];
    UIButtonConfiguration *config = _filterButton.configuration; config.title = Dkc1IOSUpscalerName(s.upscaler); _filterButton.configuration = config;
    NSArray *modes = @[@"Sharp pixels", @"Dither decoding", @"Diagonal edges", @"Fine slopes", @"Finest slopes"];
    NSMutableArray *detail = [NSMutableArray array];
    for (int i=0;i<5;i++) {
        UIAction *action = [UIAction actionWithTitle:modes[i] image:nil identifier:nil handler:^(UIAction *action) {
            DKCUpscalingController *owner = weakSelf; if (!owner) return;
            Dkc1GraphicsSettings changed = owner.settings; changed.reconstruct_mode = i;
            owner.settings = changed; [owner apply];
        }];
        action.state = s.reconstruct_mode == i ? UIMenuElementStateOn : UIMenuElementStateOff;
        [detail addObject:action];
    }
    _modeButton.menu = [UIMenu menuWithTitle:@"Reconstruct detail" children:detail];
    config = _modeButton.configuration; config.title = modes[s.reconstruct_mode]; _modeButton.configuration = config;
    _reconstruct.hidden = s.upscaler != kDkc1UpscalerReconstruct || !_preview.upscalingAvailable;
    NSArray *names = @[@"Strength", @"Softness", @"Shading"];
    int values[] = {s.strength, s.softness, s.shading};
    for (int i=0;i<3;i++) { _values[i].text = [NSString stringWithFormat:@"%@ · %d%%", names[i], values[i]]; _sliders[i].value = values[i]; }
    _description.text = _preview.upscalingAvailable ? @[
        @"The original pixel grid, crisp and unfiltered.",
        @"A softer finish with smoothly blended pixels.",
        @"Refines diagonal edges and pixel patterns. Tune the finish to your taste.",
        @"Crisp pixel interiors with gently smoothed edges."
    ][s.upscaler] : @"Upscaling is unavailable. The game is using original pixels.";
    _preview.settings = s;
}
- (void)apply { [self refresh]; if (self.settingsChanged) self.settingsChanged(self.settings); }
- (void)sliderChanged:(UISlider *)sender {
    Dkc1GraphicsSettings s = self.settings;
    int *values[] = {&s.strength,&s.softness,&s.shading};
    *values[sender.tag] = (int)lroundf(sender.value); self.settings = s; [self apply];
}
- (void)done { [self dismissViewControllerAnimated:YES completion:nil]; }
@end
