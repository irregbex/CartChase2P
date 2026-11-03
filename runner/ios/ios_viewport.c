#include "ios_viewport.h"
#include "dkc1_video.h"
#include <math.h>

Dkc1IOSViewport Dkc1IOSMakeViewport(double width, double height, int landscape) {
  Dkc1IOSViewport viewport = {kDkc1VideoNativeWidth, 0, 0};
  if (!isfinite(width) || !isfinite(height) || width <= 0 || height <= 0)
    return viewport;
  const double pixel_aspect = 7.0 / 6.0;
  const double needed = width / height * kDkc1VideoHeight / pixel_aspect;
  if (landscape) {
    double even_width = ceil(needed / 2.0) * 2.0;
    viewport.source_width = (int)fmin(kDkc1VideoMaxWidth,
                                    fmax(kDkc1VideoNativeWidth, even_width));
  }
  const double aspect = viewport.source_width * pixel_aspect / kDkc1VideoHeight;
  double scale = height / kDkc1VideoHeight;
  /* On portrait/tablet layouts, fit the complete native image. If an unusually
   * wide screen exceeds PPU capacity, keep proportions and fail closed to bars. */
  if (!landscape || needed < kDkc1VideoNativeWidth)
    scale = fmin(scale, width / (viewport.source_width * pixel_aspect));
  viewport.display_height = kDkc1VideoHeight * scale;
  viewport.display_width = viewport.display_height * aspect;
  return viewport;
}
