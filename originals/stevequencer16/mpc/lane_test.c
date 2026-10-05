/* Offline test of the lanes: the engine alone, with a fake transport (120 BPM, 1/16 steps), checking the CCs and NRPNs
 * it sends against the notes. From the plugin folder:
 *   docker run --rm -v "$PWD:$PWD" -w "$PWD" gcc:12 sh -c \
 *     'gcc -fsanitize=address,undefined -g -Impc -Impc/host src/stevequencer16.c mpc/lane_test.c -lm -o /tmp/t && /tmp/t'
 */
#include <stdio.h>
#include <string.h>
#include "host/plugin_api_v1.h"
#include "host/midi_fx_api_v1.h"

midi_fx_api_v1_t *move_midi_fx_init(const host_api_v1_t *host);

static double beat;   /* song position at the end of the block being run */
static int clock_status(void) { return MOVE_CLOCK_STATUS_RUNNING; }
static double beat_pos(void) { return beat; }
static float bpm(void) { return 120.0f; }

typedef struct { long t; unsigned char m[3]; } msg_t;
static msg_t got[16384];
static int ngot, fails;
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)

static midi_fx_api_v1_t *api;
static void set(void *q, const char *k, int v) { char b[16]; snprintf(b, sizeof b, "%d", v); api->set_param(q, k, b); }
static void *fresh(void) {   /* a new instance with no notes, lanes cleared to OFF */
    void *q = api->create_instance(NULL, NULL);
    char k[32];
    set(q, "audition", 0);
    for (int l = 1; l <= 8; l++) { snprintf(k, sizeof k, "l%d_dest", l); set(q, k, 0); }
    for (int s = 1; s <= 16; s++) { snprintf(k, sizeof k, "s%d_on", s); set(q, k, 0); }
    return q;
}

/* Run `steps` 1/16 steps from the song start (and the next step's start, in the last block), collecting messages with their block's sample time. */
static void play(void *q, int steps) {
    uint8_t out[64][3]; int lens[64];
    double spb = 44100 * 60.0 / 120.0;   /* samples per quarter note */
    long frames = (long)(steps * 0.25 * spb) + 1;
    ngot = 0; beat = 0;
    for (long t = 0; t < frames; t += 128) {
        beat = (t + 128) / spb;
        int n = api->tick(q, 128, 44100, out, lens, 64);
        for (int i = 0; i < n && ngot < 16384; i++) { got[ngot].t = t; memcpy(got[ngot].m, out[i], 3); ngot++; }
    }
}
static int ccs(int cc, int *vals, int max) {   /* the values sent on controller cc, in order */
    int n = 0;
    for (int i = 0; i < ngot; i++) if ((got[i].m[0] & 0xF0) == 0xB0 && got[i].m[1] == cc && n < max) vals[n++] = got[i].m[2];
    return n;
}
static long cc_time(int cc, int value) {   /* when cc first carried value (-1: never) */
    for (int i = 0; i < ngot; i++) if ((got[i].m[0] & 0xF0) == 0xB0 && got[i].m[1] == cc && got[i].m[2] == value) return got[i].t;
    return -1;
}
#define STEP_SAMPLES (44100 * 60 / 120 / 4)

