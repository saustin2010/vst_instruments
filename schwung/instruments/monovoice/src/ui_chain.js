/* Mono Voice — sound, arp, and performance editor for a normal Move synth slot. */
import {
    MoveKnob1, MoveShift, MoveMainKnob, MoveLeft, MoveRight, MoveRec
} from '/data/UserData/schwung/shared/constants.mjs';
import { decodeDelta } from '/data/UserData/schwung/shared/input_filter.mjs';
import { drawMenuHeader as drawHeader, drawMenuFooter as drawFooter }
    from '/data/UserData/schwung/shared/menu_layout.mjs';
import { announce, announceParameter, announceView }
    from '/data/UserData/schwung/shared/screen_reader.mjs';

const MACHINES = ['SW SAW', 'SW PULS', 'SW ENS', 'SID6581', 'DIGIPRO', 'FM+STAT'];
const PAGES = ['SYNTH', 'AMP', 'FILTER', 'EFFECT', 'LFO 1', 'LFO 2', 'LFO 3',
    'ARP', 'ARP STEP'];
const SOUND_PAGES = 7, ARP_PAGE = 7, ARP_STEP_PAGE = 8;
const LFO_DESTS = [
    'OFF','PITCH',
    'SYN 1','SYN 2','SYN 3','SYN 4','SYN 5','SYN 6','SYN 7','TUNE',
    'AMP ATK','AMP HLD','AMP DEC','AMP REL','DIST','VOLUME','PAN','PORT',
    'FLT BASE','FLT WID','HP Q','LP Q','F ATK','F DEC','F BASE','F WID',
    'EQ FREQ','EQ GAIN','SR RED','D SEND','D TIME','D FB','D BASE','D WIDTH',
    'L1 DEST','L1 TRIG','L1 WAVE','L1 MULT','L1 SPEED','L1 INTL','L1 DEPTH','L1 PHASE',
    'L2 DEST','L2 TRIG','L2 WAVE','L2 MULT','L2 SPEED','L2 INTL','L2 DEPTH','L2 PHASE',
    'L3 DEST','L3 TRIG','L3 WAVE','L3 MULT','L3 SPEED','L3 INTL','L3 DEPTH','L3 PHASE',
    'ALT 1','ALT 2','ALT 3','ALT 4','DRIFT','FOLD','BITS','NOISE',
    'AMP A CURVE','AMP D CURVE','AMP R CURVE','VELOCITY','KEY LEVEL','ENV AMOUNT','PAN KEY','GAIN',
    'FILTER KEY','VELOCITY TO FILTER','FILTER ENV AMOUNT','FILTER DRIVE','HP SLOPE','LP SLOPE','FILTER MIX','FILTER SAT',
    'EQ Q','EQ MIX','BIT DEPTH','DELAY PING PONG','DELAY DUCK','DELAY DRIVE','DELAY MOD RATE','DELAY MOD DEPTH',
    'LFO 1 FADE','LFO 1 DELAY','LFO 1 SLEW','LFO 1 SYMMETRY','LFO 1 STEPS','LFO 1 POLARITY','LFO 1 VELOCITY','LFO 1 KEY TRACK',
    'LFO 2 FADE','LFO 2 DELAY','LFO 2 SLEW','LFO 2 SYMMETRY','LFO 2 STEPS','LFO 2 POLARITY','LFO 2 VELOCITY','LFO 2 KEY TRACK',
    'LFO 3 FADE','LFO 3 DELAY','LFO 3 SLEW','LFO 3 SYMMETRY','LFO 3 STEPS','LFO 3 POLARITY','LFO 3 VELOCITY','LFO 3 KEY TRACK'
];
/* Five-glyph values fit one Move knob column without overwriting the next. */
const LFO_DEST_SCREEN = [
    'OFF','PITCH',
    'SYN1','SYN2','SYN3','SYN4','SYN5','SYN6','SYN7','TUNE',
    'AATK','AHOLD','ADEC','AREL','DIST','VOL','PAN','PORT',
    'FBASE','FWID','HPQ','LPQ','FATK','FDEC','EBASE','EWID',
    'EQFRQ','EQGN','SRATE','DSEND','DTIME','DFB','DBASE','DWID',
    'L1DST','L1TRG','L1WAV','L1MUL','L1SPD','L1INT','L1DEP','L1PHS',
    'L2DST','L2TRG','L2WAV','L2MUL','L2SPD','L2INT','L2DEP','L2PHS',
    'L3DST','L3TRG','L3WAV','L3MUL','L3SPD','L3INT','L3DEP','L3PHS',
    'ALT1','ALT2','ALT3','ALT4','DRIFT','FOLD','BITS','NOISE',
    'ACRV','DCRV','RCRV','VEL','KLVL','EAMT','PKEY','GAIN',
    'KTRK','V2F','FEAMT','FDRV','HPSLP','LPSLP','FMIX','FSAT',
    'EQQ','EQMIX','BITS','PING','DUCK','DDRV','DMODR','DMODD',
    'L1FAD','L1DLY','L1SLW','L1SYM','L1STP','L1POL','L1VEL','L1KEY',
    'L2FAD','L2DLY','L2SLW','L2SYM','L2STP','L2POL','L2VEL','L2KEY',
    'L3FAD','L3DLY','L3SLW','L3SYM','L3STP','L3POL','L3VEL','L3KEY'
];
const LFO_MODE_VALUES = [0, 32, 64, 96, 127];
const LFO_TRIGGER_NAMES = ['Free', 'Retrigger', 'Hold', 'One Shot', 'Half Shot'];
const LFO_TRIGGER_SCREEN = ['FREE', 'TRIG', 'HOLD', 'ONE', 'HALF'];
const LFO_WAVE_NAMES = ['Sine', 'Saw', 'Triangle', 'Square', 'Random'];
const LFO_WAVE_SCREEN = ['SINE', 'SAW', 'TRI', 'SQR', 'RAND'];
const SHIFT_PARAM_BASE = 56;
const COMMON = [
    null,
    ['ATK','HOLD','DEC','REL','DIST','VOL','PAN','PORT'],
    ['BASE','WDTH','HPQ','LPQ','FATK','FDEC','FBA','FWD'],
    ['EQF','EQG','SRR','DSND','DTIM','DFB','DBAS','DWID'],
    ['DEST','TRIG','WAVE','MULT','SPD','INTL','DPTH','PHAS'],
    ['DEST','TRIG','WAVE','MULT','SPD','INTL','DPTH','PHAS'],
    ['DEST','TRIG','WAVE','MULT','SPD','INTL','DPTH','PHAS']
];
const SYNTH = [
    ['UNIL','UNIW','UNIX','SUBX','SUB1','SUB2','-','TUNE'],
    ['UNIL','UNIW','SUB1','SUB2','PW','PWAD','PWRS','TUNE'],
    ['PCH2','PCH3','PCH4','WAVE','PW','CHRL','CHRW','TUNE'],
    ['PW','PWAD','PWRS','WAVE','MOD','MSRC','MFRQ','TUNE'],
    ['WAVE','WP','WPM','WPRS','SYNC','SFRQ','-','TUNE'],
    ['1FRQ','1FIN','1FB','1ENV','2FRQ','2VOL','TONE','TUNE']
];
const SYNTH_SHIFT = [
    ['BRITE','STACK','SUBPW','SUBO','DRIFT','FOLD','BITS','NOISE'],
    ['ASYM','STACK','SUB','SYNC','DRIFT','FOLD','BITS','NOISE'],
    ['LEV1','LEV2','LEV3','LEV4','DRIFT','FOLD','BITS','NOISE'],
    ['RING','SUB','MODMX','CHAOS','DRIFT','FOLD','BITS','NOISE'],
    ['WAVE2','BLEND','DETUN','OCT','DRIFT','FOLD','BITS','NOISE'],
    ['2FINE','2FB','3FRQ','3LVL','DRIFT','FOLD','BITS','NOISE']
];
const COMMON_SHIFT = [null,
    ['ACRV','DCRV','RCRV','VEL','KLVL','EAMT','PKEY','GAIN'],
    ['KTRK','V2F','EAMT','FDRV','HPSLP','LPSLP','MIX','SAT'],
    ['EQQ','EQMIX','BITS','PING','DUCK','DDRV','DMODR','DMODD'],
    ['FADE','DELAY','SLEW','SYMM','STEPS','POL','VEL','KEY'],
    ['FADE','DELAY','SLEW','SYMM','STEPS','POL','VEL','KEY'],
    ['FADE','DELAY','SLEW','SYMM','STEPS','POL','VEL','KEY']
];
const ARP_LABELS = ['ON','LATCH','MODE','RATE','OCT','GATE','LEN','VELOC'];
const ARP_STEP_LABELS = ['ST01','ST02','ST03','ST04','ST05','ST06','ST07','ST08'];
const ARP_STEP_SHIFT_LABELS = ['ST09','ST10','ST11','ST12','ST13','ST14','ST15','ST16'];
const ARP_MODE_NAMES = ['UP','DOWN','PEND','RAND','PLAY','CNVRG'];
const ARP_MODE_LONG = ['Up','Down','Pendulum','Random','Played Order','Converge'];
const ARP_RATE_NAMES = ['1/4','1/8','1/8T','1/16','1/16T','1/32','1/32T','1/64'];

