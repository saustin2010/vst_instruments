/* A port's parameters right after MPC inserts it, for screenshots (showcase.py): linked by dump_state.sh with the
 * objects the offline test left in <port>/build (host_*.o, minus host_test's own), so it runs the real engine.
 * Prints one JSON object: {"<index>": [normalized value, "display text"], ...}. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
struct AEffect { int32_t magic; intptr_t (*d)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
 void*p; void (*setP)(AEffect*,int32_t,float); float (*getP)(AEffect*,int32_t);
 int32_t np,npar,ni,no,flags; intptr_t r1,r2; int32_t a,b,c; float io; void*obj,*user; int32_t uid,ver;
 void (*pr)(AEffect*,float**,float**,int32_t); void*pdr; char f[56]; };
extern AEffect* VSTPluginMain(cb);

static intptr_t host(AEffect*e,int32_t op,int32_t i,intptr_t v,void*p,float o){
    static double ti[16];
    if (op == 7) { ti[4] = 120.0; ((int32_t*)&ti[8])[5] = 1 << 10; return (intptr_t)ti; }   /* time info: 120 BPM */
    return 0;
}

int main(void) {
    AEffect *a = VSTPluginMain(host);
    if (!a) return 1;
    float L[128], R[128], *o[2] = {L, R};
    for (int k = 0; k < 8; k++) a->pr(a, 0, o, 128);   /* let an engine settle (presets applied on the audio thread) */
    printf("{");
    for (int i = 0; i < a->npar; i++) {
        char d[256] = "";
        a->d(a, 7, i, 0, d, 0);   /* effGetParamDisplay */
        printf("%s\"%d\": [%.6f, \"", i ? ", " : "", i, a->getP(a, i));
        for (char *c = d; *c; c++) {
            if (*c == '"' || *c == '\\') putchar('\\');
            if ((unsigned char)*c >= 32) putchar(*c);
        }
        printf("\"]");
    }
    printf("}\n");
    a->d(a, 1, 0, 0, 0, 0);   /* effClose */
    return 0;
}
