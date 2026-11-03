#ifndef DKC1_CART2P_H
#define DKC1_CART2P_H

/* Cart Chase 2P: two simultaneous players in the minecart stages.
 *
 * Default OFF. Enabled only with the environment variable DKC1_CART2P=2.
 * While disabled every entry point returns immediately without touching
 * the machine, so the stock game stays byte-identical.
 *
 * Two-game mode: two complete, independent copies of the game in one
 * process. Each host frame runs game A with pad 1 and game B with pad 2
 * (as B's pad 1), swapping the whole machine state through in-memory
 * snapshots. A draws the left half, B the right half (cart2p_dual.c).
 * The old single-game mode (DKC1_CART2P=1) was retired on 2026-10-07. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool Cart2pDual(void);

/* Host frame entry: returns false when the two-game mode is off (the caller
 * then runs RtlRunFrame as usual). Leaves game A loaded, B already drawn in
 * the right half of the host buffer. */
bool Cart2pDualRunFrame(uint32_t inputs);

/* End of Dkc1DrawPpuFrame: race HUD on the half just drawn (no-op unless
 * DKC1_CART2P=2). */
void Cart2pDualDecorate(void);

/* Message shown ~3 s on both halves (uppercase A-Z, digits, : . ! -). */
void Cart2pDualNotice(const char *text);

/* Welcome + controller screens (desktop host only). The host requests them
 * once at startup; while Cart2pDualBinding() is true it moves controllers
 * between the two sides (side 0 = P1 left, 1 = P2 right) and reports each
 * side with Cart2pDualBindSlot, plus the count of extra controllers. */
void Cart2pDualRequestBinding(void);
bool Cart2pDualBinding(void);
void Cart2pDualBindSlot(int side, bool present, bool ready, const char *name);
void Cart2pDualBindSpare(int spare);

/* ---- In-game stage editor (cart2p_editor.c) ---------------------------
 * Keys: printable ASCII (lowercase) or the codes below; mods are bits. */
enum {
  kEdKeyLeft = 0x100, kEdKeyRight, kEdKeyUp, kEdKeyDown, kEdKeyDelete,
  kEdKeyBackspace, kEdKeyEnter, kEdKeyEscape, kEdKeyTab, kEdKeyHome,
  kEdKeyEnd, kEdKeyPageUp, kEdKeyPageDown, kEdKeyF1, kEdKeyF2, kEdKeyF5
};
enum { kEdModCtrl = 1, kEdModShift = 2, kEdModAlt = 4 };
enum { kEdMouseMove, kEdMouseDown, kEdMouseUp, kEdMouseWheel };

void Cart2pEditorAbrir(void);
/* 0 keep editing, 1 leave the editor, 2 play stage `id` now. */
int Cart2pEditorCuadro(char *id, size_t size);
void Cart2pEditorDibujar(uint8_t *buffer, size_t pitch, int width);
void Cart2pEditorVolverDePrueba(void);
void Cart2pEditorTecla(int key, int mods);
void Cart2pEditorTexto(const char *utf8);
/* x, y in frame pixels; button 1 left, 3 right; wheel +up/-down. */
void Cart2pEditorMouse(int kind, int x, int y, int button, int wheel, int mods);

/* Host -> race: keyboard, text and mouse while the editor (or a test of
 * the editor) runs. Cart2pDualKey returns true when it used the key. */
bool Cart2pDualKey(int key, int mods);
void Cart2pDualText(const char *utf8);
void Cart2pDualMouse(int kind, int x, int y, int button, int wheel, int mods);
bool Cart2pDualWantsMouse(void);
/* Race -> editor: load the start state of a level (its graphics for the
 * editor), read the stage list again, the HUD font. */
bool Cart2pDualCargarNivel(const char *nivel);
void Cart2pDualRecargarLista(void);
const uint8_t *Cart2pGlyph(char ch);

/* "Salir" in the game's main menu: the host ends the session. */
bool Cart2pDualWantsQuit(void);
/* 1 JUGADOR active: one game, full width (no split screen). */
bool Cart2pDualSolo(void);

/* Hidden helper process (Windows: no console window), used by the main
 * menu to run the stage randomizer. Poll: -1 while running, then the exit
 * code (once). */
bool Cart2pSpawnHidden(const char *cmdline);
int Cart2pSpawnPoll(void);

/* "--cart2p" launch (shortcut "Cart Chase 2P"): sets the DKC1_CART2P_*
 * variables from the workspace that contains the executable, so the game
 * opens in the race mode without a .bat. Call before anything else. */
void Cart2pAutoConfig(void);

/* Custom stage (DKC1_CART2P_STAGE=file, cart2p_stage.c): installed into
 * the loaded game at the race start; true if installed. */
bool Cart2pStageInstall(void);
bool Cart2pStageInstallFile(const char *path);
/* Undo every ROM image patch of the last install (original stage). */
void Cart2pStageRestoreRom(void);
/* ROM image without stage patches (original levels); NULL before the game
 * is loaded. */
const uint8_t *Cart2pStagePristineRom(size_t *size);
/* X of the level exit trigger (object type 2) of the loaded level, read from
 * the object list in the ROM image (after a stage install, the new list);
 * 0 if unknown. */
uint16_t Cart2pStageExitX(void);
/* Rail survey of the loaded level (DKC1_CART2P_SURVEY=file) and graphics
 * export for the stage editor (DKC1_CART2P_EXPORT=file). */
void Cart2pStageExportGraphics(void);
/* Theme skin over an installed stage (.c2t, tools/cart2p/temas.py). */
bool Cart2pStageInstallSkin(const char *path);
/* Development: graphics of another level (DKC1_CART2P_TEMAS). */
bool Cart2pStageExportTheme(uint16_t entrance, const char *path);
void Cart2pStageSurvey(void);

/* stderr + optional file DKC1_CART2P_LOGFILE (only when enabled). */
void Cart2pLog(const char *fmt, ...);

/* Vertical split screen (512x224): left = P1, right = P2. Active whenever
 * the two-game mode is on. */
bool Cart2pSplitScreen(void);

/* Desktop host: when 2P is on and player 2 has no input source, enable
 * keyboard + second gamepad for P2 (in memory only, never saved).
 * P2 keys: I J K L move, N = B (jump), M = Y, H = A, U = X, Y = L, O = R,
 * P = Start, right Ctrl = Select. */
struct Dkc1Controls;
void Cart2pDefaultP2Controls(struct Dkc1Controls *controls);

#endif
