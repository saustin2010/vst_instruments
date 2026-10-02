/* Mono — six-track machine synth and lock sequencer, full-surface UI. */
import * as os from 'os';
import {
    MoveKnob1, MoveShift, MoveMainKnob, MoveMainButton, MoveBack, MoveLeft, MoveRight,
    MoveUp, MoveDown, MoveRec, MoveMute,
    MoveDelete, MoveCopy, MoveUndo,
    Black, White, LightGrey, BrightRed, Blue, Green, BrightGreen,
    Cyan, Purple, YellowGreen, OrangeRed
} from '/data/UserData/schwung/shared/constants.mjs';
import { decodeDelta, setLED, setButtonLED }
    from '/data/UserData/schwung/shared/input_filter.mjs';
import { drawMenuHeader as drawHeader, drawMenuFooter as drawFooter }
    from '/data/UserData/schwung/shared/menu_layout.mjs';
import { announce, announceParameter, announceView }
    from '/data/UserData/schwung/shared/screen_reader.mjs';
import {
    openTextEntry, closeTextEntry, isTextEntryActive, handleTextEntryMidi,
    drawTextEntry, tickTextEntry
} from '/data/UserData/schwung/shared/text_entry.mjs';

const MACHINES = ['SW SAW', 'SW PULS', 'SW ENS', 'SID6581', 'DIGIPRO', 'FM+STAT'];
const MACHINE_SHORT = ['SAW', 'PULS', 'ENS', 'SID', 'DIGI', 'FM'];
const MACHINE_COLORS = [OrangeRed, BrightRed, YellowGreen, Purple, Cyan, Blue];
const PAGES = ['SYNTH', 'AMP', 'FILTER', 'EFFECT', 'LFO 1', 'LFO 2', 'LFO 3'];
const PAGE_SHORT = ['SYN', 'AMP', 'FLT', 'FX', 'L1', 'L2', 'L3'];
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
/* The Move screen gives each knob a 32 px column (five glyphs). Keep a
 * separate compact destination table so long names never overwrite the next
 * column. Full names remain available to the screen reader and Remote UI. */
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
const PLAY_ORDERS = ['FORWARD', 'REVERSE', 'PENDULUM', 'RANDOM'];
const PLAY_ORDER_SCREEN = ['FWD', 'REV', 'PEND', 'RAND'];
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

const TRACK_PADS = [92, 93, 94, 95, 96, 97];
const PAD_MACHINE = 98, PAD_TRANSPORT = 99;
const STEP_FIRST = 16, STEP_COUNT = 16;
const SHIFT_PARAM_BASE = 56;
const PRESET_DIR = '/data/UserData/schwung/presets/mono';
const SAVE_ROW = '[Save current...]';

let track = 0, page = 0, stepPage = 0, machine = 0, transport = 0;
let recordArmed = false;
let patternStart = 0, patternLen = 16, playOrder = 0, playStep = -1;
let swing = 0, trackFollow = true, trackStart = 0, trackLen = 16;
let trackRotate = 0, trackDiv = 1;
let keyboardOctave = 0;
let trackStates = new Array(6).fill(0);
let shift = false, deleteHeld = false, muteHeld = false, deleteUsed = false, tickCount = 0;
let shiftVisual = false;
let values = new Array(8).fill(0), steps = new Array(16).fill(0);
let altValues = new Array(8).fill(0);
/* Last external-CC counter seen from the engine; null until the first poll.
 * ccMoved is set by pollRuntime when that counter advanced, and tick() turns
 * it into one page_values read. */
let ccRevision = null, ccMoved = false;
let heldStep = null, ready = false, needsRedraw = true, resumePaints = 0;
let focusBank = 0;
let presetMode = false, presetIndex = 0, presets = [];
let deletePresetFile = null;
let seqSetup = false;
let setupPage = 0, setupIndex = 0, editStep = 0;
let stepNote = 60, stepVelocity = 100, stepGate = 100, stepMicro = 0, stepTie = 0, stepAccent = 0, stepProbability = 127, stepRetrig = 1;
let arpEnabled = 0, arpLatch = 0, arpMode = 0, arpRate = 3, arpOctaves = 1, arpGate = 92, arpLength = 16, arpVelocity = 0;
let arpOffsets = new Array(16).fill(0);
let routeMode = 0, routeAmount = 0, trackFxType = 0, trackFxAmount = 0, trackFxTone = 0, trackFxFeedback = 0, trackFxMix = 0, trackLevel = 64;
let songEnabled = 0, songLength = 1, songEditRow = 0, songStart = 0, songRowLength = 16, songRepeats = 1, songTranspose = 0;
const SETUP_PAGES = ['SEQUENCE', 'STEP DETAIL', 'ARPEGGIATOR', 'ROUTING + FX', 'SONG MODE'];
const ARP_MODE_SCREEN = ['UP', 'DOWN', 'PEND', 'RAND', 'PLAY', 'CNVRG'];
const ARP_RATE_SCREEN = ['1/4', '1/8', '1/8T', '1/16', '1/16T', '1/32', '1/32T', '1/64'];
const ROUTE_SCREEN = ['OFF', 'MIX', 'ONLY', 'RING', 'FM'];
const TRACK_FX_SCREEN = ['OFF', 'CHOR', 'FLNG', 'RING', 'RVRB', 'COMP', 'CRSH'];

function gp(key) {
    const v = host_module_get_param(key);
    return v === null || v === undefined ? null : String(v);
}

function suspendMono() {
    if (typeof host_suspend_overtake === 'function') host_suspend_overtake();
    else if (typeof host_exit_module === 'function') host_exit_module();
}

function copyOrPasteStep(paste) {
    if (!heldStep) return;
    heldStep.used = true;
    host_module_set_param(paste ? 'paste_step' : 'copy_step', `${track}:${heldStep.step}`);
    announce(`${paste ? 'Pasted' : 'Copied'} step ${heldStep.step + 1}`);
    needsRedraw = true;
}

function copyOrPasteTrack(paste) {
    host_module_set_param(paste ? 'paste_track' : 'copy_track', `${track}`);
    if (paste) { fetchAll(); paintAll(false); }
    announce(`${paste ? 'Pasted' : 'Copied'} track ${track + 1}`);
    needsRedraw = true;
}

function undoLastEdit() {
    host_module_set_param('undo', '1');
    fetchAll(); paintAll(false); needsRedraw = true; announce('Undo');
}

function clearHeldStep() {
    if (!heldStep) return;
    host_module_set_param('clear_step', `${track}:${heldStep.step}`);
    heldStep.used = true; parseSteps(); paintSteps(false); needsRedraw = true;
    announce(`Cleared step ${heldStep.step + 1}`);
}

