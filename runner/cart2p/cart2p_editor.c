/* Cart Chase 2P - stage editor inside the game (mouse and keyboard).
 *
 * Drawn over the whole 512x224 frame while the race is in the editor phase
 * (cart2p_dual.c, kRaceEditor). Works on the same recipes as the HTML
 * editor (cart2p\etapas_fuente\*.json) through cart2p_receta.c: validates,
 * documents, saves, compiles and adds to the menu list; "probar" plays the
 * saved stage at once (1 JUGADOR) and comes back.
 *
 * Screen:
 *   top bar       stage name, level, length, status
 *   track         the stage with the level graphics (from the PPU VRAM of
 *                 the level's start state), rail, pieces, objects, bananas
 *   minimap       whole stage (click to go)
 *   piece strip   one card per piece (click to choose, drag to reorder)
 *   info + help   selected piece, messages, keys
 * Overlays: piece palette (TAB), objects (O), rename (N), help (F1),
 * unsaved changes (ESC). Keys and mouse: cart2p\docs\EDITOR_ETAPAS.txt. */
#include "cart2p.h"
#include "cart2p_receta.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "snes/ppu.h"
#include "snes/snes.h"

extern Snes *g_snes;

enum {
  W = 512,
  H = 224,
  kTop = 11,
  kTrackY = 11,
  kTrackH = 152,
  kMiniY = 163,
  kMiniH = 7,
  kStripY = 170,
  kStripH = 32,
  kInfoY = 203,
  kHelpY = 214,
  kCardW = 46,
  kCol = 32,
  kLevelH = 512,
  kUndo = 60,
  kPanelX = 292,
};

static const uint32_t kBg = 0x00141210u, kPanel = 0x001E1A17u,
                      kPanel2 = 0x0027221Eu, kBorder = 0x003A332Du,
                      kText = 0x00ECE4DAu, kDim = 0x00A69A8Cu,
                      kYellow = 0x00F2B632u, kRed = 0x00EF5350u,
                      kGreen = 0x0052C26Bu, kBlue = 0x005AA9FFu,
                      kTramoC = 0x004F8FD6u, kRectaC = 0x006CC28Au,
                      kHoyoC = 0x00E0794Au, kBrown = 0x00C08A52u,
                      kOrange = 0x00FFA94Du;

typedef enum { kModoLista, kModoEditar } Modo;
typedef enum {
  kOvNada,
  kOvPaleta,
  kOvObjetos,
  kOvNombre,
  kOvSalir,
  kOvAyuda
} Overlay;

static struct {
  bool abierto;
  Modo modo;
  Overlay ov;
  /* list */
  char ids[400][48];
  char nombres[400][32];
  char niveles[400][12];
  int ncols[400];
  int nlista;
  int lista_sel, lista_top;
  char filtro[32];
  /* edit */
  Receta *r;
  Receta *undo[kUndo];
  int nundo;
  Receta *redo[kUndo];
  int nredo;
  bool nueva;
  bool sucia;
  const Nivel *nv;
  int sel;
  double zoom;
  int x0;         /* stage px at the left of the track */
  int vy;         /* level px at the top of the track */
  bool vy_manual;
  /* palette */
  int pal_tab, pal_sel, pal_top, recta_largo, tramo_largo;
  bool solo_calzan;
  /* objects */
  int plantilla; /* -1 none, else index in plantillas */
  int nplant;
  char plant_nombre[32][20];
  uint16_t plant_ptr[32];
  int plant_y[32];
  bool plant_enemigo[32];
  int obj_sel_list;
  /* rename */
  char nombre_edit[32];
  /* mouse */
  int mx, my;
  bool arrastre_obj;
  int arrastre_n;
  Receta *arrastre_antes;
  bool arrastre_carta;
  int carta_desde;
  bool pan;
  int pan_x, pan_y, pan_x0, pan_vy;
  /* messages */
  char msg[96];
  int msg_frames;
  bool msg_mal;
  /* actions for the race driver */
  bool salir;
  char probar[48];
  /* graphics cache */
  char cache_nivel[12];
  uint32_t *col[kRecMaxCols];
  /* theme skin (cart2p\stages\tema_<name>.c2t) of the stage being edited */
  char piel_tema[16];
  uint8_t *piel;            /* file, NULL = none */
  int piel_k, piel_keep;
  const uint8_t *piel_meta, *piel_ventana, *piel_tiles, *piel_tiles2;
  uint32_t piel_pal[128];
  uint32_t *piel_via[kRecMaxCols];   /* layer 1 with the skin, 1 = clear */
  uint32_t *piel_fondo[64];          /* window column with the skin */
  int cache_cols;
  int frame;
} E;

/* ---- drawing primitives (whole 512 frame) ------------------------------- */

static uint8_t *s_buf;
static size_t s_pitch;

static inline void Px(int x, int y, uint32_t c) {
  if ((unsigned)x < W && (unsigned)y < H)
    ((uint32_t *)(s_buf + (size_t)y * s_pitch))[x] = c;
}

static void Rect(int x, int y, int w, int h, uint32_t c) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++)
      Px(i, j, c);
}

static void Frame(int x, int y, int w, int h, uint32_t c) {
  Rect(x, y, w, 1, c);
  Rect(x, y + h - 1, w, 1, c);
  Rect(x, y, 1, h, c);
  Rect(x + w - 1, y, 1, h, c);
}

static uint32_t Mix(uint32_t a, uint32_t b, int t /*0..256*/) {
  const uint32_t r = (((a >> 16) & 255) * (256 - t) + ((b >> 16) & 255) * t) >> 8;
  const uint32_t g = (((a >> 8) & 255) * (256 - t) + ((b >> 8) & 255) * t) >> 8;
  const uint32_t bl = ((a & 255) * (256 - t) + (b & 255) * t) >> 8;
  return r << 16 | g << 8 | bl;
}

static void Shade(int x, int y, int w, int h, uint32_t c, int t) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++)
      if ((unsigned)i < W && (unsigned)j < H) {
        uint32_t *p = &((uint32_t *)(s_buf + (size_t)j * s_pitch))[i];
        *p = Mix(*p, c, t);
      }
}

static int TextW(const char *s) {
  return (int)strlen(s) * 6 - 1;
}

/* 5x7 font of the race HUD; lowercase is drawn as uppercase. */
static void Text(int x, int y, uint32_t c, const char *s) {
  for (; *s; s++, x += 6) {
    const char ch = (char)toupper((unsigned char)*s);
    const uint8_t *rows = Cart2pGlyph(ch);
    if (!rows)
      continue;
    for (int ry = 0; ry < 7; ry++)
      for (int rx = 0; rx < 5; rx++)
        if (rows[ry] & (0x10 >> rx))
          Px(x + rx, y + ry, c);
  }
}

static void TextR(int xr, int y, uint32_t c, const char *s) {
  Text(xr - TextW(s), y, c, s);
}

static void Fmt(char *out, size_t n, const char *fmt, ...) {
  va_list a;
  va_start(a, fmt);
  vsnprintf(out, n, fmt, a);
  va_end(a);
}

static void Msg(bool mal, const char *fmt, ...) {
  va_list a;
  va_start(a, fmt);
  vsnprintf(E.msg, sizeof E.msg, fmt, a);
  va_end(a);
  E.msg_frames = 240;
  E.msg_mal = mal;
}

/* ---- level graphics: one 32x512 bitmap per original column ------------- */

static void LiberarCache(void) {
  for (int i = 0; i < kRecMaxCols; i++) {
    free(E.col[i]);
    E.col[i] = NULL;
  }
  E.cache_nivel[0] = 0;
}

static const uint32_t *Columna(int c) {
  const Nivel *nv = E.nv;
  if (!nv || c < 0 || c >= nv->cols || c >= kRecMaxCols)
    return NULL;
  if (strcmp(E.cache_nivel, nv->nombre) != 0) {
    LiberarCache();
    snprintf(E.cache_nivel, sizeof E.cache_nivel, "%s", nv->nombre);
  }
  if (E.col[c])
    return E.col[c];
  uint32_t *bmp = malloc(sizeof(uint32_t) * kCol * kLevelH);
  if (!bmp || !g_snes || !g_snes->ppu)
    return NULL;
  const Ppu *ppu = g_snes->ppu;
  uint32_t pal[256];
  for (int i = 0; i < 256; i++) {
    const uint16_t v = ppu->cgram[i];
    pal[i] = (uint32_t)(((v & 31) * 255 / 31) << 16 | (((v >> 5) & 31) * 255 / 31) << 8 |
                        (((v >> 10) & 31) * 255 / 31));
  }
  for (int i = 0; i < kCol * kLevelH; i++)
    bmp[i] = 0x00100C0Au;
  size_t rom_size;
  const uint8_t *rom = Cart2pStagePristineRom(&rom_size);
  const uint32_t meta =
      (nv->map_bank >= 0xC0 ? (uint32_t)((nv->map_bank - 0xC0) << 16)
                            : (uint32_t)((nv->map_bank - 0x80) << 16)) |
      (uint32_t)nv->metatile_addr;
  /* Madness draws its background layer with other graphics: layer 1 only */
  const int capas = strcmp(nv->nombre, "carnage") == 0 ? 2 : 1;
  for (int capa = capas - 1; capa >= 0; capa--) {
    const uint8_t *layer = capa ? nv->layer2 : nv->layer1;
    const int base = PPU_bgTileAdr(ppu, capa ? 1 : 0);
    for (int k = 0; k < 16; k++) {
      const uint16_t cell = (uint16_t)(layer[c * 32 + k * 2] | layer[c * 32 + k * 2 + 1] << 8);
      const uint32_t m = meta + (uint32_t)(cell & 0x7FF) * 32;
      if (!rom || m + 32 > rom_size)
        continue;
      const bool hf = cell & 0x4000, vf = cell & 0x8000;
      for (int r = 0; r < 4; r++)
        for (int t = 0; t < 4; t++) {
          const int sr = vf ? 3 - r : r, st = hf ? 3 - t : t;
          uint16_t tw = (uint16_t)(rom[m + sr * 8 + st * 2] | rom[m + sr * 8 + st * 2 + 1] << 8);
          tw ^= (hf ? 0x4000 : 0) ^ (vf ? 0x8000 : 0);
          const int num = tw & 0x3FF, pl = (tw >> 10) & 7;
          const bool thf = tw & 0x4000, tvf = tw & 0x8000;
          const int a = (base + num * 16) & 0x7FFF;
          for (int row = 0; row < 8; row++) {
            const uint16_t w01 = ppu->vram[(a + row) & 0x7FFF];
            const uint16_t w23 = ppu->vram[(a + 8 + row) & 0x7FFF];
            for (int colp = 0; colp < 8; colp++) {
              const int bit = 7 - colp;
              const int ci = ((w01 >> bit) & 1) | (((w01 >> (8 + bit)) & 1) << 1) |
                             (((w23 >> bit) & 1) << 2) | (((w23 >> (8 + bit)) & 1) << 3);
              if (!ci)
                continue;
              const int y = k * 32 + r * 8 + (tvf ? 7 - row : row);
              const int x = t * 8 + (thf ? 7 - colp : colp);
              uint32_t px = pal[pl * 16 + ci];
              if (capa)
                px = Mix(0, px, 100);
              bmp[y * kCol + x] = px;
            }
          }
        }
    }
  }
  E.col[c] = bmp;
  return bmp;
}

