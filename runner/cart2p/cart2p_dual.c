/* Cart Chase 2P - two-game race (DKC1_CART2P=2).
 *
 * Instead of forcing a second player into one game (context swaps, cloned
 * cart, P2 passes), the process keeps two complete, independent copies of
 * the game: A (pad 1, left half) and B (pad 2, right half). Each host frame:
 *
 *   run A with pad 1 -> race rules for A -> save A -> load B -> run B with
 *   pad 2 -> race rules for B -> draw B into the right half -> save B ->
 *   load A
 *
 * and the host then draws A into the left half as usual. Each copy is the
 * stock game: HUD, bananas, letters, enemies, sounds, camera and sprite VRAM
 * all work natively for both players. B starts as an exact copy of A.
 *
 * Race (this file):
 *  - Countdown 3-2-1 with both games frozen, then "YA!".
 *  - Death never costs a life nor leaves the stage: the player's own game is
 *    rewound to one of its checkpoints (full snapshots taken every 30 frames
 *    while riding on the rail), ~1.5 s back, further back if the player
 *    keeps dying at the same place. Losing the buddy Kong is stock.
 *  - Finish: the active Kong crosses the level's exit trigger X (object
 *    type 2 of its object list; in a cart stage the stock game then removes
 *    the Kongs at once, no $1B/$1D state). That game freezes with its time;
 *    the first one wins; banner; Start restarts or loads the next stage of
 *    the list (DKC1_CART2P_LISTA). Best times in DKC1_CART2P_RECORDS.
 *  - HUD per half: race clock, best time, position (1RO/2DO by X).
 *  - Audio of both games mixed (see "Audio of B" below).
 *  - Test bot (DKC1_CART2P_BOT=1, headless tests only). */
#include "cart2p.h"
#include "cart2p_receta.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "dkc1_game.h"
#include "snes/apu.h"
#include "snes/dsp.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/saveload.h"
#include "snes/snes.h"

extern Snes *g_snes;

enum {
  kViewWidth = 256,
  kViewHeight = 224,
  kBytesPerPixel = 4,
  kStateCapacity = 1 << 20,     /* a snapshot is ~300 KB */
  kCheckpoints = 12,            /* x 30 frames = 6 s of history */
  kCheckpointEvery = 30,
  kRewindFrames = 90,           /* first death: ~1.5 s back */
  kStreakExtra = 60,            /* each repeated death: 1 s further */
  kStreakMax = 4,               /* at most 90 + 4 * 60 frames (~5 s) back */
  kMilestones = 8,              /* long-term history: 8 x 5 s = 40 s */
  kMilestoneEvery = 300,
  kStreakWindow = 150,          /* dying < 2.5 s after a respawn: the
                                   checkpoint may be doomed, go further */
  kCountdownFrames = 180,       /* 3, 2, 1 */
  kGoFrames = 60,               /* "YA!" */
  kBannerFrames = 420,          /* winner banner, then restart */
  kSnapshotMagic = 0x52544c53,  /* "RTLS", as common_rtl.c */
  kSnapshotVersion = 9,         /* RTL_SAV_VERSION of the pinned engine */
  kApuCyclesPerFrame = 17088,   /* RTL_APU_CYCLES_PER_FRAME (common_rtl.c) */
  kApuKeepCapacity = 160 << 10, /* apu_saveload: 64 KB SPC RAM + DSP + SPC */
  kWramPlayerKongIndex = 0x0084,
  kWramSpriteIdBase = 0x0D45,
  kWramRider = 0x1375,
  kWramSecondRider = 0x13E9,
  kStateFollowerInCart = 0x000C,
  kWramEntrance = 0x003E,
  kWramCurrentKong = 0x056F,
  kWramState = 0x1029,
  kWramSpriteId = 0x0D45,
  kNoKongFrames = 20,           /* no Kong actors this long = left the stage
                                   (fallback; the exit X is the real goal) */
  kWramXPos = 0x0B19,
  kWramYPos = 0x0BC1,
  kWramCartSlot = 0x1E0F,
  kWramHeight = 0x1631,
  kWramCameraX = 0x088B,
  kWramCameraY = 0x0895,
  kWramSong = 0x0523,           /* song playing (CODE_B990CE) */
  kWramSongOn = 0x051D,         /* 1 while a song plays; 0 after a stop */
  kStateRidingCart = 0x002E,
  kStateExit = 0x001B,
  kStateExit2 = 0x001D,
  kPadB = 0x0001,               /* jump, in the 12-bit pad mask */
  kPadSelect = 0x0004,
  kPadStart = 0x0008,
  kPadA = 0x0100,               /* SNES A = controller B: "back" in menus */
  kPadUp = 0x0010,
  kPadDown = 0x0020,
  kPadLeft = 0x0040,
  kPadRight = 0x0080,
  kPadL = 0x0400,               /* LB (desktop_input.c bit 10) */
  kPadR = 0x0800,               /* RB (bit 11) */
  kSelectPage = 16,             /* stage menu rows per page */
  kMaxStages = 400,
  kMaxRecords = 512,
  kFadeOutFrames = 12,          /* death: that player's half goes dark... */
  kFadeInFrames = 20,           /* ...rewinds, then lights up again */
};

/* Chronological ring of full snapshots of one player's game. */
typedef struct {
  uint8_t *state[kCheckpoints];
  size_t size[kCheckpoints];
  uint32_t frame[kCheckpoints];
  int capacity, head, count;
} Ring;

typedef struct {
  Ring near;                    /* every 30 frames, last 6 s */
  Ring far;                     /* every 300 frames, last 40 s */
  uint32_t frame;               /* race frames of this player's game */
  uint32_t wall;                /* frames played, never rewound */
  uint32_t respawn_wall;        /* wall frame of the last rewind */
  int streak, deaths;
  uint16_t x;                   /* active Kong X after its last frame */
  bool finished;
  uint32_t finish_time;         /* race clock frames */
  uint32_t no_kong;             /* consecutive frames without Kong actors */
  uint16_t last_state;
  uint32_t prev_pad;            /* for press (edge) detection */
  bool new_record;              /* this finish beat the stage's best time */
  int fade_out, fade_in;        /* death transition (frames left) */
  /* test bot: random jumps + a random distance before each place it died */
  uint32_t rng;
  int bot_hold;
  int bot_spots;
  uint16_t bot_x[64];
  uint16_t bot_offset[64];
  uint16_t max_x;
} DualPlayer;

typedef enum {
  kRaceTitle,
  kRaceMain,
  kRaceEditor,
  kRaceBind,
  kRaceSelect,
  kRaceCountdown,
  kRaceRunning,
  kRaceDone
} RacePhase;

/* Welcome screen (kRaceTitle), then the controller screen (kRaceBind):
 * each player moves his controller to his side (left = P1, right = P2) and
 * confirms with A. The host does the controller work (Cart2pBindTick in
 * sdl_host.c) and reports each side with Cart2pDualBindSlot. */
static bool s_bind_requested;
static bool s_bind_ready[2], s_bind_pad[2];
static char s_bind_name[2][40];
static int s_bind_spare;             /* connected pads beyond the two sides */
static uint32_t s_bind_all_frames;   /* both ready this long -> menu */
enum { kBindReadyFrames = 45 };

static uint8_t *s_state_a, *s_state_b, *s_start;
static size_t s_size_b, s_size_start;
static bool s_has_b;
static bool s_failed;
static DualPlayer s_players[2];
static int s_bot = -1, s_verbose = -1;
static uint32_t s_test_finish;   /* DKC1_CART2P_TEST_FINISH: P2 "arrives" */
static int s_test_nocount;       /* DKC1_CART2P_TEST_NOCOUNT: skip countdown */
static RacePhase s_phase = kRaceCountdown;
static int s_sudden_death = -1;  /* DKC1_CART2P_MUERTE=1: first death loses */
static uint32_t s_phase_frames;  /* frames in the current phase */
static uint32_t s_clock;         /* race clock (frames since "YA!") */
static int s_winner = -1;
static int s_races;
static int s_drawing_half;       /* 0 = A (left), 1 = B (right) */
static uint16_t s_exit_x;        /* X of the level exit trigger (0 = unknown) */

/* ---- Stages and records ------------------------------------------------
 * DKC1_CART2P_LISTA=file: one stage per line, "nombre | estado | etapa"
 * (paths relative to the file's folder; etapa optional). The race starts
 * with the first one; at the end of a race Left/Right pick the next stage
 * and Start goes on (the same stage again without a list).
 * DKC1_CART2P_RECORDS=file: best time per stage, "clave frames jugador
 * fecha" per line; the key is the state file name + '+' + stage file name. */
typedef struct {
  char name[32];
  char state[512];
  char stage[512];
} StageEntry;

typedef struct {
  char key[160];
  uint32_t frames;
  int player;
  char date[32];
} Record;

static StageEntry s_list[kMaxStages];
static int s_list_count;

/* ---- Main menu (kRaceMain) ---------------------------------------------
 * After the welcome screen. Left half: the options; right half: what the
 * highlighted one does (and its value). Settings are saved in
 * DKC1_CART2P_OPCIONES (cart2p\opciones.txt). "Etapas al azar" runs the
 * stage randomizer hidden (Cart2pSpawnHidden) and reloads the stage list. */
enum {
  kMainJugar,
  kMainSolo,
  kMainEditor,
  kMainAzar,
  kMainVolumen1,
  kMainVolumen2,
  kMainMuerte,
  kMainSalir,
  kMainCount
};
static int s_main_cursor;
static bool s_guion_confirma; /* test script: confirm the main menu option */
static int s_azar_cantidad = 6;
static bool s_azar_corriendo;
static int s_azar_antes;            /* list size before the randomizer */
static bool s_quit;
/* 1 JUGADOR: only game A runs, full screen (256 wide: Cart2pSplitScreen is
 * false), any controller, its own records ("1J+" key prefix). Set when the
 * stage list is entered from "1 JUGADOR", cleared back in the main menu.
 * The host changes the frame width between frames (sdl_host.c). */
static bool s_solo;
static bool s_skip_b_draw;           /* width changes this frame: no B draw */
/* Editor (kRaceEditor): the stage loaded before it, restored at the exit;
 * "probar" plays a stage in 1 JUGADOR and comes back (s_test_editor). */
static int s_editor_prev_stage;
static bool s_test_editor;
static bool SwitchStage(int index);
static bool RestartRace(void);
static void SetSolo(bool solo);
static void LoadStageList(void);
static void SaveOptions(void);
static int s_stage_index, s_next_index;
static Record s_records[kMaxRecords];
static int s_record_count;
static bool s_records_loaded;
static char s_stage_key[160];

static const uint8_t *GlyphRows(char ch);
static uint32_t BestTime(void);
static uint32_t BestTimeOfEntry(int index);
static bool SaveRecord(uint32_t frames, int player);

/* Stage menu (kRaceSelect): after the start screen and after each race
 * when there is a list; the last row picks a random stage. */
static int s_select_cursor;
static bool s_select_after_race;
static unsigned s_random_state;

/* Timing of the extra work, logged every 600 frames. */
static double s_acc_ms, s_max_ms;
static int s_acc_frames;

/* ---- Audio of B -----------------------------------------------------------
 * The host plays the DSP output ring of the loaded game, A. B's samples are
 * copied out right after its frame and mixed into A's ring over the samples
 * A produced in the same host frame. Both SPCs run exactly one frame per
 * host frame (also during the countdown, see DrawFrozenB) and B started as
 * a copy of A, so their music is sample-aligned: mixing both at 50 % leaves
 * the music as in the original and each player's own sound effects at half
 * amplitude. Gains in percent: DKC1_CART2P_MEZCLA=A,B (default 60,60).
 * DKC1_CART2P_AUDIO_DUMP=prefix writes prefix_a/_b/_m.pcm (raw, stereo
 * int16, A / B / mix) and prefix_voces.txt (per frame: samples and active
 * DSP voices of each game) for the headless tests. */
static int16_t s_b_frame[DSP_SAMPLE_RING * 2];
static uint32_t s_b_count;        /* B samples captured this host frame */
static uint32_t s_a_write0;       /* A's sampleWrite before its frame */
static int s_gain_a = -1, s_gain_b = -1;
static FILE *s_dump_a, *s_dump_b, *s_dump_m, *s_dump_v;
static uint32_t s_dump_frame;
static unsigned s_voices_b;

