#include "ios_audio_ring.h"

size_t Dkc1IOSAudioWrite(Dkc1IOSAudioRing *ring, const int16_t *samples, size_t frames) {
    uint32_t write = atomic_load_explicit(&ring->write, memory_order_relaxed);
    uint32_t read = atomic_load_explicit(&ring->read, memory_order_acquire);
    size_t available = kDkc1IOSAudioCapacity - (uint32_t)(write - read);
    if (frames > available) frames = available;
    for (size_t i = 0; i < frames; ++i) {
        size_t index = ((write + i) % kDkc1IOSAudioCapacity) * 2;
        ring->samples[index] = samples[i * 2];
        ring->samples[index + 1] = samples[i * 2 + 1];
    }
    atomic_store_explicit(&ring->write, write + (uint32_t)frames, memory_order_release);
    return frames;
}

void Dkc1IOSAudioRead(Dkc1IOSAudioRing *ring, float *left, float *right, size_t frames) {
    uint32_t read = atomic_load_explicit(&ring->read, memory_order_relaxed);
    uint32_t write = atomic_load_explicit(&ring->write, memory_order_acquire);
    size_t count = (uint32_t)(write - read);
    if (count > frames) count = frames;
    for (size_t i = 0; i < frames; ++i) {
        size_t index = ((read + i) % kDkc1IOSAudioCapacity) * 2;
        left[i] = i < count ? ring->samples[index] / 32768.0f : 0;
        right[i] = i < count ? ring->samples[index + 1] / 32768.0f : 0;
    }
    atomic_store_explicit(&ring->read, read + (uint32_t)count, memory_order_release);
}

void Dkc1IOSAudioReset(Dkc1IOSAudioRing *ring) {
    atomic_store(&ring->read, 0);
    atomic_store(&ring->write, 0);
}
