/* Cart Chase 2P - custom stages for the two-game race.
 *
 * A stage file (tools/cart2p/gen_stage.py) is a re-cut of a minecart level
 * (Mine Cart Carnage or Mine Cart Madness): its two tilemap layers rebuilt
 * column by column from pieces of the original, plus the matching object
 * list and banana groups. It is installed into a game that already sits in
 * that level (the race start state):
 *
 *  - The level reads its tilemap and metatiles through the pointer $D3 with
 *    bank $D5 (CODE_818C66), for drawing (CODE_818705) and for collision
 *    (CODE_818000): all readers go through it. Layer 2 is always layer 1 +
 *    $4B00 (CODE_818B6B). The new maps and a copy of the metatiles ($1B11,
 *    same bank) are placed in WRAM $7F:3800-$7F:EAC0 (free in both cart
 *    stages, BITACORA 2026-10-06) and $D3/$D5/$1B11 are pointed there.
 *  - The level object list lives in ROM bank $BD (DATA_BD8000[entrance]):
 *    it is rewritten in place in the ROM image in memory, same size.
 *  - Banana groups are built into $7EC000 at level start from a ROM table
 *    (DATA_B8C0E1[DATA_B8B41D[entrance]], CODE_B8B580): the table is
 *    rewritten in place and the builder is run again (from $B8:B5B4, after
 *    its HUD setup).
 *  - The camera limit $1B25 is set to the new length.
 *  - Format 3 also carries the camera zones (CODE_BCB069: per zone, start
 *    X, min and max camera Y, flags; DATA_BC8000[entrance] points to them
 *    in bank $BC). They are written over the zone tables of non-cart levels
 *    at $BC:9DEA-$BC:AC88 (the race never leaves the cart stage) and the
 *    entrance's pointer is redirected; the zone cursor $1AF9 restarts at 0.
 *  - Every ROM byte patched is backed up first and restored before the next
 *    install (Cart2pStageRestoreRom), so a list can go back to an original
 *    stage after a remix.
 *
 * DKC1_CART2P_SURVEY=file writes the rail profile of the loaded level by
 * asking the game's own terrain probe (CODE_818003: A = X, X = actor slot
 * whose Y is probed) for every X; the generator cuts the level only where
 * the rail is flat on both sides. */
#include "cart2p.h"
#include "cart2p_receta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "funcs.h"
#include "snes/cart.h"
#include "snes/ppu.h"
#include "snes/snes.h"

extern Snes *g_snes;

enum {
  kLayer2Offset = 0x4B00,       /* layer 2 map = layer 1 map + $4B00 */
  kColumnBytes = 32,            /* 16 cells of 32x32 px, 2 bytes each */
  kMaxColumns = kLayer2Offset / kColumnBytes,
  kWramMap = 0x7F3800,
  kWramFreeEnd = 0x7FEAC0,
  kObjectTableRom = 0xBD8000,   /* DATA_BD8000: word pointer per entrance */
  kObjectListBank = 0xBD0000,
  kBananaIndexRom = 0xB8B41D,   /* DATA_B8B41D: byte index per entrance */
  kBananaTableRom = 0xB8C0E1,   /* DATA_B8C0E1: word offsets, then lists */
  kBananaBuilderPc = 0xB8B5B4,  /* LDA $3E inside CODE_B8B580 */
  kMaxObjectBytes = 0x400,
  kMaxBananaBytes = 0x100,
  kMaxMetatileBytes = 0x2000,
  kCameraTableRom = 0xBC8000,   /* DATA_BC8000: word pointer per entrance */
  kCameraScratch = 0xBC9DEA,    /* zone tables of non-cart levels ... */
  kCameraScratchEnd = 0xBCAC88, /* ... up to the last table's start */
  kWramZoneCursor = 0x1AF9,
  kMaxPatches = 8,
  kProbeSlot = 0x02,            /* DK: its Y is the probe's Y */
  kWramYPos = 0x0BC1,
  kWramCurrentIndex = 0x08AB,
  /* v1 files (Carnage only) */
  kV1Entrance = 0x002E,
  kV1ObjectBytes = 41 * 8,
  kV1BananaBytes = 22 * 4,
  kV1MetatileBytes = 0x1CC0,
};

static const char kMagicV1[8] = "C2PSTG1";
static const char kMagicV2[8] = "C2PSTG2";
static const char kMagicV3[8] = "C2PSTG3";

typedef struct {
  char magic[8];
  uint16_t entrance;
  uint16_t columns;
  uint16_t camera_max;
  uint16_t object_count;
  uint16_t banana_count;
  uint16_t reserved;
} StageHeaderV1;

