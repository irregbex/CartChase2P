/* Cart Chase 2P - mode flag, log and host controls.
 *
 * The race itself lives in cart2p_dual.c (two complete games, DKC1_CART2P=2)
 * and the custom stages in cart2p_stage.c. The old single-game mode
 * (DKC1_CART2P=1: forced player context, cloned cart, right view rendered
 * from the same PPU) was retired on 2026-10-07; its code is kept in
 * cart2p/archivo/2026-10-07_modo1_retirado/. */
#include "cart2p.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desktop_input.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static int s_mode = -1; /* 0 off, 2 two games */
static FILE *s_log_file;

void Cart2pLog(const char *fmt, ...) {
  char line[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof line, fmt, args);
  va_end(args);
  fprintf(stderr, "[cart2p] %s\n", line);
  if (s_log_file) {
    fprintf(s_log_file, "%s\n", line);
    fflush(s_log_file);
  }
}

static void Cart2pInitMode(void) {
  if (s_mode >= 0)
    return;
  const char *v = getenv("DKC1_CART2P");
  s_mode = v && strcmp(v, "2") == 0 ? 2 : 0;
  const char *path = getenv("DKC1_CART2P_LOGFILE");
  if (s_mode && path && *path)
    s_log_file = fopen(path, "w");
  if (s_mode == 2)
    Cart2pLog("modo 2P de dos partidas activo (DKC1_CART2P=2)");
  else if (v && strcmp(v, "1") == 0)
    fprintf(stderr, "[cart2p] DKC1_CART2P=1 (modo 1) fue retirado el "
                    "2026-10-07; usar DKC1_CART2P=2. Se juega el original.\n");
}

bool Cart2pDual(void) {
  Cart2pInitMode();
  return s_mode == 2;
}

bool Cart2pSplitScreen(void) {
  return Cart2pDual() && !Cart2pDualSolo();
}

void Cart2pDefaultP2Controls(struct Dkc1Controls *controls) {
  if (!Cart2pSplitScreen() || !controls)
    return;
  /* P1 must accept a gamepad too (a keyboard-only P1 left pad 1 to P2). */
  if (controls->source[0] != kDkc1InputSourceBoth) {
    controls->source[0] = kDkc1InputSourceBoth;
    Cart2pLog("P1: teclado + primer mando");
  }
  if (controls->source[1] != kDkc1InputSourceNone)
    return;
  /* SDL scancodes, logical order: Up Down Left Right A B X Y L R Start
   * Select. P1 keeps arrows + Z/X/A/S/Q/W, Enter, right Shift. */
  static const int kP2Keys[12] = {12, 14, 13, 15, 11, 17, 24, 16,
                                  28, 18, 19, 228};
  controls->source[1] = kDkc1InputSourceBoth;
  bool empty = true;
  for (int i = 0; i < 12; i++)
    if (controls->keys[1][i] > 0)
      empty = false;
  if (empty)
    memcpy(controls->keys[1], kP2Keys, sizeof kP2Keys);
  Cart2pLog("P2: segundo mando + teclado (IJKL mover, N saltar, M correr)");
}

/* ---- Hidden helper process ------------------------------------------------- */

#ifdef _WIN32
static PROCESS_INFORMATION s_spawn;
static bool s_spawn_active;

bool Cart2pSpawnHidden(const char *cmdline) {
  if (s_spawn_active)
    return false;
  char cmd[2048];
  snprintf(cmd, sizeof cmd, "%s", cmdline);
  STARTUPINFOA si;
  ZeroMemory(&si, sizeof si);
  si.cb = sizeof si;
  ZeroMemory(&s_spawn, sizeof s_spawn);
  if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL,
                      NULL, &si, &s_spawn)) {
    Cart2pLog("no se pudo ejecutar: %s (error %lu)", cmdline,
              (unsigned long)GetLastError());
    return false;
  }
  s_spawn_active = true;
  return true;
}

