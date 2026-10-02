/* =============================================================================
 * MAZE LITE  -  Slot MIDI FX for Ableton Move (Schwung)      [id: maze_seq_lite]
 * -----------------------------------------------------------------------------
 * Dual generative sequencer (Moog Labyrinth style) as a clock-synced MIDI FX.
 * Two 8-step generative sequencers run on the Move transport clock.
 *
 *  - Each "on" bit stores a random voltage, attenuated by CV Range (0..100),
 *    quantized to a scale, and played as a MIDI note.
 *  - Corrupt (0..100) mutates the stored voltages, and past 12 o'clock the bits.
 *  - Trig Mix cross-fades velocity/accents between Seq1 and Seq2.
 *  - Incoming notes are CONSUMED: they only set the ROOT (transpose).
 *
 * v1.1 changes:
 *  - Bit Flip / Advance are MOMENTARY triggers (enum access:"write" in the UI):
 *    they fire ONCE per press/turn, on any non-idle value.
 *  - trig_mix_b is an alias of trig_mix so the same Trig Mix control can live on
 *    both the Seq1 and Seq2 pages, always in sync (they read/write one variable).
 *
 * v1.3.2 change — Remote UI (web_ui.html) live sync, all read-only additions
 * to get_param (no set_param, nothing here writes any of it):
 *   - "running": mirrors L->running (already existed for gating clock
 *     pulses, 0xF8) so the panel can tell transport stopped from playing.
 *   - "s1_bits"/"s2_bits": the real 8-step bit pattern as a "10110010"-
 *     style string, so the panel's Bits LEDs show the actual sequence
 *     instead of a locally-simulated stand-in.
 *   - "s1_play"/"s2_play": the current step index, so the panel's playhead
 *     tracks the real one instead of a local timer guessing at it.
 *
 * No file I/O here, so nothing to move off the audio thread (unlike the tool).
 * You (a non-coder) only ever need to touch bits marked  ==>> EDIT ME.
 * ===========================================================================*/

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "midi_fx_api_v1.h"   /* compiled with -Isrc/include */

#define NUM_STEPS   8
#define MAX_ACTIVE 32

static uint32_t rng_state = 0x1234abcdu;
static inline uint32_t rng_next(void){ uint32_t x=rng_state; x^=x<<13; x^=x>>17; x^=x<<5; rng_state=x; return x; }
static inline float rng_f(void){ return (rng_next()>>8)*(1.0f/16777216.0f); }
static inline float rng_bip(void){ return rng_f()*2.0f-1.0f; }

/* Scales  (==>> EDIT ME: keep in sync with the "scale" enum in module.json) */
typedef struct { const int8_t *pc; int n; } scale_t;
static const int8_t SC_CHROM[]={0,1,2,3,4,5,6,7,8,9,10,11};
static const int8_t SC_MAJ[]  ={0,2,4,5,7,9,11};
static const int8_t SC_MIN[]  ={0,2,3,5,7,8,10};
static const int8_t SC_PMAJ[] ={0,2,4,7,9};
static const int8_t SC_PMIN[] ={0,3,5,7,10};
static const int8_t SC_MEL[]  ={0,2,3,5,7,9,11};
static const int8_t SC_HARM[] ={0,2,3,5,7,8,11};
static const int8_t SC_WHOLE[]={0,2,4,6,8,10};
static const int8_t SC_HIRA[] ={0,2,3,7,8};
static const int8_t SC_MAJ7[] ={0,4,7,11};
static const int8_t SC_MIN7[] ={0,3,7,10};
static const int8_t SC_UNQ[]  ={0,1,2,3,4,5,6,7,8,9,10,11};
static const scale_t SCALES[]={
    {SC_CHROM,12},{SC_MAJ,7},{SC_MIN,7},{SC_PMAJ,5},{SC_PMIN,5},
    {SC_MEL,7},{SC_HARM,7},{SC_WHOLE,6},{SC_HIRA,5},{SC_MAJ7,4},{SC_MIN7,4},{SC_UNQ,12}
};
#define NUM_SCALES ((int)(sizeof(SCALES)/sizeof(SCALES[0])))