function safePresetStem(name) {
    const clean = name.replace(/[\/\\:*?"<>|\x00-\x1f]/g, '-')
        .replace(/\s+/g, ' ').replace(/[. ]+$/g, '').trim();
    return (clean || 'Mono Pattern').slice(0, 64);
}

/* QuickJS `os.readdir` answers with a [names, errno] tuple, and the raw listing
 * includes "." and "..". Reading the tuple as a flat filename array silently
 * dropped every preset: `/\.json$/.test(namesArray)` tests the *stringified*
 * array, so the whole listing was kept or discarded as one blob depending on
 * which entry the filesystem happened to return last. When it ended in ".json"
 * the follow-up `.replace` ran against an Array and threw, which killed the
 * save before it could announce, and later killed the browser on open.
 * Unwrap the tuple the same way Schwung's own preset browser does. */
function readPresetDir() {
    let result;
    try { result = os.readdir(PRESET_DIR); } catch (e) { return []; }
    if (!Array.isArray(result) || !Array.isArray(result[0])) return [];
    return result[0].filter(entry =>
        typeof entry === 'string' && entry !== '.' && entry !== '..');
}

function loadPresetList() {
    const found = [];
    for (const file of readPresetDir()) {
        if (!/\.json$/i.test(file)) continue;
        let name = file.replace(/\.json$/i, '');
        try {
            const parsed = JSON.parse(host_read_file(`${PRESET_DIR}/${file}`) || '{}');
            if (parsed.name) name = String(parsed.name);
        } catch (e) {}
        found.push({name, file});
    }
    found.sort((a, b) => a.name.toLowerCase().localeCompare(b.name.toLowerCase()));
    presets = found;
}

function uniquePresetName(rawName, excludeFile = null) {
    const base = rawName.trim() || 'Mono Pattern';
    const used = new Set(presets.filter(p => p.file !== excludeFile)
        .map(p => p.name.toLowerCase()));
    if (!used.has(base.toLowerCase())) return base;
    let suffix = 2;
    while (used.has(`${base} ${suffix}`.toLowerCase())) suffix++;
    return `${base} ${suffix}`;
}

function uniquePresetStem(rawName, excludeFile = null) {
    const base = safePresetStem(rawName);
    const used = new Set(presets.filter(p => p.file !== excludeFile)
        .map(p => p.file.replace(/\.json$/i, '').toLowerCase()));
    if (!used.has(base.toLowerCase())) return base;
    let suffix = 2, candidate = '';
    do { candidate = `${base.slice(0, 58)} ${suffix++}`; }
    while (used.has(candidate.toLowerCase()));
    return candidate;
}

/* `os.rename` reports failure with a negative return code rather than throwing,
 * so the previous try/catch called every failed rename a success and left the
 * payload stranded in the .tmp file. Check the code, and fall back to writing
 * the destination directly so a rename refusal still saves the preset. */
function writePresetAtomically(file, payload) {
    if (typeof host_write_file !== 'function') return false;
    const path = `${PRESET_DIR}/${file}`;
    const temp = `${path}.tmp`;
    if (!host_write_file(temp, payload)) return false;
    let renamed = -1;
    try { renamed = os.rename(temp, path); } catch (e) { renamed = -1; }
    if (renamed === 0) return true;
    const direct = host_write_file(path, payload);
    try { os.remove(temp); } catch (e) {}
    return !!direct;
}

/* Preset work touches the filesystem, and text_entry.mjs closes the keyboard
 * only *after* onConfirm returns — so a throw anywhere in here strands the UI
 * in text entry with the pads still blocked, and a throw out of a MIDI handler
 * leaves the module wedged. Keep every preset entry point total: report the
 * failure on screen and carry on. */
function guardPreset(action, failureMessage) {
    try {
        return action();
    } catch (e) {
        announce(failureMessage);
        needsRedraw = true;
        return null;
    }
}

function openPresetBrowser() {
    guardPreset(() => {
        loadPresetList();
        presetIndex = 0;
        deletePresetFile = null;
        presetMode = true;
        needsRedraw = true;
        announce(`Mono presets, ${presets.length} saved`);
    }, 'Preset list failed');
}

function closePresetBrowser() {
    presetMode = false;
    needsRedraw = true;
    announceView('Mono');
}

/* A state query crosses from the UI thread to the audio host. Schwung can
 * briefly return an empty value while the Move is busy, so use the same
 * bounded retry policy as its native preset browser. */
function getStateWithRetry() {
    for (let attempt = 0; attempt < 4; attempt++) {
        const stateJson = gp('state');
        if (stateJson) return stateJson;
    }
    return null;
}

function saveCurrentPreset(rawName) {
    guardPreset(() => {
        const stateJson = getStateWithRetry();
        if (!stateJson) { announce('Preset save failed'); return; }
        if (typeof host_ensure_dir === 'function') host_ensure_dir(PRESET_DIR);
        else { try { os.mkdir(PRESET_DIR); } catch (e) {} }
        const name = uniquePresetName(rawName);
        const stem = uniquePresetStem(name);
        let state;
        try { state = JSON.parse(stateJson); } catch (e) { state = stateJson; }
        const payload = JSON.stringify({name, module: 'mono', version: 1, state});
        const ok = writePresetAtomically(`${stem}.json`, payload);
        if (!ok) { announce('Preset save failed'); return; }
        loadPresetList();
        const found = presets.findIndex(p => p.name === name);
        presetIndex = found >= 0 ? found + 1 : 0;
        presetMode = false;
        needsRedraw = true;
        announce(`Saved ${name}`);
    }, 'Preset save failed');
}

function startPresetRename() {
    const entry = presets[presetIndex - 1];
    if (!entry) return;
    openTextEntry({
        title: '',
        initialText: entry.name,
        onAnnounce: announce,
        onConfirm: rawName => guardPreset(() => {
            let payload;
            try { payload = JSON.parse(host_read_file(`${PRESET_DIR}/${entry.file}`) || '{}'); }
            catch (e) { announce('Preset rename failed'); return; }
            const name = uniquePresetName(rawName || entry.name, entry.file);
            const file = `${uniquePresetStem(name, entry.file)}.json`;
            payload.name = name;
            if (!writePresetAtomically(file, JSON.stringify(payload))) {
                announce('Preset rename failed'); return;
            }
            if (file !== entry.file) {
                try { os.remove(`${PRESET_DIR}/${entry.file}`); } catch (e) {}
            }
            loadPresetList();
            presetIndex = Math.max(1, presets.findIndex(p => p.file === file) + 1);
            deletePresetFile = null;
            needsRedraw = true;
            announce(`Renamed ${name}`);
        }, 'Preset rename failed'),
        onCancel: () => { needsRedraw = true; announce('Rename cancelled'); }
    });
}

function deleteSelectedPreset() {
    guardPreset(() => {
        const entry = presets[presetIndex - 1];
        if (!entry) return;
        if (deletePresetFile !== entry.file) {
            deletePresetFile = entry.file;
            needsRedraw = true;
            announce(`Press Delete again to remove ${entry.name}`);
            return;
        }
        try {
            if (os.remove(`${PRESET_DIR}/${entry.file}`) < 0) throw new Error('remove failed');
        } catch (e) { announce('Preset delete failed'); return; }
        deletePresetFile = null;
        loadPresetList();
        presetIndex = Math.min(presetIndex, presets.length);
        needsRedraw = true;
        announce(`Deleted ${entry.name}`);
    }, 'Preset delete failed');
}

function startPresetSave() {
    openTextEntry({
        title: '',
        initialText: 'Mono Pattern',
        onAnnounce: announce,
        onConfirm: name => saveCurrentPreset(name || 'Mono Pattern'),
        onCancel: () => { needsRedraw = true; announce('Save cancelled'); }
    });
}

function loadSelectedPreset() {
    guardPreset(() => {
        const entry = presets[presetIndex - 1];
        if (!entry || typeof host_read_file !== 'function') return;
        let stateJson = null;
        try {
            const payload = JSON.parse(host_read_file(`${PRESET_DIR}/${entry.file}`) || '{}');
            stateJson = typeof payload.state === 'string'
                ? payload.state : JSON.stringify(payload.state);
        } catch (e) {}
        if (!stateJson) { announce('Preset load failed'); return; }
        if (typeof host_module_set_param_blocking === 'function')
            host_module_set_param_blocking('state', stateJson, 1000);
        else
            host_module_set_param('state', stateJson);
        track = Math.max(0, Math.min(5, parseInt(gp('track') || '0', 10)));
        page = Math.max(0, Math.min(6, parseInt(gp('page') || '0', 10)));
        stepPage = Math.max(0, Math.min(3, parseInt(gp('step_page') || '0', 10)));
        fetchAll();
        presetMode = false;
        paintAll(true);
        needsRedraw = true;
        announce(`Loaded ${entry.name}`);
    }, 'Preset load failed');
}

function shiftActive() {
    if (typeof shadow_get_shift_held === 'function' && shadow_get_shift_held() !== 0)
        return true;
    return shift;
}
function shiftLayer() { return shiftActive(); }
function names() { return shiftLayer() ? (page === 0 ? SYNTH_SHIFT[machine] : COMMON_SHIFT[page])
                                      : (page === 0 ? SYNTH[machine] : COMMON[page]); }
function activeValues() { return shiftLayer() ? altValues : values; }
function isLfoDestination(i) { return !shiftLayer() && page >= 4 && i === 0; }
function isLfoTrigger(i) { return !shiftLayer() && page >= 4 && i === 1; }
function isLfoWave(i) { return !shiftLayer() && page >= 4 && i === 2; }
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
        if (i === 0) return `W${String(bandIndex(value, 32)).padStart(2, '0')}`;
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
    if (isLfoDestination(i)) return LFO_DEST_SCREEN[destinationIndex(value)];
    if (isLfoTrigger(i)) return LFO_TRIGGER_SCREEN[lfoModeIndex(value)];
    if (isLfoWave(i)) return LFO_WAVE_SCREEN[lfoModeIndex(value)];
    return parameterValue(i, value);
}
function announcedValue(i, value) {
    if (isLfoDestination(i)) return LFO_DESTS[destinationIndex(value)];
    if (isLfoTrigger(i)) return LFO_TRIGGER_NAMES[lfoModeIndex(value)];
    if (isLfoWave(i)) return LFO_WAVE_NAMES[lfoModeIndex(value)];
    return displayValue(i, value);
}

/* decodeDelta reports ACCUMULATED encoder movement — the shim batches ticks per
 * SPI frame, so one brisk turn arrives as a single event carrying 20 or more.
 * Applied raw to a short range (6 machines, 8 track divisions, 5 LFO modes)
 * that lands on an end stop every time. Cap the magnitude at a quarter of the
 * range: one detent still moves one, a full-speed spin moves a useful chunk. */
function scaledMove(delta, min, max) {
    const span = max - min;
    const cap = span > 0 ? Math.max(1, Math.ceil(span / 4)) : 1;
    const mag = Math.min(Math.abs(delta), cap);
    return delta > 0 ? mag : -mag;
}

function unpackSteps(raw) {
    const s = `${raw}`.split(',');
    for (let i = 0; i < 16; i++) steps[i] = parseInt(s[i], 10) || 0;
}

function parseSteps() {
    const raw = gp('steps');
    if (raw !== null && raw !== '') unpackSteps(raw);
}

/* ------------------------------------------------------------ bulk reads
 *
 * A single gp() is a blocking round-trip to the shim, serviced once per SPI
 * frame (~23 ms) and abandoned after 100 ms — schwung's comment above
 * js_shadow_get_param calls it "the where-does-the-tick-time-go measurement".
 * The channel serves roughly 44 reads a second in total, so a sixty-key editor
 * refresh done one key at a time both takes over a second AND starves every
 * other reader, which makes reads time out and come back empty.
 *
 * Overtake modules get a way out that chain editors do not: BULK_GET, one
 * round-trip for up to 64 keys. Wire format from shim_handle_param_bulk /
 * bulk_next in schwung's src/schwung_shim.c — "<count>\n" then records of
 * "<len>\n<bytes>", request carrying keys and response carrying values in the
 * same order. Falls back to individual reads on a host without the binding. */
const BULK_MAX = 48;

function encodeBulk(items) {
    let s = `${items.length}\n`;
    for (const it of items) s += `${it.length}\n${it}`;
    return s;
}

function decodeBulk(blob, expected) {
    const out = new Array(expected).fill(null);
    const s = `${blob}`;
    let p = 0;
    const readLen = () => {
        let n = 0, any = false;
        while (p < s.length && s[p] >= '0' && s[p] <= '9') {
            n = n * 10 + (s.charCodeAt(p) - 48); p++; any = true;
        }
        if (!any || s[p] !== '\n') return -1;
        p++;
        return n;
    };
    const count = readLen();
    if (count < 0) return null;
    for (let i = 0; i < count && i < expected; i++) {
        const len = readLen();
        if (len < 0) return null;
        out[i] = s.slice(p, p + len);
        p += len;
    }
    return out;
}

function getParams(keys) {
    const out = {};
    if (typeof host_module_get_params !== 'function') {
        for (const k of keys) out[k] = gp(k);
        return out;
    }
    for (let base = 0; base < keys.length; base += BULK_MAX) {
        const chunk = keys.slice(base, base + BULK_MAX);
        const blob = host_module_get_params(encodeBulk(chunk));
        const vals = (blob === null || blob === undefined)
            ? null : decodeBulk(blob, chunk.length);
        if (!vals) {
            for (const k of chunk) out[k] = gp(k);
            continue;
        }
        for (let i = 0; i < chunk.length; i++) out[chunk[i]] = vals[i];
    }
    return out;
}

/* Keys fetchAll mirrors, declared once so the batch and the unpack cannot
 * drift apart. `status` is a packed tuple and is read first. */
const FETCH_KEYS = [
    'record', 'pattern_start', 'play_order', 'swing', 'track_follow',
    'track_start', 'track_len', 'track_rotate', 'track_div', 'keyboard_octave',
    'edit_step', 'step_note', 'step_velocity', 'step_gate', 'step_micro',
    'step_tie', 'step_accent', 'step_probability', 'step_retrig',
    'arp_enabled', 'arp_latch', 'arp_mode', 'arp_rate', 'arp_octaves',
    'arp_gate', 'arp_length', 'arp_velocity', 'arp_offsets',
    'route_mode', 'route_amount', 'track_fx_type', 'track_fx_amount',
    'track_fx_tone', 'track_fx_feedback', 'track_fx_mix', 'track_level',
    'song_enabled', 'song_length', 'song_edit_row', 'song_start',
    'song_row_length', 'song_repeats', 'song_transpose',
    'track_states', 'machine', 'steps',
    'p1','p2','p3','p4','p5','p6','p7','p8',
    'alt1','alt2','alt3','alt4','alt5','alt6','alt7','alt8'
];

/* A full editor refresh. Sixty-odd values, but ONE round-trip: at ~23 ms a
 * blocking read this was over a second of work when done key by key, against
 * a channel that serves about 44 reads a second in total. */
function fetchAll() {
    const status = gp('status');
    if (status === null) return false;
    const p = status.split(':');
    const v = getParams(FETCH_KEYS);

    /* Fall back to the packed status tuple, then to the previous value. A read
     * that did not come back must never become a literal default: the mirror
     * gets written back to the DSP on the next knob turn, so inventing a zero
     * here silently destroys the parameter. */
    const num = (key, prev, packed) => {
        const raw = v[key];
        if (raw !== undefined && raw !== null && raw !== '') {
            const n = parseInt(raw, 10);
            if (Number.isFinite(n)) return n;
        }
        if (packed !== undefined && packed !== '') {
            const n = parseInt(packed, 10);
            if (Number.isFinite(n)) return n;
        }
        return prev;
    };

    transport = parseInt(p[0], 10) || 0;
    playStep = parseInt(p[1], 10);
    patternLen = parseInt(p[6], 10) || 16;
    recordArmed = num('record', recordArmed ? 1 : 0, p[7]) !== 0;
    patternStart = Math.max(0, Math.min(63, num('pattern_start', patternStart, p[8])));
    playOrder = Math.max(0, Math.min(PLAY_ORDERS.length - 1,
        num('play_order', playOrder, p[9])));
    swing = Math.max(0, Math.min(127, num('swing', swing)));
    trackFollow = num('track_follow', trackFollow ? 1 : 0) !== 0;
    trackStart = Math.max(0, Math.min(63, num('track_start', trackStart)));
    trackLen = Math.max(1, Math.min(64 - trackStart, num('track_len', trackLen)));
    trackRotate = Math.max(0, Math.min(63, num('track_rotate', trackRotate)));
    trackDiv = Math.max(1, Math.min(8, num('track_div', trackDiv)));
    keyboardOctave = Math.max(-4, Math.min(4, num('keyboard_octave', keyboardOctave)));
    editStep = Math.max(0, Math.min(63, num('edit_step', editStep)));
    stepNote = num('step_note', stepNote); stepVelocity = num('step_velocity', stepVelocity);
    stepGate = num('step_gate', stepGate); stepMicro = num('step_micro', stepMicro);
    stepTie = num('step_tie', stepTie); stepAccent = num('step_accent', stepAccent);
    stepProbability = num('step_probability', stepProbability); stepRetrig = num('step_retrig', stepRetrig);
    arpEnabled = num('arp_enabled', arpEnabled); arpLatch = num('arp_latch', arpLatch);
    arpMode = num('arp_mode', arpMode); arpRate = num('arp_rate', arpRate);
    arpOctaves = num('arp_octaves', arpOctaves); arpGate = num('arp_gate', arpGate);
    arpLength = num('arp_length', arpLength); arpVelocity = num('arp_velocity', arpVelocity);
    if (v.arp_offsets) {
        arpOffsets = v.arp_offsets.split(',').map(x => parseInt(x, 10) || 0);
        while (arpOffsets.length < 16) arpOffsets.push(0);
    }
    routeMode = num('route_mode', routeMode); routeAmount = num('route_amount', routeAmount);
    trackFxType = num('track_fx_type', trackFxType); trackFxAmount = num('track_fx_amount', trackFxAmount);
    trackFxTone = num('track_fx_tone', trackFxTone); trackFxFeedback = num('track_fx_feedback', trackFxFeedback);
    trackFxMix = num('track_fx_mix', trackFxMix); trackLevel = num('track_level', trackLevel);
    songEnabled = num('song_enabled', songEnabled); songLength = num('song_length', songLength);
    songEditRow = num('song_edit_row', songEditRow); songStart = num('song_start', songStart);
    songRowLength = num('song_row_length', songRowLength); songRepeats = num('song_repeats', songRepeats);
    songTranspose = num('song_transpose', songTranspose);
    if (v.track_states) {
        const states = v.track_states.split(',');
        for (let i = 0; i < 6; i++) trackStates[i] = parseInt(states[i], 10) || 0;
    }
    machine = Math.max(0, Math.min(5, num('machine', machine)));
    for (let i = 0; i < 8; i++) values[i] = num(`p${i + 1}`, values[i]);
    for (let i = 0; i < 8; i++) altValues[i] = num(`alt${i + 1}`, altValues[i]);
    if (v.steps) unpackSteps(v.steps);
    return true;
}

/* The playhead changes far more often than sound or pattern state. Poll its
 * four-field runtime tuple directly so LED timing never waits behind the
 * dozens of getters used by a full editor refresh. */
function pollRuntime() {
    const runtime = gp('rui_play');
    if (runtime === null) return false;
    const fields = runtime.split(':');
    transport = parseInt(fields[0], 10) || 0;
    const nextPlayStep = parseInt(fields[1], 10);
    playStep = Number.isFinite(nextPlayStep) ? nextPlayStep : -1;
    recordArmed = (parseInt(fields[3], 10) || 0) !== 0;
    /* Field 5 rides along on a read this poll already makes. It counts ONLY
     * external-CC parameter writes — the engine's general `revision` also
     * counts this editor's own knob writes, so watching that would make the
     * editor chase its own tail. */
    const rev = parseInt(fields[4], 10);
    if (Number.isFinite(rev) && rev !== ccRevision) {
        /* The first poll only adopts the counter — fetchAll() ran at init. */
        if (ccRevision !== null) ccMoved = true;
        ccRevision = rev;
    }
    /* ccMoved is deliberately NOT cleared here. It is cleared only once the
     * page read has actually landed, so a timed-out read is retried rather
     * than dropped — dropping it leaves the mirror stale until the next CC. */
    return true;
}

/* The selected page's sixteen values in ONE round-trip. An external CC writes
 * the DSP directly, so this editor's values[]/altValues[] mirror goes stale:
 * the screen keeps the old number and the next knob turn sends stale+delta,
 * undoing the CC. Reading the mirror back key-by-key would cost sixteen
 * blocking reads, so the engine serves them joined as "p1,..,p8|alt1,..,alt8". */
/* true if a value moved, false if none did, NULL if the read did not come
 * back — see the chain editor for why the difference matters. */
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
        /* A bad field keeps the previous value — never fold a failed read
         * into a literal, it gets written back on the next knob turn. */
        if (Number.isFinite(pv) && pv !== values[i]) { values[i] = pv; moved = true; }
        if (Number.isFinite(av) && av !== altValues[i]) { altValues[i] = av; moved = true; }
    }
    return moved;
}

