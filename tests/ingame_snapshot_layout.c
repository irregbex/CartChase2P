#include <stddef.h>
#include <stdio.h>
#include "snes/cpu.h"
#include "snes/apu.h"
#include "snes/dsp.h"

int main(void) {
  /* common_rtl.c / snes_saveload / apu_saveload / dsp_saveload order. */
  size_t offset = 8 + sizeof(Cpu) - offsetof(Cpu, a)
      + offsetof(Apu, pad) + 6 - offsetof(Apu, ram)
      + offsetof(Dsp, sampleRead) - offsetof(Dsp, ram);
  printf("DSP host sampleRead: offset=0x%zx size=%zu\n", offset,
         sizeof(((Dsp *)0)->sampleRead));
  size_t ring = offset - offsetof(Dsp, sampleRead) + offsetof(Dsp, sampleBuffer);
  printf("DSP host sampleBuffer: offset=0x%zx ring+cursor size=%zu\n",
         ring, offset + 4 - ring);
  return offset == 0x183c0 && sizeof(((Dsp *)0)->sampleRead) == 4 &&
         ring == 0x103bc && offset + 4 - ring == 0x8008 ? 0 : 1;
}
