#ifndef DKC1_CART2P_RECETA_H
#define DKC1_CART2P_RECETA_H

/* Cart Chase 2P - stage recipes in C (same rules as tools/cart2p/
 * etapa_lib.py; format in cart2p/docs/FORMATO_ETAPAS.txt).
 *
 * A recipe (cart2p\etapas_fuente\<id>.json) lists pieces of an original
 * cart level; it is validated, documented and compiled to the .c2p v3 the
 * game installs (cart2p_stage.c). Used by the in-game editor and the
 * in-game randomizer, so no external tool is needed. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  kRecMaxPieces = 220,
  kRecMaxFreeCols = 64,
  kRecMaxRemoved = 160,
  kRecMaxAdded = 80,
  kRecMaxHistory = 48,
  kRecMaxCols = 600,
};

typedef enum {
  kPiezaTramo,
  kPiezaRecta,
  kPiezaHoyo,
  kPiezaDoble,
  kPiezaColumnas
} PiezaTipo;

typedef struct {
  PiezaTipo tipo;
  int desde, hasta;          /* tramo */
  int columna, largo;        /* recta */
  int ncols;                 /* hoyo / doble / columnas */
  int16_t cols[kRecMaxFreeCols];
  int nhoyos;
  int hoyos_px[2];
} Pieza;

typedef struct {
  int pieza, indice;
} RecQuitar;

typedef struct {
  int pieza, dx, y;
  char nombre[24];
  uint16_t puntero;
  bool enemigo;
} RecAgregar;

typedef struct {
  char id[48];
  char nombre[32];
  char nivel[12];
  char familia[24];
  char tema[16];             /* "" or one of RecTemaNombre (Carnage only) */
  char musica[16];           /* "" or one of RecMusicaNombre: another level's song */
  char dificultad[12];
  char autor[64];
  char creada[24];
  char modificada[24];
  char descripcion[600];
  char notas[600];
  int semilla;
  bool tiene_semilla;
  char generacion[200];      /* raw JSON object, kept as is */
  Pieza piezas[kRecMaxPieces];
  int npiezas;
  RecQuitar oq[kRecMaxRemoved];
  int noq;
  RecAgregar oa[kRecMaxAdded];
  int noa;
  RecQuitar pq[kRecMaxRemoved];
  int npq;
  char historial[kRecMaxHistory][160];
  int nhist;
} Receta;

/* ---- original levels ---- */

typedef struct {
  uint8_t raw[8];
  int x, y, tipo;
  uint16_t ptr;
  int sprite;
  char nombre[20];
  bool enemigo;
  int zona_antes, zona_despues;  /* no-pit zone around it (0 = none) */
} NivObjeto;

typedef struct {
  uint8_t raw[4];
  int x, y;
} NivPlatano;

typedef struct {
  int a, b, y0, y1;  /* columns a..b-1; y0/y1 = rail at the cuts (-1) */
} NivTramo;

typedef struct {
  int y, x, w;
  int ncols;
  int16_t cols[12];
} NivBloque;

typedef struct {
  char nombre[12];
  char titulo[24];
  int entrance, cols, camera_max, max_cols;
  int map_bank, map_addr, metatile_addr, metatile_bytes;
  const uint8_t *layer1, *layer2;      /* in the pristine ROM copy */
  /* survey: up to 6 surfaces per 4 px */
  int nx;
  int16_t (*surf_y)[6];
  uint8_t (*surf_t)[6];
  uint8_t *nsurf;
  int16_t *cut_y;      /* rail at the cut before column c, -1 */
  int16_t *flat_y;     /* column flat at this height, -1 */
  int16_t *edge[2];    /* rail at the left/right edge of column c, -1 */
  NivTramo tramos[80];
  int ntramos;
  NivBloque bloques[16];
  int nbloques;
  NivObjeto objetos[96];
  int nobjetos;
  NivPlatano platanos[40];
  int nplatanos;
  uint16_t zonas[96][4];
  int nzonas;
  int exit_x;
} Nivel;

