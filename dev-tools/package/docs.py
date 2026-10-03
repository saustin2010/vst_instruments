"""The vst_instruments repo's top-level documents (README, INSTALL, BUILDING, RESKINNING, CREDITS) and its gallery
image, written by package.py from catalog.py and the facts it gathered from each port."""
import os, re, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
MV = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
FW = "https://github.com/sd88me/mpc-vst-plugins"
REPO_URL = "https://github.com/saustin2010/vst_instruments"


def write(out, cat, facts, base, group_titles, sections):
    ports = sorted(facts, key=lambda p: facts[p]["name"].lower())
    path = lambda p: "%s/%s" % (cat[p]["group"], p)
    count = lambda kinds: sum(1 for p in ports if cat[p]["kind"] in kinds)

    # gallery image: every plugin's first page
    os.makedirs(os.path.join(out, "docs", "images"), exist_ok=True)
    args = []
    for p in ports:
        args += [os.path.join(out, path(p), "screenshots", "page_0.png"), facts[p]["name"]]
    subprocess.run(["docker", "run", "--rm", "-v", "%s:%s" % (out, out), "-v", "%s:%s" % (MV, MV), "mpc-vst-html-art",
                    "python3", os.path.join(HERE, "collage.py"), os.path.join(out, "docs", "images", "gallery.png"), "6",
                    os.path.join(MV, "tools", "html_art", "fonts", "TitilliumWeb-SemiBold.ttf")] + args, check=True)

    # ---------------------------------------------------------------- README.md
    n_syn, n_drum = count(("Synth",)), count(("Drum synth", "Drum sampler"))
    n_seq, n_fx = count(("MIDI sequencer", "Arpeggiator", "Modulator")), count(("Audio effect",))
    L = ["# MPC VST Instruments", "",
         "**%d plugins for Akai MPC OS standalone devices**: %d synths, %d drum machines, %d MIDI sequencers and "
         "generators, and %d audio effects. They run inside MPC's own plugin host like Akai's instruments do (pads, keys, "
         "clips, Q-Links, automation, saved with the project), and every one has its own native touchscreen page, "
         "designed in Google Stitch and converted into MPC's skin format." % (len(ports), n_syn, n_drum, n_seq, n_fx), "",
         '<img src="docs/images/gallery.png" alt="The first page of every plugin, as the MPC draws it">', "",
         "## Thank you, sd88me", "",
         "None of this would exist without **[sd88me](https://github.com/sd88me)** and his "
         "**[mpc-vst-plugins](%s)**. He worked out that MPC OS's built-in plugin host loads Linux VST2 plugins, and "
         "built everything these ports stand on: the hand-written VST2 wrapper, the adapter that runs Ableton Move's "
         "Schwung modules unchanged, the generator that turns a layout into MPC's native touchscreen pages, Skin Studio "
         "and the offline test. These ports started as experiments on top of his framework, and he'll probably bring "
         "them into his own repo. Thank you!" % FW, "",
         "Thanks also to NoQuestion and dustyslices, who first described the route on MPC-Forums (\"Proof of Concept: "
         "Custom Standalone Plugins\", September 2026); to Charles Vestal and everyone writing Schwung modules for "
         "Ableton Move; to Emilie Gillet for open-sourcing the Mutable Instruments modules; to Befaco and the VCV Rack "
         "community; and to every author in [CREDITS.md](CREDITS.md), whose engines these are.", ""]
    L += ["## The plugins", "",
          "Click a picture for the plugin's page: what it is, every screen, how to play it, where it comes from.", ""]
    for title, kinds in sections:
        ps = [p for p in ports if cat[p]["kind"] in kinds]
        L += ["### %s (%d)" % (title, len(ps)), "", "| | Plugin | What it is |", "|---|---|---|"]
        for p in ps:
            f, c = facts[p], cat[p]
            L.append('| <a href="%s/"><img src="%s/screenshots/page_0.png" width="220" alt="%s"></a> | **[%s](%s/)**'
                     "<br><sub>%s · %s · %s</sub> | %s |" % (path(p), path(p), f["name"], f["name"], path(p),
                                                             c["kind"], f["maker"], f["lic"], c["tagline"]))
        L.append("")
    L += ["## Installing", "",
          "You need a **Gen1 MPC OS device** (32-bit ARM: e.g. MPC Live, Live II, One, X, Force) with **root SSH access**, "
          "and a Mac or Linux computer (Windows: WSL) on the same network. Then:", "",
          "```", "git clone %s.git" % REPO_URL, "cd vst_instruments",
          "./install.sh <mpc-address> --dry-run all      # check first: changes nothing",
          "./install.sh <mpc-address> hera grids verglas  # or a group: instruments, sequencers, effects, all", "```", "",
          "The first install of a plugin restarts MPC once (it asks first; `MPC.settings` is backed up). "
          "**[INSTALL.md](INSTALL.md)** walks through every step: getting SSH access, what the installer changes, "
          "adding a plugin to a track, routing the sequencers, updating, uninstalling, installing by hand and "
          "troubleshooting. Presets, kits and wavetables are fetched from their original projects into `presets/` and "
          "installed with each plugin; [docs/presets-and-libraries.md](docs/presets-and-libraries.md) lists them, "
          "where they come from and how to add your own.", ""]
    L += ["## Status (%s)" % TODAY(), "",
          "- All %d build and pass the offline test: an x86 build under AddressSanitizer/UBSan that checks every "
          "parameter, presets, saving and restoring, Q-Link behaviour, notes to audio, and a stress test that hammers "
          "the plugin from two threads as MPC does." % len(ports),
          "- All %d were installed on an MPC Live II (MPC OS 3.9.1) on 2026-10-02 with their previous screens. Moog's "
          "Stitch screen was checked on that device." % len(ports),
          "- The Stitch screens of the other plugins are new and checked offline only: every control's binding, the "
          "Q-Link layout and the touch areas (`dev-tools/stitch/check_skin.py`).",
          "- Not yet tried on a device: the sequencers driving other tracks through their own MIDI port on a stock MPC "
          "(the mechanism works on a Force), the three audio effects (whether MPC lists third-party effects at all), and "
          "the Sequencer category Grids reports to MPC's plugin browser.",
          "- Developed on a Live II. Other Gen1 devices run the same MPC software and should behave the same; Gen2 "
          "devices (e.g. Live III) are reported to be more locked down. Reports welcome.", "",
          "What's next is in [ROADMAP.md](ROADMAP.md). Found a problem? Open an issue with the plugin, your MPC model "
          "and firmware, and what you did.", ""]
    L += ["## What's in this repo", "", "```",
          "schwung/instruments/   %2d instruments ported from Ableton Move \"Schwung\" modules" % sum(1 for p in ports if cat[p]["group"] == "schwung/instruments"),
          "schwung/sequencers/    %2d MIDI sequencers from Schwung MIDI FX modules" % sum(1 for p in ports if cat[p]["group"] == "schwung/sequencers"),
          "schwung/effects/       %2d audio effect from a Schwung audio FX module" % sum(1 for p in ports if cat[p]["group"] == "schwung/effects"),
          "mutable-instruments/   %2d ported from Mutable Instruments' own firmware source" % sum(1 for p in ports if cat[p]["group"] == "mutable-instruments"),
          "vcv-rack/              %2d ported from a VCV Rack module" % sum(1 for p in ports if cat[p]["group"] == "vcv-rack"),
          "  <plugin>/            README.md, screenshots/, deploy/ (ready to install), source, screen design",
          "presets/               the plugins' presets, kits, wavetables (not in git: tools/fetch-presets.py)",
          "install.sh, uninstall.sh, tools/   the installer (and tools/build.sh to build from source)",
          "framework/             our changes to sd88me's mpc-vst-plugins, as a patch, and setup.sh",
          "dev-tools/             the tools the ports and screens were made with",
          "docs/                  extra guides (presets and libraries, root SSH on a stock MPC) and images",
          "licenses/              licence texts the plugins refer to", "```", "",
          "Each plugin's `deploy/` folder is the finished build, so installing needs only this repo; the presets come "
          "from their original projects (the installer fetches them). To build from source see "
          "[BUILDING.md](BUILDING.md); to change a screen, [RESKINNING.md](RESKINNING.md).", ""]
    L += ["## Licences", "",
          "Each plugin keeps its upstream licence (GPL-2.0, GPL-3.0, MIT or BSD-3-Clause; two state none): see the "
          "plugin's README and [CREDITS.md](CREDITS.md). The GPL plugins' complete source is in their folders. The "
          "scripts and documents written for this repo don't have a licence of their own yet.", "",
          "## Disclaimer", "",
          "Not affiliated with or endorsed by Akai Professional / inMusic, or by the makers of the instruments these "
          "plugins emulate or are named after (Roland, Korg, Oberheim, Moog, Elektron, Wurlitzer, Teenage Engineering, "
          "Mutable Instruments, Befaco and others); their names are used only to say what a plugin is modelled on. VST "
          "is a trademark of Steinberg; these plugins use a hand-written VST2 interface, not Steinberg's SDK. No Akai "
          "content is included. Installing edits MPC's settings file: the installer backs it up first, and you use all "
          "of this at your own risk, on hardware you own.", ""]
    open(os.path.join(out, "README.md"), "w").write("\n".join(L))

    # ---------------------------------------------------------------- INSTALL.md
    groups = []
    for g in ("instruments", "sequencers", "effects"):
        groups.append(g)
    du = lambda d: int(subprocess.run(["du", "-sk", d], capture_output=True, text=True).stdout.split()[0]) \
        if os.path.isdir(d) else 0
    sizes = {p: du(os.path.join(out, path(p), "deploy")) + du(os.path.join(out, "presets", p)) for p in ports}
    big = [facts[p]["name"] for p in sorted(ports, key=lambda p: -sizes[p])[:4]]
    open(os.path.join(out, "INSTALL.md"), "w").write(INSTALL.replace("@N@", str(len(ports))).replace(
        "@SIZE@", str(int(round(sum(sizes.values()) / 1024.0 / 10) * 10) or 10)).replace("@BIG@", ", ".join(big)).replace("@PLUGINS@", ", ".join(
        "`%s`" % p for p in ports)).replace("@SEQS@", ", ".join(
        facts[p]["name"] for p in ports if cat[p]["kind"] in ("MIDI sequencer", "Arpeggiator", "Modulator"))).replace(
        "@FX@", ", ".join(facts[p]["name"] for p in ports if cat[p]["kind"] == "Audio effect")).replace(
        "@DATA@", "\n".join("     | %s | `/sdcard/vst/%s/` |" % (facts[p]["name"], facts[p]["data"]) for p in ports
                            if os.path.isdir(os.path.join(out, "presets", p)))))

    # ---------------------------------------------------------------- BUILDING.md / RESKINNING.md
    open(os.path.join(out, "BUILDING.md"), "w").write(BUILDING.replace("@BASE@", base[:7]).replace("@FW@", FW))
    open(os.path.join(out, "RESKINNING.md"), "w").write(RESKINNING)

    # ---------------------------------------------------------------- CREDITS.md
    C = ["# Credits", "",
         "These plugins are other people's instruments, ported to MPC OS. Thank you to all of them.", "",
         "## The framework", "",
         "- **sd88me**: [mpc-vst-plugins](%s), the VST2 wrapper, Schwung adapter, skin generator, Skin Studio and "
         "offline test every plugin here is built with. These ports exist because of it." % FW,
         "- **NoQuestion** and **dustyslices**: first described running custom plugins in MPC's standalone host "
         "(MPC-Forums, \"Proof of Concept: Custom Standalone Plugins\", September 2026).",
         "- **Charles Vestal** and the Schwung / Move Everything community: the Schwung host for Ableton Move, and many "
         "of the modules ported here.",
         "- Google Stitch: the screen designs were generated with it, then converted and finished by hand.", "",
         "## The plugins", "", "| Plugin | Original work | Source ported | Licence |", "|---|---|---|---|"]
    for p in ports:
        f, c = facts[p], cat[p]
        mod = f["mod"]
        src = f["up"][0].strip() if f["up"] else (c.get("origin") or "")
        src = src if f["up"] else "Schwung module \"%s\" v%s" % (mod.get("name", p), mod.get("version", "?")) if mod else src
        who = mod.get("author") if mod and not f["up"] else f["maker"]
        lic_file = os.path.join(f["d"], "LICENSE")
        if f["up"] and os.path.exists(lic_file):   # the copyright holder named in the upstream licence, if any
            m = re.search(r"Copyright (?:\(c\) )?[\d, -]+(?:,\s*)?([^\n.]+)", open(lic_file, errors="replace").read())
            if m and "Free Software Foundation" not in m.group(1):
                who = m.group(1).strip()
        C.append("| [%s](%s/) | %s | %s | %s |" % (f["name"], path(p), who, src, f["lic"]))
    C += ["", "Each plugin's README has the details: the exact upstream commit or module version, its licence files, "
          "and every change made for the MPC (also as `upstream-changes.diff` where upstream files were patched). Helm's "
          "factory patches are CC BY 4.0 (Matt Tytel and contributors). Fonts in the screens: Titillium Web (SIL OFL), "
          "as MPC uses.", ""]
    open(os.path.join(out, "CREDITS.md"), "w").write("\n".join(C))