typedef struct {
  char magic[8];
  uint16_t entrance;
  uint16_t columns;
  uint16_t camera_max;
  uint16_t object_count;
  uint16_t banana_count;
  uint16_t metatile_bytes;
  uint16_t object_bytes;
  uint16_t banana_bytes;
} StageHeaderV2;

typedef struct {
  StageHeaderV2 v2; /* magic "C2PSTG3" */
  uint16_t camera_bytes;
  /* low byte: 0 or theme id (RecTemaNombre): skin tema_<name>.c2t;
   * high byte: 0 or song + 1 (RecMusicaNombre): music of another level */
  uint16_t theme;
} StageHeaderV3;

/* ROM image bytes changed by an install, with their original contents. */
typedef struct {
  uint8_t *at;
  uint8_t *saved;
  uint32_t len;
} RomPatch;

static RomPatch s_patches[kMaxPatches];
static int s_patch_count;

static bool PatchRom(uint8_t *at, const void *data, uint32_t len) {
  int i = 0;
  for (; i < s_patch_count; i++)
    if (s_patches[i].at == at && s_patches[i].len >= len)
      break;
  if (i == s_patch_count) {
    if (s_patch_count == kMaxPatches)
      return false;
    uint8_t *saved = (uint8_t *)malloc(len);
    if (!saved)
      return false;
    memcpy(saved, at, len);
    s_patches[i] = (RomPatch){at, saved, len};
    s_patch_count++;
  }
  memcpy(at, data, len);
  return true;
}

void Cart2pStageRestoreRom(void) {
  for (int i = 0; i < s_patch_count; i++)
    memcpy(s_patches[i].at, s_patches[i].saved, s_patches[i].len);
}

/* Copy of the ROM image without the patches of the installed stage (the
 * original levels), for the recipes (cart2p_receta.c). Built once. */
const uint8_t *Cart2pStagePristineRom(size_t *size) {
  static uint8_t *copy;
  static size_t copy_size;
  if (!copy) {
    if (!g_snes || !g_snes->cart || !g_snes->cart->rom)
      return NULL;
    copy_size = g_snes->cart->romSize;
    copy = (uint8_t *)malloc(copy_size);
    if (!copy)
      return NULL;
    memcpy(copy, g_snes->cart->rom, copy_size);
    for (int i = 0; i < s_patch_count; i++)
      memcpy(copy + (s_patches[i].at - g_snes->cart->rom), s_patches[i].saved,
             s_patches[i].len);
  }
  *size = copy_size;
  return copy;
}

static uint16_t Rd16(uint32_t addr) {
  return (uint16_t)(g_ram[addr] | g_ram[addr + 1] << 8);
}

static void Wr16(uint32_t addr, uint16_t value) {
  g_ram[addr] = (uint8_t)value;
  g_ram[addr + 1] = (uint8_t)(value >> 8);
}

/* WRAM offset of a $7E/$7F address. */
static uint32_t WramOffset(uint32_t addr) {
  return addr - 0x7E0000u;
}

/* HiROM image byte: banks $C0-$FF and their $80-$BF mirrors. */
static uint8_t *RomAt(uint32_t addr, uint32_t len) {
  if (!g_snes || !g_snes->cart || !g_snes->cart->rom)
    return NULL;
  const uint32_t bank = addr >> 16, lo = addr & 0xFFFFu;
  uint32_t offset;
  if (bank >= 0xC0)
    offset = ((bank - 0xC0u) << 16) | lo;
  else if (bank >= 0x80 && lo >= 0x8000)
    offset = ((bank - 0x80u) << 16) | lo;
  else
    return NULL;
  if (offset + len > g_snes->cart->romSize)
    return NULL;
  return g_snes->cart->rom + offset;
}

static uint16_t RomWord(const uint8_t *p) {
  return (uint16_t)(p[0] | p[1] << 8);
}

/* Object list of the loaded level in the ROM image ($BD bank). */
static uint8_t *ObjectList(uint32_t len) {
  const uint8_t *table = RomAt(kObjectTableRom + Rd16(0x003E) * 2u, 2);
  if (!table)
    return NULL;
  return RomAt(kObjectListBank | RomWord(table), len);
}

/* Banana group table of the loaded level in the ROM image ($B8 bank). */
static uint8_t *BananaTable(uint32_t len) {
  const uint8_t *index = RomAt(kBananaIndexRom + Rd16(0x003E), 1);
  if (!index)
    return NULL;
  const uint8_t *offset = RomAt(kBananaTableRom + *index * 2u, 2);
  if (!offset)
    return NULL;
  return RomAt(kBananaTableRom + RomWord(offset), len);
}