static const int RATE_PULSES[]={3,6,12,24,48,96};
#define NUM_RATES ((int)(sizeof(RATE_PULSES)/sizeof(RATE_PULSES[0])))
/* Gate length as a multiple of one step: index 0..7 -> 1/4 .. 8/4 (i.e. 2) steps */
static const float GATE_STEPS[]={0.25f,0.5f,0.75f,1.0f,1.25f,1.5f,1.75f,2.0f};
#define NUM_GATES ((int)(sizeof(GATE_STEPS)/sizeof(GATE_STEPS[0])))

/* Sequence Reset: force the play head back to step 1 every N BARS.
   index 0..4 -> 1,2,4,8 bars, 0 = off (never reset). One bar = 96 clock pulses;
   every note rate divides it evenly, so N bars = N*(96/RATE_PULSES) steps.
   Keep in sync with the "s1_reset"/"s2_reset"/"g_reset" enum options in module.json. */
#define PULSES_PER_BAR 96
static const int RESET_BARS[]={1,2,4,8,0};
#define NUM_RESETS ((int)(sizeof(RESET_BARS)/sizeof(RESET_BARS[0])))
static inline int reset_bars_from_idx(int idx){
    if (idx<0) idx=0;
    if (idx>=NUM_RESETS) idx=NUM_RESETS-1;
    return RESET_BARS[idx];
}
static inline int reset_idx_from_bars(int bars){
    for (int i=0;i<NUM_RESETS;i++) if (RESET_BARS[i]==bars) return i;
    return NUM_RESETS-1;   /* unknown -> off */
}

#define MAX_SPREAD 64   /* ==>> EDIT ME: semitone spread each side of root */

typedef struct {
    int   bit[NUM_STEPS];
    float cv [NUM_STEPS];
    int   length, play, corrupt, cv_range;   /* corrupt/cv_range: 0..100 */
    int   reset_bars, reset_ctr;             /* reset_bars: bars per reset, 0 = off; reset_ctr counts steps */
} seq_t;
typedef struct { int note, ch; long off_pulse; int active; } voice_t;
typedef struct {
    seq_t s[2];
    int   scale, rate, gate, trig_mix, root;
    int   running; long pulse;
    voice_t voices[MAX_ACTIVE];
} maze_t;

/* A momentary trigger fires unless the incoming value is the idle spelling
   ("off") or the idle index ("0"). access:"write" means the host never scrubs
   it with a knob, so this only ever fires on a real press/turn. */
static inline int trig_fired(const char *val){
    return (strcmp(val,"off")!=0 && strcmp(val,"0")!=0);
}

static void seq_randomize(seq_t *q){
    q->length=NUM_STEPS; q->play=-1;
    for (int i=0;i<NUM_STEPS;i++){ q->bit[i]=(rng_f()<0.5f)?1:0; q->cv[i]=rng_bip(); }
}
static void seq_corrupt(seq_t *q, int idx){
    float c=q->corrupt/100.0f;
    float p_cv =(c<=0.5f)?(c*0.5f):(0.25f+(c-0.5f)*0.5f);
    float p_bit=(c<=0.5f)?0.0f:((c-0.5f));
    if (rng_f()<p_cv) q->cv[idx]=rng_bip();
    if (p_bit>0.0f && rng_f()<p_bit){ q->bit[idx]=!q->bit[idx]; if(q->bit[idx]) q->cv[idx]=rng_bip(); }
}
static int quantize_note(const maze_t *L, float cv, int cv_range){
    float scaled=cv*(cv_range/100.0f);
    int semi=(int)lrintf(scaled*MAX_SPREAD);
    int note=L->root+semi;
    if (note<0) note=0; if (note>127) note=127;
    const scale_t *sc=&SCALES[L->scale];
    int root_pc=((L->root%12)+12)%12;
    int pc=((note%12)+12)%12, best_pc=root_pc, bd=128;
    for (int i=0;i<sc->n;i++){
        int cand=(root_pc+sc->pc[i])%12;
        int d=abs(pc-cand); if(d>6)d=12-d;
        if(d<bd){ bd=d; best_pc=cand; }
    }
    int diff=best_pc-pc; if(diff>6)diff-=12; if(diff<-6)diff+=12;
    note+=diff;
    if (note<0) note+=12; if (note>127) note-=12;
    return note;
}
/* Trig Mix velocity curve: -63 Seq1=127/Seq2=off ; 0 both=100 ; +64 Seq2=127 */
static void trig_velocities(int trig_mix, int *v1, int *v2){
    if (trig_mix<=0){
        float f=(trig_mix+63)/63.0f;
        *v1=(int)lrintf(127.0f-27.0f*f);
        *v2=(trig_mix==-63)?0:(int)lrintf(1.0f+99.0f*f);
    } else {
        float f=trig_mix/64.0f;
        *v1=(int)lrintf(100.0f*(1.0f-f));
        *v2=(int)lrintf(100.0f+27.0f*f);
    }
    if(*v1<0)*v1=0; if(*v1>127)*v1=127; if(*v2<0)*v2=0; if(*v2>127)*v2=127;
}

