# Presets, kits, samples and wavetables

The files the plugins load (presets, kits, wavetables, patches) live in this repo's **`presets/<plugin>/`** folders,
which **git doesn't track**: they belong to the projects they come from. `python3 tools/fetch-presets.py` downloads
them from those projects at the commits these plugins were built and tested with (every file checked against its git
hash), and `./install.sh` runs it for you when a plugin's folder is missing, then copies `presets/<plugin>/` to the
MPC's `/sdcard/vst/<plugin>/`. The copy **merges**: files you add there yourself are kept when you update. Details:
[presets/README.md](../presets/README.md).

Checked 2026-10-03 against every engine's source (what it opens at run time) and against what upstream ships.

## What ships, and where it came from

| Plugin | Ships | On the MPC | Where it came from |
|---|---|---|---|
| Braids | 10 presets | `/sdcard/vst/braids/presets/` | the Schwung Braids module's preset files |
| Hera | 56 presets | `/sdcard/vst/hera/presets/` | the Schwung Hera module's preset files (jpcima's Hera) |
| OB-Xd | factory bank, 128 programs (`factory.fxb`) | `/sdcard/vst/obxd/presets/` | the Schwung OB-Xd module (reales' OB-Xd) |
| Libpo32 | 3 kits: tonic, tape, acid | `/sdcard/vst/libpo32/kits/` | the Schwung Libpo32 module (mestela) |
| Tablor | 115 wavetables: Adventure Kid (65), Neu KatalYst (50) | `/sdcard/vst/tablor/wavetables/` | Tablor's repo. Adventure Kid: public domain (Kristoffer Ekstrand); Neu KatalYst: free to use, per its readme |
| Groove Bank | 14 groove files | `/sdcard/vst/groovebank/patterns/` | Groove Bank's repo |
| Helm | 274 factory patches + "Move Organ" | `/sdcard/vst/helm/helm-data/patches/` | Helm's factory patches (CC BY 4.0, Matt Tytel and contributors); Move Organ from the Schwung Helm port |
| Mr Drums | "01_Starter" kit, 8 hits | `/sdcard/vst/mrdrums/kits/01_Starter/` | made for this port (`dev-tools/skin-redesign/make_starter_kit.py`): upstream ships no samples |
| MIDI Player | 1 demo file | `/sdcard/vst/midiplayer/MIDI/` | made for this port (`dev-tools/midifx/make_demo_mid.py`) |

Built into the plugin itself (nothing to install): Hank 32 presets, Hush One 11, Moog 14, Noisemaker 256, NuSaw 27,
Aphex 41, Denis 30, Fizzik 31, Wurl 10, MonkSynth 12 singers, Plaits' three 6-op FM banks, Super Arp's 40 patterns
and 40 rhythms. The others have no presets.

## Adding your own

| Plugin | Put files in | Format | Shows up |
|---|---|---|---|
| Mr Drums | `/sdcard/vst/mrdrums/kits/<kit name>/` | WAV or AIFF, up to 16 per folder; name order = pads 1-16 (notes 36-51) | as a kit in the KIT selector |
| MIDI Player | `/sdcard/vst/midiplayer/MIDI/` | Standard MIDI Files (`.mid`) | in the FILE selector (sorted by name) |
| Tablor | `/sdcard/vst/tablor/wavetables/<folder>/` | `.wav`, or FLAC wavetables named by frame size like the shipped packs (`.wt2048`) | in each oscillator's table selector |

Copy them over SSH/SFTP (e.g. `scp -r "My Kit" root@<mpc>:/sdcard/vst/mrdrums/kits/`), then re-insert the plugin.
Use only samples you have the right to use.

## Missing or not reachable yet

| # | Plugin | What | Why | Fix (see ROADMAP.md) |
|---|---|---|---|---|
| 1 | Tablor | its 9 factory presets (fetched to `presets/tablor/not-installed/factory.tbl`) | not installed and no preset control: upstream they're chosen in Move's own preset browser, which MPC doesn't have; their wavetable paths point at Move's folders (`/data/UserData/UserLibrary/Wavetables/...`) | install the file with paths rewritten to `/sdcard/vst/tablor/wavetables/`, and add a preset stepper that applies a preset's state blob (`TBLR2;key=value;...`, reset to defaults first) |
| 2 | OB-Xd | extra `.fxb` banks | the engine lists every `.fxb` in its presets folder (`fxb_bank_list`, selected by `bank_index`), but the port only exposes the factory bank's 128 programs | add a BANK selector (`bank_index`) so `.fxb` banks dropped into `/sdcard/vst/obxd/presets/` can be picked |
| 3 | Noisemaker | importing `.noisemakerpreset` banks | the engine imports TAL-NoiseMaker preset folders from `<module_dir>/presets`, but the port sets no `MODULE_DIR`, so imports are off | set `MODULE_DIR=/sdcard/vst/noisemaker` and add a bank selector |
| 4 | Hush One | importing TAL-BassLine-101 presets (`.bassline`, `.vstpreset`) | same: no `MODULE_DIR`; and PATCH stops at the 11 built-in presets (the engine reports `preset_count` = built-in + imported) | set `MODULE_DIR=/sdcard/vst/hush1`, widen PATCH (the wrapper fixes a parameter's range at build time) |
| 5 | Libpo32 | saving a kit | the engine has `save_kit` (writes `<module_dir>/presets/<kit>.json`) but there's no button, and the folder isn't created | add a SAVE KIT button and create its `presets/` folder on install |

Move-only features with no MPC equivalent (nothing to fix):

- **Mono Voice** user waves for its DigiPRO machine are drawn in Move's web editor and stored at a Move path
  (`/data/UserData/schwung/mono-user-waves-v1.bin`). On the MPC those slots hold the built-in waves.
- **Braids** (4) and **Plaits** (3) ship Move "chain patches": presets for a whole Move chain (synth + Move effects),
  not for the synth alone. Not shipped.
- **Eucalypso, Super Arp, Mr Hyde** read their `module.json` only to describe their own controls to Move's UI.

Searched upstream on 2026-10-03 for anything else to fetch: none of these projects publishes extra OB-Xd banks,
Noisemaker preset banks or TAL-BassLine presets (TAL-BassLine-101 is a commercial plugin), so for items 2-4 users
would bring files they own once the import is wired up.