let page = 0, machine = 0, shift = false, shiftVisual = false;
let recordArmed = false, tickCount = 0;
let arpEnabled = false, arpLatch = false, userWaveMask = 0;
let arpValues = [0, 0, 0, 3, 1, 92, 16, 0];
let arpOffsets = new Array(16).fill(0);
let values = new Array(8).fill(0);
let altValues = new Array(8).fill(0);
let needsRedraw = true, ready = false, focusBank = 0;
/* Last external-CC counter seen from the engine. `null` means "not read yet" —
 * the first successful poll adopts the engine's value without triggering a
 * refresh, so mounting the editor does not cost a page read. */
let ccRevision = null;

/* Every gp() is a BLOCKING round-trip to the shim, serviced once per SPI frame
 * (~23 ms) and abandoned after 100 ms — schwung's comment above
 * js_shadow_get_param calls it "the where-does-the-tick-time-go measurement".
 * The channel serves roughly 44 reads a second in total. Two consequences run
 * through this file:
 *
 *   1. fetchAll() is ~20 round-trips, so it must never sit on the input path.
 *      It used to run from setPage() and setMachine(), making every jog detent
 *      cost about half a second.
 *   2. Under contention a read TIMES OUT and returns null. Folding that into a
 *      default (`gp('p1') || '0'`) silently zeroes the mirror, and the next
 *      knob turn writes the zero back to the DSP, destroying the parameter.
 *      Reads that fail must leave the previous value alone.
 */