uint16_t Cart2pStageExitX(void) {
  const uint8_t *list = ObjectList(kMaxObjectBytes);
  if (!list)
    return 0;
  for (uint32_t i = 0; i < kMaxObjectBytes / 8; i++) {
    const uint16_t type = RomWord(list + i * 8);
    if (type == 0)
      break;
    if (type == 2)
      return RomWord(list + i * 8 + 2);
  }
  return 0;
}

static void PrepareScratch(CpuState *scratch, uint8_t bank) {
  *scratch = g_cpu;
  scratch->D = 0;
  scratch->DB = bank;
  scratch->PB = bank;
  scratch->m_flag = 0;
  scratch->x_flag = 0;
  scratch->P = (uint8_t)(scratch->P & ~0x30u);
  scratch->host_return_valid = 0;
  scratch->master_cycles = 0;
  cpu_push_jsl_return_frame(scratch);
}

/* ---- Rail survey ------------------------------------------------------------ */

/* Floor under (x, y) as the game sees it: Y of the surface, or -1. The probe
 * looks at the 32 px cell holding y and the two below it. */
static int Probe(uint16_t x, uint16_t y, uint16_t *type) {
  uint8_t dp[0x60];
  memcpy(dp, g_ram + 0x40, sizeof dp);
  const uint16_t index = Rd16(kWramCurrentIndex);
  const uint16_t y_saved = Rd16(kWramYPos + kProbeSlot);
  Wr16(kWramYPos + kProbeSlot, y);
  CpuState scratch;
  PrepareScratch(&scratch, 0x81);
  scratch.A = x;
  scratch.X = kProbeSlot;
  scratch.Y = 0;
  RtlSpeculativeExecutionBegin();
  const RecompReturn r = CODE_818003_M0X0(&scratch);
  RtlSpeculativeExecutionEnd();
  int result = -1;
  if (r == RECOMP_RETURN_NORMAL && scratch.A < 0x0200)
    result = scratch.A;
  *type = Rd16(0x009C);
  memcpy(g_ram + 0x40, dp, sizeof dp);
  Wr16(kWramCurrentIndex, index);
  Wr16(kWramYPos + kProbeSlot, y_saved);
  return result;
}

void Cart2pStageSurvey(void) {
  const char *path = getenv("DKC1_CART2P_SURVEY");
  if (!path || !*path)
    return;
  FILE *f = fopen(path, "w");
  if (!f) {
    Cart2pLog("sondeo: no se pudo crear %s", path);
    return;
  }
  const int x_end = Rd16(0x1B25) + 0x140;
  int surfaces = 0;
  fprintf(f, "# entrada %04X D3 %04X D5 %04X 1B11 %04X 1B25 %04X\n",
          Rd16(0x003E), Rd16(0x00D3), Rd16(0x00D5), Rd16(0x1B11),
          Rd16(0x1B25));
  fprintf(f, "# x y tipo (hex); y crece hacia arriba\n");
  for (int x = 0; x < x_end; x += 4) {
    int y = 0x1F0;
    while (y > 0) {
      uint16_t type = 0;
      const int floor = Probe((uint16_t)x, (uint16_t)y, &type);
      if (floor < 0) {
        y -= 0x60;
        continue;
      }
      fprintf(f, "%04X %04X %04X\n", x, floor, type);
      surfaces++;
      /* next cell down (a surface reported above the probe would loop) */
      y = ((floor < y ? floor : y) & ~0x1F) - 1;
    }
  }
  fclose(f);
  Cart2pLog("sondeo de vias: %d superficies hasta X=$%04X -> %s", surfaces,
            x_end, path);
}

/* ---- Graphics export (stage editor) ---------------------------------------- */

/* DKC1_CART2P_EXPORT=file: the background graphics of the loaded level for
 * tools/cart2p/exportar_editor.py, which draws the whole level with them:
 *   "C2PGFX1\0", u16 entrance, u16 bgmode, u16 BG1 char base, u16 BG2 char
 *   base (word addresses), u16 $D3, u16 $D5, u16 $1B11, u16 reserved,
 *   vram[0x8000] words, cgram[0x100] words (little endian). */
