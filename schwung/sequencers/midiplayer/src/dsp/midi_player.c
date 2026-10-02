/*
 * MIDI Player MIDI FX
 *
 * Plays a Standard MIDI File into the slot's downstream sound generator,
 * synced to Move's MIDI clock (24 PPQN).
 *
 * - On set_param("file", path): parses the .mid file off the realtime path,
 *   builds a tick-sorted event timeline (merged or single-track), rewrites
 *   all channels to 1.
 * - On 0xF8 (clock): advances the playhead by division/24 ticks and queues
 *   any events whose absolute tick fell within the pulse window.
 * - On 0xFA Start: rewinds. On 0xFC Stop: pauses + emits all-notes-off.
 *   On 0xFB Continue: resumes from current playhead.
 *
 * Events are drained into the chain host's out buffer from both
 * process_midi (after handling 0xF8) and tick (in case a burst exceeded
 * MIDI_FX_MAX_OUT_MSGS in a single pulse).
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include "midi_fx_api_v1.h"
#include "plugin_api_v1.h"

static const host_api_v1_t *g_host;

#define MAX_TRACKS         64
#define MAX_EVENTS         65536   /* up to 64k events per file */
#define MAX_TRACK_NAME_LEN 48
#define QUEUE_CAP          256     /* pending-event ring */
#define MAX_FILES          256     /* file list cap for browse UI */
#define MIDI_DIR           "MIDI"  /* relative to module_dir */

typedef struct {
    uint32_t tick;        /* absolute file ticks */
    uint8_t  status;      /* MIDI status (channel already rewritten to 1) */
    uint8_t  data1;
    uint8_t  data2;
    uint8_t  len;         /* 2 or 3 */
    uint8_t  track;       /* source track index, for filter */
} smf_event_t;

typedef struct {
    char name[MAX_TRACK_NAME_LEN];
    int  note_on_count;   /* number of note-on events; 0 = meta-only track */
} track_info_t;

typedef struct {
    /* Module dir (for file resolution) */
    char module_dir[256];

    /* Loaded file state */
    char     file_path[256];
    uint16_t division;          /* PPQ ticks per quarter */
    int      ntracks;           /* tracks in file */
    track_info_t tracks[MAX_TRACKS];

    /* All parsed events, sorted by (tick, track) */
    smf_event_t *events;
    int          event_count;
    uint32_t     end_tick;

    /* Active timeline = filtered view into events[] */
    int      track_filter;      /* -1 = All, else 0-based track index */
    int      timeline_count;    /* count of active events */
    int     *timeline_idx;      /* indices into events[] */

    /* Transport */
    int      loop;              /* 1 = on */
    int      running;
    uint32_t playhead;          /* absolute file ticks */
    int      event_cursor;      /* next event index in timeline */

    /* Pending event queue (drained across calls) */
    uint8_t  queue_status[QUEUE_CAP];
    uint8_t  queue_d1[QUEUE_CAP];
    uint8_t  queue_d2[QUEUE_CAP];
    uint8_t  queue_len[QUEUE_CAP];
    int      queue_head;
    int      queue_tail;

    /* File list cache (for browse UI) */
    char     files[MAX_FILES][96];
    int      file_count;
    int      file_index;        /* selected index */
} player_t;

/* ---- JSON helpers (for state save/restore) ---- */

static int json_get_string(const char *json, const char *key, char *out, int out_len) {
    if (!json || !key || !out || out_len < 1) return 0;
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *pos = strstr(json, needle);
    if (!pos) return 0;
    const char *colon = strchr(pos + strlen(needle), ':');
    if (!colon) return 0;
    colon++;
    while (*colon == ' ' || *colon == '\t') colon++;
    if (*colon != '"') return 0;
    colon++;
    const char *end = strchr(colon, '"');
    if (!end) return 0;
    int len = (int)(end - colon);
    if (len >= out_len) len = out_len - 1;
    memcpy(out, colon, len);
    out[len] = '\0';
    return len;
}

static int json_get_int(const char *json, const char *key, int *out) {
    if (!json || !key || !out) return 0;
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *pos = strstr(json, needle);
    if (!pos) return 0;
    const char *colon = strchr(pos + strlen(needle), ':');
    if (!colon) return 0;
    colon++;
    while (*colon == ' ' || *colon == '\t') colon++;
    *out = atoi(colon);
    return 1;
}

