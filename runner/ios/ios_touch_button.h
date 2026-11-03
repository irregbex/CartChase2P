#ifndef DKC1_IOS_TOUCH_BUTTON_H
#define DKC1_IOS_TOUCH_BUTTON_H
#include <stdbool.h>

typedef struct Dkc1IOSTouchButton {
  unsigned fingers;
  unsigned rolled_fingers;
  bool latched;
  bool tap_candidate;
  bool has_previous_tap;
  double began_at;
  double previous_tap_at;
} Dkc1IOSTouchButton;

void Dkc1IOSTouchBegin(Dkc1IOSTouchButton *button, double time);
void Dkc1IOSTouchEnd(Dkc1IOSTouchButton *button, double time, bool can_latch);
void Dkc1IOSTouchCancel(Dkc1IOSTouchButton *button);
bool Dkc1IOSTouchPressed(const Dkc1IOSTouchButton *button);
void Dkc1IOSTouchRollBegin(Dkc1IOSTouchButton *button);
void Dkc1IOSTouchRollEnd(Dkc1IOSTouchButton *button);
/* Contact center relative to the target center, in UIKit points. Use stable
 * layout bounds, not the button's animated press transform. */
bool Dkc1IOSTouchRollHits(double x, double y, double width, double height,
                        double contact_radius, bool already_pressed);
#endif