/* ---- theme skin -------------------------------------------------------------- */

enum { kClaro = 1 }; /* transparent pixel in the skin's track bitmaps */

static void LiberarPiel(void) {
  for (int i = 0; i < kRecMaxCols; i++) {
    free(E.piel_via[i]);
    E.piel_via[i] = NULL;
  }
  for (int i = 0; i < 64; i++) {
    free(E.piel_fondo[i]);
    E.piel_fondo[i] = NULL;
  }
  free(E.piel);
  E.piel = NULL;
}

/* Loads the skin of the stage's theme when it changes (none: NULL). Same
 * layout as Cart2pStageInstallSkin. */
static void CargarPiel(void) {
  const char *tema = E.r ? E.r->tema : "";
  if (strcmp(tema, E.piel_tema) == 0)
    return;
  LiberarPiel();
  snprintf(E.piel_tema, sizeof E.piel_tema, "%s", tema);
  if (!tema[0])
    return;
  char path[1200];
  snprintf(path, sizeof path, "%sstages/tema_%s%s.c2t", RecDirCart2p(), tema,
           strcmp(E.nv->nombre, "madness") ? "" : "_madness");
  FILE *f = fopen(path, "rb");
  if (!f)
    return;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *d = n > 56 && n < (1 << 20) ? malloc((size_t)n) : NULL;
  const bool ok = d && fread(d, 1, (size_t)n, f) == (size_t)n;
  fclose(f);
  if (!ok || memcmp(d, "C2PPIEL2", 8) != 0) {
    free(d);
    return;
  }
  /* same layout as SkinHeader (cart2p_stage.c) */
  const int keep = d[10] | d[11] << 8, k = d[12] | d[13] << 8;
  const int metas = d[14] | d[15] << 8, tiles = d[18] | d[19] << 8;
  const int tiles2 = d[22] | d[23] << 8;
  const size_t need =
      56 + (size_t)metas * 32 + (size_t)k * 32 + (size_t)(tiles + tiles2) * 32 + 256;
  if ((size_t)n < need || k <= 0 || k > 64) {
    free(d);
    return;
  }
  E.piel = d;
  E.piel_keep = keep;
  E.piel_k = k;
  E.piel_meta = d + 56;
  E.piel_ventana = E.piel_meta + (size_t)metas * 32;
  E.piel_tiles = E.piel_ventana + (size_t)k * 32;
  /* the theme's characters: its own set (Madness BG2) or shared */
  E.piel_tiles2 = tiles2 ? E.piel_tiles + (size_t)tiles * 32 : E.piel_tiles;
  const uint8_t *cg = E.piel_tiles + (size_t)(tiles + tiles2) * 32;
  for (int i = 0; i < 128; i++) {
    const uint16_t v = (uint16_t)(cg[i * 2] | cg[i * 2 + 1] << 8);
    E.piel_pal[i] = (uint32_t)(((v & 31) * 255 / 31) << 16 | (((v >> 5) & 31) * 255 / 31) << 8 |
                               (((v >> 10) & 31) * 255 / 31));
  }
}

/* Draws 16 cells (one column of a layer map) with the skin's metatiles. */
static void PielCeldas(uint32_t *bmp, const uint8_t *cells, bool solo_via) {
  for (int k = 0; k < 16; k++) {
    const uint16_t cell = (uint16_t)(cells[k * 2] | cells[k * 2 + 1] << 8);
    const int idx = cell & 0x7FF;
    if (solo_via && idx >= E.piel_keep)
      continue;
    const uint8_t *m = E.piel_meta + (size_t)idx * 32;
    if (m + 32 > E.piel_ventana)
      continue;
    const bool hf = cell & 0x4000, vf = cell & 0x8000;
    for (int r = 0; r < 4; r++)
      for (int q = 0; q < 4; q++) {
        const int sr = vf ? 3 - r : r, sq = hf ? 3 - q : q;
        uint16_t tw = (uint16_t)(m[sr * 8 + sq * 2] | m[sr * 8 + sq * 2 + 1] << 8);
        tw ^= (hf ? 0x4000 : 0) ^ (vf ? 0x8000 : 0);
        const uint8_t *td = (solo_via ? E.piel_tiles : E.piel_tiles2) + (size_t)(tw & 0x3FF) * 32;
        const int pl = (tw >> 10) & 7;
        for (int row = 0; row < 8; row++)
          for (int colp = 0; colp < 8; colp++) {
            const int bit = 7 - colp;
            const int ci = ((td[row * 2] >> bit) & 1) | (((td[row * 2 + 1] >> bit) & 1) << 1) |
                           (((td[16 + row * 2] >> bit) & 1) << 2) |
                           (((td[17 + row * 2] >> bit) & 1) << 3);
            if (!ci)
              continue;
            const int y = k * 32 + r * 8 + ((tw & 0x8000) ? 7 - row : row);
            const int x = q * 8 + ((tw & 0x4000) ? 7 - colp : colp);
            bmp[y * kCol + x] = E.piel_pal[pl * 16 + ci] | 0x01000000u;
          }
      }
  }
}

/* Track of original column c with the skin (kClaro where the theme shows). */
static const uint32_t *PielVia(int c) {
  if (!E.piel || c < 0 || c >= E.nv->cols || c >= kRecMaxCols)
    return NULL;
  if (!E.piel_via[c]) {
    uint32_t *bmp = malloc(sizeof(uint32_t) * kCol * kLevelH);
    if (!bmp)
      return NULL;
    for (int i = 0; i < kCol * kLevelH; i++)
      bmp[i] = kClaro;
    PielCeldas(bmp, E.nv->layer1 + (size_t)c * 32, true);
    E.piel_via[c] = bmp;
  }
  return E.piel_via[c];
}

/* Theme scenery behind stage column n (window column n % k). */
static const uint32_t *PielFondo(int n) {
  if (!E.piel)
    return NULL;
  const int j = n % E.piel_k;
  if (!E.piel_fondo[j]) {
    uint32_t *bmp = malloc(sizeof(uint32_t) * kCol * kLevelH);
    if (!bmp)
      return NULL;
    for (int i = 0; i < kCol * kLevelH; i++)
      bmp[i] = E.piel_pal[0];
    PielCeldas(bmp, E.piel_ventana + (size_t)j * 32, false);
    E.piel_fondo[j] = bmp;
  }
  return E.piel_fondo[j];
}

/* Pixel of the stage at stage column n (source column c), row ly, x 0-31. */
static uint32_t PixelEtapa(int n, int c, int ly, int x) {
  if (E.piel) {
    const uint32_t *via = PielVia(c);
    const uint32_t v = via ? via[ly * kCol + x] : kClaro;
    if (v != kClaro)
      return v & 0x00FFFFFFu;
    const uint32_t *fondo = PielFondo(n);
    return fondo ? fondo[ly * kCol + x] & 0x00FFFFFFu : 0;
  }
  const uint32_t *bmp = Columna(c);
  return bmp ? bmp[ly * kCol + x] : 0x00202020u;
}

/* ---- model helpers ---------------------------------------------------------- */

static int PiezaLen(const Pieza *p) {
  int16_t tmp[kRecMaxFreeCols > 600 ? kRecMaxFreeCols : 600];
  return RecPiezaCols(p, tmp, 600);
}

static int Inicio(int pieza) {
  int n = 0;
  for (int i = 0; i < pieza && i < E.r->npiezas; i++)
    n += PiezaLen(&E.r->piezas[i]);
  return n;
}

static int s_src[kRecMaxCols * 2];
static int s_total;
static int s_pieza_de[kRecMaxCols * 2];

static void Disponer(void) {
  s_total = 0;
  int16_t cols[600];
  for (int i = 0; i < E.r->npiezas; i++) {
    const int n = RecPiezaCols(&E.r->piezas[i], cols, 600);
    for (int k = 0; k < n && s_total < kRecMaxCols * 2; k++) {
      s_src[s_total] = cols[k];
      s_pieza_de[s_total] = i;
      s_total++;
    }
  }
}

static int EdgeIn(int i) {
  int16_t cols[600];
  const int n = RecPiezaCols(&E.r->piezas[i], cols, 600);
  return n && cols[0] < E.nv->cols ? E.nv->edge[0][cols[0]] : -1;
}

static int EdgeOut(int i) {
  int16_t cols[600];
  const int n = RecPiezaCols(&E.r->piezas[i], cols, 600);
  return n && cols[n - 1] < E.nv->cols ? E.nv->edge[1][cols[n - 1]] : -1;
}

static bool UnionMala(int i) { /* joint between i and i+1 */
  if (i < 0 || i + 1 >= E.r->npiezas)
    return false;
  const int a = EdgeOut(i), b = EdgeIn(i + 1);
  return a < 0 || b < 0 || abs(a - b) > 2;
}

static void Guardar_undo(void) {
  if (E.nundo == kUndo) {
    free(E.undo[0]);
    memmove(E.undo, E.undo + 1, sizeof(E.undo[0]) * (kUndo - 1));
    E.nundo--;
  }
  Receta *c = malloc(sizeof *c);
  if (!c)
    return;
  *c = *E.r;
  E.undo[E.nundo++] = c;
  for (int i = 0; i < E.nredo; i++)
    free(E.redo[i]);
  E.nredo = 0;
  E.sucia = true;
}

static void Deshacer(bool rehacer) {
  Receta **from = rehacer ? E.redo : E.undo, **to = rehacer ? E.undo : E.redo;
  int *nf = rehacer ? &E.nredo : &E.nundo, *nt = rehacer ? &E.nundo : &E.nredo;
  if (!*nf) {
    Msg(false, rehacer ? "NADA PARA REHACER" : "NADA PARA DESHACER");
    return;
  }
  Receta *c = malloc(sizeof *c);
  if (c && *nt < kUndo) {
    *c = *E.r;
    to[(*nt)++] = c;
  } else {
    free(c);
  }
  Receta *prev = from[--*nf];
  *E.r = *prev;
  free(prev);
  if (E.sel >= E.r->npiezas)
    E.sel = E.r->npiezas - 1;
  E.sucia = true;
}

static void MoverIndices(int desde, int delta) {
  for (int i = 0; i < E.r->noq; i++)
    if (E.r->oq[i].pieza >= desde)
      E.r->oq[i].pieza += delta;
  for (int i = 0; i < E.r->noa; i++)
    if (E.r->oa[i].pieza >= desde)
      E.r->oa[i].pieza += delta;
  for (int i = 0; i < E.r->npq; i++)
    if (E.r->pq[i].pieza >= desde)
      E.r->pq[i].pieza += delta;
}

