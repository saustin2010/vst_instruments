# rack: VCV Rack modules as MPC plugins (2026-10-02)

A VCV Rack module is C++ against the Rack SDK: a `Module` whose `process()` runs once per sample on params, input
voltages and output voltages. That DSP runs on the MPC through a small stand-in for the parts of the API it uses;
the panel/widget code (which needs the whole Rack GUI) is left out, and the port gets a skin like the others.

| File | What |
|---|---|
| `rack_shim.hpp` | the stand-in: `rack::engine::Module` (config*, params/inputs/outputs/lights, ProcessArgs), Port voltages with the poly helpers, `rack::simd::float_4` (portable: no SSE/NEON), the simd math, `dsp::TSchmittTrigger`, `dsp::PulseGenerator`, `ENUMS`. Extend it as modules need more |
| `extract_module.py <Module.cpp> <WidgetStruct> <out.hpp> [<plugin.hpp> <Helper>...]` | copies a module's code up to its panel widget, unchanged, plus helper structs from the plugin's plugin.hpp |

A port then has `mpc/<port>_engine.cc` (an `mpc_engine_t`: creates the Module, maps VST params to its params, feeds
MIDI in as voltages, takes outputs out as audio and/or MIDI through `steve/tools/midiout`), copies of `rack_shim.hpp`,
`mi_engine.h` (params/state) and `engine.h`, and the vendored originals under `src/<brand>/`.

Done: **rampage** (Befaco Rampage, GPL-3.0): `steve/schwung-ports/rampage`.

Licences: most open VCV plugins are GPL-3.0 (fine for your own use); closed ones (VCV's paid modules, Vult) have no
source to port. Plugins already ported to the 4ms MetaModule (Befaco, Bogaudio, Airwindows, ChowDSP, Sapphire, Count
Modula, Impromptu, Venom, Vostok...) are a good guide to which modules build without the full Rack.
