/**
 * Wurl — Physically-modeled Wurlitzer 200A Electric Piano
 * Schwung sound generator for Ableton Move
 *
 * Port of OpenWurli (hal0zer0, GPL-3.0) to single-file C.
 * Signal chain: Modal reed -> Pickup -> [sum voices] -> 2x oversample
 *               -> Preamp (with tremolo LDR) -> downsample -> Volume
 *               -> PowerAmp -> Speaker
 *
 * 16-voice polyphony with 5ms crossfade voice stealing.
 * MLP v2 per-note corrections (runs once at note-on).
 * 2x polyphase IIR oversampling on preamp.
 *
 * Author: fillioning (port), Original: hal0zer0/openwurli
 * License: GPL-3.0
 * API: plugin_api_v2_t
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define SAMPLE_RATE     44100.0f
#define SAMPLE_RATE_D   44100.0
#define OS_SAMPLE_RATE  88200.0
#define NUM_MODES       7
#define NUM_VOICES      16
#define BLOCK_SIZE      128
#define MIDI_LO         33
#define MIDI_HI         96

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TWO_PI (2.0 * M_PI)

/* Pickup */
#define PICKUP_TAU          (287.0e3 * 240.0e-12)
#define PICKUP_SENSITIVITY  1.8375
#define PICKUP_MAX_Y        0.98

/* Tremolo */
#define TREM_RATE_HZ        5.63
#define LDR_ATTACK_TAU      0.003
#define LDR_RELEASE_TAU     0.050
#define LDR_R_MIN           50.0
#define LDR_R_MAX           1000000.0
#define LDR_GAMMA           1.1

/* Power amp */
#define PA_OPEN_LOOP_GAIN   19000.0
#define PA_FEEDBACK_BETA    (220.0 / (220.0 + 15000.0))
#define PA_HEADROOM         22.0
#define PA_CROSSOVER_VT     0.013
#define PA_QUIESCENT_GAIN   0.1
#define PA_NR_MAX_ITER      8
#define PA_NR_TOL           1e-6

/* Speaker */
#define SPK_HPF_AUTH_HZ     95.0
#define SPK_HPF_Q           0.75
#define SPK_LPF_AUTH_HZ     5500.0
#define SPK_LPF_Q           0.707
#define SPK_HPF_BYPASS_HZ   20.0
#define SPK_LPF_BYPASS_HZ   20000.0
#define SPK_THERMAL_TAU     5.0

/* Reed jitter */
#define JITTER_SIGMA        0.0004
#define JITTER_TAU_S        0.020
#define JITTER_SUBSAMPLE    16
#define RENORM_INTERVAL     1024
#define SQRT_3              1.7320508080

/* Preamp simplified */
#define PREAMP_GAIN         40.0
#define PREAMP_CIN_FC       329.0
#define PREAMP_BW_FC        15500.0
#define PREAMP_SAT_POS      6.0
#define PREAMP_SAT_NEG      3.0

/* Output */
#define POST_SPEAKER_GAIN   2.2

/* Voice stealing crossfade */
#define STEAL_FADE_MS       5.0
#define STEAL_FADE_SAMPLES  ((int)(SAMPLE_RATE_D * STEAL_FADE_MS / 1000.0))

/* MLP */
#define MLP_HIDDEN          8
#define MLP_OUTPUTS         11
#define MLP_N_FREQ          5
#define MLP_N_DECAY         5
#define MLP_DS_IDX          10
#define MLP_TRAIN_LO        65.0
#define MLP_TRAIN_HI        97.0
#define MLP_FADE_SEMI       12.0
#define MLP_MIDI_MIN        21.0
#define MLP_MIDI_MAX        108.0

static const double BASE_MODE_AMPS[NUM_MODES] =
    {1.0, 0.005, 0.0035, 0.0018, 0.0011, 0.0007, 0.0005};

/* ── Eigenvalue table ─────────────────────────────────────────────── */
#define N_EIG_ROWS 8
static const double EIG_MU[N_EIG_ROWS] = {0.00,0.01,0.05,0.10,0.15,0.20,0.30,0.50};
static const double EIG_BETAS[N_EIG_ROWS][NUM_MODES] = {
    {1.8751,4.6941,7.8548,10.9955,14.1372,17.2788,20.4204},
    {1.8584,4.6849,7.8504,10.9930,14.1356,17.2776,20.4195},
    {1.7920,4.6477,7.8316,10.9830,14.1288,17.2726,20.4158},
    {1.7227,4.6024,7.8077,10.9700,14.1198,17.2660,20.4110},
    {1.6625,4.5618,7.7859,10.9580,14.1114,17.2598,20.4065},
    {1.6097,4.5254,7.7659,10.9470,14.1036,17.2540,20.4023},
    {1.5201,4.4620,7.7310,10.9280,14.0894,17.2434,20.3946},
    {1.3853,4.3601,7.6745,10.8970,14.0650,17.2252,20.3814},
};

/* ── MLP Weights (from ml/generate_rust_weights.py) ──────────────── */
static const double MLP_W1[MLP_HIDDEN][2] = {
    {1.56686981472843877e-19, 3.09290068863936887e-18},
    {-9.08862514285822018e-17, -1.47885679252835237e-16},
    {-1.38144184980149021e-09, -1.64411306791123479e-09},
    {1.95399487018585205e+00, -7.03737437725067139e-01},
    {2.30859661102294922e+00, -1.01678681373596191e+00},
    {-1.26831877231597900e+00, 1.29154849052429199e+00},
    {2.24526381492614746e+00, -9.83363211154937744e-01},
    {-9.60274747089165381e-14, -8.15704546009571319e-13},
};
static const double MLP_B1[MLP_HIDDEN] = {
    -5.42883433666104871e-18, -2.19092708338739872e-16,
    -4.39656266948418306e-09, -1.92866057157516479e-01,
    -1.27982497215270996e-01, 1.62702167034149170e+00,
    -1.16396985948085785e-01, -4.82681663527273486e-08,
};
static const double MLP_W2[MLP_HIDDEN][MLP_HIDDEN] = {
    {4.71490881203694392e-19,-6.23992215039685723e-18,3.68967356667582180e-10,4.12995100021362305e-01,1.92521438002586365e-02,1.07265138626098633e+00,2.96818703413009644e-01,-3.35988258911352136e-30},
    {-3.68874680120698837e-19,3.57918073201489884e-18,-1.61619417848868352e-09,1.58253407478332520e+00,1.67132282257080078e+00,-1.04586672782897949e+00,1.33839440345764160e+00,6.76914737596234772e-07},
    {-5.41381697603772506e-19,-6.26324078545503335e-18,-4.16821299698000303e-10,1.45379066467285156e+00,1.74438750743865967e+00,8.76122415065765381e-02,1.82204794883728027e+00,3.25532340104290761e-30},
    {-4.39377403378913052e-19,-8.54205182191563876e-18,3.23977311644796373e-09,1.59806489944458008e+00,1.55876457691192627e+00,-2.91198343038558960e-01,1.41463243961334229e+00,6.89096370528306034e-31},
    {-2.78115223899492438e-18,4.10711618483155795e-17,8.27387403035118041e-09,1.30992364883422852e+00,1.52749049663543701e+00,-5.92434525489807129e-01,1.03794872760772705e+00,-1.76213739238283151e-13},
    {-2.81144317943630998e-19,1.84628987468520298e-18,1.17642393604455719e-08,1.15634346008300781e+00,1.47807240486145020e+00,-1.80295002460479736e+00,1.04140973091125488e+00,5.50309986187172470e-29},
    {-7.75137717133641386e-19,1.57800285927230528e-17,1.20377663392901013e-09,1.96022820472717285e+00,1.97174060344696045e+00,-3.36909860372543335e-01,1.68447530269622803e+00,1.13870999166415273e-29},
    {7.21224462951696132e-20,1.63631098608729071e-17,-1.66197566819903386e-09,-6.20087146759033203e-01,-9.55895125865936279e-01,1.01977944374084473e+00,-1.12457466125488281e+00,1.47820707035726539e-22},
};
static const double MLP_B2[MLP_HIDDEN] = {
    9.60890054702758789e-01, -5.17330348491668701e-01,
    6.70606672763824463e-01, 7.22398161888122559e-02,
    -1.06727011501789093e-01, -1.01090300083160400e+00,
    4.62684258818626404e-02, 5.32092571258544922e-01,
};
static const double MLP_W3[MLP_OUTPUTS][MLP_HIDDEN] = {
    {-1.16327062249183655e-01,-2.07507178187370300e-01,1.83438926935195923e-01,-6.91891014575958252e-01,2.22693610191345215e+00,1.08898365497589111e+00,-9.33992862701416016e-01,6.12608671188354492e-01},
    {4.47977751493453979e-01,7.33569636940956116e-02,-2.04668894410133362e-01,-6.70150458812713623e-01,2.22560429573059082e+00,1.27480792999267578e+00,-1.00337898731231689e+00,6.12099245190620422e-02},
    {-1.04825112532742968e-39,-5.95720003153766233e-40,-9.52780660952979897e-40,2.27176264958796424e-39,6.26935327745065858e-40,-4.26521621377330521e-40,2.63990617694152288e-40,2.20988130940338032e-39},
    {1.20576127661293210e-40,1.89535146129488918e-39,-9.48614600618542215e-40,1.58238546227258453e-39,-2.15534697706725141e-39,-1.52216606206668984e-39,9.14015981014976752e-39,-3.56921929251245507e-40},
    {-1.81587822018917150e-39,1.61856418602452266e-39,8.14192242831255488e-40,-4.48490337921936408e-39,5.27112430340423189e-40,-6.68593128492514020e-40,1.19344946830537425e-39,-2.23617947768336416e-39},
    {-7.39777565002441406e-01,-1.72646328806877136e-01,3.48306030035018921e-01,-9.88223589956760406e-04,-6.06767944991588593e-02,1.94022071361541748e+00,2.54535019397735596e-01,7.73453593254089355e-01},
    {-8.34351956844329834e-01,4.95428778231143951e-02,9.66360449790954590e-01,-2.57174164056777954e-01,6.17581546306610107e-01,1.58351850509643555e+00,-5.50141274929046631e-01,8.03667008876800537e-01},
    {-1.29625292754363581e-39,1.24634428143824312e-39,1.36580217292500513e-39,-3.49576322701254815e-40,-1.69876610233168924e-39,1.79556780024724760e-39,1.45177043241286833e-39,-2.20128294202628324e-39},
    {1.57973700817501063e-39,-7.50332269215044925e-40,-4.44505885868475223e-40,-1.19099018950048420e-39,7.67449129956772565e-41,-8.24461938886752775e-39,1.13144621515439407e-39,-5.79725582481962771e-40},
    {1.67187238220436735e-39,-1.61799385754954246e-39,1.70872933441303869e-40,-2.01373735945644270e-39,1.64067807709003260e-39,1.17350338596417481e-39,-1.62681363008400286e-39,-1.80152051612369942e-39},
    {-9.72993135452270508e-01,-3.53800582885742188e+00,-2.09962034225463867e+00,1.81430196762084961e+00,1.47631537914276123e+00,2.85080003738403320e+00,2.34224319458007812e+00,2.69207119941711426e+00},
};
static const double MLP_B3[MLP_OUTPUTS] = {
    3.66708874702453613e-01, 3.35273742675781250e-01,
    -1.43753464131379252e-39, 1.12875011690903313e-39, 1.41431232316300165e-39,
    -3.98375630378723145e-01, -7.87216246128082275e-01,
    2.02601693789932107e-39, -2.30784328244739963e-39, 6.35465031497411020e-40,
    -8.64731252193450928e-01,
};
static const double MLP_TARGET_MEANS[MLP_OUTPUTS] = {
    3.03975892066955566e+00, 2.99099349975585938e+00, 0.0, 0.0, 0.0,
    2.36430644989013672e+00, 5.03540325164794922e+00, 0.0, 0.0, 0.0,
    1.25574219226837158e+00,
};
static const double MLP_TARGET_STDS[MLP_OUTPUTS] = {
    5.47939729690551758e+00, 5.36447429656982422e+00, 1.0, 1.0, 1.0,
    3.47016668319702148e+00, 6.96494293212890625e+00, 1.0, 1.0, 1.0,
    4.23309683799743652e-01,
};