static void IrAPieza(int i) {
  const int a = Inicio(i) * kCol, b = a + PiezaLen(&E.r->piezas[i]) * kCol;
  const int vis = (int)(W / E.zoom);
  if (a < E.x0 || b > E.x0 + vis)
    E.x0 = a - vis / 5 > 0 ? a - vis / 5 : 0;
  E.vy_manual = false;
}

static void Insertar(const Pieza *p) {
  const int donde = E.sel + 1 < E.r->npiezas ? E.sel + 1 : E.r->npiezas - 1;
  if (E.r->npiezas >= kRecMaxPieces) {
    Msg(true, "DEMASIADAS PIEZAS");
    return;
  }
  Guardar_undo();
  memmove(&E.r->piezas[donde + 1], &E.r->piezas[donde],
          sizeof(Pieza) * (size_t)(E.r->npiezas - donde));
  E.r->piezas[donde] = *p;
  E.r->npiezas++;
  MoverIndices(donde, +1);
  E.sel = donde;
  IrAPieza(donde);
}

static void QuitarRefs(int pieza) {
  int k = 0;
  for (int i = 0; i < E.r->noq; i++)
    if (E.r->oq[i].pieza != pieza)
      E.r->oq[k++] = E.r->oq[i];
  E.r->noq = k;
  k = 0;
  for (int i = 0; i < E.r->noa; i++)
    if (E.r->oa[i].pieza != pieza)
      E.r->oa[k++] = E.r->oa[i];
  E.r->noa = k;
  k = 0;
  for (int i = 0; i < E.r->npq; i++)
    if (E.r->pq[i].pieza != pieza)
      E.r->pq[k++] = E.r->pq[i];
  E.r->npq = k;
}

static void Borrar(int i) {
  if (i <= 0 || i >= E.r->npiezas - 1) {
    Msg(true, "LA PARTIDA Y LA META NO SE BORRAN");
    return;
  }
  Guardar_undo();
  QuitarRefs(i);
  memmove(&E.r->piezas[i], &E.r->piezas[i + 1],
          sizeof(Pieza) * (size_t)(E.r->npiezas - i - 1));
  E.r->npiezas--;
  MoverIndices(i + 1, -1);
  if (E.sel >= E.r->npiezas - 1)
    E.sel = E.r->npiezas - 2;
}

static void Mover(int i, int j) {
  const int n = E.r->npiezas;
  if (i <= 0 || i >= n - 1 || j <= 0 || j >= n - 1 || i == j)
    return;
  Guardar_undo();
  int mapa[kRecMaxPieces];
  for (int k = 0; k < n; k++)
    mapa[k] = k;
  const Pieza p = E.r->piezas[i];
  const int m = mapa[i];
  if (i < j) {
    memmove(&E.r->piezas[i], &E.r->piezas[i + 1], sizeof(Pieza) * (size_t)(j - i));
    memmove(&mapa[i], &mapa[i + 1], sizeof(int) * (size_t)(j - i));
  } else {
    memmove(&E.r->piezas[j + 1], &E.r->piezas[j], sizeof(Pieza) * (size_t)(i - j));
    memmove(&mapa[j + 1], &mapa[j], sizeof(int) * (size_t)(i - j));
  }
  E.r->piezas[j] = p;
  mapa[j] = m;
  int nuevo[kRecMaxPieces];
  for (int k = 0; k < n; k++)
    nuevo[mapa[k]] = k;
  for (int k = 0; k < E.r->noq; k++)
    E.r->oq[k].pieza = nuevo[E.r->oq[k].pieza];
  for (int k = 0; k < E.r->noa; k++)
    E.r->oa[k].pieza = nuevo[E.r->oa[k].pieza];
  for (int k = 0; k < E.r->npq; k++)
    E.r->pq[k].pieza = nuevo[E.r->pq[k].pieza];
  E.sel = j;
  IrAPieza(j);
}

static void Duplicar(int i) {
  if (i <= 0 || i >= E.r->npiezas - 1) {
    Msg(true, "LA PARTIDA Y LA META NO SE DUPLICAN");
    return;
  }
  const Pieza p = E.r->piezas[i];
  E.sel = i;
  Insertar(&p);
}

static void AjustarLargo(int d, bool desde) {
  Pieza *p = &E.r->piezas[E.sel];
  if (p->tipo == kPiezaRecta) {
    if (p->largo + d < 1 || p->largo + d > 40)
      return;
    Guardar_undo();
    p->largo += d;
  } else if (p->tipo == kPiezaTramo) {
    if (desde) {
      if (E.sel == 0 || p->desde + d < 0 || p->desde + d >= p->hasta)
        return;
      Guardar_undo();
      p->desde += d;
    } else {
      if (E.sel == E.r->npiezas - 1 || p->hasta + d <= p->desde ||
          p->hasta + d > E.nv->cols)
        return;
      Guardar_undo();
      p->hasta += d;
    }
  } else {
    Msg(true, "LOS HOYOS NO CAMBIAN DE LARGO");
  }
}

/* ---- objects ---------------------------------------------------------------- */

static RecObj s_objs[200];
static int s_nobjs, s_salida_x;

static void Plantillas(void) {
  E.nplant = 0;
  for (int i = 0; i < E.nv->nobjetos && E.nplant < 32; i++) {
    const NivObjeto *o = &E.nv->objetos[i];
    if (o->tipo != 1)
      continue;
    int k = 0;
    while (k < E.nplant && !(E.plant_ptr[k] == o->ptr &&
                             !strcmp(E.plant_nombre[k], o->nombre)))
      k++;
    if (k < E.nplant)
      continue;
    snprintf(E.plant_nombre[k], 20, "%s", o->nombre);
    E.plant_ptr[k] = o->ptr;
    E.plant_y[k] = o->y;
    E.plant_enemigo[k] = o->enemigo;
    E.nplant++;
  }
}

static void AlternarQuitado(int pieza, int indice) {
  Guardar_undo();
  for (int i = 0; i < E.r->noq; i++)
    if (E.r->oq[i].pieza == pieza && E.r->oq[i].indice == indice) {
      E.r->oq[i] = E.r->oq[--E.r->noq];
      Msg(false, "OBJETO RESTAURADO");
      return;
    }
  if (E.r->noq < kRecMaxRemoved) {
    E.r->oq[E.r->noq].pieza = pieza;
    E.r->oq[E.r->noq].indice = indice;
    E.r->noq++;
    Msg(false, "OBJETO QUITADO (CLIC OTRA VEZ PARA RESTAURAR)");
  }
}

static int PiezaEnX(int x) {
  const int n = x / kCol;
  if (n < 0)
    return 0;
  if (n >= s_total)
    return E.r->npiezas - 1;
  return s_pieza_de[n];
}

static void AgregarObjeto(int x, int y) {
  int activos = 0;
  for (int i = 0; i < s_nobjs; i++)
    activos += !s_objs[i].quitado;
  if (activos >= E.nv->nobjetos - 1) {
    Msg(true, "LIMITE DE OBJETOS: %d. QUITA UNO ANTES", E.nv->nobjetos - 1);
    return;
  }
  if (E.r->noa >= kRecMaxAdded)
    return;
  Guardar_undo();
  const int p = PiezaEnX(x);
  RecAgregar *a = &E.r->oa[E.r->noa++];
  memset(a, 0, sizeof *a);
  a->pieza = p;
  a->dx = x - Inicio(p) * kCol;
  a->y = y < 0x10 ? 0x10 : y > 0x1F0 ? 0x1F0 : y;
  snprintf(a->nombre, sizeof a->nombre, "%s", E.plant_nombre[E.plantilla]);
  a->puntero = E.plant_ptr[E.plantilla];
  a->enemigo = E.plant_enemigo[E.plantilla];
  Msg(false, "%s AGREGADO", a->nombre);
}

static void BorrarAgregado(int n) {
  if (n < 0 || n >= E.r->noa)
    return;
  Guardar_undo();
  memmove(&E.r->oa[n], &E.r->oa[n + 1], sizeof(RecAgregar) * (size_t)(E.r->noa - n - 1));
  E.r->noa--;
  Msg(false, "OBJETO BORRADO");
}

/* ---- list ------------------------------------------------------------------- */

static void CargarLista(void) {
  E.nlista = RecListar(E.ids, 400);
  static Receta r;
  for (int i = 0; i < E.nlista; i++) {
    E.nombres[i][0] = 0;
    E.niveles[i][0] = 0;
    E.ncols[i] = 0;
    if (RecCargar(E.ids[i], &r)) {
      snprintf(E.nombres[i], 32, "%s", r.nombre);
      snprintf(E.niveles[i], 12, "%s", r.nivel);
      E.ncols[i] = RecTotalCols(&r);
    }
  }
}

static int FiltrarLista(int *out) {
  int n = 0;
  out[n++] = -1; /* nueva carnage */
  out[n++] = -2; /* nueva madness */
  for (int i = 0; i < E.nlista; i++) {
    if (E.filtro[0]) {
      char a[100], f[32];
      snprintf(a, sizeof a, "%s %s", E.nombres[i], E.ids[i]);
      for (char *c = a; *c; c++)
        *c = (char)toupper((unsigned char)*c);
      snprintf(f, sizeof f, "%s", E.filtro);
      for (char *c = f; *c; c++)
        *c = (char)toupper((unsigned char)*c);
      if (!strstr(a, f))
        continue;
    }
    out[n++] = i;
  }
  return n;
}

static void Centrar(void);

static bool Abrir(Receta *r, bool nueva) {
  const Nivel *nv = RecNivel(r->nivel);
  if (!nv) {
    Msg(true, "NO SE PUDO LEER EL NIVEL %s", r->nivel);
    return false;
  }
  if (!Cart2pDualCargarNivel(r->nivel)) {
    Msg(true, "NO SE PUDO CARGAR EL NIVEL");
    return false;
  }
  LiberarCache();
  if (!E.r)
    E.r = malloc(sizeof(Receta));
  *E.r = *r;
  for (int i = 0; i < E.nundo; i++)
    free(E.undo[i]);
  for (int i = 0; i < E.nredo; i++)
    free(E.redo[i]);
  E.nundo = E.nredo = 0;
  E.nv = nv;
  E.nueva = nueva;
  E.sucia = nueva;
  E.sel = r->npiezas > 1 ? 1 : 0;
  E.x0 = 0;
  E.vy_manual = false;
  E.ov = kOvNada;
  E.plantilla = -1;
  E.modo = kModoEditar;
  Plantillas();
  Msg(false, nueva ? "ETAPA NUEVA (SIN GUARDAR). TAB: PIEZAS  F1: AYUDA"
                   : "F1: AYUDA   TAB: PIEZAS   CTRL+S: GUARDAR   P: PROBAR");
  Cart2pLog("editor: %s %s", nueva ? "nueva" : "abre", r->id);
  return true;
}