function octaveLabel() { return `O${keyboardOctave >= 0 ? '+' : ''}${keyboardOctave}`; }

function setKeyboardOctave(next) {
    next = Math.max(-4, Math.min(4, next));
    if (next === keyboardOctave) return;
    keyboardOctave = next;
    host_module_set_param('keyboard_octave', `${keyboardOctave}`);
    announceParameter('Keyboard octave', keyboardOctave === 0 ? 'Normal'
        : `${Math.abs(keyboardOctave)} octave${Math.abs(keyboardOctave) === 1 ? '' : 's'} ${keyboardOctave > 0 ? 'up' : 'down'}`);
    needsRedraw = true;
}

function selectTrack(next) {
    track = Math.max(0, Math.min(5, next));
    host_module_set_param('track', `${track}`);
    fetchAll(); paintAll(false); needsRedraw = true;
    announce(`Track ${track + 1}, ${MACHINES[machine]}`);
}

function setPage(next) {
    page = (next + PAGES.length) % PAGES.length;
    focusBank = 0;
    host_module_set_param('page', `${page}`);
    fetchAll(); needsRedraw = true;
    announce(`${PAGES[page]} page`);
}

function setStepPage(next) {
    stepPage = (next + 4) % 4;
    host_module_set_param('step_page', `${stepPage}`);
    parseSteps(); paintSteps(false); needsRedraw = true;
    announce(`Steps ${stepPage * 16 + 1} to ${stepPage * 16 + 16}`);
}

