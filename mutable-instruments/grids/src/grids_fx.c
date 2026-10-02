/* Grids (Mutable Instruments topographic drum sequencer) as a Schwung-style MIDI FX module, for the MPC through
 * steve/tools/midifx (its notes leave by the plugin's own MIDI port, clocked by MPC's transport).
 *
 * Port of grids/pattern_generator.cc (Copyright 2011-2012 Emilie Gillet, GPL-3.0-or-later) for the MPC,
 * steve/schwung-ports/grids, 2026-10-01: the same drum maps, map interpolation, chaos perturbation, Euclidean
 * mode and accent rule, rewritten with per-instance state (the original is a static, single-instance AVR class) and
 * MIDI notes in place of trigger outputs. This file is GPL-3.0-or-later too (see ../LICENSE).
 *
 * Three parts (BD, SD, HH) play notes BD/SD/HH NOTE; an accented hit uses ACCENT VEL, the rest NORMAL VEL. Steps
 * follow MIDI clock (24 PPQN from the adapter): RESOLUTION 1/16 reads every other step of the 32-step maps, as the
 * module does on a 4 PPQN clock; 1/32 reads them all (its 24 PPQN mode). Start lines the pattern up with MPC's bar
 * (host beat position, 4/4). SWING delays the off-beat sixteenths by 1-3 clock ticks. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "plugin_api_v1.h"
#include "midi_fx_api_v1.h"
#include "grids_maps.h"

#define NPARTS 3
#define STEPS 32

static const host_api_v1_t *g_host;

static const uint8_t *const drum_map[5][5] = {
  { node_10, node_8, node_0, node_9, node_11 },
  { node_15, node_7, node_13, node_12, node_6 },
  { node_18, node_14, node_4, node_5, node_3 },
  { node_23, node_16, node_21, node_1, node_2 },
  { node_24, node_19, node_17, node_20, node_22 },
};

static inline uint8_t mix8(uint8_t a, uint8_t b, uint8_t balance) {   /* avrlib U8Mix */
  return (uint8_t)((a * (255 - balance) + b * balance) >> 8);
}
static inline uint8_t mul8(uint8_t a, uint8_t b) { return (uint8_t)((a * b) >> 8); }   /* U8U8MulShift8 */

/* ---- parameters ------------------------------------------------------------------------------------------------ */
enum { P_MODE, P_X, P_Y, P_CHAOS, P_BD, P_SD, P_HH, P_LEN1, P_LEN2, P_LEN3, P_RES, P_SWING, P_BD_NOTE, P_SD_NOTE,
       P_HH_NOTE, P_ACCENT, P_NORMAL, P_CHANNEL, P_COUNT };
typedef struct { const char *key; int min, max, def; const char *const *opts; } param_t;
static const char *const k_mode[] = {"DRUMS", "EUCLIDEAN"};
static const char *const k_res[] = {"1/16", "1/32"};
static const char *const k_swing[] = {"OFF", "LIGHT", "MEDIUM", "HEAVY"};
static const param_t k_params[P_COUNT] = {
  {"mode", 0, 1, 0, k_mode}, {"map_x", 0, 255, 128, 0}, {"map_y", 0, 255, 128, 0}, {"chaos", 0, 255, 0, 0},
  {"bd_fill", 0, 255, 128, 0}, {"sd_fill", 0, 255, 128, 0}, {"hh_fill", 0, 255, 128, 0},
  {"len_bd", 1, 32, 16, 0}, {"len_sd", 1, 32, 16, 0}, {"len_hh", 1, 32, 16, 0},
  {"resolution", 0, 1, 0, k_res}, {"swing", 0, 3, 0, k_swing},
  {"bd_note", 0, 127, 36, 0}, {"sd_note", 0, 127, 38, 0}, {"hh_note", 0, 127, 42, 0},
  {"accent_vel", 1, 127, 127, 0}, {"normal_vel", 1, 127, 90, 0}, {"channel", 1, 16, 1, 0},
};

typedef struct { int tick, note, on, vel; } pending_t;   /* a note event due at clock tick `tick` */