/* Loaded once per level ("carnage" / "madness"); NULL on error (message in
 * the log). Needs DKC1_CART2P_LISTA (folder cart2p\) and the ROM. */
const Nivel *RecNivel(const char *nombre);
/* Rail height at level X (-1 = none). */
int RecRailY(const Nivel *nv, int x);
const char *RecNivelEstado(const char *nombre); /* "states\\x_montado.state" */

/* ---- recipes ---- */

int RecPiezaCols(const Pieza *p, int16_t *out, int max); /* source columns */
int RecTotalCols(const Receta *r);
/* Problems (0 = valid). errors: up to max lines. */
int RecValidar(const Receta *r, const Nivel *nv, char errors[][160], int max);
/* .c2p bytes (malloc'd) and size; NULL if it does not fit. */
uint8_t *RecCompilar(const Receta *r, const Nivel *nv, size_t *size);
/* Text of one piece (the "nota" of the JSON). */
void RecDescribir(const Receta *r, const Nivel *nv, int pieza, char *out, size_t size);

typedef struct {
  int columnas, piezas, tramos, inventadas, hoyos, enemigos, platanos;
  int cortes; /* joints with a visible seam */
  double segundos, vs_original;
  int nobj;
  char obj_nombre[24][20];
  int obj_cuenta[24];
} RecStats;
void RecEstadisticas(const Receta *r, const Nivel *nv, RecStats *st);

/* Object list of the stage (for the editor): level objects of each tramo
 * (first copy) incl. removed ones, plus added ones, sorted by X. */
typedef struct {
  int x, y, pieza;
  int indice;      /* level object index, or -1 */
  int agregado;    /* index in oa, or -1 */
  bool quitado, enemigo;
  const char *nombre;
  uint16_t ptr;
  uint8_t raw[8];
} RecObj;
int RecObjetos(const Receta *r, const Nivel *nv, RecObj *out, int max,
               int *salida_x);

/* Joint after piece i leaves a visible seam (the original column that
 * would follow is not identical to the next one). */
bool RecCorteVisible(const Receta *r, const Nivel *nv, int i);
/* Seamless jumps: columns identical to column c elsewhere (out, max). */
int RecGemelas(const Nivel *nv, int c, int *out, int max);

/* Themes (tools/cart2p/temas.py): name of theme id 1..RecTemas(), or
 * NULL; id of a name (0 = none). Same list as etapa_lib.TEMAS. */
int RecTemas(void);
const char *RecTemaNombre(int id);
int RecTemaId(const char *nombre);
/* Music name of the level a theme comes from ("" for no theme). */
const char *RecTemaMusica(int id);
/* Songs of other levels: name of id (song + 1, as in the .c2p header high
 * byte) or NULL; id of a name (0 = the level's own). Same list as
 * etapa_lib.MUSICAS. */
int RecMusicas(void);
const char *RecMusicaNombre(int id);
int RecMusicaId(const char *nombre);

/* ---- files ---- */

bool RecCargar(const char *id, Receta *r);           /* etapas_fuente\id.json */
bool RecGuardar(Receta *r, const char *cambio);      /* backup + json + .c2p + list */
bool RecExiste(const char *id);
void RecDesdeNivel(Receta *r, const Nivel *nv, const char *id, const char *nombre);
/* Directory of cart2p\ (with trailing separator), "" if unknown. */
const char *RecDirCart2p(void);

/* Ids of all recipes (sorted); returns count. */
int RecListar(char ids[][48], int max);

/* ---- randomizer ---- */

/* Random stage: level, seed and invented pieces chosen by the caller.
 * Fills r (without id/name). false if nothing fits. */
bool RecGenerar(const Nivel *nv, uint32_t seed, int cols, int inventar,
                bool hoyos, Receta *r);
/* Seamless stage: a walk through the level jumping between identical
 * columns (no invented pieces). */
bool RecGenerarSinCostura(const Nivel *nv, uint32_t seed, int cols, int saltos_min,
                          Receta *r);
/* n new random stages saved and added to the menu list; returns how many. */
int RecAzar(int n, uint32_t seed);

#endif