void Cart2pStageExportGraphics(void) {
  const char *path = getenv("DKC1_CART2P_EXPORT");
  if (!path || !*path || !g_snes || !g_snes->ppu)
    return;
  FILE *f = fopen(path, "wb");
  if (!f) {
    Cart2pLog("export: no se pudo crear %s", path);
    return;
  }
  const Ppu *ppu = g_snes->ppu;
  const uint16_t header[8] = {
      Rd16(0x003E),
      ppu->bgmode,
      (uint16_t)PPU_bgTileAdr(ppu, 0),
      (uint16_t)PPU_bgTileAdr(ppu, 1),
      Rd16(0x00D3),
      g_ram[0x00D5],
      Rd16(0x1B11),
      (uint16_t)(ppu->screenEnabled[0] | ppu->screenEnabled[1] << 8)}; /* TM, TS */
  fwrite("C2PGFX1", 1, 8, f);
  fwrite(header, sizeof header, 1, f);
  fwrite(ppu->vram, sizeof ppu->vram, 1, f);
  fwrite(ppu->cgram, sizeof ppu->cgram, 1, f);
  fclose(f);
  Cart2pLog("export: graficos de la entrada $%04X -> %s", Rd16(0x003E), path);
}

/* ---- Theme export (development tool) ---------------------------------------- */

/* Graphics of another level ("theme") for tools/cart2p/temas.py: sets the
 * entrance $3E and runs that level's video setup (the routine its init calls
 * first through DATA_F9801E: video mode, map set CODE_818C66, CHR lists
 * CODE_F9A924, palette CODE_B999F1), then dumps the same as
 * Cart2pStageExportGraphics plus the map set pointers ($D7/$D9 collision,
 * $DB solid metatiles). Clobbers the loaded game: the caller restores it. */
bool Cart2pStageExportTheme(uint16_t entrance, const char *path) {
  const uint8_t *table = RomAt(0xF9801E + entrance * 2u, 2);
  if (!table)
    return false;
  const uint32_t start = 0xF90000u | RomWord(table);
  uint8_t *init = RomAt(start, 0x100);
  if (!init)
    return false;
  uint32_t setup = start;
  uint8_t *stop = NULL, stop_saved = 0;
  if (init[0] == 0x20) { /* JSR setup: a routine of its own */
    setup = 0xF90000u | RomWord(init + 1);
  } else {
    /* Setup inline in the init: return right after the palette load
     * (JSL $B999F1), with an RTS put there in the ROM image for the call. */
    for (int i = 0; i + 4 < 0x100 && !stop; i++)
      if (init[i] == 0x22 && init[i + 1] == 0xF1 && init[i + 2] == 0x99 &&
          init[i + 3] == 0xB9)
        stop = init + i + 4;
    if (!stop) {
      Cart2pLog("tema $%04X: sin carga de paleta en el arranque", entrance);
      return false;
    }
    stop_saved = *stop;
    *stop = 0x60;
  }
  Wr16(0x003E, entrance);
  CpuState scratch = g_cpu;
  scratch.D = 0;
  scratch.DB = 0xF9;
  scratch.PB = 0xF9;
  scratch.m_flag = 0;
  scratch.x_flag = 0;
  scratch.P = (uint8_t)(scratch.P & ~0x30u);
  scratch.host_return_valid = 0;
  scratch.master_cycles = 0;
  cpu_push_jsr_return_frame(&scratch);
  const RecompReturn r =
      interp_tier_run_call_frame(&scratch, setup, setup, 2, NULL);
  if (stop)
    *stop = stop_saved;
  if (r != RECOMP_RETURN_NORMAL) {
    Cart2pLog("tema $%04X: el arranque $%06X devolvio %d", entrance, setup,
              (int)r);
    return false;
  }
  FILE *f = fopen(path, "wb");
  if (!f)
    return false;
  const Ppu *ppu = g_snes->ppu;
  const uint16_t header[16] = {
      entrance, ppu->bgmode, (uint16_t)PPU_bgTileAdr(ppu, 0),
      (uint16_t)PPU_bgTileAdr(ppu, 1), Rd16(0x00D3), g_ram[0x00D5],
      Rd16(0x1B11), Rd16(0x00D7), g_ram[0x00D9], Rd16(0x00DB), Rd16(0x1B0F),
      Rd16(0x1B13), (uint16_t)setup, 0, 0, 0};
  fwrite("C2PTEMA", 1, 8, f);
  fwrite(header, sizeof header, 1, f);
  fwrite(ppu->vram, sizeof ppu->vram, 1, f);
  fwrite(ppu->cgram, sizeof ppu->cgram, 1, f);
  fwrite(g_ram, 0x20000, 1, f); /* WRAM: palette buffers, decompressed data */
  fclose(f);
  Cart2pLog("tema $%04X: arranque $%06X, BG1 chars $%04X, mapa %02X:%04X, "
            "metatiles $%04X, colision %02X:%04X -> %s", entrance, setup,
            header[2], header[5], header[4], header[6], header[8], header[7],
            path);
  return true;
}

/* ---- Stage install ---------------------------------------------------------- */

static uint8_t *s_file;
static size_t s_file_size;
static char s_file_path[1024];