def TODAY():
    import datetime
    return datetime.date.today().isoformat()


INSTALL = """# Installing

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
- **Space**: all @N@ plugins take about @SIZE@ MB on the MPC's internal drive (`/sdcard`). The biggest are the ones
  that ship presets, wavetables or samples (@BIG@).
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

Plugin names: @PLUGINS@.

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
@DATA@

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

Copy them with `scp -r` or an SFTP app (e.g. Cyberduck) using the same SSH login, then re-insert the plugin. Updating
a plugin never deletes files you added. What every plugin ships, where it came from, and what isn't reachable yet
(Tablor's factory presets, extra OB-Xd banks, Noisemaker and Hush One preset imports) is in
[docs/presets-and-libraries.md](docs/presets-and-libraries.md).

## 5. Play it

On the MPC, add a new track of the **Plugin** type and choose the plugin in the plugin browser. The browser lists it
under its name and maker (e.g. **Hera**, by jpcima). Its touchscreen page appears with the track; the tabs along the
bottom are its pages. Turn a control on screen or with the Q-Links (on a 4-knob MPC, the Q-Link button steps through
the page's columns of four; the active column is outlined). Settings are saved with the project, and automation
works like on Akai's own plugins. Plugins with presets have a preset or patch selector on their first page; Moog's
presets also appear in MPC's own PRESET menu.

### Sequencers and other MIDI generators

@SEQS@ make no sound of their own: they play other tracks. MPC ignores a plugin's MIDI output, so each one opens its
own MIDI port, the way a USB MIDI device would appear (MPC picks it up without a restart):

1. Put the sequencer on a plugin track. Grids, Marbles, Maze Lite and MIDI Player play as soon as the transport runs
   (a note into Maze Lite sets its key); the others turn the notes you play or hold on their track into patterns.
2. **Menu → Preferences → MIDI**: switch **Track** on for the port named after the plugin.
3. On each track that should play, set its **MIDI input** to that port (not *All*, and not on the sequencer's own
   track, or it hears itself).
4. Press play. They follow MPC's tempo and transport.

Rampage works the same way but sends control changes: MIDI-learn a parameter on the target track to its CC. Each
plugin's README has the details. This routing is confirmed on a Force; on a stock MPC it's still to be tried.

### Audio effects

@FX@ are audio effects: insert them where MPC offers plugin effects. Whether MPC OS lists third-party VST effects in
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
scp -r "Synths/jpcima - VST - Hera" root@<mpc>:/sdcard/Synths/
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
  MIDI input set to that port; the transport must be running.
"""


