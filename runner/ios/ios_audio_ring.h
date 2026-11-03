#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum { kDkc1IOSAudioCapacity = 8192 };
/* One main-thread producer, one real-time audio consumer. Never touch game
 * state on the audio thread. Counters wrap using unsigned arithmetic. */
typedef struct Dkc1IOSAudioRing {
    _Atomic uint32_t read;
    _Atomic uint32_t write;
    int16_t samples[kDkc1IOSAudioCapacity * 2];
} Dkc1IOSAudioRing;
size_t Dkc1IOSAudioWrite(Dkc1IOSAudioRing *ring, const int16_t *samples, size_t frames);
void Dkc1IOSAudioRead(Dkc1IOSAudioRing *ring, float *left, float *right, size_t frames);
/* Call only after stopping the audio engine (both owners quiescent). */
void Dkc1IOSAudioReset(Dkc1IOSAudioRing *ring);
