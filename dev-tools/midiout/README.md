# midiout: a plugin's own MIDI output port (2026-10-02)

`alsa_midi_out.h` (header-only, C or C++): `mo_open(&port, name)`, `mo_send(&port, bytes, len)`, `mo_close(&port)`.
MPC OS ignores a VST's MIDI output, so a plugin that plays or modulates other tracks opens an ALSA sequencer port
named after itself; MPC subscribes to it by itself and another track takes it as its MIDI input (verified on a Live II
2026-10-01). Taken from `steve/tools/midifx` (the sequencer adapter, which keeps its own copy). `MIDIOUT_DEBUG=1`
prints what would be sent (offline probe: `steve/tools/probe/probe.sh` passes it through). Used by: rampage.
