/* probe <module_dir> cmd...   cmds: get:KEY  set:KEY=VAL  note:N (plays it, prints rms)  presets:COUNTKEY:SETKEY:NAMEKEY
 *   hold:N / off:N (note on / off)  dump:FILE:BLOCKS (render that many 128-frame blocks, write raw int16 stereo) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include "engine.h"
static double note(const mpc_engine_t *e, void *i, int n) {
    uint8_t on[3] = {0x90, (uint8_t)n, 100}, off[3] = {0x80, (uint8_t)n, 0}; int16_t b[256]; double s = 0;
    e->midi(i, on, 3);
    usleep(300000);   /* engines that render hits on a worker thread (libpo32) */
    for (int k = 0; k < 120; k++) { e->render(i, b, 128); for (int j = 0; j < 256; j++) s += (b[j] / 32768.0) * (b[j] / 32768.0); }
    e->midi(i, off, 3); for (int k = 0; k < 400; k++) e->render(i, b, 128);
    return sqrt(s / (120 * 256));
}
int main(int argc, char **argv) {
    const mpc_engine_t *e = mpc_engine(); void *i = e->create(argv[1]); static char buf[262144];
    int16_t b[256]; for (int k = 0; k < 8; k++) e->render(i, b, 128);
    for (int a = 2; a < argc; a++) {
        char *c = argv[a];
        if (!strncmp(c, "get:", 4)) { buf[0] = 0; int r = e->get_param(i, c + 4, buf, sizeof buf); printf("%s = %s%s\n", c + 4, r > 0 ? buf : "<none>", ""); }
        else if (!strncmp(c, "set:", 4)) { char k[128]; char *eq = strchr(c + 4, '='); snprintf(k, sizeof k, "%.*s", (int)(eq - c - 4), c + 4); e->set_param(i, k, eq + 1); for (int q = 0; q < 4; q++) e->render(i, b, 128); }
        else if (!strncmp(c, "note:", 5)) printf("note %s -> rms %.4f\n", c + 5, note(e, i, atoi(c + 5)));
        else if (!strncmp(c, "play:", 5) && mpc_engine_transport) {   /* play:N  N blocks at 120 BPM, transport running */
            static double ppq; int n = atoi(c + 5);
            for (int k = 0; k < n; k++) { mpc_engine_transport(i, 120, ppq, 1); e->render(i, b, 128); ppq += 128 * 2.0 / 44100; }
            printf("played %d blocks (%.2f beats)\n", n, ppq); fflush(stdout);
        }
        else if (!strcmp(c, "stop") && mpc_engine_transport) { mpc_engine_transport(i, 120, 0, 0); e->render(i, b, 128); printf("stopped\n"); }
        else if (!strncmp(c, "off:", 4)) { uint8_t off[3] = {0x80, (uint8_t)atoi(c + 4), 0}; e->midi(i, off, 3); }
        else if (!strncmp(c, "dump:", 5)) {   /* dump:FILE:BLOCKS */
            char path[512]; int n = 0; char *colon = strrchr(c + 5, ':');
            if (!colon) continue;
            snprintf(path, sizeof path, "%.*s", (int)(colon - c - 5), c + 5); n = atoi(colon + 1);
            FILE *fp = fopen(path, "wb"); if (!fp) { printf("dump: can't write %s\n", path); continue; }
            for (int k = 0; k < n; k++) { e->render(i, b, 128); fwrite(b, sizeof(int16_t), 256, fp); }
            fclose(fp); printf("dumped %d blocks to %s\n", n, path); fflush(stdout);
        }
        else if (!strncmp(c, "hold:", 5)) { uint8_t on[3] = {0x90, (uint8_t)atoi(c + 5), 100}; e->midi(i, on, 3); printf("holding %s\n", c + 5); }
        else if (!strncmp(c, "presets:", 8)) {   /* presets:count_key:set_key:name_key */
            char ck[64], sk[64], nk[64]; sscanf(c + 8, "%63[^:]:%63[^:]:%63s", ck, sk, nk);
            buf[0] = 0; e->get_param(i, ck, buf, sizeof buf); int n = atoi(buf); printf("%s = %d:", ck, n);
            for (int p = 0; p < n; p++) { char v[16]; snprintf(v, 16, "%d", p); e->set_param(i, sk, v); for (int q = 0; q < 2; q++) e->render(i, b, 128);
                buf[0] = 0; e->get_param(i, nk, buf, sizeof buf); printf("%s%s", p ? " | " : " ", buf); }
            printf("\n");
        }
    }
    e->destroy(i); return 0;
}
