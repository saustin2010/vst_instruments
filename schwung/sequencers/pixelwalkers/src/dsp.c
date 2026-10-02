#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

typedef struct host_api_v1 host_api_v1_t;
typedef struct midi_fx_api_v1 {
  uint32_t api_version;
  void *(*create_instance)(const char *, const char *);
  void (*destroy_instance)(void *);
  int (*process_midi)(void *, const uint8_t *, int, uint8_t (*)[3], int *, int);
  int (*tick)(void *, int, int, uint8_t (*)[3], int *, int);
  void (*set_param)(void *, const char *, const char *);
  int (*get_param)(void *, const char *, char *, int);
} midi_fx_api_v1_t;

#define MAX_WALKERS 16
#define MAX_PLATFORMS 6
#define MAX_PENDING_OFFS 128
#define VIEW_W 128
#define VIEW_H 43
#define WALKER_W 6
#define WALKER_H 9

static uint32_t instance_serial;

typedef struct {
  float x, y, vy;
  float bounce, hardness, series_level, fall_origin_y;
  uint8_t note, velocity, channel, active, id, hit_count, rebound_count;
  uint8_t rebounding, landed_once;
  int8_t dir, surface, rebound_surface, series_surface;
} walker_t;

typedef struct { uint8_t x, y, w; } platform_t;
typedef struct {
  int64_t samples_left;
  uint8_t note, channel, active;
} pending_off_t;

typedef struct {
  walker_t walkers[MAX_WALKERS];
  platform_t platforms[MAX_PLATFORMS];
  pending_off_t offs[MAX_PENDING_OFFS];
  uint32_t rng, spawn_serial, snapshot_serial, snapshot_samples;
  uint32_t instance_id;
  int sample_rate;
  uint8_t platform_count;
  uint8_t birth_note;
  uint8_t kill_pending;
  uint8_t active_notes[16][16];
  float birth_level, hit_level, hit_decay, bounce, hardness;
  char viz[2][256];
  volatile int viz_index;
} pixel_walkers_t;

static uint32_t next_random(pixel_walkers_t *s) {
  s->rng = s->rng * 1664525u + 1013904223u;
  return s->rng;
}

static void make_platforms(pixel_walkers_t *s) {
  s->platform_count = 0;
  /* Three vertically separated pairs. The lower member of each pair is
   * deliberately offset but horizontally overlapping, so walkers can leave
   * an upper ledge and fall onto another platform instead of every platform
   * being arranged in one left-to-right strip. */
  for (int pair = 0; pair < MAX_PLATFORMS / 2; ++pair) {
    int zone_left = 2 + pair * (VIEW_W - 4) / 3;
    int zone_right = 2 + (pair + 1) * (VIEW_W - 4) / 3;
    int upper_w = 13 + (int)(next_random(s) % 15u);
    int upper_span = zone_right - zone_left - upper_w;
    if (upper_span < 1) upper_span = 1;
    int upper_x = zone_left + (int)(next_random(s) % (uint32_t)upper_span);
    int upper_y = 11 + (int)(next_random(s) % 7u);

    platform_t *upper = &s->platforms[s->platform_count++];
    upper->x = (uint8_t)upper_x;
    upper->y = (uint8_t)upper_y;
    upper->w = (uint8_t)upper_w;

    int lower_w = 13 + (int)(next_random(s) % 15u);
    int offset = (int)(next_random(s) % 17u) - 8;
    int lower_x = upper_x + offset;
    if (lower_x < 2) lower_x = 2;
    if (lower_x + lower_w > VIEW_W - 2) lower_x = VIEW_W - 2 - lower_w;
    int lower_y = upper_y + 10 + (int)(next_random(s) % 10u);
    if (lower_y > VIEW_H - 5) lower_y = VIEW_H - 5;

    platform_t *lower = &s->platforms[s->platform_count++];
    lower->x = (uint8_t)lower_x;
    lower->y = (uint8_t)lower_y;
    lower->w = (uint8_t)lower_w;
  }
}

static void append_hex_byte(char *dst, size_t cap, size_t *used, unsigned value) {
  static const char hex[] = "0123456789ABCDEF";
  if (*used + 2 >= cap) return;
  dst[(*used)++] = hex[(value >> 4) & 15u];
  dst[(*used)++] = hex[value & 15u];
  dst[*used] = '\0';
}