static bool ReadFile(const char *path) {
  if (s_file && strcmp(s_file_path, path) == 0)
    return true;
  free(s_file);
  s_file = NULL;
  s_file_size = 0;
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n <= 0 || n > (4 << 20)) {
    fclose(f);
    return false;
  }
  s_file = (uint8_t *)malloc((size_t)n);
  if (!s_file) {
    fclose(f);
    return false;
  }
  s_file_size = fread(s_file, 1, (size_t)n, f);
  fclose(f);
  if (s_file_size != (size_t)n)
    return false;
  snprintf(s_file_path, sizeof s_file_path, "%s", path);
  return true;
}

static bool RebuildBananas(void) {
  CpuState scratch;
  PrepareScratch(&scratch, 0xB8);
  const RecompReturn r = interp_tier_run_call_frame(
      &scratch, kBananaBuilderPc, kBananaBuilderPc, 3, NULL);
  if (r != RECOMP_RETURN_NORMAL) {
    Cart2pLog("etapa: el constructor de platanos devolvio %d", (int)r);
    return false;
  }
  return true;
}

/* Music of another level: the game's own song change (CODE_B99036: stop
 * the song, upload the new song's sample set after the common one, upload
 * and start the song; what every level start does) run on the loaded game.
 * It writes $4200 = $01 (no NMI) through CODE_B99061; the level code would
 * enable it again later, here it is restored as it was. */
static bool PlaySong(uint16_t song) {
  if (!g_snes)
    return false;
  Snes *s = g_snes;
  const bool nmi = s->nmiEnabled, hirq = s->hIrqEnabled,
             virq = s->vIrqEnabled, joy = s->autoJoyRead;
  CpuState scratch;
  PrepareScratch(&scratch, 0xB9);
  scratch.A = song;
  const RecompReturn r =
      interp_tier_run_call_frame(&scratch, 0xB99036, 0xB99036, 3, NULL);
  s->nmiEnabled = nmi;
  s->hIrqEnabled = hirq;
  s->vIrqEnabled = virq;
  s->autoJoyRead = joy;
  if (r != RECOMP_RETURN_NORMAL) {
    Cart2pLog("musica: el cambio de cancion devolvio %d", (int)r);
    return false;
  }
  return true;
}