function openSeqSetup() {
    seqSetup = true;
    setupPage = 0;
    setupIndex = 0;
    heldStep = null;
    fetchAll();
    paintSteps(false);
    needsRedraw = true;
    announce('Sequence setup');
}

function setSetupPage(next) {
    setupPage = (next + SETUP_PAGES.length) % SETUP_PAGES.length;
    focusBank = 0;
    fetchAll(); paintSteps(false); needsRedraw = true;
    announce(SETUP_PAGES[setupPage]);
}

function closeSeqSetup() {
    seqSetup = false;
    paintAll(true);
    needsRedraw = true;
    announceView('Mono');
}

function setPatternStart(next) {
    host_module_set_param('pattern_start', `${Math.max(0, Math.min(63, next))}`);
    fetchAll(); paintSteps(false); needsRedraw = true;
    announceParameter('Sequence start', `${patternStart + 1}`);
}

function setPatternLength(next) {
    host_module_set_param('pattern_len', `${Math.max(1, Math.min(64 - patternStart, next))}`);
    fetchAll(); paintSteps(false); needsRedraw = true;
    announceParameter('Sequence length', `${patternLen}`);
}

function setPlayOrder(next) {
    playOrder = Math.max(0, Math.min(PLAY_ORDERS.length - 1, next));
    host_module_set_param('play_order', `${playOrder}`);
    fetchAll(); paintSteps(false); needsRedraw = true;
    announceParameter('Play order', PLAY_ORDERS[playOrder]);
}