/* Short host message on both halves (controller swap, records). */
static char s_notice[48];
static int s_notice_frames;

static bool EnvOn(const char *name);

void Cart2pDualRequestBinding(void) {
  /* DKC1_CART2P_DIRECTO=1 (button "Probar" of the stage editor): straight
   * to the race with the remembered controllers. */
  if (Cart2pDual() && !EnvOn("DKC1_CART2P_DIRECTO"))
    s_bind_requested = true;
}

bool Cart2pDualBinding(void) {
  return Cart2pDual() && s_phase == kRaceBind;
}

void Cart2pDualBindSlot(int side, bool present, bool ready, const char *name) {
  if (side < 0 || side > 1)
    return;
  s_bind_pad[side] = present;
  s_bind_ready[side] = present && ready;
  /* uppercase, only the characters of the HUD font */
  size_t o = 0;
  for (const char *c = name ? name : ""; *c && o + 1 < sizeof s_bind_name[0];
       c++) {
    char ch = *c;
    if (ch >= 'a' && ch <= 'z')
      ch = (char)(ch - 'a' + 'A');
    if (!(ch >= 'A' && ch <= 'Z') && !(ch >= '0' && ch <= '9'))
      ch = ' ';
    if (ch == ' ' && (o == 0 || s_bind_name[side][o - 1] == ' '))
      continue;
    s_bind_name[side][o++] = ch;
  }
  while (o && s_bind_name[side][o - 1] == ' ')
    o--;
  s_bind_name[side][o] = 0;
}

void Cart2pDualBindSpare(int spare) {
  s_bind_spare = spare;
}

void Cart2pDualNotice(const char *text) {
  if (!Cart2pDual() || !text)
    return;
  snprintf(s_notice, sizeof s_notice, "%s", text);
  s_notice_frames = 180;
  Cart2pLog("aviso: %s", text);
}

static double NowMs(void) {
  return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
}

static bool EnvOn(const char *name) {
  const char *v = getenv(name);
  return v && strcmp(v, "1") == 0;
}

static uint16_t Rd16(uint32_t addr) {
  return (uint16_t)(g_ram[addr] | g_ram[addr + 1] << 8);
}

static void Wr16(uint32_t addr, uint16_t value) {
  g_ram[addr] = (uint8_t)value;
  g_ram[addr + 1] = (uint8_t)(value >> 8);
}

/* ---- Snapshots ---------------------------------------------------------------
 * The engine's RtlSaveSnapshotToMemory/RtlLoadSnapshotFromMemory serialize
 * the machine, but a load also "reconciles" the audio: it rebases the APU
 * port timeline, drops queued CPU->APU port writes and resets the host
 * resampler. Swapping two games through that path twice per frame made the
 * music stutter and lose sound commands. These are the same blobs (same
 * header, same snes_saveload + game chunk) plus the APU port timeline, and
 * the load touches nothing else, so a save/load round trip is a no-op. */
typedef struct {
  SaveLoadInfo base;
  uint8_t *data;
  size_t capacity, position;
  bool save, error;
} MemSli;

static void MemSliFunc(SaveLoadInfo *sli, void *data, size_t n) {
  MemSli *m = (MemSli *)sli;
  if (m->error || m->position + n > m->capacity) {
    m->error = true;
    return;
  }
  if (m->save)
    memcpy(m->data + m->position, data, n);
  else
    memcpy(data, m->data + m->position, n);
  m->position += n;
}

/* Port event state after Apu.pad: the engine resets it on load instead of
 * serializing it (apu.h); here it travels with the game. */
#define APU_TAIL_SIZE (sizeof(Apu) - offsetof(Apu, portQueue))

static size_t SaveState(uint8_t *buffer, size_t capacity) {
  MemSli m = {{MemSliFunc}, buffer, capacity, 0, true, false};
  uint32_t header[2] = {kSnapshotMagic, kSnapshotVersion};
  MemSliFunc(&m.base, header, sizeof header);
  RtlApuLock();
  snes_saveload_set_version(kSnapshotVersion);
  snes_saveload(g_snes, &m.base);
  if (g_rtl_game_info && g_rtl_game_info->state_save_extra)
    g_rtl_game_info->state_save_extra(&m.base);
  MemSliFunc(&m.base, &g_snes->apu->portQueue, APU_TAIL_SIZE);
  RtlApuUnlock();
  return m.error ? 0 : m.position;
}

static bool LoadState(const uint8_t *buffer, size_t size) {
  MemSli m = {{MemSliFunc}, (uint8_t *)buffer, size, 0, false, false};
  uint32_t header[2];
  MemSliFunc(&m.base, header, sizeof header);
  if (m.error || header[0] != kSnapshotMagic)
    return false;
  RtlApuLock();
  snes_saveload_set_version(header[1]);
  snes_saveload(g_snes, &m.base);
  if (g_rtl_game_info && g_rtl_game_info->state_load_extra)
    g_rtl_game_info->state_load_extra(&m.base, header[1]);
  MemSliFunc(&m.base, &g_snes->apu->portQueue, APU_TAIL_SIZE);
  RtlApuUnlock();
  if (m.error)
    return false;
  g_cpu.ram = g_ram;
  g_snes->beamMasterLast = g_cpu.master_cycles;
  interp_bridge_set_master_deadline(0);
  return true;
}

static size_t Save(uint8_t **buffer) {
  if (!*buffer)
    *buffer = (uint8_t *)malloc(kStateCapacity);
  if (!*buffer)
    return 0;
  return SaveState(*buffer, kStateCapacity);
}

/* The APU (SPC RAM, DSP ring, timers, ports) of the loaded game, so a rewind
 * of the CPU side can keep the music playing where it is. */
static uint8_t *s_apu_keep;

static size_t ApuSave(void) {
  if (!s_apu_keep)
    s_apu_keep = (uint8_t *)malloc(kApuKeepCapacity);
  if (!s_apu_keep)
    return 0;
  MemSli m = {{MemSliFunc}, s_apu_keep, kApuKeepCapacity, 0, true, false};
  RtlApuLock();
  apu_saveload(g_snes->apu, &m.base);
  MemSliFunc(&m.base, &g_snes->apu->portQueue, APU_TAIL_SIZE);
  RtlApuUnlock();
  return m.error ? 0 : m.position;
}

static void ApuRestore(size_t size) {
  MemSli m = {{MemSliFunc}, s_apu_keep, size, 0, false, false};
  Apu *apu = g_snes->apu;
  RtlApuLock();
  apu_saveload(apu, &m.base);
  MemSliFunc(&m.base, &apu->portQueue, APU_TAIL_SIZE);
  /* The CPU side went back in time, the SPC did not: map the restored
   * guest clock onto the SPC's current position... */
  apu_rebasePortTimeline(apu,
                         (uint64_t)snes_frame_counter * kApuCyclesPerFrame);
  RtlApuUnlock();
  /* ...and realign the command handshake. APU_SubmitCommand ($8A:B1AA)
   * spins until port 0 echoes the toggle kept in $00; the SPC echoes the
   * last command byte the CPU wrote (inPorts[0]), so that is what $00 must
   * hold now (a restored older toggle would never be echoed: 5 s freeze). */
  g_ram[0x0000] = apu->inPorts[0];
}

/* Load a checkpoint of the loaded game but keep its audio going. */
static bool LoadStateKeepAudio(const uint8_t *buffer, size_t size) {
  const size_t apu_size = ApuSave();
  if (!LoadState(buffer, size))
    return false;
  if (apu_size)
    ApuRestore(apu_size);
  return true;
}

/* While a game is frozen (countdown) its SPC still has to play: advance it
 * one frame exactly as the engine does at a frame boundary. */
static void AdvanceAudioOneFrame(void) {
  snes_frame_counter++;
  RtlApuLock();
  apu_runToGuestCycle(g_snes->apu,
                      (uint64_t)snes_frame_counter * kApuCyclesPerFrame,
                      1u << 20);
  RtlApuUnlock();
}

static Dsp *LoadedDsp(void) {
  return g_snes && g_snes->apu ? g_snes->apu->dsp : NULL;
}

static FILE *OpenDump(const char *prefix, const char *suffix) {
  char path[1024];
  snprintf(path, sizeof path, "%s%s", prefix, suffix);
  FILE *f = fopen(path, "wb");
  if (!f)
    Cart2pLog("audio: no se pudo crear %s", path);
  return f;
}

static void AudioInit(void) {
  if (s_gain_a >= 0)
    return;
  s_gain_a = s_gain_b = 60;
  const char *m = getenv("DKC1_CART2P_MEZCLA");
  if (m && *m) {
    int a, b;
    if (sscanf(m, "%d,%d", &a, &b) == 2 && a >= 0 && a <= 200 && b >= 0 &&
        b <= 200) {
      s_gain_a = a;
      s_gain_b = b;
    } else {
      Cart2pLog("audio: DKC1_CART2P_MEZCLA invalida (%s); uso 60,60", m);
    }
  }
  Cart2pLog("audio: mezcla A %d%% + B %d%%", s_gain_a, s_gain_b);
  const char *d = getenv("DKC1_CART2P_AUDIO_DUMP");
  if (d && *d) {
    s_dump_a = OpenDump(d, "_a.pcm");
    s_dump_b = OpenDump(d, "_b.pcm");
    s_dump_m = OpenDump(d, "_m.pcm");
    s_dump_v = OpenDump(d, "_voces.txt");
    if (s_dump_v)
      fprintf(s_dump_v, "# frame muestrasA muestrasB vocesA vocesB "
                        "(bit = voz con envolvente activa)\n");
  }
}

/* Bit per DSP voice whose envelope is running in the loaded game. */
static unsigned ActiveVoices(void) {
  const Dsp *dsp = LoadedDsp();
  unsigned mask = 0;
  if (!dsp)
    return 0;
  for (int i = 0; i < 8; i++)
    if (dsp->channel[i].gain > 0 && dsp->channel[i].adsrState != 4)
      mask |= 1u << i;
  return mask;
}

/* B loaded, right after its frame: keep its new samples, empty its ring so
 * it never fills up (the ring travels inside B's state). */
static void CaptureAudioB(void) {
  Dsp *dsp = LoadedDsp();
  s_b_count = 0;
  if (!dsp)
    return;
  RtlApuLock();
  uint32_t n = dsp->sampleWrite - dsp->sampleRead;
  if (n > DSP_SAMPLE_RING)
    n = DSP_SAMPLE_RING;
  for (uint32_t k = 0; k < n; k++) {
    const uint32_t idx = (dsp->sampleRead + k) & (DSP_SAMPLE_RING - 1);
    s_b_frame[k * 2] = dsp->sampleBuffer[idx * 2];
    s_b_frame[k * 2 + 1] = dsp->sampleBuffer[idx * 2 + 1];
  }
  dsp->sampleRead = dsp->sampleWrite;
  RtlApuUnlock();
  s_b_count = n;
  s_voices_b = ActiveVoices();
}

static int16_t Clamp16(int v) {
  return (int16_t)(v < -0x8000 ? -0x8000 : v > 0x7FFF ? 0x7FFF : v);
}

/* A loaded, after both frames: A's samples of this host frame, not yet
 * consumed by the host, become gain_a * A + gain_b * B. */
static void MixAudioB(void) {
  Dsp *dsp = LoadedDsp();
  if (!dsp)
    return;
  RtlApuLock();
  uint32_t n = dsp->sampleWrite - s_a_write0;
  if (n > DSP_SAMPLE_RING)
    n = DSP_SAMPLE_RING;
  for (uint32_t k = 0; k < n; k++) {
    const uint32_t index = s_a_write0 + k;
    if ((int32_t)(index - dsp->sampleRead) < 0)
      continue; /* already played by the host */
    const uint32_t idx = index & (DSP_SAMPLE_RING - 1);
    for (int c = 0; c < 2; c++) {
      const int a = dsp->sampleBuffer[idx * 2 + c];
      const int b = k < s_b_count ? s_b_frame[k * 2 + c] : 0;
      if (s_dump_a) {
        const int16_t sa = (int16_t)a, sb = (int16_t)b;
        fwrite(&sa, sizeof sa, 1, s_dump_a);
        if (s_dump_b)
          fwrite(&sb, sizeof sb, 1, s_dump_b);
      }
      dsp->sampleBuffer[idx * 2 + c] =
          Clamp16((a * s_gain_a + b * s_gain_b) / 100);
      if (s_dump_m)
        fwrite(&dsp->sampleBuffer[idx * 2 + c], sizeof(int16_t), 1, s_dump_m);
    }
  }
  RtlApuUnlock();
  if (s_dump_v) {
    fprintf(s_dump_v, "%u %u %u $%02X $%02X\n", s_dump_frame, n, s_b_count,
            ActiveVoices(), s_voices_b);
    fflush(s_dump_v);
  }
  s_dump_frame++;
  static bool warned;
  if (s_b_count && n != s_b_count && !warned) {
    warned = true;
    Cart2pLog("audio: A produjo %u muestras y B %u en el mismo frame "
              "(se avisa una vez)", n, s_b_count);
  }
}