static void NuevaEtapa(const char *nivel) {
  const Nivel *nv = RecNivel(nivel);
  if (!nv) {
    Msg(true, "NO SE PUDO LEER EL NIVEL");
    return;
  }
  static Receta r;
  char id[48];
  const time_t t = time(NULL);
  struct tm *tm = localtime(&t);
  if (tm)
    strftime(id, sizeof id, "mia_%Y%m%d_%H%M%S", tm);
  else
    snprintf(id, sizeof id, "mia_%ld", (long)t);
  RecDesdeNivel(&r, nv, id, "MI ETAPA");
  snprintf(r.descripcion, sizeof r.descripcion,
           "Etapa creada en el editor del juego a partir de %s.", nv->titulo);
  Abrir(&r, true);
}

void Cart2pEditorAbrir(void) {
  memset(&E.ids, 0, sizeof E.ids);
  E.abierto = true;
  E.modo = kModoLista;
  E.ov = kOvNada;
  E.salir = false;
  E.probar[0] = 0;
  if (E.zoom <= 0)
    E.zoom = 0.5;
  if (!E.recta_largo)
    E.recta_largo = 4;
  if (!E.tramo_largo)
    E.tramo_largo = 32;
  E.solo_calzan = true;
  E.lista_sel = 2;
  E.lista_top = 0;
  E.filtro[0] = 0;
  CargarLista();
  Msg(false, "ELIGE UNA ETAPA PARA EDITAR O CREA UNA NUEVA");
}

void Cart2pEditorVolverDePrueba(void) {
  E.ov = kOvNada;
  E.probar[0] = 0;
  if (E.r)
    Cart2pDualCargarNivel(E.r->nivel);
  LiberarCache();
  Msg(false, "DE VUELTA EN EL EDITOR");
}

/* ---- save / test ------------------------------------------------------------- */

static bool GuardarEtapa(void) {
  char errs[6][160];
  const int n = RecValidar(E.r, E.nv, errs, 6);
  if (n) {
    Msg(true, "NO SE PUEDE GUARDAR: %s", errs[0]);
    return false;
  }
  if (E.nueva && RecExiste(E.r->id)) {
    Msg(true, "YA EXISTE UNA ETAPA %s", E.r->id);
    return false;
  }
  char cambio[96];
  snprintf(cambio, sizeof cambio, "%s en el editor del juego (%d piezas, %d columnas)",
           E.nueva ? "creada" : "editada", E.r->npiezas, RecTotalCols(E.r));
  if (!RecGuardar(E.r, cambio)) {
    Msg(true, "ERROR AL GUARDAR (VER EL REGISTRO)");
    return false;
  }
  E.nueva = false;
  E.sucia = false;
  Cart2pDualRecargarLista();
  CargarLista();
  Msg(false, "GUARDADA Y AGREGADA AL MENU: %s", E.r->id);
  return true;
}

static void Probar(void) {
  if ((E.sucia || E.nueva) && !GuardarEtapa())
    return;
  snprintf(E.probar, sizeof E.probar, "%s", E.r->id);
}

/* ---- per frame ----------------------------------------------------------------- */

int Cart2pEditorCuadro(char *probar_id, size_t size) {
  E.frame++;
  if (E.msg_frames > 0)
    E.msg_frames--;
  if (E.salir) {
    E.salir = false;
    E.abierto = false;
    return 1;
  }
  if (E.probar[0]) {
    snprintf(probar_id, size, "%s", E.probar);
    E.probar[0] = 0;
    return 2;
  }
  return 0;
}

/* ---- drawing -------------------------------------------------------------------- */

static void Centrar(void) {
  if (E.vy_manual)
    return;
  const int visible_h = (int)(kTrackH / E.zoom);
  int suma = 0, n = 0;
  const int c0 = E.x0 / kCol, c1 = (E.x0 + (int)(W / E.zoom)) / kCol;
  for (int c = c0; c <= c1 && c < s_total; c++) {
    const int y = E.nv->edge[0][s_src[c]];
    if (y >= 0) {
      suma += y;
      n++;
    }
  }
  const int ry = n ? 0x200 - suma / n : 256;
  int vy = ry - visible_h / 2;
  if (vy > kLevelH - visible_h)
    vy = kLevelH - visible_h;
  if (vy < 0)
    vy = 0;
  E.vy = vy;
}

static int Sx(int x) { return (int)((x - E.x0) * E.zoom); }
static int Sy(int y) { return kTrackY + (int)((0x200 - y - E.vy) * E.zoom); }

static const char *Abrev(const char *n) {
  static const char *const k[][2] = {
      {"ZINGER", "Z"}, {"NECKY", "N"}, {"GNAWTY", "G"}, {"CARRO", "C"},
      {"LETRA KONG", "K"}, {"RACIMO", "R"}, {"BARRIL DK", "DK"}, {"CANON", "CA"},
      {"COFRE", "CF"}, {"FICHA", "F"}, {"BARRIL CONTROL", "BC"}, {"GLOBO", "GL"},
      {"SENAL", "S"}, {"NEUMATICO", "NE"}, {"MEDIO NEUMATICO", "NE"}, {"CUERDA", "CU"}};
  for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
    if (!strcmp(k[i][0], n))
      return k[i][1];
  return "?";
}

static uint32_t ColorObj(const char *n, bool enemigo) {
  if (enemigo)
    return kRed;
  if (!strcmp(n, "LETRA KONG"))
    return kYellow;
  if (!strcmp(n, "BARRIL DK") || !strcmp(n, "CANON") || !strcmp(n, "BARRIL CONTROL"))
    return kBrown;
  return kBlue;
}

static void Circulo(int cx, int cy, int r, uint32_t c, uint32_t borde) {
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++) {
      const int d = x * x + y * y;
      if (d <= r * r)
        Px(cx + x, cy + y, d >= (r - 1) * (r - 1) ? borde : c);
    }
}

static void DibujarPista(void) {
  const int vis_w = (int)(W / E.zoom);
  if (E.x0 > s_total * kCol - vis_w)
    E.x0 = s_total * kCol - vis_w;
  if (E.x0 < 0)
    E.x0 = 0;
  Centrar();
  /* level graphics (with the stage's theme, if any) */
  CargarPiel();
  for (int sy = 0; sy < kTrackH; sy++) {
    const int ly = E.vy + (int)(sy / E.zoom);
    uint32_t *row = (uint32_t *)(s_buf + (size_t)(kTrackY + sy) * s_pitch);
    for (int sx = 0; sx < W; sx++) {
      const int lx = E.x0 + (int)(sx / E.zoom);
      const int n = lx / kCol;
      if (n >= s_total || ly >= kLevelH) {
        row[sx] = 0;
        continue;
      }
      row[sx] = PixelEtapa(n, s_src[n], ly, lx & 31);
    }
  }
  /* rail line */
  for (int sx = 0; sx < W; sx++) {
    const int lx = E.x0 + (int)(sx / E.zoom);
    const int n = lx / kCol;
    if (n >= s_total)
      break;
    const int y = RecRailY(E.nv, s_src[n] * kCol + (lx & 31));
    if (y >= 0) {
      const int py = Sy(y);
      if (py >= kTrackY && py < kTrackY + kTrackH)
        Px(sx, py, 0x00FF503Cu);
    }
  }
  /* pieces: top band, borders, selection, bad joints */
  for (int i = 0; i < E.r->npiezas; i++) {
    const int a = Inicio(i) * kCol, len = PiezaLen(&E.r->piezas[i]) * kCol;
    const int xa = Sx(a), xb = Sx(a + len);
    if (xb < 0 || xa >= W)
      continue;
    const PiezaTipo t = E.r->piezas[i].tipo;
    const uint32_t c = t == kPiezaTramo ? kTramoC : t == kPiezaRecta ? kRectaC
                       : t == kPiezaColumnas ? 0x009A7FD1u : kHoyoC;
    Shade(xa, kTrackY, xb - xa, 9, c, i == E.sel ? 230 : 150);
    Rect(xa, kTrackY, 1, kTrackH, Mix(c, 0x00FFFFFFu, 80));
    char num[8];
    Fmt(num, sizeof num, "%d", i + 1);
    if (xb - xa > 14)
      Text(xa + 2, kTrackY + 1, kText, num);
    if (i == E.sel) {
      Frame(xa, kTrackY, xb - xa, kTrackH, kYellow);
      Frame(xa + 1, kTrackY + 1, xb - xa - 2, kTrackH - 2, kYellow);
    }
    if (UnionMala(i)) {
      Rect(xb - 2, kTrackY, 4, kTrackH, kRed);
      Text(xb + 4, kTrackY + 12, kRed, "NO CALZA");
    } else if (RecCorteVisible(E.r, E.nv, i)) {
      for (int yy = kTrackY + 9; yy < kTrackY + kTrackH; yy += 6)
        Rect(xb - 1, yy, 2, 3, kOrange);
      Text(xb + 4, kTrackY + 12, kOrange, "CORTE");
    }
  }
  /* start and finish */
  const int sp = Sx(0x3A6);
  Rect(sp, kTrackY + 9, 1, kTrackH - 9, kGreen);
  Text(sp + 3, kTrackY + 12, kGreen, "PARTIDA");
  if (s_salida_x >= 0) {
    const int mx = Sx(s_salida_x);
    Rect(mx, kTrackY + 9, 2, kTrackH - 9, kGreen);
    Text(mx + 4, kTrackY + 12, kGreen, "META");
  }
  /* bananas (first copies of each tramo) */
  for (int i = 0; i < E.r->npiezas; i++) {
    const Pieza *p = &E.r->piezas[i];
    if (p->tipo != kPiezaTramo)
      continue;
    const int ini = Inicio(i) * kCol;
    for (int k = 0; k < E.nv->nplatanos; k++) {
      const NivPlatano *g = &E.nv->platanos[k];
      if (g->x < p->desde * kCol || g->x >= p->hasta * kCol)
        continue;
      const int x = Sx(g->x - p->desde * kCol + ini), y = Sy(g->y);
      if (y >= kTrackY + 9 && y < kTrackY + kTrackH)
        Rect(x - 1, y - 1, 3, 3, 0x00FFE14Du);
    }
  }
  /* objects */
  const int rad = E.zoom >= 0.75 ? 7 : 5;
  for (int i = 0; i < s_nobjs; i++) {
    const RecObj *o = &s_objs[i];
    const int x = Sx(o->x), y = Sy(o->y);
    if (x < -10 || x > W + 10 || y < kTrackY + 6 || y > kTrackY + kTrackH + 4)
      continue;
    const uint32_t c = ColorObj(o->nombre, o->enemigo);
    Circulo(x, y, rad, o->quitado ? Mix(c, 0, 160) : c,
            o->agregado >= 0 ? 0x00FFFFFFu : 0x00000000u);
    const char *ab = Abrev(o->nombre);
    Text(x - TextW(ab) / 2, y - 3, 0x00111111u, ab);
    if (o->quitado)
      for (int d = -rad; d <= rad; d++) {
        Px(x + d, y + d, 0x00FFFFFFu);
        Px(x + d, y - d, 0x00FFFFFFu);
      }
  }
  if (E.plantilla >= 0 && E.my >= kTrackY && E.my < kTrackY + kTrackH) {
    Circulo(E.mx, E.my, rad, ColorObj(E.plant_nombre[E.plantilla],
                                       E.plant_enemigo[E.plantilla]), 0x00FFFFFFu);
    Text(E.mx + 10, E.my - 3, kYellow, "CLIC: PONER");
  }
}