function gp(key) {
    const v = host_module_get_param(key);
    return v === null || v === undefined ? null : String(v);
}

/* Integer read that reports failure instead of inventing a value. */
function gpNum(key) {
    const s = gp(key);
    if (s === null || s === '') return null;
    const n = parseInt(s, 10);
    return Number.isFinite(n) ? n : null;
}

/* Keep `prev` when the read did not come back. */
function gpNumOr(key, prev) {
    const n = gpNum(key);
    return n === null ? prev : n;
}

/* decodeDelta reports ACCUMULATED encoder movement — the shim batches ticks per
 * SPI frame, so one brisk turn arrives as a single event carrying 20 or more.
 * Applied raw to a short range (6 machines, 9 pages, 6 arp modes) that lands on
 * an end stop every time. Cap the magnitude at a quarter of the range: one
 * detent still moves one, a full-speed spin moves a useful chunk. */
function scaledMove(delta, min, max) {
    const span = max - min;
    const cap = span > 0 ? Math.max(1, Math.ceil(span / 4)) : 1;
    const mag = Math.min(Math.abs(delta), cap);
    return delta > 0 ? mag : -mag;
}

function shiftActive() {
    if (typeof shadow_get_shift_held === 'function' && shadow_get_shift_held() !== 0)
        return true;
    return shift;
}