function setSwing(next) {
    swing = Math.max(0, Math.min(127, next));
    host_module_set_param('swing', `${swing}`);
    needsRedraw = true;
    announceParameter('Swing', `${50 + Math.round(swing * 25 / 127)} percent`);
}

function setTrackTiming(key, value, label) {
    host_module_set_param(key, `${value}`);
    fetchAll(); paintSteps(false); needsRedraw = true;
    announceParameter(label, `${value}`);
}

function adjustSeqSetup(i, delta) {
    focusBank = i >= 4 ? 1 : 0;
    if (i === 0) setPatternStart(patternStart + scaledMove(delta, 0, 63));
    else if (i === 1) setPatternLength(patternLen + scaledMove(delta, 1, 64));
    else if (i === 2) setPlayOrder(playOrder + scaledMove(delta, 0, PLAY_ORDERS.length - 1));
    else if (i === 3) setSwing(swing + scaledMove(delta, 0, 127));
    else if (shiftActive()) {
        host_module_set_param('track_follow', '1');
        fetchAll(); paintSteps(false); needsRedraw = true;
        announce(`Track ${track + 1} follows global window`);
    } else if (i === 4) setTrackTiming('track_start', Math.max(0, Math.min(63, trackStart + scaledMove(delta, 0, 63))), 'Track start');
    else if (i === 5) setTrackTiming('track_len', Math.max(1, Math.min(64 - trackStart, trackLen + scaledMove(delta, 1, 64))), 'Track length');
    else if (i === 6) setTrackTiming('track_rotate', (trackRotate + scaledMove(delta, 0, 63) + 64) % 64, 'Track rotation');
    else if (i === 7) setTrackTiming('track_div', Math.max(1, Math.min(8, trackDiv + scaledMove(delta, 1, 8))), 'Track division');
}

function setSetupValue(key, value, label) {
    host_module_set_param(key, `${value}`);
    fetchAll(); paintSteps(false); needsRedraw = true;
    announceParameter(label, `${value}`);
}

function adjustSetup(i, delta) {
    focusBank = i >= 4 ? 1 : 0;
    if (setupPage === 0) { adjustSeqSetup(i, delta); return; }
    if (setupPage === 1) {
        const keys = ['step_note','step_velocity','step_gate','step_micro','step_tie','step_accent','step_probability','step_retrig'];
        const labels = ['Note','Velocity','Gate','Microtiming','Tie','Accent','Probability','Retrig'];
        const values = [stepNote,stepVelocity,stepGate,stepMicro,stepTie,stepAccent,stepProbability,stepRetrig];
        const min = [-1,1,1,-23,0,0,0,1], max = [127,127,127,23,1,127,127,8];
        const next = i === 4 ? (stepTie ? 0 : 1) : Math.max(min[i], Math.min(max[i], values[i] + scaledMove(delta, min[i], max[i])));
        setSetupValue(keys[i], next, labels[i]);
        return;
    }
    if (setupPage === 2) {
        const keys = ['arp_enabled','arp_latch','arp_mode','arp_rate','arp_octaves','arp_gate','arp_length'];
        const labels = ['Arpeggiator','Latch','Mode','Rate','Octaves','Gate','Length'];
        const values = [arpEnabled,arpLatch,arpMode,arpRate,arpOctaves,arpGate,arpLength];
        const min = [0,0,0,0,1,1,1], max = [1,1,5,7,4,127,16];
        if (i < 7) {
            const next = i < 2 ? (values[i] ? 0 : 1) : Math.max(min[i], Math.min(max[i], values[i] + scaledMove(delta, min[i], max[i])));
            setSetupValue(keys[i], next, labels[i]);
        } else {
            const next = shiftActive() ? Math.max(0, Math.min(127, arpVelocity + scaledMove(delta, 0, 127)))
                : Math.max(-24, Math.min(24, arpOffsets[setupIndex] + scaledMove(delta, -24, 24)));
            if (shiftActive()) setSetupValue('arp_velocity', next, 'Arp velocity');
            else setSetupValue('arp_offset', `${setupIndex}:${next}`, `Arp step ${setupIndex + 1}`);
        }
        return;
    }
    if (setupPage === 3) {
        if (track === 0 && i < 2) {
            announce('Track 1 has no previous routing source. Select Track 2 through 6.');
            needsRedraw = true;
            return;
        }
        const keys = ['route_mode','route_amount','track_fx_type','track_fx_amount','track_fx_tone','track_fx_feedback','track_fx_mix','track_level'];
        const labels = [`Input from Track ${track}`,'Input depth','Track effect','Effect amount','Tone','Feedback','Mix','Level'];
        const values = [routeMode,routeAmount,trackFxType,trackFxAmount,trackFxTone,trackFxFeedback,trackFxMix,trackLevel];
        const max = [4,127,6,127,127,127,127,127];
        setSetupValue(keys[i], Math.max(0, Math.min(max[i], values[i] + scaledMove(delta, 0, max[i]))), labels[i]);
        return;
    }
    const keys = ['song_enabled','song_length','song_edit_row','song_start','song_row_length','song_repeats','song_transpose'];
    const labels = ['Song mode','Song rows','Edit row','Row start','Row length','Repeats','Transpose'];
    const values = [songEnabled,songLength,songEditRow,songStart,songRowLength,songRepeats,songTranspose];
    const min = [0,1,0,0,1,1,-24], max = [1,16,15,63,64,16,24];
    if (i < 7) {
        const next = i === 0 ? (songEnabled ? 0 : 1) : Math.max(min[i], Math.min(max[i], values[i] + scaledMove(delta, min[i], max[i])));
        setSetupValue(keys[i], next, labels[i]);
    }
}