typedef struct {
  int v[P_COUNT];
  uint16_t rng;
  int running;
  long tick;          /* clock ticks since Start */
  int step;           /* 0..31, the maps' step */
  uint8_t euclid[NPARTS];
  uint8_t perturb[NPARTS];
  pending_t pend[32];
  int npend;
} grids_t;

static uint8_t rng_byte(grids_t *g) {   /* avrlib Random: 16-bit Galois LFSR, its high byte */
  g->rng = (uint16_t)((g->rng >> 1) ^ (-(g->rng & 1) & 0xb400));
  return (uint8_t)(g->rng >> 8);
}

static uint8_t read_map(int step, int part, uint8_t x, uint8_t y) {   /* PatternGenerator::ReadDrumMap */
  int i = x >> 6, j = y >> 6, off = part * STEPS + step;
  uint8_t a = drum_map[i][j][off], b = drum_map[i + 1][j][off];
  uint8_t c = drum_map[i][j + 1][off], d = drum_map[i + 1][j + 1][off];
  return mix8(mix8(a, b, (uint8_t)(x << 2)), mix8(c, d, (uint8_t)(x << 2)), (uint8_t)(y << 2));
}

/* ---- note output ----------------------------------------------------------------------------------------------- */
static void schedule(grids_t *g, int tick, int note, int on, int vel) {
  if (g->npend < 32) g->pend[g->npend++] = (pending_t){tick, note, on, vel};
}

static int flush(grids_t *g, long now, int all, uint8_t out[][3], int lens[], int max, int n) {
  int ch = (g->v[P_CHANNEL] - 1) & 15, j = 0;
  for (int i = 0; i < g->npend; ++i) {
    pending_t e = g->pend[i];
    if ((all || e.tick <= now) && n < max && (e.on ? !all : 1)) {
      out[n][0] = (uint8_t)((e.on ? 0x90 : 0x80) | ch);
      out[n][1] = (uint8_t)e.note;
      out[n][2] = (uint8_t)(e.on ? e.vel : 0);
      lens[n++] = 3;
    } else if (!all) {
      g->pend[j++] = e;
    }
  }
  g->npend = all ? 0 : j;
  return n;
}

/* One step of the pattern: which parts hit, and which are accented (PatternGenerator::Evaluate*). */
static void evaluate(grids_t *g, long now) {
  uint8_t hits = 0, accents = 0;
  if (g->v[P_MODE] == 0) {
    if (g->step == 0)
      for (int i = 0; i < NPARTS; ++i) g->perturb[i] = mul8(rng_byte(g), (uint8_t)(g->v[P_CHAOS] >> 2));
    for (int i = 0; i < NPARTS; ++i) {
      uint8_t level = read_map(g->step, i, (uint8_t)g->v[P_X], (uint8_t)g->v[P_Y]);
      level = level < 255 - g->perturb[i] ? (uint8_t)(level + g->perturb[i]) : 255;
      uint8_t threshold = (uint8_t)~g->v[P_BD + i];
      if (level > threshold) {
        hits |= 1 << i;
        if (level > 192) accents |= 1 << i;
      }
    }
  } else {
    if (g->step & 1) return;   /* Euclidean patterns move on sixteenths */
    for (int i = 0; i < NPARTS; ++i) {
      int length = g->v[P_LEN1 + i], density = g->v[P_BD + i] >> 3;
      while (g->euclid[i] >= length) g->euclid[i] -= length;
      if (lut_res_euclidean[(length - 1) * 32 + density] & (1u << g->euclid[i])) hits |= 1 << i;
    }
  }
  /* swing: the second sixteenth of each eighth comes 1-3 ticks late */
  int delay = ((g->step >> 1) & 1) ? g->v[P_SWING] : 0;
  for (int i = 0; i < NPARTS; ++i) {
    if (!(hits & (1 << i))) continue;
    int note = g->v[P_BD_NOTE + i], vel = (accents & (1 << i)) ? g->v[P_ACCENT] : g->v[P_NORMAL];
    schedule(g, (int)(now + delay), note, 1, vel);
    schedule(g, (int)(now + delay + 1), note, 0, 0);   /* a one-tick trigger */
  }
}

static void advance(grids_t *g, int steps) {
  for (int s = 0; s < steps; ++s) {
    if (!(g->step & 1))
      for (int i = 0; i < NPARTS; ++i) ++g->euclid[i];
    g->step = (g->step + 1) % STEPS;
  }
}