static void append_hex_u32(char *dst, size_t cap, size_t *used, uint32_t value) {
  append_hex_byte(dst, cap, used, value >> 24);
  append_hex_byte(dst, cap, used, value >> 16);
  append_hex_byte(dst, cap, used, value >> 8);
  append_hex_byte(dst, cap, used, value);
}

/* Publish an immutable compact frame for the UI. The audio thread writes the
 * inactive buffer and flips one index; the UI never reads half a physics step. */
static void update_snapshot(pixel_walkers_t *s) {
  int next = 1 - s->viz_index;
  char *dst = s->viz[next];
  size_t used = 0;
  dst[used++] = 'I';
  append_hex_u32(dst, sizeof(s->viz[next]), &used, s->instance_id);
  dst[used++] = 'T';
  append_hex_u32(dst, sizeof(s->viz[next]), &used, s->snapshot_serial++);
  dst[used++] = 'P';
  append_hex_byte(dst, sizeof(s->viz[next]), &used, s->platform_count);
  for (uint8_t i = 0; i < s->platform_count; ++i) {
    append_hex_byte(dst, sizeof(s->viz[next]), &used, s->platforms[i].x);
    append_hex_byte(dst, sizeof(s->viz[next]), &used, s->platforms[i].y);
    append_hex_byte(dst, sizeof(s->viz[next]), &used, s->platforms[i].w);
  }
  dst[used++] = 'W';
  unsigned count_at = (unsigned)used;
  append_hex_byte(dst, sizeof(s->viz[next]), &used, 0);
  unsigned count = 0;
  for (int i = 0; i < MAX_WALKERS; ++i) {
    const walker_t *w = &s->walkers[i];
    if (!w->active) continue;
    int xi = (int)(w->x + 0.5f);
    int yi = (int)(w->y + 0.5f);
    if (xi < 0) xi = 0; else if (xi > 255) xi = 255;
    if (yi < 0) yi = 0; else if (yi > 255) yi = 255;
    int vi = (int)(w->vy + (w->vy >= 0.0f ? 0.5f : -0.5f));
    if (vi < -127) vi = -127; else if (vi > 127) vi = 127;
    int encoded_surface = w->rebounding ? w->rebound_surface : w->surface;
    unsigned motion = (w->dir < 0 ? 0x80u : 0u)
                    | (w->rebounding ? 0x40u : 0u)
                    | (encoded_surface == -2 ? 0u
                       : (encoded_surface < 0 ? 1u
                          : (unsigned)encoded_surface + 2u));
    append_hex_byte(dst, sizeof(s->viz[next]), &used, w->id);
    append_hex_byte(dst, sizeof(s->viz[next]), &used, (unsigned)xi);
    append_hex_byte(dst, sizeof(s->viz[next]), &used, (unsigned)yi);
    append_hex_byte(dst, sizeof(s->viz[next]), &used, (unsigned)(uint8_t)vi);
    append_hex_byte(dst, sizeof(s->viz[next]), &used, motion);
    /* Pack both physics controls into one byte to keep a maximum-size frame
     * within the fixed 256-byte host parameter buffer. */
    unsigned bounce = (unsigned)(w->bounce * 15.0f + 0.5f);
    unsigned hardness = (unsigned)(w->hardness * 15.0f + 0.5f);
    append_hex_byte(dst, sizeof(s->viz[next]), &used,
                    (bounce << 4) | hardness);
    ++count;
  }
  dst[count_at] = "0123456789ABCDEF"[(count >> 4) & 15u];
  dst[count_at + 1] = "0123456789ABCDEF"[count & 15u];
  __sync_synchronize();
  s->viz_index = next;
}

static walker_t *spawn_walker(pixel_walkers_t *s, uint8_t note, uint8_t velocity,
                              uint8_t channel) {
  int slot = -1;
  for (int i = 0; i < MAX_WALKERS; ++i) {
    if (!s->walkers[i].active) { slot = i; break; }
  }
  if (slot < 0) slot = (int)(s->spawn_serial % MAX_WALKERS);
  walker_t *w = &s->walkers[slot];
  memset(w, 0, sizeof(*w));
  w->active = 1;
  w->x = (float)(5 + (s->spawn_serial * 19u) % (VIEW_W - 12));
  w->y = 0.0f;
  w->note = note;
  w->velocity = velocity ? velocity : 100;
  w->channel = channel & 0x0f;
  w->id = (uint8_t)s->spawn_serial;
  w->dir = (s->spawn_serial & 1u) ? -1 : 1;
  w->surface = -2;
  w->rebound_surface = -2;
  w->series_surface = -2;
  w->fall_origin_y = (float)WALKER_H;
  w->bounce = s->bounce;
  w->hardness = s->hardness;
  ++s->spawn_serial;
  update_snapshot(s);
  return w;
}