BUILDING = """# Building from source

Every plugin folder already contains its finished build (`deploy/`); you only need this to change a plugin or check
the build. The plugins are 32-bit ARM Linux libraries, cross-built in Docker, then tested on your computer under
AddressSanitizer before they go anywhere near an MPC.

## What you need

- macOS or Linux, `git`, Python 3, bash 4 or later (macOS: `brew install bash`).
- **Docker** with 32-bit ARM emulation. On macOS, [Colima](https://github.com/abiosoft/colima) works:
  ```
  brew install colima docker && colima start
  docker run --privileged --rm tonistiigi/binfmt --install arm     # again after every Colima restart
  ```
  You don't have to remember either step: `tools/build.sh` (via `tools/docker-ready.sh`) starts Colima if Docker isn't
  running and registers the ARM emulation when it's missing. Check by hand with `bash tools/docker-ready.sh`.
  The build pulls `arm32v7/gcc:12`, `gcc:12` and builds an image with headless Chromium for the artwork
  (`mpc-vst-html-art`) the first time. Installing plugins needs none of this, only SSH.

## 1. The framework

The wrapper, the Schwung adapter, the skin generator and the test are sd88me's [mpc-vst-plugins](@FW@). This repo
carries only our changes to it, as a patch against commit `@BASE@` (see [framework/README.md](framework/README.md)):

```
framework/setup.sh      # clones it into framework/mpc-vst-plugins/ and applies framework/mpc-vst-plugins.patch
```

(Already have a checkout with the patch applied? Point to it with `export MPC_VST_FRAMEWORK=<path>`.)

## 2. Build a plugin

```
tools/build.sh hera              # or several: tools/build.sh hera grids rampage
```

For each plugin it:

1. builds the `.so` for the MPC's ARM CPU and its screen (`TUI.json`, `Q-Links.json`, artwork) from `vst.json`,
   `params.json`, `layout.conf` and `images/` (log: `build/build.log`),
2. refreshes `deploy/` with the new `.so`, screen and plugin-list entry (the plugin's presets stay in
   `presets/<plugin>/`; fetched first if missing, so the test runs with them),
3. runs the **offline test** (log: `build/test.log`): an x86 build of the same plugin under AddressSanitizer/UBSan
   that loads it like MPC does and checks two instances, every parameter's name and display, set/get round trips,
   options and pop-ups, notes to audio (or audio through, for effects), saving and restoring, slow Q-Link turns,
   and VST programs. It prints `PASSED` or `TEST FAILED`.

Then install the result as usual (`./install.sh <mpc> hera`). `build/` is ignored by git.

## Where things are

| In a plugin folder | What |
|---|---|
| `vst.json` | name, maker, plugin ID, the `.so` name, source files and compiler flags, preset folder (`MODULE_DIR`), category |
| `params.json` | the parameters MPC sees, in VST index order: ranges, options, names, steppers |
| `layout.conf` | the screen: pages, every control's position and kind, artwork, Q-Link order |
| `src/` | the engine as published upstream (plus our patches: `upstream-changes.diff`) |
| `mpc/` | glue for engines that aren't plain Schwung sound modules: MIDI FX / audio FX adapters, engine wrappers for Mutable's and Rack's code, headers |

Never change the order of `params.json` entries in a plugin people already use: MPC saves projects and Q-Link
assignments by parameter index. Add new parameters at the end.

## More checks

`dev-tools/fuzz/` stress-tests a plugin the way a user hammers it (two threads, state save/restore, re-inserts) and
`dev-tools/probe/` pokes an engine to learn its data folders and units; see [dev-tools/README.md](dev-tools/README.md).
The framework's `tools/bench.sh` measures CPU on the device.
"""


