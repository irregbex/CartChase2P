#include "desktop_sram.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define MakeDir(p) _mkdir(p)
#define RemoveDir(p) _rmdir(p)
#define ProcessId() _getpid()
#else
#include <unistd.h>
#define MakeDir(p) mkdir(p, 0755)
#define RemoveDir(p) rmdir(p)
#define ProcessId() getpid()
#endif

static void ReadExpected(const uint8_t *expected) {
  uint8_t actual[kDkc1SramSize];
  FILE *file = fopen("saves/save.srm", "rb");
  assert(file);
  assert(fread(actual, 1, sizeof actual, file) == sizeof actual);
  assert(fgetc(file) == EOF);
  assert(fclose(file) == 0);
  assert(memcmp(actual, expected, sizeof actual) == 0);
}

int main(void) {
  Dkc1SramStore store;
  uint8_t ram[kDkc1SramSize] = {0}, previous[kDkc1SramSize];
  char error[256], temporary[96];
  assert(Dkc1SramLoad(&store, ram, sizeof ram, error, sizeof error));
  assert(Dkc1SramFlush(&store, ram, sizeof ram, false, error, sizeof error));
  assert(fopen("saves/save.srm", "rb") == NULL);
  for (size_t i = 0; i < sizeof ram; i++) ram[i] = (uint8_t)(i * 37u + 113u);
  memcpy(previous, ram, sizeof ram);
  assert(Dkc1SramFlush(&store, ram, sizeof ram, false, error, sizeof error));
  ReadExpected(previous);
  memset(ram, 0, sizeof ram);
  assert(Dkc1SramLoad(&store, ram, sizeof ram, error, sizeof error));
  assert(memcmp(ram, previous, sizeof ram) == 0);

  /* Force a real fopen failure without relying on platform ACL semantics. */
  snprintf(temporary, sizeof temporary, "saves/save.srm.%ld.tmp", (long)ProcessId());
  assert(MakeDir(temporary) == 0);
  assert(Dkc1SramFlush(&store, ram, sizeof ram, false, error, sizeof error));
  ram[91] ^= 0xff;
  assert(!Dkc1SramFlush(&store, ram, sizeof ram, false, error, sizeof error));
  ReadExpected(previous);
  assert(memcmp(store.persisted, previous, sizeof ram) == 0);
  assert(RemoveDir(temporary) == 0);
  assert(Dkc1SramFlush(&store, ram, sizeof ram, false, error, sizeof error));
  ReadExpected(previous); /* retry is deferred, not reported as persisted */
  assert(Dkc1SramFlush(&store, ram, sizeof ram, true, error, sizeof error));
  ReadExpected(ram);

  /* Truncated and oversized files must leave both RAM and disk untouched. */
  for (int length = 2047; length <= 2049; length += 2) {
    FILE *file = fopen("saves/save.srm", "wb");
    assert(file);
    for (int i = 0; i < length; i++) assert(fputc(0x5a, file) == 0x5a);
    assert(fclose(file) == 0);
    memcpy(previous, ram, sizeof ram);
    assert(!Dkc1SramLoad(&store, ram, sizeof ram, error, sizeof error));
    assert(memcmp(previous, ram, sizeof ram) == 0);
    assert(!store.ready);
    assert(Dkc1SramFlush(&store, ram, sizeof ram, true, error, sizeof error));
    file = fopen("saves/save.srm", "rb");
    assert(file);
    for (int i = 0; i < length; i++) assert(fgetc(file) == 0x5a);
    assert(fgetc(file) == EOF);
    assert(fclose(file) == 0);
  }
  assert(!Dkc1SramLoad(&store, ram, 1024, error, sizeof error));
  puts("SRAM persistence, atomic failure recovery, and invalid-file tests passed");
  return 0;
}