function cycleMachine(delta) {
    machine = (machine + scaledMove(delta, 0, MACHINES.length - 1) + MACHINES.length) % MACHINES.length;
    host_module_set_param('machine', `${machine}`);
    fetchAll(); paintTracks(false); needsRedraw = true;
    announceParameter('Machine', MACHINES[machine]);
}

function adjust(i, delta) {
    focusBank = i >= 4 ? 1 : 0;
    const target = activeValues();
    let v;
    if (isLfoDestination(i)) {
        v = Math.max(0, Math.min(LFO_DESTS.length - 1, destinationIndex(target[i]) + scaledMove(delta, 0, LFO_DESTS.length - 1)));
    } else if (isLfoMode(i)) {
        const next = Math.max(0, Math.min(4, lfoModeIndex(target[i]) + scaledMove(delta, 0, 4)));
        v = LFO_MODE_VALUES[next];
    } else {
        v = Math.max(0, Math.min(127, target[i] + scaledMove(delta, 0, 127)));
    }
    if (v === target[i]) return;
    target[i] = v;
    if (heldStep) {
        heldStep.used = true;
        const absoluteParam = (shiftLayer() ? SHIFT_PARAM_BASE : 0) + page * 8 + i;
        if (deleteHeld) {
            deleteUsed = true;
            host_module_set_param('unlock', `${track}:${heldStep.step}:${absoluteParam}:0`);
        } else {
            host_module_set_param('lock', `${track}:${heldStep.step}:${absoluteParam}:${v}`);
        }
        parseSteps(); paintSteps(false);
    } else {
        host_module_set_param(shiftLayer() ? `alt${i + 1}` : `p${i + 1}`, `${v}`);
    }
    announceParameter(names()[i], deleteHeld && heldStep ? 'unlocked' : announcedValue(i, v));
    needsRedraw = true;
}

function toggleTransport() {
    transport = transport ? 0 : 1;
    host_module_set_param('transport', `${transport}`);
    announce(transport ? 'Sequencer playing' : 'Sequencer stopped');
    paintGlobals(false); needsRedraw = true;
}

function toggleRecord() {
    recordArmed = !recordArmed;
    host_module_set_param('record', recordArmed ? '1' : '0');
    announce(recordArmed ? 'Automation recording armed' : 'Automation recording off');
    paintGlobals(false); needsRedraw = true;
}

function paintTracks(force) {
    for (let i = 0; i < 6; i++) {
        const state = trackStates[i];
        const color = state & 2 ? BrightGreen : (state & 1 ? 0x10
            : (i === track ? MACHINE_COLORS[machine] : LightGrey));
        setLED(TRACK_PADS[i], color, force);
    }
}

function paintGlobals(force) {
    setLED(PAD_MACHINE, MACHINE_COLORS[machine], force);
    setLED(PAD_TRANSPORT, transport ? Green : LightGrey, force);
    setButtonLED(MoveRec, recordArmed ? BrightRed : Black, force);
}

function paintSteps(force) {
    const localPlay = playStep >= stepPage * 16 && playStep < stepPage * 16 + 16
        ? playStep - stepPage * 16 : -1;
    for (let i = 0; i < 16; i++) {
        const absolute = stepPage * 16 + i;
        if (seqSetup) {
            let c = Black;
            if (setupPage === 0) {
                const shownStart = focusBank && !trackFollow ? trackStart : patternStart;
                const shownLen = focusBank && !trackFollow ? trackLen : patternLen;
                const inWindow = absolute >= shownStart && absolute < shownStart + shownLen;
                c = inWindow ? Green : 0x10;
                if (absolute === shownStart) c = White;
                if (absolute === playStep) c = BrightRed;
            } else if (setupPage === 1) {
                c = steps[i] ? Purple : 0x10;
                if (absolute === editStep) c = White;
            } else if (setupPage === 2) {
                c = i < arpLength ? (arpOffsets[i] ? Purple : Green) : 0x10;
                if (i === setupIndex) c = White;
            } else if (setupPage === 4) {
                c = i < songLength ? Green : 0x10;
                if (i === songEditRow) c = White;
            }
            setLED(STEP_FIRST + i, c, force);
            continue;
        }
        let c = steps[i] === 2 ? Purple : (steps[i] === 1 ? BrightRed : Black);
        if (i === localPlay) c = White;
        if (heldStep && heldStep.step === stepPage * 16 + i) c = BrightGreen;
        setLED(STEP_FIRST + i, c, force);
    }
}

function paintAll(force) { paintTracks(force); paintGlobals(force); paintSteps(force); }

function draw() {
    clear_screen();
    drawHeader(`${recordArmed ? 'REC' : 'MONO'} T${track + 1} ${octaveLabel()} ${MACHINE_SHORT[machine]}`);
    const n = names();
    const shown = activeValues();
    const first = focusBank * 4;
    for (let column = 0; column < 4; column++) {
        const i = first + column;
        const x = column * 32 + 2;
        print(x, 18, n[i], 1);
        print(x, 34, displayValue(i, shown[i]), 1);
    }
    drawFooter({left: `${shiftLayer() ? 'SH ' : ''}${PAGE_SHORT[page]} K${first + 1}-${first + 4}`,
                right: heldStep ? (deleteHeld ? 'Delete+turn=clear'
                    : (shiftLayer() ? 'Shift lock' : 'turn=lock'))
                    : `${stepPage * 16 + 1}-${stepPage * 16 + 16} BACK`});
    needsRedraw = false;
}

function drawPresetBrowser() {
    clear_screen();
    drawHeader('MONO · PATTERN PRESETS');
    const rows = [SAVE_ROW, ...presets.map(p => p.name)];
    const first = Math.max(0, Math.min(Math.max(0, rows.length - 3), presetIndex - 1));
    for (let row = 0; row < 3 && first + row < rows.length; row++) {
        const index = first + row, y = 19 + row * 11;
        const selected = index === presetIndex;
        if (selected) fill_rect(0, y - 2, 128, 10, 1);
        const label = `${selected ? '> ' : '  '}${rows[index]}`.slice(0, 20);
        print(3, y, label, selected ? 0 : 1);
    }
    drawFooter({left: deletePresetFile ? 'Delete again' : 'Back · Sh+Left del',
                right: 'Click load · Shift rename'});
    needsRedraw = false;
}