RESKINNING = """# How the screens were made, and how to change one

MPC draws a plugin's page itself from a skin folder (`/sdcard/Synths/<maker> - VST - <name>/`): a `TUI.json` that
places knobs, sliders, buttons, switches, labels and pictures and binds each to a parameter, a `Q-Links.json`, and
PNG artwork. The framework generates that folder from a plugin's `layout.conf`. The screens in this repo started as
**Google Stitch** designs (one HTML page per plugin, kept as Stitch wrote it in each plugin's `design/stitch.html`),
turned into `layout.conf` by `dev-tools/stitch/convert.py`. The raw designs are mockups of the whole MPC screen and
mention the hardware in decorative text; the conversion keeps only the plugin's area and replaces maker badges
(e.g. a "Roland" logo on 303) with neutral text:

1. **Render and measure.** The design is opened in headless Chromium; every knob, slider, switch and pop-up is found
   by its CSS selector, measured and matched to a parameter (by `data-` attributes, ids or labels). The page itself,
   with the controls hidden, becomes the background; knob artwork is rendered at 64 angles into filmstrips.
2. **Fill the gaps.** Controls the design left out are added in the design's own style, pages it didn't draw are
   generated from the plugin's page plan (`layout.grid.conf`), and design labels become the parameter names MPC
   shows. Per-plugin settings live in `MAPS` in `convert.py`.
3. **Displays.** Waveform pictures come from the engine's real output, envelope displays follow their knobs
   (`waveforms.py`, `envelope.py`).
4. **Q-Links** follow the design's reading order, four per column.
5. **Check.** `check_skin.py` reads the built skin and reports any control bound to the wrong parameter, Q-Links
   off their page or out of order, touch areas overlapping (MPC would give the touch to the wrong control) and option
   lists that don't match. `showcase.py` renders the screenshots in each README, with the engine's real values.

## Changing a screen

- **Small changes** (move a knob, rename a control, change Q-Link order): edit `layout.conf` (or `params.json` for a
  name) and rebuild with `tools/build.sh <plugin>`. Skin Studio, the framework's browser editor, opens a plugin's
  `vst.json` and previews the pages:
  `python3 framework/mpc-vst-plugins/tools/studio.py serve <plugin folder>/vst.json --open`.
- **A new design**: make the page in Stitch (1280 x 628, one tab per page), save its HTML as `design/stitch.html`, set
  up the workspace (`dev-tools/workspace.sh`, see [dev-tools/README.md](dev-tools/README.md)) and run
  `python3 steve/tools/stitch/convert.py <plugin>` there, adding a `MAPS` entry for the design's selectors.

## What MPC can and can't draw

Learned while making these (details in the framework's `docs/NOTES.md`):

- Controls are knobs, sliders, buttons and switch groups bound to parameters; names and values are live text in
  MPC's own font (Titillium Web). MPC shows a control's **parameter name**, so names live in `params.json`.
- There are no drop-down menus for plugins: option lists are drawn as segment switches or the skin's own pop-up.
- Pictures can't be drawn live, but a filmstrip can follow a parameter, which is how the envelope and waveform
  displays work. Animation costs MPC's screen thread too much, so displays stay still between changes.
- Each knob's touch area is about 130 px wide and carries its name and value; where controls sit closer, the
  converter narrows the touch area (`bw=`) so MPC never gives a touch to the neighbour.
"""
