/* Cart Chase 2P - stage recipes in C. Port of tools/cart2p/etapa_lib.py
 * (levels, validation, documentation, compilation to .c2p v3) and of the
 * generator gen_stage.py, so the game itself can edit, compile and create
 * stages. The compiled .c2p of a recipe is byte-identical to the Python
 * compiler's (checked over every recipe, see BITACORA 2026-10-09).
 * Formats: cart2p/docs/FORMATO_ETAPAS.txt. */
#include "cart2p_receta.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cart2p.h"

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define MKDIR(p) _mkdir(p)
#else
#include <dirent.h>
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0777)
#endif

enum {
  kColPx = 32,
  kLayer2Offset = 0x4B00,
  kFirstCutMin = 48,
  kExtraCols = 12,
  kBananaXMax = 0x3FF0,
  kCameraMaxBytes = 0x0E00,
  kBlockMaxCols = 9,
  kMaxColsMem = 600,
};
static const uint32_t kWramLayer2EndBase = 0x7F3800 + 0x4B00;
static const uint32_t kWramFreeEnd = 0x7FEAC0;
static const double kSpeed = 3.5;

/* ---- small helpers ------------------------------------------------------ */

static uint16_t U16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static uint32_t RomOffset(int bank, int addr) {
  return bank >= 0xC0 ? (uint32_t)((bank - 0xC0) << 16 | addr)
                      : (uint32_t)((bank - 0x80) << 16 | addr);
}

static void Copy(char *dst, size_t size, const char *src) {
  snprintf(dst, size, "%s", src ? src : "");
}

static bool IsRail(int t) {
  return t == 0x40 || t == 0x41 || t == 0x42 || t == 0x43 || t == 0x44 ||
         t == 0x45 || t == 0x60 || t == 0xC0 || t == 0xC1 || t == 0xC2 ||
         t == 0xC3 || t == 0xC4 || t == 0xC5;
}

static bool IsFlat(int t) { return t == 0x40 || t == 0xC0; }

/* ---- paths ---------------------------------------------------------------- */

const char *RecDirCart2p(void) {
  static char dir[1024];
  if (dir[0])
    return dir;
  const char *lista = getenv("DKC1_CART2P_LISTA");
  if (!lista || !*lista)
    return "";
  Copy(dir, sizeof dir, lista);
  char *a = strrchr(dir, '\\'), *b = strrchr(dir, '/');
  char *s = a > b ? a : b;
  if (!s) {
    dir[0] = 0;
    return "";
  }
  s[1] = 0;
  return dir;
}

static void PathIn(char *out, size_t size, const char *sub, const char *name,
                   const char *ext) {
  snprintf(out, size, "%s%s%s%s", RecDirCart2p(), sub, name, ext);
}

/* ---- levels --------------------------------------------------------------- */

typedef struct {
  const char *nombre, *titulo, *estado, *perfil;
} NivelInfo;

static const NivelInfo kNiveles[2] = {
    {"carnage", "Mine Cart Carnage", "states\\carnage_montado.state",
     "vias_carnage.txt"},
    {"madness", "Mine Cart Madness", "states\\madness_montado.state",
     "vias_madness.txt"},
};

const char *RecNivelEstado(const char *nombre) {
  for (int i = 0; i < 2; i++)
    if (strcmp(kNiveles[i].nombre, nombre) == 0)
      return kNiveles[i].estado;
  return kNiveles[0].estado;
}

typedef struct {
  int sprite;
  const char *nombre;
  int antes, despues;
} SpriteInfo;

static const SpriteInfo kSprites[] = {
    {0x19, "ZINGER", 0x40, 0x200}, {0x4F, "NECKY", 0x40, 0x200},
    {0x4D, "GNAWTY", 0x80, 0x80},  {0x51, "CARRO", 0x80, 0x80},
    {0x16, "LETRA KONG", 0, 0},    {0x15, "RACIMO", 0, 0},
    {0x26, "BARRIL DK", 0, 0},     {0x38, "CANON", 0, 0},
    {0x2C, "COFRE", 0, 0},         {0x45, "FICHA", 0, 0},
    {0x4A, "BARRIL CONTROL", 0, 0}, {0x34, "GLOBO", 0, 0},
    {0x73, "SENAL", 0, 0},         {0x12, "OBJETO 12", 0, 0},
    {0x1B, "MEDIO NEUMATICO", 0, 0}, {0x1F, "NEUMATICO", 0, 0},
    {0x30, "CUERDA", 0, 0},        {0x6A, "PUERTA SALIDA", 0, 0},
};

static int SpriteId(const uint8_t *rom, size_t size, uint16_t ptr) {
  for (int k = 0; k < 8; k++) {
    const uint32_t o = RomOffset(0xB5, ptr);
    if (o + 4 > size)
      return -1;
    const uint16_t op = U16(rom + o);
    if (op == 0x8200)
      ptr = U16(rom + o + 2);
    else if (op == 0x0D45)
      return U16(rom + o + 2);
    else
      return -1;
  }
  return -1;
}

static int RailY(const Nivel *nv, int x, int *type) {
  const int i = (x & ~3) / 4;
  if (i < 0 || i >= nv->nx)
    return -1;
  int by = -1, bt = 0;
  for (int k = 0; k < nv->nsurf[i]; k++) {
    const int y = nv->surf_y[i][k], t = nv->surf_t[i][k];
    if (!IsRail(t))
      continue;
    if (y > by || (y == by && t > bt)) {
      by = y;
      bt = t;
    }
  }
  if (type)
    *type = bt;
  return by;
}

int RecRailY(const Nivel *nv, int x) {
  return RailY(nv, x, NULL);
}

static int Surfaces(const Nivel *nv, int x) {
  const int i = (x & ~3) / 4;
  return i >= 0 && i < nv->nx ? nv->nsurf[i] : 0;
}

static bool LoadSurvey(Nivel *nv, const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return false;
  char line[256];
  bool header = false;
  int d3 = 0, d5 = 0, mt = 0, cam = 0;
  /* first pass: header */
  while (fgets(line, sizeof line, f)) {
    if (line[0] == '#') {
      int e;
      if (sscanf(line, "# entrada %x D3 %x D5 %x 1B11 %x 1B25 %x", &e, &d3,
                 &d5, &mt, &cam) == 5) {
        nv->entrance = e;
        header = true;
      }
      continue;
    }
    break;
  }
  if (!header) {
    fclose(f);
    return false;
  }
  nv->map_bank = d5 & 0xFF;
  nv->map_addr = d3;
  nv->metatile_addr = mt;
  nv->camera_max = cam;
  nv->cols = (cam + 1) / kColPx + kExtraCols;
  nv->nx = nv->cols * 8 + 64;
  nv->surf_y = calloc((size_t)nv->nx, sizeof *nv->surf_y);
  nv->surf_t = calloc((size_t)nv->nx, sizeof *nv->surf_t);
  nv->nsurf = calloc((size_t)nv->nx, 1);
  rewind(f);
  while (fgets(line, sizeof line, f)) {
    if (line[0] == '#')
      continue;
    int x, y, t;
    if (sscanf(line, "%x %x %x", &x, &y, &t) != 3)
      continue;
    const int i = x / 4;
    if (i < 0 || i >= nv->nx || nv->nsurf[i] >= 6)
      continue;
    nv->surf_y[i][nv->nsurf[i]] = (int16_t)y;
    nv->surf_t[i][nv->nsurf[i]] = (uint8_t)t;
    nv->nsurf[i]++;
  }
  fclose(f);
  return true;
}

static void BuildPieces(Nivel *nv) {
  const int cols = nv->cols;
  nv->cut_y = malloc(sizeof(int16_t) * (size_t)cols);
  nv->flat_y = malloc(sizeof(int16_t) * (size_t)cols);
  nv->edge[0] = malloc(sizeof(int16_t) * (size_t)cols);
  nv->edge[1] = malloc(sizeof(int16_t) * (size_t)cols);
  for (int c = 0; c < cols; c++) {
    nv->cut_y[c] = -1;
    nv->flat_y[c] = -1;
    nv->edge[0][c] = (int16_t)RailY(nv, c * kColPx, NULL);
    nv->edge[1][c] = (int16_t)RailY(nv, c * kColPx + kColPx - 4, NULL);
    /* cut before c */
    if (c >= 1) {
      int y0 = -2;
      bool ok = true;
      for (int x = c * kColPx - 16; x < c * kColPx + 16; x += 4) {
        int t;
        const int y = RailY(nv, x, &t);
        if (y < 0 || !IsFlat(t) || Surfaces(nv, x) != 1) {
          ok = false;
          break;
        }
        if (y0 == -2)
          y0 = y;
        else if (y0 != y)
          ok = false;
      }
      if (ok && y0 >= 0)
        nv->cut_y[c] = (int16_t)y0;
    }
    /* flat column */
    {
      int y0 = -2;
      bool ok = true;
      for (int x = c * kColPx; x < c * kColPx + kColPx; x += 4) {
        int t;
        const int y = RailY(nv, x, &t);
        if (y < 0 || !IsFlat(t) || Surfaces(nv, x) != 1) {
          ok = false;
          break;
        }
        if (y0 == -2)
          y0 = y;
        else if (y0 != y)
          ok = false;
      }
      if (ok && y0 >= 0)
        nv->flat_y[c] = (int16_t)y0;
    }
  }
  /* segments between flat cuts (min 6 columns) */
  int bounds[100];
  int nb = 0;
  bounds[nb++] = 0;
  for (int c = kFirstCutMin; c < cols && nb < 98; c++)
    if (nv->cut_y[c] >= 0 && c - bounds[nb - 1] >= 6)
      bounds[nb++] = c;
  bounds[nb++] = cols;
  nv->ntramos = 0;
  for (int i = 0; i + 1 < nb && nv->ntramos < 80; i++) {
    NivTramo *t = &nv->tramos[nv->ntramos++];
    t->a = bounds[i];
    t->b = bounds[i + 1];
    t->y0 = t->a > 0 ? nv->cut_y[t->a] : -1;
    t->y1 = t->b < cols ? nv->cut_y[t->b] : -1;
  }
  /* original pit blocks with the same flat height on both sides */
  nv->nbloques = 0;
  const int xmax = nv->camera_max + 256;
  int x = 0x200;
  while (x < xmax) {
    if (Surfaces(nv, x)) {
      x += 4;
      continue;
    }
    const int s = x;
    while (x < xmax && !Surfaces(nv, x))
      x += 4;
    const int hc0 = s / kColPx, hc1 = (x - 4) / kColPx;
    int a = hc0 - 1;
    while (a > 0 && nv->flat_y[a] < 0 && hc0 - a <= 3)
      a--;
    int b = hc1 + 1;
    while (b < cols && nv->flat_y[b] < 0 && b - hc1 <= 3)
      b++;
    if (a >= 0 && b < cols && nv->flat_y[a] >= 0 &&
        nv->flat_y[a] == nv->flat_y[b] && b - a - 1 > 0 &&
        b - a - 1 <= kBlockMaxCols && nv->nbloques < 16) {
      NivBloque *bl = &nv->bloques[nv->nbloques++];
      bl->y = nv->flat_y[a];
      bl->x = s;
      bl->w = x - s;
      bl->ncols = 0;
      for (int c = a + 1; c < b; c++)
        bl->cols[bl->ncols++] = (int16_t)c;
    }
  }
}

