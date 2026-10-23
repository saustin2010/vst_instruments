/* Which parameters other labels follow (dev-tools/displaydeps/scan.sh <plugin folder>).
 *
 * Since sd88me's wrapper fix (#486, 2026-10-21) a plain host write no longer asks MPC for an UpdateDisplay, and MPC
 * re-reads a control's name, and the text of a control whose value didn't change, only on one. So a parameter that
 * renames another ("<key>_name") or changes the text another shows ("<key>_display") without moving its value needs
 * "refresh_display": true, or that label stays stale on the device.
 *
 * This sets each parameter through the plugin's VST interface, as MPC does, on a fresh instance, and lists the other
 * parameters whose name or shown text then changed while their value stayed put, or whose value moved without the
 * plugin telling MPC (audioMasterAutomate), unless the plugin asked MPC to redraw (audioMasterUpdateDisplay: MPC then
 * re-reads everything). Polled readouts ("display": "string" without "poll": false) and stepper buttons ("step_of")
 * are left out: the wrapper refreshes those itself.
 * Only what changes from the default state is seen (a label that differs only with a sample loaded, say, is not). */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include "params.h"

typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
struct AEffect { int32_t magic; intptr_t (*d)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
 void*p; void (*setP)(AEffect*,int32_t,float); float (*getP)(AEffect*,int32_t);
 int32_t np,npar,ni,no,flags; intptr_t r1,r2; int32_t a,b,c; float io; void*obj,*user; int32_t uid,ver;
 void (*pr)(AEffect*,float**,float**,int32_t); void*pdr; char f[56]; };
extern AEffect* VSTPluginMain(cb);

static double ti[16];
static int told[NPARAMS > 0 ? NPARAMS : 1];   /* audioMasterAutomate calls per param since the last set */
static int redrawn;                            /* audioMasterUpdateDisplay calls since the last set */
static intptr_t host(AEffect*e,int32_t op,int32_t i,intptr_t v,void*p,float o){
    if (op == 0 && i >= 0 && i < NPARAMS) told[i]++;
    if (op == 42) redrawn++;
    if (op == 7) { ti[4] = 120.0; ((int32_t*)&ti[8])[5] = 1 << 10; return (intptr_t)ti; }
    return 0;
}

static void run(AEffect *a, int blocks) {
    float L[128], R[128], I0[128] = {0}, I1[128] = {0}, *o[2] = {L, R}, *in[2] = {I0, I1};
    for (int k = 0; k < blocks; k++) a->pr(a, a->ni >= 2 ? in : 0, o, 128);
}

/* run some blocks, give an engine's own threads (a folder listing) time, run again */
static void settle(AEffect *a) { run(a, 4); usleep(20000); run(a, 4); }

typedef struct { char name[64], text[256]; float v; } snap_t;
static snap_t S0[NPARAMS], S1[NPARAMS];

static void snap(AEffect *a, snap_t *s) {
    for (int i = 0; i < NPARAMS; i++) {
        s[i].name[0] = s[i].text[0] = 0;
        a->d(a, 8, i, 0, s[i].name, 0);
        a->d(a, 7, i, 0, s[i].text, 0);
        s[i].v = a->getP(a, i);
    }
}

static AEffect *fresh(void) {
    AEffect *a = VSTPluginMain(host);
    a->d(a, 0, 0, 0, 0, 0);
    a->d(a, 10, 0, 0, 0, 44100.0f);
    a->d(a, 11, 0, 128, 0, 0);
    a->d(a, 12, 0, 1, 0, 0);
    settle(a);
    return a;
}

static int polled(int x) { return PARAMS[x].string_display && !PARAMS[x].no_poll; }

int main(void) {
    static char dep[NPARAMS > 0 ? NPARAMS : 1][NPARAMS > 0 ? NPARAMS : 1];   /* dep[y][x]: 1 name, 2 text, 4 value untold */
    int selectors = 0;
    for (int y = 0; y < NPARAMS; y++) {
        const param_t *p = &PARAMS[y];
        if (p->step_target >= 0 || p->refresh_display) continue;     /* the wrapper refreshes these already */
        if (p->string_display && !(p->max > p->min) && !p->nopts) continue;   /* a readout: nothing to write */
        float vals[10];
        int nv = 0;
        if (p->momentary) vals[nv++] = 1.0f;
        else if (p->nopts > 1) {
            int n = p->nopts - 1, step = n > 8 ? (n + 7) / 8 : 1;
            for (int k = 0; k <= n && nv < 9; k += step) vals[nv++] = (float)k / n;
            if (vals[nv - 1] < 1.0f) vals[nv++] = 1.0f;
        } else { vals[nv++] = 0.0f; vals[nv++] = 0.5f; vals[nv++] = 1.0f; }
        AEffect *a = fresh();
        snap(a, S0);
        for (int k = 0; k < nv; k++) {
            if (fabsf(vals[k] - S0[y].v) < 1e-6f && !p->momentary) continue;
            memset(told, 0, sizeof told);
            redrawn = 0;
            a->setP(a, y, vals[k]);
            if (p->momentary) { run(a, 2); a->setP(a, y, 0.0f); }
            settle(a);
            snap(a, S1);
            if (redrawn) continue;   /* MPC was asked to redraw (a refresh_display param, or the engine said so): it re-reads all */
            for (int x = 0; x < NPARAMS; x++) {
                if (x == y || polled(x) || PARAMS[x].step_target >= 0) continue;
                if (strcmp(S0[x].name, S1[x].name)) dep[y][x] |= 1;
                if (strcmp(S0[x].text, S1[x].text) && fabsf(S0[x].v - S1[x].v) < 1e-6f) dep[y][x] |= 2;
                if (fabsf(S0[x].v - S1[x].v) >= 1e-6f && !told[x]) dep[y][x] |= 4;
            }
        }
        a->d(a, 1, 0, 0, 0, 0);
        int n = 0;
        for (int x = 0; x < NPARAMS; x++) n += dep[y][x] != 0;
        if (!n) continue;
        selectors++;
        printf("%s ->", p->key);
        int shown = 0;
        for (int x = 0; x < NPARAMS && shown < 6; x++)
            if (dep[y][x]) {
                printf(" %s(%s%s%s)", PARAMS[x].key, dep[y][x] & 1 ? "name" : "", dep[y][x] & 2 ? (dep[y][x] & 1 ? "+text" : "text") : "",
                       dep[y][x] & 4 ? (dep[y][x] & 3 ? "+value" : "value") : "");
                shown++;
            }
        if (n > shown) printf(" +%d more", n - shown);
        printf("\n");
    }
    printf("refresh_display:");
    for (int y = 0; y < NPARAMS; y++) {
        int n = 0;
        for (int x = 0; x < NPARAMS; x++) n += dep[y][x] != 0;
        if (n) printf(" %s", PARAMS[y].key);
    }
    printf("\n%d of %d parameters change another's label\n", selectors, NPARAMS);
    return 0;
}