static void add_voice(maze_t *L,int note,int ch,long off){
    for (int i=0;i<MAX_ACTIVE;i++) if(!L->voices[i].active){
        L->voices[i].note=note; L->voices[i].ch=ch; L->voices[i].off_pulse=off; L->voices[i].active=1; return; }
}
static int flush_due_offs(maze_t *L,uint8_t o[][3],int ln[],int max,int w){
    for (int i=0;i<MAX_ACTIVE&&w<max;i++) if(L->voices[i].active&&L->pulse>=L->voices[i].off_pulse){
        o[w][0]=0x80|(L->voices[i].ch&0x0F); o[w][1]=L->voices[i].note&0x7F; o[w][2]=0; ln[w]=3; w++;
        L->voices[i].active=0; }
    return w;
}
static int all_notes_off(maze_t *L,uint8_t o[][3],int ln[],int max,int w){
    for (int i=0;i<MAX_ACTIVE&&w<max;i++) if(L->voices[i].active){
        o[w][0]=0x80|(L->voices[i].ch&0x0F); o[w][1]=L->voices[i].note&0x7F; o[w][2]=0; ln[w]=3; w++;
        L->voices[i].active=0; }
    return w;
}
static int step_seq(maze_t *L,int which,int vel,uint8_t o[][3],int ln[],int max,int w){
    seq_t *q=&L->s[which];
    int n=q->length<1?1:q->length;
    /* Sequence Reset: every reset_bars bars, snap the play head back to step 1.
       steps/bar = 96/RATE_PULSES at the current note rate. */
    if (q->reset_bars>0){
        int thresh = q->reset_bars * (PULSES_PER_BAR / RATE_PULSES[L->rate]);
        if (thresh>0 && q->reset_ctr>=thresh){ q->play=-1; q->reset_ctr=0; }
    }
    q->play=(q->play+1)%n;
    q->reset_ctr++;
    seq_corrupt(q,q->play);
    if (q->bit[q->play] && vel>0 && w<max){
        int note=quantize_note(L,q->cv[q->play],q->cv_range);
        int ch=0;                              /* ==>> EDIT ME: output MIDI channel */
        o[w][0]=0x90|(ch&0x0F); o[w][1]=note&0x7F; o[w][2]=vel&0x7F; ln[w]=3; w++;
        long off=L->pulse+(long)lrintf(GATE_STEPS[L->gate]*RATE_PULSES[L->rate]);
        if (off<=L->pulse) off=L->pulse+1;
        add_voice(L,note,ch,off);
    }
    return w;
}
static void transport_reset(maze_t *L){
    L->pulse=0;
    L->s[0].play=-1; L->s[1].play=-1;
    L->s[0].reset_ctr=0; L->s[1].reset_ctr=0;
}

static void *maze_create(const char *dir,const char *cfg){
    (void)dir;(void)cfg;
    maze_t *L=(maze_t*)calloc(1,sizeof(maze_t));
    if(!L) return NULL;
    L->scale=1; L->rate=1; L->gate=3; L->trig_mix=0; L->root=60;
    rng_state^=(uint32_t)(uintptr_t)L|0x9e3779b9u;
    seq_randomize(&L->s[0]); seq_randomize(&L->s[1]);
    L->s[0].cv_range=50; L->s[1].cv_range=50;
    L->s[0].reset_bars=0; L->s[1].reset_bars=0;   /* default: off (never reset) */
    transport_reset(L);
    return L;
}
static void maze_destroy(void *inst){ if(inst) free(inst); }