static int schedule_off(pixel_walkers_t *s, uint8_t note, uint8_t channel,
                        int sample_rate) {
  /* Re-triggering the same note (especially during a short bounce) extends
   * its gate. An older pending off must not silence the newer hit. */
  for (int i = 0; i < MAX_PENDING_OFFS; ++i) {
    if (s->offs[i].active && s->offs[i].note == note &&
        s->offs[i].channel == channel) {
      s->offs[i].samples_left = (int64_t)sample_rate * 140 / 1000;
      return 1;
    }
  }
  for (int i = 0; i < MAX_PENDING_OFFS; ++i) {
    if (!s->offs[i].active) {
      s->offs[i].active = 1;
      s->offs[i].note = note;
      s->offs[i].channel = channel;
      s->offs[i].samples_left = (int64_t)sample_rate * 140 / 1000;
      return 1;
    }
  }
  return 0;
}

static int note_is_active(const pixel_walkers_t *s, uint8_t note,
                          uint8_t channel) {
  return (s->active_notes[channel & 0x0f][note >> 3] &
          (uint8_t)(1u << (note & 7u))) != 0;
}

static void set_note_active(pixel_walkers_t *s, uint8_t note, uint8_t channel,
                            int active) {
  uint8_t *bits = &s->active_notes[channel & 0x0f][note >> 3];
  uint8_t mask = (uint8_t)(1u << (note & 7u));
  if (active) *bits |= mask;
  else *bits &= (uint8_t)~mask;
}

static int emit_note_on(pixel_walkers_t *s, const walker_t *w, uint8_t velocity,
                        int sample_rate,
                        uint8_t out[][3], int *lens, int count, int max) {
  if (velocity == 0) return count;
  /* A retrigger is always articulated as OFF then ON. Besides sounding more
   * deterministic across synths, this repairs a downstream voice even if an
   * earlier scheduled off was missed. */
  if (note_is_active(s, w->note, w->channel)) {
    if (count >= max) return count;
    out[count][0] = (uint8_t)(0x80 | (w->channel & 0x0f));
    out[count][1] = w->note;
    out[count][2] = 0;
    lens[count] = 3;
    ++count;
    set_note_active(s, w->note, w->channel, 0);
  }
  if (count >= max) return count;
  /* Never create a voice unless its eventual release has storage reserved. */
  if (!schedule_off(s, w->note, w->channel, sample_rate)) return count;
  out[count][0] = (uint8_t)(0x90 | (w->channel & 0x0f));
  out[count][1] = w->note;
  out[count][2] = velocity;
  lens[count] = 3;
  set_note_active(s, w->note, w->channel, 1);
  return count + 1;
}

static void *create_instance(const char *module_dir, const char *json) {
  (void)json;
  pixel_walkers_t *s = (pixel_walkers_t *)calloc(1, sizeof(*s));
  if (!s) return NULL;
  s->rng = 0x57414c4bu;
  if (module_dir) {
    for (const unsigned char *p = (const unsigned char *)module_dir; *p; ++p)
      s->rng = (s->rng ^ *p) * 16777619u;
  }
  make_platforms(s);
  /* malloc commonly returns the same address when a slot is removed and
   * immediately re-added. An address-derived ID then collides while the frame
   * serial restarts at zero, so the cached canvas rejects every new frame as
   * stale. Time distinguishes library reloads; the counter guarantees distinct
   * IDs for rapid creates within one loaded image. */
  struct timeval now;
  gettimeofday(&now, NULL);
  uint32_t unique = __sync_add_and_fetch(&instance_serial, 0x9e3779b9u);
  s->instance_id = (uint32_t)(uintptr_t)s ^ s->rng ^ unique ^
                   (uint32_t)now.tv_sec ^ ((uint32_t)now.tv_usec * 16777619u);
  s->sample_rate = 44100;
  s->birth_note = 1;
  s->birth_level = 1.0f;
  s->hit_level = 0.7f;
  s->hit_decay = 0.01f;
  s->bounce = 0.7f;
  s->hardness = 1.0f;
  update_snapshot(s);
  return s;
}