static void DibujarMinimapa(void) {
  Rect(0, kMiniY, W, kMiniH, 0);
  if (!s_total)
    return;
  for (int sx = 0; sx < W; sx++) {
    const int n = sx * s_total / W;
    const int p = s_pieza_de[n];
    const PiezaTipo t = E.r->piezas[p].tipo;
    uint32_t c = t == kPiezaTramo ? (p % 2 ? 0x00304E70u : 0x00406A96u)
                 : t == kPiezaRecta ? kRectaC : kHoyoC;
    Rect(sx, kMiniY, 1, kMiniH, c);
  }
  const int a = E.x0 * W / (s_total * kCol);
  const int w = (int)(W / E.zoom) * W / (s_total * kCol);
  Frame(a, kMiniY, w < 3 ? 3 : w, kMiniH, kYellow);
}

static int StripScroll(void) {
  const int visibles = W / (kCardW + 2);
  int first = E.sel - visibles / 2;
  if (first > E.r->npiezas - visibles)
    first = E.r->npiezas - visibles;
  return first < 0 ? 0 : first;
}

static void Hex3(char *out, size_t n, int v) {
  if (v < 0)
    snprintf(out, n, "---");
  else
    snprintf(out, n, "%03X", v);
}

static void DibujarTira(void) {
  Rect(0, kStripY, W, kStripH, kBg);
  const int first = StripScroll();
  for (int i = first; i < E.r->npiezas; i++) {
    const int x = (i - first) * (kCardW + 2) + 1;
    if (x > W)
      break;
    const Pieza *p = &E.r->piezas[i];
    const uint32_t c = p->tipo == kPiezaTramo ? kTramoC : p->tipo == kPiezaRecta ? kRectaC
                                                                                 : kHoyoC;
    Rect(x, kStripY + 1, kCardW, kStripH - 2, kPanel2);
    Rect(x, kStripY + 1, kCardW, 3, c);
    if (i == E.sel)
      Frame(x - 1, kStripY, kCardW + 2, kStripH, kYellow);
    if (E.arrastre_carta && i == E.carta_desde)
      Shade(x, kStripY + 1, kCardW, kStripH - 2, 0, 140);
    char l1[16], l2[16], a[8], b[8];
    if (p->tipo == kPiezaTramo)
      Fmt(l1, sizeof l1, "%dT%d", i + 1, p->desde);
    else if (p->tipo == kPiezaRecta)
      Fmt(l1, sizeof l1, "%dR%d", i + 1, p->largo);
    else
      Fmt(l1, sizeof l1, "%d%s", i + 1, p->tipo == kPiezaDoble ? "HH" : "H");
    Text(x + 2, kStripY + 6, kText, l1);
    Fmt(l2, sizeof l2, "%dC", PiezaLen(p));
    Text(x + 2, kStripY + 14, kDim, l2);
    Hex3(a, sizeof a, EdgeIn(i));
    Hex3(b, sizeof b, EdgeOut(i));
    Text(x + 2, kStripY + 22, UnionMala(i - 1) ? kRed : kDim, a);
    Text(x + 25, kStripY + 22,
         UnionMala(i) ? kRed : RecCorteVisible(E.r, E.nv, i) ? kOrange : kDim, b);
  }
  if (E.arrastre_carta && E.my >= kStripY && E.my < kStripY + kStripH) {
    const int k = E.mx / (kCardW + 2);
    Rect(k * (kCardW + 2) - 1, kStripY, 2, kStripH, kYellow);
  }
}

static void DibujarBarras(void) {
  Rect(0, 0, W, kTop, 0x000D0B0Au);
  char t[160];
  RecStats st;
  RecEstadisticas(E.r, E.nv, &st);
  Fmt(t, sizeof t, "%s%s  %s%s%s%s%s  %d COL  %.0f S", E.sucia ? "*" : "", E.r->nombre,
      E.nv->nombre, E.r->tema[0] ? " TEMA " : "", E.r->tema,
      E.r->musica[0] ? " MUSICA " : "", E.r->musica, st.columnas, st.segundos);
  Text(3, 2, kYellow, t);
  char errs[4][160];
  const int ne = RecValidar(E.r, E.nv, errs, 4);
  if (ne) {
    Fmt(t, sizeof t, "%d PROBLEMA%s", ne, ne > 1 ? "S" : "");
    TextR(W - 3, 2, kRed, t);
  } else {
    int cortes = 0;
    for (int i = 0; i + 1 < E.r->npiezas; i++)
      cortes += RecCorteVisible(E.r, E.nv, i);
    if (cortes) {
      Fmt(t, sizeof t, "VALIDA  %d CORTE%s", cortes, cortes > 1 ? "S" : "");
      TextR(W - 3, 2, kOrange, t);
    } else {
      TextR(W - 3, 2, kGreen, "VALIDA SIN CORTES");
    }
  }
  /* info line */
  Rect(0, kInfoY, W, H - kInfoY, kBg);
  const Pieza *p = &E.r->piezas[E.sel];
  char a[8], b[8];
  Hex3(a, sizeof a, EdgeIn(E.sel));
  Hex3(b, sizeof b, EdgeOut(E.sel));
  if (E.msg_frames > 0) {
    Text(3, kInfoY + 1, E.msg_mal ? kRed : kGreen, E.msg);
  } else if (ne) {
    Text(3, kInfoY + 1, kRed, errs[0]);
  } else {
    const char *tipo = p->tipo == kPiezaTramo ? "TRAMO" : p->tipo == kPiezaRecta ? "RECTA"
                       : p->tipo == kPiezaDoble ? "DOBLE HOYO" : p->tipo == kPiezaHoyo ? "HOYO"
                                                                                       : "COLUMNAS";
    if (p->tipo == kPiezaTramo)
      Fmt(t, sizeof t, "PIEZA %d/%d %s %d-%d  X $%04X  VIA $%s>$%s  +/-: LARGO",
          E.sel + 1, E.r->npiezas, tipo, p->desde, p->hasta - 1, Inicio(E.sel) * kCol, a, b);
    else
      Fmt(t, sizeof t, "PIEZA %d/%d %s  %d COL  X $%04X  VIA $%s>$%s", E.sel + 1,
          E.r->npiezas, tipo, PiezaLen(p), Inicio(E.sel) * kCol, a, b);
    Text(3, kInfoY + 1, kText, t);
  }
  Text(3, kHelpY + 2, kDim,
       "TAB PIEZAS  O OBJETOS  N NOMBRE  T TEMA  M MUSICA  SUPR BORRAR  CTRL+S GUARDAR  P PROBAR");
}

/* ---- overlays --------------------------------------------------------------------- */

typedef struct {
  Pieza p;
  char t1[48], t2[48];
  bool calza;
} Opcion;

static Opcion s_ops[160];
static int s_nops;

static int SalidaSel(void) {
  const int i = E.sel < E.r->npiezas - 1 ? E.sel : E.r->npiezas - 2;
  return i >= 0 ? EdgeOut(i) : -1;
}

static int NearestFlat(int y, int near) {
  int best = -1;
  for (int c = 0; c < E.nv->cols; c++)
    if (E.nv->flat_y[c] == y && (best < 0 || abs(c - near) < abs(best - near)))
      best = c;
  return best;
}

/* Rail height where the inserted piece must end: the entry of the piece
 * after the chosen one (-1 = anything). */
static int EntradaSiguiente(void) {
  const int i = E.sel < E.r->npiezas - 1 ? E.sel + 1 : E.r->npiezas - 1;
  return EdgeIn(i);
}