/* ---- Race rules (run with that player's game loaded) -------------------- */

static uint16_t ActiveKongSlot(void) {
  return Rd16(kWramCurrentKong) == 2 ? 0x04 : 0x02;
}

/* Stock death states of the active Kong: $11 hit, $3B pit, $3F bonus pit,
 * $0B falling after death (BITACORA 2026-10-06). A hit with the buddy still
 * alive ($0D/$0E on the hit Kong, buddy takes over) is not a death. */
static bool PlayerDying(void) {
  const uint16_t s = Rd16(kWramState + ActiveKongSlot());
  return s == 0x0011 || s == 0x003B || s == 0x003F || s == 0x000B;
}

static bool PlayerAtExit(void) {
  const uint16_t s = Rd16(kWramState + ActiveKongSlot());
  return s == kStateExit || s == kStateExit2;
}

/* The active Kong, still present, crossed the level exit trigger (object
 * type 2 of the level's list; the stock game then removes the Kongs and
 * leaves the stage, which the race never lets happen: the game freezes). */
static bool PlayerPastExit(void) {
  if (!s_exit_x)
    return false;
  const uint16_t slot = ActiveKongSlot();
  if (Rd16(kWramSpriteId + slot) == 0)
    return false;
  const uint16_t x = Rd16(kWramXPos + slot);
  return x < 0x8000 && x >= s_exit_x;
}

/* Checkpoint only while seated in a cart resting on the rail. */
static bool PlayerSafe(void) {
  const uint16_t s = Rd16(kWramState + ActiveKongSlot());
  if (s != kStateRidingCart)
    return false;
  const uint16_t cart = Rd16(kWramCartSlot);
  if (cart < 0x02 || cart >= 0x34 || (cart & 1))
    return false;
  return Rd16(kWramHeight + cart) == 0;
}

static void RingStore(Ring *r, const uint8_t *state, size_t size,
                      uint32_t frame) {
  uint8_t **slot = &r->state[r->head];
  if (!*slot)
    *slot = (uint8_t *)malloc(kStateCapacity);
  if (!*slot)
    return;
  memcpy(*slot, state, size);
  r->size[r->head] = size;
  r->frame[r->head] = frame;
  r->head = (r->head + 1) % r->capacity;
  if (r->count < r->capacity)
    r->count++;
}

static int RingAt(const Ring *r, int k) { /* k = 0: newest */
  return (r->head - 1 - k + 2 * r->capacity) % r->capacity;
}

/* After a rewind to `frame`: forget the abandoned future. */
static void RingDropNewer(Ring *r, uint32_t frame) {
  while (r->count > 0 && r->frame[RingAt(r, 0)] > frame) {
    r->head = RingAt(r, 0);
    r->count--;
  }
}

static void BotLearn(DualPlayer *p, uint16_t x);

/* Death: load an older checkpoint of this player's own game. */
static void Rewind(int index) {
  DualPlayer *p = &s_players[index];
  if (p->near.count == 0 && p->far.count == 0)
    return;
  if (p->deaths > 0 && p->wall - p->respawn_wall < kStreakWindow)
    p->streak++;
  else
    p->streak = 0;
  p->respawn_wall = p->wall;
  p->deaths++;
  if (s_bot)
    BotLearn(p, Rd16(kWramXPos + ActiveKongSlot()));
  const int streak = p->streak < kStreakMax ? p->streak : kStreakMax;
  const uint32_t want = kRewindFrames + (uint32_t)streak * kStreakExtra;
  /* Newest snapshot (short or long-term history) at least `want` frames
   * old; if there is none, the oldest one available. */
  const Ring *best_ring = NULL, *old_ring = NULL;
  int best = -1, oldest = -1;
  const Ring *rings[2] = {&p->near, &p->far};
  for (int r = 0; r < 2; r++) {
    for (int k = 0; k < rings[r]->count; k++) {
      const int at = RingAt(rings[r], k);
      const uint32_t f = rings[r]->frame[at];
      if (p->frame - f >= want &&
          (best < 0 || f > best_ring->frame[best])) {
        best_ring = rings[r];
        best = at;
      }
      if (oldest < 0 || f < old_ring->frame[oldest]) {
        old_ring = rings[r];
        oldest = at;
      }
    }
  }
  if (best < 0) {
    best_ring = old_ring;
    best = oldest;
  }
  if (best < 0)
    return;
  const uint32_t to = best_ring->frame[best];
  const uint32_t back = p->frame - to;
  /* Song in the SPC now ($0523, and $051D: playing or stopped): a crash may
   * already have stopped it or switched it to the death jingle, and the
   * kept SPC would stay silent (the level song is never asked for again).
   * Then the snapshot's own audio comes back too (the music goes back with
   * the game; uploading the song again from here hangs the APU handshake:
   * 5 s watchdog). */
  const uint32_t song_now = Rd16(kWramSong) | (uint32_t)Rd16(kWramSongOn) << 16;
  if (!LoadStateKeepAudio(best_ring->state[best], best_ring->size[best])) {
    Cart2pLog("carrera: P%d no pudo retroceder", index + 1);
    return;
  }
  const uint32_t song_then = Rd16(kWramSong) | (uint32_t)Rd16(kWramSongOn) << 16;
  if (song_now != song_then) {
    const bool ok = LoadState(best_ring->state[best], best_ring->size[best]);
    if (s_verbose || !ok)
      Cart2pLog("carrera: P%d vuelve con el sonido del punto de retroceso "
                "(cancion $%02X; sonaba $%02X%s)%s", index + 1,
                song_then & 0xFFFF, song_now & 0xFFFF,
                song_now >> 16 ? "" : " detenida", ok ? "" : ": FALLO");
  }
  /* The restored snapshot becomes the newest: dying again rewinds further. */
  RingDropNewer(&p->near, to);
  RingDropNewer(&p->far, to);
  p->frame = to;
  p->bot_hold = 0;
  Cart2pLog("carrera: P%d murio (muertes %d, racha %d) -> retrocede %u "
            "frames a X=$%04X", index + 1, p->deaths, p->streak, back,
            Rd16(kWramXPos + ActiveKongSlot()));
}

static void RaceAfterFrame(int index) {
  DualPlayer *p = &s_players[index];
  p->frame++;
  p->wall++;
  if (Rd16(kWramSpriteId + 0x02) == 0 && Rd16(kWramSpriteId + 0x04) == 0)
    p->no_kong++;
  else
    p->no_kong = 0;
  if (!p->finished && PlayerDying() && s_sudden_death > 0 && !s_solo &&
      s_phase == kRaceRunning) {
    /* Sudden death: the other player wins now. */
    const int other = 1 - index;
    s_players[other].finished = true;
    s_players[other].finish_time = s_clock;
    s_winner = other;
    s_phase = kRaceDone;
    s_phase_frames = 0;
    Cart2pLog("carrera %d: P%d murio -> gana P%d (muerte subita)", s_races + 1,
              index + 1, other + 1);
  } else if (!p->finished && p->fade_out > 0) {
    /* The half is going dark (the death animation goes on): rewind at the
     * end, so the jump back is not seen as the track changing. */
    if (--p->fade_out == 0) {
      Rewind(index);
      p->fade_in = kFadeInFrames;
    }
  } else if (!p->finished && PlayerDying()) {
    p->fade_out = kFadeOutFrames;
  } else if (!p->finished &&
             (PlayerPastExit() || PlayerAtExit() ||
              p->no_kong >= kNoKongFrames ||
              (index == 1 && s_test_finish && s_clock >= s_test_finish))) {
    if (p->no_kong >= kNoKongFrames)
      Cart2pLog("carrera: P%d desaparecio de la etapa (sin Kongs %u frames): "
                "cuenta como llegada", index + 1, p->no_kong);
    p->finished = true; /* its game freezes from the next frame on */
    p->finish_time = s_clock;
    p->new_record = SaveRecord(s_clock, index);
    Cart2pLog("carrera %d: P%d llega a la meta en %u frames (%u.%02u s), "
              "muertes %d%s", s_races + 1, index + 1, s_clock, s_clock / 60,
              (s_clock % 60) * 100 / 60, p->deaths,
              p->new_record ? " - NUEVO RECORD" : "");
    if (s_winner < 0) {
      s_winner = index;
      s_phase = kRaceDone;
      s_phase_frames = 0;
    }
  }
  if (!p->fade_out) {
    if (p->fade_in > 0)
      p->fade_in--;
  }
  const uint16_t x = Rd16(kWramXPos + ActiveKongSlot());
  if (x < 0x8000)
    p->x = x;
  if (s_verbose) {
    if (p->x > p->max_x)
      p->max_x = p->x;
    if (p->wall % 1000 == 0)
      Cart2pLog("P%d avance: X max $%04X (muertes %d); ahora X=$%04X "
                "Y=$%04X estado $%04X camara Y=$%04X zona $1AF9=%u",
                index + 1, p->max_x, p->deaths,
                Rd16(kWramXPos + ActiveKongSlot()),
                Rd16(kWramYPos + ActiveKongSlot()),
                Rd16(kWramState + ActiveKongSlot()), Rd16(kWramCameraY),
                Rd16(0x1AF9));
    const uint16_t s = Rd16(kWramState + ActiveKongSlot());
    if (s != p->last_state)
      Cart2pLog("P%d f%u estado $%04X X=$%04X Y=$%04X", index + 1, p->frame,
                s, p->x, Rd16(kWramYPos + ActiveKongSlot()));
    p->last_state = s;
  }
  /* DKC1_CART2P_OBJLOG=N (development): every N frames of P1, the objects
   * alive (slot, sprite ID $0D45, X $0B19, Y $0BC1). */
  static int objlog = -1;
  if (objlog < 0) {
    const char *t = getenv("DKC1_CART2P_OBJLOG");
    objlog = t ? atoi(t) : 0;
  }
  if (objlog > 0 && index == 0 && p->frame % (uint32_t)objlog == 0) {
    char line[600];
    int n = snprintf(line, sizeof line, "objetos P1 f%u X=$%04X:", p->frame, p->x);
    for (int slot = 4; slot < 0x34 && n < (int)sizeof line - 40; slot += 2) {
      const uint16_t id = Rd16(0x0D45 + slot);
      if (id > 2 && id != 0x69)
        n += snprintf(line + n, sizeof line - n, " %02X:%02X@%04X,%04X", slot, id,
                      Rd16(kWramXPos + slot), Rd16(kWramYPos + slot));
    }
    Cart2pLog("%s", line);
  }
}

static void RaceAfterSave(int index, const uint8_t *state, size_t size) {
  DualPlayer *p = &s_players[index];
  if (p->finished || !PlayerSafe())
    return;
  /* First safe frame once the interval has passed since the newest one. */
  if (p->near.count == 0 ||
      p->frame - p->near.frame[RingAt(&p->near, 0)] >= kCheckpointEvery)
    RingStore(&p->near, state, size, p->frame);
  if (p->far.count == 0 ||
      p->frame - p->far.frame[RingAt(&p->far, 0)] >= kMilestoneEvery)
    RingStore(&p->far, state, size, p->frame);
}

/* ---- Test bot ------------------------------------------------------------ */

static uint32_t BotRand(DualPlayer *p) {
  if (!p->rng)
    p->rng = 0x9E3779B9u * (uint32_t)(1 + (p == &s_players[1]));
  p->rng = p->rng * 1664525u + 1013904223u;
  return p->rng >> 8;
}

static void BotLearn(DualPlayer *p, uint16_t x) {
  int i = 0;
  for (; i < p->bot_spots; i++)
    if ((int16_t)(x - p->bot_x[i]) > -40 && (int16_t)(x - p->bot_x[i]) < 40)
      break;
  if (i == p->bot_spots) {
    if (p->bot_spots == 64)
      return;
    p->bot_spots++;
    p->bot_x[i] = x;
  }
  p->bot_offset[i] = (uint16_t)(8 + BotRand(p) % 120);
}