function drawSeqSetup() {
    clear_screen();
    drawHeader(`MONO · ${SETUP_PAGES[setupPage]}`);
    let labels, shown, footerLeft, footerRight;
    if (setupPage === 0) {
        labels = ['START', 'LENGTH', 'ORDER', 'SWING', 'TSTART', 'TLEN', 'ROTATE', 'DIV'];
        shown = [String(patternStart + 1).padStart(2, '0'), String(patternLen).padStart(2, '0'),
            PLAY_ORDER_SCREEN[playOrder], `${50 + Math.round(swing * 25 / 127)}%`,
            trackFollow ? 'GLOBAL' : String(trackStart + 1).padStart(2, '0'),
            trackFollow ? 'GLOBAL' : String(trackLen).padStart(2, '0'),
            String(trackRotate).padStart(2, '0'), `1/${trackDiv}`];
        footerLeft = focusBank ? 'Shift+turn: global' : `END ${String(patternStart + patternLen).padStart(2, '0')}`;
        footerRight = `K${focusBank * 4 + 1}-${focusBank * 4 + 4} · Step=start`;
    } else if (setupPage === 1) {
        labels = ['NOTE','VEL','GATE','MICRO','TIE','ACCNT','PROB','RTRIG'];
        shown = [stepNote < 0 ? 'OFF' : noteName(stepNote), `${stepVelocity}`, `${stepGate}`,
            `${stepMicro >= 0 ? '+' : ''}${stepMicro}`, stepTie ? 'ON' : 'OFF', `${stepAccent}`,
            `${Math.round(stepProbability * 100 / 127)}%`, `${stepRetrig}`];
        footerLeft = `STEP ${String(editStep + 1).padStart(2, '0')}`; footerRight = 'Pads select step';
    } else if (setupPage === 2) {
        labels = ['ARP','LATCH','MODE','RATE','OCT','GATE','LEN',shiftLayer() ? 'VELOC' : 'OFFSET'];
        shown = [arpEnabled ? 'ON' : 'OFF', arpLatch ? 'ON' : 'OFF', ARP_MODE_SCREEN[arpMode],
            ARP_RATE_SCREEN[arpRate], `${arpOctaves}`, `${arpGate}`, `${arpLength}`,
            shiftLayer() ? (arpVelocity ? `${arpVelocity}` : 'PLAY') : `${arpOffsets[setupIndex] >= 0 ? '+' : ''}${arpOffsets[setupIndex]}`];
        footerLeft = `ARP STEP ${String(setupIndex + 1).padStart(2, '0')}`; footerRight = 'Pads select · Shift K8 velocity';
    } else if (setupPage === 3) {
        labels = ['INPUT','DEPTH','FX','FAMT','TONE','FDBK','MIX','LEVEL'];
        shown = [track ? ROUTE_SCREEN[routeMode] : 'NONE', track ? `${routeAmount}` : '--',
            TRACK_FX_SCREEN[trackFxType], `${trackFxAmount}`,
            `${trackFxTone}`, `${trackFxFeedback}`, `${trackFxMix}`, `${trackLevel}`];
        footerLeft = track ? `T${track + 1} <- T${track}` : 'T1 NO SRC';
        footerRight = track ? `${ROUTE_SCREEN[routeMode]} K1/2` : 'Use T2-T6';
    } else {
        labels = ['SONG','ROWS','EDIT','START','LEN','REPEAT','TRANS','PLAY'];
        shown = [songEnabled ? 'ON' : 'OFF', `${songLength}`, `${songEditRow + 1}`,
            `${songStart + 1}`, `${songRowLength}`, `${songRepeats}`,
            `${songTranspose >= 0 ? '+' : ''}${songTranspose}`, `${parseInt(gp('song_play_row') || '0', 10) + 1}`];
        footerLeft = `ROW ${songEditRow + 1}`; footerRight = 'Pads select row';
    }
    const first = focusBank * 4;
    for (let column = 0; column < 4; column++) {
        const i = first + column, x = column * 32 + 2;
        print(x, 18, labels[i], 1);
        print(x, 34, shown[i], 1);
    }
    drawFooter({left: footerLeft, right: footerRight});
    needsRedraw = false;
}

globalThis.init = function() {
    host_module_set_param('track', '0');
    host_module_set_param('page', '0');
    host_module_set_param('step_page', '0');
    ready = fetchAll(); paintAll(true); needsRedraw = true;
    announceView('Mono, six track machine synth. Up and Down change octave. Back returns to Move.');
};

globalThis.onResume = function() {
    shift = false; deleteHeld = false; muteHeld = false;
    ready = fetchAll(); paintAll(true); resumePaints = 3; needsRedraw = true;
    ccRevision = null;   /* fetchAll just made the mirror current */
};

/* New hosts use suspend_self_managed and forward Back so Mono can close an
 * internal view before parking. wantsBack keeps the same behavior on hosts
 * that implement the earlier conditional-Back proposal. */
globalThis.wantsBack = function() { return isTextEntryActive() || presetMode || seqSetup; };

globalThis.tick = function() {
    if (isTextEntryActive()) {
        tickTextEntry();
        drawTextEntry();
        return;
    }
    tickCount++;
    const active = shiftActive();
    if (active !== shiftVisual) { shiftVisual = active; needsRedraw = true; }
    if (!ready) ready = fetchAll();
    /* Every other tick, not every tick. This poll is one blocking read, and at
     * 44 a second it was claiming the entire param channel by itself — which
     * is what made the editor's own reads time out and come back empty.
     * Twenty-two playhead updates a second is still smoother than the eye. */
    if (ready && !heldStep && tickCount % 2 === 0) {
        const oldPlay = playStep, oldTransport = transport, oldRecord = recordArmed;
        pollRuntime();
        if (oldPlay !== playStep) paintSteps(false);
        if (oldTransport !== transport || oldRecord !== recordArmed) {
            paintGlobals(false); needsRedraw = true;
        }
        /* Follow an external CC now rather than waiting for the periodic
         * fetchAll below, which is up to sixty ticks away. */
        if (ccMoved) {
            const moved = fetchPageValues();
            if (moved !== null) {
                ccMoved = false;
                if (moved) needsRedraw = true;
            }
        }
        if (tickCount % 30 === 0) {
            const oldScreen = `${machine}:${keyboardOctave}:${values.join(',')}:${altValues.join(',')}`;
            const oldSteps = steps.join(','), oldTracks = trackStates.join(',');
            const oldMachine = machine;
            fetchAll();
            if (oldScreen !== `${machine}:${keyboardOctave}:${values.join(',')}:${altValues.join(',')}`)
                needsRedraw = true;
            if (oldSteps !== steps.join(',')) paintSteps(false);
            if (oldTracks !== trackStates.join(',') || oldMachine !== machine) paintTracks(false);
            if (oldMachine !== machine) paintGlobals(false);
        }
    }
    if (resumePaints > 0 && tickCount % 8 === 0) { paintAll(true); resumePaints--; }
    if (needsRedraw) presetMode ? drawPresetBrowser() : (seqSetup ? drawSeqSetup() : draw());
};