int Cart2pSpawnPoll(void) {
  if (!s_spawn_active)
    return 0;
  if (WaitForSingleObject(s_spawn.hProcess, 0) == WAIT_TIMEOUT)
    return -1;
  DWORD code = 1;
  GetExitCodeProcess(s_spawn.hProcess, &code);
  CloseHandle(s_spawn.hProcess);
  CloseHandle(s_spawn.hThread);
  s_spawn_active = false;
  return (int)code;
}

/* ---- "--cart2p" launch ----------------------------------------------------- */

static void SetIfMissing(const char *name, const char *value) {
  if (!getenv(name))
    _putenv_s(name, value);
}

/* Lo que sale de la ROM (estados, perfiles, pieles, etapas) no viene en el
 * proyecto: si falta, tools\cart2p\preparar_desde_rom.py lo crea una vez
 * desde la ROM del usuario (consola visible mientras trabaja). */
static void PrepareFromRom(const char *root) {
  char state[MAX_PATH + 64];
  snprintf(state, sizeof state, "%s\\cart2p\\states\\carnage_montado.state", root);
  if (GetFileAttributesA(state) != INVALID_FILE_ATTRIBUTES)
    return;
  char cmd[2 * MAX_PATH + 64];
  snprintf(cmd, sizeof cmd, "python \"%s\\tools\\cart2p\\preparar_desde_rom.py\"", root);
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof si);
  si.cb = sizeof si;
  DWORD code = 1;
  if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, root,
                     &si, &pi)) {
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  if (code != 0)
    MessageBoxA(NULL,
                "Falta preparar Cart Chase 2P desde la ROM / Cart Chase 2P must be "
                "prepared from your ROM.\n\n"
                "1. Copiar / copy Donkey Kong Country (USA) v1.0 to rom\\dkc1.sfc\n"
                "2. cart2p\\JUGAR.bat preparar",
                "Cart Chase 2P", MB_OK | MB_ICONWARNING);
}

void Cart2pAutoConfig(void) {
  char exe[MAX_PATH];
  if (!GetModuleFileNameA(NULL, exe, sizeof exe))
    return;
  /* build-windows\release\DKC1Recomp.exe -> raiz del workspace */
  char root[MAX_PATH];
  snprintf(root, sizeof root, "%s\\..\\..\\..", exe);
  char full[MAX_PATH];
  if (!_fullpath(full, root, sizeof full))
    return;
  char path[MAX_PATH + 64];
  snprintf(path, sizeof path, "%s\\cart2p\\etapas.txt", full);
  if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
    return; /* no es el workspace de Cart Chase 2P: juego original */
  PrepareFromRom(full);
  char v[MAX_PATH + 96];
  SetIfMissing("DKC1_CART2P", "2");
  SetIfMissing("DKC1_CART2P_LISTA", path);
#define SET_PATH(name, rel)                                   \
  snprintf(v, sizeof v, "%s\\%s", full, rel);                 \
  SetIfMissing(name, v)
  SET_PATH("DKC1_CART2P_RECORDS", "cart2p\\records.txt");
  SET_PATH("DKC1_CART2P_MANDOS", "cart2p\\mandos.txt");
  SET_PATH("DKC1_CART2P_OPCIONES", "cart2p\\opciones.txt");
  SET_PATH("DKC1_CART2P_LOGFILE", "cart2p\\logs\\ultima_partida.txt");
  SET_PATH("DKC1_INPUT_RECORD", "cart2p\\logs\\ultima_partida_entradas.txt");
  SET_PATH("DKC1_SAVESTATE_INPUT", "cart2p\\states\\carnage_montado.state");
  SET_PATH("DKC1_ROM", "rom\\dkc1.sfc");
#undef SET_PATH
  SetIfMissing("DKC1_WIDESCREEN", "0");
  snprintf(v, sizeof v, "%s\\cart2p\\logs", full);
  CreateDirectoryA(v, NULL);
}
#else
bool Cart2pSpawnHidden(const char *cmdline) {
  (void)cmdline;
  return false;
}

int Cart2pSpawnPoll(void) {
  return 0;
}

void Cart2pAutoConfig(void) {}
#endif