bool Cart2pStageInstallFile(const char *path) {
  if (!path || !*path)
    return false;
  if (!ReadFile(path)) {
    Cart2pLog("etapa: no se pudo leer %s", path);
    return false;
  }
  /* Back to the original ROM first (a previous install may have patched
   * this or another level). */
  Cart2pStageRestoreRom();
  StageHeaderV2 h;
  size_t header_size;
  uint16_t camera_bytes = 0, theme = 0;
  if (s_file_size >= sizeof(StageHeaderV3) &&
      memcmp(s_file, kMagicV3, sizeof kMagicV3) == 0) {
    StageHeaderV3 v3;
    memcpy(&v3, s_file, sizeof v3);
    h = v3.v2;
    camera_bytes = v3.camera_bytes;
    theme = v3.theme;
    header_size = sizeof(StageHeaderV3);
  } else if (s_file_size >= sizeof(StageHeaderV2) &&
             memcmp(s_file, kMagicV2, sizeof kMagicV2) == 0) {
    memcpy(&h, s_file, sizeof h);
    header_size = sizeof(StageHeaderV2);
  } else if (s_file_size >= sizeof(StageHeaderV1) &&
             memcmp(s_file, kMagicV1, sizeof kMagicV1) == 0) {
    StageHeaderV1 v1;
    memcpy(&v1, s_file, sizeof v1);
    memset(&h, 0, sizeof h);
    h.entrance = v1.entrance;
    h.columns = v1.columns;
    h.camera_max = v1.camera_max;
    h.object_count = v1.object_count;
    h.banana_count = v1.banana_count;
    h.metatile_bytes = kV1MetatileBytes;
    h.object_bytes = kV1ObjectBytes;
    h.banana_bytes = kV1BananaBytes;
    header_size = sizeof(StageHeaderV1);
    if (h.entrance != kV1Entrance) {
      Cart2pLog("etapa: formato 1 solo para Mine Cart Carnage");
      return false;
    }
  } else {
    Cart2pLog("etapa: formato desconocido (%zu bytes)", s_file_size);
    return false;
  }
  const size_t layer_bytes = (size_t)h.columns * kColumnBytes;
  const size_t need = header_size + 2 * layer_bytes + h.object_bytes +
                      h.banana_bytes + camera_bytes;
  if (h.columns == 0 || h.columns > kMaxColumns || s_file_size < need ||
      h.object_bytes == 0 || h.object_bytes > kMaxObjectBytes ||
      h.banana_bytes > kMaxBananaBytes || h.metatile_bytes == 0 ||
      h.metatile_bytes > kMaxMetatileBytes ||
      camera_bytes > kCameraScratchEnd - kCameraScratch ||
      (camera_bytes && (camera_bytes < 10 || camera_bytes % 8 != 2))) {
    Cart2pLog("etapa: formato invalido (%zu bytes, %u columnas, %u/%u/%u "
              "bytes de objetos/platanos/metatiles)", s_file_size, h.columns,
              h.object_bytes, h.banana_bytes, h.metatile_bytes);
    return false;
  }
  if (Rd16(0x003E) != h.entrance) {
    Cart2pLog("etapa: la partida esta en la entrada $%04X, la etapa es para "
              "$%04X", Rd16(0x003E), h.entrance);
    return false;
  }
  const uint8_t map_bank = g_ram[0x00D5];
  if (map_bank < 0x80 || map_bank == 0x7E || map_bank == 0x7F) {
    Cart2pLog("etapa: el mapa no esta en ROM ($D5=%02X): ya instalada?",
              map_bank);
    return false;
  }
  /* WRAM layout: layer 1, layer 2 at +$4B00, metatiles right after. */
  const uint32_t wram_metatiles =
      kWramMap + kLayer2Offset + (uint32_t)layer_bytes;
  if (wram_metatiles + h.metatile_bytes > kWramFreeEnd) {
    Cart2pLog("etapa: no cabe en WRAM libre (%u columnas + %u bytes de "
              "metatiles)", h.columns, h.metatile_bytes);
    return false;
  }
  const uint8_t *metatiles =
      RomAt(((uint32_t)map_bank << 16) | Rd16(0x1B11), h.metatile_bytes);
  uint8_t *objects = ObjectList(h.object_bytes);
  uint8_t *bananas = h.banana_bytes ? BananaTable(h.banana_bytes) : NULL;
  uint8_t *camera_ptr = RomAt(kCameraTableRom + h.entrance * 2u, 2);
  uint8_t *camera = camera_bytes ? RomAt(kCameraScratch, camera_bytes) : NULL;
  if (!metatiles || !objects || (h.banana_bytes && !bananas) || !camera_ptr ||
      (camera_bytes && !camera)) {
    Cart2pLog("etapa: sin acceso a la imagen ROM");
    return false;
  }
  const uint8_t *layer1 = s_file + header_size;
  const uint8_t *layer2 = layer1 + layer_bytes;
  const uint8_t *object_list = layer2 + layer_bytes;
  const uint8_t *banana_table = object_list + h.object_bytes;
  const uint8_t *camera_table = banana_table + h.banana_bytes;

  /* Tilemaps + metatiles into WRAM $7F; empty cells past the last column. */
  uint8_t *map = g_ram + WramOffset(kWramMap);
  memset(map, 0, kLayer2Offset + layer_bytes);
  memcpy(map, layer1, layer_bytes);
  memcpy(map + kLayer2Offset, layer2, layer_bytes);
  memcpy(g_ram + WramOffset(wram_metatiles), metatiles, h.metatile_bytes);
  Wr16(0x00D3, (uint16_t)(kWramMap & 0xFFFF));
  g_ram[0x00D5] = (uint8_t)(kWramMap >> 16);
  Wr16(0x1B11, (uint16_t)(wram_metatiles & 0xFFFF));
  Wr16(0x1B25, h.camera_max);

  /* Objects, bananas and camera zones: ROM image in memory (backed up),
   * then the stock banana builder. */
  bool rom_ok = PatchRom(objects, object_list, h.object_bytes);
  if (h.banana_bytes)
    rom_ok &= PatchRom(bananas, banana_table, h.banana_bytes);
  if (camera_bytes) {
    const uint8_t pointer[2] = {(uint8_t)(kCameraScratch & 0xFF),
                                (uint8_t)((kCameraScratch >> 8) & 0xFF)};
    rom_ok &= PatchRom(camera, camera_table, camera_bytes);
    rom_ok &= PatchRom(camera_ptr, pointer, 2);
    Wr16(kWramZoneCursor, 0);
  }
  if (!rom_ok)
    Cart2pLog("etapa: sin espacio para respaldar la ROM (al volver a una "
              "etapa original puede quedar mezclada)");
  const bool bananas_ok = RebuildBananas();
  Cart2pLog("etapa instalada: %s (%u columnas, camara hasta $%04X, %u objetos, "
            "%u grupos de platanos, metatiles %u bytes en $%06X, %u zonas de "
            "camara%s)", path, h.columns, h.camera_max, h.object_count,
            h.banana_count, h.metatile_bytes, wram_metatiles,
            camera_bytes ? (camera_bytes - 2u) / 8u : 0u,
            bananas_ok ? "" : ", platanos NO reconstruidos");
  const uint16_t music = theme >> 8;
  theme &= 0xFF;
  if (music) {
    const char *name = RecMusicaNombre(music);
    if (name && PlaySong((uint16_t)(music - 1)))
      Cart2pLog("musica: %s (cancion $%02X)", name, music - 1);
    else
      Cart2pLog("etapa: no se pudo poner la musica %u", music);
  }
  if (theme) {
    /* the skin lives next to the stage: <dir>/tema_<name>.c2t */
    const char *name = RecTemaNombre(theme);
    char skin[1100];
    const char *slash = strrchr(path, '\\');
    const char *slash2 = strrchr(path, '/');
    if (!slash || (slash2 && slash2 > slash))
      slash = slash2;
    const int dir = slash ? (int)(slash - path + 1) : 0;
    /* Madness has its own skins (tools/cart2p/temas.py nombre_piel) */
    snprintf(skin, sizeof skin, "%.*stema_%s%s.c2t", dir, path, name ? name : "?",
             h.entrance == 0x27 /* Mine Cart Madness */ ? "_madness" : "");
    if (!name || !Cart2pStageInstallSkin(skin))
      Cart2pLog("etapa: no se pudo poner el tema %u (%s)", theme, skin);
  }
  return true;
}