/* ── Helpers ──────────────────────────────────────────────────────── */
static double clamp_d(double x, double lo, double hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static double midi_to_freq(int midi) {
    return 440.0 * pow(2.0, (midi - 69) / 12.0);
}
static int isfinite_d(double x) {
    return (x == x) && (x - x == 0.0);
}

/* ── Fast math (per-sample hot path) ─────────────────────────────── */

/* Fast tanh: Padé [1,2], max error ~0.001 in [-3,3] */
static inline double fast_tanh(double x) {
    if (x < -3.0) return -1.0;
    if (x >  3.0) return  1.0;
    double x2 = x * x;
    return x * (27.0 + x2) / (27.0 + 9.0 * x2);
}

/* Fast exp for small negative arguments (used in NR loop, damper).
 * Padé [2,2] approximation: accurate to ~0.1% for |x| < 4 */
static inline double fast_exp(double x) {
    if (x < -6.0) return 0.0;
    if (x > 6.0) return 403.4; /* exp(6) */
    double x2 = x * x;
    return (12.0 + 6.0*x + x2) / (12.0 - 6.0*x + x2);
}

/* Fast sin for LFO (full range, ~0.001 error) */
static inline double fast_sin(double phase) {
    /* Parabolic approximation with correction */
    double x = phase;
    if (x > M_PI) x -= TWO_PI;
    double y = (4.0/M_PI) * x - (4.0/(M_PI*M_PI)) * x * (x < 0 ? -x : x);
    return y * (0.775 + 0.225 * (y < 0 ? -y : y));
}

/* ── MLP v2 Forward Pass ─────────────────────────────────────────── */
typedef struct {
    double freq_cents[MLP_N_FREQ];
    double decay_ratio[MLP_N_DECAY];
    double ds_mult;
} mlp_corrections_t;

static mlp_corrections_t mlp_identity(void) {
    mlp_corrections_t c;
    for (int i=0;i<MLP_N_FREQ;i++) c.freq_cents[i]=0.0;
    for (int i=0;i<MLP_N_DECAY;i++) c.decay_ratio[i]=1.0;
    c.ds_mult=1.0;
    return c;
}

static mlp_corrections_t mlp_infer(int midi_note, double velocity) {
    double midi=(double)midi_note;
    double fade;
    if (midi < MLP_TRAIN_LO)
        fade = clamp_d((midi-(MLP_TRAIN_LO-MLP_FADE_SEMI))/MLP_FADE_SEMI, 0.0, 1.0);
    else if (midi > MLP_TRAIN_HI)
        fade = clamp_d(((MLP_TRAIN_HI+MLP_FADE_SEMI)-midi)/MLP_FADE_SEMI, 0.0, 1.0);
    else
        fade = 1.0;
    if (fade <= 0.0) return mlp_identity();

    double midi_norm = clamp_d((midi-MLP_MIDI_MIN)/(MLP_MIDI_MAX-MLP_MIDI_MIN), 0.0, 1.0);
    double vel_norm = clamp_d(velocity, 0.0, 1.0);
    double input[2] = {midi_norm, vel_norm};

    /* Layer 1: affine + ReLU */
    double h1[MLP_HIDDEN];
    for (int i=0;i<MLP_HIDDEN;i++) {
        double s=MLP_B1[i];
        for (int j=0;j<2;j++) s+=MLP_W1[i][j]*input[j];
        h1[i]=(s>0.0)?s:0.0;
    }
    /* Layer 2: affine + ReLU */
    double h2[MLP_HIDDEN];
    for (int i=0;i<MLP_HIDDEN;i++) {
        double s=MLP_B2[i];
        for (int j=0;j<MLP_HIDDEN;j++) s+=MLP_W2[i][j]*h1[j];
        h2[i]=(s>0.0)?s:0.0;
    }
    /* Layer 3: affine (linear) + denormalization */
    double raw[MLP_OUTPUTS];
    for (int i=0;i<MLP_OUTPUTS;i++) {
        double s=MLP_B3[i];
        for (int j=0;j<MLP_HIDDEN;j++) s+=MLP_W3[i][j]*h2[j];
        raw[i]=s*MLP_TARGET_STDS[i]+MLP_TARGET_MEANS[i];
    }

    mlp_corrections_t c;
    for (int i=0;i<MLP_N_FREQ;i++)
        c.freq_cents[i]=clamp_d(raw[i]*fade, -100.0, 100.0);
    for (int i=0;i<MLP_N_DECAY;i++) {
        double rd=clamp_d(raw[MLP_N_FREQ+i], 0.3, 3.0);
        c.decay_ratio[i]=1.0+(rd-1.0)*fade;
    }
    double rds=clamp_d(raw[MLP_DS_IDX], 0.7, 1.5);
    c.ds_mult=1.0+(rds-1.0)*fade;
    return c;
}

/* ── Per-note tables (ported from tables.rs) ─────────────────────── */
static double tip_mass_ratio(int midi) {
    static const double a[][2] = {{33,0.10},{52,0.00},{62,0.00},{74,0.02},{96,0.01}};
    double m = (double)midi;
    if (m <= a[0][0]) return a[0][1];
    if (m >= a[4][0]) return a[4][1];
    for (int i = 0; i < 4; i++) {
        if (m <= a[i+1][0]) {
            double t = (m - a[i][0]) / (a[i+1][0] - a[i][0]);
            return a[i][1] + t * (a[i+1][1] - a[i][1]);
        }
    }
    return 0.0;
}

static void eigenvalues(double mu, double out[NUM_MODES]) {
    double mu_c = clamp_d(mu, 0.0, 0.50);
    int lo = 0;
    for (int i = N_EIG_ROWS - 1; i >= 0; i--) {
        if (EIG_MU[i] <= mu_c) { lo = i; break; }
    }
    int hi = (lo + 1 < N_EIG_ROWS) ? lo + 1 : N_EIG_ROWS - 1;
    double t = (EIG_MU[hi] > EIG_MU[lo]) ?
        (mu_c - EIG_MU[lo]) / (EIG_MU[hi] - EIG_MU[lo]) : 0.0;
    for (int i = 0; i < NUM_MODES; i++)
        out[i] = EIG_BETAS[lo][i] + t * (EIG_BETAS[hi][i] - EIG_BETAS[lo][i]);
}

static void mode_ratios(double mu, double out[NUM_MODES]) {
    double betas[NUM_MODES];
    eigenvalues(mu, betas);
    double b1_sq = betas[0] * betas[0];
    for (int i = 0; i < NUM_MODES; i++)
        out[i] = (betas[i] * betas[i]) / b1_sq;
}

static double reed_length_mm(int midi) {
    double n = clamp_d((double)(midi - 32), 1.0, 64.0);
    double inches = (n <= 20.0) ? (3.0 - n / 20.0) : (2.0 - (n - 20.0) / 44.0);
    return inches * 25.4;
}

static void reed_blank_dims(int midi, double *w_mm, double *t_mm) {
    int reed = midi - 32;
    if (reed < 1) reed = 1; if (reed > 64) reed = 64;
    double w_in = (reed<=14)?0.151:(reed<=20)?0.127:(reed<=42)?0.121:(reed<=50)?0.111:0.098;
    double t_in;
    if (reed <= 16) t_in = 0.026;
    else if (reed <= 26) { t_in = 0.026 + ((reed-16.0)/10.0)*(0.034-0.026); }
    else t_in = 0.034;
    *w_mm = w_in * 25.4; *t_mm = t_in * 25.4;
}

static double reed_compliance(int midi) {
    double l = reed_length_mm(midi);
    double w, t; reed_blank_dims(midi, &w, &t);
    return (l*l*l) / (w*t*t*t);
}

static double pickup_displacement_scale(int midi) {
    double c = reed_compliance(midi), c_ref = reed_compliance(60);
    return clamp_d(0.75 * pow(c / c_ref, 0.75), 0.02, 0.82);
}

static double fundamental_decay_rate(int midi) {
    double d = 0.005 * pow(midi_to_freq(midi), 1.22);
    return (d > 3.0) ? d : 3.0;
}

static void mode_decay_rates(int midi, const double ratios[NUM_MODES], double out[NUM_MODES]) {
    double base = fundamental_decay_rate(midi);
    for (int i = 0; i < NUM_MODES; i++) out[i] = base * ratios[i] * ratios[i];
}

static void spatial_coupling(double mu, double reed_len, double out[NUM_MODES]) {
    double betas[NUM_MODES]; eigenvalues(mu, betas);
    double ell_l = clamp_d(6.0 / reed_len, 0.0, 1.0);
    double k_raw[NUM_MODES];
    for (int m = 0; m < NUM_MODES; m++) {
        double beta = betas[m];
        double tip = cosh(beta)-cos(beta)-((cosh(beta)+cos(beta))/(sinh(beta)+sin(beta)))*(sinh(beta)-sin(beta));
        if (fabs(tip) < 1e-30 || ell_l < 1e-12) { k_raw[m]=1.0; continue; }
        double xi0 = 1.0 - ell_l, h = ell_l / 32.0;
        double sig = (cosh(beta)+cos(beta))/(sinh(beta)+sin(beta));
        double sum = 0.0;
        for (int j = 0; j <= 32; j++) {
            double bx = beta*(xi0+j*h);
            double phi = cosh(bx)-cos(bx)-sig*(sinh(bx)-sin(bx));
            double w = (j==0||j==32)?1.0:((j%2==1)?4.0:2.0);
            sum += w * phi;
        }
        k_raw[m] = clamp_d(fabs((sum*h/3.0)/(ell_l*tip)), 0.0, 1.0);
    }
    double k1 = k_raw[0];
    for (int i = 0; i < NUM_MODES; i++)
        out[i] = (k1>1e-30) ? clamp_d(k_raw[i]/k1, 0.0, 1.0) : 1.0;
}

static double velocity_scurve(double vel) {
    double k=1.5, s=1.0/(1.0+exp(-k*(vel-0.5)));
    double s0=1.0/(1.0+exp(k*0.5)), s1=1.0/(1.0+exp(-k*0.5));
    return (s-s0)/(s1-s0);
}

static double velocity_exponent(int midi) {
    double m=(double)midi, t=exp(-0.5*pow((m-62.0)/15.0,2));
    return 1.3+t*0.4;
}

static double pickup_rms_proxy(double ds, double f0, double fc) {
    if (ds<1e-10) return 0.0;
    double r=(1.0-sqrt(1.0-ds*ds))/ds, inv=1.0/sqrt(1.0-ds*ds), sum=0.0, rn=r;
    for (int n=1;n<=8;n++) {
        double cn=2.0*rn*inv, nf=n*f0, h=nf/sqrt(nf*nf+fc*fc);
        sum+=(cn*h)*(cn*h); rn*=r;
    }
    return sqrt(sum);
}

static double register_trim_db(int midi) {
    static const double a[][2]={{36,-1.3},{40,0},{44,-1.3},{48,0.7},{52,0.2},
        {56,-1.0},{60,0},{64,0.9},{68,1.2},{72,0},{76,1.8},{80,2.4},{84,3.6}};
    double m=(double)midi;
    if (m<=a[0][0]) return a[0][1]; if (m>=a[12][0]) return a[12][1];
    for (int i=0;i<12;i++) {
        if (m<=a[i+1][0]) { double t=(m-a[i][0])/(a[i+1][0]-a[i][0]); return a[i][1]+t*(a[i+1][1]-a[i][1]); }
    }
    return 0.0;
}

static double output_scale(int midi, double vel) {
    double ds=pickup_displacement_scale(midi), f0=midi_to_freq(midi);
    double sv=velocity_scurve(vel), vs=pow(sv,velocity_exponent(midi)), vc4=pow(sv,velocity_exponent(60));
    double ed=fmax(ds*vs,1e-6), edr=fmax(0.75*vc4,1e-6);
    double rms=pickup_rms_proxy(ed,f0,2312.0), rr=pickup_rms_proxy(edr,midi_to_freq(60),2312.0);
    double fdb=-20.0*log10(rms/rr), vdb=-0.04*fmax((double)midi-60.0,0.0);
    /* Low-note compensation: boost below middle C to even out the register */
    double low_boost = (midi < 60) ? (60.0 - midi) * 0.15 : 0.0;
    return pow(10.0,(-35.0+fdb+vdb+low_boost+register_trim_db(midi)*pow(vel,1.3))/20.0);
}

static void dwell_attenuation(double vel, double f0, const double r[NUM_MODES], double out[NUM_MODES]) {
    double td=clamp_d((0.75+0.25*(1.0-vel))/f0,0.0003,0.020), ss=128.0;
    for (int i=0;i<NUM_MODES;i++) { double ft=f0*r[i]*td; out[i]=exp(-ft*ft/(2.0*ss)); }
    double a0=out[0]; if (a0>1e-30) for (int i=0;i<NUM_MODES;i++) out[i]/=a0;
}

static double onset_ramp_time(double vel, double f0) {
    return clamp_d((2.5+2.5*(1.0-vel))/f0, 0.002, 0.030);
}

static double hash_f64(int midi, uint32_t seed) {
    uint32_t h=2166136261u; h^=(uint32_t)midi; h*=16777619u;
    h^=seed; h*=16777619u; h^=h>>16; h*=2654435769u;
    return (double)(h&0x00FFFFFF)/16777216.0;
}

static double freq_detune(int midi) {
    return 1.0 + (hash_f64(midi,0xDEAD)*2.0-1.0)*0.00173;
}

/* ── Biquad (Audio EQ Cookbook, DF-II Transposed) ─────────────────── */
typedef struct { double b0,b1,b2,a1,a2,z1,z2; } biquad_t;

static void biquad_reset(biquad_t *f) { f->z1=f->z2=0.0; }

static void biquad_set_hpf(biquad_t *f, double fc, double q, double sr) {
    double w0=TWO_PI*fc/sr, alpha=sin(w0)/(2.0*q), cs=cos(w0), a0=1.0+alpha;
    f->b0=((1.0+cs)/2.0)/a0; f->b1=-(1.0+cs)/a0; f->b2=f->b0;
    f->a1=(-2.0*cs)/a0; f->a2=(1.0-alpha)/a0;
}

static void biquad_set_lpf(biquad_t *f, double fc, double q, double sr) {
    double w0=TWO_PI*fc/sr, alpha=sin(w0)/(2.0*q), cs=cos(w0), a0=1.0+alpha;
    f->b0=((1.0-cs)/2.0)/a0; f->b1=(1.0-cs)/a0; f->b2=f->b0;
    f->a1=(-2.0*cs)/a0; f->a2=(1.0-alpha)/a0;
}

static void biquad_set_bpf(biquad_t *f, double fc, double q, double sr) {
    double w0=TWO_PI*fc/sr, alpha=sin(w0)/(2.0*q), a0=1.0+alpha;
    f->b0=alpha/a0; f->b1=0.0; f->b2=-alpha/a0;
    f->a1=(-2.0*cos(w0))/a0; f->a2=(1.0-alpha)/a0;
}

static inline double biquad_process(biquad_t *f, double x) {
    double y=f->b0*x+f->z1;
    f->z1=f->b1*x-f->a1*y+f->z2;
    f->z2=f->b2*x-f->a2*y;
    return y;
}

/* ── 2x Polyphase IIR Half-Band Oversampler ──────────────────────── */
#define OS_N_SECTIONS 3

static const double OS_BRANCH_A[OS_N_SECTIONS] = {
    0.036681502163648, 0.248030921580110, 0.643184620136480
};
static const double OS_BRANCH_B[OS_N_SECTIONS] = {
    0.110377634768680, 0.420399304190880, 0.854640112701920
};

typedef struct { double a, state; } allpass1_t;

static inline double allpass1_process(allpass1_t *ap, double x) {
    double y = ap->a * x + ap->state;
    ap->state = x - ap->a * y;
    return y;
}

typedef struct {
    allpass1_t up_a[OS_N_SECTIONS], up_b[OS_N_SECTIONS];
    allpass1_t dn_a[OS_N_SECTIONS], dn_b[OS_N_SECTIONS];
    double dn_delay;
} oversampler_t;

static void os_init(oversampler_t *os) {
    memset(os, 0, sizeof(oversampler_t));
    for (int i=0;i<OS_N_SECTIONS;i++) {
        os->up_a[i].a = OS_BRANCH_A[i];
        os->up_b[i].a = OS_BRANCH_B[i];
        os->dn_a[i].a = OS_BRANCH_A[i];
        os->dn_b[i].a = OS_BRANCH_B[i];
    }
}

static void os_reset(oversampler_t *os) {
    for (int i=0;i<OS_N_SECTIONS;i++) {
        os->up_a[i].state=0; os->up_b[i].state=0;
        os->dn_a[i].state=0; os->dn_b[i].state=0;
    }
    os->dn_delay=0;
}

static void os_upsample(oversampler_t *os, const double *in, double *out, int n) {
    for (int i=0;i<n;i++) {
        double x=in[i], a=x, b=x;
        for (int s=0;s<OS_N_SECTIONS;s++) a=allpass1_process(&os->up_a[s],a);
        for (int s=0;s<OS_N_SECTIONS;s++) b=allpass1_process(&os->up_b[s],b);
        out[i*2]=a; out[i*2+1]=b;
    }
}

static void os_downsample(oversampler_t *os, const double *in, double *out, int n) {
    for (int i=0;i<n;i++) {
        double a=in[i*2], b=in[i*2+1];
        for (int s=0;s<OS_N_SECTIONS;s++) a=allpass1_process(&os->dn_a[s],a);
        for (int s=0;s<OS_N_SECTIONS;s++) b=allpass1_process(&os->dn_b[s],b);
        out[i]=(a+os->dn_delay)*0.5;
        os->dn_delay=b;
    }
}

/* ── Modal Reed Oscillator ───────────────────────────────────────── */
typedef struct {
    double s,c,cos_inc,sin_inc,phase_inc,amplitude,decay_mult,envelope;
    double jitter_drift,damper_rate,damper_mult;
} reed_mode_t;

typedef struct {
    reed_mode_t modes[NUM_MODES];
    uint64_t sample, onset_ramp_samples;
    double onset_ramp_inc, onset_shape_exp;
    int damper_active, damper_ramp_done;
    double damper_ramp_samples, damper_release_count;
    uint32_t jitter_state;
    double jitter_revert, jitter_diffusion;
} reed_t;

static inline double lcg_uniform_scaled(uint32_t *s) {
    *s = (*s)*1664525u+1013904223u;
    return ((double)(*s>>1)/(double)0x7FFFFFFFu*2.0-1.0)*SQRT_3;
}

static void reed_init(reed_t *r, double f0, const double ratios[NUM_MODES],
    const double amps[NUM_MODES], const double decays_db[NUM_MODES],
    double onset_time, double velocity, double sr, uint32_t seed) {
    double dt=1.0/sr;
    r->jitter_revert=exp(-dt/JITTER_TAU_S);
    r->jitter_diffusion=JITTER_SIGMA*sqrt(1.0-r->jitter_revert*r->jitter_revert);
    r->jitter_state=(seed>0)?seed:1;
    r->sample=0; r->damper_active=0; r->damper_ramp_done=0;
    r->damper_ramp_samples=0; r->damper_release_count=0;
    uint32_t js=r->jitter_state;
    for (int i=0;i<NUM_MODES;i++) {
        js=js*1664525u+1013904223u; double u1=(double)(js>>1)/(double)0x7FFFFFFFu;
        js=js*1664525u+1013904223u; double u2=(double)(js>>1)/(double)0x7FFFFFFFu;
        double freq=f0*ratios[i], pi=TWO_PI*freq/sr;
        double an=decays_db[i]/8.686;
        reed_mode_t *m=&r->modes[i];
        m->s=0; m->c=1; m->cos_inc=cos(pi); m->sin_inc=sin(pi);
        m->phase_inc=pi; m->amplitude=amps[i]; m->decay_mult=exp(-an/sr);
        m->envelope=1.0; m->damper_rate=0; m->damper_mult=1.0;
        m->jitter_drift=JITTER_SIGMA*sqrt(-2.0*log(fmax(u1,1e-30)))*cos(TWO_PI*u2);
    }
    r->jitter_state=js;
    uint64_t rs=(uint64_t)(onset_time*sr+0.5);
    r->onset_ramp_samples=rs;
    r->onset_ramp_inc=(rs>0)?(M_PI/(double)rs):0.0;
    r->onset_shape_exp=1.0+(1.0-velocity);
}

static void reed_start_damper(reed_t *r, int midi, double sr) {
    if (midi>=92) return;
    double base=fmax(55.0*pow(2.0,(midi-60.0)/24.0),0.5);
    for (int m=0;m<NUM_MODES;m++) {
        double f=fmin(base*pow(3.0,m),2000.0);
        r->modes[m].damper_rate=f/sr;
        r->modes[m].damper_mult=exp(-r->modes[m].damper_rate);
    }
    r->damper_ramp_samples=((midi<48)?0.050:(midi<72)?0.025:0.008)*sr;
    r->damper_active=1; r->damper_release_count=0; r->damper_ramp_done=0;
}

static void reed_render(reed_t *r, double *out, int frames) {
    for (int i=0;i<frames;i++) {
        double ramp=1.0;
        if (r->sample < r->onset_ramp_samples) {
            double tn=(double)r->sample*r->onset_ramp_inc;
            double t01=clamp_d((double)r->sample/(double)r->onset_ramp_samples,0.0,1.0);
            ramp=t01*t01; /* quadratic onset ramp (replaces cos+pow) */
        }
        double df=1.0;
        if (r->damper_active && !r->damper_ramp_done) {
            r->damper_release_count+=1.0;
            if (r->damper_release_count>=r->damper_ramp_samples) r->damper_ramp_done=1;
            else df=r->damper_release_count/r->damper_ramp_samples;
        }
        if ((r->sample%JITTER_SUBSAMPLE)==0)
            for (int m=0;m<NUM_MODES;m++)
                r->modes[m].jitter_drift=r->jitter_revert*r->modes[m].jitter_drift
                    +r->jitter_diffusion*lcg_uniform_scaled(&r->jitter_state);
        double sum=0.0;
        for (int m=0;m<NUM_MODES;m++) {
            reed_mode_t *md=&r->modes[m];
            double di=md->phase_inc*md->jitter_drift;
            double ci=md->cos_inc+(-md->sin_inc*di), si=md->sin_inc+(md->cos_inc*di);
            double sn=md->s*ci+md->c*si, cn=md->c*ci-md->s*si;
            md->s=sn; md->c=cn;
            md->envelope*=md->decay_mult;
            if (r->damper_active) {
                if (r->damper_ramp_done)
                    md->envelope*=md->damper_mult;
                else
                    md->envelope*=fast_exp(-md->damper_rate*df);
            }
            if (md->envelope < 1e-15) md->envelope = 0.0;
            sum+=md->s*md->amplitude*md->envelope;
        }
        if ((r->sample%RENORM_INTERVAL)==0 && r->sample>0)
            for (int m=0;m<NUM_MODES;m++) {
                reed_mode_t *md=&r->modes[m];
                double mag=sqrt(md->s*md->s+md->c*md->c);
                if (mag>1e-30) { md->s/=mag; md->c/=mag; }
            }
        out[i]+=sum*ramp;
        r->sample++;
    }
}

static int reed_is_silent(const reed_t *r) {
    double total=0.0;
    for (int m=0;m<NUM_MODES;m++) {
        double lv=r->modes[m].amplitude*r->modes[m].envelope;
        total+=lv*lv;
    }
    return (10.0*log10(total+1e-30) < -80.0);
}

/* ── Pickup (time-varying RC) ────────────────────────────────────── */
typedef struct { double q,beta,ds; } pickup_t;

static void pickup_init(pickup_t *p, double sr, double ds) {
    p->q=1.0; p->beta=(1.0/sr)/(2.0*PICKUP_TAU); p->ds=ds;
}

static void pickup_process(pickup_t *p, double *buf, int frames) {
    for (int i=0;i<frames;i++) {
        double y=clamp_d(buf[i]*p->ds,-PICKUP_MAX_Y,PICKUP_MAX_Y);
        double omy=1.0-y, alpha=p->beta*omy;
        double qn=(p->q*(1.0-alpha)+2.0*p->beta)/(1.0+alpha);
        p->q=qn; buf[i]=(qn*omy-1.0)*PICKUP_SENSITIVITY;
    }
}

/* ── Attack Noise ────────────────────────────────────────────────── */
typedef struct { double amp,decay; uint32_t rem; biquad_t bpf; uint32_t rng; } noise_t;

static void noise_init(noise_t *n, double vel, double f0, double sr, uint32_t seed) {
    n->amp=0.025*vel*vel; n->decay=exp(-1.0/(0.003*sr));
    n->rem=(uint32_t)(0.015*sr); memset(&n->bpf,0,sizeof(biquad_t));
    biquad_set_bpf(&n->bpf,clamp_d(f0*5.0,200.0,2000.0),0.7,sr); n->rng=seed;
}

static void noise_render(noise_t *n, double *buf, int frames) {
    int c=(n->rem<(uint32_t)frames)?(int)n->rem:frames;
    for (int i=0;i<c;i++) {
        n->rng=n->rng*1664525u+1013904223u;
        buf[i]+=n->amp*biquad_process(&n->bpf,(double)(int32_t)(n->rng)/(double)0x7FFFFFFF);
        n->amp*=n->decay;
    }
    n->rem-=(uint32_t)c;
}

/* ── Voice ───────────────────────────────────────────────────────── */
typedef struct {
    reed_t reed; pickup_t pickup; noise_t noise;
    double post_gain; int midi_note, active, releasing; uint64_t age;
    /* Steal crossfade */
    reed_t steal_reed; pickup_t steal_pickup; noise_t steal_noise;
    double steal_gain; int steal_active, steal_midi;
    int steal_fade, steal_fade_len;
} voice_t;

static void voice_note_on(voice_t *v, int note, double vel, double sr, uint32_t seed,
    double brightness, double bark_mod, double attack_mod, double decay_mod, double tune_cents) {

    /* If voice is active, move current state to steal crossfade */
    if (v->active) {
        v->steal_reed = v->reed;
        v->steal_pickup = v->pickup;
        v->steal_noise = v->noise;
        v->steal_gain = v->post_gain;
        v->steal_midi = v->midi_note;
        v->steal_active = 1;
        v->steal_fade = STEAL_FADE_SAMPLES;
        v->steal_fade_len = STEAL_FADE_SAMPLES;
    }

    /* MLP corrections */
    mlp_corrections_t mlp = mlp_infer(note, vel);

    double mu=tip_mass_ratio(note), ratios[NUM_MODES];
    mode_ratios(mu, ratios);

    /* Apply MLP frequency corrections to modes 1-5 */
    for (int i=1; i<NUM_MODES && i<=MLP_N_FREQ; i++)
        ratios[i] *= pow(2.0, mlp.freq_cents[i-1]/1200.0);

    double f0=midi_to_freq(note)*freq_detune(note)*pow(2.0,tune_cents/1200.0);
    double dwell[NUM_MODES]; dwell_attenuation(vel,f0,ratios,dwell);
    double amps[NUM_MODES]; double coupling[NUM_MODES];
    spatial_coupling(mu, reed_length_mm(note), coupling);
    for (int i=0;i<NUM_MODES;i++) {
        double var=1.0+(hash_f64(note,0xBEEF+(uint32_t)i)*2.0-1.0)*0.08;
        amps[i]=BASE_MODE_AMPS[i]*dwell[i]*var*coupling[i];
    }
    double vs=pow(velocity_scurve(vel),velocity_exponent(note));
    for (int i=0;i<NUM_MODES;i++) amps[i]*=vs;
    /* Brightness: 0=very dark (0.05x), 0.5=neutral (1x), 1=bright (3x) */
    double bs = (brightness < 0.5)
        ? 0.05 + brightness * 1.9   /* 0.05 → 1.0 */
        : 1.0 + (brightness - 0.5) * 4.0; /* 1.0 → 3.0 */
    for (int i=1;i<NUM_MODES;i++) amps[i]*=bs;

    double decays[NUM_MODES]; mode_decay_rates(note,ratios,decays);
    /* Apply MLP decay corrections to modes 1-5 */
    for (int i=1; i<NUM_MODES && i<=MLP_N_DECAY; i++)
        decays[i] /= mlp.decay_ratio[i-1];

    /* Inverted: 0=short/percussive (5.7x decay), 1=long sustain (0.3x decay) */
    double dm = 5.7 - decay_mod * 5.4;
    dm = clamp_d(dm, 0.3, 5.7);
    for (int i=0;i<NUM_MODES;i++) decays[i]*=dm;

    /* Inverted: 0=slow ring-up (4x onset), 1=fast/punchy (0.2x onset) */
    double base_onset=onset_ramp_time(vel,f0);
    double onset=clamp_d(base_onset*(4.0-attack_mod*3.8),0.001,0.100);
    reed_init(&v->reed,f0,ratios,amps,decays,onset,vel,sr,seed);

    /* Pickup with bark control + MLP ds correction */
    double ds=pickup_displacement_scale(note)*mlp.ds_mult*(0.3+bark_mod*1.4);
    pickup_init(&v->pickup,sr,clamp_d(ds,0.02,0.95));
    noise_init(&v->noise,vel,f0,sr,seed);
    v->post_gain=output_scale(note,vel);
    v->midi_note=note; v->active=1; v->releasing=0;
}

static void voice_note_off(voice_t *v) {
    reed_start_damper(&v->reed,v->midi_note,SAMPLE_RATE_D);
    v->releasing=1;
}

static void voice_render(voice_t *v, double *buf, int frames) {
    memset(buf,0,frames*sizeof(double));
    reed_render(&v->reed,buf,frames);
    if (v->noise.rem>0) noise_render(&v->noise,buf,frames);
    pickup_process(&v->pickup,buf,frames);
    for (int i=0;i<frames;i++) buf[i]*=v->post_gain;

    /* Render stealing voice with linear crossfade */
    if (v->steal_active) {
        double steal_buf[BLOCK_SIZE];
        int len = (frames < BLOCK_SIZE) ? frames : BLOCK_SIZE;
        memset(steal_buf,0,len*sizeof(double));
        reed_render(&v->steal_reed,steal_buf,len);
        if (v->steal_noise.rem>0) noise_render(&v->steal_noise,steal_buf,len);
        pickup_process(&v->steal_pickup,steal_buf,len);
        double fl = (double)v->steal_fade_len;
        for (int i=0;i<len;i++) {
            int rem = v->steal_fade - i;
            if (rem <= 0) break;
            double gain = (double)rem / fl;
            buf[i] += steal_buf[i] * v->steal_gain * gain;
        }
        v->steal_fade -= len;
        if (v->steal_fade <= 0) v->steal_active = 0;
    }
}

/* ── Preamp (simplified two-stage CE, runs at 2x rate) ───────────── */
typedef struct { biquad_t hpf,lpf; double gain,base_gain; } preamp_t;

static void preamp_init(preamp_t *p, double sr) {
    memset(p,0,sizeof(preamp_t));
    biquad_set_hpf(&p->hpf,PREAMP_CIN_FC,0.707,sr);
    biquad_set_lpf(&p->lpf,PREAMP_BW_FC,0.707,sr);
    p->base_gain=PREAMP_GAIN; p->gain=PREAMP_GAIN;
}

static void preamp_set_ldr(preamp_t *p, double r_path) {
    double re1=33000.0, re_eff=(re1*r_path)/(re1+r_path);
    double re_nom=(re1*1000000.0)/(re1+1000000.0);
    p->gain=p->base_gain*clamp_d(re_eff/re_nom,0.1,2.0);
}

static void preamp_reset(preamp_t *p) {
    biquad_reset(&p->hpf); biquad_reset(&p->lpf);
}

static inline double preamp_process(preamp_t *p, double in) {
    double x=biquad_process(&p->hpf,in)*p->gain;
    x=(x>0)?fast_tanh(x/PREAMP_SAT_POS)*PREAMP_SAT_POS:fast_tanh(x/PREAMP_SAT_NEG)*PREAMP_SAT_NEG;
    return biquad_process(&p->lpf,x);
}

/* ── Tremolo (sine LFO + CdS LDR) ───────────────────────────────── */
typedef struct {
    double phase,phase_inc,depth,ldr_env,ldr_atk,ldr_rel;
    double ln_r_max, ln_min_minus_max, r_series;
} tremolo_t;

static void tremolo_init(tremolo_t *t, double depth, double sr) {
    t->phase=0; t->phase_inc=TWO_PI*TREM_RATE_HZ/sr; t->depth=depth;
    t->ldr_env=0; t->ldr_atk=exp(-1.0/(LDR_ATTACK_TAU*sr));
    t->ldr_rel=exp(-1.0/(LDR_RELEASE_TAU*sr));
    t->ln_r_max=log(LDR_R_MAX); t->ln_min_minus_max=log(LDR_R_MIN)-log(LDR_R_MAX);
    t->r_series=18000.0+50000.0*(1.0-depth);
}

static void tremolo_set_depth(tremolo_t *t, double d) {
    t->depth=clamp_d(d,0.0,1.0);
    t->r_series=18000.0+50000.0*(1.0-t->depth);
}

static double tremolo_process(tremolo_t *t) {
    double lfo=fast_sin(t->phase); t->phase+=t->phase_inc;
    if (t->phase>=TWO_PI) t->phase-=TWO_PI;
    double led=(lfo>0?lfo:0)*t->depth;
    double coeff=(led>t->ldr_env)?t->ldr_atk:t->ldr_rel;
    t->ldr_env=led+coeff*(t->ldr_env-led);
    double drv=clamp_d(t->ldr_env,0.0,1.0), r_ldr;
    if (drv<1e-6) r_ldr=LDR_R_MAX;
    else { /* pow(drv,1.1) ≈ drv * (1 - 0.1*(1-drv)) for drv in [0,1] */
        double drv11 = drv * (0.9 + 0.1*drv);
        r_ldr=exp(t->ln_r_max+t->ln_min_minus_max*drv11); }
    return t->r_series+r_ldr;
}

/* ── Power Amp (behavioral NR, Class AB) ─────────────────────────── */
typedef struct { double cl_gain; } power_amp_t;

static void pa_init(power_amp_t *pa) {
    pa->cl_gain=PA_OPEN_LOOP_GAIN/(1.0+PA_OPEN_LOOP_GAIN*PA_FEEDBACK_BETA);
}

static inline double pa_process(power_amp_t *pa, double in) {
    double y=clamp_d(in*pa->cl_gain,-PA_HEADROOM+PA_NR_TOL,PA_HEADROOM-PA_NR_TOL);
    /* NR loop: 4 iterations max (was 8), fast approximations */
    for (int it=0;it<4;it++) {
        double err=in-PA_FEEDBACK_BETA*y, v=PA_OPEN_LOOP_GAIN*err;
        double vs=v*v, vts=PA_CROSSOVER_VT*PA_CROSSOVER_VT, et=fast_exp(-vs/vts);
        double cg=PA_QUIESCENT_GAIN+(1.0-PA_QUIESCENT_GAIN)*(1.0-et);
        double vc=v*cg, dcg=cg+v*(1.0-PA_QUIESCENT_GAIN)*(2.0*v/vts)*et;
        double ta=fast_tanh(vc/PA_HEADROOM), fv=PA_HEADROOM*ta, fd=(1.0-ta*ta)*dcg;
        double res=y-fv, jac=1.0+PA_OPEN_LOOP_GAIN*PA_FEEDBACK_BETA*fd;
        double delta=res/jac; y-=delta;
        if (fabs(delta)<PA_NR_TOL) break;
    }
    return clamp_d(y/PA_HEADROOM, -1.0, 1.0);
}

/* ── Speaker (polynomial + HPF/LPF + thermal) ───────────────────── */
typedef struct {
    biquad_t hpf,lpf; double character,a2,a3,th_coeff,th_alpha,th_state;
} speaker_t;

static void spk_init(speaker_t *s, double sr) {
    memset(s,0,sizeof(speaker_t)); s->character=1.0;
    biquad_set_hpf(&s->hpf,SPK_HPF_AUTH_HZ,SPK_HPF_Q,sr);
    biquad_set_lpf(&s->lpf,SPK_LPF_AUTH_HZ,SPK_LPF_Q,sr);
    s->a2=0.2; s->a3=0.6; s->th_coeff=2.0;
    s->th_alpha=1.0/(SPK_THERMAL_TAU*sr); s->th_state=0;
}

static void spk_set_char(speaker_t *s, double ch, double sr) {
    double c=clamp_d(ch,0,1); if (fabs(c-s->character)<0.002) return;
    s->character=c;
    biquad_set_hpf(&s->hpf,SPK_HPF_BYPASS_HZ*pow(SPK_HPF_AUTH_HZ/SPK_HPF_BYPASS_HZ,c),SPK_HPF_Q,sr);
    biquad_set_lpf(&s->lpf,SPK_LPF_BYPASS_HZ*pow(SPK_LPF_AUTH_HZ/SPK_LPF_BYPASS_HZ,c),SPK_LPF_Q,sr);
    s->a2=0.2*c; s->a3=0.6*c; s->th_coeff=2.0*c;
}

static void spk_reset(speaker_t *s) {
    biquad_reset(&s->hpf); biquad_reset(&s->lpf); s->th_state=0;
}

static inline double spk_process(speaker_t *s, double in) {
    double x2=in*in, x3=x2*in;
    double sh=(in+s->a2*x2+s->a3*x3)/(1.0+s->a2+s->a3);
    double lim=(s->character<0.001)?sh:fast_tanh(sh);
    s->th_state+=(x2-s->th_state)*s->th_alpha;
    double tg=1.0/(1.0+s->th_coeff*sqrt(s->th_state));
    return biquad_process(&s->lpf,biquad_process(&s->hpf,lim*tg));
}

/* ── Spring Reverb (dispersive allpass model) ────────────────────── */
/* Based on Parker (2011) "Efficient Dispersion Generation Structures  *
 * for Spring Reverb Emulation". Models the chirp dispersion of a      *
 * helical spring using cascaded allpass filters + feedback delay.      */

#define SPRING_N_AP     6       /* dispersive allpass sections */
#define SPRING_DLY_LEN  1764    /* ~40ms round-trip at 44.1kHz */
#define SPRING_PREDL    331     /* ~7.5ms pre-delay (spring travel) */

typedef struct {
    /* Dispersive allpass chain — creates the chirp */
    double ap_state[SPRING_N_AP];
    double ap_coeff[SPRING_N_AP];
    /* Feedback delay line */
    double dly[SPRING_DLY_LEN];
    int dly_pos;
    /* Pre-delay */
    double pdly[SPRING_PREDL];
    int pdly_pos;
    /* Damping LPF (one-pole) in feedback */
    double lpf_state;
    double lpf_coeff;
    /* Macro-derived parameters (set per-sample from knob) */
    double feedback;
    double wet_mix;
} spring_reverb_t;

static void spring_init(spring_reverb_t *s) {
    memset(s, 0, sizeof(spring_reverb_t));
    /* Allpass coefficients: increasing values create frequency-dependent
     * group delay → chirp. Tuned for classic Fender-style spring. */
    s->ap_coeff[0] = 0.30;
    s->ap_coeff[1] = 0.42;
    s->ap_coeff[2] = 0.55;
    s->ap_coeff[3] = 0.62;
    s->ap_coeff[4] = 0.72;
    s->ap_coeff[5] = 0.81;
    s->lpf_coeff = exp(-TWO_PI * 1200.0 / SAMPLE_RATE_D);
    s->lpf_state = 0.0;
    s->feedback = 0.0;
    s->wet_mix = 0.0;
    s->dly_pos = 0;
    s->pdly_pos = 0;
}

/* Macro control: one knob (0-1) drives feedback, damping, and mix together.
 *
 * 0.00       — fully dry, no reverb
 * 0.00-0.15  — subtle room ambience (short decay, dark, low mix)
 * 0.15-0.50  — classic spring (medium decay, warm, moderate mix)
 * 0.50-0.80  — lush spring (long decay, brighter, prominent mix)
 * 0.80-1.00  — drenched (very long decay, bright, equal wet/dry)
 *
 * Feedback:  0.40 → 0.82  (decay time ~0.5s → ~4s)
 * Damping:   2.0kHz → 7kHz (dark → bright as knob increases)
 * Wet mix:   0.0 → 0.85   (dry → nearly equal)
 */
static void spring_set_macro(spring_reverb_t *s, double knob) {
    double k = clamp_d(knob, 0.0, 1.0);
    /* Feedback: quadratic curve, more dramatic increase at high values */
    s->feedback = 0.40 + 0.42 * k * (0.5 + 0.5 * k);
    /* Damping LPF: 1.2kHz at 0, opens to 4kHz at 1 (warm spring tone) */
    double fc = 1200.0 + 2800.0 * k;
    s->lpf_coeff = exp(-TWO_PI * fc / SAMPLE_RATE_D);
    /* Wet mix: gentle ramp with slight exponential curve */
    s->wet_mix = 0.85 * k * (0.6 + 0.4 * k);
}

static inline double spring_process(spring_reverb_t *s, double in) {
    /* Pre-delay: spring travel time */
    double pd_out = s->pdly[s->pdly_pos];
    s->pdly[s->pdly_pos] = in;
    s->pdly_pos++; if (s->pdly_pos >= SPRING_PREDL) s->pdly_pos = 0;

    /* Read from feedback delay */
    double dly_out = s->dly[s->dly_pos];

    /* Input to allpass chain: pre-delayed input + feedback */
    double x = pd_out + dly_out * s->feedback;

    /* Dispersive allpass chain — creates the chirpy spring character */
    for (int i = 0; i < SPRING_N_AP; i++) {
        double a = s->ap_coeff[i];
        double y = a * x + s->ap_state[i];
        s->ap_state[i] = x - a * y;
        x = y;
    }

    /* Damping LPF in feedback path */
    s->lpf_state = x + s->lpf_coeff * (s->lpf_state - x);

    /* Write to delay line */
    s->dly[s->dly_pos] = s->lpf_state;
    s->dly_pos++; if (s->dly_pos >= SPRING_DLY_LEN) s->dly_pos = 0;

    return x;
}

/* ── Darken (smooth one-pole LPF on output) ──────────────────────── */
typedef struct { double state, coeff; } darken_t;

static void darken_init(darken_t *d) { d->state=0; d->coeff=1.0; }

static void darken_set(darken_t *d, double amount, double sr) {
    /* 0=bypass (20kHz), 1=very dark (800Hz) */
    double fc = (amount < 0.001) ? 20000.0 : 20000.0 * pow(800.0/20000.0, amount);
    d->coeff = exp(-TWO_PI * fc / sr);
}

static inline double darken_process(darken_t *d, double in) {
    d->state = in + d->coeff * (d->state - in);
    return d->state;
}

/* ── Preset data ─────────────────────────────────────────────────── */
#define NUM_PRESETS 10
typedef struct {
    float vol,trem,atk,dcy,brt,dark,bark,rev,spk,tune;
} preset_data_t;
/*              vol  trem  atk   dcy   brt   dark  bark  rev   spk   tune */
static const preset_data_t PRESETS[NUM_PRESETS] = {
    {0.50,0.50,0.80,0.60,0.50,0.00,0.50,0.00,0.70,0.50}, /* 0 Classic 200A */
    {0.55,0.70,0.75,0.75,0.40,0.30,0.35,0.65,0.80,0.50}, /* 1 Dreamy Keys */
    {0.55,0.40,0.85,0.55,0.65,0.00,0.85,0.15,0.90,0.50}, /* 2 Barky Soul */
    {0.50,0.80,0.80,0.60,0.55,0.10,0.50,0.80,0.85,0.50}, /* 3 Surf Spring */
    {0.60,0.25,0.70,0.80,0.20,0.55,0.30,0.40,0.60,0.50}, /* 4 Dark Ballad */
    {0.50,0.00,1.00,0.15,0.80,0.00,0.70,0.00,0.30,0.50}, /* 5 Percussive Clav */
    {0.55,0.60,0.30,0.95,0.25,0.45,0.20,0.75,0.50,0.50}, /* 6 Warm Pad */
    {0.45,0.35,0.75,0.50,0.15,0.70,0.60,0.30,1.00,0.48}, /* 7 Lo-Fi Tape */
    {0.50,0.15,0.90,0.50,0.90,0.00,0.45,0.50,0.00,0.50}, /* 8 Bright Bell */
    {0.60,0.55,0.85,0.55,0.70,0.15,0.95,0.25,0.95,0.50}, /* 9 Gospel Growl */
};

static void apply_preset(void *instance, int idx);  /* forward decl */

/* ── Instance ────────────────────────────────────────────────────── */
typedef struct {
    voice_t voices[NUM_VOICES]; uint64_t age_counter;
    preamp_t preamp; tremolo_t tremolo; power_amp_t pa; speaker_t speaker;
    oversampler_t oversampler; spring_reverb_t spring; darken_t darken;
    double vbuf[BLOCK_SIZE], sbuf[BLOCK_SIZE];
    double up_buf[BLOCK_SIZE*2], os_out[BLOCK_SIZE];
    /* Knob params: vol, trem, atk, dcy, brt, dark, bark, reverb */
    float p_vol,p_trem,p_atk,p_dcy,p_brt,p_dark,p_bark,p_rev;
    /* Menu-only params */
    float p_spk,p_tune;
    int p_preset;
    /* Per-sample smoothed values (10ms tau) */
    double s_vol,s_spk,s_rev,s_dark;
    float pitch_bend;
} wurl_t;

/* 10ms smoothing coefficient (per-sample) */
#define SMOOTH_ALPHA (1.0 - exp(-1.0 / (SAMPLE_RATE_D * 0.010)))

typedef struct { const char *key; const char *label; float min,max,step; } knob_def_t;
static const knob_def_t KNOBS[8]={
    {"volume","Volume",0,1,0.01f},{"tremolo","Tremolo",0,1,0.01f},
    {"attack","Attack",0,1,0.01f},{"decay","Decay",0,1,0.01f},
    {"brightness","Bright",0,1,0.01f},{"darken","Darken",0,1,0.01f},
    {"bark","Bark",0,1,0.01f},{"reverb","Reverb",0,1,0.01f}
};

/* ── Lifecycle ───────────────────────────────────────────────────── */
static void *create_instance(const char *md, const char *jd) {
    (void)md;(void)jd;
    wurl_t *w=(wurl_t*)calloc(1,sizeof(wurl_t)); if(!w) return NULL;
    w->p_vol=0.5f; w->p_trem=0.5f; w->p_atk=0.5f;
    w->p_dcy=0.5f; w->p_brt=0.5f; w->p_dark=0.0f; w->p_bark=0.5f; w->p_rev=0.0f;
    w->p_spk=0.5f; w->p_tune=0.5f; w->p_preset=0;
    w->s_vol=(double)w->p_vol; w->s_spk=(double)w->p_spk;
    w->s_rev=0.0; w->s_dark=0.0;
    /* All nonlinear stages run at 2x rate to avoid aliasing */
    preamp_init(&w->preamp,OS_SAMPLE_RATE);
    tremolo_init(&w->tremolo,0.5,OS_SAMPLE_RATE);
    pa_init(&w->pa); spk_init(&w->speaker,OS_SAMPLE_RATE);
    os_init(&w->oversampler); spring_init(&w->spring);
    darken_init(&w->darken);
    return w;
}
static void destroy_instance(void *inst) { free(inst); }

static void apply_preset(void *instance, int idx) {
    wurl_t *w=(wurl_t*)instance;
    if (idx<0||idx>=NUM_PRESETS) return;
    const preset_data_t *p=&PRESETS[idx];
    w->p_vol=p->vol; w->p_trem=p->trem; w->p_atk=p->atk; w->p_dcy=p->dcy;
    w->p_brt=p->brt; w->p_dark=p->dark; w->p_bark=p->bark; w->p_rev=p->rev;
    w->p_spk=p->spk; w->p_tune=p->tune; w->p_preset=idx;
}

/* ── MIDI ────────────────────────────────────────────────────────── */
static void on_midi(void *instance, const uint8_t *msg, int len, int src) {
    (void)src; wurl_t *w=(wurl_t*)instance; if(len<3) return;
    uint8_t st=msg[0]&0xF0, d1=msg[1], d2=msg[2];
    if (st==0x90 && d2>0) {
        int slot=-1; uint64_t oldest=UINT64_MAX;
        for (int i=0;i<NUM_VOICES;i++) if(!w->voices[i].active){slot=i;break;}
        if (slot<0) { uint64_t or2=UINT64_MAX;
            for(int i=0;i<NUM_VOICES;i++) if(w->voices[i].releasing&&w->voices[i].age<or2){or2=w->voices[i].age;slot=i;}
        }
        if (slot<0) for(int i=0;i<NUM_VOICES;i++) if(w->voices[i].age<oldest){oldest=w->voices[i].age;slot=i;}
        if (slot<0) slot=0;
        double vel=d2/127.0, tune=((double)w->p_tune-0.5)*200.0+(double)w->pitch_bend*200.0;
        uint32_t seed=((uint32_t)d1*2654435761u)^(uint32_t)w->age_counter;
        voice_note_on(&w->voices[slot],d1,vel,SAMPLE_RATE_D,seed,
            (double)w->p_brt,(double)w->p_bark,(double)w->p_atk,(double)w->p_dcy,tune);
        w->voices[slot].age=w->age_counter++;
    } else if (st==0x80||(st==0x90&&d2==0)) {
        for(int i=0;i<NUM_VOICES;i++)
            if(w->voices[i].active&&!w->voices[i].releasing&&w->voices[i].midi_note==d1)
                {voice_note_off(&w->voices[i]);break;}
    } else if (st==0xE0) {
        w->pitch_bend=(float)((d2<<7|d1)-8192)/8192.0f;
    }
}

/* ── Params ──────────────────────────────────────────────────────── */
static float *param_ptr(wurl_t *w, int i) {
    float *p[]={&w->p_vol,&w->p_trem,&w->p_atk,&w->p_dcy,&w->p_brt,&w->p_dark,&w->p_bark,&w->p_rev};
    return (i>=0&&i<8)?p[i]:NULL;
}

static void set_param(void *instance, const char *key, const char *val) {
    wurl_t *w=(wurl_t*)instance; if(!w||!key||!val) return;
    if(strncmp(key,"knob_",5)==0&&strstr(key,"_adjust")) {
        int idx=atoi(key+5)-1; float *p=param_ptr(w,idx);
        if(p){int d=atoi(val);*p=clampf(*p+d*KNOBS[idx].step,KNOBS[idx].min,KNOBS[idx].max);}
        return;
    }
    float f=(float)atof(val);
    if(!strcmp(key,"volume"))w->p_vol=clampf(f,0,1);
    else if(!strcmp(key,"tremolo"))w->p_trem=clampf(f,0,1);
    else if(!strcmp(key,"attack"))w->p_atk=clampf(f,0,1);
    else if(!strcmp(key,"decay"))w->p_dcy=clampf(f,0,1);
    else if(!strcmp(key,"brightness"))w->p_brt=clampf(f,0,1);
    else if(!strcmp(key,"darken"))w->p_dark=clampf(f,0,1);
    else if(!strcmp(key,"bark"))w->p_bark=clampf(f,0,1);
    else if(!strcmp(key,"reverb"))w->p_rev=clampf(f,0,1);
    else if(!strcmp(key,"speaker"))w->p_spk=clampf(f,0,1);
    else if(!strcmp(key,"tune"))w->p_tune=clampf(f,0,1);
    else if(!strcmp(key,"preset")){int p=atoi(val);if(p>=0&&p<NUM_PRESETS)apply_preset(instance,p);}
    else if(!strcmp(key,"state"))
        sscanf(val,"%f %f %f %f %f %f %f %f %f %f %d",
            &w->p_vol,&w->p_trem,&w->p_atk,&w->p_dcy,&w->p_brt,&w->p_dark,
            &w->p_bark,&w->p_rev,&w->p_spk,&w->p_tune,&w->p_preset);
}

static int get_param(void *instance, const char *key, char *buf, int bl) {
    wurl_t *w=(wurl_t*)instance;
    if(!strcmp(key,"chain_params")) return snprintf(buf,bl,
        "[{\"key\":\"volume\",\"name\":\"Volume\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"tremolo\",\"name\":\"Tremolo\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"attack\",\"name\":\"Attack\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"decay\",\"name\":\"Decay\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"brightness\",\"name\":\"Bright\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"darken\",\"name\":\"Darken\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"bark\",\"name\":\"Bark\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"reverb\",\"name\":\"Reverb\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"speaker\",\"name\":\"Speaker\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"tune\",\"name\":\"Tune\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"preset\",\"name\":\"Preset\",\"type\":\"int\",\"min\":0,\"max\":9,\"step\":1}]");
    if(!strcmp(key,"ui_hierarchy")) {
        static const char ui[]="{\"modes\":null,\"levels\":{\"root\":{\"name\":\"Wurl\","
            "\"knobs\":[\"volume\",\"tremolo\",\"attack\",\"decay\",\"brightness\",\"darken\",\"bark\",\"reverb\"],"
            "\"params\":[\"volume\",\"tremolo\",\"attack\",\"decay\",\"brightness\",\"darken\",\"bark\",\"reverb\",\"speaker\",\"tune\",\"preset\"]}}}";
        int l=(int)strlen(ui); if(l<bl) memcpy(buf,ui,l+1); return l;
    }
    if(strncmp(key,"knob_",5)==0&&strstr(key,"_name")){int i=atoi(key+5)-1;if(i>=0&&i<8)return snprintf(buf,bl,"%s",KNOBS[i].label);return -1;}
    if(strncmp(key,"knob_",5)==0&&strstr(key,"_value")){int i=atoi(key+5)-1;float*p=param_ptr(w,i);if(p)return snprintf(buf,bl,"%d%%",(int)(*p*100));return -1;}
    if(!strcmp(key,"name")) return snprintf(buf,bl,"Wurl");
    if(!strcmp(key,"volume"))return snprintf(buf,bl,"%.4f",w->p_vol);
    if(!strcmp(key,"tremolo"))return snprintf(buf,bl,"%.4f",w->p_trem);
    if(!strcmp(key,"attack"))return snprintf(buf,bl,"%.4f",w->p_atk);
    if(!strcmp(key,"decay"))return snprintf(buf,bl,"%.4f",w->p_dcy);
    if(!strcmp(key,"brightness"))return snprintf(buf,bl,"%.4f",w->p_brt);
    if(!strcmp(key,"darken"))return snprintf(buf,bl,"%.4f",w->p_dark);
    if(!strcmp(key,"bark"))return snprintf(buf,bl,"%.4f",w->p_bark);
    if(!strcmp(key,"reverb"))return snprintf(buf,bl,"%.4f",w->p_rev);
    if(!strcmp(key,"speaker"))return snprintf(buf,bl,"%.4f",w->p_spk);
    if(!strcmp(key,"tune"))return snprintf(buf,bl,"%.4f",w->p_tune);
    if(!strcmp(key,"preset"))return snprintf(buf,bl,"%d",w->p_preset);
    if(!strcmp(key,"state"))return snprintf(buf,bl,"%f %f %f %f %f %f %f %f %f %f %d",
        w->p_vol,w->p_trem,w->p_atk,w->p_dcy,w->p_brt,w->p_dark,
        w->p_bark,w->p_rev,w->p_spk,w->p_tune,w->p_preset);
    return -1;
}

/* ── Render ──────────────────────────────────────────────────────── */
static void render_block(void *instance, int16_t *out_lr, int frames) {
    wurl_t *w=(wurl_t*)instance;
    if (frames > BLOCK_SIZE) frames = BLOCK_SIZE;

    /* Block-rate updates for non-smoothed params */
    tremolo_set_depth(&w->tremolo,(double)w->p_trem);

    /* Sum all active voices */
    memset(w->sbuf,0,frames*sizeof(double));
    for (int v=0;v<NUM_VOICES;v++) {
        if(!w->voices[v].active) continue;
        voice_render(&w->voices[v],w->vbuf,frames);
        for(int i=0;i<frames;i++) w->sbuf[i]+=w->vbuf[i];
        if(w->voices[v].releasing&&reed_is_silent(&w->voices[v].reed))
            w->voices[v].active=0;
    }

    /* NaN guard on voice sum */
    for (int i=0;i<frames;i++) {
        if (!isfinite_d(w->sbuf[i])) {
            memset(w->sbuf,0,frames*sizeof(double));
            break;
        }
    }

    /* 2x oversample: all nonlinear stages run at 88.2kHz to avoid aliasing.
     * Chain: upsample -> preamp+tremolo -> volume -> power amp -> speaker -> downsample
     * Then at base rate: darken -> spring reverb -> output */
    os_upsample(&w->oversampler, w->sbuf, w->up_buf, frames);

    /* Smoothing targets */
    double a = SMOOTH_ALPHA;
    double t_vol=(double)w->p_vol, t_spk=(double)w->p_spk;
    double t_rev=(double)w->p_rev, t_dark=(double)w->p_dark;

    for (int i=0;i<frames;i++) {
        /* 10ms smoothing (once per base-rate sample, applied to both OS samples) */
        w->s_vol += a * (t_vol - w->s_vol);
        w->s_spk += a * (t_spk - w->s_spk);
        w->s_rev += a * (t_rev - w->s_rev);
        w->s_dark += a * (t_dark - w->s_dark);

        spk_set_char(&w->speaker, w->s_spk, OS_SAMPLE_RATE);
        double vol = w->s_vol * w->s_vol; /* audio taper */

        /* Process both oversampled samples through entire analog chain */
        for (int j=0;j<2;j++) {
            int idx = i*2+j;
            double r_ldr = tremolo_process(&w->tremolo);
            preamp_set_ldr(&w->preamp, r_ldr);
            double x = preamp_process(&w->preamp, w->up_buf[idx]);
            x *= vol;
            x = pa_process(&w->pa, x);
            x = spk_process(&w->speaker, x);
            x *= POST_SPEAKER_GAIN;
            x = fast_tanh(x) * 0.90;
            w->up_buf[idx] = x;
        }
    }

    os_downsample(&w->oversampler, w->up_buf, w->os_out, frames);

    /* Post-downsample processing at base rate (linear or mild nonlinearity) */
    for (int i=0;i<frames;i++) {
        double x = w->os_out[i];

        /* Darken: smooth one-pole LPF */
        if (w->s_dark > 0.001) {
            darken_set(&w->darken, w->s_dark, SAMPLE_RATE_D);
            x = darken_process(&w->darken, x);
        }

        /* Spring reverb */
        spring_set_macro(&w->spring, w->s_rev);
        if (w->s_rev > 0.001) {
            double wet = spring_process(&w->spring, x);
            x = x + wet * w->spring.wet_mix;
            x = clamp_d(x, -1.0, 1.0); /* hard clip instead of tanh to avoid more aliasing */
        }

        /* NaN guard */
        if (!isfinite_d(x)) {
            x = 0.0;
            preamp_reset(&w->preamp);
            os_reset(&w->oversampler);
            spk_reset(&w->speaker);
        }
        int16_t s = (int16_t)(clamp_d(x, -1.0, 1.0) * 32767.0);
        out_lr[i*2] = s; out_lr[i*2+1] = s;
    }
}

/* ── API v2 ──────────────────────────────────────────────────────── */
typedef struct {
    uint32_t api_version;
    void*(*create_instance)(const char*,const char*);
    void(*destroy_instance)(void*);
    void(*on_midi)(void*,const uint8_t*,int,int);
    void(*set_param)(void*,const char*,const char*);
    int(*get_param)(void*,const char*,char*,int);
    int(*get_error)(void*,char*,int);
    void(*render_block)(void*,int16_t*,int);
} plugin_api_v2_t;

__attribute__((visibility("default")))
plugin_api_v2_t* move_plugin_init_v2(const void *host) {
    (void)host;
    static plugin_api_v2_t api={
        .api_version=2,.create_instance=create_instance,.destroy_instance=destroy_instance,
        .on_midi=on_midi,.set_param=set_param,.get_param=get_param,
        .get_error=NULL,.render_block=render_block,
    };
    return &api;
}
