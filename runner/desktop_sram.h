#ifndef DKC1_DESKTOP_SRAM_H
#define DKC1_DESKTOP_SRAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* DKC1 USA v1.0 cartridge RAM. No guest memory or save-state format changes. */
enum { kDkc1SramSize = 2048 };
typedef struct Dkc1SramStore {
  uint8_t persisted[kDkc1SramSize];
  bool ready;
  unsigned retry_frames;
} Dkc1SramStore;

/* Relative to the host's user directory. Missing is a normal first launch;
 * malformed/unreadable files fail without changing SRAM or the disk file. */
bool Dkc1SramLoad(Dkc1SramStore *store, uint8_t *ram, size_t size,
                  char *error, size_t error_size);
/* Call after a successful emulated frame; force retries pending I/O at exit.
 * Only changed data is written. Failure preserves the previous disk image. */
bool Dkc1SramFlush(Dkc1SramStore *store, const uint8_t *ram, size_t size,
                   bool force, char *error, size_t error_size);

#endif