function shiftLayer() { return shiftActive() && page !== ARP_PAGE; }
function names() {
    if (page === ARP_PAGE) return ARP_LABELS;
    if (page === ARP_STEP_PAGE)
        return shiftLayer() ? ARP_STEP_SHIFT_LABELS : ARP_STEP_LABELS;
    return shiftLayer() ? (page === 0 ? SYNTH_SHIFT[machine] : COMMON_SHIFT[page])
                        : (page === 0 ? SYNTH[machine] : COMMON[page]);
}
function activeValues() {
    if (page === ARP_PAGE) return arpValues;
    if (page === ARP_STEP_PAGE) return arpOffsets.slice(shiftLayer() ? 8 : 0,
                                                       shiftLayer() ? 16 : 8);
    return shiftLayer() ? altValues : values;
}
function isLfoDestination(i) { return !shiftLayer() && page >= 4 && page < SOUND_PAGES && i === 0; }
function isLfoTrigger(i) { return !shiftLayer() && page >= 4 && page < SOUND_PAGES && i === 1; }
function isLfoWave(i) { return !shiftLayer() && page >= 4 && page < SOUND_PAGES && i === 2; }
function isLfoMode(i) { return isLfoTrigger(i) || isLfoWave(i); }
function destinationIndex(value) { return Math.max(0, Math.min(LFO_DESTS.length - 1, Math.round(value))); }
function lfoModeIndex(value) {
    const raw = Math.max(0, Math.min(127, Math.round(value)));
    return Math.max(0, Math.min(4, Math.floor(raw * 5 / 128)));
}
function currentParamId(i) { return (shiftLayer() ? SHIFT_PARAM_BASE : 0) + page * 8 + i; }
const FM_RATIO_NAMES = ['1/64','1/32','1/16','3/32','1/8','3/16','1/4','5/16',
    '3/8','7/16','1/2','5/8','3/4','7/8','1','1.25','1.5','1.75','2','2.5',
    '3','3.5','4','5','6','7','8','9','10','12','16','24'];