static void ArmarOpciones(void) {
  s_nops = 0;
  const int ys = SalidaSel();
  const int ye = EntradaSiguiente();
  const Nivel *nv = E.nv;
  /* fits on both sides: enters at ys and leaves at ye */
#define CALZA2(yin, yout) ((ys < 0 || ((yin) >= 0 && abs((yin) - ys) <= 2)) &&                            (ye < 0 || ((yout) >= 0 && abs((yout) - ye) <= 2)))
#define CALZA(y) CALZA2(y, y)
  if (E.pal_tab == 0) {
    for (int k = 1; k + 1 < nv->ntramos && s_nops < 160; k++) {
      const NivTramo *t = &nv->tramos[k];
      const int yin = nv->edge[0][t->a], yout = nv->edge[1][t->b - 1];
      const bool ok = CALZA2(yin, yout);
      if (E.solo_calzan && !ok)
        continue;
      int ene = 0, otros = 0;
      for (int i = 0; i < nv->nobjetos; i++)
        if (nv->objetos[i].x >= t->a * kCol && nv->objetos[i].x < t->b * kCol)
          nv->objetos[i].enemigo ? ene++ : otros++;
      Opcion *o = &s_ops[s_nops++];
      memset(o, 0, sizeof *o);
      o->p.tipo = kPiezaTramo;
      o->p.desde = t->a;
      o->p.hasta = t->b;
      o->calza = ok;
      Fmt(o->t1, sizeof o->t1, "TRAMO %d-%d  %d COL", t->a, t->b - 1, t->b - t->a);
      Fmt(o->t2, sizeof o->t2, "VIA %03X>%03X  %d ENEM %d OTROS", yin, yout, ene, otros);
    }
  } else if (E.pal_tab == 1) {
    int vistos[64], nv2 = 0;
    for (int c = 0; c < nv->cols; c++) {
      const int y = nv->flat_y[c];
      if (y < 0)
        continue;
      int k = 0;
      while (k < nv2 && vistos[k] != y)
        k++;
      if (k < nv2 || nv2 >= 64)
        continue;
      vistos[nv2++] = y;
    }
    for (int i = 1; i < nv2; i++) /* by height */
      for (int j = i; j > 0 && vistos[j - 1] > vistos[j]; j--) {
        const int t = vistos[j];
        vistos[j] = vistos[j - 1];
        vistos[j - 1] = t;
      }
    for (int k = 0; k < nv2 && s_nops < 160; k++) {
      const bool ok = CALZA(vistos[k]);
      if (E.solo_calzan && !ok)
        continue;
      int16_t cols[600];
      const int n = RecPiezaCols(&E.r->piezas[E.sel], cols, 600);
      const int c = NearestFlat(vistos[k], n ? cols[n - 1] : 0);
      Opcion *o = &s_ops[s_nops++];
      memset(o, 0, sizeof *o);
      o->p.tipo = kPiezaRecta;
      o->p.columna = c;
      o->p.largo = E.recta_largo;
      o->calza = ok;
      Fmt(o->t1, sizeof o->t1, "RECTA A $%03X  LARGO %d", vistos[k], E.recta_largo);
      Fmt(o->t2, sizeof o->t2, "COLUMNA %d  (+/- CAMBIA EL LARGO)", c);
    }
  } else if (E.pal_tab == 3) {
    /* seamless: continue at a column identical to the one that follows */
    int16_t cols[600];
    const int n = RecPiezaCols(&E.r->piezas[E.sel], cols, 600);
    const int nat = n ? cols[n - 1] + 1 : 0;
    int gem[24];
    int ng = RecGemelas(nv, nat, gem, 23);
    if (nat < nv->cols)
      gem[ng++] = nat; /* the original continuation */
    for (int k = 0; k < ng && s_nops < 160; k++) {
      int hasta = gem[k] + E.tramo_largo;
      if (hasta > nv->cols)
        hasta = nv->cols;
      const int yin = nv->edge[0][gem[k]], yout = nv->edge[1][hasta - 1];
      const bool ok = CALZA2(yin, yout);
      if (E.solo_calzan && !ok)
        continue;
      Opcion *o = &s_ops[s_nops++];
      memset(o, 0, sizeof *o);
      o->p.tipo = kPiezaTramo;
      o->p.desde = gem[k];
      o->p.hasta = hasta;
      o->calza = ok;
      Fmt(o->t1, sizeof o->t1, "%s %d-%d", gem[k] == nat ? "SEGUIR" : "SALTO A", gem[k],
          hasta - 1);
      Fmt(o->t2, sizeof o->t2, "SIN CORTE  %d COL (+/-)  VIA %03X>%03X", hasta - gem[k], yin,
          yout);
    }
  } else {
    for (int b = 0; b < nv->nbloques && s_nops < 158; b++) {
      const NivBloque *bl = &nv->bloques[b];
      const bool ok = CALZA(bl->y);
      if (E.solo_calzan && !ok)
        continue;
      const int f = NearestFlat(bl->y, bl->cols[0]);
      for (int doble = 0; doble < 2; doble++) {
        Opcion *o = &s_ops[s_nops++];
        memset(o, 0, sizeof *o);
        Pieza *p = &o->p;
        p->tipo = doble ? kPiezaDoble : kPiezaHoyo;
        for (int k = 0; k < 2; k++)
          p->cols[p->ncols++] = (int16_t)f;
        for (int h = 0; h <= doble; h++) {
          if (h)
            for (int k = 0; k < 5; k++)
              p->cols[p->ncols++] = (int16_t)f;
          for (int k = 0; k < bl->ncols; k++)
            p->cols[p->ncols++] = bl->cols[k];
          p->hoyos_px[p->nhoyos++] = bl->w;
        }
        for (int k = 0; k < 2; k++)
          p->cols[p->ncols++] = (int16_t)f;
        o->calza = ok;
        Fmt(o->t1, sizeof o->t1, "%s %d PX A $%03X", doble ? "DOBLE HOYO" : "HOYO", bl->w,
            bl->y);
        Fmt(o->t2, sizeof o->t2, "BORDE ORIGINAL X $%04X  %d COL", bl->x, p->ncols);
      }
    }
  }
#undef CALZA
#undef CALZA2
  if (E.pal_sel >= s_nops)
    E.pal_sel = s_nops ? s_nops - 1 : 0;
}

static void Miniatura(int x, int y, int w, int h, const Pieza *p) {
  int16_t cols[600];
  const int n = RecPiezaCols(p, cols, 600);
  Rect(x, y, w, h, 0);
  if (!n)
    return;
  int suma = 0, k = 0;
  for (int i = 0; i < n; i++)
    if (E.nv->edge[0][cols[i]] >= 0) {
      suma += E.nv->edge[0][cols[i]];
      k++;
    }
  const int ry = k ? 0x200 - suma / k : 256;
  int top = ry - 110;
  if (top < 0)
    top = 0;
  if (top > kLevelH - 220)
    top = kLevelH - 220;
  for (int sx = 0; sx < w; sx++) {
    const int lx = sx * n * kCol / w;
    const uint32_t *bmp = Columna(cols[lx / kCol]);
    if (!bmp)
      continue;
    for (int sy = 0; sy < h; sy++) {
      const int ly = top + sy * 220 / h;
      Px(x + sx, y + sy, bmp[ly * kCol + (lx & 31)]);
    }
  }
}

static const int kRowH = 22;

static int PanelFilas(void) {
  return (kStripY - kTrackY - 24) / kRowH;
}

static void DibujarPaleta(void) {
  ArmarOpciones();
  const int x = kPanelX, y = kTrackY, w = W - kPanelX, h = kStripY - kTrackY;
  Rect(x, y, w, h, kPanel);
  Frame(x, y, w, h, kBorder);
  static const char *const tabs[4] = {"TRAMOS", "RECTAS", "HOYOS", "SIN CORTE"};
  for (int t = 0; t < 4; t++) {
    const int tx = x + 4 + t * 48;
    if (t == E.pal_tab)
      Rect(tx - 2, y + 2, t == 3 ? 58 : 44, 10, kPanel2);
    Text(tx, y + 4, t == E.pal_tab ? kYellow : kDim, tabs[t]);
  }
  Text(x + 4, y + 14, kDim, E.solo_calzan ? "C: SOLO LAS QUE CALZAN (SI)"
                                           : "C: SOLO LAS QUE CALZAN (NO)");
  const int filas = PanelFilas();
  if (E.pal_sel < E.pal_top)
    E.pal_top = E.pal_sel;
  if (E.pal_sel >= E.pal_top + filas)
    E.pal_top = E.pal_sel - filas + 1;
  if (!s_nops)
    Text(x + 4, y + 30, kDim, "NINGUNA CALZA AQUI (C: VER TODAS)");
  for (int i = E.pal_top; i < s_nops && i < E.pal_top + filas; i++) {
    const int ry = y + 24 + (i - E.pal_top) * kRowH;
    if (i == E.pal_sel)
      Rect(x + 2, ry - 1, w - 4, kRowH - 1, kPanel2);
    Miniatura(x + 4, ry, 44, kRowH - 3, &s_ops[i].p);
    const uint32_t c = s_ops[i].calza ? kText : kDim;
    Text(x + 52, ry + 2, i == E.pal_sel ? kYellow : c, s_ops[i].t1);
    Text(x + 52, ry + 11, kDim, s_ops[i].t2);
  }
  Text(x + 4, y + h - 10, kDim, "CLIC/ENTER INSERTA  TAB/ESC CIERRA");
}

static void DibujarObjetosOv(void) {
  const int x = kPanelX, y = kTrackY, w = W - kPanelX, h = kStripY - kTrackY;
  Rect(x, y, w, h, kPanel);
  Frame(x, y, w, h, kBorder);
  Text(x + 4, y + 4, kYellow, "OBJETOS DEL NIVEL");
  int activos = 0;
  for (int i = 0; i < s_nobjs; i++)
    activos += !s_objs[i].quitado;
  char t[64];
  Fmt(t, sizeof t, "%d DE %d EN LA ETAPA", activos, E.nv->nobjetos - 1);
  Text(x + 4, y + 14, kDim, t);
  const int filas = (h - 50) / 13;
  if (E.obj_sel_list < 0)
    E.obj_sel_list = 0;
  for (int i = 0; i < E.nplant && i < filas; i++) {
    const int ry = y + 26 + i * 13;
    if (i == E.plantilla)
      Rect(x + 2, ry - 2, w - 4, 12, kPanel2);
    Circulo(x + 10, ry + 3, 5, ColorObj(E.plant_nombre[i], E.plant_enemigo[i]), 0);
    Fmt(t, sizeof t, "%s  $%04X", E.plant_nombre[i], E.plant_ptr[i]);
    Text(x + 20, ry, i == E.plantilla ? kYellow : kText, t);
  }
  Text(x + 4, y + h - 22, kDim, "CLIC EN UNO, LUEGO EN LA PISTA");
  Text(x + 4, y + h - 12, kDim, "CLIC DER: BORRA AGREGADO  ESC");
}

static void DibujarNombre(void) {
  const int x = 106, y = 70, w = 300, h = 60;
  Rect(x, y, w, h, kPanel);
  Frame(x, y, w, h, kYellow);
  Text(x + 8, y + 8, kYellow, "NOMBRE EN EL MENU");
  Rect(x + 8, y + 22, w - 16, 14, 0);
  char t[48];
  Fmt(t, sizeof t, "%s%s", E.nombre_edit, (E.frame / 20) % 2 ? "_" : "");
  Text(x + 12, y + 26, kText, t);
  Text(x + 8, y + 44, kDim, "ESCRIBE  ENTER: LISTO  ESC: CANCELAR");
}

static void DibujarSalir(void) {
  const int x = 96, y = 70, w = 320, h = 64;
  Rect(x, y, w, h, kPanel);
  Frame(x, y, w, h, kRed);
  Text(x + 8, y + 8, kYellow, "HAY CAMBIOS SIN GUARDAR");
  Text(x + 8, y + 24, kText, "G: GUARDAR Y SALIR");
  Text(x + 8, y + 34, kText, "D: DESCARTAR LOS CAMBIOS Y SALIR");
  Text(x + 8, y + 48, kDim, "ESC: SEGUIR EDITANDO");
}

