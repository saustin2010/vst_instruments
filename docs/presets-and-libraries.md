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
| Tablor | 9 factory presets (`factory.tbl`) | `/sdcard/vst/tablor/presets/` | Tablor's repo |
| Tablor | 115 wavetables: Adventure Kid (65), Neu KatalYst (50) | `/sdcard/vst/tablor/wavetables/` | Tablor's repo. Adventure Kid: public domain (Kristoffer Ekstrand); Neu KatalYst: free to use, per its readme |
| Groove Bank | 14 groove files | `/sdcard/vst/groovebank/patterns/` | Groove Bank's repo |
| Helm | 274 factory patches + "Move Organ" | `/sdcard/vst/helm/helm-data/patches/` | Helm's factory patches (CC BY 4.0, Matt Tytel and contributors); Move Organ from the Schwung Helm port |
| Mr Drums | "01_Starter" kit, 8 hits | `/sdcard/vst/mrdrums/kits/01_Starter/` | made for this port (`dev-tools/skin-redesign/make_starter_kit.py`): upstream ships no samples |
| MIDI Player | 1 demo file | `/sdcard/vst/midiplayer/MIDI/` | made for this port (`dev-tools/midifx/make_demo_mid.py`) |

Built into the plugin itself (nothing to install): Hank 32 presets, Hush One 11, Moog 14, Noisemaker 256, NuSaw 27,
Aphex 41, Denis 30, Fizzik 31, Wurl 10, MonkSynth 12 singers, Mono Voice 12 patches, Plaits' three 6-op FM banks, Super Arp's 40 patterns
and 40 rhythms. Made for this port, where upstream has none (`presets.json`, the wrapper's own presets): 303 13,
Elements 12, Rings 14, Plaits 24, Mr Hyde 19, Rings FX 12, Verglas 12, Warps 12, Mutable Vibe 24, MPC Plaits 31
(2026-10-05, levels measured with `dev-tools/presets/levels.sh`). Patterns written for this repo's own
sequencers: Stevequencer 20, Stevequencer 16 14 (most with modulation lanes). Percolator has 8 kits built in; it also
plays Erica Synths' four free Pērkons kit packs (169 kits), which aren't shipped (they come without a licence to pass
them on): its [README](../originals/percolator/README.md#pērkons-kit-packs) has the download links. The other sequencers / generators
have none.

## Adding your own

| Plugin | Put files in | Format | Shows up |
|---|---|---|---|
| Mr Drums | `/sdcard/vst/mrdrums/kits/<kit name>/` | WAV or AIFF, up to 16 per folder; name order = pads 1-16 (notes 36-51) | as a kit in the KIT selector |
| MIDI Player | `/sdcard/vst/midiplayer/MIDI/` | Standard MIDI Files (`.mid`) | in the FILE selector (sorted by name) |
| Tablor | `/sdcard/vst/tablor/wavetables/<folder>/` | `.wav`, or FLAC wavetables named by frame size like the shipped packs (`.wt2048`) | in each oscillator's table selector |
| OB-Xd | `/sdcard/vst/obxd/presets/` | `.fxb` banks (OB-Xd / discoDSP, up to 128 programs each, 32 banks). An OB-Xd 1.x LV2 bank (`presets.ttl`): `python3 tools/obxd-lv2-to-fxb.py <presets.ttl or its archive> "presets/obxd/presets/<Bank name>.fxb"` | in BANK (under PATCH), by file name; PATCH browses it |
| Noisemaker | `/sdcard/vst/noisemaker/presets/<bank name>/` | TAL-NoiseMaker `.noisemakerpreset` files, subfolders included (a pack's BASS/LEAD/PAD... folders); up to 512 per bank, 64 banks | in BANK (under PATCH), by folder name; PATCH browses its first 256, by file name |
| Percolator | `/sdcard/vst/percolator/<pack folder>/` | Pērkons `.KIT` kit packs, copied as they unzip (`BANKS/NN/KITS/`, `NN/KITS/` or `KITS/`), with their `SAMPLES/1-3.wav`; optional `names.txt` (`NN Name`) | after the built-in kits in KIT and the PRESET menu (`Pack 2 05`, ...); read when the plugin is inserted |
| Hush One | `/sdcard/vst/hush1/presets/` (any subfolders) | TAL-BassLine-101 `.bassline` / `.vstpreset`, up to 512 | in PATCH after the 11 built-in presets, sorted and named by file name; read when the plugin is inserted |

Copy them over SSH/SFTP (e.g. `scp -r "My Kit" root@<mpc>:/sdcard/vst/mrdrums/kits/`), then re-insert the plugin.
Use only samples and presets you have the right to use.
Free banks to start with: for Noisemaker, the 442 presets in nine banks that oldcastle shared on LinuxMusicians
(where to get them, who made each bank and how to copy them: [its README](../schwung/instruments/noisemaker/README.md#more-presets)).

Or keep them in this repo's `presets/<plugin>/` (same layout, not tracked by git) and let `./install.sh` copy them:
put an archive you unpacked in `presets/<plugin>/not-installed/` so it isn't copied too. Careful:
`tools/fetch-presets.py --force <plugin>` replaces the whole `presets/<plugin>/` folder (OB-Xd's included), so keep
your originals elsewhere as well.

## Missing or not reachable yet

Nothing, as of 2026-10-04 (Tablor's factory presets and Libpo32's kit saving were the last two).

Move-only features with no MPC equivalent (nothing to fix):

- **Mono Voice** user waves for its DigiPRO machine are drawn in Move's web editor and stored at a Move path
  (`/data/UserData/schwung/mono-user-waves-v1.bin`). On the MPC those slots hold the built-in waves.
- **Braids** (4) and **Plaits** (3) ship Move "chain patches": presets for a whole Move chain (synth + Move effects),
  not for the synth alone. Not shipped.
- **Eucalypso, Super Arp, Mr Hyde** read their `module.json` only to describe their own controls to Move's UI.

Searched upstream on 2026-10-03 for anything else to fetch: none of these projects publishes extra OB-Xd banks,
Noisemaker preset banks or TAL-BassLine presets (TAL-BassLine-101 is a commercial plugin), so `tools/fetch-presets.py`
has none: OB-Xd, Noisemaker and Hush One read the files you bring ("Adding your own" above; done 2026-10-03).
