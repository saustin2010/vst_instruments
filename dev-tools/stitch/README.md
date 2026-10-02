# stitch: the Stitch layout mockups (steve/stitch_layouts) and waveform pictures

`steve/stitch_layouts/*.md` are the user's Stitch designs (1280-wide plugin screens with tabs, knobs, scopes), one per
port, pulled with `pull.py`. These tools turn them into the ports' MPC skins: every port with a design was converted
2026-10-02/03 (`convert.py` `MAPS` has one entry per design; chiptune has no design).

| File | What |
|---|---|
| `pull.py <list_screens.json> [--force] [port ...]` | **your Stitch designs -> `stitch_layouts/<port>.md`** (+ `stitch_png/<port>.png`), named by the port the screen title starts with (`ALIAS` for Raffo -> moog, Hush One -> hush1, Mono Voice & Helm -> monovoice); a second design for a port is `<port>.v2.md`, a title ending "page N" is `<port>.pN.md`. Keeps an existing `<port>.md` (may be hand-edited) and puts the new copy in `incoming/` unless `--force`. The JSON is the Stitch MCP's `list_screens` result (Claude saves it; its download links last ~1 h). `index.json` records port -> screen |
| `survey.py <port> [--no-shots]` | what a design's conversion needs, in one go: the port's parameters, its current pages, an outline of the design's markup, and every tab rendered to `stitch_layouts/build/look/<port>_tabs.png` |
| `shoot.py <dir>` | screenshots every tab of every mockup in `<dir>` into `<dir>/shots/` (runs in the `mpc-vst-html-art` container: `docker run --rm -v "$PWD":"$PWD" -w "$PWD" mpc-vst-html-art python3 steve/tools/stitch/shoot.py "$PWD/steve/stitch_layouts"`) |
| `waveforms.py <port> <param> [--adapter schwung] [--w --h] [--note] [--periods] [--set k=v]` | plays each option of `<param>` through the port's real engine (`steve/tools/probe`, new `dump:`/`off:` commands) and draws two periods (`--periods 4` for chords) of its output as `images/waves/<param>_<i>.svg` in the port's theme colours |
| `sheet.py <port> <param> <out.png>` | contact sheet of those pictures, to check them (container, like `shoot.py`) |
| `convert.py <port> ...` | **a mockup -> the port's layout.conf** (2026-10-02; done for 303, Aphex, Braids, Moog, Noisemaker). Runs `extract.py` in the container, matches controls to parameters, writes the layout; keeps the previous one as `layout.grid.conf`. Then `steve/tools/skin-redesign/build.sh <port>` |
| `extract.py <config> <out>` | (container) per tab: the background with live parts hidden, every control's position and parameter hints, and each distinct knob look rendered from the mockup's own knob as a 64-frame filmstrip |
| `envelope.py <spec>` | (container) envelope displays: straight A/D/S/R segments with corner dots, one filmstrip column each; D, S and R once per sustain band (11 bands, `when=<sustain>:<i>/11`) so they always meet |
| `envelope_layout.py <port>...` | redraws a port's envelope strips and rewrites only its envelope lines in layout.conf (old file: `layout.conf.bak-env`) |
| `meter_sim.py <skin dir> <prefix> key=v ...` | (container) draws each skin page with its meters at given values, as MPC picks frames and bands: checks envelopes offline |

A spec shows them with a picture item, e.g. Braids (`specs/braids.py`):
`{"key": "engine", "kind": "picture", "w": 580, "h": 108, "dy": 164, "files": [...]}` -> layout
`picture x= y= w= h= key=engine files="..."`, which MPC switches with the option (IndexedEnabling, verified on a
device 2026-09-24). The picture shows the waveform at the knobs' defaults; it follows the selector, not TIMBRE/COLOR.

How a converted skin works: the background image is the mockup itself (panels, titles, switch and pop-up captions,
knob scales) with its moving parts removed and the content centred in MPC's 1280x628; knobs are filmstrips of the
mockup's knobs; MPC draws knob/slider names and values live (smaller where the mockup packs controls closer: `ns=`/
`vs=`); switches, toggles, sliders, steppers, pop-ups and trigger buttons are ours, at the mockup's spots; canvases
become real waveform pictures (Braids algorithm, 303 waveform, Moog/Noisemaker osc 1 wave) and envelope displays that
follow their ADSR controls (Braids, Moog, Noisemaker amp + filter, Aphex EG2). Per-mockup selectors and label fixes are
`MAPS` in convert.py. The mockups' Q-Link chips and audition buttons are dropped (MPC has its own; they can't work).
Not checked on a device yet: whether MPC redraws a display filmstrip when its parameter changes from another control
(a Q-Link or the slider next to it); band switching on a continuous parameter was verified for labels.

