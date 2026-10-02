# How the screens were made, and how to change one

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
