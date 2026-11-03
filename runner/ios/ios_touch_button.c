#include "ios_touch_button.h"
#include <math.h>
#include <string.h>

void Dkc1IOSTouchBegin(Dkc1IOSTouchButton *button, double time) {
  if (button->fingers++ == 0) {
    button->began_at = time;
    button->tap_candidate = true;
  } else {
    // Two fingers on the same button are a chord, never a double tap.
    button->tap_candidate = false;
  }
}

void Dkc1IOSTouchEnd(Dkc1IOSTouchButton *button, double time, bool can_latch) {
  if (!button->fingers || --button->fingers) return;
  double duration = time - button->began_at;
  bool tap = can_latch && button->tap_candidate && duration >= 0 && duration <= 0.25;
  double gap = button->began_at - button->previous_tap_at;
  if (tap && button->has_previous_tap && gap >= 0 && gap <= 0.32) {
    button->latched = !button->latched;
    button->has_previous_tap = false;
  } else {
    button->has_previous_tap = tap;
    button->previous_tap_at = time;
  }
}

void Dkc1IOSTouchCancel(Dkc1IOSTouchButton *button) {
  memset(button, 0, sizeof *button);
}

bool Dkc1IOSTouchPressed(const Dkc1IOSTouchButton *button) {
  return button->fingers != 0 || button->rolled_fingers != 0 || button->latched;
}

void Dkc1IOSTouchRollBegin(Dkc1IOSTouchButton *button) {
  ++button->rolled_fingers;
}

void Dkc1IOSTouchRollEnd(Dkc1IOSTouchButton *button) {
  if (button->rolled_fingers) --button->rolled_fingers;
}

bool Dkc1IOSTouchRollHits(double x, double y, double width, double height,
                        double contact_radius, bool already_pressed) {
  if (!isfinite(x) || !isfinite(y) || !isfinite(width) || !isfinite(height) ||
      width <= 0 || height <= 0)
    return false;
  if (!isfinite(contact_radius)) contact_radius = 8;
  // A thumb can cover B before its center crosses B's edge. Limit contact
  // expansion so a resting thumb centered on Y cannot also trigger B.
  double radius = fmax(0, fmin(width, height)/2 - 3) +
                  fmax(8, fmin(contact_radius, 24));
  // A five-point release band prevents repeated jumps from edge jitter.
  if (already_pressed) radius += 5;
  return x*x + y*y <= radius*radius;
}