function bandIndex(value, count) { return Math.max(0, Math.min(count - 1, Math.floor(value * count / 128))); }
function noteName(value) {
    const names = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
    return `${names[value % 12]}${Math.floor(value / 12) - 1}`;
}
function machineParameterValue(i, value) {
    if (shiftLayer() || page !== 0) return null;
    if (machine === 1 && i === 6) return value < 64 ? 'OFF' : 'ON';
    if (machine === 2 && i < 3) {
        if (value < 4) return 'OFF';
        if (value >= 112) return ['6/5','5/4','4/3','3/2'][Math.min(3, Math.floor((value - 112) / 4))];
        return `${Math.round((value - 4) * 24 / 107)}ST`;
    }
    if (machine === 3) {
        if (i === 2) return value < 64 ? 'OFF' : 'ON';
        if (i === 3) return ['TRI','SAW','PULS','MIX','NOIS'][bandIndex(value, 5)];
        if (i === 4) return ['OFF','RING','SYNC','R+S'][bandIndex(value, 4)];
        if (i === 5) return value < 64 ? 'MFRQ' : 'PRCH';
        if (i === 6) return noteName(value);
    }
    if (machine === 4) {
        if (i === 0) {
            const wave = bandIndex(value, 32);
            return wave >= 24 && ((userWaveMask >> (wave - 24)) & 1)
                ? `USR${wave - 23}` : `W${String(wave).padStart(2, '0')}`;
        }
        if (i === 3) return value < 64 ? 'OFF' : 'ON';
        if (i === 4) return ['OFF','SFRQ','PRCH'][bandIndex(value, 3)];
        if (i === 5) return noteName(value);
    }
    if (machine === 5 && (i === 0 || i === 4)) return FM_RATIO_NAMES[bandIndex(value, 32)];
    return null;
}
function secondsFromParam(value, maxSeconds) {
    return value <= 0 ? 0 : 0.002 * Math.pow(maxSeconds / 0.002, value / 127);
}
function shortTime(seconds) {
    if (seconds <= 0) return '0MS';
    if (seconds < 1) return `${Math.round(seconds * 1000)}MS`.slice(0, 5);
    return `${seconds < 10 ? seconds.toFixed(2) : seconds.toFixed(1)}S`.slice(0, 5);
}
function shortHz(value) {
    const hz = 18 * Math.pow(1000, value / 127);
    return hz >= 1000 ? `${(hz / 1000).toFixed(hz < 10000 ? 1 : 0)}K` : `${Math.round(hz)}HZ`;
}
function parameterValue(i, value) {
    const machineValue = machineParameterValue(i, value);
    if (machineValue !== null) return machineValue;
    const pid = currentParamId(i);
    const times = {8:4, 9:4, 10:12, 11:8, 15:3, 20:4, 21:8,
        88:8, 89:4, 90:2, 96:8, 97:4, 98:2, 104:8, 105:4, 106:2};
    if (times[pid]) return shortTime(secondsFromParam(value, times[pid]));
    if (pid === 28) return shortTime(0.015 + 1.82 * value / 127);
    if (pid === 24) return shortHz(value);
    if (pid === 7) { const cents = Math.round((value - 64) * 100 / 64); return `${cents >= 0 ? '+' : ''}${cents}C`; }
    if (pid === 14) { const pan = Math.round((value - 64) * 100 / 64); return pan === 0 ? 'CENTR' : `${pan < 0 ? 'L' : 'R'}${Math.abs(pan)}`; }
    if (pid === 25) { const gain = value - 64; return gain === 0 ? '0DB' : `${gain > 0 ? '+' : ''}${gain}DB`; }
    if (pid === 76 || pid === 77) return value < 64 ? '12DB' : '24DB';
    if (pid === 93 || pid === 101 || pid === 109) return value < 64 ? 'UNI' : 'BI';
    if (pid === 62 || pid === 82) return `${16 - Math.round(value * 12 / 127)}BIT`;
    return `${value}`.padStart(3, '0');
}
function displayValue(i, value) {
    if (page === ARP_PAGE) {
        if (i < 2) return value ? 'ON' : 'OFF';
        if (i === 2) return ARP_MODE_NAMES[Math.max(0, Math.min(5, value))];
        if (i === 3) return ARP_RATE_NAMES[Math.max(0, Math.min(7, value))];
        if (i === 7 && value === 0) return 'PLAY';
        return `${value}`;
    }
    if (page === ARP_STEP_PAGE) return `${value >= 0 ? '+' : ''}${value}`;
    if (isLfoDestination(i)) return LFO_DEST_SCREEN[destinationIndex(value)];
    if (isLfoTrigger(i)) return LFO_TRIGGER_SCREEN[lfoModeIndex(value)];
    if (isLfoWave(i)) return LFO_WAVE_SCREEN[lfoModeIndex(value)];
    return parameterValue(i, value);
}
function announcedValue(i, value) {
    if (page === ARP_PAGE) {
        if (i < 2) return value ? 'On' : 'Off';
        if (i === 2) return ARP_MODE_LONG[Math.max(0, Math.min(5, value))];
        if (i === 3) return ARP_RATE_NAMES[Math.max(0, Math.min(7, value))];
        if (i === 7 && value === 0) return 'Played velocity';
    }
    if (page === ARP_STEP_PAGE) return `${value >= 0 ? 'plus ' : 'minus '}${Math.abs(value)} semitones`;
    if (isLfoDestination(i)) return LFO_DESTS[destinationIndex(value)];
    if (isLfoTrigger(i)) return LFO_TRIGGER_NAMES[lfoModeIndex(value)];
    if (isLfoWave(i)) return LFO_WAVE_NAMES[lfoModeIndex(value)];
    return displayValue(i, value);
}

