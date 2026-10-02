import {
  init,
  tick,
  onMidiMessageInternal,
  onMidiMessageExternal
} from './ui.js';

globalThis.chain_ui = {
  init,
  tick,
  onMidiMessageInternal,
  onMidiMessageExternal
};
