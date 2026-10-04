# Installing

This guide takes you from a stock MPC to playing these plugins. The short version, once you have SSH access:

```
./install.sh <mpc-address> --dry-run all     # see what it would do; changes nothing
./install.sh <mpc-address> all               # or name plugins / groups
```

## Before you start

- **A Gen1 MPC OS standalone device**: 32-bit ARM, e.g. MPC Live, Live II, One, X or a Force. Developed and tested on
  an MPC Live II with MPC OS 3.9.1. Gen2 devices (e.g. Live III) are reported to be more locked down; untested.
- **Root SSH access to it** (step 1). Stock MPC OS has none.
- **A computer** on the same network: macOS or Linux with `bash`, `ssh`, `tar` and `git` (all standard). On Windows,
  use WSL.
- **Space**: all 36 plugins take about 130 MB on the MPC's internal drive (`/sdcard`). The biggest are the ones
  that ship presets, wavetables or samples (Tablor, Aphex, Braids, Moog).
- **Save your project.** Adding plugins MPC hasn't seen before restarts MPC once.

## 1. Get root SSH access

The installer copies files to the MPC and edits its settings file, which needs a root login over SSH.

- If your MPC already runs a community firmware mod that gives root SSH, use that.
- On a stock MPC, the route used to develop these plugins (rebuilding Akai's own update image with SSH switched on) is
  written up in [docs/ssh-on-stock-firmware.md](docs/ssh-on-stock-firmware.md). Read its warnings first. Any official
  Akai update removes SSH again (your plugins stay installed; you just can't install more until SSH is back).

Check it works, from your computer (the address is in the MPC's Wi-Fi settings; many units also answer to a
`<hostname>.local` name):

```
ssh root@<mpc-address>
```

You should get a `root@...:~#` prompt; type `exit`. If you log in with a specific key file, tell the installer:
`export MPC_SSH_KEY=~/.ssh/<your key>`. With an `ssh-agent` or `~/.ssh/config` entry you don't need to.

## 2. Get this repo

```
git clone https://github.com/saustin2010/vst_instruments.git
cd vst_instruments
```

(Or download the ZIP from GitHub and unpack it.) Every plugin folder already holds its finished build in `deploy/`, so
nothing needs compiling. The presets, kits and wavetables aren't part of the download: they belong to the projects
they come from, so the installer fetches them from there the first time (into `presets/`, see step 4). To get them
all in advance: `python3 tools/fetch-presets.py`.

## 3. Run the installer

Pick plugins by folder name, by group, or `all`:

```
./install.sh --list                                  # every plugin and its folder
./install.sh <mpc-address> --dry-run hera grids      # check the MPC and show what would happen
./install.sh <mpc-address> hera grids                # install
./install.sh <mpc-address> instruments               # groups: instruments, sequencers, effects,
                                                     #   schwung, mutable-instruments, vcv-rack, all
```

Plugin names: `303`, `aphex`, `braids`, `chordism`, `denis`, `elements`, `eucalypso`, `fizzik`, `grids`, `groovebank`, `hank`, `helm`, `hera`, `hush1`, `libpo32`, `marbles`, `mazelite`, `midiplayer`, `monksynth`, `monovoice`, `moog`, `mrhyde`, `mrdrums`, `noisemaker`, `nusaw`, `obxd`, `pixelwalkers`, `plaits`, `rampage`, `rings`, `ringsfx`, `superarp`, `tablor`, `verglas`, `warps`, `wurl`.

What it does, in order:

1. **Checks the MPC**: that it's 32-bit ARM, where `MPC.settings` is, free space on `/sdcard`, and which of the plugins
   MPC already lists.
2. **Fetches missing presets.** For plugins that use preset/kit/wavetable files, it downloads them into
   `presets/<plugin>/` if they aren't there yet (`tools/fetch-presets.py`: from the original projects, at the commits
   these plugins were tested with, each file checked; needs internet access and Python 3).
3. **Copies each plugin** into a staging folder on the MPC and checks every file against its SHA-256, so nothing
   arrives damaged. Then it moves them into place:
   - the plugin itself: `/sdcard/vst/<name>.so`
   - its screen: `/sdcard/Synths/<maker> - VST - <name>/` (`TUI.json`, `Q-Links.json`, artwork)
   - its presets/data from `presets/<plugin>/` (merged in, so files you added yourself are kept):

     | Plugin | Data folder |
     |---|---|
     | Braids | `/sdcard/vst/braids/` |
     | Groove Bank | `/sdcard/vst/groovebank/` |
     | Helm | `/sdcard/vst/helm/` |
     | Hera | `/sdcard/vst/hera/` |
     | Libpo32 | `/sdcard/vst/libpo32/` |
     | MIDI Player | `/sdcard/vst/midiplayer/` |
     | Mr Drums | `/sdcard/vst/mrdrums/` |
     | OB-Xd | `/sdcard/vst/obxd/` |
     | Tablor | `/sdcard/vst/tablor/` |

   MPC keeps running while files are copied; a plugin already on a track keeps its old version until you re-insert it.
4. **Adds new plugins to MPC's plugin list.** MPC only offers plugins listed in its settings file
   (`/media/az01-internal/Settings/MPC/MPC.settings`), and only reads that list at start-up. If any plugin is new (or
   its entry changed), the installer asks before it:
   - stops MPC (the screen goes dark for about 30 seconds),
   - saves a backup next to the settings file: `MPC.settings.bak-vst_instruments-<date>`,
   - adds or updates one `<PLUGIN .../>` line per plugin (with `tools/plugin_list.awk`), checks the result (each
     plugin listed exactly once, the file complete) and only then swaps it in,
   - starts MPC again. If anything fails, the old settings stay untouched and MPC is started anyway.

   Say no and the files stay in place; run the same command later with `--yes` to do just the restart. Add
   `--register` to rewrite the entries of plugins MPC already lists (e.g. after a plugin's category changed).

## 4. Presets, kits and your own sounds

The presets, kits, samples and wavetables each plugin needs are installed with it, into `/sdcard/vst/<name>/`, from
this repo's `presets/<name>/` folder (fetched from their original projects; git doesn't track it, see
[presets/README.md](presets/README.md)). Some plugins also take your own files:

- **Mr Drums**: kit folders in `/sdcard/vst/mrdrums/kits/` (WAV or AIFF, up to 16 per folder; name order = pads 1-16).
  It ships only a small starter kit, so this is how you make it yours.
- **MIDI Player**: `.mid` files in `/sdcard/vst/midiplayer/MIDI/`.
- **Tablor**: wavetables (`.wav`, or FLAC `.wt2048` like the shipped packs) in `/sdcard/vst/tablor/wavetables/`.
- **OB-Xd**: `.fxb` banks in `/sdcard/vst/obxd/presets/`, picked with BANK under PATCH (an OB-Xd 1.x LV2 bank converts
  with `tools/obxd-lv2-to-fxb.py`).
- **Noisemaker**: TAL-NoiseMaker preset folders (`.noisemakerpreset`) in `/sdcard/vst/noisemaker/presets/`, one bank
  per folder, picked with BANK under PATCH.
- **Hush One**: TAL-BassLine-101 presets (`.bassline`, `.vstpreset`) in `/sdcard/vst/hush1/presets/`; they follow the
  11 built-in presets in PATCH.

Copy them with `scp -r` or an SFTP app (e.g. Cyberduck) using the same SSH login, then re-insert the plugin. Updating
a plugin never deletes files you added. What every plugin ships, where it came from, and what isn't reachable yet
(Tablor's factory presets) is in
[docs/presets-and-libraries.md](docs/presets-and-libraries.md).

## 5. Play it

On the MPC, add a new track of the **Plugin** type and choose the plugin in the plugin browser. The browser lists it
under its name and maker (e.g. **[SYN] Hera**, by jpcima): each name starts with its kind, **[SYN]**, **[DRUM]**,
**[SEQ]** or **[FX]**, so sorted by type (where MPC puts every VST in one VST folder) they group together. The
**[FX]** audio effects are in a track's insert effect slots instead, under VST. A plugin's touchscreen page appears
with it; the tabs along the bottom are its pages. Turn a control on screen or with the Q-Links (on a 4-knob MPC, the
Q-Link button steps through the page's columns of four; the active column is outlined). Settings are saved with the project, and automation
works like on Akai's own plugins. Plugins with presets have a preset or patch selector on their first page, and
their presets (kits for the drum machines) are also in MPC's own PRESET menu in the plugin header, which shows on the
arrangement screen too. For OB-Xd and Noisemaker it lists the bank that's loaded.

### Sequencers and other MIDI generators

Eucalypso, Grids, Groove Bank, Marbles, Maze Lite, MIDI Player, Pixel Walkers, Rampage, Super Arp make no sound of their own: they play other tracks. MPC ignores a plugin's MIDI output, so each one opens
its own MIDI port, the way a USB MIDI device would appear (MPC picks it up without a restart). MPC lists it as
**[SEQ] <name> MIDI Out**, e.g. **[SEQ] Super Arp MIDI Out**:

1. Put the sequencer on a plugin track. Grids, Marbles, Maze Lite and MIDI Player play as soon as the transport runs
   (a note into Maze Lite sets its key); the others turn the notes you play or hold on their track into patterns.
2. **Menu → Preferences → MIDI**: switch **Track** on for the sequencer's port.
3. On each track that should play, set its **MIDI Input Port** to that port (channel All) and its **Monitor** to
   **In**, not Auto: with Auto a track only listens while it's selected, and you'll be on the sequencer's track. Never
   on the sequencer's own track, or it hears itself.
4. Press play. They follow MPC's tempo and transport.

Rampage works the same way but sends control changes: MIDI-learn a parameter on the target track to its CC.
**[docs/sequencers.md](docs/sequencers.md)** has the whole walk-through, what makes each one play, and what to check
when nothing plays. On a Live II, MPC connects to the port by itself (checked 2026-10-04); the full route is confirmed
on a Force.

### Audio effects

Rings FX, Verglas, Warps are audio effects: insert them where MPC offers plugin effects. Whether MPC OS lists third-party VST effects in
its insert-effect browser is **not yet confirmed** on a device; please report what you see.

## Updating

Pull the latest version of this repo (`git pull`) and run the installer again with the plugins you want to update.
Files are replaced; MPC isn't restarted unless a plugin is new or its entry changed. Then, on the MPC, **remove the
plugin from its track and insert it again**: a fresh insert loads the new version and screen (save the patch you want
to keep first, or rely on the project's saved settings).

## Uninstalling

```
./uninstall.sh <mpc-address> hera            # or several, or a group
./uninstall.sh <mpc-address> hera --purge    # also delete its data folder (presets, kits, your own files in it)
```

It asks first, stops MPC, backs up `MPC.settings`, removes the plugins' entries, their `.so` files and screen folders,
and starts MPC. Remove the plugins from your projects first: a project that used one will open without it.

## Installing by hand

If you'd rather not run the script, these are the same steps (from your computer, then on the MPC):

```
# copy one plugin's files (example: Hera)
cd schwung/instruments/hera/deploy
scp vst/hera.so root@<mpc>:/sdcard/vst/
scp -r "Synths/jpcima - VST - [SYN] Hera" root@<mpc>:/sdcard/Synths/
scp -r ../../../../presets/hera root@<mpc>:/sdcard/vst/       # its presets, if it has some
                                                               # (python3 tools/fetch-presets.py hera first)
scp pluginlist-entry.xml ../../../../tools/plugin_list.awk root@<mpc>:/tmp/

# on the MPC (ssh root@<mpc>)
systemctl stop acvs                                            # stops MPC
cd /media/az01-internal/Settings/MPC
cp MPC.settings MPC.settings.bak                               # keep a backup
awk -v add=/tmp/pluginlist-entry.xml -f /tmp/plugin_list.awk MPC.settings > MPC.settings.new
grep -c 'file="/sdcard/vst/hera.so"' MPC.settings.new          # must print 1
mv MPC.settings.new MPC.settings
systemctl start acvs                                           # starts MPC
```

The plugin-list entry is one line, `deploy/pluginlist-entry.xml` in each plugin folder; it goes inside
`<VALUE name="pluginList-arm"><KNOWNPLUGINS>` in `MPC.settings`. Edit that file only while MPC is stopped: MPC writes
it on exit, and a damaged file makes MPC fall back to default settings.

## Troubleshooting

- **The plugin isn't in the browser.** MPC reads its plugin list only at start-up: run the installer again (it
  registers what's missing) or restart MPC. Check the entry: `ssh root@<mpc> grep -c hera.so /media/az01-internal/Settings/*/MPC.settings`.
- **The plugin shows plain sliders instead of its page.** The screen folder is matched by maker and name. Check
  `/sdcard/Synths/<maker> - VST - <name>/` exists (the installer prints it). After a fresh install MPC adds
  `/sdcard/Synths` to its content locations on the next start.
- **An update didn't change anything.** Remove the plugin from the track and insert it again.
- **MPC won't start or lost its settings after a manual edit.** Put the backup back, from SSH:
  `systemctl stop acvs; cp <backup> /media/az01-internal/Settings/MPC/MPC.settings; systemctl start acvs`. The
  installer's backups are named `MPC.settings.bak-vst_instruments-<date>`.
- **A plugin crashes MPC.** A plugin runs inside MPC, so a crash takes MPC down. Note what you did, uninstall that
  plugin, and open an issue.
- **`can't log in ... over SSH`.** Check the address and that `ssh root@<mpc-address>` works by itself (step 1).
- **A sequencer plays nothing.** Its port needs **Track** switched on in Preferences → MIDI, and the target track's
  MIDI Input Port set to that port with **Monitor: In** (not Auto); the transport must be running. More in
  [docs/sequencers.md](docs/sequencers.md).
