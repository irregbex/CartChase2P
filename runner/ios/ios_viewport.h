#ifndef DKC1_IOS_VIEWPORT_H
#define DKC1_IOS_VIEWPORT_H

typedef struct Dkc1IOSViewport {
  int source_width;
  double display_width;
  double display_height;
} Dkc1IOSViewport;

/* Dimensions in host points; height stays 224 source pixels. Correct SNES
 * pixel aspect is always 7:6. At most one additional source column is clipped
 * on each outside edge when rounding to a symmetric even source width. */
Dkc1IOSViewport Dkc1IOSMakeViewport(double width, double height, int landscape);

#endif