globalThis.onMidiMessageInternal = function(data) {
    if (isTextEntryActive()) {
        if ((data[0] & 0xF0) === 0xB0 && data[1] === MoveShift)
            shift = data[2] > 0;
        handleTextEntryMidi(data);
        return;
    }
    const status = data[0] & 0xF0, d1 = data[1], d2 = data[2];
    if (status === 0xB0) {
        if (d1 === MoveShift) { shift = d2 > 0; needsRedraw = true; return; }
        if (d1 === MoveMute) { muteHeld = d2 > 0; needsRedraw = true; return; }
        if (seqSetup) {
            if (d1 === MoveBack && d2 > 0) { closeSeqSetup(); return; }
            if (d1 === MoveMainKnob) { const d = decodeDelta(d2); if (d) setSetupPage(setupPage + (d > 0 ? 1 : -1)); return; }
            if (d1 === MoveLeft && d2 >= 64) { setStepPage(stepPage - 1); return; }
            if (d1 === MoveRight && d2 >= 64) { setStepPage(stepPage + 1); return; }
            if (d1 >= MoveKnob1 && d1 < MoveKnob1 + 8) {
                const d = decodeDelta(d2); if (d) adjustSetup(d1 - MoveKnob1, d); return;
            }
            return;
        }
        if (presetMode) {
            if (d1 === MoveDelete) {
                deleteHeld = d2 > 0;
                if (d2 > 0) deleteSelectedPreset();
                return;
            }
            if (d1 === MoveMainKnob) {
                const d = decodeDelta(d2);
                if (d) {
                    presetIndex = Math.max(0, Math.min(presets.length, presetIndex + d));
                    deletePresetFile = null;
                    const label = presetIndex === 0 ? SAVE_ROW : presets[presetIndex - 1].name;
                    announce(`Preset, ${label}`);
                    needsRedraw = true;
                }
                return;
            }
            if (d1 === MoveMainButton && d2 > 0) {
                if (presetIndex === 0) startPresetSave();
                else if (shiftActive()) startPresetRename();
                else loadSelectedPreset();
                return;
            }
            if (d1 === MoveLeft && d2 >= 64 && shiftActive()) {
                deleteSelectedPreset();
                return;
            }
            if (d1 === MoveBack && d2 > 0) { closePresetBrowser(); return; }
            return;
        }
        if (d1 === MoveBack && d2 > 0) { suspendMono(); return; }
        if ((d1 === MoveUp || d1 === MoveDown) && d2 > 0) {
            setKeyboardOctave(keyboardOctave + (d1 === MoveUp ? 1 : -1));
            return;
        }
        if (d1 === MoveCopy && d2 > 0) {
            if (heldStep) {
                copyOrPasteStep(shiftActive());
            } else copyOrPasteTrack(shiftActive());
            return;
        }
        if (d1 === MoveUndo && d2 > 0) {
            undoLastEdit(); return;
        }
        if (d1 === MoveDelete) {
            if (d2 > 0) { deleteHeld = true; deleteUsed = false; }
            else {
                if (heldStep && !deleteUsed) {
                    clearHeldStep();
                }
                deleteHeld = false; deleteUsed = false;
            }
            needsRedraw = true; return;
        }
        if (d1 === MoveMainButton && d2 > 0 && shiftActive()) {
            openPresetBrowser();
            return;
        }
        if (d1 === MoveRec && d2 > 0) {
            if (heldStep) clearHeldStep();
            else if (shiftActive()) undoLastEdit();
            else toggleRecord();
            return;
        }
        if (d1 === MoveMainKnob) { const d = decodeDelta(d2); if (d) setPage(page + d); return; }
        if (d1 === MoveLeft && d2 >= 64) {
            shiftActive() ? copyOrPasteTrack(false) : setStepPage(stepPage - 1); return;
        }
        if (d1 === MoveRight && d2 >= 64) {
            shiftActive() ? copyOrPasteTrack(true) : setStepPage(stepPage + 1); return;
        }
        if (d1 >= MoveKnob1 && d1 < MoveKnob1 + 8) {
            const d = decodeDelta(d2); if (d) adjust(d1 - MoveKnob1, d); return;
        }
        return;
    }

    if (presetMode) return;

    const release = status === 0x80 || (status === 0x90 && d2 === 0);
    if (release) {
        if (heldStep && d1 === STEP_FIRST + (heldStep.step % 16)) {
            if (!heldStep.used) {
                host_module_set_param('toggle_step', `${heldStep.step}`);
                parseSteps();
                announce(`Step ${heldStep.step + 1} ${steps[heldStep.step % 16] ? 'on' : 'off'}`);
            }
            const editedLock = heldStep.used;
            heldStep = null;
            if (editedLock) fetchAll();
            paintSteps(false); needsRedraw = true; return;
        }
        if (d1 >= 68 && d1 < 92) return;
        return;
    }

    if (status === 0x90 && d2 > 0) {
        if (seqSetup) {
            for (let i = 0; i < 6; i++) if (d1 === TRACK_PADS[i]) {
                selectTrack(i); focusBank = 1; needsRedraw = true; return;
            }
            if (d1 >= STEP_FIRST && d1 < STEP_FIRST + STEP_COUNT) {
                const local = d1 - STEP_FIRST;
                const absolute = stepPage * 16 + local;
                if (setupPage === 0) {
                    if (focusBank) setTrackTiming('track_start', absolute, 'Track start');
                    else setPatternStart(absolute);
                } else if (setupPage === 1) {
                    host_module_set_param('edit_step', `${absolute}`); editStep = absolute;
                    fetchAll(); paintSteps(false); needsRedraw = true; announce(`Edit step ${absolute + 1}`);
                } else if (setupPage === 2) {
                    setupIndex = local; paintSteps(false); needsRedraw = true; announce(`Arp step ${local + 1}`);
                } else if (setupPage === 4) {
                    setupIndex = local; host_module_set_param('song_edit_row', `${local}`);
                    fetchAll(); paintSteps(false); needsRedraw = true; announce(`Song row ${local + 1}`);
                }
                return;
            }
            if (d1 === PAD_TRANSPORT) {
                shiftActive() ? closeSeqSetup() : toggleTransport();
                return;
            }
            return;
        }
        if (d1 >= STEP_FIRST && d1 < STEP_FIRST + STEP_COUNT) {
            heldStep = {step: stepPage * 16 + d1 - STEP_FIRST, used: false};
            paintSteps(false); needsRedraw = true; return;
        }
        if (heldStep && d1 === PAD_MACHINE) {
            copyOrPasteStep(shiftActive());
            return;
        }
        for (let i = 0; i < 6; i++) if (d1 === TRACK_PADS[i]) {
            if (deleteHeld || muteHeld) {
                deleteUsed = true;
                host_module_set_param(shiftActive() ? 'track_solo_toggle' : 'track_mute_toggle', `${i}`);
                fetchAll(); paintTracks(false); needsRedraw = true;
                announce(`Track ${i + 1} ${shiftActive()
                    ? (trackStates[i] & 2 ? 'solo' : 'solo off')
                    : (trackStates[i] & 1 ? 'muted' : 'unmuted')}`);
            } else selectTrack(i);
            return;
        }
        if (d1 === PAD_MACHINE) { cycleMachine(shiftActive() ? -1 : 1); return; }
        if (d1 === PAD_TRANSPORT) {
            shiftActive() ? openSeqSetup() : toggleTransport();
            return;
        }
        if (d1 >= 68 && d1 < 92) return;
    }
};

globalThis.onUnload = function() {
    if (isTextEntryActive()) closeTextEntry();
    setButtonLED(MoveRec, Black, true);
};