/* A full refresh. ~20 blocking round-trips, so callers must keep it off the
 * input path — see refreshSoon(). Every read that fails keeps its previous
 * value rather than falling back to a literal. */
function fetchAll() {
    const mv = gpNum('machine');
    if (mv === null) return false;
    machine = Math.max(0, Math.min(MACHINES.length - 1, mv));
    recordArmed = gpNumOr('record', recordArmed ? 1 : 0) !== 0;
    arpEnabled = gpNumOr('arp_enabled', arpEnabled ? 1 : 0) !== 0;
    arpLatch = gpNumOr('arp_latch', arpLatch ? 1 : 0) !== 0;
    arpValues = [arpEnabled ? 1 : 0, arpLatch ? 1 : 0,
        gpNumOr('arp_mode', arpValues[2]),
        gpNumOr('arp_rate', arpValues[3]),
        gpNumOr('arp_octaves', arpValues[4]),
        gpNumOr('arp_gate', arpValues[5]),
        gpNumOr('arp_length', arpValues[6]),
        gpNumOr('arp_velocity', arpValues[7])];
    const offsets = gp('arp_offsets');
    if (offsets !== null && offsets !== '') {
        arpOffsets = offsets.split(',').map(v => parseInt(v, 10) || 0);
        while (arpOffsets.length < 16) arpOffsets.push(0);
    }
    userWaveMask = gpNumOr('user_wave_mask', userWaveMask);
    if (page < SOUND_PAGES) host_module_set_param('page', `${page}`);
    for (let i = 0; i < 8; i++) values[i] = gpNumOr(`p${i + 1}`, values[i]);
    for (let i = 0; i < 8; i++) altValues[i] = gpNumOr(`alt${i + 1}`, altValues[i]);
    return true;
}

/* Ask for a full refresh on a later tick. setPage() and setMachine() used to
 * call fetchAll() inline, so a jog turn through the nine pages cost ~20
 * round-trips per detent — most of a second of frozen UI each time. */
let refreshPending = false;
function refreshSoon() { refreshPending = true; }

/* Pull the visible page's sixteen values in ONE round-trip.
 *
 * This exists because external MIDI CC edits the DSP directly and this editor
 * never hears about it: an external CC arrives on the audio process's on_midi
 * (schwung routes cable 2 to the DSP, and onMidiMessageExternal is an
 * overtake-only hook — a chain UI cannot see external MIDI at all). So the
 * mirror in values[]/altValues[] went stale, the screen kept showing the old
 * number, and the next knob turn sent stale+delta and undid the CC.
 *
 * Refreshing that mirror with sixteen separate reads would cost most of a
 * second per CC and saturate the channel during a controller sweep, so the
 * engine serves them joined: "p1,..,p8|alt1,..,alt8". */
/* Returns true if a value moved, false if none did, and NULL if the read did
 * not come back. The caller must not retire the pending refresh on null — a
 * read that timed out has corrected nothing, and treating it as "done" leaves
 * the mirror stale until the NEXT CC arrives, which is the original bug. */
function fetchPageValues() {
    const raw = gp('page_values');
    if (raw === null || raw === '') return null;
    const banks = raw.split('|');
    if (banks.length < 2) return null;
    const primary = banks[0].split(','), alt = banks[1].split(',');
    if (primary.length < 8 || alt.length < 8) return null;
    let moved = false;
    for (let i = 0; i < 8; i++) {
        const pv = parseInt(primary[i], 10), av = parseInt(alt[i], 10);
        /* A malformed field keeps the previous value, exactly as a failed
         * single-key read does — never fold a bad read into a literal. */
        if (Number.isFinite(pv) && pv !== values[i]) { values[i] = pv; moved = true; }
        if (Number.isFinite(av) && av !== altValues[i]) { altValues[i] = av; moved = true; }
    }
    return moved;
}

