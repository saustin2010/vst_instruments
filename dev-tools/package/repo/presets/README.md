# presets/: the plugins' libraries (not in git)

Presets, kits, wavetables, patches and demo files, one folder per plugin. **Git ignores everything here except
this README**: the libraries belong to the projects they come from, so instead of copying them into this repo's
history, `tools/fetch-presets.py` downloads them from those projects, at the exact commits the plugins were built and
tested with, and checks every file against its git hash. `./install.sh` runs it for you when a plugin's folder is
missing.

```
python3 tools/fetch-presets.py              # all of them (about 25 MB)
python3 tools/fetch-presets.py hera tablor  # just these
python3 tools/fetch-presets.py --force hera # fetch again over what's there
```

Each `presets/<plugin>/` is laid out exactly like that plugin's data folder on the MPC, `/sdcard/vst/<plugin>/`;
the installer copies it there (merging: files you add on the MPC are kept). A `not-installed/` folder is never
copied.

| Folder | What | From |
|---|---|---|
| `braids/presets/` | 10 presets | [charlesvestal/schwung-braids](https://github.com/charlesvestal/schwung-braids) `src/presets` |
| `hera/presets/` | 56 presets | [charlesvestal/schwung-hera](https://github.com/charlesvestal/schwung-hera) `src/presets` |
| `obxd/presets/` | factory bank, 128 programs | [charlesvestal/schwung-obxd](https://github.com/charlesvestal/schwung-obxd) `src/presets` |
| `libpo32/kits/` | 3 kits | [mestela/schwung-libpo32](https://github.com/mestela/schwung-libpo32) `src/kits` |
| `tablor/wavetables/` | 115 wavetables (Adventure Kid, Neu KatalYst) | [athousanddetails/schwung-tablor](https://github.com/athousanddetails/schwung-tablor) `src/wavetables` |
| `tablor/not-installed/factory.tbl` | 9 factory presets: no control for them on the MPC yet (ROADMAP.md) | same repo, `src/presets` |
| `groovebank/patterns/` | 14 groove files | [mission-minnow/groovebank](https://github.com/mission-minnow/groovebank) `src/patterns` |
| `helm/helm-data/patches/` | 274 factory patches + Move Organ | [mtytel/helm](https://github.com/mtytel/helm) `patches` (CC BY 4.0); Move Organ from [andree182/schwung-helm](https://github.com/andree182/schwung-helm) |
| `mrdrums/kits/01_Starter/` | 8 synthesised drum hits | made here: `dev-tools/skin-redesign/make_starter_kit.py` |
| `midiplayer/MIDI/` | a demo file | made here: `dev-tools/midifx/make_demo_mid.py` |

Adding your own (Mr Drums kits, MIDI files, wavetables) and what's still missing: see
[../docs/presets-and-libraries.md](../docs/presets-and-libraries.md).