/* Runs with the player's game loaded (before its frame). */
static uint32_t BotInput(int index, uint32_t pad) {
  if (!s_bot)
    return pad;
  DualPlayer *p = &s_players[index];
  if (p->bot_hold > 0) {
    p->bot_hold--;
    return pad | kPadB;
  }
  const uint16_t x = Rd16(kWramXPos + ActiveKongSlot());
  for (int i = 0; i < p->bot_spots; i++) {
    const int d = (int16_t)(p->bot_x[i] - p->bot_offset[i] - x);
    if (d >= 0 && d < 4) {
      p->bot_hold = 12;
      return pad | kPadB;
    }
  }
  if (BotRand(p) % 100 < 2)
    p->bot_hold = 6 + (int)(BotRand(p) % 16);
  return pad;
}

/* ---- Drawing --------------------------------------------------------------- */

/* Draw the loaded game into the right half of the host buffer. */
static void DrawRight(void) {
  if (!g_ppu || !g_ppu->renderBuffer)
    return;
  uint8_t *const buffer = g_ppu->renderBuffer;
  g_ppu->renderBuffer = buffer + (size_t)kViewWidth * kBytesPerPixel;
  s_drawing_half = 1;
  Dkc1DrawPpuFrame();
  s_drawing_half = 0;
  g_ppu->renderBuffer = buffer;
}