function toggleRecord() {
    recordArmed = !recordArmed;
    host_module_set_param('record', recordArmed ? '1' : '0');
    announce(recordArmed ? 'Automation recording armed' : 'Automation recording off');
    needsRedraw = true;
}

function setPage(next) {
    page = (next + PAGES.length) % PAGES.length;
    focusBank = 0;
    if (page < SOUND_PAGES) host_module_set_param('page', `${page}`);
    refreshSoon();
    announce(`${PAGES[page]} page`);
    needsRedraw = true;
}

function setMachine(next) {
    machine = (next + MACHINES.length) % MACHINES.length;
    host_module_set_param('machine', `${machine}`);
    refreshSoon();
    announceParameter('Machine', MACHINES[machine]);
    needsRedraw = true;
}

function adjust(i, delta) {
    focusBank = i >= 4 ? 1 : 0;
    if (page === ARP_PAGE) {
        const keys = ['arp_enabled','arp_latch','arp_mode','arp_rate',
            'arp_octaves','arp_gate','arp_length','arp_velocity'];
        const low = [0,0,0,0,1,1,1,0], high = [1,1,5,7,4,127,16,127];
        const v = i < 2 ? (arpValues[i] ? 0 : 1)
            : Math.max(low[i], Math.min(high[i],
                arpValues[i] + scaledMove(delta, low[i], high[i])));
        if (v === arpValues[i] && i >= 2) return;
        arpValues[i] = v;
        if (i === 0) arpEnabled = v !== 0;
        if (i === 1) arpLatch = v !== 0;
        host_module_set_param(keys[i], `${v}`);
        announceParameter(ARP_LABELS[i], announcedValue(i, v));
        needsRedraw = true;
        return;
    }
    if (page === ARP_STEP_PAGE) {
        const offsetIndex = i + (shiftLayer() ? 8 : 0);
        const v = Math.max(-24, Math.min(24,
            arpOffsets[offsetIndex] + scaledMove(delta, -24, 24)));
        if (v === arpOffsets[offsetIndex]) return;
        arpOffsets[offsetIndex] = v;
        host_module_set_param('arp_offset', `${offsetIndex}:${v}`);
        announceParameter(`Arp step ${offsetIndex + 1}`, announcedValue(i, v));
        needsRedraw = true;
        return;
    }
    const target = activeValues();
    let v;
    if (isLfoDestination(i)) {
        v = Math.max(0, Math.min(LFO_DESTS.length - 1,
            destinationIndex(target[i]) + scaledMove(delta, 0, LFO_DESTS.length - 1)));
    } else if (isLfoMode(i)) {
        const next = Math.max(0, Math.min(4,
            lfoModeIndex(target[i]) + scaledMove(delta, 0, 4)));
        v = LFO_MODE_VALUES[next];
    } else {
        v = Math.max(0, Math.min(127, target[i] + scaledMove(delta, 0, 127)));
    }
    if (v === target[i]) return;
    target[i] = v;
    host_module_set_param(shiftLayer() ? `alt${i + 1}` : `p${i + 1}`, `${v}`);
    announceParameter(names()[i], announcedValue(i, v));
    needsRedraw = true;
}

function draw() {
    clear_screen();
    drawHeader(recordArmed ? `REC · ${MACHINES[machine]}`
                           : `${arpEnabled ? (arpLatch ? 'ARP L ·' : 'ARP ·') : 'MONO V ·'} ${MACHINES[machine]}`);
    const n = names();
    const shown = activeValues();
    const first = focusBank * 4;
    for (let column = 0; column < 4; column++) {
        const i = first + column;
        const x = column * 32 + 2;
        print(x, 18, n[i], 1);
        print(x, 34, displayValue(i, shown[i]), 1);
    }
    let footer;
    if (page === ARP_PAGE)
        footer = {left: `ARP K${first + 1}-${first + 4}`, right: 'JOG=PAGE'};
    else if (page === ARP_STEP_PAGE)
        footer = {left: `${shiftLayer() ? 'ST9-16' : 'ST1-8'} K${first + 1}-${first + 4}`,
                  right: shiftLayer() ? 'SH BANK' : 'SH=9-16'};
    else if (shiftActive())
        footer = {left: `SH K${first + 1}-${first + 4}`,
                  right: recordArmed ? 'REC KNOBS' : 'JOG=MACH'};
    else footer = {left: `${PAGES[page]} K${first + 1}-${first + 4}`,
                   right: recordArmed ? 'REC KNOBS' : 'JOG=PAGE'};
    drawFooter(footer);
    needsRedraw = false;
}

