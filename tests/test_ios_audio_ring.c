#include "ios_audio_ring.h"
#include <assert.h>
#include <stdio.h>

static Dkc1IOSAudioRing ring;
static int16_t input[kDkc1IOSAudioCapacity * 2];
static float left[kDkc1IOSAudioCapacity], right[kDkc1IOSAudioCapacity];

int main(void) {
    for (int i=0;i<kDkc1IOSAudioCapacity;i++) {
        input[i*2] = (int16_t)(i*3);
        input[i*2+1] = (int16_t)(-i*3);
    }
    Dkc1IOSAudioRead(&ring, left, right, 16);
    for (int i=0;i<16;i++) assert(left[i] == 0 && right[i] == 0);
    assert(Dkc1IOSAudioWrite(&ring, input, kDkc1IOSAudioCapacity) == kDkc1IOSAudioCapacity);
    assert(Dkc1IOSAudioWrite(&ring, input, 1) == 0);
    Dkc1IOSAudioRead(&ring, left, right, kDkc1IOSAudioCapacity);
    for (int i=0;i<kDkc1IOSAudioCapacity;i++) {
        assert(left[i] == input[i*2]/32768.0f);
        assert(right[i] == input[i*2+1]/32768.0f);
    }
    /* Ring boundary and 32-bit counter overflow must preserve stereo order. */
    atomic_store(&ring.read, UINT32_MAX-7);
    atomic_store(&ring.write, UINT32_MAX-7);
    assert(Dkc1IOSAudioWrite(&ring, input, 40) == 40);
    Dkc1IOSAudioRead(&ring, left, right, 64);
    for (int i=0;i<64;i++) {
        assert(left[i] == (i<40 ? input[i*2]/32768.0f : 0));
        assert(right[i] == (i<40 ? input[i*2+1]/32768.0f : 0));
    }
    assert(Dkc1IOSAudioWrite(&ring, input, 20) == 20);
    Dkc1IOSAudioReset(&ring);
    Dkc1IOSAudioRead(&ring, left, right, 64);
    for (int i=0;i<64;i++) assert(left[i] == 0 && right[i] == 0);
    puts("iOS audio ring tests passed");
}