static Nivel s_niveles[2];
static bool s_nivel_ok[2];
static uint8_t *s_rom;
static size_t s_rom_size;

const Nivel *RecNivel(const char *nombre) {
  int k = -1;
  for (int i = 0; i < 2; i++)
    if (nombre && strcmp(kNiveles[i].nombre, nombre) == 0)
      k = i;
  if (k < 0)
    return NULL;
  if (s_nivel_ok[k])
    return &s_niveles[k];
  if (!s_rom) {
    const uint8_t *rom = Cart2pStagePristineRom(&s_rom_size);
    if (!rom)
      return NULL;
    s_rom = (uint8_t *)rom;
  }
  Nivel *nv = &s_niveles[k];
  memset(nv, 0, sizeof *nv);
  Copy(nv->nombre, sizeof nv->nombre, kNiveles[k].nombre);
  Copy(nv->titulo, sizeof nv->titulo, kNiveles[k].titulo);
  char path[1200];
  PathIn(path, sizeof path, "stages\\", kNiveles[k].perfil, "");
  if (!LoadSurvey(nv, path)) {
    Cart2pLog("recetas: no se pudo leer el perfil %s", path);
    return NULL;
  }
  const uint8_t *rom = s_rom;
  const uint32_t map1 = RomOffset(nv->map_bank, nv->map_addr);
  if (map1 + kLayer2Offset + (uint32_t)nv->cols * kColPx > s_rom_size)
    return NULL;
  nv->layer1 = rom + map1;
  nv->layer2 = rom + map1 + kLayer2Offset;
  const int visible = (nv->camera_max + 1) / kColPx + 8;
  int max_index = 0;
  for (int L = 0; L < 2; L++) {
    const uint8_t *layer = L ? nv->layer2 : nv->layer1;
    const int n = (visible < nv->cols ? visible : nv->cols) * kColPx;
    for (int i = 0; i < n; i += 2) {
      const int v = U16(layer + i) & 0x07FF;
      if (v > max_index)
        max_index = v;
    }
  }
  nv->metatile_bytes = (max_index + 1) * 32;
  const int by_mem =
      (int)((kWramFreeEnd - (uint32_t)nv->metatile_bytes - kWramLayer2EndBase) /
            kColPx);
  nv->max_cols = by_mem < kMaxColsMem ? by_mem : kMaxColsMem;
  BuildPieces(nv);
  /* objects */
  {
    const uint16_t ptr = U16(rom + RomOffset(0xBD, 0x8000) + nv->entrance * 2);
    uint32_t o = RomOffset(0xBD, ptr);
    nv->nobjetos = 0;
    while (o + 8 <= s_rom_size && U16(rom + o) != 0 && nv->nobjetos < 96) {
      NivObjeto *ob = &nv->objetos[nv->nobjetos++];
      memcpy(ob->raw, rom + o, 8);
      ob->tipo = U16(rom + o);
      ob->x = U16(rom + o + 2);
      ob->y = U16(rom + o + 4);
      ob->ptr = U16(rom + o + 6);
      ob->sprite = SpriteId(rom, s_rom_size, ob->ptr);
      snprintf(ob->nombre, sizeof ob->nombre, "OBJETO %02X",
               ob->sprite < 0 ? 0 : ob->sprite);
      for (size_t i = 0; i < sizeof kSprites / sizeof kSprites[0]; i++)
        if (kSprites[i].sprite == ob->sprite) {
          Copy(ob->nombre, sizeof ob->nombre, kSprites[i].nombre);
          ob->zona_antes = kSprites[i].antes;
          ob->zona_despues = kSprites[i].despues;
          ob->enemigo = kSprites[i].despues > 0;
        }
      if (ob->tipo == 2) {
        Copy(ob->nombre, sizeof ob->nombre, "SALIDA");
        nv->exit_x = ob->x;
        ob->enemigo = false;
        ob->zona_antes = ob->zona_despues = 0;
      }
      o += 8;
    }
  }
  /* bananas */
  {
    const int idx = rom[RomOffset(0xB8, 0xB41D) + nv->entrance];
    const uint32_t base = RomOffset(0xB8, 0xC0E1);
    uint32_t o = base + U16(rom + base + idx * 2);
    nv->nplatanos = 0;
    while (o + 4 <= s_rom_size && U16(rom + o) != 0 && nv->nplatanos < 40) {
      NivPlatano *g = &nv->platanos[nv->nplatanos++];
      memcpy(g->raw, rom + o, 4);
      g->x = (U16(rom + o + 2) & 0xFFE0) >> 2;
      g->y = (U16(rom + o + 1) & 0x1FFC) << 1;
      o += 4;
    }
  }
  /* camera zones */
  {
    const uint16_t ptr = U16(rom + RomOffset(0xBC, 0x8000) + nv->entrance * 2);
    uint32_t o = RomOffset(0xBC, ptr);
    nv->nzonas = 0;
    while (o + 8 <= s_rom_size && U16(rom + o) != 0xFFFF && nv->nzonas < 96) {
      for (int k2 = 0; k2 < 4; k2++)
        nv->zonas[nv->nzonas][k2] = U16(rom + o + k2 * 2);
      nv->nzonas++;
      o += 8;
    }
  }
  s_nivel_ok[k] = true;
  return nv;
}

/* ---- layout --------------------------------------------------------------- */

int RecPiezaCols(const Pieza *p, int16_t *out, int max) {
  int n = 0;
  if (p->tipo == kPiezaTramo) {
    for (int c = p->desde; c < p->hasta && n < max; c++)
      out[n++] = (int16_t)c;
  } else if (p->tipo == kPiezaRecta) {
    for (int k = 0; k < p->largo && n < max; k++)
      out[n++] = (int16_t)p->columna;
  } else {
    for (int k = 0; k < p->ncols && n < max; k++)
      out[n++] = p->cols[k];
  }
  return n;
}

static int PiezaLen(const Pieza *p) {
  if (p->tipo == kPiezaTramo)
    return p->hasta > p->desde ? p->hasta - p->desde : 0;
  if (p->tipo == kPiezaRecta)
    return p->largo > 0 ? p->largo : 0;
  return p->ncols;
}

int RecTotalCols(const Receta *r) {
  int n = 0;
  for (int i = 0; i < r->npiezas; i++)
    n += PiezaLen(&r->piezas[i]);
  return n;
}

static int Inicio(const Receta *r, int pieza) {
  int n = 0;
  for (int i = 0; i < pieza && i < r->npiezas; i++)
    n += PiezaLen(&r->piezas[i]);
  return n;
}

/* ---- objects -------------------------------------------------------------- */

static void StableSortObj(RecObj *o, int n) {
  for (int i = 1; i < n; i++) {
    RecObj v = o[i];
    int j = i - 1;
    while (j >= 0 && o[j].x > v.x) {
      o[j + 1] = o[j];
      j--;
    }
    o[j + 1] = v;
  }
}

int RecObjetos(const Receta *r, const Nivel *nv, RecObj *out, int max,
               int *salida_x) {
  bool visto[96] = {0};
  int n = 0;
  if (salida_x)
    *salida_x = -1;
  for (int i = 0; i < r->npiezas; i++) {
    const Pieza *p = &r->piezas[i];
    if (p->tipo != kPiezaTramo)
      continue;
    const int ini = Inicio(r, i);
    for (int k = 0; k < nv->nobjetos; k++) {
      const NivObjeto *o = &nv->objetos[k];
      if (o->x < p->desde * kColPx || o->x >= p->hasta * kColPx || visto[k])
        continue;
      visto[k] = true;
      const int x = o->x - p->desde * kColPx + ini * kColPx;
      if (o->tipo == 2) {
        if (salida_x)
          *salida_x = x;
        continue;
      }
      if (n >= max)
        continue;
      RecObj *e = &out[n++];
      memset(e, 0, sizeof *e);
      e->x = x;
      e->y = o->y;
      e->pieza = i;
      e->indice = k;
      e->agregado = -1;
      e->enemigo = o->enemigo;
      e->nombre = o->nombre;
      e->ptr = o->ptr;
      memcpy(e->raw, o->raw, 8);
      for (int q = 0; q < r->noq; q++)
        if (r->oq[q].pieza == i && r->oq[q].indice == k)
          e->quitado = true;
    }
  }
  for (int a = 0; a < r->noa && n < max; a++) {
    const RecAgregar *ag = &r->oa[a];
    if (ag->pieza < 0 || ag->pieza >= r->npiezas)
      continue;
    RecObj *e = &out[n++];
    memset(e, 0, sizeof *e);
    e->x = Inicio(r, ag->pieza) * kColPx + ag->dx;
    e->y = ag->y;
    e->pieza = ag->pieza;
    e->indice = -1;
    e->agregado = a;
    e->enemigo = ag->enemigo;
    e->nombre = ag->nombre;
    e->ptr = ag->puntero;
    const uint16_t w[4] = {1, 0, (uint16_t)ag->y, ag->puntero};
    for (int k = 0; k < 4; k++) {
      e->raw[k * 2] = (uint8_t)w[k];
      e->raw[k * 2 + 1] = (uint8_t)(w[k] >> 8);
    }
  }
  StableSortObj(out, n);
  return n;
}