/* ---- Theme skin ---------------------------------------------------------- */

/* A skin (.c2t, tools/cart2p/temas.py) dresses an installed stage in the
 * graphics of another world: layer 1 keeps only the metatiles with collision
 * (rails, posts, start platform; indexes below `keep`, so the $D7 collision
 * table still applies), redrawn with palette row 7; layer 2 becomes the
 * theme's scenery (a window of k columns repeated), drawn by the game as BG2
 * behind the track. Metatiles, BG1/BG2 characters ($2000) and palette rows
 * 0-7 are replaced, BG3 (the cave wall of Carnage) is cleared, and the
 * visible part of both tilemaps is redrawn at once. */
typedef struct {
  char magic[8];
  uint16_t entrance, keep, k, metatiles;
  uint16_t track_base, track_tiles;   /* BG1 characters (VRAM word address) */
  uint16_t theme_base, theme_tiles;   /* BG2 characters (0 tiles: shared) */
  uint16_t flags;                     /* bit 0: clear BG3 */
  uint16_t reserved[3];
  char title[24];
} SkinHeader;

static const char kSkinMagic[8] = "C2PPIEL2";

enum {
  kSkinRows = 16,               /* cells per column */
};

/* Writes the tilemap of one BG (layer map in WRAM, 2 bytes per cell) for the
 * area its scroll shows, as CODE_818705 draws it. */
static void SkinRedraw(Ppu *ppu, int layer, const uint8_t *map, int columns,
                       const uint8_t *metatiles) {
  const uint16_t sc = ppu->bgXsc[layer];
  const uint16_t base = (uint16_t)((sc & 0xFC) << 8);
  const bool wide = sc & 1;
  const int x0 = ppu->hScroll[layer] / 8 - 2, y0 = ppu->vScroll[layer] / 8 - 2;
  for (int ty = y0; ty < y0 + 32; ty++) {
    if (ty < 0 || ty >= kSkinRows * 4)
      continue;
    for (int tx = x0; tx < x0 + (wide ? 64 : 32); tx++) {
      if (tx < 0 || tx >= columns * 4)
        continue;
      const uint16_t cell =
          RomWord(map + (tx / 4) * kColumnBytes + (ty / 4) * 2);
      const uint16_t idx = cell & 0x7FF;
      int r = ty & 3, q = tx & 3;
      if (cell & 0x8000)
        r = 3 - r;
      if (cell & 0x4000)
        q = 3 - q;
      const uint16_t word =
          (uint16_t)(RomWord(metatiles + idx * 32 + r * 8 + q * 2) ^ (cell & 0xC000));
      const uint16_t at = (uint16_t)(base + ((wide && (tx & 32)) ? 0x400 : 0) +
                                     (ty & 31) * 32 + (tx & 31));
      ppu->vram[at & 0x7FFF] = word;
    }
  }
}

