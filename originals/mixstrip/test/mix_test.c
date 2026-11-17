/* Mix Strip, Mix Bus and Mix Desk together, offline: several strips and buses and a desk in one process, sharing the
 * region as they do in MPC. The framework's host test checks each plugin alone (tools/build.sh); this checks what they
 * do between them. Built by test/run.sh: mixstrip.c, mixbus.c and mixdesk.c with their exported names renamed, under
 * ASan and UBSan.
 *
 *     test/run.sh          (Docker gcc:12, from the plugin folder; prints PASSED or the failures) */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/mixshm.h"   /* only to pretend a controller is plugged in (the region's controllers count) */

typedef struct {
    void *(*create)(const char *data_dir);
    void (*destroy)(void *inst);
    void (*midi)(void *inst, const uint8_t *msg, int len);
    void (*set_param)(void *inst, const char *key, const char *val);
    int (*get_param)(void *inst, const char *key, char *buf, int buf_len);
    void (*render)(void *inst, int16_t *out_lr, int frames);
    void (*process)(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames);
} mpc_engine_t;
const mpc_engine_t *strip_engine(void);
const mpc_engine_t *bus_engine(void);
const mpc_engine_t *desk_engine(void);
int strip_changed(void *inst, char *key, int len);

#define N 128
static const mpc_engine_t *SE, *BE, *DE;
static int fails, checks;

#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void set(const mpc_engine_t *E, void *i, const char *k, const char *v) { E->set_param(i, k, v); }
static int geti(const mpc_engine_t *E, void *i, const char *k) {
    char b[128] = "";
    E->get_param(i, k, b, sizeof b);
    return atoi(b);
}
static float getf(const mpc_engine_t *E, void *i, const char *k) {
    char b[128] = "";
    E->get_param(i, k, b, sizeof b);
    return (float)atof(b);
}
static const char *gets_(const mpc_engine_t *E, void *i, const char *k) {
    static char b[8][128];
    static int n;
    char *s = b[n++ & 7];
    s[0] = 0;
    E->get_param(i, k, s, 128);
    return s;
}
static void cc(void *strip, int ch, int num, int v) {
    uint8_t m[3] = {(uint8_t)(0xB0 | (ch - 1)), (uint8_t)num, (uint8_t)v};
    SE->midi(strip, m, 3);
}
static void sine(int16_t *b, int frames, float hz, float amp, long *ph) {
    for (int i = 0; i < frames; i++, (*ph)++) {
        int16_t s = (int16_t)lrint(amp * 32767.0 * sin(2.0 * 3.141592653589793 * hz * (double)*ph / 44100.0));
        b[2 * i] = s;
        b[2 * i + 1] = (int16_t)-s;
    }
}
static double rms(const int16_t *b, int frames) {
    double s = 0;
    for (int i = 0; i < 2 * frames; i++) s += (double)b[i] * b[i];
    return sqrt(s / (2 * frames)) / 32768.0;
}
/* a sine through the strip for `blocks` blocks; the output's rms over the last 20 (2.3 cycles at 40 Hz) */
static double settle(void *strip, float hz, int blocks, long *ph) {
    int16_t in[2 * N], out[2 * N];
    double s = 0;
    for (int b = 0; b < blocks; b++) {
        sine(in, N, hz, 0.5f, ph);
        SE->process(strip, in, out, N);
        if (b >= blocks - 20)
            for (int i = 0; i < 2 * N; i++) s += (double)out[i] * out[i];
    }
    return sqrt(s / (20.0 * 2 * N)) / 32768.0;
}
static int changed_has(void *strip, const char *key) {   /* drains the strip's reports; 1 if key was among them */
    char k[64];
    int got = 0;
    while (strip_changed(strip, k, sizeof k) > 0) got |= !strcmp(k, key);
    return got;
}

