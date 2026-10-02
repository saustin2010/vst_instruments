/*
 * Noisemaker UI for Schwung / Ableton Move
 *
 * Virtual-analog synth ported from TAL Noisemaker (Patrick Kunz).
 * Page-based hardware UI on the shared sound_generator_ui base: the root
 * screen handles preset browsing + octave; the full parameter tree is
 * declared by the plugin's ui_hierarchy (get_param) and driven by the
 * Shadow UI, with the rich 128x64 "Editor" surface in canvas.js.
 *
 * GPL-2.0 (engine: Patrick Kunz / TAL)
 */

import { createSoundGeneratorUI } from '/data/UserData/schwung/shared/sound_generator_ui.mjs';

const ui = createSoundGeneratorUI({
    moduleName: 'Noisemaker',

    onOctaveChange: () => {
        /* avoid stuck notes across octave shifts */
        host_module_set_param('all_notes_off', '1');
    },

    /* 6-voice poly synth: show octave, no need for a mono indicator */
    showPolyphony: false,
    showOctave: true,
});

globalThis.init                  = ui.init;
globalThis.tick                  = ui.tick;
globalThis.onMidiMessageInternal = ui.onMidiMessageInternal;
globalThis.onMidiMessageExternal = ui.onMidiMessageExternal;