bool Cart2pStageInstallSkin(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    Cart2pLog("piel: no se pudo leer %s", path);
    return false;
  }
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *data = n > (long)sizeof(SkinHeader) && n < (1 << 20)
                      ? (uint8_t *)malloc((size_t)n) : NULL;
  const bool read_ok = data && fread(data, 1, (size_t)n, f) == (size_t)n;
  fclose(f);
  if (!read_ok) {
    free(data);
    return false;
  }
  SkinHeader h;
  memcpy(&h, data, sizeof h);
  const size_t meta_bytes = (size_t)h.metatiles * 32;
  const size_t window_bytes = (size_t)h.k * kColumnBytes;
  const size_t tiles_bytes = ((size_t)h.track_tiles + h.theme_tiles) * 32;
  const size_t need = sizeof h + meta_bytes + window_bytes + tiles_bytes + 256;
  bool ok = memcmp(h.magic, kSkinMagic, sizeof kSkinMagic) == 0 && (size_t)n >= need &&
            h.k > 0 && h.keep <= h.metatiles &&
            h.track_base + (uint32_t)h.track_tiles * 16 <= 0x8000 &&
            h.theme_base + (uint32_t)h.theme_tiles * 16 <= 0x8000;
  if (ok && Rd16(0x003E) != h.entrance) {
    Cart2pLog("piel: es para la entrada $%04X, la partida esta en $%04X",
              h.entrance, Rd16(0x003E));
    ok = false;
  }
  if (ok && g_ram[0x00D5] != (uint8_t)(kWramMap >> 16)) {
    Cart2pLog("piel: necesita una etapa instalada (mapa en WRAM)");
    ok = false;
  }
  const uint32_t meta_at = 0x7F0000u | Rd16(0x1B11);
  if (ok && meta_at + meta_bytes > kWramFreeEnd) {
    Cart2pLog("piel: %u metatiles no caben en WRAM", h.metatiles);
    ok = false;
  }
  if (!ok || !g_snes || !g_snes->ppu) {
    free(data);
    return false;
  }
  const uint8_t *meta = data + sizeof h;
  const uint8_t *window = meta + meta_bytes;
  const uint8_t *tiles = window + window_bytes;
  const uint8_t *cgram = tiles + tiles_bytes;
  /* the installer puts the metatiles right after layer 2 */
  const int columns =
      (int)(meta_at - (kWramMap + kLayer2Offset)) / kColumnBytes;
  uint8_t *map1 = g_ram + WramOffset(kWramMap);
  uint8_t *map2 = map1 + kLayer2Offset;
  for (int c = 0; c < columns; c++)
    for (int k = 0; k < kSkinRows; k++) {
      uint8_t *cell = map1 + c * kColumnBytes + k * 2;
      if ((RomWord(cell) & 0x7FF) >= h.keep)
        cell[0] = cell[1] = 0;
    }
  for (int c = 0; c < columns; c++)
    memcpy(map2 + c * kColumnBytes, window + (c % h.k) * kColumnBytes, kColumnBytes);
  memcpy(g_ram + WramOffset(meta_at), meta, meta_bytes);
  Ppu *ppu = g_snes->ppu;
  for (size_t i = 0; i < (size_t)h.track_tiles * 16; i++)
    ppu->vram[h.track_base + i] = RomWord(tiles + i * 2);
  const uint8_t *theme_tiles = tiles + (size_t)h.track_tiles * 32;
  for (size_t i = 0; i < (size_t)h.theme_tiles * 16; i++)
    ppu->vram[h.theme_base + i] = RomWord(theme_tiles + i * 2);
  for (int i = 0; i < 128; i++)
    ppu->cgram[i] = RomWord(cgram + i * 2);
  if (h.flags & 1) {
    /* BG3 (Carnage's cave wall): empty map and blank character 0 */
    const uint16_t bg3 = (uint16_t)((ppu->bgXsc[2] & 0xFC) << 8);
    for (int i = 0; i < 0x400; i++)
      ppu->vram[(bg3 + i) & 0x7FFF] = 0;
    const uint16_t bg3_chars = (uint16_t)PPU_bgTileAdr(ppu, 2);
    for (int i = 0; i < 8; i++)
      ppu->vram[(bg3_chars + i) & 0x7FFF] = 0;
  }
  SkinRedraw(ppu, 0, map1, columns, meta);
  SkinRedraw(ppu, 1, map2, columns, meta);
  char title[25];
  memcpy(title, h.title, 24);
  title[24] = 0;
  Cart2pLog("piel instalada: %s (%s: %u metatiles, %u+%u teselas, ventana de %u "
            "columnas)", path, title, h.metatiles, h.track_tiles, h.theme_tiles, h.k);
  free(data);
  return true;
}

bool Cart2pStageInstall(void) {
  const char *path = getenv("DKC1_CART2P_STAGE");
  if (!path || !*path)
    return false;
  return Cart2pStageInstallFile(path);
}