int main(void) {
    SE = strip_engine();
    BE = bus_engine();
    DE = desk_engine();
    int16_t in[2 * N], out[2 * N], zero[2 * N] = {0};
    long ph = 0;

    /* ---- new strips take the lowest free TRACK; a bus starts on CUE ---- */
    void *bus = BE->create(NULL), *s1 = SE->create(NULL), *s2 = SE->create(NULL);
    CHECK(geti(SE, s1, "track") == 1 && geti(SE, s2, "track") == 2, "auto TRACK: %d %d", geti(SE, s1, "track"), geti(SE, s2, "track"));
    CHECK(geti(BE, bus, "bus") == 0, "bus starts on CUE");
    CHECK(!strcmp(gets_(SE, s1, "level_display"), "0.0 dB"), "level starts at 0 dB: %s", gets_(SE, s1, "level_display"));

    /* ---- at 0 dB, filter off, unmuted: the audio passes untouched ---- */
    for (int b = 0; b < 4; b++) {
        for (int i = 0; i < 2 * N; i++) in[i] = (int16_t)(rand() & 0xffff);
        SE->process(s1, in, out, N);
        CHECK(!memcmp(in, out, sizeof in), "block %d not passed through bit for bit", b);
    }
    CHECK(!strcmp(gets_(SE, s1, "status"), "COLUMN 1, CH 1: NO CONTROLLER"), "status: %s", gets_(SE, s1, "status"));

    /* ---- the controller: faders absolute, knobs relative, buttons on at 64 ---- */
    changed_has(s1, "");
    cc(s1, 1, 77, 0);   /* column 1 fader down */
    SE->process(s1, in, out, N);
    CHECK(getf(SE, s1, "level") == 0.0f, "fader 0 -> level %g", getf(SE, s1, "level"));
    CHECK(changed_has(s1, "level"), "level move reported");
    sine(in, N, 440, 0.5f, &ph);
    SE->process(s1, in, out, N);
    CHECK(rms(out, N) == 0.0, "level 0 is silent: %g", rms(out, N));
    cc(s1, 1, 77, 95);   /* back to (about) three-quarters: the detent holds 0 dB */
    SE->process(s1, in, out, N);
    CHECK(!strcmp(gets_(SE, s1, "level_display"), "0.0 dB"), "fader 95 -> %s", gets_(SE, s1, "level_display"));
    cc(s2, 1, 78, 127);   /* column 2 is the second strip's */
    SE->process(s2, in, out, N);
    CHECK(!strcmp(gets_(SE, s2, "level_display"), "+6.0 dB"), "strip 2 fader top: %s", gets_(SE, s2, "level_display"));
    CHECK(getf(SE, s1, "level") > 74 && getf(SE, s1, "level") < 76, "strip 1 left alone by column 2");
    for (int k = 0; k < 10; k++) cc(s1, 1, 13, 65);   /* SEND A: ten steps up */
    cc(s1, 1, 49, 60);                                 /* FILTER: four down rests at OFF (the detent) */
    SE->process(s1, in, out, N);
    CHECK(fabsf(getf(SE, s1, "send_a") - 1000.0f / 127.0f) < 0.02f, "relative send A: %g", getf(SE, s1, "send_a"));
    CHECK(getf(SE, s1, "filter") == 0.0f, "filter rests at OFF: %g", getf(SE, s1, "filter"));
    cc(s1, 1, 49, 57);                                 /* seven more: 11 down, 3 past the detent */
    SE->process(s1, in, out, N);
    CHECK(fabsf(getf(SE, s1, "filter") + 600.0f / 127.0f) < 0.02f, "relative filter past the detent: %g", getf(SE, s1, "filter"));
    cc(s1, 1, 49, 64 + 40);                            /* a fast turn back stops at OFF, not across */
    SE->process(s1, in, out, N);
    CHECK(getf(SE, s1, "filter") == 0.0f, "a turn back stops at OFF: %g", getf(SE, s1, "filter"));
    cc(s1, 1, 49, 64 + 8);                             /* eight up: still OFF; one more leaves it */
    SE->process(s1, in, out, N);
    CHECK(getf(SE, s1, "filter") == 0.0f, "eight up is still OFF: %g", getf(SE, s1, "filter"));
    cc(s1, 1, 49, 65);
    SE->process(s1, in, out, N);
    CHECK(fabsf(getf(SE, s1, "filter") - 200.0f / 127.0f) < 0.02f, "ninth leaves OFF: %g", getf(SE, s1, "filter"));
    set(SE, s1, "filter", "0");
    set(SE, s1, "knobs", "1");   /* ABSOLUTE */
    cc(s1, 1, 49, 67);
    SE->process(s1, in, out, N);
    CHECK(getf(SE, s1, "filter") == 0.0f, "absolute filter near the middle is OFF: %g", getf(SE, s1, "filter"));
    cc(s1, 1, 49, 127);
    SE->process(s1, in, out, N);
    CHECK(getf(SE, s1, "filter") == 100.0f, "absolute filter top: %g", getf(SE, s1, "filter"));
    cc(s1, 1, 49, 0);
    cc(s1, 1, 29, 127);
    SE->process(s1, in, out, N);
    CHECK(getf(SE, s1, "filter") == -100.0f && getf(SE, s1, "send_b") == 100.0f, "absolute knobs: %g %g", getf(SE, s1, "filter"), getf(SE, s1, "send_b"));
    set(SE, s1, "knobs", "0");
    set(SE, s1, "filter", "0");
    set(SE, s1, "send_a", "0");
    set(SE, s1, "send_b", "0");
    cc(s1, 1, 104, 127);   /* MUTE */
    SE->process(s1, in, out, N);
    CHECK(geti(SE, s1, "mute") == 1 && changed_has(s1, "mute"), "mute button");
    SE->process(s1, in, out, N);
    SE->process(s1, in, out, N);
    CHECK(rms(out, N) == 0.0, "muted after the 5 ms fade: %g", rms(out, N));
    cc(s1, 1, 104, 0);
    SE->process(s1, in, out, N);
    CHECK(geti(SE, s1, "mute") == 0, "unmute button");

    /* ---- CUE: the CUE bus plays a cued strip before its LEVEL and MUTE, a block or two late, sample for sample ---- */
    set(SE, s2, "level", "0");
    set(SE, s2, "mute", "1");
    cc(s2, 1, 113, 127);   /* column 2 CUE */
    for (int order = 0; order < 2; order++) {   /* the strip before the bus, then the bus first */
        int16_t hist[8][2 * N], got[2 * N];
        int lag = -1;
        for (int b = 0; b < 8; b++) {
            for (int i = 0; i < 2 * N; i++) in[i] = (int16_t)((rand() & 0x3fff) - 0x2000);
            memcpy(hist[b], in, sizeof in);
            if (order == 0) {
                SE->process(s2, in, out, N);
                BE->process(bus, zero, got, N);
            } else {
                BE->process(bus, zero, got, N);
                SE->process(s2, in, out, N);
            }
            if (b >= 2) CHECK(rms(out, N) == 0.0, "the muted strip's own output stays silent");
            if (b >= 4) {
                int l = -1;
                for (int k = 1; k <= 3 && l < 0; k++)
                    if (!memcmp(got, hist[b - k], sizeof got)) l = k;
                CHECK(l >= 1 && l <= 2, "order %d block %d: cue is not the strip's input 1-2 blocks back (%d)", order, b, l);
                if (lag < 0) lag = l;
                CHECK(l == lag, "order %d: the cue's lag moved (%d then %d)", order, lag, l);
            }
        }
    }
    CHECK(geti(SE, s2, "cue") == 1 && changed_has(s2, "cue"), "cue button");
    CHECK(!strcmp(gets_(BE, bus, "status"), "CUE: TRACK 2"), "bus status: %s", gets_(BE, bus, "status"));
    CHECK(!strcmp(gets_(BE, bus, "f2"), "MUTE  CUE") && !strcmp(gets_(BE, bus, "t2"), "OFF"), "overview 2: %s / %s", gets_(BE, bus, "t2"), gets_(BE, bus, "f2"));
    CHECK(!strcmp(gets_(BE, bus, "t1"), "0.0 dB") && !strcmp(gets_(BE, bus, "t3"), "-"), "overview 1/3: %s / %s", gets_(BE, bus, "t1"), gets_(BE, bus, "t3"));
    set(BE, bus, "level", "0");   /* the bus's own LEVEL */
    for (int b = 0; b < 3; b++) {
        sine(in, N, 440, 0.5f, &ph);
        SE->process(s2, in, out, N);
        BE->process(bus, zero, out, N);
    }
    CHECK(rms(out, N) == 0.0, "bus LEVEL off: %g", rms(out, N));
    set(BE, bus, "level", "75");

    /* ---- SOLO: a new cue turns the others off, and says so ---- */
    set(BE, bus, "cue_mode", "1");
    BE->process(bus, zero, out, N);   /* the CUE bus publishes its mode */
    cc(s1, 1, 112, 127);
    SE->process(s1, in, out, N);
    SE->process(s2, in, out, N);
    CHECK(geti(SE, s1, "cue") == 1 && geti(SE, s2, "cue") == 0, "solo: cues %d %d", geti(SE, s1, "cue"), geti(SE, s2, "cue"));
    CHECK(changed_has(s2, "cue"), "solo turning strip 2 off is reported");
    set(BE, bus, "cue_mode", "0");
    BE->process(bus, zero, out, N);
    set(SE, s2, "cue", "1");
    SE->process(s1, in, out, N);
    SE->process(s2, in, out, N);
    CHECK(geti(SE, s1, "cue") == 1 && geti(SE, s2, "cue") == 1, "add: both cues stay on");
    CHECK(!strcmp(gets_(BE, bus, "status"), "CUE: TRACK 1 + 2"), "bus status: %s", gets_(BE, bus, "status"));
    set(SE, s1, "cue", "0");
    set(SE, s2, "cue", "0");

    /* ---- FX A: the sends, after LEVEL and MUTE ---- */
    void *fx = BE->create(NULL);
    set(BE, fx, "bus", "1");
    set(SE, s2, "mute", "0");
    set(SE, s2, "level", "75");
    set(SE, s2, "send_a", "100");
    double r = 0;
    for (int b = 0; b < 6; b++) {
        sine(in, N, 440, 0.5f, &ph);
        SE->process(s2, in, out, N);
        BE->process(fx, zero, out, N);
        r = rms(out, N);
    }
    CHECK(fabs(r - 0.5 / sqrt(2.0)) < 0.01, "send A at 0 dB carries the strip: rms %g", r);
    CHECK(!strcmp(gets_(BE, fx, "status"), "1 OF 2 STRIPS SEND TO FX A"), "fx status: %s", gets_(BE, fx, "status"));
    set(SE, s2, "mute", "1");
    for (int b = 0; b < 6; b++) {
        sine(in, N, 440, 0.5f, &ph);
        SE->process(s2, in, out, N);
        BE->process(fx, zero, out, N);
    }
    CHECK(rms(out, N) == 0.0, "a muted strip sends nothing: %g", rms(out, N));
    set(SE, s2, "mute", "0");

    /* ---- KNOB 3 = SEND KILL: the third knob cuts the sends (left kills, right brings them back), the dry stays ---- */
    {
        int16_t fxo[2 * N];
        set(SE, s2, "knob3", "1");
        set(SE, s2, "filter", "-100");   /* not used while the third knob is SEND KILL */
        changed_has(s2, "");
        cc(s2, 1, 50, 63);               /* column 2, third knob, one step left */
        for (int b = 0; b < 6; b++) {
            sine(in, N, 440, 0.5f, &ph);
            SE->process(s2, in, out, N);
            BE->process(fx, zero, fxo, N);
            if (b >= 2) CHECK(!memcmp(in, out, sizeof in), "block %d: SEND KILL leaves the dry sound (and no filter) alone", b);
        }
        CHECK(geti(SE, s2, "send_kill") == 1 && geti(SE, s2, "filter") == -100 && changed_has(s2, "send_kill"),
              "left kills the sends, reported, filter untouched");
        CHECK(rms(fxo, N) == 0.0, "killed sends are silent: %g", rms(fxo, N));
        CHECK(!strcmp(gets_(BE, fx, "f2"), "KILL"), "overview shows KILL: %s", gets_(BE, fx, "f2"));
        CHECK(!strcmp(gets_(BE, fx, "status"), "0 OF 2 STRIPS SEND TO FX A"), "fx status: %s", gets_(BE, fx, "status"));
        cc(s2, 1, 50, 66);               /* right brings them back */
        for (int b = 0; b < 6; b++) {
            sine(in, N, 440, 0.5f, &ph);
            SE->process(s2, in, out, N);
            BE->process(fx, zero, fxo, N);
        }
        CHECK(geti(SE, s2, "send_kill") == 0 && fabs(rms(fxo, N) - 0.5 / sqrt(2.0)) < 0.01, "sends back: rms %g", rms(fxo, N));
        set(SE, s2, "knobs", "1");       /* an absolute knob: below the middle kills */
        cc(s2, 1, 50, 20);
        SE->process(s2, in, out, N);
        CHECK(geti(SE, s2, "send_kill") == 1, "absolute 20 kills");
        cc(s2, 1, 50, 100);
        SE->process(s2, in, out, N);
        CHECK(geti(SE, s2, "send_kill") == 0, "absolute 100 opens");
        set(SE, s2, "knobs", "0");
        set(SE, s2, "knob3", "0");
        set(SE, s2, "filter", "0");
    }
    set(SE, s2, "send_a", "0");

    /* ---- FILTER: low-pass to the left, high-pass to the right ---- */
    {
        double lo, hi;
        set(SE, s2, "filter", "-100");
        hi = settle(s2, 5000, 60, &ph);
        lo = settle(s2, 40, 60, &ph);
        CHECK(hi < 0.005 && lo > 0.25, "LP fully left: 5 kHz %g, 40 Hz %g", hi, lo);
        set(SE, s2, "filter", "100");
        lo = settle(s2, 100, 60, &ph);
        hi = settle(s2, 15000, 60, &ph);
        CHECK(lo < 0.01 && hi > 0.25, "HP fully right: 100 Hz %g, 15 kHz %g", lo, hi);
        CHECK(!strcmp(gets_(SE, s2, "filter_display"), "HP 8.0 kHz"), "filter text %s", gets_(SE, s2, "filter_display"));
        set(SE, s2, "filter", "0");
        for (int b = 0; b < 60; b++) SE->process(s2, in, out, N);
        for (int i = 0; i < 2 * N; i++) in[i] = (int16_t)(rand() & 0xffff);
        SE->process(s2, in, out, N);
        CHECK(!memcmp(in, out, sizeof in), "filter back to OFF passes untouched");
    }

    /* ---- one TRACK, one strip: a second strip on it says so, and takes it when the first goes ---- */
    void *s3 = SE->create(NULL);
    CHECK(geti(SE, s3, "track") == 3, "third strip TRACK 3: %d", geti(SE, s3, "track"));
    set(SE, s3, "track", "1");
    SE->process(s3, in, out, N);
    CHECK(!strcmp(gets_(SE, s3, "status"), "TRACK 1 IS ON ANOTHER STRIP"), "conflict status: %s", gets_(SE, s3, "status"));
    SE->destroy(s1);
    SE->process(s3, in, out, N);
    CHECK(!strcmp(gets_(SE, s3, "status"), "COLUMN 1, CH 1: NO CONTROLLER"), "took TRACK 1: %s", gets_(SE, s3, "status"));

    /* ---- MIDI CH and KNOBS carry over to new strips; TRACK 9-16 are page 2, on MIDI CH + 1 ---- */
    set(SE, s3, "midi_ch", "6");
    set(SE, s3, "knobs", "1");
    void *s4 = SE->create(NULL);
    CHECK(geti(SE, s4, "midi_ch") == 6 && geti(SE, s4, "knobs") == 1, "new strip: MIDI CH %d KNOBS %d", geti(SE, s4, "midi_ch"), geti(SE, s4, "knobs"));
    set(SE, s4, "track", "9");
    SE->process(s4, in, out, N);
    CHECK(!strcmp(gets_(SE, s4, "status"), "COLUMN 1, CH 7: NO CONTROLLER"), "page 2: %s", gets_(SE, s4, "status"));
    set(SE, s4, "track", "11");
    CHECK(!strcmp(gets_(SE, s4, "cc_send_a"), "CC 15") && !strcmp(gets_(SE, s4, "cc_cue"), "CC 114") &&
          !strcmp(gets_(SE, s4, "cc_level"), "CC 79"), "CC readouts: %s %s %s", gets_(SE, s4, "cc_send_a"), gets_(SE, s4, "cc_level"), gets_(SE, s4, "cc_cue"));
    set(SE, s4, "track", "9");
    cc(s4, 6, 77, 0);   /* page 1 column 1 is strip 3's */
    cc(s4, 7, 77, 127);
    SE->process(s4, in, out, N);
    SE->process(s3, in, out, N);
    CHECK(getf(SE, s4, "level") == 100.0f && getf(SE, s3, "level") == 0.0f, "pages: %g %g", getf(SE, s4, "level"), getf(SE, s3, "level"));

    /* ---- state round trip ---- */
    {
        char st[256];
        set(SE, s4, "filter", "-37.5");
        set(SE, s4, "send_b", "12.25");
        SE->get_param(s4, "state", st, sizeof st);
        void *s5 = SE->create(NULL);
        set(SE, s5, "state", st);
        char st2[256];
        SE->get_param(s5, "state", st2, sizeof st2);
        CHECK(!strcmp(st, st2), "state round trip:\n  %s\n  %s", st, st2);
        SE->destroy(s5);
        BE->get_param(fx, "state", st, sizeof st);
        void *b2 = BE->create(NULL);
        set(BE, b2, "state", st);
        BE->get_param(b2, "state", st2, sizeof st2);
        CHECK(!strcmp(st, st2), "bus state round trip: %s / %s", st, st2);
        BE->destroy(b2);
    }

    /* ---- Mix Desk: any column on any track; LEVEL + MUTE to MPC's own fader and Track Mute; BUTTON 2 = SEND KILL ---- */
    {
        int16_t fxo[2 * N];
        double fr = 0;
        set(SE, s2, "midi_ch", "6");
        void *d = DE->create(NULL);
        CHECK(geti(DE, d, "midi_ch") == 6 && geti(DE, d, "col1") == 1 && geti(DE, d, "col16") == 16 && geti(DE, d, "mode") == 0,
              "desk starts: MIDI CH %d (the strips'), column n = track n, STRIPS", geti(DE, d, "midi_ch"));
        for (int i = 0; i < 2 * N; i++) in[i] = (int16_t)(rand() & 0xffff);
        DE->process(d, in, out, N);   /* takes the columns */
        CHECK(!memcmp(in, out, sizeof in), "the desk passes its audio through");
        CHECK(!strcmp(gets_(DE, d, "status"), "NO CONTROLLER FOUND (COLUMNS ON CH 6 + 7)"), "desk status: %s", gets_(DE, d, "status"));
        SE->process(s2, in, out, N);
        SE->process(s3, in, out, N);
        SE->process(s4, in, out, N);
        CHECK(!strcmp(gets_(SE, s3, "status"), "DESK COLUMN 1, CH 6: NO CONTROLLER") &&
              !strcmp(gets_(SE, s2, "status"), "DESK COLUMN 2, CH 6: NO CONTROLLER"), "strips on their columns: %s / %s",
              gets_(SE, s3, "status"), gets_(SE, s2, "status"));
        CHECK(!strcmp(gets_(SE, s4, "status"), "DESK COLUMN 9, CH 7: NO CONTROLLER") && !strcmp(gets_(SE, s4, "cc_level"), "CC 77"),
              "track 9 on page 2's first column: %s %s", gets_(SE, s4, "status"), gets_(SE, s4, "cc_level"));
        set(DE, d, "col9", "0");
        DE->process(d, in, out, N);
        SE->process(s4, in, out, N);
        CHECK(!strcmp(gets_(SE, s4, "status"), "TRACK 9 HAS NO COLUMN ON MIX DESK") && !strcmp(gets_(SE, s4, "cc_level"), "-"),
              "column 9 OFF: track 9 has no column: %s", gets_(SE, s4, "status"));
        set(DE, d, "col12", "9");   /* track 9 on page 2's fourth column */
        DE->process(d, in, out, N);
        SE->process(s4, in, out, N);
        cc(s4, 7, 80, 0);
        SE->process(s4, in, out, N);
        CHECK(!strcmp(gets_(SE, s4, "status"), "DESK COLUMN 12, CH 7: NO CONTROLLER") && getf(SE, s4, "level") == 0.0f,
              "column 12 moves track 9: %s %g", gets_(SE, s4, "status"), getf(SE, s4, "level"));
        set(DE, d, "col12", "12");
        set(DE, d, "col9", "9");

        /* columns 1 and 2 swapped: column 1's fader moves track 2's strip, not track 1's */
        set(DE, d, "col1", "2");
        set(DE, d, "col2", "1");
        DE->process(d, in, out, N);
        SE->process(s2, in, out, N);
        SE->process(s3, in, out, N);
        CHECK(!strcmp(gets_(SE, s2, "status"), "DESK COLUMN 1, CH 6: NO CONTROLLER") && !strcmp(gets_(SE, s2, "cc_level"), "CC 77"),
              "track 2 on column 1: %s %s", gets_(SE, s2, "status"), gets_(SE, s2, "cc_level"));
        set(SE, s2, "level", "75");
        set(SE, s3, "level", "75");
        cc(s2, 6, 77, 0);
        SE->process(s2, in, out, N);
        SE->process(s3, in, out, N);
        CHECK(getf(SE, s2, "level") == 0.0f && getf(SE, s3, "level") == 75.0f, "column 1 moved track 2: %g %g",
              getf(SE, s2, "level"), getf(SE, s3, "level"));
        CHECK(!strcmp(gets_(DE, d, "lv1"), "OFF") && !strcmp(gets_(DE, d, "lv2"), "0.0 dB") && !strcmp(gets_(DE, d, "sd1"), "A OFF  B OFF"),
              "desk shows the strips: %s %s %s", gets_(DE, d, "lv1"), gets_(DE, d, "lv2"), gets_(DE, d, "sd1"));
        CHECK(!strcmp(gets_(DE, d, "lv3"), "NO STRIP"), "column 3 (track 3) has no strip: %s", gets_(DE, d, "lv3"));

        /* MPC MIXER: the column's fader and MUTE go to MPC, the strip leaves its LEVEL and MUTE alone */
        set(DE, d, "mode", "1");
        DE->process(d, in, out, N);
        SE->process(s2, in, out, N);
        SE->process(s3, in, out, N);   /* each strip reads the desk's settings in its own block */
        {
            mixshm_t *m = mix_open();   /* the same region: as if the listener had found a controller */
            MIX_STORE(m->controllers, 1u);
            CHECK(!strcmp(gets_(SE, s2, "status"), "DESK COLUMN 1, LEVEL + MUTE ON MPC") &&
                  !strcmp(gets_(SE, s3, "status"), "DESK COLUMN 2, LEVEL + MUTE ON MPC") &&
                  !strcmp(gets_(DE, d, "status"), "MPC MIXER: NO MIDI PORT"), "MPC MIXER status: %s / %s / %s",
                  gets_(SE, s2, "status"), gets_(SE, s3, "status"), gets_(DE, d, "status"));
            set(DE, d, "mode", "0");
            CHECK(!strcmp(gets_(DE, d, "status"), "LEVEL + MUTE ON THE STRIPS (CH 6 + 7)"), "STRIPS status: %s", gets_(DE, d, "status"));
            set(DE, d, "mode", "1");
            MIX_STORE(m->controllers, 0u);
            mix_close(m);
        }
        gets_(DE, d, "sent");
        set(SE, s2, "level", "0");   /* the strip's own LEVEL and MUTE: not used now */
        set(SE, s2, "mute", "1");
        cc(s2, 6, 77, 40);
        DE->process(d, in, out, N);
        CHECK(!strcmp(gets_(DE, d, "sent"), "B0 67 28;"), "fader to MPC track 2's volume (CC 103 ch 1)");
        for (int b = 0; b < 3; b++) {
            for (int i = 0; i < 2 * N; i++) in[i] = (int16_t)(rand() & 0xffff);
            SE->process(s2, in, out, N);
            if (b) CHECK(!memcmp(in, out, sizeof in), "block %d: MPC MIXER strip passes bit for bit (LEVEL 0, MUTE on)", b);
        }
        CHECK(getf(SE, s2, "level") == 0.0f && !strcmp(gets_(DE, d, "lv1"), "FADER 40"), "column didn't move the strip; desk %s",
              gets_(DE, d, "lv1"));
        set(SE, s2, "mute", "0");
        set(SE, s2, "send_a", "100");   /* the sends follow the fader (MPC's comes after the strip) */
        cc(s2, 6, 77, 0);
        for (int b = 0; b < 6; b++) {
            DE->process(d, zero, out, N);
            sine(in, N, 440, 0.5f, &ph);
            SE->process(s2, in, out, N);
            BE->process(fx, zero, fxo, N);
        }
        CHECK(rms(fxo, N) == 0.0 && rms(out, N) > 0.3, "fader down: no send (%g), the dry is MPC's to turn down (%g)", rms(fxo, N), rms(out, N));
        cc(s2, 6, 77, 95);
        for (int b = 0; b < 8; b++) {   /* rms over the last 4 blocks (5 cycles): one block's depends on the phase */
            DE->process(d, zero, out, N);
            sine(in, N, 440, 0.5f, &ph);
            SE->process(s2, in, out, N);
            BE->process(fx, zero, fxo, N);
            if (b >= 4) fr += rms(fxo, N) * rms(fxo, N) / 4.0;
        }
        fr = sqrt(fr);
        CHECK(fabs(fr - 0.5 / sqrt(2.0)) < 0.01, "fader at 95 (0 dB): the send at 0 dB: %g", fr);
        gets_(DE, d, "sent");
        cc(s2, 6, 104, 127);   /* column 1 MUTE */
        DE->process(d, zero, out, N);
        CHECK(!strcmp(gets_(DE, d, "sent"), "B1 67 7F;"), "mute to MPC track 2's Track Mute (CC 103 ch 2)");
        SE->process(s2, in, out, N);
        CHECK(geti(SE, s2, "mute") == 0 && !strcmp(gets_(DE, d, "st1"), "MUTE"), "strip's MUTE untouched, desk shows MUTE");
        CHECK(!strcmp(gets_(BE, fx, "t2"), "ON MPC") && !strcmp(gets_(BE, fx, "f2"), "MUTE"), "bus overview: %s / %s",
              gets_(BE, fx, "t2"), gets_(BE, fx, "f2"));
        set(DE, d, "mute_out", "1");   /* PRESS: every message is a press */
        cc(s2, 6, 104, 0);
        DE->process(d, zero, out, N);
        CHECK(!strcmp(gets_(DE, d, "sent"), "B1 67 7F;B1 67 00;") && !strcmp(gets_(DE, d, "st1"), " "), "PRESS: %s", gets_(DE, d, "st1"));
        set(DE, d, "mute_out", "0");
        set(DE, d, "col3", "0");   /* a column set OFF sends nothing */
        DE->process(d, zero, out, N);
        cc(s2, 6, 79, 100);
        DE->process(d, zero, out, N);
        CHECK(!strcmp(gets_(DE, d, "sent"), " ") && !strcmp(gets_(DE, d, "lv3"), "-"), "OFF column: nothing sent");
        set(DE, d, "col3", "2");   /* two columns on one track: the desk says so */
        CHECK(!strcmp(gets_(DE, d, "st3"), "SAME AS 1"), "duplicate: %s", gets_(DE, d, "st3"));
        set(DE, d, "col3", "3");

        /* BUTTON 2 = SEND KILL: a cue left on goes off; the button cuts the sends, the dry plays on */
        set(SE, s2, "cue", "1");
        changed_has(s2, "");
        set(DE, d, "button2", "1");
        DE->process(d, zero, out, N);
        SE->process(s2, in, out, N);
        CHECK(geti(SE, s2, "cue") == 0 && changed_has(s2, "cue"), "BUTTON 2 = SEND KILL turned the cue off");
        cc(s2, 6, 112, 127);   /* column 1's BUTTON 2 */
        for (int b = 0; b < 6; b++) {
            DE->process(d, zero, out, N);
            sine(in, N, 440, 0.5f, &ph);
            SE->process(s2, in, out, N);
            BE->process(fx, zero, fxo, N);
            if (b >= 2) CHECK(!memcmp(in, out, sizeof in), "block %d: SEND KILL leaves the dry alone", b);
        }
        CHECK(geti(SE, s2, "send_kill") == 1 && geti(SE, s2, "cue") == 0 && rms(fxo, N) == 0.0, "BUTTON 2 kills the sends: %g", rms(fxo, N));
        CHECK(!strcmp(gets_(DE, d, "st1"), "KILL"), "desk shows KILL: %s", gets_(DE, d, "st1"));
        cc(s2, 6, 112, 0);
        SE->process(s2, in, out, N);
        CHECK(geti(SE, s2, "send_kill") == 0, "BUTTON 2 off opens the sends");

        /* state, a second desk waiting, and the desk going */
        char st[512], st2[512];
        DE->get_param(d, "state", st, sizeof st);
        void *d2 = DE->create(NULL);
        set(DE, d2, "state", st);
        DE->get_param(d2, "state", st2, sizeof st2);
        CHECK(!strcmp(st, st2) &&  !strcmp(st, "mode=1;button2=1;midi_ch=6;mute_out=0;col1=2;col2=1;col3=3;col4=4;col5=5;col6=6;col7=7;col8=8;col9=9;"
                                    "col10=10;col11=11;col12=12;col13=13;col14=14;col15=15;col16=16;"),
              "desk state round trip:\n  %s\n  %s", st, st2);
        DE->process(d2, zero, out, N);
        CHECK(!strcmp(gets_(DE, d2, "status"), "ANOTHER MIX DESK HAS THE COLUMNS"), "second desk: %s", gets_(DE, d2, "status"));
        DE->destroy(d);
        DE->process(d2, zero, out, N);
        CHECK(strcmp(gets_(DE, d2, "status"), "ANOTHER MIX DESK HAS THE COLUMNS"), "the second desk takes over: %s", gets_(DE, d2, "status"));
        DE->destroy(d2);
        set(SE, s2, "send_a", "0");
        sine(in, N, 440, 0.5f, &ph);
        SE->process(s2, in, out, N);
        SE->process(s2, in, out, N);
        CHECK(!strcmp(gets_(SE, s2, "status"), "COLUMN 2, CH 6: NO CONTROLLER") && rms(out, N) == 0.0,
              "no desk: the strip's own column and LEVEL (0) again: %s %g", gets_(SE, s2, "status"), rms(out, N));
    }

    SE->destroy(s2);
    SE->destroy(s3);
    SE->destroy(s4);
    BE->destroy(bus);
    BE->destroy(fx);
    char path[64];
    snprintf(path, sizeof path, "/dev/shm/mixstrip-v2-%d", (int)getpid());
    remove(path);
    snprintf(path, sizeof path, "/tmp/mixstrip-v2-%d", (int)getpid());
    remove(path);
    printf("%s: %d checks, %d failed\n", fails ? "FAILED" : "PASSED", checks, fails);
    return fails != 0;
}