static int maze_process(void *inst,const uint8_t *in,int in_len,
                        uint8_t o[][3],int ln[],int max){
    maze_t *L=(maze_t*)inst;
    if(!L||in_len<1) return 0;
    uint8_t st=in[0]; int w=0;
    switch(st){
        case 0xFA: transport_reset(L); L->running=1; return 0;
        case 0xFB: L->running=1; return 0;
        case 0xFC: L->running=0; return all_notes_off(L,o,ln,max,0);
        case 0xF8:
            if(!L->running) return 0;
            L->pulse++;
            w=flush_due_offs(L,o,ln,max,w);
            if (L->pulse % RATE_PULSES[L->rate] == 0){
                int v1,v2; trig_velocities(L->trig_mix,&v1,&v2);
                w=step_seq(L,0,v1,o,ln,max,w);
                w=step_seq(L,1,v2,o,ln,max,w);
            }
            return w;
        default: break;
    }
    if ((st&0xF0)==0x90 && in_len>=3 && in[2]>0){ L->root=in[1]&0x7F; return 0; }
    if ((st&0xF0)==0x80) return 0;
    if ((st&0xF0)==0x90 && in_len>=3 && in[2]==0) return 0;
    if (in_len<=3 && max>=1){ for(int i=0;i<in_len;i++) o[0][i]=in[i]; ln[0]=in_len; return 1; }
    return 0;
}
static int maze_tick(void *inst,int frames,int sr,uint8_t o[][3],int ln[],int max){
    (void)inst;(void)frames;(void)sr;(void)o;(void)ln;(void)max; return 0;
}
static void maze_set_param(void *inst,const char *key,const char *val){
    maze_t *L=(maze_t*)inst;
    if(!L||!key||!val) return;
    int v=atoi(val);
    if      (!strcmp(key,"s1_corrupt"))  L->s[0].corrupt  =(v<0?0:(v>100?100:v));
    else if (!strcmp(key,"s1_cv_range")) L->s[0].cv_range =(v<0?0:(v>100?100:v));
    else if (!strcmp(key,"s1_length"))   L->s[0].length   = (v<1?1:(v>8?8:v));
    /* momentary: flip the current step once per fire */
    else if (!strcmp(key,"s1_bit_flip")){ if(trig_fired(val)){ int p=L->s[0].play<0?0:L->s[0].play; L->s[0].bit[p]=!L->s[0].bit[p]; if(L->s[0].bit[p])L->s[0].cv[p]=rng_bip(); } }
    /* momentary: advance the play head once per fire */
    else if (!strcmp(key,"s1_advance")){ if(trig_fired(val)){ int n=L->s[0].length<1?1:L->s[0].length; L->s[0].play=(L->s[0].play+1)%n; } }
    else if (!strcmp(key,"s2_corrupt"))  L->s[1].corrupt  =(v<0?0:(v>100?100:v));
    else if (!strcmp(key,"s2_cv_range")) L->s[1].cv_range =(v<0?0:(v>100?100:v));
    else if (!strcmp(key,"s2_length"))   L->s[1].length   = (v<1?1:(v>8?8:v));
    else if (!strcmp(key,"s2_bit_flip")){ if(trig_fired(val)){ int p=L->s[1].play<0?0:L->s[1].play; L->s[1].bit[p]=!L->s[1].bit[p]; if(L->s[1].bit[p])L->s[1].cv[p]=rng_bip(); } }
    else if (!strcmp(key,"s2_advance")){ if(trig_fired(val)){ int n=L->s[1].length<1?1:L->s[1].length; L->s[1].play=(L->s[1].play+1)%n; } }
    /* Sequence Reset (in bars): per-seq, or g_reset for both at once */
    else if (!strcmp(key,"s1_reset"))    L->s[0].reset_bars=reset_bars_from_idx(v);
    else if (!strcmp(key,"s2_reset"))    L->s[1].reset_bars=reset_bars_from_idx(v);
    else if (!strcmp(key,"g_reset")){ int rb=reset_bars_from_idx(v); L->s[0].reset_bars=rb; L->s[1].reset_bars=rb; }
    /* trig_mix and its alias trig_mix_b both drive the SAME value (in sync) */
    else if (!strcmp(key,"trig_mix"))    L->trig_mix=(v<-63?-63:(v>64?64:v));
    else if (!strcmp(key,"trig_mix_b"))  L->trig_mix=(v<-63?-63:(v>64?64:v));
    else if (!strcmp(key,"scale"))       L->scale=(v<0?0:(v>=NUM_SCALES?NUM_SCALES-1:v));
    else if (!strcmp(key,"note_rate"))   L->rate=(v<0?0:(v>=NUM_RATES?NUM_RATES-1:v));
    else if (!strcmp(key,"note_length")) L->gate=(v<0?0:(v>=NUM_GATES?NUM_GATES-1:v));
}
static int maze_get_param(void *inst,const char *key,char *buf,int len){
    maze_t *L=(maze_t*)inst;
    if(!L||!key||!buf) return -1;
    /* momentary triggers report a constant idle spelling */
    if (!strcmp(key,"s1_bit_flip")||!strcmp(key,"s1_advance")||
        !strcmp(key,"s2_bit_flip")||!strcmp(key,"s2_advance"))
        return snprintf(buf,len,"off");
    /* read-only transport/pattern state for the Remote UI (not chain_params
     * — nothing on the device's own knob grid needs them). bits is an
     * 8-char '0'/'1' string, step 0 first; play is the current step index
     * (-1 before the first clock pulse). */
    if (!strcmp(key,"running")) return snprintf(buf,len,"%d",L->running);
    if (!strcmp(key,"s1_bits")||!strcmp(key,"s2_bits")){
        seq_t *q=&L->s[key[1]=='1'?0:1];
        char bits[NUM_STEPS+1];
        for (int i=0;i<NUM_STEPS;i++) bits[i]=q->bit[i]?'1':'0';
        bits[NUM_STEPS]='\0';
        return snprintf(buf,len,"%s",bits);
    }
    if (!strcmp(key,"s1_play")) return snprintf(buf,len,"%d",L->s[0].play);
    if (!strcmp(key,"s2_play")) return snprintf(buf,len,"%d",L->s[1].play);
    int v;
    if      (!strcmp(key,"s1_corrupt"))  v=L->s[0].corrupt;
    else if (!strcmp(key,"s1_cv_range")) v=L->s[0].cv_range;
    else if (!strcmp(key,"s1_length"))   v=L->s[0].length;
    else if (!strcmp(key,"s2_corrupt"))  v=L->s[1].corrupt;
    else if (!strcmp(key,"s2_cv_range")) v=L->s[1].cv_range;
    else if (!strcmp(key,"s2_length"))   v=L->s[1].length;
    else if (!strcmp(key,"trig_mix"))    v=L->trig_mix;
    else if (!strcmp(key,"trig_mix_b"))  v=L->trig_mix;   /* alias -> same value */
    else if (!strcmp(key,"s1_reset"))    v=reset_idx_from_bars(L->s[0].reset_bars);
    else if (!strcmp(key,"s2_reset"))    v=reset_idx_from_bars(L->s[1].reset_bars);
    else if (!strcmp(key,"g_reset"))     v=reset_idx_from_bars(L->s[0].reset_bars);
    else if (!strcmp(key,"scale"))       v=L->scale;
    else if (!strcmp(key,"note_rate"))   v=L->rate;
    else if (!strcmp(key,"note_length")) v=L->gate;
    else return -1;
    return snprintf(buf,len,"%d",v);
}
static midi_fx_api_v1_t g_api={
    .api_version=MIDI_FX_API_VERSION,
    .create_instance=maze_create,
    .destroy_instance=maze_destroy,
    .process_midi=maze_process,
    .tick=maze_tick,
    .set_param=maze_set_param,
    .get_param=maze_get_param
};
midi_fx_api_v1_t *move_midi_fx_init(const struct host_api_v1 *host){ (void)host; return &g_api; }
