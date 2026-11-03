#import <UIKit/UIKit.h>
#include "desktop_graphics.h"

Dkc1GraphicsSettings Dkc1IOSLoadGraphicsSettings(void);
void Dkc1IOSSaveGraphicsSettings(Dkc1GraphicsSettings settings);
NSString *Dkc1IOSUpscalerName(int upscaler);

// Presentation only: the supplied framebuffer is copied before GPU submission.
@interface DKCMetalView : UIView
@property(nonatomic) Dkc1GraphicsSettings settings;
@property(nonatomic, readonly) BOOL upscalingAvailable;
- (void)presentPixels:(const void *)pixels width:(int)width height:(int)height;
@end

@interface DKCUpscalingController : UIViewController
@property(nonatomic) Dkc1GraphicsSettings settings;
@property(nonatomic, copy) NSData *pixels;
@property(nonatomic) int sourceWidth;
@property(nonatomic) int sourceHeight;
@property(nonatomic, copy) void (^settingsChanged)(Dkc1GraphicsSettings);
@end
