/*
 * Hank UI.
 *
 * The knob grid is driven by the host from module.json's ui_hierarchy, and the
 * one custom graphic lives in ui/canvas.js, so this only needs the shared
 * sound-generator base: preset browsing plus the standard lifecycle exports.
 *
 * MIT License
 */
import { createSoundGeneratorUI } from '/data/UserData/schwung/shared/sound_generator_ui.mjs';

const ui = createSoundGeneratorUI({
    moduleName: 'Hank',

    onPresetChange: () => {
        /* A preset rewrites ratio, feedback and every envelope at once. Voices
         * still ringing were started under the old values, so let them go
         * rather than have them finish as a hybrid of two patches. */
        host_module_set_param('all_notes_off', '1');
    },

    showPolyphony: false,
    showOctave: true,
});

globalThis.init = ui.init;
globalThis.tick = ui.tick;
globalThis.onMidiMessageInternal = ui.onMidiMessageInternal;
globalThis.onMidiMessageExternal = ui.onMidiMessageExternal;
