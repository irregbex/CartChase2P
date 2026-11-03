#include "ios_viewport.h"
#include "dkc1_video.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void check_phone(double width, double height) {
  Dkc1IOSViewport v = Dkc1IOSMakeViewport(width, height, 1);
  assert(v.source_width >= 256 && v.source_width <= 448);
  assert(!(v.source_width & 1));
  assert(fabs(v.display_height-height) < 0.000001);
  assert(v.display_width >= width-0.000001);
  const double pixel_width = v.display_height/224.0 * 7.0/6.0;
  assert(v.display_width-width < 2*pixel_width+0.000001);
  assert(fabs(v.display_width/v.source_width / (v.display_height/224.0)-7.0/6.0) < 0.000001);
  // Only added outside columns can be cropped; all native game columns fit.
  assert(256*pixel_width <= width);
}

int main(void) {
  check_phone(2868,1320); // connected iPhone 16 Pro Max
  check_phone(2622,1206); // simulator iPhone 17 Pro
  check_phone(2556,1179);
  check_phone(1334,750);
  assert(Dkc1IOSMakeViewport(2868,1320,1).source_width == 418);
  Dkc1IOSViewport portrait = Dkc1IOSMakeViewport(390,420,0);
  assert(portrait.source_width == 256);
  assert(fabs(portrait.display_width-390) < 0.000001);
  assert(fabs(portrait.display_width/portrait.display_height-4.0/3.0) < 0.000001);
  Dkc1IOSViewport tablet = Dkc1IOSMakeViewport(1024,768,1);
  assert(tablet.source_width == 256);
  assert(fabs(tablet.display_width-1024) < 0.000001);
  Dkc1IOSViewport beyond = Dkc1IOSMakeViewport(2400,600,1);
  assert(beyond.source_width == kDkc1VideoMaxWidth);
  assert(beyond.display_width < 2400); // capacity limit never stretches
  assert(Dkc1IOSMakeViewport(0,600,1).display_width == 0);
  assert(Dkc1IOSMakeViewport(INFINITY,600,1).display_width == 0);
  puts("iOS viewport: native proportions, full phone coverage, bounded crop and capacity passed");
  return 0;
}