static void destroy_instance(void *instance) { free(instance); }

static int process_midi(void *instance, const uint8_t *in, int len,
                        uint8_t out[][3], int *lens, int max) {
  pixel_walkers_t *s = (pixel_walkers_t *)instance;
  if (!s || !in || len < 1) return 0;
  uint8_t kind = in[0] & 0xf0;
  if (len >= 3 && (kind == 0x90 || kind == 0x80)) {
    if (kind == 0x90 && in[2] > 0) {
      walker_t *w = spawn_walker(s, in[1], in[2], in[0] & 0x0f);
      if (s->birth_note && out && lens && max > 0) {
        int velocity = (int)((float)w->velocity * s->birth_level + 0.5f);
        return emit_note_on(s, w, (uint8_t)velocity, s->sample_rate,
                            out, lens, 0, max);
      }
    }
    return 0;
  }
  if (!out || !lens || max < 1) return 0;
  out[0][0] = in[0];
  out[0][1] = len > 1 ? in[1] : 0;
  out[0][2] = len > 2 ? in[2] : 0;
  lens[0] = len;
  return 1;
}

static int tick(void *instance, int frames, int sample_rate,
                uint8_t out[][3], int *lens, int max) {
  pixel_walkers_t *s = (pixel_walkers_t *)instance;
  if (!s || frames <= 0 || sample_rate <= 0) return 0;
  s->sample_rate = sample_rate;
  int count = 0;
  if (s->kill_pending) {
    int notes_left = 0;
    for (int channel = 0; channel < 16; ++channel) {
      for (int note = 0; note < 128; ++note) {
        if (!note_is_active(s, (uint8_t)note, (uint8_t)channel)) continue;
        if (count < max) {
          out[count][0] = (uint8_t)(0x80 | channel);
          out[count][1] = (uint8_t)note;
          out[count][2] = 0;
          lens[count] = 3;
          ++count;
          set_note_active(s, (uint8_t)note, (uint8_t)channel, 0);
        } else {
          notes_left = 1;
        }
      }
    }
    if (!notes_left) {
      s->kill_pending = 0;
      memset(s->offs, 0, sizeof(s->offs));
    }
  }
  for (int i = 0; i < MAX_PENDING_OFFS; ++i) {
    pending_off_t *off = &s->offs[i];
    if (!off->active) continue;
    off->samples_left -= frames;
    if (off->samples_left <= 0 && count < max) {
      out[count][0] = (uint8_t)(0x80 | (off->channel & 0x0f));
      out[count][1] = off->note;
      out[count][2] = 0;
      lens[count] = 3;
      ++count;
      off->active = 0;
      set_note_active(s, off->note, off->channel, 0);
    }
  }

  float dt = (float)frames / (float)sample_rate;
  if (dt > 0.05f) dt = 0.05f;
  for (int i = 0; i < MAX_WALKERS; ++i) {
    walker_t *w = &s->walkers[i];
    if (!w->active) continue;
    if (w->surface != -2) {
      w->x += (float)w->dir * 15.0f * dt;
      if (w->surface >= 0) {
        platform_t *support = &s->platforms[(uint8_t)w->surface];
        float foot = w->dir > 0 ? w->x + WALKER_W - 1 : w->x;
        if (foot < support->x || foot > support->x + support->w) {
          w->fall_origin_y = (float)support->y;
          w->surface = -2;
          w->vy = 0.0f;
        }
      }
      if (w->x < -WALKER_W || w->x > VIEW_W) w->active = 0;
      continue;
    }

    float old_bottom = w->y + WALKER_H;
    w->vy += 42.0f * dt;
    w->y += w->vy * dt;
    float new_bottom = w->y + WALKER_H;
    int landing = -2;
    float landing_y = (float)(VIEW_H - 1);
    for (uint8_t j = 0; j < s->platform_count; ++j) {
      platform_t *p = &s->platforms[j];
      if (w->rebounding && w->rebound_surface != (int8_t)j) continue;
      /* Strictly above, not merely touching. After walking off an edge the
       * walker's feet are exactly level with its old platform for one frame
       * while its body still overlaps horizontally. <= therefore re-landed
       * it on the platform it had just left and emitted 2-3 rapid notes. */
      if (w->x + WALKER_W >= p->x && w->x <= p->x + p->w &&
          old_bottom < p->y && new_bottom >= p->y && w->vy > 0.0f &&
          p->y < landing_y) {
        landing = j;
        landing_y = p->y;
      }
    }
    if (landing == -2 && (!w->rebounding || w->rebound_surface == -1) &&
        old_bottom <= VIEW_H - 1 &&
        new_bottom >= VIEW_H - 1) {
      landing = -1;
      landing_y = (float)(VIEW_H - 1);
    }
    if (landing != -2) {
      w->y = landing_y - WALKER_H;
      int new_surface = w->series_surface != landing;
      if (new_surface) {
        float impact_scale = 1.0f;
        if (w->landed_once) {
          /* Twenty pixels is a full-strength fall. A typical ten-pixel step
           * therefore begins its new echo series at half level. */
          impact_scale = (landing_y - w->fall_origin_y) / 20.0f;
          if (impact_scale < 0.0f) impact_scale = 0.0f;
          if (impact_scale > 1.0f) impact_scale = 1.0f;
        }
        w->series_level = s->hit_level * impact_scale;
        w->hit_count = 0;
        w->rebound_count = 0;
        w->series_surface = (int8_t)landing;
        w->landed_once = 1;
      } else if (w->rebounding && w->rebound_count < 255) {
        ++w->rebound_count;
      }

      float level = w->series_level - s->hit_decay * (float)w->hit_count;
      int velocity = level > 0.0f
                   ? (int)((float)w->velocity * level + 0.5f) : 0;
      if (velocity > 127) velocity = 127;
      count = emit_note_on(s, w, (uint8_t)velocity, sample_rate,
                           out, lens, count, max);
      if (w->hit_count < 255) ++w->hit_count;

      /* Bounce sets the first rebound height. Hardness controls retained
       * energy and caps the series from three rubbery rebounds to twenty
       * glassy ones. The shrinking flight time creates the accelerating echo. */
      int max_rebounds = 3 + (int)(17.0f * w->hardness + 0.5f);
      float coefficient = new_surface
                        ? (w->bounce > 0.001f
                           ? 0.15f + 0.55f * w->bounce : 0.0f)
                        : 0.55f + 0.37f * w->hardness;
      float rebound_speed = w->vy * coefficient;
      if (rebound_speed > 26.0f) rebound_speed = 26.0f;
      if (rebound_speed >= 2.5f && w->rebound_count < max_rebounds) {
        w->vy = -rebound_speed;
        w->surface = -2;
        w->rebounding = 1;
        w->rebound_surface = (int8_t)landing;
      } else {
        w->vy = 0.0f;
        w->surface = (int8_t)landing;
        w->rebounding = 0;
        w->rebound_surface = -2;
      }
    }
  }
  /* Serialising the whole visual state in every 128-sample audio callback is
   * unnecessary work on the realtime thread. The display consumes at most
   * one frame tick anyway, and canvas.js advances smoothly between these
   * authoritative snapshots. */
  s->snapshot_samples += (uint32_t)frames;
  if (s->snapshot_samples >= (uint32_t)(sample_rate / 50)) {
    s->snapshot_samples %= (uint32_t)(sample_rate / 50);
    update_snapshot(s);
  }
  return count;
}