What the mockups have that an MPC skin can't: their top/bottom bars (MPC draws its own tabs and Q-Link info), the
animated scopes (no drawing or animation on MPC; their waveforms are decorative sine sums anyway), and a live audio
scope (the wrapper can't push engine-driven values to the screen yet: docs/NOTES.md 2026-09-25). 

## Envelope displays, second version (2026-10-02)
The first version (exponential curves with a filled area, 16-frame strips, one S strip) drew wrongly on a Live II:
pieces sat above or below the display box, the levels didn't meet, and the fill showed as grey blocks (photo from the
device). The skin builder had resampled each strip to the knobs' 128 square frames (141 x 18048 px); knob strips
(<= 12288 px) draw right, so the builder now keeps a meter's own frame count (`tools/shadow_skin.py`). The envelopes
are now straight segments (the user's ask: lines the knobs bend), no fill, D/S/R switched together per sustain band.
Applied to Moog, Noisemaker, Braids and Aphex with `envelope_layout.py`.

That alone still drew pieces off target: the real cause was the strip layout. Stock skins set `numFrames` to the
frame count and use frames of any shape; the builder had square-padded frames with `numFrames` = count - 1.
`tools/shadow_skin.py` now lays meters out as stock does, and Moog drew correctly on the Live II (user: "heaps
better"; screen grab `steve/screens/moog_v3_device.png`). `meter_sim.py` models that layout. Details: docs/NOTES.md.

## Designs that leave controls out: `augment` (2026-10-02, Chordism first)
Most Stitch designs draw only some of a port's controls (Chordism: ~80 of 135) and fill the rest of a panel with
made-up status text. A design's `MAPS` entry can add the missing ones **in the design's own markup** before anything is
extracted, so they get its knob filmstrip, label font and panel boxes:

- `"tpl"`: the design's markup for one `knob`, `enum` (+ `opt_enum`), `select` (+ `opt_select`), `toggle`, a grid `row`
  and a sub-panel `box`, with `{key}`, `{label}`, `{opts}`, `{n}`, `{items}`, `{title}`. Copy them from the design.
- `"augment"`: a list of `{sel, nth: [keys]}` (tag a design's untagged switches/pop-ups in page order), `{sel, attr}`,
  `{sel, html, where}` (`beforeend`, `replace`, ...) and `{sel, remove: true}`. In `html`, `@knob(k)`, `@enum(k)`,
  `@select(k)`, `@toggle(k)`, `@ctl(k)` (picks: OFF/ON -> toggle, <= 4 options -> switch, more -> pop-up, else knob),
  `@row(k1, k2, ...)` and `<box>TITLE ... </div>` expand from `tpl`.
- More ops: `{sel, each: [html, ...]}` sets each match's contents in order (Denis's matrix cells -> knobs),
  `{sel, text}`, `{sel, unclass: regex}` (drop Tailwind `rotate-45` classes baked into a knob cap),
  `{sel, clone: [{replace: [[a, b]]}]}` (copies of a page with keys swapped: Eucalypso's LANE 2-4 from LANE 1).
- **Names: MPC writes a knob's / slider's / toggle's name from the PARAMETER's name** (`shadow_skin._name_label`), not
  the layout's label. So better names are renames in `params.json` (the original list is kept once as
  `params.pre-stitch.json`; every run starts from it): `"names": {key: NAME}`, and with `"design_labels": True` the
  design's own label (3-12 characters, no `[Q1]` hints or trailing colons, and only while every name stays unique).
  `"labels"` only sets text in added markup (a switch's caption).
- Q-Links follow page order (a row of four in a panel = one Q-Link column); `"qlinks": {tab: [keys]}` overrides.
- The screen root is found by size (1280 x 628, up to 24 px wider: some designs are 1300) and forced to 1280 wide.

Then `convert.py <port>`, `waveforms.py` for any picture it references, `skin-redesign/build.sh <port>`.

## Pages a design didn't draw: `grid` (2026-10-02)
Most designs draw one page of several. `"grid": {"tabs": [...], "tpl": ...}` draws the named pages of the port's own
plan (`layout.grid.conf`, the generated layout the design replaced: its frames, their titles and every control's place)
in the design's markup: templates `frame`, `art` (optional plate where the plan had a logo), `knob`, `enum` (+ `opt_enum`),
`popup`, `stepper`, `toggle`, `button` (a kind without one becomes a knob; `tpl: None` = the entry's `tpl`).
extract.py shows each such page in place of the design's content and reads it like a drawn page; `"tab_order"` puts
drawn and generated pages in order, `"skip"` leaves out keys already on a drawn page. Used for Helm (11 pages in the
Mono Voice design's style), Mono Voice, MonkSynth (CHOIR), Mr Drums (PAD), Mr Hyde, NuSaw, OB-Xd, Plaits, Rampage,
Super Arp, Tablor, Verglas, Hush One, Marbles.

## Other per-design options (convert.py MAPS)
`tabs` (selector; `#no-tabs` = one page named `page`), `drop` (header/footer to remove; `autodrop: False` when a panel
mentions "Q-LINK"), `center`, `rot` (the element to turn for knobs whose pointer isn't turned inline), `twice` (a key
placed on two pages, e.g. an edit-pad stepper), `qlinks_skip` (touch-only controls kept off the Q-Links when a page has
more than 16), `option_labels` (switch labels; otherwise the design's own when they abbreviate ours in order, e.g.
EXT/TRI), `nowrap` and `nudge` ({key: [dx, dy]}) for switches wider than the design's.
Generic rules in extract.py: tabs are read without icon ligatures, a grid frame becomes a column once its header is
dropped, tab glows are hidden. Switches taller than the design's keep the design's top edge; a switch too wide wraps to
two rows. `envelope.py` also draws ADS (three-stage) displays (Hank).

## Per-port notes (2026-10-02/03)
- Removed because they have no parameter (they'd be dead on the MPC): pad/HIT/audition buttons, Mr Drums'
  PUNCH/CRISP/CLIP/ANALOG SAT, MIDI Player's transpose/velocity sliders and transport buttons, Denis's page-1 RND MOD
  (it's on page 2), Libpo32's PITCH DROP/CUTOFF/SATURATION, frozen readouts that pretend to be live (X/Y, PPQN, etc.).
- Live-looking decorations without a parameter (MIDI monitors, spectra, LCD stats) stay as the design's still art.
- Engine-rendered pictures: Chordism (chord), Elements (model), Fizzik (model A), Hank (FM ratio), Mr Hyde (model).
  Rings' model picture was dropped: 3 of 7 models record silence in the probe.
- Envelope displays that follow the knobs: Denis, Hank (ADS), Hera, MonkSynth.
- Not on screen (as before): Denis PAD MODE, Helm steps 17-32, Libpo32's per-pad detail params (edited via EDIT PAD),
  internal readouts.

## Checking a built skin, touch boxes, screenshots (2026-10-03)
- `check_skin.py <port>|all [-v]` reads what MPC will load (`deploy/.../TUI.json`, `Q-Links.json`) against
  `build/params.h`, `layout.conf` and `params.json`: BIND (a layout control no widget is bound to, or a wrong number),
  QLINK (off its page, out of the layout's order, > 16), TOUCH (two controls' touch boxes overlapping by 12 px or more:
  MPC would give the touch to one of them), EDGE, OPTS (option labels vs the parameter's). Exit 1 on any of those.
- Touch boxes: a knob's box is 130 px wide (it carries the name and value), a vertical slider's as wide as it is
  long. `convert.py` fits them (`fit_boxes`): where neighbours sit closer it writes `bw=<width>` (shadow_skin.py honours
  it on knobs and vertical sliders), splitting the gap; switches, pop-ups, toggles and buttons keep their size, with
  their real option count from params.json. `convert.py --fit <port>` re-fits an existing layout.conf. Toggles have a
  fixed 120 px box: two closer than that get a `nudge` (OB-Xd's BEND pair); so do Aphex's 2x2 MAIN knobs (6 px each way).
  2026-10-03 result: all 36 OK (no BIND/QLINK/TOUCH/EDGE/OPTS; a few overlaps under 12 px at box edges).
- `dump_state.sh <port>...` links `dump_state.c` with the offline test's objects (`build/host_*.o`) and writes
  `build/state.json`: every parameter's value and display text right after an insert, from the real engine.
  `showcase.py <port> <dir>` (in the mpc-vst-html-art image) renders each page as MPC draws it at that state (knob
  frames, lit switches, `when=` visibility, names/values in Titillium Web, no debug outlines): the screenshots of the
  vst_instruments repo (`steve/tools/package/`).