/* ---- midi_fx_api_v1 -------------------------------------------------------------------------------------------- */
static void *create(const char *dir, const char *cfg) {
  (void)dir; (void)cfg;
  grids_t *g = calloc(1, sizeof *g);
  if (!g) return NULL;
  for (int i = 0; i < P_COUNT; ++i) g->v[i] = k_params[i].def;
  g->rng = 0x2101;
  return g;
}

static void destroy(void *p) { free(p); }

static int process_midi(void *p, const uint8_t *m, int len, uint8_t out[][3], int lens[], int max) {
  grids_t *g = p;
  int n = 0;
  if (len < 1) return 0;
  if (m[0] == 0xFA) {   /* Start: line up with the host's bar */
    double beat = g_host && g_host->get_beat_position ? g_host->get_beat_position() : 0.0;
    g->running = 1;
    g->tick = 0;
    g->step = beat > 0 ? (int)(fmod(beat, 4.0) * 8.0) % STEPS : 0;
    if (g->v[P_RES] == 0) g->step &= ~1;
    memset(g->euclid, 0, sizeof g->euclid);
    g->npend = 0;
  } else if (m[0] == 0xFC) {
    g->running = 0;
    n = flush(g, 0, 1, out, lens, max, n);   /* pending note-offs, now */
  } else if (m[0] == 0xF8 && g->running) {
    int every = g->v[P_RES] == 0 ? 6 : 3;     /* MIDI ticks per evaluation: 1/16 or 1/32 */
    if (g->tick % every == 0) {
      evaluate(g, g->tick);
      advance(g, g->v[P_RES] == 0 ? 2 : 1);
    }
    n = flush(g, g->tick, 0, out, lens, max, n);
    ++g->tick;
  }
  return n;   /* notes played into the track are not passed on: Grids makes its own */
}

static int tick(void *p, int frames, int sr, uint8_t out[][3], int lens[], int max) {
  (void)p; (void)frames; (void)sr; (void)out; (void)lens; (void)max;
  return 0;
}

static void set_param(void *p, const char *key, const char *val) {
  grids_t *g = p;
  for (int i = 0; i < P_COUNT; ++i) {
    if (strcmp(key, k_params[i].key)) continue;
    int v = -1;
    if (k_params[i].opts)
      for (int o = 0; o <= k_params[i].max && v < 0; ++o)
        if (!strcasecmp(val, k_params[i].opts[o])) v = o;
    if (v < 0) v = (int)lround(atof(val));
    g->v[i] = v < k_params[i].min ? k_params[i].min : (v > k_params[i].max ? k_params[i].max : v);
    return;
  }
  if (!strcmp(key, "state")) {   /* "key=value;..." */
    char tmp[1024], *save = NULL;
    snprintf(tmp, sizeof tmp, "%s", val);
    for (char *kv = strtok_r(tmp, ";", &save); kv; kv = strtok_r(NULL, ";", &save)) {
      char *eq = strchr(kv, '=');
      if (eq && strncmp(kv, "state", 5)) { *eq = 0; set_param(p, kv, eq + 1); }
    }
  }
}

static int get_param(void *p, const char *key, char *buf, int len) {
  grids_t *g = p;
  for (int i = 0; i < P_COUNT; ++i)
    if (!strcmp(key, k_params[i].key))
      return k_params[i].opts ? snprintf(buf, len, "%s", k_params[i].opts[g->v[i]]) : snprintf(buf, len, "%d", g->v[i]);
  if (!strcmp(key, "state")) {
    int n = 0;
    for (int i = 0; i < P_COUNT && n < len; ++i) {
      char v[24];
      get_param(p, k_params[i].key, v, sizeof v);
      n += snprintf(buf + n, len - n, "%s%s=%s", i ? ";" : "", k_params[i].key, v);
    }
    return n;
  }
  return -1;
}

static midi_fx_api_v1_t g_api = {MIDI_FX_API_VERSION, create, destroy, process_midi, tick, set_param, get_param};

midi_fx_api_v1_t *move_midi_fx_init(const host_api_v1_t *host) {
  g_host = host;
  return &g_api;
}
