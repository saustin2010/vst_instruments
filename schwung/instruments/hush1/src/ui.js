import { createSoundGeneratorUI } from '/data/UserData/schwung/shared/sound_generator_ui.mjs';

const ui = createSoundGeneratorUI({
  moduleName: 'SH-101',
  showPolyphony: false,
  showOctave: true,
  onPresetChange: () => {
    host_module_set_param('all_notes_off', '1');
  }
});

globalThis.init = ui.init;
globalThis.tick = ui.tick;
globalThis.onMidiMessageInternal = ui.onMidiMessageInternal;
globalThis.onMidiMessageExternal = ui.onMidiMessageExternal;
globalThis.loadTalVstPreset = (path) => {
  host_module_set_param('import_vstpreset_path', String(path ?? ''));
};
globalThis.reloadPresetFolder = () => {
  host_module_set_param('rescan_presets', '1');
};