/* 5x7 font for the race texts. */
typedef struct { char ch; uint8_t rows[7]; } Glyph;
static const Glyph kFont[] = {
  {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
  {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
  {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
  {'3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
  {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
  {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
  {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}},
  {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
  {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
  {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
  {':', {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}},
  {'.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}},
  {'!', {0x04,0x04,0x04,0x04,0x04,0x00,0x04}},
  {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
  {'>', {0x08,0x04,0x02,0x01,0x02,0x04,0x08}},
  {'<', {0x02,0x04,0x08,0x10,0x08,0x04,0x02}},
  {'/', {0x01,0x01,0x02,0x04,0x08,0x10,0x10}},
  {'(', {0x02,0x04,0x08,0x08,0x08,0x04,0x02}},
  {')', {0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
  {',', {0x00,0x00,0x00,0x00,0x06,0x02,0x04}},
  {'?', {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}},
  {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
  {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
  {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
  {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
  {'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
  {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
  {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
  {'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
  {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
  {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
  {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
  {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
  {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
  {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
  {'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}},
  {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
  {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
  {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
  {'N', {0x11,0x11,0x19,0x15,0x13,0x11,0x11}},
  {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
  {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
  {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
  {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
  {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
  {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
  {'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
};

static const uint8_t *GlyphRows(char ch) {
  for (size_t i = 0; i < sizeof kFont / sizeof kFont[0]; i++)
    if (kFont[i].ch == ch)
      return kFont[i].rows;
  return NULL;
}

static void PutPixel(uint8_t *buffer, size_t pitch, int x, int y,
                     uint32_t color) {
  if (x < 0 || x >= kViewWidth || y < 0 || y >= kViewHeight)
    return;
  ((uint32_t *)(buffer + (size_t)y * pitch))[x] = color;
}

/* Text with a 1-pixel black outline, `scale` pixels per font dot. */
static void DrawText(uint8_t *buffer, size_t pitch, int x, int y, int scale,
                     uint32_t color, const char *text) {
  for (int pass = 0; pass < 2; pass++) {
    int cx = x;
    for (const char *c = text; *c; c++, cx += 6 * scale) {
      const uint8_t *rows = GlyphRows(*c);
      if (!rows)
        continue;
      for (int ry = 0; ry < 7; ry++)
        for (int rx = 0; rx < 5; rx++) {
          if (!(rows[ry] & (0x10 >> rx)))
            continue;
          for (int sy = 0; sy < scale; sy++)
            for (int sx = 0; sx < scale; sx++) {
              const int px = cx + rx * scale + sx, py = y + ry * scale + sy;
              if (pass == 0) {
                for (int oy = -1; oy <= 1; oy++)
                  for (int ox = -1; ox <= 1; ox++)
                    PutPixel(buffer, pitch, px + ox, py + oy, 0x00000000u);
              } else {
                PutPixel(buffer, pitch, px, py, color);
              }
            }
        }
    }
  }
}

static int TextWidth(const char *text, int scale) {
  return (int)strlen(text) * 6 * scale - scale;
}

static void DrawCentered(uint8_t *buffer, size_t pitch, int y, int scale,
                         uint32_t color, const char *text) {
  DrawText(buffer, pitch, (kViewWidth - TextWidth(text, scale)) / 2, y, scale,
           color, text);
}

static void FormatClock(uint32_t frames, char *out, size_t size) {
  const uint32_t cs = frames * 100 / 60;
  snprintf(out, size, "%u:%02u.%02u", cs / 6000, (cs / 100) % 60, cs % 100);
}

static void Darken(uint8_t *buffer, size_t pitch) {
  for (int y = 0; y < kViewHeight; y++) {
    uint32_t *row = (uint32_t *)(buffer + (size_t)y * pitch);
    for (int x = 0; x < kViewWidth; x++)
      row[x] = (row[x] >> 2) & 0x003F3F3Fu;
  }
}

/* Brightness 0..16 of the whole half. */
static void Dim(uint8_t *buffer, size_t pitch, int level) {
  if (level >= 16)
    return;
  for (int y = 0; y < kViewHeight; y++) {
    uint32_t *row = (uint32_t *)(buffer + (size_t)y * pitch);
    for (int x = 0; x < kViewWidth; x++) {
      const uint32_t c = row[x];
      row[x] = ((((c >> 16) & 0xFF) * (uint32_t)level >> 4) << 16) |
               ((((c >> 8) & 0xFF) * (uint32_t)level >> 4) << 8) |
               ((c & 0xFF) * (uint32_t)level >> 4);
    }
  }
}

/* Death transition of the player drawn in this half: dark fade and the
 * "RETROCEDE" notice around the rewind. */
static void DrawRewind(uint8_t *buffer, size_t pitch, const DualPlayer *p) {
  const uint32_t kYellow = 0x00FFD800u;
  if (p->fade_out > 0)
    Dim(buffer, pitch, 16 * p->fade_out / kFadeOutFrames);
  else if (p->fade_in > 0)
    Dim(buffer, pitch, 16 - 16 * p->fade_in / kFadeInFrames);
  if ((p->fade_out > 0 && p->fade_out <= kFadeOutFrames / 2) || p->fade_in > 0)
    DrawCentered(buffer, pitch, 40, 3, kYellow, "RETROCEDE");
}

static void FillRect(uint8_t *buffer, size_t pitch, int x0, int y0, int w,
                     int h, uint32_t color) {
  for (int y = y0; y < y0 + h; y++)
    for (int x = x0; x < x0 + w; x++)
      PutPixel(buffer, pitch, x, y, color);
}

/* Welcome screen: the title spans both halves ("CART" | "CHASE"). */
static void DrawTitle(uint8_t *buffer, size_t pitch, int half) {
  const uint32_t kYellow = 0x00FFD800u, kWhite = 0x00FFFFFFu,
                 kGrey = 0x00B0B0B0u, kBrown = 0x00A05A20u;
  Darken(buffer, pitch);
  /* rail across the screen */
  FillRect(buffer, pitch, 0, 150, kViewWidth, 3, kBrown);
  FillRect(buffer, pitch, 0, 160, kViewWidth, 3, kBrown);
  for (int x = (half ? 12 : 4); x < kViewWidth; x += 16)
    FillRect(buffer, pitch, x, 147, 4, 19, 0x00603818u);
  if (half == 0) {
    DrawText(buffer, pitch, kViewWidth - TextWidth("CART", 7) - 6, 40, 7,
             kYellow, "CART");
    DrawText(buffer, pitch, kViewWidth - TextWidth("DONKEY KONG", 1) - 8, 104,
             1, kGrey, "DONKEY KONG");
  } else {
    DrawText(buffer, pitch, 6, 40, 7, kYellow, "CHASE");
    DrawText(buffer, pitch, 8, 104, 1, kGrey, "COUNTRY  2 JUGADORES");
  }
  if ((s_phase_frames / 30) % 2 == 0)
    DrawText(buffer, pitch,
             half == 0 ? kViewWidth - TextWidth("PULSA", 2) - 4 : 4, 182, 2,
             kWhite, half == 0 ? "PULSA" : "START");
  char text[48];
  if (half == 0)
    snprintf(text, sizeof text, "%d ETAPAS", s_list_count ? s_list_count : 1);
  else
    snprintf(text, sizeof text, "F10 CAMBIA MANDOS");
  DrawCentered(buffer, pitch, 210, 1, kGrey, text);
}

/* Main menu: options on the left half, the highlighted one explained on
 * the right half. */
static void DrawMain(uint8_t *buffer, size_t pitch, int half) {
  const uint32_t kWhite = 0x00FFFFFFu, kYellow = 0x00FFD800u,
                 kGrey = 0x00808080u, kSoft = 0x00C0C0C0u;
  static const char *const kLabels[kMainCount] = {
      "2 JUGADORES", "1 JUGADOR", "EDITOR", "ETAPAS AL AZAR", "VOLUMEN P1",
      "VOLUMEN P2", "MUERTE SUBITA", "SALIR"};
  Darken(buffer, pitch);
  char text[64];
  if (half == 0) {
    DrawText(buffer, pitch, 20, 14, 2, kYellow, "CART CHASE 2P");
    FillRect(buffer, pitch, 20, 34, 216, 1, 0x00A05A20u);
    for (int i = 0; i < kMainCount; i++) {
      const int y = 46 + i * 21;
      const bool sel = i == s_main_cursor;
      const uint32_t c = sel ? kYellow : kWhite;
      if (sel)
        DrawText(buffer, pitch, 14, y, 2, kYellow, ">");
      DrawText(buffer, pitch, 32, y, 2, c, kLabels[i]);
    }
    DrawText(buffer, pitch, 20, 214, 1, kSoft, "ARRIBA/ABAJO  A: ELEGIR");
    return;
  }
  /* right half: the highlighted option */
  DrawText(buffer, pitch, 12, 14, 2, kYellow, kLabels[s_main_cursor]);
  FillRect(buffer, pitch, 12, 34, 232, 1, 0x00A05A20u);
  const char *lines[6] = {0};
  const char *value = NULL;
  switch (s_main_cursor) {
  case kMainJugar:
    lines[0] = "CARRERA DE 2 JUGADORES.";
    lines[1] = "PRIMERO CADA UNO ELIGE SU LADO";
    lines[2] = "CON SU MANDO, LUEGO LA ETAPA.";
    snprintf(text, sizeof text, "%d ETAPAS EN LA LISTA", s_list_count);
    lines[4] = text;
    break;
  case kMainSolo:
    lines[0] = "UN JUGADOR CONTRA EL RELOJ,";
    lines[1] = "A PANTALLA COMPLETA, CON";
    lines[2] = "CUALQUIER MANDO O EL TECLADO.";
    lines[3] = "RECORDS APARTE DE LOS DE 2.";
    snprintf(text, sizeof text, "%d ETAPAS EN LA LISTA", s_list_count);
    lines[5] = text;
    break;
  case kMainEditor:
    lines[0] = "ARMAR Y CAMBIAR ETAPAS DENTRO";
    lines[1] = "DEL JUEGO, CON MOUSE Y TECLADO:";
    lines[2] = "TRAMOS, RECTAS, HOYOS, OBJETOS.";
    lines[3] = "SE GUARDAN EN LA LISTA Y SE";
    lines[4] = "PRUEBAN AL INSTANTE (TECLA P).";
    break;
  case kMainAzar:
    lines[0] = "CREA ETAPAS NUEVAS AL AZAR CON";
    lines[1] = "TRAMOS DE LOS DOS NIVELES Y LAS";
    lines[2] = "AGREGA AL FINAL DE LA LISTA.";
    lines[4] = "IZQ/DER: CUANTAS   A: CREAR";
    snprintf(text, sizeof text, "< %d >", s_azar_cantidad);
    value = text;
    break;
  case kMainVolumen1:
  case kMainVolumen2:
    lines[0] = s_main_cursor == kMainVolumen1 ?
        "VOLUMEN DEL JUEGO DE P1." : "VOLUMEN DEL JUEGO DE P2.";
    lines[1] = "60 Y 60: LA MUSICA COMO EL";
    lines[2] = "ORIGINAL Y LOS SONIDOS DE CADA";
    lines[3] = "JUGADOR UN POCO MAS BAJOS.";
    snprintf(text, sizeof text, "< %d >",
             s_main_cursor == kMainVolumen1 ? s_gain_a : s_gain_b);
    value = text;
    break;
  case kMainMuerte:
    lines[0] = "SI: EL PRIMERO QUE MUERE PIERDE.";
    lines[1] = "NO: AL MORIR SE RETROCEDE UN";
    lines[2] = "POCO Y LA CARRERA SIGUE.";
    value = s_sudden_death > 0 ? "< SI >" : "< NO >";
    break;
  case kMainSalir:
    lines[0] = "CERRAR EL JUEGO.";
    break;
  }
  for (int i = 0; i < 6; i++)
    if (lines[i])
      DrawText(buffer, pitch, 12, 48 + i * 12, 1, kWhite, lines[i]);
  if (value)
    DrawCentered(buffer, pitch, 140, 3, kYellow, value);
  if (s_azar_corriendo) {
    static const char *const kDots[4] = {"CREANDO ETAPAS", "CREANDO ETAPAS.",
                                         "CREANDO ETAPAS..", "CREANDO ETAPAS..."};
    DrawCentered(buffer, pitch, 180, 1, kYellow, kDots[(s_phase_frames / 15) % 4]);
  }
  if (s_notice_frames > 0)
    DrawCentered(buffer, pitch, 196, 1, kYellow, s_notice);
  DrawText(buffer, pitch, 12, 214, 1, kSoft, "F10: CAMBIAR MANDOS DE LADO");
}

/* Controller screen on one half: the side, the controller on it, and
 * what to do. Same layout both halves. */
static void DrawBind(uint8_t *buffer, size_t pitch, int half) {
  const uint32_t kWhite = 0x00FFFFFFu, kYellow = 0x00FFD800u,
                 kGreen = 0x0040FF60u, kGrey = 0x00B0B0B0u;
  const uint32_t side_color = half == 0 ? 0x00FF4040u : 0x0060A0FFu;
  Darken(buffer, pitch);
  DrawCentered(buffer, pitch, 10, 1, kGrey,
               half == 0 ? "LADO IZQUIERDO" : "LADO DERECHO");
  DrawCentered(buffer, pitch, 24, 6, side_color, half == 0 ? "P1" : "P2");
  /* controller card */
  const int cy = 76;
  FillRect(buffer, pitch, 20, cy, kViewWidth - 40, 1, side_color);
  FillRect(buffer, pitch, 20, cy + 46, kViewWidth - 40, 1, side_color);
  if (s_bind_pad[half]) {
    DrawCentered(buffer, pitch, cy + 8, 1, kWhite,
                 s_bind_name[half][0] ? s_bind_name[half] : "MANDO");
    if (s_bind_ready[half])
      DrawCentered(buffer, pitch, cy + 24, 2, kGreen, "LISTO");
    else
      DrawCentered(buffer, pitch, cy + 24, 2, kYellow, "PULSA A");
  } else {
    DrawCentered(buffer, pitch, cy + 10, 1, kGrey, "SIN MANDO");
    DrawCentered(buffer, pitch, cy + 26, 1, kGrey,
                 half == 0 ? "TECLADO: FLECHAS Y Z" : "TECLADO: IJKL Y N");
  }
  DrawCentered(buffer, pitch, 134, 1, kGrey,
               half == 0 ? "PARA JUGAR A LA IZQUIERDA:" :
                           "PARA JUGAR A LA DERECHA:");
  DrawCentered(buffer, pitch, 146, 1, kWhite,
               half == 0 ? "EMPUJA TU MANDO A LA IZQUIERDA" :
                           "EMPUJA TU MANDO A LA DERECHA");
  DrawCentered(buffer, pitch, 158, 1, kWhite, "Y PULSA A.  B: CANCELAR");
  if (s_bind_spare > 0 && half == 1)
    DrawCentered(buffer, pitch, 172, 1, kGrey, "HAY MAS DE 2 MANDOS");
  if (s_bind_ready[0] && s_bind_ready[1])
    DrawCentered(buffer, pitch, 186, 3, kYellow, "LISTOS!");
  else
    DrawCentered(buffer, pitch, 200, 1, kGrey, "START: EMPEZAR ASI  SELECT: MENU");
}

static void FormatClock(uint32_t frames, char *out, size_t size);

/* Stage menu on one half (both halves show the same menu). */
static void DrawSelect(uint8_t *buffer, size_t pitch) {
  const uint32_t kWhite = 0x00FFFFFFu, kYellow = 0x00FFD800u,
                 kGrey = 0x00A0A0A0u;
  for (int y = 0; y < kViewHeight; y++) {
    uint32_t *row = (uint32_t *)(buffer + (size_t)y * pitch);
    for (int x = 0; x < kViewWidth; x++)
      row[x] = (row[x] >> 2) & 0x003F3F3Fu;
  }
  DrawCentered(buffer, pitch, 8, 2, kYellow, "ELIGE ETAPA");
  DrawText(buffer, pitch, 8, 20, 1, 0x00A0A0A0u, s_solo ? "1 JUGADOR" : "2 JUGADORES");
  const int rows = s_list_count + 1;
  {
    char pos[48];
    snprintf(pos, sizeof pos, "PAG %d/%d  %d/%d", s_select_cursor / kSelectPage + 1,
             (rows + kSelectPage - 1) / kSelectPage, s_select_cursor + 1, rows);
    DrawText(buffer, pitch, 248 - TextWidth(pos, 1), 20, 1, kGrey, pos);
  }
  const int step = rows > 15 ? 10 : 11;
  const int visible = kSelectPage;
  const int first = s_select_cursor / kSelectPage * kSelectPage; /* whole pages */
  for (int r = first; r < rows && r < first + visible; r++) {
    const int y = 30 + (r - first) * step;
    const bool sel = r == s_select_cursor;
    const uint32_t color = sel ? kYellow : kWhite;
    if (sel)
      DrawText(buffer, pitch, 8, y, 1, kYellow, ">");
    if (r == s_list_count) {
      DrawText(buffer, pitch, 20, y, 1, color, "AL AZAR ?");
      continue;
    }
    DrawText(buffer, pitch, 20, y, 1, color, s_list[r].name);
    const uint32_t best = BestTimeOfEntry(r);
    if (best) {
      char clock[32];
      FormatClock(best, clock, sizeof clock);
      DrawText(buffer, pitch, 248 - TextWidth(clock, 1), y, 1,
               sel ? kYellow : kGrey, clock);
    }
  }
  DrawCentered(buffer, pitch, 200, 1, kGrey, "ARRIBA/ABAJO ELIGE   LB/RB PAGINA");
  DrawCentered(buffer, pitch, 211, 1, kGrey, "A: JUGAR   B: MENU PRINCIPAL");
}

static void DrawHud(uint8_t *buffer, size_t pitch, int half) {
  const uint32_t kWhite = 0x00FFFFFFu, kYellow = 0x00FFD800u,
                 kRed = 0x00FF4040u, kGreen = 0x0040FF60u,
                 kGrey = 0x00C0C0C0u;
  char text[64];
  const DualPlayer *me = &s_players[half];
  if (s_phase == kRaceTitle) {
    DrawTitle(buffer, pitch, half);
    return;
  }
  if (s_phase == kRaceMain) {
    DrawMain(buffer, pitch, half);
    return;
  }
  if (s_phase == kRaceEditor)
    return; /* drawn by Cart2pEditorDibujar (whole frame) */
  if (s_phase == kRaceBind) {
    DrawBind(buffer, pitch, half);
    return;
  }
  if (s_phase == kRaceSelect) {
    DrawSelect(buffer, pitch);
    return;
  }
  if (s_phase == kRaceRunning || s_phase == kRaceDone)
    DrawRewind(buffer, pitch, me);
  /* Player tag. */
  if (!s_solo)
    DrawText(buffer, pitch, 200, 4, 1, half == 0 ? kRed : 0x0060A0FFu,
             half == 0 ? "P1" : "P2");
  if (s_notice_frames > 0)
    DrawCentered(buffer, pitch, 200, 1, kYellow, s_notice);
  if (s_phase == kRaceCountdown) {
    const int n = 3 - (int)(s_phase_frames * 3 / kCountdownFrames);
    snprintf(text, sizeof text, "%d", n < 1 ? 1 : n);
    DrawCentered(buffer, pitch, 80, 8, kYellow, text);
    return;
  }
  FormatClock(me->finished ? me->finish_time : s_clock, text, sizeof text);
  DrawCentered(buffer, pitch, 4, 1, kWhite, text);
  {
    const uint32_t best = BestTime();
    if (best) {
      char clock[32];
      FormatClock(best, clock, sizeof clock);
      snprintf(text, sizeof text, "MEJOR %s", clock);
      DrawCentered(buffer, pitch, 13, 1, kGrey, text);
    }
  }
  if (s_phase == kRaceRunning && s_phase_frames < kGoFrames)
    DrawCentered(buffer, pitch, 80, 8, kGreen, "YA!");
  if (s_solo) {
    if (s_phase == kRaceDone) {
      DrawCentered(buffer, pitch, 70, 4, kYellow, "LLEGASTE!");
      FormatClock(me->finish_time, text, sizeof text);
      DrawCentered(buffer, pitch, 110, 2, kWhite, text);
      if (me->new_record)
        DrawCentered(buffer, pitch, 130, 1, kYellow, "NUEVO RECORD!");
      snprintf(text, sizeof text, "MUERTES: %d", me->deaths);
      DrawCentered(buffer, pitch, 144, 1, kGrey, text);
      DrawCentered(buffer, pitch, 160, 1, kGrey,
                   s_list_count > 1 ? "START: ELEGIR ETAPA" : "START: OTRA VEZ");
    }
    return;
  }
  /* Position by progress (X); a finished player keeps its place. */
  const DualPlayer *rival = &s_players[1 - half];
  bool first;
  if (me->finished || rival->finished)
    first = me->finished && (!rival->finished ||
                             me->finish_time <= rival->finish_time);
  else
    first = me->x >= rival->x;
  DrawText(buffer, pitch, 226, 4, 1, first ? kYellow : kWhite,
           first ? "1RO" : "2DO");
  if (s_phase == kRaceDone) {
    snprintf(text, sizeof text, "P%d GANA!", s_winner + 1);
    DrawCentered(buffer, pitch, 70, 4, s_winner == half ? kYellow : kWhite,
                 text);
    FormatClock(s_players[s_winner].finish_time, text, sizeof text);
    DrawCentered(buffer, pitch, 110, 2, kWhite, text);
    if (s_players[s_winner].new_record)
      DrawCentered(buffer, pitch, 130, 1, kYellow, "NUEVO RECORD!");
    if (s_list_count > 1) {
      DrawCentered(buffer, pitch, 160, 1, kGrey, "START: ELEGIR ETAPA");
    } else {
      DrawCentered(buffer, pitch, 160, 1, kGrey, "START: OTRA VEZ");
    }
  }
}

void Cart2pDualDecorate(void) {
  if (!Cart2pDual() || !g_ppu || !g_ppu->renderBuffer)
    return;
  const int half = s_drawing_half;
  uint8_t *const buffer = g_ppu->renderBuffer;
  const size_t pitch = g_ppu->renderPitch;
  if (s_phase == kRaceEditor) {
    if (half == 0)
      Cart2pEditorDibujar(buffer, pitch, Dkc1VideoWidth());
    return;
  }
  DrawHud(buffer, pitch, half);
  if (half == 1) /* divider */
    for (int y = 0; y < kViewHeight; y++)
      PutPixel(buffer, pitch, 0, y, 0x00101010u);
}

/* ---- Frame driver ---------------------------------------------------------- */

static void ResetPlayers(void) {
  for (int i = 0; i < 2; i++) {
    DualPlayer *p = &s_players[i];
    p->near.capacity = kCheckpoints;
    p->far.capacity = kMilestones;
    p->near.head = p->near.count = 0;
    p->far.head = p->far.count = 0;
    p->frame = 0;
    p->respawn_wall = 0;
    p->streak = p->deaths = 0;
    p->x = 0;
    p->finished = false;
    p->finish_time = 0;
    p->no_kong = 0;
    p->bot_hold = 0;
    p->max_x = 0;
    p->new_record = false;
    p->fade_out = p->fade_in = 0;
  }
  s_winner = -1;
  s_clock = 0;
  s_phase = kRaceCountdown;
  s_phase_frames = 0;
}

/* P2 plays Diddy: in B (a copy of A, DK leading) the Kongs swap roles the
 * way the stock Kong switch leaves them: Diddy active ($056F = 2, Kong index
 * $84 = 2) riding the cart, DK the follower in it. Select does nothing in a
 * minecart, so the swap is applied directly, once, at the race start. */
static void MakeDiddyLead(void) {
  const uint16_t cart = Rd16(kWramCartSlot);
  if (Rd16(kWramCurrentKong) != 1 || Rd16(kWramSpriteIdBase + 0x02) != 1 ||
      Rd16(kWramSpriteIdBase + 0x04) != 2 ||
      Rd16(kWramState + 0x02) != kStateRidingCart ||
      Rd16(kWramState + 0x04) != kStateFollowerInCart || cart < 2 ||
      cart >= 0x34 || (cart & 1) || Rd16(kWramRider + cart) != 0x02 ||
      Rd16(kWramSecondRider + cart) != 0x04) {
    Cart2pLog("P2: no se pudo poner a Diddy al mando (estado inesperado)");
    return;
  }
  Wr16(kWramCurrentKong, 2);
  Wr16(kWramPlayerKongIndex, 2);
  Wr16(kWramState + 0x02, kStateFollowerInCart);
  Wr16(kWramState + 0x04, kStateRidingCart);
  Wr16(kWramRider + cart, 0x04);
  Wr16(kWramSecondRider + cart, 0x02);
  Wr16(kWramXPos + 0x04, Rd16(kWramXPos + cart));
  Wr16(kWramYPos + 0x04, Rd16(kWramYPos + cart));
}

/* Both games back to the race start (A loaded, B = copy with Diddy). */
static bool RestartRace(void) {
  if (!LoadState(s_start, s_size_start))
    return false;
  if (!s_state_b)
    s_state_b = (uint8_t *)malloc(kStateCapacity);
  if (!s_state_b)
    return false;
  MakeDiddyLead();
  /* B starts with an empty output ring: the start state's backlog of
   * samples belongs to A (the host plays it) and would be mixed misaligned. */
  {
    Dsp *dsp = LoadedDsp();
    if (dsp)
      dsp->sampleRead = dsp->sampleWrite;
  }
  s_size_b = Save(&s_state_b);
  if (!s_size_b || !LoadState(s_start, s_size_start))
    return false;
  s_has_b = true;
  ResetPlayers();
  Cart2pLog("carrera %d: cuenta regresiva", s_races + 1);
  return true;
}

/* ---- Stage list and records ------------------------------------------------ */

static const char *BaseName(const char *path) {
  const char *b = path;
  for (const char *p = path; *p; p++)
    if (*p == '/' || *p == '\\')
      b = p + 1;
  return b;
}

static void StageKey(const char *state, const char *stage, char *out,
                     size_t size) {
  snprintf(out, size, "%s+%s", state && *state ? BaseName(state) : "?",
           stage && *stage ? BaseName(stage) : "original");
  for (char *c = out; *c; c++)
    if (*c == ' ')
      *c = '_';
}

static void MakeStageKey(const char *state, const char *stage) {
  StageKey(state, stage, s_stage_key, sizeof s_stage_key);
}

static const char *RecordsPath(void) {
  const char *p = getenv("DKC1_CART2P_RECORDS");
  return p && *p ? p : "cart2p/records.txt";
}

static void LoadRecords(void) {
  if (s_records_loaded)
    return;
  s_records_loaded = true;
  FILE *f = fopen(RecordsPath(), "r");
  if (!f)
    return;
  char line[512];
  while (s_record_count < kMaxRecords && fgets(line, sizeof line, f)) {
    Record *r = &s_records[s_record_count];
    unsigned frames = 0;
    int player = 0;
    r->date[0] = 0;
    if (sscanf(line, "%159s %u P%d %31s", r->key, &frames, &player,
               r->date) >= 3 && frames) {
      r->frames = frames;
      r->player = player;
      s_record_count++;
    }
  }
  fclose(f);
}

/* Key of the loaded stage for the current mode. */
static const char *ModeKey(const char *base) {
  static char key[176];
  snprintf(key, sizeof key, "%s%s", s_solo ? "1J+" : "", base);
  return key;
}

static uint32_t BestTimeOf(const char *base) {
  const char *key = ModeKey(base);
  LoadRecords();
  for (int i = 0; i < s_record_count; i++)
    if (strcmp(s_records[i].key, key) == 0)
      return s_records[i].frames;
  return 0;
}

static uint32_t BestTime(void) {
  return BestTimeOf(s_stage_key);
}

static uint32_t BestTimeOfEntry(int index) {
  char key[160];
  StageKey(s_list[index].state, s_list[index].stage, key, sizeof key);
  return BestTimeOf(key);
}

/* True (and saved) when `frames` beats the stage's best time. */
static bool SaveRecord(uint32_t frames, int player) {
  if (!s_stage_key[0] || !frames)
    return false;
  const char *key = ModeKey(s_stage_key);
  LoadRecords();
  Record *r = NULL;
  for (int i = 0; i < s_record_count; i++)
    if (strcmp(s_records[i].key, key) == 0)
      r = &s_records[i];
  if (r && r->frames <= frames)
    return false;
  if (!r) {
    if (s_record_count >= kMaxRecords)
      return false;
    r = &s_records[s_record_count++];
    snprintf(r->key, sizeof r->key, "%s", key);
  }
  r->frames = frames;
  r->player = player + 1;
  const time_t now = time(NULL);
  const struct tm *t = localtime(&now);
  r->date[0] = 0;
  if (t)
    strftime(r->date, sizeof r->date, "%Y-%m-%d_%H:%M", t);
  FILE *f = fopen(RecordsPath(), "w");
  if (!f) {
    Cart2pLog("records: no se pudo escribir %s", RecordsPath());
    return true;
  }
  fprintf(f, "# clave frames jugador fecha (segundos = frames / 60)\n");
  for (int i = 0; i < s_record_count; i++)
    fprintf(f, "%s %u P%d %s\n", s_records[i].key, s_records[i].frames,
            s_records[i].player, s_records[i].date);
  fclose(f);
  return true;
}

static void Trim(char *s) {
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' ||
               s[n - 1] == '\n'))
    s[--n] = 0;
  size_t i = 0;
  while (s[i] == ' ' || s[i] == '\t')
    i++;
  if (i)
    memmove(s, s + i, n - i + 1);
}

/* Path of the list file's folder + a relative path. */
static void ResolvePath(const char *base_dir, const char *path, char *out,
                        size_t size) {
  if (!*path) {
    out[0] = 0;
    return;
  }
  const bool absolute =
      path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':');
  if (absolute || !*base_dir)
    snprintf(out, size, "%s", path);
  else
    snprintf(out, size, "%s%s", base_dir, path);
}

/* cart2p\opciones.txt: "volumen_p1=60", "volumen_p2=60", "muerte_subita=0".
 * Environment variables (tests, .bat) win over the file. */
static const char *OptionsPath(void) {
  const char *p = getenv("DKC1_CART2P_OPCIONES");
  return p && *p ? p : NULL;
}

static void LoadOptions(void) {
  const char *path = OptionsPath();
  FILE *f = path ? fopen(path, "r") : NULL;
  if (!f)
    return;
  char line[128];
  while (fgets(line, sizeof line, f)) {
    int v;
    if (sscanf(line, "volumen_p1=%d", &v) == 1 && !getenv("DKC1_CART2P_MEZCLA") &&
        v >= 0 && v <= 100)
      s_gain_a = v;
    else if (sscanf(line, "volumen_p2=%d", &v) == 1 &&
             !getenv("DKC1_CART2P_MEZCLA") && v >= 0 && v <= 100)
      s_gain_b = v;
    else if (sscanf(line, "muerte_subita=%d", &v) == 1 &&
             !getenv("DKC1_CART2P_MUERTE"))
      s_sudden_death = v ? 1 : 0;
  }
  fclose(f);
  Cart2pLog("opciones: volumen P1 %d%%, P2 %d%%, muerte subita %s (%s)",
            s_gain_a, s_gain_b, s_sudden_death > 0 ? "si" : "no", path);
}

static void SaveOptions(void) {
  const char *path = OptionsPath();
  FILE *f = path ? fopen(path, "w") : NULL;
  if (!f)
    return;
  fprintf(f, "# Cart Chase 2P - opciones (se cambian en el menu del juego)\n");
  fprintf(f, "volumen_p1=%d\nvolumen_p2=%d\nmuerte_subita=%d\n", s_gain_a,
          s_gain_b, s_sudden_death > 0 ? 1 : 0);
  fclose(f);
}

/* "Etapas al azar" of the main menu: the C generator (cart2p_receta.c)
 * writes the recipes and .c2p and adds them to the list, then the list is
 * read again. Drawn as "CREANDO ETAPAS..." for one frame first. */
static void StartRandomizer(void) {
  if (!RecDirCart2p()[0]) {
    Cart2pDualNotice("SIN LISTA DE ETAPAS");
    return;
  }
  s_azar_corriendo = true; /* shown this frame, done in the next one */
  s_azar_antes = s_list_count;
}

static void PollRandomizer(void) {
  if (!s_azar_corriendo)
    return;
  static int wait;
  if (wait++ < 2)
    return;
  wait = 0;
  const int hechas = RecAzar(s_azar_cantidad, 0);
  s_azar_corriendo = false;
  s_list_count = 0;
  LoadStageList();
  char text[48];
  snprintf(text, sizeof text, "%d ETAPAS NUEVAS", hechas);
  Cart2pDualNotice(hechas ? text : "NO SE PUDIERON CREAR");
  Cart2pLog("menu: %d etapas al azar creadas, %d en la lista", hechas,
            s_list_count);
}

void Cart2pDualRecargarLista(void) {
  s_list_count = 0;
  LoadStageList();
}

const uint8_t *Cart2pGlyph(char ch) {
  return GlyphRows(ch);
}

bool Cart2pDualWantsQuit(void) {
  return s_quit;
}

bool Cart2pDualSolo(void) {
  return s_solo;
}

bool Cart2pDualCargarNivel(const char *nivel) {
  char path[1200];
  snprintf(path, sizeof path, "%s%s", RecDirCart2p(), RecNivelEstado(nivel));
  Cart2pStageRestoreRom();
  const bool ok = RtlLoadSnapshot(path);
  if (!ok)
    Cart2pLog("editor: no se pudo cargar %s", path);
  return ok;
}

/* DKC1_CART2P_TEMAS=dir: dumps dir/tema_<entrance>.bin of the levels in
 * DKC1_CART2P_TEMAS_ENTRADAS (hex, comma separated) for tools/cart2p/
 * temas.py, each on a copy of the loaded game, which is restored after. */
static void ExportThemes(void) {
  const char *dir = getenv("DKC1_CART2P_TEMAS");
  if (!dir || !*dir)
    return;
  const char *list = getenv("DKC1_CART2P_TEMAS_ENTRADAS");
  if (!list || !*list)
    list = "16,0D,07,01,0A,40,41,BF,42,24,6D,A4,CE,D0";
  static uint8_t *saved;
  const size_t size = Save(&saved);
  if (!size)
    return;
  for (const char *p = list; *p;) {
    char *end;
    const unsigned long e = strtoul(p, &end, 16);
    if (end == p)
      break;
    char path[1200];
    snprintf(path, sizeof path, "%s/tema_%02lX.bin", dir, e);
    Cart2pStageExportTheme((uint16_t)e, path);
    LoadState(saved, size);
    p = *end == ',' ? end + 1 : end;
  }
}

static void VolverAlEditor(void) {
  s_test_editor = false;
  SetSolo(false);
  s_phase = kRaceEditor;
  s_phase_frames = 0;
  Cart2pEditorVolverDePrueba();
  Cart2pLog("editor: fin de la prueba");
}

/* DKC1_CART2P_EDITOR_GUION=file (headless tests of the editor): lines
 * "<frame> tecla <name|char> [ctrl|shift|alt]", "<frame> texto <word>",
 * "<frame> mouse <move|down|up|wheel> <x> <y> [button] [wheel]",
 * "<frame> menu <option index>" (main menu cursor + A). */
static void GuionEditor(uint32_t frame) {
  static FILE *f;
  static bool opened;
  static char pending[160];
  if (!opened) {
    opened = true;
    const char *p = getenv("DKC1_CART2P_EDITOR_GUION");
    f = p && *p ? fopen(p, "r") : NULL;
  }
  if (!f)
    return;
  for (;;) {
    if (!pending[0] && !fgets(pending, sizeof pending, f)) {
      fclose(f);
      f = NULL;
      return;
    }
    unsigned at;
    char cmd[16], a[24], b[16];
    int x = 0, y = 0, c = 0, d = 0;
    if (sscanf(pending, "%u %15s", &at, cmd) != 2) {
      pending[0] = 0;
      continue;
    }
    if (at > frame)
      return;
    if (!strcmp(cmd, "tecla") && sscanf(pending, "%*u %*s %23s %15s", a, b) >= 1) {
      static const struct { const char *n; int k; } kNames[] = {
          {"left", kEdKeyLeft}, {"right", kEdKeyRight}, {"up", kEdKeyUp},
          {"down", kEdKeyDown}, {"del", kEdKeyDelete}, {"bksp", kEdKeyBackspace},
          {"enter", kEdKeyEnter}, {"esc", kEdKeyEscape}, {"tab", kEdKeyTab},
          {"home", kEdKeyHome}, {"end", kEdKeyEnd}, {"pgup", kEdKeyPageUp},
          {"pgdn", kEdKeyPageDown}, {"f1", kEdKeyF1}, {"f2", kEdKeyF2},
          {"f5", kEdKeyF5}};
      int key = (unsigned char)a[0];
      for (size_t i = 0; i < sizeof kNames / sizeof kNames[0]; i++)
        if (!strcmp(kNames[i].n, a))
          key = kNames[i].k;
      int mods = 0;
      if (strstr(pending, " ctrl")) mods |= kEdModCtrl;
      if (strstr(pending, " shift")) mods |= kEdModShift;
      if (strstr(pending, " alt")) mods |= kEdModAlt;
      Cart2pDualKey(key, mods);
    } else if (!strcmp(cmd, "texto") && sscanf(pending, "%*u %*s %23s", a) == 1) {
      Cart2pDualText(a);
    } else if (!strcmp(cmd, "mouse") &&
               sscanf(pending, "%*u %*s %15s %d %d %d %d", b, &x, &y, &c, &d) >= 3) {
      const int kind = !strcmp(b, "down") ? kEdMouseDown : !strcmp(b, "up") ? kEdMouseUp
                       : !strcmp(b, "wheel") ? kEdMouseWheel : kEdMouseMove;
      Cart2pDualMouse(kind, x, y, c, d, 0);
    } else if (!strcmp(cmd, "menu") && sscanf(pending, "%*u %*s %d", &x) == 1) {
      s_main_cursor = x;
      s_guion_confirma = true;
    }
    pending[0] = 0;
  }
}

bool Cart2pDualKey(int key, int mods) {
  if (!Cart2pDual())
    return false;
  if (s_phase == kRaceEditor) {
    Cart2pEditorTecla(key, mods);
    return true;
  }
  if (s_test_editor && key == kEdKeyEscape) {
    VolverAlEditor();
    return true;
  }
  return false;
}

void Cart2pDualText(const char *utf8) {
  if (Cart2pDual() && s_phase == kRaceEditor)
    Cart2pEditorTexto(utf8);
}

bool Cart2pDualWantsMouse(void) {
  return Cart2pDual() && s_phase == kRaceEditor;
}

void Cart2pDualMouse(int kind, int x, int y, int button, int wheel, int mods) {
  if (Cart2pDualWantsMouse())
    Cart2pEditorMouse(kind, x, y, button, wheel, mods);
}

static void SetSolo(bool solo) {
  if (s_solo == solo)
    return;
  s_solo = solo;
  s_skip_b_draw = true;
  Cart2pLog("modo: %s", solo ? "1 jugador (pantalla completa)" : "2 jugadores");
}

static void LoadStageList(void) {
  const char *path = getenv("DKC1_CART2P_LISTA");
  if (!path || !*path)
    return;
  FILE *f = fopen(path, "r");
  if (!f) {
    Cart2pLog("lista de etapas: no se pudo leer %s", path);
    return;
  }
  char base_dir[512];
  snprintf(base_dir, sizeof base_dir, "%s", path);
  base_dir[BaseName(base_dir) - base_dir] = 0; /* keeps the separator */
  char line[1200];
  while (s_list_count < kMaxStages && fgets(line, sizeof line, f)) {
    Trim(line);
    if (!line[0] || line[0] == '#')
      continue;
    char *fields[3] = {line, NULL, NULL};
    int n = 1;
    for (char *c = line; *c && n < 3; c++)
      if (*c == '|') {
        *c = 0;
        fields[n++] = c + 1;
      }
    if (n < 2)
      continue;
    for (int i = 0; i < n; i++)
      Trim(fields[i]);
    StageEntry *e = &s_list[s_list_count];
    snprintf(e->name, sizeof e->name, "%s", fields[0]);
    for (char *c = e->name; *c; c++) {
      if (*c >= 'a' && *c <= 'z')
        *c = (char)(*c - 'a' + 'A');
      else if (*c != ' ' && !GlyphRows(*c))
        *c = ' ';
    }
    ResolvePath(base_dir, fields[1], e->state, sizeof e->state);
    ResolvePath(base_dir, n > 2 ? fields[2] : "", e->stage, sizeof e->stage);
    s_list_count++;
  }
  fclose(f);
  Cart2pLog("lista de etapas: %d etapas en %s", s_list_count, path);
}

/* A stage of the list becomes the race start (A loaded afterwards). */
static bool SwitchStage(int index) {
  if (index < 0 || index >= s_list_count)
    return false;
  const StageEntry *e = &s_list[index];
  Cart2pStageRestoreRom(); /* an original stage after a remix */
  if (!RtlLoadSnapshot(e->state)) {
    Cart2pLog("etapa %s: no se pudo cargar %s", e->name, e->state);
    return false;
  }
  if (e->stage[0] && !Cart2pStageInstallFile(e->stage)) {
    Cart2pLog("etapa %s: no se pudo instalar %s", e->name, e->stage);
    return false;
  }
  s_size_start = Save(&s_start);
  if (!s_size_start)
    return false;
  s_stage_index = s_next_index = index;
  MakeStageKey(e->state, e->stage);
  s_exit_x = Cart2pStageExitX();
  Cart2pLog("etapa %d/%d: %s (meta en X=$%04X, mejor tiempo %u frames)",
            index + 1, s_list_count, e->name, s_exit_x, BestTime());
  return RestartRace();
}

/* Countdown: nothing runs; B is drawn as it is (A is drawn by the host).
 * B's SPC still advances one frame, like A's, so both stay sample-aligned
 * and B's sound is heard: its state is saved again with the SPC progress. */
static void DrawFrozenB(bool advance_audio) {
  if (!s_has_b || s_solo || s_skip_b_draw || s_phase == kRaceEditor)
    return;
  const size_t size_a = Save(&s_state_a);
  if (!size_a || !LoadState(s_state_b, s_size_b))
    return;
  if (advance_audio) {
    AdvanceAudioOneFrame();
    CaptureAudioB();
  }
  DrawRight();
  if (advance_audio) {
    const size_t n = Save(&s_state_b);
    if (n)
      s_size_b = n;
  }
  LoadState(s_state_a, size_a);
  if (advance_audio)
    MixAudioB();
}

bool Cart2pDualRunFrame(uint32_t inputs) {
  if (!Cart2pDual())
    return false;
  if (s_failed)
    return true;
  if (s_bot < 0) {
    s_bot = EnvOn("DKC1_CART2P_BOT");
    s_verbose = EnvOn("DKC1_CART2P_LOG");
    s_sudden_death = EnvOn("DKC1_CART2P_MUERTE");
    if (s_sudden_death)
      Cart2pLog("reglas: muerte subita (el primero que muere pierde)");
    const char *t = getenv("DKC1_CART2P_TEST_FINISH");
    s_test_finish = t ? (uint32_t)strtoul(t, NULL, 10) : 0;
    s_test_nocount = EnvOn("DKC1_CART2P_TEST_NOCOUNT");
    AudioInit();
    LoadOptions();
  }
  s_b_count = 0;
  if (s_notice_frames > 0)
    s_notice_frames--;
  s_skip_b_draw = false;
  if (!s_start) {
    /* First frame: the race start is the first stage of the list, or the
     * loaded state (plus the custom stage). */
    LoadStageList();
    bool ok;
    if (s_list_count) {
      ok = SwitchStage(0);
    } else {
      /* Survey after the install: the profile of an installed stage is how
       * new stages are checked (the generator rejects it as a source). */
      Cart2pStageExportGraphics(); /* original level, before the install */
      ExportThemes();
      const bool installed = Cart2pStageInstall();
      {
        const char *skin = getenv("DKC1_CART2P_PIEL");
        if (installed && skin && *skin)
          Cart2pStageInstallSkin(skin);
      }
      Cart2pStageSurvey();
      s_size_start = Save(&s_start);
      MakeStageKey(getenv("DKC1_SAVESTATE_INPUT"),
                   installed ? getenv("DKC1_CART2P_STAGE") : NULL);
      s_exit_x = Cart2pStageExitX();
      Cart2pLog("etapa %s: meta en X=$%04X, mejor tiempo %u frames",
                s_stage_key, s_exit_x, BestTime());
      ok = s_size_start != 0 && RestartRace();
    }
    if (!ok) {
      Cart2pLog("dos partidas: no se pudo preparar la carrera");
      s_failed = true;
      return true;
    }
    Cart2pLog("dos partidas: B creada como copia de A (%zu bytes de estado)",
              s_size_start);
    /* DKC1_CART2P_RECETAS_PRUEBA=dir: compile every recipe with the C
     * compiler into dir (test against tools/cart2p/compilar_etapas.py). */
    {
      const char *dir = getenv("DKC1_CART2P_RECETAS_PRUEBA");
      if (dir && *dir) {
        static char ids[400][48];
        const int n = RecListar(ids, 400);
        int ok = 0;
        static Receta r;
        for (int i = 0; i < n; i++) {
          const Nivel *nv;
          size_t size = 0;
          uint8_t *data;
          if (!RecCargar(ids[i], &r) || !(nv = RecNivel(r.nivel)) ||
              !(data = RecCompilar(&r, nv, &size)))
            continue;
          char path[1200];
          snprintf(path, sizeof path, "%s/%s.c2p", dir, ids[i]);
          FILE *f = fopen(path, "wb");
          if (f) {
            fwrite(data, 1, size, f);
            fclose(f);
            ok++;
          }
          free(data);
        }
        Cart2pLog("recetas: %d de %d compiladas en C -> %s", ok, n, dir);
      }
    }
    if (EnvOn("DKC1_CART2P_INICIO")) /* headless test of the start screen */
      s_bind_requested = true;
    if (s_bind_requested && !s_bot && !s_test_finish && !s_test_nocount) {
      s_phase = kRaceTitle;
      s_phase_frames = 0;
      Cart2pLog("inicio: pantalla de bienvenida");
    }
  }

  s_phase_frames++;
  {
    static uint32_t guion_frame;
    GuionEditor(++guion_frame);
  }
  const uint32_t raw1 = inputs & 0xfffu, raw2 = (inputs >> 12) & 0xfffu;
  uint32_t press =
      (raw1 & ~s_players[0].prev_pad) | (raw2 & ~s_players[1].prev_pad);
  s_players[0].prev_pad = raw1;
  s_players[1].prev_pad = raw2;
  if (s_phase == kRaceTitle) {
    if (press & (kPadStart | kPadB) && s_phase_frames > 20) {
      s_phase = kRaceMain;
      s_phase_frames = 0;
      press = 0;
      Cart2pLog("inicio: menu principal");
    }
    Dsp *dsp = LoadedDsp();
    s_a_write0 = dsp ? dsp->sampleWrite : 0;
    AdvanceAudioOneFrame();
    DrawFrozenB(true);
    return true;
  }
  if (s_phase == kRaceMain) {
    PollRandomizer();
    if (!s_azar_corriendo && s_phase_frames > 10) {
      if (press & kPadDown)
        s_main_cursor = (s_main_cursor + 1) % kMainCount;
      if (press & kPadUp)
        s_main_cursor = (s_main_cursor + kMainCount - 1) % kMainCount;
      const int d = (press & kPadRight) ? 1 : (press & kPadLeft) ? -1 : 0;
      if (d) {
        if (s_main_cursor == kMainAzar)
          s_azar_cantidad = s_azar_cantidad + d < 1 ? 1 :
                            s_azar_cantidad + d > 30 ? 30 : s_azar_cantidad + d;
        if (s_main_cursor == kMainVolumen1 || s_main_cursor == kMainVolumen2) {
          int *g = s_main_cursor == kMainVolumen1 ? &s_gain_a : &s_gain_b;
          *g = *g + 10 * d < 0 ? 0 : *g + 10 * d > 100 ? 100 : *g + 10 * d;
          SaveOptions();
        }
        if (s_main_cursor == kMainMuerte) {
          s_sudden_death = s_sudden_death > 0 ? 0 : 1;
          SaveOptions();
        }
      }
      if ((press & (kPadStart | kPadB)) || s_guion_confirma) {
        s_guion_confirma = false;
        switch (s_main_cursor) {
        case kMainSolo:
          SetSolo(true);
          s_phase = s_list_count > 1 ? kRaceSelect : kRaceCountdown;
          s_phase_frames = 0;
          s_select_cursor = s_stage_index;
          s_select_after_race = false;
          press = 0;
          Cart2pLog("inicio: 1 jugador");
          break;
        case kMainJugar:
          s_phase = kRaceBind;
          s_phase_frames = 0;
          s_bind_all_frames = 0;
          press = 0;
          Cart2pLog("inicio: pantalla de mandos");
          break;
        case kMainEditor:
          s_editor_prev_stage = s_stage_index;
          s_phase = kRaceEditor;
          s_phase_frames = 0;
          press = 0;
          Cart2pEditorAbrir();
          Cart2pLog("menu: editor de etapas");
          break;
        case kMainAzar:
          StartRandomizer();
          break;
        case kMainMuerte:
          s_sudden_death = s_sudden_death > 0 ? 0 : 1;
          SaveOptions();
          break;
        case kMainSalir:
          s_quit = true;
          Cart2pLog("menu: salir");
          break;
        }
      }
    }
    if (s_phase == kRaceMain) {
      Dsp *dsp = LoadedDsp();
      s_a_write0 = dsp ? dsp->sampleWrite : 0;
      AdvanceAudioOneFrame();
      DrawFrozenB(true);
      return true;
    }
  }
  if (s_phase == kRaceEditor) {
    char id[48];
    const int accion = Cart2pEditorCuadro(id, sizeof id);
    if (accion == 1) { /* leave: back to the stage loaded before */
      s_phase = kRaceMain;
      s_phase_frames = 0;
      if (s_list_count)
        SwitchStage(s_editor_prev_stage < s_list_count ? s_editor_prev_stage : 0);
      else
        RestartRace();
      s_phase = kRaceMain;
      Cart2pLog("editor: salida al menu principal");
    } else if (accion == 2) { /* play the saved stage now */
      int idx = -1;
      char clave[64];
      snprintf(clave, sizeof clave, "%s.c2p", id);
      for (int i = 0; i < s_list_count; i++) {
        const size_t ls = strlen(s_list[i].stage), lc = strlen(clave);
        if (ls >= lc && !strcmp(s_list[i].stage + ls - lc, clave))
          idx = i;
      }
      if (idx < 0) {
        Cart2pDualNotice("NO ESTA EN LA LISTA");
      } else {
        SetSolo(true);
        s_test_editor = true;
        if (SwitchStage(idx)) {
          s_phase = kRaceCountdown;
          s_phase_frames = 0;
          Cart2pLog("editor: prueba de %s", id);
        } else {
          VolverAlEditor();
        }
      }
      return true;
    }
    if (s_phase == kRaceEditor) {
      AdvanceAudioOneFrame();
      return true;
    }
  }
  if (s_phase == kRaceBind && (press & kPadSelect) && s_bind_requested) {
    s_phase = kRaceMain;
    s_phase_frames = 0;
    press = 0;
  }
  if (s_phase == kRaceBind) {
    s_bind_all_frames =
        s_bind_ready[0] && s_bind_ready[1] ? s_bind_all_frames + 1 : 0;
    if (((press & kPadStart) && s_phase_frames > 20) ||
        s_bind_all_frames >= kBindReadyFrames) {
      Cart2pLog("inicio: %s", s_bind_all_frames ? "ambos mandos listos"
                                                : "Start, se empieza");
      s_phase = s_list_count > 1 ? kRaceSelect : kRaceCountdown;
      s_phase_frames = 0;
      s_select_cursor = s_stage_index;
      s_select_after_race = false;
      press = 0; /* the same Start must not also pick a stage */
    } else {
      Dsp *dsp = LoadedDsp();
      s_a_write0 = dsp ? dsp->sampleWrite : 0;
      AdvanceAudioOneFrame(); /* the music plays on the start screen */
      DrawFrozenB(true);
      return true;
    }
  }
  if (s_phase == kRaceSelect && (press & (kPadA | kPadSelect)) &&
      s_bind_requested) {
    s_phase = kRaceMain;
    s_phase_frames = 0;
    press = 0;
    SetSolo(false);
    Cart2pLog("menu: vuelta al menu principal");
    Dsp *dsp = LoadedDsp();
    s_a_write0 = dsp ? dsp->sampleWrite : 0;
    AdvanceAudioOneFrame();
    DrawFrozenB(true);
    return true;
  }
  if (s_phase == kRaceSelect) {
    const int rows = s_list_count + 1; /* + "al azar" */
    /* pages: RB / right = first row of the next page (from the last page,
     * the first one), LB / left = first row of this page or the previous */
    const int pages = (rows + kSelectPage - 1) / kSelectPage;
    const int page = s_select_cursor / kSelectPage;
    if (press & (kPadRight | kPadR))
      s_select_cursor = (page + 1) % pages * kSelectPage;
    if (press & (kPadLeft | kPadL))
      s_select_cursor = s_select_cursor % kSelectPage
                            ? page * kSelectPage
                            : (page + pages - 1) % pages * kSelectPage;
    if (press & kPadDown)
      s_select_cursor = (s_select_cursor + 1) % rows;
    if (press & kPadUp)
      s_select_cursor = (s_select_cursor + rows - 1) % rows;
    if (press & (kPadStart | kPadB)) {
      int pick = s_select_cursor;
      if (pick == s_list_count) {
        if (!s_random_state)
          s_random_state = (unsigned)time(NULL) | 1u;
        s_random_state = s_random_state * 1103515245u + 12345u;
        pick = (int)((s_random_state >> 16) % (unsigned)s_list_count);
      }
      Cart2pLog("menu: etapa %d (%s)%s", pick + 1, s_list[pick].name,
                s_select_cursor == s_list_count ? " al azar" : "");
      if (s_select_after_race)
        s_races++;
      bool ok;
      if (pick == s_stage_index && !s_select_after_race)
        ok = true; /* already loaded and fresh: just count down */
      else
        ok = SwitchStage(pick);
      if (!ok)
        s_failed = true;
      s_phase = kRaceCountdown;
      s_phase_frames = 0;
      DrawFrozenB(false);
      return true;
    }
    Dsp *dsp = LoadedDsp();
    s_a_write0 = dsp ? dsp->sampleWrite : 0;
    AdvanceAudioOneFrame();
    DrawFrozenB(true);
    return true;
  }
  if (s_phase == kRaceCountdown) {
    if (s_bot || s_test_nocount) /* tests skip the wait */
      s_phase_frames = kCountdownFrames;
    if (s_phase_frames < kCountdownFrames) {
      Dsp *dsp = LoadedDsp();
      s_a_write0 = dsp ? dsp->sampleWrite : 0;
      AdvanceAudioOneFrame(); /* A's music keeps playing */
      DrawFrozenB(true);      /* and so does B's */
      return true;
    }
    s_phase = kRaceRunning;
    s_phase_frames = 0;
    Cart2pLog("carrera %d: YA!", s_races + 1);
  }
  if (s_phase == kRaceDone) {
    /* Winner banner (the other player may still be racing): Start opens
     * the stage menu (with a list) or restarts. Tests go on by themselves
     * to the next stage of the list. */
    const bool automatic = (s_bot || s_test_finish || s_test_nocount) &&
                           s_phase_frames >= kBannerFrames;
    if (automatic && s_list_count > 1)
      s_next_index = (s_stage_index + 1) % s_list_count;
    if (s_test_editor && (press & (kPadStart | kPadB))) {
      VolverAlEditor();
      return true;
    }
    if (!automatic && (press & kPadStart) && s_list_count > 1) {
      s_phase = kRaceSelect;
      s_phase_frames = 0;
      s_select_cursor = (s_stage_index + 1) % s_list_count;
      s_select_after_race = true;
      Dsp *dsp = LoadedDsp(); /* both SPCs advance, as on the menu */
      s_a_write0 = dsp ? dsp->sampleWrite : 0;
      AdvanceAudioOneFrame();
      DrawFrozenB(true);
      return true;
    }
    if (automatic || (press & kPadStart)) {
      s_races++;
      const bool ok = s_list_count ? SwitchStage(s_next_index) : RestartRace();
      if (!ok)
        s_failed = true;
      DrawFrozenB(false);
      return true;
    }
  }
  if (s_solo ? !s_players[0].finished
             : !(s_players[0].finished && s_players[1].finished))
    s_clock++;

  /* A runs with pad 1, or stays frozen once finished (its music goes on).
   * Each game gets its player's pad on both ports: in the 2P "team" file
   * mode ($42 = 1, CODE_BFB27C) Diddy reads port 2. */
  {
    const Dsp *dsp = LoadedDsp();
    s_a_write0 = dsp ? dsp->sampleWrite : 0;
  }
  if (s_players[0].finished) {
    AdvanceAudioOneFrame();
  } else {
    const uint32_t pad1 = BotInput(0, s_solo ? (raw1 | raw2) : raw1);
    RtlRunFrame(pad1 | pad1 << 12);
    if (g_fail || !Dkc1LastLleResult())
      return true; /* host reports A's failure; B stays frozen */
    RaceAfterFrame(0);
  }

  const double t0 = NowMs();
  const size_t size_a = Save(&s_state_a);
  if (!size_a) {
    Cart2pLog("dos partidas: no se pudo guardar el estado de A");
    s_failed = true;
    return true;
  }
  RaceAfterSave(0, s_state_a, size_a);
  if (s_solo)
    return true; /* 1 JUGADOR: only game A */

  bool ok = LoadState(s_state_b, s_size_b);
  if (ok) {
    if (s_players[1].finished) {
      AdvanceAudioOneFrame();
    } else {
      const uint32_t pad2 = BotInput(1, raw2);
      RtlRunFrame(pad2 | pad2 << 12);
      if (g_fail || !Dkc1LastLleResult()) {
        Cart2pLog("dos partidas: B se detuvo (resume=$%06X); queda congelada",
                  (unsigned)Dkc1ResumePc());
        g_fail = false;
        ok = false;
      } else {
        RaceAfterFrame(1);
      }
    }
    if (ok) {
      CaptureAudioB(); /* mixed into A's ring once A is back */
      DrawRight();
      const size_t n = Save(&s_state_b);
      if (n) {
        s_size_b = n;
        RaceAfterSave(1, s_state_b, n);
      } else {
        ok = false;
      }
    }
  } else {
    Cart2pLog("dos partidas: no se pudo cargar el estado de B");
  }
  if (!ok)
    s_failed = true;
  if (!LoadState(s_state_a, size_a)) {
    Cart2pLog("dos partidas: no se pudo recargar A");
    s_failed = true;
  } else {
    MixAudioB();
  }

  const double dt = NowMs() - t0;
  s_acc_ms += dt;
  if (dt > s_max_ms)
    s_max_ms = dt;
  if (++s_acc_frames == 600) {
    Cart2pLog("dos partidas: costo extra por frame %.2f ms promedio, "
              "%.2f ms maximo (600 frames)", s_acc_ms / 600.0, s_max_ms);
    s_acc_ms = 0;
    s_max_ms = 0;
    s_acc_frames = 0;
  }
  return true;
}