static void DibujarAyuda(void) {
  const int x = 20, y = 12, w = W - 40, h = 208;
  Rect(x, y, w, h, kPanel);
  Frame(x, y, w, h, kYellow);
  static const char *const lineas[] = {
      "EDITOR DE ETAPAS - TECLAS Y MOUSE",
      "",
      "CLIC EN LA PISTA O EN UNA CARTA: ELEGIR PIEZA",
      "ARRASTRAR UNA CARTA: CAMBIARLA DE LUGAR (TAMBIEN ALT+FLECHAS)",
      "FLECHAS IZQ/DER: PIEZA ANTERIOR/SIGUIENTE   INICIO/FIN",
      "RUEDA: AVANZAR  CTRL+RUEDA O Z/X: ZOOM  ARRIBA/ABAJO: SUBIR/BAJAR",
      "CLIC DERECHO Y ARRASTRAR: MOVER LA VISTA",
      "TAB: PIEZAS PARA INSERTAR DESPUES DE LA ELEGIDA (4: SIN CORTE)",
      "SUPR: BORRAR PIEZA   CTRL+D: DUPLICAR   +/-: LARGO (SHIFT: INICIO)",
      "O: OBJETOS. CLIC EN UN OBJETO DEL NIVEL LO QUITA O LO RESTAURA",
      "   LOS AGREGADOS SE ARRASTRAN; CLIC DERECHO LOS BORRA",
      "N: NOMBRE   T: TEMA (OTRO MUNDO DETRAS DE LA VIA)",
      "M: MUSICA (CANCION DE OTRO NIVEL DEL JUEGO)",
      "CTRL+Z / CTRL+Y: DESHACER / REHACER",
      "CTRL+S: GUARDAR (VALIDA, COMPILA Y LA AGREGA AL MENU)",
      "P: PROBAR AHORA (1 JUGADOR). ESC EN LA PRUEBA VUELVE AQUI",
      "ESC: SALIR DEL EDITOR",
      "",
      "LA VIA DEBE CALZAR EN CADA UNION (ROJO = NO CALZA).",
      "LA PRIMERA PIEZA (PARTIDA) Y LA ULTIMA (META) SON FIJAS.",
      "NARANJO = CORTE VISIBLE (EL PAISAJE CAMBIA DE GOLPE). SIN CORTE LO EVITA.",
  };
  for (size_t i = 0; i < sizeof lineas / sizeof lineas[0]; i++)
    Text(x + 8, y + 6 + (int)i * 9, i ? kText : kYellow, lineas[i]);
  Text(x + 8, y + h - 10, kDim, "CUALQUIER TECLA: CERRAR");
}

static void DibujarLista(void) {
  Rect(0, 0, W, H, kBg);
  Text(8, 6, kYellow, "EDITOR DE ETAPAS");
  char t[96];
  Fmt(t, sizeof t, "BUSCAR: %s%s", E.filtro, (E.frame / 20) % 2 ? "_" : "");
  Text(200, 6, kDim, t);
  int idx[402];
  const int n = FiltrarLista(idx);
  if (E.lista_sel >= n)
    E.lista_sel = n - 1;
  if (E.lista_sel < 0)
    E.lista_sel = 0;
  const int filas = 17;
  if (E.lista_sel < E.lista_top)
    E.lista_top = E.lista_sel;
  if (E.lista_sel >= E.lista_top + filas)
    E.lista_top = E.lista_sel - filas + 1;
  for (int k = E.lista_top; k < n && k < E.lista_top + filas; k++) {
    const int y = 20 + (k - E.lista_top) * 10;
    const bool sel = k == E.lista_sel;
    if (sel)
      Rect(4, y - 2, W - 8, 10, kPanel2);
    if (idx[k] < 0) {
      Text(10, y, sel ? kYellow : kGreen,
           idx[k] == -1 ? "+ NUEVA ETAPA DE MINE CART CARNAGE"
                        : "+ NUEVA ETAPA DE MINE CART MADNESS");
      continue;
    }
    const int i = idx[k];
    Text(10, y, sel ? kYellow : kText, E.nombres[i]);
    Fmt(t, sizeof t, "%s  %d COL", E.niveles[i], E.ncols[i]);
    Text(200, y, kDim, t);
    Text(320, y, kDim, E.ids[i]);
  }
  Fmt(t, sizeof t, "%d ETAPAS", E.nlista);
  TextR(W - 8, 6, kDim, t);
  if (E.msg_frames > 0)
    Text(8, kInfoY + 1, E.msg_mal ? kRed : kGreen, E.msg);
  Text(8, kHelpY + 2, kDim,
       "ARRIBA/ABAJO O CLIC: ELEGIR  ENTER: ABRIR  ESCRIBE: BUSCAR  ESC: MENU PRINCIPAL");
}

void Cart2pEditorDibujar(uint8_t *buffer, size_t pitch, int width) {
  if (!E.abierto || width < W)
    return;
  s_buf = buffer;
  s_pitch = pitch;
  if (E.modo == kModoLista) {
    DibujarLista();
    return;
  }
  Disponer();
  s_nobjs = RecObjetos(E.r, E.nv, s_objs, 200, &s_salida_x);
  Rect(0, 0, W, H, kBg);
  DibujarPista();
  DibujarMinimapa();
  DibujarTira();
  DibujarBarras();
  switch (E.ov) {
  case kOvPaleta: DibujarPaleta(); break;
  case kOvObjetos: DibujarObjetosOv(); break;
  case kOvNombre: DibujarNombre(); break;
  case kOvSalir: DibujarSalir(); break;
  case kOvAyuda: DibujarAyuda(); break;
  default: break;
  }
}

/* ---- input ------------------------------------------------------------------------- */

static void Zoom(double factor, int ancla_x) {
  static const double niveles[] = {0.25, 0.375, 0.5, 0.75, 1.0, 1.5};
  int k = 0;
  while (k < 5 && niveles[k] < E.zoom - 1e-6)
    k++;
  k += factor > 1 ? 1 : -1;
  if (k < 0) k = 0;
  if (k > 5) k = 5;
  const int lx = E.x0 + (int)(ancla_x / E.zoom);
  E.zoom = niveles[k];
  E.x0 = lx - (int)(ancla_x / E.zoom);
}

static void TeclaLista(int key, int mods) {
  int idx[402];
  const int n = FiltrarLista(idx);
  (void)mods;
  if (key == kEdKeyUp && E.lista_sel > 0)
    E.lista_sel--;
  else if (key == kEdKeyDown && E.lista_sel < n - 1)
    E.lista_sel++;
  else if (key == kEdKeyPageUp)
    E.lista_sel = E.lista_sel > 16 ? E.lista_sel - 16 : 0;
  else if (key == kEdKeyPageDown)
    E.lista_sel = E.lista_sel + 16 < n ? E.lista_sel + 16 : n - 1;
  else if (key == kEdKeyBackspace) {
    const size_t l = strlen(E.filtro);
    if (l)
      E.filtro[l - 1] = 0;
  } else if (key == kEdKeyEscape) {
    if (E.filtro[0])
      E.filtro[0] = 0;
    else
      E.salir = true;
  } else if (key == kEdKeyEnter && n) {
    const int i = idx[E.lista_sel];
    if (i == -1)
      NuevaEtapa("carnage");
    else if (i == -2)
      NuevaEtapa("madness");
    else {
      static Receta r;
      if (RecCargar(E.ids[i], &r))
        Abrir(&r, false);
      else
        Msg(true, "NO SE PUDO LEER %s", E.ids[i]);
    }
  }
}

static void TeclaPaleta(int key, int mods) {
  if (key == kEdKeyEscape || key == kEdKeyTab) {
    E.ov = kOvNada;
  } else if (key >= '1' && key <= '4') {
    E.pal_tab = key - '1';
    E.pal_sel = E.pal_top = 0;
  } else if (key == 'c') {
    E.solo_calzan = !E.solo_calzan;
  } else if (key == kEdKeyUp && E.pal_sel > 0) {
    E.pal_sel--;
  } else if (key == kEdKeyDown && E.pal_sel + 1 < s_nops) {
    E.pal_sel++;
  } else if ((key == '+' || key == '=') && E.pal_tab == 3 && E.tramo_largo < 200) {
    E.tramo_largo += 4;
  } else if (key == '-' && E.pal_tab == 3 && E.tramo_largo > 8) {
    E.tramo_largo -= 4;
  } else if ((key == '+' || key == '=') && E.recta_largo < 20) {
    E.recta_largo++;
  } else if (key == '-' && E.recta_largo > 1) {
    E.recta_largo--;
  } else if (key == kEdKeyEnter) {
    ArmarOpciones();
    if (E.pal_sel < s_nops)
      Insertar(&s_ops[E.pal_sel].p);
  } else if (key == kEdKeyLeft || key == kEdKeyRight) {
    E.pal_tab = (E.pal_tab + (key == kEdKeyRight ? 1 : 3)) % 4;
    E.pal_sel = E.pal_top = 0;
  }
  (void)mods;
}

void Cart2pEditorTecla(int key, int mods) {
  if (!E.abierto)
    return;
  const bool ctrl = mods & kEdModCtrl, alt = mods & kEdModAlt,
             shift = mods & kEdModShift;
  if (E.modo == kModoLista) {
    TeclaLista(key, mods);
    return;
  }
  switch (E.ov) {
  case kOvAyuda:
    E.ov = kOvNada;
    return;
  case kOvSalir:
    if (key == 'g') {
      if (GuardarEtapa()) {
        E.ov = kOvNada;
        E.modo = kModoLista;
      }
    } else if (key == 'd') {
      E.ov = kOvNada;
      E.modo = kModoLista;
      Msg(false, "CAMBIOS DESCARTADOS");
    } else if (key == kEdKeyEscape) {
      E.ov = kOvNada;
    }
    return;
  case kOvNombre:
    if (key == kEdKeyEnter) {
      if (E.nombre_edit[0] && strcmp(E.nombre_edit, E.r->nombre)) {
        Guardar_undo();
        snprintf(E.r->nombre, sizeof E.r->nombre, "%s", E.nombre_edit);
      }
      E.ov = kOvNada;
    } else if (key == kEdKeyEscape) {
      E.ov = kOvNada;
    } else if (key == kEdKeyBackspace) {
      const size_t l = strlen(E.nombre_edit);
      if (l)
        E.nombre_edit[l - 1] = 0;
    }
    return;
  case kOvPaleta:
    TeclaPaleta(key, mods);
    return;
  case kOvObjetos:
    if (key == kEdKeyEscape || key == 'o') {
      E.ov = kOvNada;
      E.plantilla = -1;
      return;
    }
    break;
  default:
    break;
  }
  if (ctrl && key == 's') {
    GuardarEtapa();
  } else if (ctrl && key == 'z') {
    Deshacer(false);
  } else if (ctrl && key == 'y') {
    Deshacer(true);
  } else if (ctrl && key == 'd') {
    Duplicar(E.sel);
  } else if (key == kEdKeyF1 || key == 'h') {
    E.ov = kOvAyuda;
  } else if (key == kEdKeyTab) {
    E.ov = kOvPaleta;
    E.pal_sel = E.pal_top = 0;
  } else if (key == 'o') {
    E.ov = kOvObjetos;
  } else if (key == 'n' || key == kEdKeyF2) {
    snprintf(E.nombre_edit, sizeof E.nombre_edit, "%s", E.r->nombre);
    E.ov = kOvNombre;
  } else if (key == 't') {
    /* theme: graphics of another world behind the track (Carnage only) */
    {
      Guardar_undo();
      const int antes = RecTemaId(E.r->tema);
      const int id = (antes + 1) % (RecTemas() + 1);
      snprintf(E.r->tema, sizeof E.r->tema, "%s", id ? RecTemaNombre(id) : "");
      /* the music follows the theme (unless another one was chosen with M) */
      if (!E.r->musica[0] || !strcmp(E.r->musica, RecTemaMusica(antes)))
        snprintf(E.r->musica, sizeof E.r->musica, "%s", RecTemaMusica(id));
      Msg(false, id ? "TEMA: %s  MUSICA: %s" : "SIN TEMA", E.r->tema, E.r->musica);
    }
  } else if (key == 'm') {
    /* music: the song of another level (RecMusicaNombre), or the level's own */
    Guardar_undo();
    const char *n = NULL;
    for (int k = RecMusicaId(E.r->musica) + 1; k <= 0x20 && !n; k++)
      n = RecMusicaNombre(k);
    snprintf(E.r->musica, sizeof E.r->musica, "%s", n ? n : "");
    Msg(false, n ? "MUSICA: %s" : "MUSICA DEL NIVEL", E.r->musica);
  } else if (key == 'p' || key == kEdKeyF5) {
    Probar();
  } else if (key == kEdKeyEscape) {
    if (E.plantilla >= 0)
      E.plantilla = -1;
    else if (E.sucia)
      E.ov = kOvSalir;
    else
      E.modo = kModoLista;
  } else if (key == kEdKeyLeft || key == kEdKeyRight) {
    const int d = key == kEdKeyLeft ? -1 : 1;
    if (alt)
      Mover(E.sel, E.sel + d);
    else if (E.sel + d >= 0 && E.sel + d < E.r->npiezas) {
      E.sel += d;
      IrAPieza(E.sel);
    }
  } else if (key == kEdKeyHome) {
    E.sel = 0;
    IrAPieza(0);
  } else if (key == kEdKeyEnd) {
    E.sel = E.r->npiezas - 1;
    IrAPieza(E.sel);
  } else if (key == kEdKeyUp || key == kEdKeyDown) {
    E.vy_manual = true;
    E.vy += (key == kEdKeyUp ? -24 : 24);
    if (E.vy < 0) E.vy = 0;
    if (E.vy > kLevelH - (int)(kTrackH / E.zoom)) E.vy = kLevelH - (int)(kTrackH / E.zoom);
  } else if (key == kEdKeyPageUp || key == kEdKeyPageDown) {
    E.x0 += (key == kEdKeyPageUp ? -1 : 1) * (int)(W / E.zoom) * 3 / 4;
  } else if (key == kEdKeyDelete || key == kEdKeyBackspace) {
    Borrar(E.sel);
  } else if (key == '+' || key == '=') {
    AjustarLargo(+1, shift);
  } else if (key == '-') {
    AjustarLargo(-1, shift);
  } else if (key == 'z') {
    Zoom(0.5, W / 2);
  } else if (key == 'x') {
    Zoom(2, W / 2);
  }
}