static void set_param(void *instance, const char *key, const char *value) {
  pixel_walkers_t *s = (pixel_walkers_t *)instance;
  if (!s || !key || !value) return;
  float v = strtof(value, NULL);
  if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
  if (!strcmp(key, "birth_note")) {
    s->birth_note = (!strcmp(value, "On") || !strcmp(value, "on") || v >= 0.5f);
  } else if (!strcmp(key, "birth_level")) {
    s->birth_level = v;
  } else if (!strcmp(key, "hit_level")) {
    s->hit_level = v;
  } else if (!strcmp(key, "hit_decay")) {
    s->hit_decay = v;
  } else if (!strcmp(key, "bounce")) {
    s->bounce = v;
    for (int i = 0; i < MAX_WALKERS; ++i)
      if (s->walkers[i].active) s->walkers[i].bounce = v;
  } else if (!strcmp(key, "hardness")) {
    s->hardness = v;
    for (int i = 0; i < MAX_WALKERS; ++i)
      if (s->walkers[i].active) s->walkers[i].hardness = v;
  } else if (!strcmp(key, "kill_all")) {
    if (!strcmp(value, "-") || !strcmp(value, "0")) return;
    for (int i = 0; i < MAX_WALKERS; ++i) s->walkers[i].active = 0;
    s->kill_pending = 1;
    update_snapshot(s);
  } else if (!strcmp(key, "randomize")) {
    if (!strcmp(value, "-") || !strcmp(value, "0")) return;
    make_platforms(s);
    for (int i = 0; i < MAX_WALKERS; ++i) {
      if (!s->walkers[i].active) continue;
      s->walkers[i].surface = -2;
      s->walkers[i].vy = 0.0f;
      s->walkers[i].rebounding = 0;
      s->walkers[i].rebound_surface = -2;
      s->walkers[i].series_surface = -2;
      s->walkers[i].fall_origin_y = s->walkers[i].y + WALKER_H;
      s->walkers[i].rebound_count = 0;
    }
    update_snapshot(s);
  }
}