int main(void) {
    static host_api_v1_t host;
    host.get_clock_status = clock_status; host.get_beat_position = beat_pos; host.get_bpm = bpm;
    api = move_midi_fx_init(&host);
    int v[4096], n;

    /* HOLD on CC 20 (lane 1's default): values only on the steps that have one, a repeat not sent again, before the note */
    void *q = api->create_instance(NULL, NULL);
    set(q, "audition", 0);
    set(q, "l1_s1", 10); set(q, "l1_s3", 100); set(q, "l1_s4", 100);
    play(q, 4);
    n = ccs(20, v, 64);
    CHECK(n == 2 && v[0] == 10 && v[1] == 100, "HOLD: 10, nothing on the empty step, 100 once (%d CCs)", n);
    int first_cc = -1, first_on = -1;
    for (int i = 0; i < ngot; i++) {
        if (first_cc < 0 && (got[i].m[0] & 0xF0) == 0xB0) first_cc = i;
        if (first_on < 0 && (got[i].m[0] & 0xF0) == 0x90) first_on = i;
    }
    CHECK(first_cc >= 0 && first_on > first_cc && got[first_cc].t == got[first_on].t, "a step's lane value goes out just before its note");

    /* RETURN: an empty step goes back to LOW */
    void *r = fresh();
    set(r, "l1_dest", 1); set(r, "l1_num", 30); set(r, "l1_mode", 1); set(r, "l1_low", 5); set(r, "l1_s1", 90);
    play(r, 3);
    n = ccs(30, v, 64);
    CHECK(n == 2 && v[0] == 90 && v[1] == 5, "RETURN on CC 30: 90, then LOW 5");

    /* SLIDE: 0 on step 1 and 127 on step 9 make a ramp over the 8 steps between, then back down to step 1 */
    void *s = fresh();
    set(s, "l1_dest", 1); set(s, "l1_num", 40); set(s, "l1_mode", 2); set(s, "l1_s1", 0); set(s, "l1_s9", 127);
    play(s, 8);   /* up to step 9's start (after it the lane slides back down to step 1) */
    n = ccs(40, v, 4096);
    int rising = 1;
    for (int i = 1; i < n; i++) rising &= v[i] > v[i - 1];
    long t_mid = cc_time(40, 64), t_top = cc_time(40, 127);
    CHECK(n > 40 && v[0] == 0 && v[n - 1] == 127 && rising, "SLIDE: rises from 0 to 127 in %d steady values", n);
    CHECK(t_mid > 3 * STEP_SAMPLES && t_mid < 5 * STEP_SAMPLES && t_top >= 8 * STEP_SAMPLES - 256,
          "SLIDE: halfway (64) around step 5, 127 at step 9");

    /* LFO: a square between 10 and 100, a cycle every 2 steps: 100, 10, 100, 10 */
    void *o = fresh();
    set(o, "l1_dest", 1); set(o, "l1_num", 50); set(o, "l1_mode", 3); set(o, "l1_shape", 4); set(o, "l1_cycle", 2);
    set(o, "l1_low", 10); set(o, "l1_high", 100);
    play(o, 4);
    n = ccs(50, v, 64);
    CHECK(n >= 4 && v[0] == 100 && v[1] == 10 && v[2] == 100 && v[3] == 10, "LFO square, 2-step cycle: 100 10 100 10 (%d values)", n);
    char b[64];
    api->get_param(o, "l1_cycle", b, sizeof b);
    CHECK(!strcmp(b, "2"), "LFO cycle reads back as its length in steps (%s)", b);

    /* FOLLOW: lane 2 follows lane 1 upside down (LOW 127, HIGH 0): a modulation group */
    void *f = fresh();
    set(f, "l1_dest", 1); set(f, "l1_num", 60); set(f, "l1_s1", 0); set(f, "l1_s2", 127);
    set(f, "l2_dest", 1); set(f, "l2_num", 61); set(f, "l2_mode", 4); set(f, "l2_src", 0); set(f, "l2_low", 127); set(f, "l2_high", 0);
    play(f, 2);
    int a[8], c2[8], na = ccs(60, a, 8), nc = ccs(61, c2, 8);
    CHECK(na == 2 && nc == 2 && c2[0] == 127 && c2[1] == 0, "FOLLOW: lane 2 mirrors lane 1 (0 -> 127, 127 -> 0)");

    /* PARAM: an NRPN, number then value; the number isn't sent again while it stays selected */
    void *p = fresh();
    set(p, "l1_dest", 2); set(p, "l1_num", 300); set(p, "l1_s1", 33); set(p, "l1_s2", 44);
    play(p, 2);
    int seq[8], ns = 0;
    for (int i = 0; i < ngot && ns < 8; i++) if ((got[i].m[0] & 0xF0) == 0xB0) seq[ns++] = got[i].m[1] * 1000 + got[i].m[2];
    CHECK(ns == 4 && seq[0] == 99002 && seq[1] == 98043 && seq[2] == 6033 && seq[3] == 6044,
          "PARAM 300: NRPN 299 (CC 99 = 2, CC 98 = 43), CC 6 = 33, then just CC 6 = 44");
    api->get_param(p, "l1_num_display", b, sizeof b);
    CHECK(!strcmp(b, "P300"), "its NUMBER shows as P300 (%s)", b);

    /* RATE 1/2X: the lane moves on every 2 steps; LENGTH 2: it loops its first 2 values */
    void *h = fresh();
    set(h, "l1_dest", 1); set(h, "l1_num", 70); set(h, "l1_rate", 1); set(h, "l1_s1", 1); set(h, "l1_s2", 2); set(h, "l1_s3", 3);
    play(h, 6);
    n = ccs(70, v, 64);
    CHECK(n == 3 && v[0] == 1 && v[1] == 2 && v[2] == 3 && cc_time(70, 2) >= 2 * STEP_SAMPLES - 256, "RATE 1/2X: 1, 2, 3 every other step");
    void *g = fresh();
    set(g, "l1_dest", 1); set(g, "l1_num", 71); set(g, "l1_len", 2); set(g, "l1_s1", 7); set(g, "l1_s2", 8); set(g, "l1_s3", 9);
    play(g, 4);
    n = ccs(71, v, 64);
    CHECK(n >= 4 && v[0] == 7 && v[1] == 8 && v[2] == 7 && v[3] == 8, "LENGTH 2: 7 8 7 8 (step 3's 9 is outside the lane)");

    /* the state keeps steps, lane values and settings */
    char st[8192];
    int len = api->get_param(f, "state", st, sizeof st);
    void *c = api->create_instance(NULL, NULL);
    api->set_param(c, "state", st);
    char x1[16], x2[16], x3[16];
    api->get_param(c, "l1_s2", x1, sizeof x1); api->get_param(c, "l2_mode", x2, sizeof x2); api->get_param(c, "l2_low", x3, sizeof x3);
    CHECK(len > 0 && !strncmp(st, "S16 1 ", 6) && !strcmp(x1, "127") && !strcmp(x2, "4") && !strcmp(x3, "127"),
          "state round trip: lane 1 step 2 = 127, lane 2 FOLLOW, LOW 127 (%d bytes)", len);

    void *all[] = {q, r, s, o, f, p, h, g, c};
    for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++) api->destroy_instance(all[i]);
    printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails != 0;
}
