# skin-redesign: the 2026-10-01 redesign of the Schwung ports

Tools used to give every port in `steve/schwung-ports/` the OB-Xd treatment (correct parameters, presets, and a
designed screen). Everything here runs from the repo root (`~/my_code/mpc-vst-plugins`).

| File | What |
|---|---|
| `skin_gen.py` | the generator: a small Python API (`Port`) that writes a port's `params.json`, `layout.conf`, `<port>.css`, `images/` (LED buttons, wordmark, captions) and updates `vst.json` |
| `specs/<port>.py` | one spec per port: names, ranges and options, preset browser, theme, pages. Run it to regenerate that port's files |
| `build.sh <port> [src:dest ...]` | build (Docker) → package `deploy/` (with data folders) → offline test with the data mounted at its device path → previews |
| `build_all.sh [port ...]` | `build.sh` for every port (or the ones named) with its data arguments; one result line each |
| `make_gallery.py` | writes `steve/schwung-ports/GALLERY.html` (every port's previews, grouped) |
| `make_readmes.py` | writes the README of each port added 2026-10-01 from its spec, UPSTREAM, layout and test log |
| `specs/_seq.py` | shared bits for the sequencer specs: the MIDI OUT routing panel, `tidy()` (int display, labels, `send`) |
| `make_starter_kit.py` | synthesises mrdrums' bundled `01_Starter` kit |

## Typical use
```
python3 steve/tools/skin-redesign/specs/hera.py
steve/tools/skin-redesign/build.sh hera src/presets:presets
steve/tools/deploy.sh <mpc-ip> hera
```
Data arguments per port: `braids src/presets:presets`, `hera src/presets:presets`, `obxd src/presets:presets`,
`libpo32 src/kits:kits`, `mrdrums src/kits:kits`, `tablor src/wavetables:wavetables`, `groovebank src/patterns:patterns`,
`midiplayer data/MIDI:MIDI`, and Helm's two (in `build_all.sh`); the rest take none. Effects (`"effect": true`) build the
same way; their test feeds audio in.

**A spec overwrites `layout.conf`.** Once you've edited a port's layout by hand (Skin Studio), don't rerun its spec;
run only `build.sh`. OB-Xd has no spec (it was designed by hand and is the template).

## Conventions the generator follows (and why)
- Two rows of frames per page (y=92 and 404, 304 tall), eight 158-px slots; one Q-Link bank (≤16 controls) per page.
- New parameters are **appended**, so a port's existing VST indices (saved automation, Q-Link assignments) stay put.
  Parameters the engine should no longer see are renamed (same slot) with `Port.rename`, never deleted.
- Presets: a numeric index stepped by hidden PREV/NEXT triggers, showing the engine's name text (`preset_browser`).
- A stepped numeric engine value can still be a switch: give the options `values` (what each sends), e.g. Moog's
  `-2..2` ranges as `32'..2'`.
- Engines that report option values as **text** (Hush One, Mr Hyde, mrdrums) match them case-insensitively, so
  their option labels may only change case; pop-ups can still draw friendlier labels (`opts`).
- The original parameter list is saved once as `params.base.json`, so specs can be rerun.
- An engine that only parses its own option words (Eucalypso, Super Arp) gets them as `send` strings, so the labels
  shown can differ (`tidy(p, text_options=True)` in `specs/_seq.py`).
- A list too long for a pop-up becomes a stepper with PREV/NEXT triggers (`option_stepper`, e.g. Mono Voice's 114
  LFO destinations, Super Arp's 40 patterns).
- A `{"kind": "picture", "files": [...]}` item shows one image per option of its parameter (no Q-Link), e.g. the
  real waveform per Braids algorithm from `steve/tools/stitch/waveforms.py`.
- Frame title `"@logo"` draws the port's wordmark; `"@info:TITLE|LINE|LINE"` a titled frame of text lines (the
  sequencers' MIDI OUT routing panel).
- `write(module_dir=..., defines={...})`: extra C defines for the build, e.g. `MIDIFX_INIT` (`"sync=clock"`), the
  sequencer adapter's start-up settings.

## Needs
Docker (Colima) with arm32 emulation (`docker run --privileged --rm tonistiigi/binfmt --install arm` after a
Colima restart), the `mpc-vst-html-art` image (`docker build -t mpc-vst-html-art tools/html_art`), and Homebrew
bash for `tools/build_port.sh`.