globalThis.init = function() {
    ready = fetchAll();
    needsRedraw = true;
    announceView('Mono Voice');
};

globalThis.onResume = function() {
    ready = fetchAll();
    ccRevision = null;   /* fetchAll just made the mirror current */
    needsRedraw = true;
};

globalThis.tick = function() {
    tickCount++;
    if (!ready) ready = fetchAll();

    /* A page or machine change asked for a refresh; do it here rather than on
     * the input path, so the jog wheel stays responsive while it happens. */
    if (ready && refreshPending) {
        refreshPending = false;
        fetchAll();
        needsRedraw = true;
    } else if (ready && tickCount % 4 === 0) {
        /* One read carries both the record-arm state and the external-CC
         * counter, so following CC costs nothing extra while it is idle. The
         * counter is bumped ONLY by mono_cc_param, never by mono_set_param —
         * watching the engine's general `revision` here would fire on this
         * editor's own knob writes and re-read what it just sent. */
        const poll = gp('ui_poll');
        if (poll !== null) {
            const fields = poll.split(':');
            const rev = parseInt(fields[0], 10);
            /* NaN !== 0 is true, so an absent or malformed field would arm
             * recording on its own. A field that did not parse keeps the
             * previous value, like every other read in this file. */
            const record = parseInt(fields[1], 10);
            if (Number.isFinite(record) && (record !== 0) !== recordArmed) {
                recordArmed = record !== 0;
                needsRedraw = true;
            }
            if (Number.isFinite(rev) && rev !== ccRevision) {
                if (ccRevision === null) {
                    /* First poll only adopts the counter; there is nothing
                     * stale to correct because init() just read everything. */
                    ccRevision = rev;
                } else {
                    const moved = fetchPageValues();
                    /* Only retire the pending refresh once the read actually
                     * landed, so a timed-out read is retried next poll instead
                     * of being silently forgotten. */
                    if (moved !== null) {
                        if (moved) needsRedraw = true;
                        ccRevision = rev;
                    }
                }
            }
        }
        /* The arp pages used to run a whole fetchAll() here — twenty
         * round-trips six times a second, three times what the param channel
         * can serve, which is what made reads elsewhere time out. Once a
         * second is plenty for reflecting an edit made from the browser
         * editor. */
        if (page >= ARP_PAGE && tickCount % 48 === 0) {
            fetchAll();
            needsRedraw = true;
        }
    }
    const active = shiftActive();
    if (active !== shiftVisual) { shiftVisual = active; needsRedraw = true; }
    if (needsRedraw) draw();
};

globalThis.onMidiMessageInternal = function(data) {
    if ((data[0] & 0xF0) !== 0xB0) return;
    const cc = data[1], val = data[2];
    if (cc === MoveShift) { shift = val > 0; needsRedraw = true; return; }
    if (cc === MoveRec && val > 0) { toggleRecord(); return; }
    if (cc === MoveMainKnob) {
        const d = decodeDelta(val);
        if (d) shiftActive()
            ? setMachine(machine + scaledMove(d, 0, MACHINES.length - 1))
            : setPage(page + scaledMove(d, 0, PAGES.length - 1));
        return;
    }
    if (cc === MoveLeft && val >= 64) { setPage(page - 1); return; }
    if (cc === MoveRight && val >= 64) { setPage(page + 1); return; }
    if (cc >= MoveKnob1 && cc < MoveKnob1 + 8) {
        const d = decodeDelta(val);
        if (d) adjust(cc - MoveKnob1, d);
    }
};