typedef struct {
  int x, y;
  uint8_t raw[4];
} PlatOut;

static int Platanos(const Receta *r, const Nivel *nv, PlatOut *out) {
  bool visto[40] = {0};
  int n = 0;
  for (int i = 0; i < r->npiezas; i++) {
    const Pieza *p = &r->piezas[i];
    if (p->tipo != kPiezaTramo)
      continue;
    const int ini = Inicio(r, i);
    for (int k = 0; k < nv->nplatanos; k++) {
      const NivPlatano *g = &nv->platanos[k];
      if (g->x < p->desde * kColPx || g->x >= p->hasta * kColPx || visto[k])
        continue;
      visto[k] = true;
      const int x = g->x - p->desde * kColPx + ini * kColPx;
      bool quitado = false;
      for (int q = 0; q < r->npq; q++)
        if (r->pq[q].pieza == i && r->pq[q].indice == k)
          quitado = true;
      if (quitado || x > kBananaXMax || n >= 40)
        continue;
      out[n].x = x;
      out[n].y = g->y;
      memcpy(out[n].raw, g->raw, 4);
      n++;
    }
  }
  for (int i = 1; i < n; i++) { /* stable by x */
    PlatOut v = out[i];
    int j = i - 1;
    while (j >= 0 && out[j].x > v.x) {
      out[j + 1] = out[j];
      j--;
    }
    out[j + 1] = v;
  }
  return n < nv->nplatanos ? n : nv->nplatanos;
}

/* ---- validation ----------------------------------------------------------- */

static const char *const kTemas[] = {"jungla", "cueva",   "fabrica", "nieve",
                                     "arboles", "bosque", "templo",  "mina"};

int RecTemas(void) { return (int)(sizeof kTemas / sizeof kTemas[0]); }

const char *RecTemaNombre(int id) {
  return id >= 1 && id <= RecTemas() ? kTemas[id - 1] : NULL;
}

/* Song of the level each theme comes from (same order as kTemas; same table
 * as etapa_lib.TEMA_MUSICA): a theme brings its music. */
static const char *const kTemaMusica[] = {"isla",    "cueva",  "fabrica", "nieve",
                                          "arboles", "bosque", "templo",  "niebla"};

const char *RecTemaMusica(int id) {
  return id >= 1 && id <= RecTemas() ? kTemaMusica[id - 1] : "";
}

int RecTemaId(const char *nombre) {
  for (int i = 0; nombre && nombre[0] && i < RecTemas(); i++)
    if (strcmp(nombre, kTemas[i]) == 0)
      return i + 1;
  return 0;
}

/* Level songs (Define_DKC1_MusicID_*); jingles left out. Same list as
 * etapa_lib.MUSICAS. */
static const struct {
  const char *nombre;
  uint8_t cancion;
} kMusicas[] = {
    {"isla", 0x00},   {"cueva", 0x01},  {"niebla", 0x02}, {"agua", 0x03},
    {"carro", 0x04},  {"nieve", 0x05},  {"templo", 0x06}, {"fabrica", 0x07},
    {"minas", 0x08},  {"hielo", 0x0B},  {"bosque", 0x0D}, {"bonus", 0x10},
    {"arboles", 0x13}, {"jefe", 0x15},  {"galeon", 0x18},
};

int RecMusicas(void) { return (int)(sizeof kMusicas / sizeof kMusicas[0]); }

const char *RecMusicaNombre(int id) {
  for (int i = 0; i < RecMusicas(); i++)
    if (kMusicas[i].cancion + 1 == id)
      return kMusicas[i].nombre;
  return NULL;
}

int RecMusicaId(const char *nombre) {
  for (int i = 0; nombre && nombre[0] && i < RecMusicas(); i++)
    if (strcmp(nombre, kMusicas[i].nombre) == 0)
      return kMusicas[i].cancion + 1;
  return 0;
}

int RecValidar(const Receta *r, const Nivel *nv, char errors[][160], int max) {
  int n = 0;
#define ERR(...)                                                   \
  do {                                                             \
    if (n < max)                                                   \
      snprintf(errors[n], 160, __VA_ARGS__);                       \
    n++;                                                           \
  } while (0)
  if (strcmp(r->nivel, nv->nombre) != 0)
    ERR("LA RECETA ES DE OTRO NIVEL");
  if (r->npiezas <= 0) {
    ERR("LA ETAPA NO TIENE PIEZAS");
    return n;
  }
  const Pieza *f = &r->piezas[0], *u = &r->piezas[r->npiezas - 1];
  if (f->tipo != kPiezaTramo || f->desde != 0 || f->hasta < kFirstCutMin)
    ERR("LA PRIMERA PIEZA DEBE SER EL TRAMO DE PARTIDA");
  if (u->tipo != kPiezaTramo || u->hasta != nv->cols ||
      u->desde * kColPx > nv->exit_x)
    ERR("LA ULTIMA PIEZA DEBE SER EL TRAMO DE LA META");
  if (r->tema[0] && !RecTemaId(r->tema))
    ERR("TEMA DESCONOCIDO: %s", r->tema);
  else if (r->tema[0] && strcmp(nv->nombre, "carnage") != 0 &&
           strcmp(nv->nombre, "madness") != 0)
    ERR("LOS TEMAS SOLO EXISTEN PARA CARNAGE Y MADNESS");
  if (r->musica[0] && !RecMusicaId(r->musica))
    ERR("MUSICA DESCONOCIDA: %s", r->musica);
  const int total = RecTotalCols(r);
  if (total > nv->max_cols)
    ERR("ETAPA DE %d COLUMNAS: MAXIMO %d", total, nv->max_cols);
  if (total < kFirstCutMin + 20)
    ERR("ETAPA DEMASIADO CORTA (%d COLUMNAS)", total);
  int16_t cols[kRecMaxCols];
  for (int i = 0; i < r->npiezas; i++) {
    const int k = RecPiezaCols(&r->piezas[i], cols, kRecMaxCols);
    if (!k)
      ERR("LA PIEZA %d ESTA VACIA", i + 1);
    for (int j = 0; j < k; j++)
      if (cols[j] < 0 || cols[j] >= nv->cols) {
        ERR("LA PIEZA %d USA COLUMNAS FUERA DEL NIVEL", i + 1);
        break;
      }
  }
  for (int i = 0; i + 1 < r->npiezas; i++) {
    const int ka = RecPiezaCols(&r->piezas[i], cols, kRecMaxCols);
    if (!ka)
      continue;
    const int ca = cols[ka - 1];
    const int kb = RecPiezaCols(&r->piezas[i + 1], cols, kRecMaxCols);
    if (!kb)
      continue;
    const int cb = cols[0];
    if (ca < 0 || ca >= nv->cols || cb < 0 || cb >= nv->cols)
      continue;
    const int ya = nv->edge[1][ca], yb = nv->edge[0][cb];
    if (ya < 0 || yb < 0 || abs(ya - yb) > 2)
      ERR("UNION %d-%d: LA VIA NO CALZA", i + 1, i + 2);
  }
  return n;
#undef ERR
}

/* ---- seams ---------------------------------------------------------------- */

static bool ColIgual(const Nivel *nv, int a, int b) {
  return a >= 0 && b >= 0 && a < nv->cols && b < nv->cols &&
         !memcmp(nv->layer1 + (size_t)a * kColPx, nv->layer1 + (size_t)b * kColPx, kColPx);
}

bool RecCorteVisible(const Receta *r, const Nivel *nv, int i) {
  if (i < 0 || i + 1 >= r->npiezas)
    return false;
  int16_t a[kRecMaxCols], b[kRecMaxCols];
  const int na = RecPiezaCols(&r->piezas[i], a, kRecMaxCols);
  const int nb = RecPiezaCols(&r->piezas[i + 1], b, kRecMaxCols);
  if (!na || !nb)
    return false;
  const int nat = a[na - 1] + 1;
  if (nat == b[0])
    return false;
  return !ColIgual(nv, nat, b[0]);
}

/* Columns where a seamless jump may start or land: rail on both sides of
 * the cut at the same height, away from start and finish. */
static bool PuntoSalto(const Nivel *nv, int c) {
  const int last = (nv->camera_max + 1) / kColPx;
  if (c < kFirstCutMin + 8 || c >= last - 8)
    return false;
  const int y0 = nv->edge[1][c - 1], y1 = nv->edge[0][c];
  return y0 >= 0 && y1 >= 0 && abs(y0 - y1) <= 2;
}

int RecGemelas(const Nivel *nv, int c, int *out, int max) {
  int n = 0;
  if (!PuntoSalto(nv, c))
    return 0;
  for (int t = 0; t < nv->cols && n < max; t++)
    if (abs(t - c) > 8 && PuntoSalto(nv, t) && ColIgual(nv, c, t))
      out[n++] = t;
  return n;
}

/* ---- compile -------------------------------------------------------------- */