static const char *basename_of(const char *path) {
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* ---- ring queue ---- */

static int queue_empty(const player_t *p) {
    return p->queue_head == p->queue_tail;
}

static int queue_full(const player_t *p) {
    return ((p->queue_tail + 1) % QUEUE_CAP) == p->queue_head;
}

static void queue_push(player_t *p, uint8_t s, uint8_t d1, uint8_t d2, uint8_t len) {
    if (queue_full(p)) return;  /* overflow: drop */
    p->queue_status[p->queue_tail] = s;
    p->queue_d1[p->queue_tail]     = d1;
    p->queue_d2[p->queue_tail]     = d2;
    p->queue_len[p->queue_tail]    = len;
    p->queue_tail = (p->queue_tail + 1) % QUEUE_CAP;
}

static int queue_drain(player_t *p, uint8_t out_msgs[][3], int out_lens[], int max_out) {
    int n = 0;
    while (n < max_out && !queue_empty(p)) {
        out_msgs[n][0] = p->queue_status[p->queue_head];
        out_msgs[n][1] = p->queue_d1[p->queue_head];
        out_msgs[n][2] = p->queue_d2[p->queue_head];
        out_lens[n]    = p->queue_len[p->queue_head];
        p->queue_head  = (p->queue_head + 1) % QUEUE_CAP;
        n++;
    }
    return n;
}

static void queue_clear(player_t *p) {
    p->queue_head = p->queue_tail = 0;
}

/* ---- all-notes-off ---- */

static void emit_all_notes_off_ch1(player_t *p) {
    /* CC 123 All Notes Off, then explicit note-offs would be overkill;
     * 0xB0 7B 00 hits every note hanging on ch 1. */
    queue_push(p, 0xB0, 123, 0, 3);
}

/* ---- variable-length quantity ---- */

static uint32_t read_vlq(const uint8_t **p, const uint8_t *end) {
    uint32_t v = 0;
    while (*p < end) {
        uint8_t b = *(*p)++;
        v = (v << 7) | (b & 0x7F);
        if ((b & 0x80) == 0) return v;
    }
    return v;
}

static uint16_t read_be16(const uint8_t *p) { return (p[0] << 8) | p[1]; }
static uint32_t read_be32(const uint8_t *p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

/* ---- SMF parsing ---- */

/* Sort comparator: tick asc, then track asc (stable-ish ordering for ties). */
static int event_cmp(const void *a, const void *b) {
    const smf_event_t *ea = (const smf_event_t *)a;
    const smf_event_t *eb = (const smf_event_t *)b;
    if (ea->tick < eb->tick) return -1;
    if (ea->tick > eb->tick) return 1;
    if (ea->track < eb->track) return -1;
    if (ea->track > eb->track) return 1;
    return 0;
}

static void clear_timeline(player_t *p) {
    if (p->timeline_idx) { free(p->timeline_idx); p->timeline_idx = NULL; }
    p->timeline_count = 0;
}

static void clear_file(player_t *p) {
    clear_timeline(p);
    if (p->events) { free(p->events); p->events = NULL; }
    p->event_count = 0;
    p->end_tick = 0;
    p->division = 480;
    p->ntracks = 0;
    for (int i = 0; i < MAX_TRACKS; i++) {
        p->tracks[i].name[0] = '\0';
        p->tracks[i].note_on_count = 0;
    }
    p->playhead = 0;
    p->event_cursor = 0;
    p->running = 0;
    queue_clear(p);
}

static int parse_smf(player_t *p, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 14 || sz > 8 * 1024 * 1024) { fclose(f); return -2; }
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (!buf) { fclose(f); return -3; }
    if (fread(buf, 1, sz, f) != (size_t)sz) { free(buf); fclose(f); return -4; }
    fclose(f);

    if (memcmp(buf, "MThd", 4) != 0) { free(buf); return -5; }
    uint32_t hlen = read_be32(buf + 4);
    if (hlen < 6) { free(buf); return -6; }
    /* uint16_t fmt = read_be16(buf + 8); */
    uint16_t ntrk = read_be16(buf + 10);
    uint16_t div = read_be16(buf + 12);
    if (div & 0x8000) { free(buf); return -7; }  /* SMPTE divisions not supported */
    if (div == 0) div = 480;
    if (ntrk > MAX_TRACKS) ntrk = MAX_TRACKS;

    clear_file(p);
    p->division = div;
    p->ntracks = ntrk;

    /* Allocate event buffer (best effort). */
    p->events = (smf_event_t *)calloc(MAX_EVENTS, sizeof(smf_event_t));
    if (!p->events) { free(buf); return -8; }

    const uint8_t *cur = buf + 8 + hlen;
    const uint8_t *end = buf + sz;
    int track_idx = 0;

    while (cur + 8 <= end && track_idx < (int)ntrk) {
        if (memcmp(cur, "MTrk", 4) != 0) break;
        uint32_t tlen = read_be32(cur + 4);
        const uint8_t *tstart = cur + 8;
        const uint8_t *tend = tstart + tlen;
        if (tend > end) tend = end;
        cur = tend;

        const uint8_t *tp = tstart;
        uint32_t abs_tick = 0;
        uint8_t running_status = 0;

        while (tp < tend) {
            uint32_t delta = read_vlq(&tp, tend);
            abs_tick += delta;
            if (tp >= tend) break;
            uint8_t status = *tp;

            if (status < 0x80) {
                /* running status: reuse last channel-voice status */
                status = running_status;
            } else {
                tp++;
            }

            if (status == 0xFF) {
                /* Meta event */
                if (tp >= tend) break;
                uint8_t meta_type = *tp++;
                uint32_t mlen = read_vlq(&tp, tend);
                if (tp + mlen > tend) break;
                if (meta_type == 0x03 && track_idx < MAX_TRACKS) {
                    /* Track name */
                    int copy = (mlen < MAX_TRACK_NAME_LEN - 1) ? (int)mlen : MAX_TRACK_NAME_LEN - 1;
                    memcpy(p->tracks[track_idx].name, tp, copy);
                    p->tracks[track_idx].name[copy] = '\0';
                }
                /* Tempo (0x51) and others: discarded by design */
                tp += mlen;
                continue;
            }
            if (status == 0xF0 || status == 0xF7) {
                /* SysEx: skip body */
                uint32_t sxlen = read_vlq(&tp, tend);
                tp += sxlen;
                continue;
            }

            /* Channel-voice event */
            running_status = status;
            uint8_t type = status & 0xF0;
            int dlen;
            if (type == 0xC0 || type == 0xD0) dlen = 1; else dlen = 2;
            if (tp + dlen > tend) break;
            uint8_t d1 = (dlen >= 1) ? *tp++ : 0;
            uint8_t d2 = (dlen >= 2) ? *tp++ : 0;

            /* Drop Program Change and most CCs. The user picked the synth
             * and dialed in its mix on the downstream track — we don't want
             * the MIDI file overriding pan, volume, sends, etc. Allow only
             * expressive performance CCs through:
             *   CC 1   mod wheel
             *   CC 64  sustain pedal
             *   CC 66  sostenuto pedal
             *   CC 67  soft pedal
             *   CC 123 all notes off
             */
            if (type == 0xC0) continue;
            if (type == 0xB0 && d1 != 1 && d1 != 64 && d1 != 66 && d1 != 67 && d1 != 123) continue;

            if (p->event_count >= MAX_EVENTS) continue;

            /* Rewrite channel to 1 (0x00 nibble). */
            uint8_t out_status = (uint8_t)((status & 0xF0) | 0x00);

            smf_event_t *ev = &p->events[p->event_count++];
            ev->tick   = abs_tick;
            ev->status = out_status;
            ev->data1  = d1;
            ev->data2  = d2;
            ev->len    = (uint8_t)(1 + dlen);
            ev->track  = (uint8_t)track_idx;

            if (type == 0x90 && d2 > 0 && track_idx < MAX_TRACKS) {
                p->tracks[track_idx].note_on_count++;
            }
            if (abs_tick > p->end_tick) p->end_tick = abs_tick;
        }
        track_idx++;
    }

    free(buf);

    /* Sort the merged event list by (tick, track). */
    qsort(p->events, p->event_count, sizeof(smf_event_t), event_cmp);

    /* Snapshot path */
    snprintf(p->file_path, sizeof(p->file_path), "%s", path);

    return 0;
}

/* Rebuild timeline_idx based on current track_filter, preserving the
 * current playhead. event_cursor is positioned at the first event whose
 * tick is at or after the playhead, so a live track change picks up
 * playback from the same musical position rather than rewinding. */
static void rebuild_timeline(player_t *p) {
    clear_timeline(p);
    if (p->event_count == 0) return;
    p->timeline_idx = (int *)malloc(sizeof(int) * p->event_count);
    if (!p->timeline_idx) return;
    int n = 0;
    for (int i = 0; i < p->event_count; i++) {
        if (p->track_filter < 0 || p->events[i].track == p->track_filter) {
            p->timeline_idx[n++] = i;
        }
    }
    p->timeline_count = n;

    int cur = 0;
    while (cur < n && p->events[p->timeline_idx[cur]].tick < p->playhead) {
        cur++;
    }
    p->event_cursor = cur;
}

/* ---- file listing (for browse UI) ---- */

static int has_mid_ext(const char *name) {
    size_t n = strlen(name);
    if (n < 4) return 0;
    return strcasecmp(name + n - 4, ".mid") == 0;
}

static int cmpstr(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

static void rescan_files(player_t *p) {
    p->file_count = 0;
    char dir[320];
    snprintf(dir, sizeof(dir), "%s/%s", p->module_dir, MIDI_DIR);
    /* Best-effort mkdir so first run doesn't return empty silently */
    mkdir(dir, 0755);

    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && p->file_count < MAX_FILES) {
        if (ent->d_name[0] == '.') continue;
        if (!has_mid_ext(ent->d_name)) continue;
        snprintf(p->files[p->file_count], sizeof(p->files[0]), "%s", ent->d_name);
        p->file_count++;
    }
    closedir(d);

    if (p->file_count > 1) {
        qsort(p->files, p->file_count, sizeof(p->files[0]), cmpstr);
    }
}

/* ---- plugin API impl ---- */

static void* mp_create_instance(const char *module_dir, const char *config_json) {
    (void)config_json;
    player_t *p = (player_t *)calloc(1, sizeof(player_t));
    if (!p) return NULL;
    if (module_dir) {
        snprintf(p->module_dir, sizeof(p->module_dir), "%s", module_dir);
    } else {
        snprintf(p->module_dir, sizeof(p->module_dir),
                 "/data/UserData/schwung/modules/midi_fx/midi-player");
    }
    p->division = 480;
    p->track_filter = -1;
    p->loop = 1;
    p->running = 0;
    p->file_index = -1;
    rescan_files(p);

    /* Auto-load files[0] so the preset browser shows a real filename on
     * first view rather than "(no file)". Any saved patch state will run
     * immediately after via set_param("state", ...) and override this. */
    if (p->file_count > 0) {
        char abs_path[512];
        snprintf(abs_path, sizeof(abs_path), "%s/%s/%s",
                 p->module_dir, MIDI_DIR, p->files[0]);
        if (parse_smf(p, abs_path) == 0) {
            p->file_index = 0;
            rebuild_timeline(p);
        }
    }
    return p;
}

static void mp_destroy_instance(void *instance) {
    if (!instance) return;
    player_t *p = (player_t *)instance;
    clear_file(p);
    free(p);
}

/* Advance playhead by one clock pulse worth of file ticks, queuing
 * any events that fall within [playhead, new_playhead). */
static void advance_one_clock(player_t *p) {
    if (!p->running || p->timeline_count == 0) return;

    uint32_t step = (p->division + 12) / 24;  /* round to nearest */
    if (step == 0) step = 1;
    uint32_t target = p->playhead + step;

    /* Drain due events. Handle loop wrap inside the loop. */
    int safety = 4096;  /* prevent runaway loops on pathological files */
    while (safety-- > 0) {
        while (p->event_cursor < p->timeline_count) {
            const smf_event_t *ev = &p->events[p->timeline_idx[p->event_cursor]];
            if (ev->tick >= target && ev->tick >= p->playhead) {
                if (ev->tick < target) {
                    /* shouldn't happen, but bail */
                }
                break;
            }
            queue_push(p, ev->status, ev->data1, ev->data2, ev->len);
            p->event_cursor++;
        }

        if (target >= p->end_tick + 1 || p->event_cursor >= p->timeline_count) {
            if (p->loop) {
                emit_all_notes_off_ch1(p);
                p->playhead = 0;
                p->event_cursor = 0;
                /* If target overshot the end, carry the remainder into the
                 * new pass. */
                if (target > p->end_tick) target = target - p->end_tick - 1;
                else target = 0;
                continue;
            } else {
                p->running = 0;
                emit_all_notes_off_ch1(p);
                p->playhead = target;
                return;
            }
        }
        break;
    }

    p->playhead = target;
}

static int mp_process_midi(void *instance,
                           const uint8_t *in_msg, int in_len,
                           uint8_t out_msgs[][3], int out_lens[],
                           int max_out) {
    player_t *p = (player_t *)instance;
    if (!p || in_len < 1) return 0;
    uint8_t status = in_msg[0];

    /* Events always flow out via mp_tick, never via mp_process_midi return.
     * The chain's Pre-mode injection to Move's native tracks runs in two
     * places (chain_host.c v2_on_midi vs v2_tick_midi_fx). The v2_on_midi
     * path gates on len >= 2, which excludes single-byte system messages
     * like 0xF8 clock — so any notes we return synchronously from a clock
     * pulse never reach Move. The tick path has no such gate, so we route
     * everything through there. Synth-side behavior is identical (the tick
     * path also forwards to synth). */
    if (status == 0xF8) {
        advance_one_clock(p);
        (void)out_msgs; (void)out_lens; (void)max_out;
        return 0;
    }
    if (status == 0xFA) {  /* Start */
        p->playhead = 0;
        p->event_cursor = 0;
        p->running = 1;
        queue_clear(p);
        return 0;
    }
    if (status == 0xFB) {  /* Continue */
        p->running = 1;
        return 0;
    }
    if (status == 0xFC) {  /* Stop */
        p->running = 0;
        queue_clear(p);
        emit_all_notes_off_ch1(p);
        return 0;
    }

    /* Everything else: pass through unchanged (don't gate the synth). */
    if (max_out >= 1) {
        out_msgs[0][0] = in_len > 0 ? in_msg[0] : 0;
        out_msgs[0][1] = in_len > 1 ? in_msg[1] : 0;
        out_msgs[0][2] = in_len > 2 ? in_msg[2] : 0;
        out_lens[0] = in_len;
        return 1;
    }
    return 0;
}

static int mp_tick(void *instance, int frames, int sample_rate,
                   uint8_t out_msgs[][3], int out_lens[], int max_out) {
    (void)frames; (void)sample_rate;
    player_t *p = (player_t *)instance;
    if (!p) return 0;
    return queue_drain(p, out_msgs, out_lens, max_out);
}

/* ---- params ---- */

static int parse_int(const char *v) { return v ? atoi(v) : 0; }

static void mp_set_param(void *instance, const char *key, const char *val) {
    if (!instance || !key || !val) return;
    player_t *p = (player_t *)instance;

    if (strcmp(key, "file") == 0) {
        /* val is either an absolute path or a bare filename relative to MIDI/ */
        char abs_path[512];
        const char *base = basename_of(val);
        if (val[0] == '/') {
            snprintf(abs_path, sizeof(abs_path), "%s", val);
        } else {
            snprintf(abs_path, sizeof(abs_path), "%s/%s/%s",
                     p->module_dir, MIDI_DIR, val);
        }
        int prev_filter = p->track_filter;
        if (parse_smf(p, abs_path) == 0) {
            p->track_filter = prev_filter;
            /* Sync file_index with file list so the UI's list_param
             * displays the right entry after a path-based load. */
            rescan_files(p);
            p->file_index = -1;
            for (int i = 0; i < p->file_count; i++) {
                if (strcmp(p->files[i], base) == 0) { p->file_index = i; break; }
            }
            rebuild_timeline(p);
        }
        return;
    }
    if (strcmp(key, "file_index") == 0) {
        int idx = parse_int(val);
        if (idx < 0 || idx >= p->file_count) return;
        p->file_index = idx;
        char abs_path[512];
        snprintf(abs_path, sizeof(abs_path), "%s/%s/%s",
                 p->module_dir, MIDI_DIR, p->files[idx]);
        int prev_filter = p->track_filter;
        if (parse_smf(p, abs_path) == 0) {
            p->track_filter = prev_filter;
            p->file_index = idx;  /* parse_smf → clear_file resets it */
            rebuild_timeline(p);
        }
        return;
    }
    if (strcmp(key, "state") == 0) {
        /* Restore from chain patch JSON: {"file":"...","track":N,"loop":"..."}
         * Order matters — load file first so track filter / loop apply to a
         * populated timeline. */
        char file_buf[160];
        int track_val;
        char loop_str[8];
        if (json_get_string(val, "file", file_buf, sizeof(file_buf)) > 0) {
            mp_set_param(instance, "file", file_buf);
        }
        if (json_get_int(val, "track", &track_val)) {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%d", track_val);
            mp_set_param(instance, "track", tmp);
        }
        if (json_get_string(val, "loop", loop_str, sizeof(loop_str)) > 0) {
            mp_set_param(instance, "loop", loop_str);
        }
        return;
    }
    if (strcmp(key, "rescan") == 0) {
        rescan_files(p);
        return;
    }
    if (strcmp(key, "track") == 0) {
        /* -1 = All, 0..ntracks-1 = specific track (shadow UI items_param
         * passes the item.index directly). Live switch: keep playhead,
         * just silence any notes the previous filter was sounding so they
         * don't hang while the new track's stream picks up. */
        int t = parse_int(val);
        if (t < 0 || t >= p->ntracks) p->track_filter = -1;
        else p->track_filter = t;
        queue_clear(p);
        emit_all_notes_off_ch1(p);
        rebuild_timeline(p);
        return;
    }
    if (strcmp(key, "loop") == 0) {
        if (strcmp(val, "on") == 0 || strcmp(val, "1") == 0) p->loop = 1;
        else p->loop = 0;
        return;
    }
}

static int mp_get_param(void *instance, const char *key, char *buf, int buf_len) {
    if (!instance || !key || !buf || buf_len < 1) return -1;
    player_t *p = (player_t *)instance;

    if (strcmp(key, "file") == 0) {
        return snprintf(buf, buf_len, "%s", p->file_path);
    }
    if (strcmp(key, "loop") == 0) {
        return snprintf(buf, buf_len, "%s", p->loop ? "on" : "off");
    }
    if (strcmp(key, "track") == 0) {
        return snprintf(buf, buf_len, "%d", p->track_filter);
    }
    if (strcmp(key, "position") == 0) {
        uint32_t qn = p->division ? (p->playhead / p->division) : 0;
        uint32_t bar = qn / 4;
        uint32_t beat = qn % 4;
        return snprintf(buf, buf_len, "%u.%u", bar + 1, beat + 1);
    }
    if (strcmp(key, "file_count") == 0) {
        /* List browser entry point: rescan so the count is always fresh. */
        rescan_files(p);
        if (p->file_index >= p->file_count) p->file_index = -1;
        return snprintf(buf, buf_len, "%d", p->file_count);
    }
    if (strcmp(key, "file_index") == 0) {
        return snprintf(buf, buf_len, "%d", p->file_index < 0 ? 0 : p->file_index);
    }
    if (strcmp(key, "file_name_display") == 0) {
        if (p->file_index >= 0 && p->file_index < p->file_count) {
            return snprintf(buf, buf_len, "%s", p->files[p->file_index]);
        }
        if (p->file_count == 0) {
            return snprintf(buf, buf_len, "(no files)");
        }
        /* file_index < 0 but files exist — display matches the list_param
         * getter (which clamps to 0) so the browser is internally
         * consistent rather than showing "(no file)" at slot 0. */
        return snprintf(buf, buf_len, "%s", p->files[0]);
    }
    if (strcmp(key, "state") == 0) {
        /* Chain patch persistence: bundle the user-facing settings into a
         * single JSON blob. Filename only — index numbers shift when files
         * are added/removed; the basename is stable. */
        const char *fname = "";
        if (p->file_index >= 0 && p->file_index < p->file_count) {
            fname = p->files[p->file_index];
        } else if (p->file_path[0]) {
            fname = basename_of(p->file_path);
        }
        return snprintf(buf, buf_len,
                        "{\"file\":\"%s\",\"track\":%d,\"loop\":\"%s\"}",
                        fname, p->track_filter, p->loop ? "on" : "off");
    }
    if (strcmp(key, "error") == 0) {
        /* Surface a clock-availability warning so the user knows why
         * nothing's playing if MIDI Clock Out is disabled in Move
         * settings. Empty string = no error. */
        if (!g_host || !g_host->get_clock_status) {
            buf[0] = '\0';
            return 0;
        }
        int status = g_host->get_clock_status();
        if (status == MOVE_CLOCK_STATUS_UNAVAILABLE) {
            return snprintf(buf, buf_len, "Enable MIDI Clock Out in Move settings");
        }
        if (status == MOVE_CLOCK_STATUS_STOPPED) {
            return snprintf(buf, buf_len, "Clock out enabled, transport stopped");
        }
        buf[0] = '\0';
        return 0;
    }
    if (strcmp(key, "file_list") == 0) {
        /* Shadow UI items_param expects [{label,index}, ...]. Rescan on every
         * read so newly-dropped .mid files appear without reloading the
         * module. Cheap: one readdir of a small directory. */
        rescan_files(p);
        int w = snprintf(buf, buf_len, "[");
        for (int i = 0; i < p->file_count && w < buf_len - 64; i++) {
            w += snprintf(buf + w, buf_len - w,
                          "%s{\"label\":\"%s\",\"index\":%d}",
                          i ? "," : "", p->files[i], i);
        }
        w += snprintf(buf + w, buf_len - w, "]");
        return w;
    }
    if (strcmp(key, "track_list_display") == 0) {
        /* Items for the track_select level: "All" (-1) followed by every
         * track that actually has note-on events. Tempo/title meta tracks
         * (common as track 0 in Type 1 files) would otherwise be selectable
         * and produce silence. Original track index is preserved so the
         * label numbering matches the .mid file's track ordering.
         *
         * The currently-selected entry gets a leading "* " so the user can
         * see which one is live when they re-enter the picker — items_param
         * has no built-in current-selection styling in the shadow UI. */
        const char *all_mark = (p->track_filter < 0) ? "* " : "";
        int w = snprintf(buf, buf_len,
                         "[{\"label\":\"%sAll\",\"index\":-1}", all_mark);
        for (int i = 0; i < p->ntracks && w < buf_len - 64; i++) {
            if (p->tracks[i].note_on_count == 0) continue;
            const char *mark = (i == p->track_filter) ? "* " : "";
            const char *nm = p->tracks[i].name[0] ? p->tracks[i].name : "";
            if (nm[0]) {
                w += snprintf(buf + w, buf_len - w,
                              ",{\"label\":\"%sTrack %d: %s\",\"index\":%d}",
                              mark, i + 1, nm, i);
            } else {
                w += snprintf(buf + w, buf_len - w,
                              ",{\"label\":\"%sTrack %d\",\"index\":%d}",
                              mark, i + 1, i);
            }
        }
        w += snprintf(buf + w, buf_len - w, "]");
        return w;
    }
    if (strcmp(key, "track_list") == 0) {
        /* Structured form for JS UI. */
        int w = snprintf(buf, buf_len, "[{\"index\":-1,\"name\":\"All\"}");
        for (int i = 0; i < p->ntracks && w < buf_len - 16; i++) {
            w += snprintf(buf + w, buf_len - w,
                          ",{\"index\":%d,\"name\":\"%s\"}",
                          i, p->tracks[i].name);
        }
        w += snprintf(buf + w, buf_len - w, "]");
        return w;
    }
    if (strcmp(key, "chain_params") == 0) {
        const char *params =
            "["
            "{\"key\":\"track\",\"name\":\"Track\",\"type\":\"int\",\"min\":0,\"max\":64,\"step\":1},"
            "{\"key\":\"loop\",\"name\":\"Loop\",\"type\":\"enum\",\"options\":[\"off\",\"on\"]}"
            "]";
        return snprintf(buf, buf_len, "%s", params);
    }
    return -1;
}

/* ---- API export ---- */

static midi_fx_api_v1_t g_api = {
    .api_version    = MIDI_FX_API_VERSION,
    .create_instance = mp_create_instance,
    .destroy_instance = mp_destroy_instance,
    .process_midi    = mp_process_midi,
    .tick            = mp_tick,
    .set_param       = mp_set_param,
    .get_param       = mp_get_param,
};

midi_fx_api_v1_t* move_midi_fx_init(const struct host_api_v1 *host) {
    g_host = host;
    return &g_api;
}