static int get_param(void *instance, const char *key, char *out, int size) {
  pixel_walkers_t *s = (pixel_walkers_t *)instance;
  if (!s || !key || !out || size < 1) return -1;
  if (!strcmp(key, "viz_state") || !strcmp(key, "viz_state:effective") ||
      !strcmp(key, "viz_state:base")) {
    int index = s->viz_index;
    __sync_synchronize();
    return snprintf(out, (size_t)size, "%s", s->viz[index]);
  }
  if (!strcmp(key, "birth_note"))
    return snprintf(out, (size_t)size, "%s", s->birth_note ? "On" : "Off");
  if (!strcmp(key, "birth_level"))
    return snprintf(out, (size_t)size, "%.3f", s->birth_level);
  if (!strcmp(key, "hit_level"))
    return snprintf(out, (size_t)size, "%.3f", s->hit_level);
  if (!strcmp(key, "hit_decay"))
    return snprintf(out, (size_t)size, "%.3f", s->hit_decay);
  if (!strcmp(key, "bounce"))
    return snprintf(out, (size_t)size, "%.3f", s->bounce);
  if (!strcmp(key, "hardness"))
    return snprintf(out, (size_t)size, "%.3f", s->hardness);
  if (!strcmp(key, "kill_all"))
    return snprintf(out, (size_t)size, "-");
  if (!strcmp(key, "randomize"))
    return snprintf(out, (size_t)size, "-");
  if (!strcmp(key, "chain_params")) {
    return snprintf(out, (size_t)size,
      "[{\"key\":\"birth_note\",\"name\":\"Birth Note\",\"short_name\":\"Birth\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"],\"default\":1},"
      "{\"key\":\"birth_level\",\"name\":\"Birth Level\",\"short_name\":\"Birth Lv\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":1,\"unit\":\"%%\"},"
      "{\"key\":\"hit_level\",\"name\":\"Collision Level\",\"short_name\":\"Hit Lv\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.7,\"unit\":\"%%\"},"
      "{\"key\":\"hit_decay\",\"name\":\"Collision Decay\",\"short_name\":\"Decay\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.01,\"unit\":\"%%\"},"
      "{\"key\":\"randomize\",\"name\":\"Randomize Platforms\",\"short_name\":\"Random\",\"type\":\"enum\",\"options\":[\"-\",\"Rnd!\"],\"access\":\"write\"},"
      "{\"key\":\"bounce\",\"name\":\"Bounce\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.7,\"unit\":\"%%\"},"
      "{\"key\":\"hardness\",\"name\":\"Hardness\",\"short_name\":\"Hard\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":1,\"unit\":\"%%\"},"
      "{\"key\":\"kill_all\",\"name\":\"Kill All\",\"short_name\":\"Kill\",\"type\":\"enum\",\"options\":[\"-\",\"Kill!\"],\"access\":\"write\"},"
      "{\"key\":\"tombola\",\"name\":\"Pixel Walkers\",\"type\":\"canvas\","
      "\"canvas_script\":\"canvas.js\",\"as_page\":true,\"show_value\":false,\"extra_keys\":[\"viz_state\"]},"
      "{\"key\":\"viz_state\",\"name\":\"Activity\",\"type\":\"string\","
      "\"access\":\"read\",\"hidden\":true}]");
  }
  out[0] = '\0';
  return -1;
}

static midi_fx_api_v1_t api = {
  1, create_instance, destroy_instance, process_midi, tick, set_param, get_param
};

__attribute__((visibility("default")))
midi_fx_api_v1_t *move_midi_fx_init(const host_api_v1_t *host) {
  (void)host;
  return &api;
}