static void Put16(uint8_t *p, int v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

/* Background (layer 2) column for stage column n: Mine Cart Madness scrolls
 * its background at half speed (parallax), so the original continuous
 * background is used (column n); Carnage moves it with the rail, so it goes
 * with its piece (c). The last map columns hold foreign data: repeat 64
 * columns earlier. Same rule as etapa_lib.fondo_columna. */
static int FondoColumna(const Nivel *nv, int n, int c) {
  if (strcmp(nv->nombre, "madness") != 0)
    return c;
  int seguro = (nv->camera_max + 1) / kColPx + 8;
  if (seguro > nv->cols)
    seguro = nv->cols;
  seguro -= 8;
  while (n >= seguro)
    n -= 64;
  return n;
}

uint8_t *RecCompilar(const Receta *r, const Nivel *nv, size_t *size) {
  int16_t src[kRecMaxCols];
  int total = 0;
  for (int i = 0; i < r->npiezas; i++)
    total += RecPiezaCols(&r->piezas[i], src + total, kRecMaxCols - total);
  RecObj objs[200];
  int salida_x = -1;
  const int nobj = RecObjetos(r, nv, objs, 200, &salida_x);
  int activos = 0;
  RecObj act[200];
  for (int i = 0; i < nobj; i++)
    if (!objs[i].quitado)
      act[activos++] = objs[i];
  const int con_salida = salida_x >= 0;
  int limite = nv->nobjetos - con_salida;
  if (activos > limite)
    activos = limite;
  PlatOut plat[40];
  const int nplat = Platanos(r, nv, plat);
  /* camera zones */
  uint16_t zones[256][4];
  int nz = 0;
  for (int n0 = 0; n0 < total;) {
    int n1 = n0 + 1;
    while (n1 < total && src[n1] == src[n1 - 1] + 1)
      n1++;
    const int c0 = src[n0], c1 = src[n1 - 1] + 1;
    const int ox0 = c0 * kColPx, ox1 = c1 * kColPx, nx0 = n0 * kColPx;
    int zi = 0;
    for (int z = 0; z < nv->nzonas; z++)
      if (nv->zonas[z][0] <= ox0)
        zi = z;
      else
        break;
    uint16_t cand[97][4];
    int nc = 0;
    cand[nc][0] = (uint16_t)nx0;
    cand[nc][1] = nv->zonas[zi][1];
    cand[nc][2] = nv->zonas[zi][2];
    cand[nc][3] = nv->zonas[zi][3];
    nc++;
    for (int z = 0; z < nv->nzonas && nc < 97; z++)
      if (ox0 < nv->zonas[z][0] && nv->zonas[z][0] < ox1) {
        cand[nc][0] = (uint16_t)(nx0 + nv->zonas[z][0] - ox0);
        cand[nc][1] = nv->zonas[z][1];
        cand[nc][2] = nv->zonas[z][2];
        cand[nc][3] = nv->zonas[z][3];
        nc++;
      }
    for (int k = 0; k < nc; k++) {
      if (nz && zones[nz - 1][1] == cand[k][1] && zones[nz - 1][2] == cand[k][2] &&
          zones[nz - 1][3] == cand[k][3])
        continue;
      if (nz && zones[nz - 1][0] == cand[k][0]) {
        memcpy(zones[nz - 1], cand[k], sizeof cand[k]);
      } else if (nz < 256) {
        memcpy(zones[nz], cand[k], sizeof cand[k]);
        nz++;
      }
    }
    n0 = n1;
  }
  const size_t camera_bytes = (size_t)nz * 8 + 2;
  if (camera_bytes > kCameraMaxBytes)
    return NULL;
  const size_t object_bytes = (size_t)(nv->nobjetos + 1) * 8;
  const size_t banana_bytes = (size_t)nv->nplatanos * 4;
  const size_t layer = (size_t)total * kColPx;
  const size_t need = 28 + 2 * layer + object_bytes + banana_bytes + camera_bytes;
  uint8_t *out = calloc(1, need);
  if (!out)
    return NULL;
  memcpy(out, "C2PSTG3", 8);
  Put16(out + 8, nv->entrance);
  Put16(out + 10, total);
  Put16(out + 12, nv->camera_max - (nv->cols - total) * kColPx);
  Put16(out + 14, activos + con_salida);
  Put16(out + 16, nplat);
  Put16(out + 18, nv->metatile_bytes);
  Put16(out + 20, (int)object_bytes);
  Put16(out + 22, (int)banana_bytes);
  Put16(out + 24, (int)camera_bytes);
  Put16(out + 26, RecTemaId(r->tema) | RecMusicaId(r->musica) << 8);
  uint8_t *p = out + 28;
  for (int i = 0; i < total; i++)
    memcpy(p + (size_t)i * kColPx, nv->layer1 + (size_t)src[i] * kColPx, kColPx);
  p += layer;
  for (int i = 0; i < total; i++)
    memcpy(p + (size_t)i * kColPx, nv->layer2 + (size_t)FondoColumna(nv, i, src[i]) * kColPx,
           kColPx);
  p += layer;
  for (int i = 0; i < activos; i++) {
    memcpy(p + (size_t)i * 8, act[i].raw, 8);
    Put16(p + (size_t)i * 8 + 2, act[i].x);
  }
  if (con_salida) {
    for (int k = 0; k < nv->nobjetos; k++)
      if (nv->objetos[k].tipo == 2) {
        memcpy(p + (size_t)activos * 8, nv->objetos[k].raw, 8);
        Put16(p + (size_t)activos * 8 + 2, salida_x);
      }
  }
  p += object_bytes;
  for (int i = 0; i < nplat; i++) {
    memcpy(p + (size_t)i * 4, plat[i].raw, 4);
    const int w2 = (U16(plat[i].raw + 2) & 0x1F) | ((plat[i].x << 2) & 0xFFE0);
    Put16(p + (size_t)i * 4 + 2, w2);
  }
  p += banana_bytes;
  for (int z = 0; z < nz; z++)
    for (int k = 0; k < 4; k++)
      Put16(p + z * 8 + k * 2, zones[z][k]);
  Put16(p + nz * 8, 0xFFFF);
  *size = need;
  return out;
}

/* ---- documentation -------------------------------------------------------- */

static void Hx(char *out, size_t size, int v) {
  if (v < 0)
    snprintf(out, size, "-");
  else
    snprintf(out, size, "$%04X", v & 0xFFFF);
}

static void Compactar(const int16_t *cols, int n, char *out, size_t size) {
  size_t o = 0;
  out[0] = 0;
  for (int i = 0; i < n;) {
    int j = i;
    while (j + 1 < n && cols[j + 1] == cols[i])
      j++;
    char part[32];
    if (j > i) {
      snprintf(part, sizeof part, "%d x%d", cols[i], j - i + 1);
      /* python: '%dx%d' */
      snprintf(part, sizeof part, "%dx%d", cols[i], j - i + 1);
      i = j + 1;
    } else {
      j = i;
      while (j + 1 < n && cols[j + 1] == cols[j] + 1)
        j++;
      if (j > i)
        snprintf(part, sizeof part, "%d-%d", cols[i], cols[j]);
      else
        snprintf(part, sizeof part, "%d", cols[i]);
      i = j + 1;
    }
    o += (size_t)snprintf(out + o, o < size ? size - o : 0, "%s%s",
                          o ? ", " : "", part);
  }
}

void RecDescribir(const Receta *r, const Nivel *nv, int pieza, char *out,
                  size_t size) {
  const Pieza *p = &r->piezas[pieza];
  int16_t cols[kRecMaxCols];
  const int n = RecPiezaCols(p, cols, kRecMaxCols);
  const int x0 = Inicio(r, pieza) * kColPx;
  char hx0[12], hin[12], hout[12], alt[64];
  Hx(hx0, sizeof hx0, x0);
  Hx(hin, sizeof hin, n ? nv->edge[0][cols[0]] : -1);
  Hx(hout, sizeof hout, n ? nv->edge[1][cols[n - 1]] : -1);
  snprintf(alt, sizeof alt, "via entra a %s y sale a %s", hin, hout);
  if (p->tipo == kPiezaTramo) {
    /* object counts by name, sorted by name */
    char nombres[24][20];
    int cuentas[24];
    int nn = 0;
    for (int k = 0; k < nv->nobjetos; k++) {
      const NivObjeto *o = &nv->objetos[k];
      if (o->x < p->desde * kColPx || o->x >= p->hasta * kColPx)
        continue;
      int j = 0;
      while (j < nn && strcmp(nombres[j], o->nombre) != 0)
        j++;
      if (j == nn && nn < 24) {
        Copy(nombres[nn], 20, o->nombre);
        cuentas[nn++] = 0;
      }
      if (j < 24)
        cuentas[j]++;
    }
    for (int i = 1; i < nn; i++)
      for (int j = i; j > 0 && strcmp(nombres[j - 1], nombres[j]) > 0; j--) {
        char t[20];
        Copy(t, 20, nombres[j]);
        Copy(nombres[j], 20, nombres[j - 1]);
        Copy(nombres[j - 1], 20, t);
        const int c = cuentas[j];
        cuentas[j] = cuentas[j - 1];
        cuentas[j - 1] = c;
      }
    char lista[400] = "";
    size_t o = 0;
    for (int i = 0; i < nn; i++)
      o += (size_t)snprintf(lista + o, o < sizeof lista ? sizeof lista - o : 0,
                            "%s%d %s", i ? ", " : "", cuentas[i], nombres[i]);
    if (!nn)
      Copy(lista, sizeof lista, "sin objetos");
    int nb = 0;
    for (int k = 0; k < nv->nplatanos; k++)
      if (nv->platanos[k].x >= p->desde * kColPx &&
          nv->platanos[k].x < p->hasta * kColPx)
        nb++;
    char ha[12], hb[12];
    Hx(ha, sizeof ha, p->desde * kColPx);
    Hx(hb, sizeof hb, p->hasta * kColPx - 1);
    snprintf(out, size,
             "Tramo original de %s, columnas %d-%d (X original %s-%s), %d "
             "columnas, en la etapa desde X %s. %s; %d grupo(s) de platanos; %s.",
             nv->titulo, p->desde, p->hasta - 1, ha, hb, p->hasta - p->desde,
             hx0, lista, nb, alt);
    return;
  }
  if (p->tipo == kPiezaRecta) {
    char hy[12];
    Hx(hy, sizeof hy,
       p->columna >= 0 && p->columna < nv->cols ? nv->flat_y[p->columna] : -1);
    snprintf(out, size,
             "Recta inventada: columna %d del original repetida %d veces (%d px) "
             "a la altura %s, en la etapa desde X %s. Sin objetos.",
             p->columna, p->largo, p->largo * kColPx, hy[0] == '-' ? "$0000" : hy,
             hx0);
    return;
  }
  char comp[400];
  Compactar(cols, n, comp, sizeof comp);
  const char *nombre = p->tipo == kPiezaHoyo    ? "Hoyo inventado"
                       : p->tipo == kPiezaDoble ? "Doble hoyo inventado"
                                                : "Pieza de columnas";
  snprintf(out, size,
           "%s: %d columnas copiadas del original %s, en la etapa desde X %s; "
           "los hoyos son bordes de hoyo originales (salto real del juego); %s. "
           "Sin objetos.",
           nombre, n, comp, hx0, alt);
}

void RecEstadisticas(const Receta *r, const Nivel *nv, RecStats *st) {
  memset(st, 0, sizeof *st);
  st->columnas = RecTotalCols(r);
  st->piezas = r->npiezas;
  for (int i = 0; i < r->npiezas; i++) {
    const PiezaTipo t = r->piezas[i].tipo;
    if (t == kPiezaTramo)
      st->tramos++;
    else
      st->inventadas++;
    if (t == kPiezaHoyo)
      st->hoyos += 1;
    if (t == kPiezaDoble)
      st->hoyos += 2;
  }
  RecObj objs[200];
  const int n = RecObjetos(r, nv, objs, 200, NULL);
  int activos = 0;
  for (int i = 0; i < n; i++) {
    if (objs[i].quitado)
      continue;
    if (activos++ >= nv->nobjetos - 1)
      break;
    if (objs[i].enemigo)
      st->enemigos++;
    int j = 0;
    while (j < st->nobj && strcmp(st->obj_nombre[j], objs[i].nombre) != 0)
      j++;
    if (j == st->nobj && st->nobj < 24) {
      Copy(st->obj_nombre[st->nobj], 20, objs[i].nombre);
      st->obj_cuenta[st->nobj++] = 0;
    }
    if (j < 24)
      st->obj_cuenta[j]++;
  }
  for (int i = 1; i < st->nobj; i++) /* sorted by name, like the Python dict */
    for (int j = i; j > 0 && strcmp(st->obj_nombre[j - 1], st->obj_nombre[j]) > 0;
         j--) {
      char t[20];
      Copy(t, 20, st->obj_nombre[j]);
      Copy(st->obj_nombre[j], 20, st->obj_nombre[j - 1]);
      Copy(st->obj_nombre[j - 1], 20, t);
      const int c = st->obj_cuenta[j];
      st->obj_cuenta[j] = st->obj_cuenta[j - 1];
      st->obj_cuenta[j - 1] = c;
    }
  PlatOut plat[40];
  st->platanos = Platanos(r, nv, plat);
  for (int i = 0; i + 1 < r->npiezas; i++)
    st->cortes += RecCorteVisible(r, nv, i);
  const int px = st->columnas * kColPx - 0x3A6;
  st->segundos = (double)(px > 0 ? px : 0) / kSpeed / 60.0;
  st->vs_original = (double)st->columnas / (double)nv->cols;
}

/* ---- JSON ----------------------------------------------------------------- */

typedef struct J {
  int t; /* 0 null, 1 bool, 2 number, 3 string, 4 array, 5 object */
  double n;
  char *s;
  int count;
  struct J **v;
  char **k;
  const char *raw0, *raw1; /* text span of this value */
} J;

static void JFree(J *j) {
  if (!j)
    return;
  for (int i = 0; i < j->count; i++) {
    JFree(j->v[i]);
    if (j->k)
      free(j->k[i]);
  }
  free(j->v);
  free(j->k);
  free(j->s);
  free(j);
}

static const char *JWs(const char *p) {
  while (*p && isspace((unsigned char)*p))
    p++;
  return p;
}

static J *JParse(const char **pp);

static char *JString(const char **pp) {
  const char *p = *pp + 1;
  size_t cap = 64, n = 0;
  char *out = malloc(cap);
  while (*p && *p != '"') {
    char c = *p++;
    if (c == '\\') {
      c = *p++;
      if (c == 'n') c = '\n';
      else if (c == 't') c = '\t';
      else if (c == 'r') c = '\r';
      else if (c == 'u') {
        unsigned v = 0;
        for (int i = 0; i < 4 && *p; i++, p++)
          v = v * 16 + (unsigned)(isdigit((unsigned char)*p) ? *p - '0'
                                                             : (tolower(*p) - 'a' + 10));
        c = v < 128 ? (char)v : '?';
      }
    }
    if (n + 2 > cap)
      out = realloc(out, cap *= 2);
    out[n++] = c;
  }
  out[n] = 0;
  *pp = *p ? p + 1 : p;
  return out;
}

static J *JParse(const char **pp) {
  const char *p = JWs(*pp);
  J *j = calloc(1, sizeof *j);
  j->raw0 = p;
  if (*p == '{' || *p == '[') {
    const char close = *p == '{' ? '}' : ']';
    j->t = *p == '{' ? 5 : 4;
    p = JWs(p + 1);
    int cap = 8;
    j->v = malloc(sizeof(J *) * (size_t)cap);
    if (j->t == 5)
      j->k = malloc(sizeof(char *) * (size_t)cap);
    while (*p && *p != close) {
      if (j->count == cap) {
        cap *= 2;
        j->v = realloc(j->v, sizeof(J *) * (size_t)cap);
        if (j->k)
          j->k = realloc(j->k, sizeof(char *) * (size_t)cap);
      }
      if (j->t == 5) {
        p = JWs(p);
        if (*p != '"')
          break;
        j->k[j->count] = JString(&p);
        p = JWs(p);
        if (*p == ':')
          p++;
      }
      j->v[j->count++] = JParse(&p);
      p = JWs(p);
      if (*p == ',')
        p = JWs(p + 1);
    }
    if (*p == close)
      p++;
  } else if (*p == '"') {
    j->t = 3;
    j->s = JString(&p);
  } else if (!strncmp(p, "true", 4) || !strncmp(p, "false", 5)) {
    j->t = 1;
    j->n = *p == 't';
    p += *p == 't' ? 4 : 5;
  } else if (!strncmp(p, "null", 4)) {
    p += 4;
  } else {
    char *end;
    j->t = 2;
    j->n = strtod(p, &end);
    p = end > p ? end : p + 1;
  }
  j->raw1 = p;
  *pp = p;
  return j;
}

static J *JGet(const J *o, const char *key) {
  if (!o || o->t != 5)
    return NULL;
  for (int i = 0; i < o->count; i++)
    if (strcmp(o->k[i], key) == 0)
      return o->v[i];
  return NULL;
}

static int JInt(const J *o, const char *key, int def) {
  const J *v = JGet(o, key);
  return v && v->t == 2 ? (int)v->n : def;
}

static void JStr(const J *o, const char *key, char *out, size_t size) {
  const J *v = JGet(o, key);
  Copy(out, size, v && v->t == 3 ? v->s : "");
}

static uint16_t ParsePtr(const J *v) {
  if (!v)
    return 0;
  if (v->t == 2)
    return (uint16_t)v->n;
  if (v->t == 3)
    return (uint16_t)strtol(v->s[0] == '$' ? v->s + 1 : v->s, NULL, 16);
  return 0;
}

static void FromJson(const J *o, Receta *r) {
  memset(r, 0, sizeof *r);
  JStr(o, "id", r->id, sizeof r->id);
  JStr(o, "nombre", r->nombre, sizeof r->nombre);
  JStr(o, "nivel", r->nivel, sizeof r->nivel);
  JStr(o, "familia", r->familia, sizeof r->familia);
  JStr(o, "tema", r->tema, sizeof r->tema);
  JStr(o, "musica", r->musica, sizeof r->musica);
  JStr(o, "dificultad", r->dificultad, sizeof r->dificultad);
  JStr(o, "autor", r->autor, sizeof r->autor);
  JStr(o, "creada", r->creada, sizeof r->creada);
  JStr(o, "modificada", r->modificada, sizeof r->modificada);
  JStr(o, "descripcion", r->descripcion, sizeof r->descripcion);
  JStr(o, "notas", r->notas, sizeof r->notas);
  const J *sem = JGet(o, "semilla");
  if (sem && sem->t == 2) {
    r->semilla = (int)sem->n;
    r->tiene_semilla = true;
  }
  const J *gen = JGet(o, "generacion");
  if (gen && gen->t == 5) {
    size_t len = (size_t)(gen->raw1 - gen->raw0);
    char buf[200];
    size_t o2 = 0;
    for (size_t i = 0; i < len && o2 + 1 < sizeof buf; i++) {
      const char c = gen->raw0[i];
      if (c == '\n' || c == '\r')
        continue;
      if (c == ' ' && o2 && buf[o2 - 1] == ' ')
        continue;
      buf[o2++] = c;
    }
    buf[o2] = 0;
    Copy(r->generacion, sizeof r->generacion, buf);
  }
  const J *ps = JGet(o, "piezas");
  for (int i = 0; ps && ps->t == 4 && i < ps->count && r->npiezas < kRecMaxPieces;
       i++) {
    const J *pj = ps->v[i];
    Pieza *p = &r->piezas[r->npiezas++];
    memset(p, 0, sizeof *p);
    char tipo[16];
    JStr(pj, "tipo", tipo, sizeof tipo);
    p->tipo = !strcmp(tipo, "tramo")   ? kPiezaTramo
              : !strcmp(tipo, "recta") ? kPiezaRecta
              : !strcmp(tipo, "hoyo")  ? kPiezaHoyo
              : !strcmp(tipo, "doble") ? kPiezaDoble
                                       : kPiezaColumnas;
    p->desde = JInt(pj, "desde", 0);
    p->hasta = JInt(pj, "hasta", 0);
    p->columna = JInt(pj, "columna", 0);
    p->largo = JInt(pj, "largo", 0);
    const J *cs = JGet(pj, "columnas");
    for (int k = 0; cs && cs->t == 4 && k < cs->count && p->ncols < kRecMaxFreeCols;
         k++)
      p->cols[p->ncols++] = (int16_t)cs->v[k]->n;
    const J *hp = JGet(pj, "hoyos_px");
    for (int k = 0; hp && hp->t == 4 && k < hp->count && k < 2; k++)
      p->hoyos_px[p->nhoyos++] = (int)hp->v[k]->n;
  }
  const J *q = JGet(o, "objetos_quitar");
  for (int i = 0; q && q->t == 4 && i < q->count && r->noq < kRecMaxRemoved; i++) {
    r->oq[r->noq].pieza = JInt(q->v[i], "pieza", 0);
    r->oq[r->noq].indice = JInt(q->v[i], "indice", 0);
    r->noq++;
  }
  const J *a = JGet(o, "objetos_agregar");
  for (int i = 0; a && a->t == 4 && i < a->count && r->noa < kRecMaxAdded; i++) {
    RecAgregar *g = &r->oa[r->noa++];
    g->pieza = JInt(a->v[i], "pieza", 0);
    g->dx = JInt(a->v[i], "dx", 0);
    g->y = JInt(a->v[i], "y", 0);
    JStr(a->v[i], "nombre", g->nombre, sizeof g->nombre);
    g->puntero = ParsePtr(JGet(a->v[i], "puntero"));
    const J *en = JGet(a->v[i], "enemigo");
    g->enemigo = en && en->t == 1 && en->n != 0;
  }
  const J *pq = JGet(o, "platanos_quitar");
  for (int i = 0; pq && pq->t == 4 && i < pq->count && r->npq < kRecMaxRemoved;
       i++) {
    r->pq[r->npq].pieza = JInt(pq->v[i], "pieza", 0);
    r->pq[r->npq].indice = JInt(pq->v[i], "indice", 0);
    r->npq++;
  }
  const J *h = JGet(o, "historial");
  for (int i = 0; h && h->t == 4 && i < h->count && r->nhist < kRecMaxHistory; i++)
    if (h->v[i]->t == 3)
      Copy(r->historial[r->nhist++], 160, h->v[i]->s);
}

static char *ReadAll(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = n > 0 && n < (8 << 20) ? malloc((size_t)n + 1) : NULL;
  if (buf) {
    const size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
  }
  fclose(f);
  return buf;
}

bool RecCargar(const char *id, Receta *r) {
  char path[1200];
  PathIn(path, sizeof path, "etapas_fuente\\", id, ".json");
  char *text = ReadAll(path);
  if (!text)
    return false;
  const char *p = text;
  J *j = JParse(&p);
  const bool ok = j && j->t == 5;
  if (ok)
    FromJson(j, r);
  JFree(j);
  free(text);
  return ok && r->npiezas > 0;
}

bool RecExiste(const char *id) {
  char path[1200];
  PathIn(path, sizeof path, "etapas_fuente\\", id, ".json");
  FILE *f = fopen(path, "rb");
  if (f)
    fclose(f);
  return f != NULL;
}

static void JEsc(FILE *f, const char *s) {
  fputc('"', f);
  for (; *s; s++) {
    if (*s == '"' || *s == '\\')
      fputc('\\', f), fputc(*s, f);
    else if (*s == '\n')
      fputs("\\n", f);
    else if ((unsigned char)*s < 32)
      fputc(' ', f);
    else
      fputc(*s, f);
  }
  fputc('"', f);
}

static bool WriteJson(const Receta *r, const Nivel *nv, const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return false;
  RecStats st;
  RecEstadisticas(r, nv, &st);
  fputs("{\n  \"formato\": 1,\n  \"id\": ", f);
  JEsc(f, r->id);
  fputs(",\n  \"nombre\": ", f);
  JEsc(f, r->nombre);
  fputs(",\n  \"nivel\": ", f);
  JEsc(f, r->nivel);
#define FIELD(key, val)                \
  if ((val)[0]) {                      \
    fputs(",\n  \"" key "\": ", f);    \
    JEsc(f, val);                      \
  }
  FIELD("tema", r->tema);
  FIELD("musica", r->musica);
  FIELD("familia", r->familia);
  FIELD("descripcion", r->descripcion);
  FIELD("autor", r->autor);
  FIELD("creada", r->creada);
  FIELD("modificada", r->modificada);
  if (r->tiene_semilla)
    fprintf(f, ",\n  \"semilla\": %d", r->semilla);
  FIELD("notas", r->notas);
#undef FIELD
  fprintf(f,
          ",\n  \"estadisticas\": {\n    \"columnas\": %d,\n    \"largo_px\": %d,"
          "\n    \"segundos_aprox\": %.1f,\n    \"piezas\": %d,\n"
          "    \"tramos_originales\": %d,\n    \"piezas_inventadas\": %d,\n"
          "    \"hoyos_inventados\": %d,\n    \"enemigos\": %d,\n    \"objetos\": {",
          st.columnas, st.columnas * kColPx, st.segundos, st.piezas, st.tramos,
          st.inventadas, st.hoyos, st.enemigos);
  for (int i = 0; i < st.nobj; i++) {
    fprintf(f, "%s\n      ", i ? "," : "");
    JEsc(f, st.obj_nombre[i]);
    fprintf(f, ": %d", st.obj_cuenta[i]);
  }
  fprintf(f, "%s},\n    \"grupos_platanos\": %d,\n    \"largo_vs_original\": %.2f,"
          "\n    \"cortes_visibles\": %d\n  }",
          st.nobj ? "\n    " : "", st.platanos, st.vs_original, st.cortes);
  fputs(",\n  \"piezas\": [", f);
  for (int i = 0; i < r->npiezas; i++) {
    const Pieza *p = &r->piezas[i];
    fprintf(f, "%s\n    {\n      \"tipo\": ", i ? "," : "");
    static const char *const kTipos[] = {"tramo", "recta", "hoyo", "doble",
                                         "columnas"};
    JEsc(f, kTipos[p->tipo]);
    if (p->tipo == kPiezaTramo)
      fprintf(f, ",\n      \"desde\": %d,\n      \"hasta\": %d", p->desde, p->hasta);
    else if (p->tipo == kPiezaRecta)
      fprintf(f, ",\n      \"columna\": %d,\n      \"largo\": %d", p->columna,
              p->largo);
    else {
      if (p->nhoyos) {
        fputs(",\n      \"hoyos_px\": [", f);
        for (int k = 0; k < p->nhoyos; k++)
          fprintf(f, "%s%d", k ? ", " : "", p->hoyos_px[k]);
        fputs("]", f);
      }
      fputs(",\n      \"columnas\": [", f);
      for (int k = 0; k < p->ncols; k++)
        fprintf(f, "%s%d", k ? ", " : "", p->cols[k]);
      fputs("]", f);
    }
    char nota[800];
    RecDescribir(r, nv, i, nota, sizeof nota);
    fputs(",\n      \"nota\": ", f);
    JEsc(f, nota);
    fputs("\n    }", f);
  }
  fputs("\n  ],\n  \"objetos_quitar\": [", f);
  for (int i = 0; i < r->noq; i++)
    fprintf(f, "%s\n    {\"pieza\": %d, \"indice\": %d}", i ? "," : "",
            r->oq[i].pieza, r->oq[i].indice);
  fputs(r->noq ? "\n  ],\n  \"objetos_agregar\": [" : "],\n  \"objetos_agregar\": [", f);
  for (int i = 0; i < r->noa; i++) {
    const RecAgregar *a = &r->oa[i];
    fprintf(f, "%s\n    {\"pieza\": %d, \"dx\": %d, \"y\": %d, \"nombre\": ",
            i ? "," : "", a->pieza, a->dx, a->y);
    JEsc(f, a->nombre);
    fprintf(f, ", \"puntero\": \"$%04X\", \"enemigo\": %s}", a->puntero,
            a->enemigo ? "true" : "false");
  }
  fputs(r->noa ? "\n  ],\n  \"platanos_quitar\": [" : "],\n  \"platanos_quitar\": [", f);
  for (int i = 0; i < r->npq; i++)
    fprintf(f, "%s\n    {\"pieza\": %d, \"indice\": %d}", i ? "," : "",
            r->pq[i].pieza, r->pq[i].indice);
  fputs(r->npq ? "\n  ]" : "]", f);
  if (r->generacion[0])
    fprintf(f, ",\n  \"generacion\": %s", r->generacion);
  if (r->dificultad[0]) {
    fputs(",\n  \"dificultad\": ", f);
    JEsc(f, r->dificultad);
  }
  fputs(",\n  \"historial\": [", f);
  for (int i = 0; i < r->nhist; i++) {
    fprintf(f, "%s\n    ", i ? "," : "");
    JEsc(f, r->historial[i]);
  }
  fputs(r->nhist ? "\n  ]\n}\n" : "]\n}\n", f);
  fclose(f);
  return true;
}

/* ---- menu list ------------------------------------------------------------ */

static void NombreValido(const char *in, char *out, size_t size) {
  size_t o = 0;
  for (; *in && o + 1 < size && o < 28; in++) {
    char c = (char)toupper((unsigned char)*in);
    if (!isalnum((unsigned char)c) && !strchr(" :.!-/(),", c))
      c = ' ';
    if (c == ' ' && (o == 0 || out[o - 1] == ' '))
      continue;
    out[o++] = c;
  }
  while (o && out[o - 1] == ' ')
    o--;
  out[o] = 0;
}

static bool AsegurarEnLista(const Receta *r) {
  const char *lista = getenv("DKC1_CART2P_LISTA");
  if (!lista)
    return false;
  char *text = ReadAll(lista);
  if (!text)
    return false;
  char nombre[40], linea[300], clave[80];
  NombreValido(r->nombre, nombre, sizeof nombre);
  snprintf(linea, sizeof linea, "%s | %s | stages\\%s.c2p", nombre,
           RecNivelEstado(r->nivel), r->id);
  snprintf(clave, sizeof clave, "stages\\%s.c2p", r->id);
  FILE *f = fopen(lista, "wb");
  if (!f) {
    free(text);
    return false;
  }
  bool found = false;
  for (char *line = text; *line;) {
    char *end = strchr(line, '\n');
    const size_t len = end ? (size_t)(end - line) : strlen(line);
    char buf[600];
    snprintf(buf, sizeof buf, "%.*s", (int)(len < 599 ? len : 599), line);
    size_t bl = strlen(buf);
    while (bl && (buf[bl - 1] == '\r'))
      buf[--bl] = 0;
    if (buf[0] != '#' && strstr(buf, clave)) {
      fprintf(f, "%s\n", linea);
      found = true;
    } else {
      fprintf(f, "%s\n", buf);
    }
    line = end ? end + 1 : line + len;
  }
  if (!found)
    fprintf(f, "%s\n", linea);
  fclose(f);
  free(text);
  return true;
}

static void Ahora(char *out, size_t size, const char *fmt) {
  const time_t t = time(NULL);
  const struct tm *tm = localtime(&t);
  if (tm)
    strftime(out, size, fmt, tm);
  else
    Copy(out, size, "");
}

bool RecGuardar(Receta *r, const char *cambio) {
  const Nivel *nv = RecNivel(r->nivel);
  if (!nv || !RecDirCart2p()[0])
    return false;
  char errs[4][160];
  if (RecValidar(r, nv, errs, 4) > 0)
    return false;
  char path[1200], dir[1200];
  PathIn(dir, sizeof dir, "etapas_fuente", "", "");
  MKDIR(dir);
  PathIn(path, sizeof path, "etapas_fuente\\", r->id, ".json");
  FILE *old = fopen(path, "rb");
  if (old) { /* the previous version goes to historial\ */
    fclose(old);
    char hdir[1200], stamp[32], hpath[1300];
    PathIn(hdir, sizeof hdir, "etapas_fuente\\historial", "", "");
    MKDIR(hdir);
    Ahora(stamp, sizeof stamp, "%Y%m%d_%H%M%S");
    snprintf(hpath, sizeof hpath, "%s\\%s_%s.json", hdir, r->id, stamp);
    char *prev = ReadAll(path);
    FILE *h = prev ? fopen(hpath, "wb") : NULL;
    if (h) {
      fputs(prev, h);
      fclose(h);
    }
    free(prev);
  }
  char now[24];
  Ahora(now, sizeof now, "%Y-%m-%d %H:%M");
  if (!r->creada[0])
    snprintf(r->creada, sizeof r->creada, "%.10s", now);
  Copy(r->modificada, sizeof r->modificada, now);
  if (cambio && *cambio && r->nhist < kRecMaxHistory)
    snprintf(r->historial[r->nhist++], 160, "%s %s", now, cambio);
  if (!WriteJson(r, nv, path))
    return false;
  size_t size = 0;
  uint8_t *data = RecCompilar(r, nv, &size);
  if (!data)
    return false;
  char c2p[1200];
  PathIn(c2p, sizeof c2p, "stages\\", r->id, ".c2p");
  FILE *f = fopen(c2p, "wb");
  const bool ok = f && fwrite(data, 1, size, f) == size;
  if (f)
    fclose(f);
  free(data);
  if (ok)
    AsegurarEnLista(r);
  Cart2pLog("recetas: guardada %s (%d piezas, %d columnas)", r->id, r->npiezas,
            RecTotalCols(r));
  return ok;
}

void RecDesdeNivel(Receta *r, const Nivel *nv, const char *id,
                   const char *nombre) {
  memset(r, 0, sizeof *r);
  Copy(r->id, sizeof r->id, id);
  Copy(r->nombre, sizeof r->nombre, nombre);
  Copy(r->nivel, sizeof r->nivel, nv->nombre);
  Copy(r->familia, sizeof r->familia, "PROPIA");
  Copy(r->autor, sizeof r->autor, "editor del juego");
  for (int i = 0; i < nv->ntramos && r->npiezas < kRecMaxPieces; i++) {
    Pieza *p = &r->piezas[r->npiezas++];
    memset(p, 0, sizeof *p);
    p->tipo = kPiezaTramo;
    p->desde = nv->tramos[i].a;
    p->hasta = nv->tramos[i].b;
  }
}

static int CmpIds(const void *a, const void *b) {
  return strcmp((const char *)a, (const char *)b);
}

int RecListar(char ids[][48], int max) {
  int n = 0;
  char pattern[1200];
  PathIn(pattern, sizeof pattern, "etapas_fuente\\", "*", ".json");
#ifdef _WIN32
  struct _finddata_t fd;
  intptr_t h = _findfirst(pattern, &fd);
  if (h == -1)
    return 0;
  do {
    const size_t len = strlen(fd.name);
    if (len > 5 && n < max && len - 5 < 48) {
      snprintf(ids[n], 48, "%.*s", (int)(len - 5), fd.name);
      n++;
    }
  } while (_findnext(h, &fd) == 0);
  _findclose(h);
#else
  char dir[1200];
  PathIn(dir, sizeof dir, "etapas_fuente", "", "");
  DIR *d = opendir(dir);
  struct dirent *e;
  while (d && (e = readdir(d)) && n < max) {
    const size_t len = strlen(e->d_name);
    if (len > 5 && !strcmp(e->d_name + len - 5, ".json") && len - 5 < 48) {
      snprintf(ids[n], 48, "%.*s", (int)(len - 5), e->d_name);
      n++;
    }
  }
  if (d)
    closedir(d);
#endif
  qsort(ids, (size_t)n, 48, CmpIds);
  return n;
}

/* ---- randomizer (port of gen_stage.generar; own RNG) ---------------------- */

typedef struct {
  uint32_t s;
} Rng;

static uint32_t RngNext(Rng *g) {
  uint32_t x = g->s ? g->s : 0x9E3779B9u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return g->s = x;
}

static int RngInt(Rng *g, int a, int b) { /* a..b inclusive */
  return a + (int)(RngNext(g) % (uint32_t)(b - a + 1));
}

static int BuildOrder(const Nivel *nv, int target, Rng *g, int *order,
                      int *total_out) {
  const int nseg = nv->ntramos, last = nseg - 1;
  int best_score = -1, best_n = 0, best_total = 0;
  static int best[200], cur[200];
  for (int tries = 0; tries < 20000; tries++) {
    int n = 0;
    bool used[80] = {0};
    cur[n++] = 0;
    used[0] = true;
    int y = nv->tramos[0].y1;
    int total = nv->tramos[0].b - nv->tramos[0].a;
    for (;;) {
      const int remaining =
          target - total - (nv->tramos[last].b - nv->tramos[last].a);
      if (y == nv->tramos[last].y0 && remaining <= 12) {
        cur[n++] = last;
        total += nv->tramos[last].b - nv->tramos[last].a;
        break;
      }
      int cands[80], nc = 0;
      for (int i = 1; i < last; i++)
        if (nv->tramos[i].y0 == y && !used[i])
          cands[nc++] = i;
      if (!nc)
        for (int i = 1; i < last; i++)
          if (nv->tramos[i].y0 == y)
            cands[nc++] = i;
      int k2 = 0;
      for (int i = 0; i < nc; i++)
        if (nv->tramos[cands[i]].b - nv->tramos[cands[i]].a <= remaining + 12)
          cands[k2++] = cands[i];
      if (!k2 || n >= 190)
        break;
      const int i = cands[RngInt(g, 0, k2 - 1)];
      cur[n++] = i;
      used[i] = true;
      y = nv->tramos[i].y1;
      total += nv->tramos[i].b - nv->tramos[i].a;
    }
    if (cur[n - 1] != last)
      continue;
    int distinct = 0;
    bool seen[80] = {0};
    for (int i = 0; i < n; i++)
      if (!seen[cur[i]]) {
        seen[cur[i]] = true;
        distinct++;
      }
    const int score = abs(total - target) + 40 * (n - distinct);
    if (best_score < 0 || score < best_score) {
      best_score = score;
      best_n = n;
      best_total = total;
      memcpy(best, cur, sizeof(int) * (size_t)n);
      if (!score)
        break;
    }
  }
  if (best_score < 0)
    return 0;
  memcpy(order, best, sizeof(int) * (size_t)best_n);
  *total_out = best_total;
  return best_n;
}

static int Nearest(const Nivel *nv, int y, int near) {
  int best = -1;
  for (int c = 0; c < nv->cols; c++)
    if (nv->flat_y[c] == y && (best < 0 || abs(c - near) < abs(best - near)))
      best = c;
  return best;
}

static bool Invent(const Nivel *nv, Rng *g, int y, int near, bool pits_ok,
                   Pieza *p) {
  const int f = Nearest(nv, y, near);
  if (f < 0)
    return false;
  int blocks[16], nb = 0;
  for (int i = 0; i < nv->nbloques; i++)
    if (nv->bloques[i].y == y)
      blocks[nb++] = i;
  memset(p, 0, sizeof *p);
  int kind = 0; /* 0 recta, 1 hoyo, 2 doble */
  if (pits_ok && nb) {
    static const int kKinds[4] = {0, 1, 1, 2};
    kind = kKinds[RngInt(g, 0, 3)];
  }
  if (!kind) {
    p->tipo = kPiezaRecta;
    p->columna = f;
    p->largo = RngInt(g, 3, 8);
    return true;
  }
  p->tipo = kind == 1 ? kPiezaHoyo : kPiezaDoble;
  int n = RngInt(g, 2, 4);
  for (int k = 0; k < n; k++)
    p->cols[p->ncols++] = (int16_t)f;
  for (int h = 0; h < kind; h++) {
    if (h) {
      n = 4 + RngInt(g, 0, 2);
      for (int k = 0; k < n; k++)
        p->cols[p->ncols++] = (int16_t)f;
    }
    const NivBloque *b = &nv->bloques[blocks[RngInt(g, 0, nb - 1)]];
    for (int k = 0; k < b->ncols && p->ncols < kRecMaxFreeCols; k++)
      p->cols[p->ncols++] = b->cols[k];
    p->hoyos_px[p->nhoyos++] = b->w;
  }
  n = RngInt(g, 2, 4);
  for (int k = 0; k < n && p->ncols < kRecMaxFreeCols; k++)
    p->cols[p->ncols++] = (int16_t)f;
  return true;
}

bool RecGenerar(const Nivel *nv, uint32_t seed, int cols, int inventar,
                bool hoyos, Receta *r) {
  Rng g = {seed * 2654435761u + 1};
  const int target = cols ? cols : nv->cols;
  int base_target = (target < nv->max_cols ? target : nv->max_cols) - 10 * inventar;
  if (base_target < 40)
    base_target = 40;
  int order[200], n = 0, base_total = 0;
  while (!n && base_target <= nv->max_cols) {
    n = BuildOrder(nv, base_target, &g, order, &base_total);
    base_target += 10;
  }
  if (!n)
    return false;
  /* joints, enemies of the remix without inventions */
  int joint_x[200];
  int pos = 0;
  int place_a[200], place_new[200];
  for (int k = 0; k < n; k++) {
    place_a[k] = nv->tramos[order[k]].a;
    place_new[k] = pos;
    pos += nv->tramos[order[k]].b - nv->tramos[order[k]].a;
    joint_x[k] = pos * kColPx;
  }
  int ex[96], eb[96], ea[96], ne = 0;
  for (int i = 0; i < nv->nobjetos; i++) {
    const NivObjeto *o = &nv->objetos[i];
    if (o->tipo != 1 || !o->zona_despues)
      continue;
    for (int k = 0; k < n; k++) {
      const NivTramo *t = &nv->tramos[order[k]];
      if (o->x >= t->a * kColPx && o->x < t->b * kColPx) {
        ex[ne] = o->x - place_a[k] * kColPx + place_new[k] * kColPx;
        eb[ne] = o->zona_antes;
        ea[ne] = o->zona_despues;
        ne++;
        break;
      }
    }
  }
  int joints[200], nj = 0;
  bool pits[200];
  for (int k = 0; k + 1 < n; k++) {
    const int y = nv->tramos[order[k]].y1;
    if (y < 0 || Nearest(nv, y, 0) < 0)
      continue;
    bool ok = hoyos;
    bool has_block = false;
    for (int b = 0; b < nv->nbloques; b++)
      if (nv->bloques[b].y == y)
        has_block = true;
    ok = ok && has_block;
    for (int e = 0; e < ne && ok; e++)
      if (ex[e] - eb[e] <= joint_x[k] && joint_x[k] <= ex[e] + ea[e])
        ok = false;
    pits[k] = ok;
    joints[nj++] = k;
  }
  for (int i = nj - 1; i > 0; i--) { /* shuffle */
    const int j = RngInt(&g, 0, i);
    const int t = joints[i];
    joints[i] = joints[j];
    joints[j] = t;
  }
  int sorted[200], ns = 0; /* pit-capable joints first, stable */
  for (int i = 0; i < nj; i++)
    if (pits[joints[i]])
      sorted[ns++] = joints[i];
  for (int i = 0; i < nj; i++)
    if (!pits[joints[i]])
      sorted[ns++] = joints[i];
  static Pieza inserts[200];
  bool has_insert[200] = {0};
  int ninserts = 0, room = nv->max_cols - base_total;
  for (int i = 0; i < ns && ninserts < inventar; i++) {
    const int k = sorted[i];
    const NivTramo *t = &nv->tramos[order[k]];
    Pieza p;
    if (!Invent(nv, &g, t->y1, t->b - 1, pits[k], &p))
      continue;
    const int len = PiezaLen(&p);
    if (len > room)
      continue;
    inserts[k] = p;
    has_insert[k] = true;
    room -= len;
    ninserts++;
  }
  memset(r, 0, sizeof *r);
  Copy(r->nivel, sizeof r->nivel, nv->nombre);
  for (int k = 0; k < n && r->npiezas < kRecMaxPieces - 1; k++) {
    Pieza *p = &r->piezas[r->npiezas++];
    memset(p, 0, sizeof *p);
    p->tipo = kPiezaTramo;
    p->desde = nv->tramos[order[k]].a;
    p->hasta = nv->tramos[order[k]].b;
    if (has_insert[k])
      r->piezas[r->npiezas++] = inserts[k];
  }
  char errs[2][160];
  return RecValidar(r, nv, errs, 2) == 0;
}

bool RecGenerarSinCostura(const Nivel *nv, uint32_t seed, int cols, int saltos_min,
                          Receta *r) {
  Rng g = {seed * 2654435761u + 7};
  static int puntos[600], gem[600][16], ngem[600];
  int np = 0;
  for (int c = 0; c < nv->cols && c < 600; c++) {
    ngem[c] = RecGemelas(nv, c, gem[c], 16);
    if (ngem[c])
      puntos[np++] = c;
  }
  int target = cols ? cols : nv->cols;
  if (target > nv->max_cols)
    target = nv->max_cols;
  const int tol = 16, min_run = 12, max_run = 110;
  int best_score = -1, best_n = 0;
  static int best_a[48], best_b[48], ra[48], rb[48];
  static bool vista[600];
  for (int tries = 0; tries < 6000; tries++) {
    int n = 0, pos = 0, total = 0;
    bool ok = false;
    for (int paso = 0; paso < 40; paso++) {
      const int resto = nv->cols - pos, diff = total + resto - target;
      if (n >= saltos_min && abs(diff) <= tol) {
        ra[n] = pos;
        rb[n++] = nv->cols;
        total += resto;
        ok = true;
        break;
      }
      const int lo = pos + (pos == 0 ? kFirstCutMin + min_run : min_run);
      /* too long: only forward jumps; too short: only backward (any if
       * none in that direction is within reach) */
      int sentido = diff > 60 ? 1 : diff < -60 ? -1 : 0;
      int cands[600], nc = 0;
      for (int pasada = 0; pasada < 2 && !nc; pasada++) {
        if (pasada)
          sentido = 0;
        for (int i = 0; i < np; i++) {
          const int c = puntos[i];
          if (c < lo || c > pos + max_run)
            continue;
          bool hay = !sentido;
          for (int k = 0; k < ngem[c]; k++)
            hay |= (gem[c][k] - c) * sentido > 0;
          if (hay)
            cands[nc++] = c;
        }
      }
      if (!nc || n >= 46)
        break;
      const int c = cands[RngInt(&g, 0, nc - 1)];
      int ts[16], nt = 0;
      for (int k = 0; k < ngem[c]; k++) {
        const int t = gem[c][k];
        if (!sentido || (t - c) * sentido > 0)
          ts[nt++] = t;
      }
      const int t = ts[RngInt(&g, 0, nt - 1)];
      ra[n] = pos;
      rb[n++] = c;
      total += c - pos;
      pos = t;
    }
    if (!ok || total > nv->max_cols)
      continue;
    int repetidas = 0;
    memset(vista, 0, sizeof vista);
    for (int k = 0; k < n; k++)
      for (int x = ra[k]; x < rb[k] && x < 600; x++) {
        repetidas += vista[x];
        vista[x] = true;
      }
    const int score = abs(total - target) + repetidas / 3;
    if (best_score < 0 || score < best_score) {
      best_score = score;
      best_n = n;
      memcpy(best_a, ra, sizeof(int) * (size_t)n);
      memcpy(best_b, rb, sizeof(int) * (size_t)n);
      if (!score)
        break;
    }
  }
  if (best_score < 0)
    return false;
  memset(r, 0, sizeof *r);
  Copy(r->nivel, sizeof r->nivel, nv->nombre);
  for (int k = 0; k < best_n; k++) {
    Pieza *p = &r->piezas[r->npiezas++];
    p->tipo = kPiezaTramo;
    p->desde = best_a[k];
    p->hasta = best_b[k];
  }
  char errs[2][160];
  return RecValidar(r, nv, errs, 2) == 0;
}

int RecAzar(int cantidad, uint32_t seed) {
  Rng g = {seed ? seed : (uint32_t)time(NULL)};
  int hechas = 0;
  for (int tries = 0; hechas < cantidad && tries < cantidad * 10; tries++) {
    const char *nivel = RngInt(&g, 0, 1) ? "madness" : "carnage";
    const Nivel *nv = RecNivel(nivel);
    if (!nv)
      return hechas;
    const int s = RngInt(&g, 1, 99999), inv = RngInt(&g, 6, 12);
    Receta *r = calloc(1, sizeof *r);
    if (!r)
      return hechas;
    char id[48];
    snprintf(id, sizeof id, "azar_%s_%d", nivel, s);
    if (RecExiste(id) || !RecGenerarSinCostura(nv, (uint32_t)s, 0, 5 + inv / 2, r)) {
      free(r);
      continue;
    }
    /* a theme (another world behind the track) or none */
    const int tema = RngInt(&g, 0, RecTemas());
    if (tema) {
      Copy(r->tema, sizeof r->tema, RecTemaNombre(tema));
      Copy(r->musica, sizeof r->musica, RecTemaMusica(tema));
    }
    Copy(r->id, sizeof r->id, id);
    snprintf(r->nombre, sizeof r->nombre, "AZAR %s %d",
             !strcmp(nivel, "madness") ? "MADNESS" : "CARNAGE", s);
    Copy(r->familia, sizeof r->familia, "AZAR");
    Copy(r->autor, sizeof r->autor, "juego (etapas al azar)");
    r->semilla = s;
    r->tiene_semilla = true;
    snprintf(r->generacion, sizeof r->generacion,
             "{\"semilla\": %d, \"columnas\": 0, \"saltos_min\": %d, "
             "\"sin_costura\": true, \"generador\": \"juego\"}", s, 5 + inv / 2);
    RecStats st;
    RecEstadisticas(r, nv, &st);
    snprintf(r->descripcion, sizeof r->descripcion,
             "Etapa al azar creada en el juego, sin costuras: un recorrido por %s "
             "que salta %d veces entre columnas identicas (el corte no se ve). "
             "%d columnas, unos %.1f s, %d enemigos.%s%s",
             nv->titulo, r->npiezas - 1, st.columnas, st.segundos, st.enemigos,
             r->tema[0] ? " Tema: " : "", r->tema);
    if (RecGuardar(r, "creada en el juego (etapas al azar)"))
      hechas++;
    free(r);
  }
  return hechas;
}
