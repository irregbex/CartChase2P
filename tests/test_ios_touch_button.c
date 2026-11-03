#include "ios_touch_button.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static unsigned mask(Dkc1IOSTouchButton *y, Dkc1IOSTouchButton *b) {
  return (Dkc1IOSTouchPressed(y) ? 2 : 0) | (Dkc1IOSTouchPressed(b) ? 1 : 0);
}
static void tap(Dkc1IOSTouchButton *button, double time, bool y) {
  Dkc1IOSTouchBegin(button,time);
  Dkc1IOSTouchEnd(button,time+0.05,y);
}

static void roll(Dkc1IOSTouchButton *y, Dkc1IOSTouchButton *b,
                 double x, double y_position, double contact_radius, bool *down) {
  bool hit = Dkc1IOSTouchRollHits(x,y_position,58,58,contact_radius,*down);
  if (hit && !*down) Dkc1IOSTouchRollBegin(b);
  if (!hit && *down) Dkc1IOSTouchRollEnd(b);
  if (hit) y->tap_candidate = false;
  *down = hit;
}

static void check_rolling_thumb(void) {
  Dkc1IOSTouchButton y = {0}, b = {0};
  bool down = false;
  // Production 158-point diamond: B is 45.82 points right/below Y.
  Dkc1IOSTouchBegin(&y,1);
  roll(&y,&b,-45.82,-45.82,24,&down); assert(mask(&y,&b)==2);
  // Contact overlaps B while its center is still outside B's drawn circle.
  roll(&y,&b,-25,-25,14,&down); assert(mask(&y,&b)==3);
  assert(!y.tap_candidate); // rolling must never toggle Y hold mode
  roll(&y,&b,-45.82,-45.82,14,&down); assert(mask(&y,&b)==2);
  roll(&y,&b,0,0,14,&down); assert(mask(&y,&b)==3);
  // Rolling away must preserve another finger that is independently on B.
  Dkc1IOSTouchBegin(&b,2);
  roll(&y,&b,-45.82,-45.82,14,&down); assert(mask(&y,&b)==3);
  Dkc1IOSTouchEnd(&b,2.1,false); assert(mask(&y,&b)==2);
  roll(&y,&b,0,0,14,&down); assert(mask(&y,&b)==3);
  Dkc1IOSTouchRollEnd(&b); Dkc1IOSTouchEnd(&y,2.2,true);
  assert(mask(&y,&b)==0 && !y.latched);
  // Edge jitter does not repeatedly release/repress jump.
  assert(Dkc1IOSTouchRollHits(39,0,58,58,14,false));
  assert(!Dkc1IOSTouchRollHits(42,0,58,58,14,false));
  assert(Dkc1IOSTouchRollHits(42,0,58,58,14,true));
  assert(!Dkc1IOSTouchRollHits(46,0,58,58,14,true));
  // Contact estimates cannot turn a centered Y touch into Y+B.
  assert(!Dkc1IOSTouchRollHits(-45.82,-45.82,58,58,1000,false));
  assert(Dkc1IOSTouchRollHits(30,0,58,58,0,false));
  assert(!Dkc1IOSTouchRollHits(NAN,0,58,58,14,false));
  assert(!Dkc1IOSTouchRollHits(0,0,0,58,14,false));
  // Multiple rolling contacts release independently and cancel cleanly.
  Dkc1IOSTouchRollBegin(&b); Dkc1IOSTouchRollBegin(&b);
  Dkc1IOSTouchRollEnd(&b); assert(mask(&y,&b)==1);
  Dkc1IOSTouchCancel(&b); assert(mask(&y,&b)==0);
  Dkc1IOSTouchRollEnd(&b); assert(b.rolled_fingers==0);
}

int main(void) {
  check_rolling_thumb();
  Dkc1IOSTouchButton y = {0}, b = {0};
  // Hold run, jump, land, jump again: releasing B must never release Y.
  Dkc1IOSTouchBegin(&y,1);
  Dkc1IOSTouchBegin(&b,2); assert(mask(&y,&b) == 3);
  Dkc1IOSTouchEnd(&b,2.1,false); assert(mask(&y,&b) == 2);
  Dkc1IOSTouchBegin(&b,3); assert(mask(&y,&b) == 3);
  Dkc1IOSTouchEnd(&y,3.1,true); assert(mask(&y,&b) == 1);
  Dkc1IOSTouchEnd(&b,3.2,false); assert(mask(&y,&b) == 0);
  assert(!y.latched); // holding Y was not a tap

  tap(&y,4,true); assert(!y.latched);
  tap(&y,4.2,true); assert(y.latched && mask(&y,&b) == 2);
  tap(&b,5,false); tap(&b,5.2,false);
  assert(!b.latched && mask(&y,&b) == 2);
  Dkc1IOSTouchBegin(&b,6); assert(mask(&y,&b) == 3);
  Dkc1IOSTouchEnd(&b,6.1,false); assert(mask(&y,&b) == 2);
  tap(&y,7,true); tap(&y,7.2,true);
  assert(!y.latched && mask(&y,&b) == 0);

  tap(&y,8,true); tap(&y,9,true); assert(!y.latched); // separated taps
  Dkc1IOSTouchBegin(&y,9.2); y.tap_candidate=false;
  Dkc1IOSTouchEnd(&y,9.25,true); assert(!y.latched); // thumb drag
  Dkc1IOSTouchBegin(&y,10); Dkc1IOSTouchBegin(&y,10.1);
  Dkc1IOSTouchEnd(&y,10.15,true); assert(Dkc1IOSTouchPressed(&y));
  Dkc1IOSTouchEnd(&y,10.2,true); assert(!y.latched && !Dkc1IOSTouchPressed(&y));
  tap(&y,11,true); tap(&y,11.2,true); assert(y.latched);
  Dkc1IOSTouchCancel(&y); assert(mask(&y,&b) == 0 && !y.has_previous_tap);
  Dkc1IOSTouchEnd(&y,12,true); assert(y.fingers == 0); // stale lift after pause
  puts("iOS touch: rolling Y+B, contact edges, independent fingers, latch and cancellation passed");
  return 0;
}