void Cart2pEditorTexto(const char *utf8) {
  if (!E.abierto || !utf8)
    return;
  char *dest = NULL;
  size_t cap = 0;
  if (E.modo == kModoLista) {
    dest = E.filtro;
    cap = sizeof E.filtro;
  } else if (E.ov == kOvNombre) {
    dest = E.nombre_edit;
    cap = 29;
  } else {
    return;
  }
  for (const char *c = utf8; *c; c++) {
    const unsigned char ch = (unsigned char)*c;
    if (ch >= 128 || !(isalnum(ch) || ch == ' ' || ch == '-' || ch == '.' || ch == '!'))
      continue;
    const size_t l = strlen(dest);
    if (l + 1 < cap) {
      dest[l] = (char)toupper(ch);
      dest[l + 1] = 0;
    }
  }
}

static int ObjetoBajo(int sx, int sy) {
  const int rad = E.zoom >= 0.75 ? 7 : 5;
  int best = -1, bd = 1 << 30;
  for (int i = 0; i < s_nobjs; i++) {
    const int dx = Sx(s_objs[i].x) - sx, dy = Sy(s_objs[i].y) - sy;
    const int d = dx * dx + dy * dy;
    if (d <= (rad + 2) * (rad + 2) && d < bd) {
      bd = d;
      best = i;
    }
  }
  return best;
}

void Cart2pEditorMouse(int kind, int x, int y, int button, int wheel, int mods) {
  if (!E.abierto)
    return;
  E.mx = x;
  E.my = y;
  if (E.modo == kModoLista) {
    int idx[402];
    const int n = FiltrarLista(idx);
    if (kind == kEdMouseWheel) {
      E.lista_sel -= wheel * 3;
      if (E.lista_sel < 0) E.lista_sel = 0;
      if (E.lista_sel > n - 1) E.lista_sel = n - 1;
    } else if (kind == kEdMouseDown && button == 1 && y >= 18 && y < 20 + 17 * 10) {
      const int k = E.lista_top + (y - 18) / 10;
      if (k < n) {
        if (k == E.lista_sel)
          TeclaLista(kEdKeyEnter, 0);
        else
          E.lista_sel = k;
      }
    }
    return;
  }
  if (E.ov == kOvAyuda) {
    if (kind == kEdMouseDown)
      E.ov = kOvNada;
    return;
  }
  if (E.ov == kOvNombre || E.ov == kOvSalir)
    return;
  Disponer();
  s_nobjs = RecObjetos(E.r, E.nv, s_objs, 200, &s_salida_x);
  const bool en_panel = (E.ov == kOvPaleta || E.ov == kOvObjetos) && x >= kPanelX &&
                        y >= kTrackY && y < kStripY;
  if (en_panel) {
    if (E.ov == kOvPaleta) {
      ArmarOpciones();
      if (kind == kEdMouseWheel) {
        E.pal_sel -= wheel;
        if (E.pal_sel < 0) E.pal_sel = 0;
        if (E.pal_sel >= s_nops) E.pal_sel = s_nops - 1;
      } else if (kind == kEdMouseDown && button == 1) {
        if (y < kTrackY + 12) {
          const int t = (x - kPanelX - 2) / 48;
          if (t >= 0 && t < 4) {
            E.pal_tab = t;
            E.pal_sel = E.pal_top = 0;
          }
        } else if (y < kTrackY + 22) {
          E.solo_calzan = !E.solo_calzan;
        } else {
          const int k = E.pal_top + (y - kTrackY - 23) / kRowH;
          if (k >= 0 && k < s_nops) {
            E.pal_sel = k;
            Insertar(&s_ops[k].p);
          }
        }
      }
    } else if (kind == kEdMouseDown && button == 1) {
      const int k = (y - kTrackY - 24) / 13;
      if (k >= 0 && k < E.nplant) {
        E.plantilla = E.plantilla == k ? -1 : k;
        if (E.plantilla >= 0)
          Msg(false, "CLIC EN LA PISTA PARA PONER %s", E.plant_nombre[k]);
      }
    }
    return;
  }
  const int lx = E.x0 + (int)(x / E.zoom);
  const int ly = 0x200 - (E.vy + (int)((y - kTrackY) / E.zoom));
  const bool en_pista = y >= kTrackY + 9 && y < kTrackY + kTrackH;
  switch (kind) {
  case kEdMouseWheel:
    if (mods & kEdModCtrl)
      Zoom(wheel > 0 ? 2 : 0.5, x);
    else if (mods & kEdModShift) {
      E.vy_manual = true;
      E.vy -= wheel * 24;
      if (E.vy < 0) E.vy = 0;
      if (E.vy > kLevelH - (int)(kTrackH / E.zoom)) E.vy = kLevelH - (int)(kTrackH / E.zoom);
    } else
      E.x0 -= (int)(wheel * 96 / E.zoom);
    break;
  case kEdMouseDown:
    if (button == 3 && en_pista) {
      const int o = ObjetoBajo(x, y);
      if (o >= 0 && s_objs[o].agregado >= 0) {
        BorrarAgregado(s_objs[o].agregado);
      } else {
        E.pan = true;
        E.pan_x = x;
        E.pan_y = y;
        E.pan_x0 = E.x0;
        E.pan_vy = E.vy;
      }
    } else if (button == 1 && y >= kMiniY && y < kMiniY + kMiniH) {
      E.x0 = x * s_total * kCol / W - (int)(W / E.zoom) / 2;
    } else if (button == 1 && y >= kStripY && y < kStripY + kStripH) {
      const int k = StripScroll() + x / (kCardW + 2);
      if (k < E.r->npiezas) {
        E.sel = k;
        IrAPieza(k);
        if (k > 0 && k < E.r->npiezas - 1) {
          E.arrastre_carta = true;
          E.carta_desde = k;
        }
      }
    } else if (button == 1 && en_pista) {
      if (E.plantilla >= 0) {
        AgregarObjeto(lx, ly);
        break;
      }
      const int o = ObjetoBajo(x, y);
      if (o >= 0 && s_objs[o].agregado >= 0) {
        E.arrastre_obj = true;
        E.arrastre_n = s_objs[o].agregado;
        free(E.arrastre_antes);
        E.arrastre_antes = malloc(sizeof(Receta));
        if (E.arrastre_antes)
          *E.arrastre_antes = *E.r;
      } else if (o >= 0) {
        AlternarQuitado(s_objs[o].pieza, s_objs[o].indice);
      } else {
        E.sel = PiezaEnX(lx);
      }
    }
    break;
  case kEdMouseMove:
    if (E.pan) {
      E.x0 = E.pan_x0 - (int)((x - E.pan_x) / E.zoom);
      E.vy_manual = true;
      E.vy = E.pan_vy - (int)((y - E.pan_y) / E.zoom);
      if (E.vy < 0) E.vy = 0;
      if (E.vy > kLevelH - (int)(kTrackH / E.zoom)) E.vy = kLevelH - (int)(kTrackH / E.zoom);
    }
    if (E.arrastre_obj && E.arrastre_n < E.r->noa) {
      RecAgregar *a = &E.r->oa[E.arrastre_n];
      const int p = PiezaEnX(lx);
      a->pieza = p;
      a->dx = lx - Inicio(p) * kCol;
      a->y = ly < 0x10 ? 0x10 : ly > 0x1F0 ? 0x1F0 : ly;
    }
    break;
  case kEdMouseUp:
    E.pan = false;
    if (E.arrastre_obj) {
      E.arrastre_obj = false;
      if (E.arrastre_antes && E.nundo < kUndo) { /* one undo step for the drag */
        E.undo[E.nundo++] = E.arrastre_antes;
        E.arrastre_antes = NULL;
        E.sucia = true;
      }
    }
    if (E.arrastre_carta) {
      E.arrastre_carta = false;
      if (y >= kStripY && y < kStripY + kStripH) {
        int k = StripScroll() + x / (kCardW + 2);
        if (k > E.carta_desde)
          k--;
        if (k < 1) k = 1;
        if (k > E.r->npiezas - 2) k = E.r->npiezas - 2;
        if (k != E.carta_desde)
          Mover(E.carta_desde, k);
      }
    }
    break;
  default:
    break;
  }
}

bool Cart2pEditorAbiertoEn(void) {
  return E.abierto;
}
