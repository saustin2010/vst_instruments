/* Offline test of the MOD lanes: the engine alone, with a fake transport (120 BPM, 1/16 steps), checking the CCs it
 * sends against the notes. From the plugin folder:
 *   docker run --rm -v "$PWD:$PWD" -w "$PWD" gcc:12 sh -c \
 *     'gcc -fsanitize=address,undefined -g -Impc -Impc/host src/stevequencer.c mpc/mod_test.c -lm -o /tmp/t && /tmp/t'
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
static msg_t got[4096];
static int ngot, fails;
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)

static midi_fx_api_v1_t *api;
static void set(void *q, const char *k, int v) { char b[16]; snprintf(b, sizeof b, "%d", v); api->set_param(q, k, b); }

/* Run `steps` 1/16 steps from the song start, collecting messages with their block's sample time. */
static void play(void *q, int steps) {
    uint8_t out[64][3]; int lens[64];
    double spb = 44100 * 60.0 / 120.0;   /* samples per quarter note */
    long frames = (long)(steps * 0.25 * spb) + 1;
    ngot = 0; beat = 0;
    for (long t = 0; t < frames; t += 128) {
        beat = (t + 128) / spb;
        int n = api->tick(q, 128, 44100, out, lens, 64);
        for (int i = 0; i < n && ngot < 4096; i++) { got[ngot].t = t; memcpy(got[ngot].m, out[i], 3); ngot++; }
    }
}
static int count_cc(int cc) { int n = 0; for (int i = 0; i < ngot; i++) n += (got[i].m[0] & 0xF0) == 0xB0 && got[i].m[1] == cc; return n; }
static int nth_cc_value(int cc, int k) {
    for (int i = 0; i < ngot; i++) if ((got[i].m[0] & 0xF0) == 0xB0 && got[i].m[1] == cc && k-- == 0) return got[i].m[2];
    return -1;
}

int main(void) {
    static host_api_v1_t host;
    host.get_clock_status = clock_status; host.get_beat_position = beat_pos; host.get_bpm = bpm;
    api = move_midi_fx_init(&host);
    void *q = api->create_instance(NULL, NULL);
    set(q, "audition", 0);
    /* MOD A (CC 20 by default): step 1 = 10, step 2 none, step 3 = 100, step 4 = 100 (the engine gets the value) */
    set(q, "s1_moda", 10); set(q, "s3_moda", 100); set(q, "s4_moda", 100);
    set(q, "s2_on", 0);   /* a lane plays on a step with no note too */
    set(q, "s2_modb", 50); /* MOD B (CC 21) = 50 on step 2 */
    play(q, 4);

    int first_cc = -1, first_on = -1;
    for (int i = 0; i < ngot; i++) {
        if (first_cc < 0 && (got[i].m[0] & 0xF0) == 0xB0) first_cc = i;
        if (first_on < 0 && (got[i].m[0] & 0xF0) == 0x90) first_on = i;
    }
    CHECK(first_cc >= 0 && first_on >= 0 && first_cc < first_on && got[first_cc].t == got[first_on].t,
          "step 1: its CC goes out just before its note, in the same block");
    CHECK(count_cc(20) == 2 && nth_cc_value(20, 0) == 10 && nth_cc_value(20, 1) == 100,
          "MOD A, HOLD: 10, nothing on the empty step, 100, and the repeat of 100 not sent again (%d CCs)", count_cc(20));
    CHECK(count_cc(21) == 1 && nth_cc_value(21, 0) == 50, "MOD B on CC 21: 50 on step 2, a step with no note");
    CHECK((got[first_cc].m[0] & 0x0F) == 0, "the CCs use the notes' channel (MIDI CH 1)");

    /* RETURN: the empty step sends BASE */
    void *r = api->create_instance(NULL, NULL);
    set(r, "audition", 0); set(r, "s1_moda", 10); set(r, "moda_mode", 1); set(r, "moda_base", 64);
    play(r, 3);
    CHECK(count_cc(20) == 2 && nth_cc_value(20, 0) == 10 && nth_cc_value(20, 1) == 64,
          "MOD A, RETURN: 10 on step 1, BASE 64 on step 2, nothing more while it stays 64");

    /* a lane switched off sends nothing; another channel moves the CCs with the notes */
    set(r, "moda_cc", 0); set(r, "channel", 5);
    play(r, 3);
    CHECK(count_cc(20) == 0, "MOD A off (CC 0): no CC");
    set(r, "moda_cc", 30);
    play(r, 2);
    CHECK(count_cc(30) >= 1 && (got[0].m[0] & 0x0F) == 4, "MOD A on CC 30 and MIDI CH 5: CC 30 on channel 5");

    /* the pattern's state keeps the MOD values and settings; an older SQ1 state still loads, with no MOD values */
    char st[4096];
    int n = api->get_param(r, "state", st, sizeof st);
    void *c = api->create_instance(NULL, NULL);
    api->set_param(c, "state", st);
    char b1[16], b2[16], b3[16];
    api->get_param(c, "s1_moda", b1, sizeof b1); api->get_param(c, "moda_cc", b2, sizeof b2); api->get_param(c, "moda_mode", b3, sizeof b3);
    CHECK(n > 0 && !strncmp(st, "SQ2 ", 4) && !strcmp(b1, "10") && !strcmp(b2, "30") && !strcmp(b3, "1"),
          "state SQ2 round trip: s1 MOD A = 10, MOD A on CC 30, RETURN (%d bytes)", n);
    char sq1[1024] = "SQ1 2 50 0 100 1 16 0 0 0 0 1 1 1 ";
    for (int i = 0; i < 64; i++) strcat(sq1, "3c032164641");
    api->set_param(c, "state", sq1);
    api->get_param(c, "s1_moda", b1, sizeof b1); api->get_param(c, "moda_cc", b2, sizeof b2); api->get_param(c, "s1_velo", b3, sizeof b3);
    CHECK(!strcmp(b1, "-1") && !strcmp(b2, "20") && !strcmp(b3, "100"), "an SQ1 state loads: no MOD values, MOD A on CC 20, velocity 100");

    api->destroy_instance(q); api->destroy_instance(r); api->destroy_instance(c);
    printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails != 0;
}
