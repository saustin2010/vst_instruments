# The sample browser: how it works, and how to put it in another plugin

Incipience's SAMPLES tab ([originals/incipience](../originals/incipience/), 1.1.0) lets you pick WAV files from any
drive on the MPC, hear them and load them into an instrument, on the MPC's own screen, with its data wheel and Q-Links.
MPC gives a plugin no file dialog and no file access of its own, so the plugin lists the drives itself, shows the
list through its own parameters and reads the files on a thread of its own. This page is the design, the MPC facts it
rests on (what's been checked on a device and what hasn't), and the steps to reuse it.

Owner's asks that shaped it: "can we browse samples on the MPC or point to folders?" (2026-10-18, first version);
"a bit clunky" (2026-10-19: a cursor that plays each file, LOAD TO as buttons, shorter names, AUDITION); "can we just
use the wheel to scroll through folders and sounds?", "the main wheel does not scroll through the files", "make the
font white and not red", "the qlinks change really really quickly and skip a few rows" (2026-10-20: one list across
subfolders, the wheel on FILE's box, AUTO LOAD, white text, a visible cursor, Q-Links a step per three events).

## What you see

```
 BROWSE                                         DRIVES · FOLDERS · WAV FILES  |  LOAD TO        PICK, THEN LOAD
 [ FOLDER: SSD/Samples/Drums/Kicks         ] [ 64 WAVS IN 5 FOLDERS ]          |  [CL HAT][OP HAT][CLAP][RIM]
 | .. UP                                                              |        |  [CRASH ][RIDE  ][ X1 ][ X2]
 | Kick Hard 01                                                       |        |
 |#Kick Hard 02                       (the cursor: a red border)     #|        |  AS YOU TURN
 | > Kick Hard 03                     (> : what LOAD TO's one plays)  |        |  PREVIEW  AUTO LOAD  AUDITION
 | Kicks/Room /                       (a subfolder's heading)         |        |
 | Room 01                                                            |
 | ...                                (8 rows)                        |
 [< ROWS 9-16 >] [< Kick Hard 02             >] [LOAD] [UP] [DRIVES]
   SCROLL          FILE: TAP, THEN THE WHEEL
```

- **The list:** 8 rows of white text. Opening a folder lists its own WAV files, then every subfolder's (depth first, in
  name order), each under a heading with its path from the folder you opened (`Kicks/Room /`). Folders without a WAV
  file aren't listed. If a folder holds too much for one list (more than 999 rows, or more than 256 folders to look
  through, as a drive's top level often does), it's listed one level at a time instead: its subfolders as rows, then
  its own files.
- **The cursor (FILE):** a red border on its row; the FILE box shows its name. Tap the FILE box once and the **data
  wheel** moves it a file a click, stepping over headings, from one folder's last file into the next one's first. The
  rows take no hardware focus, so tapping them afterwards (to hear or load) leaves the wheel on FILE. **Q-Link 1**
  (BROWSE, a parameter of its own) moves it with no tap, a file per three Q-Link events; the FILE box's arrows step
  one. **SCROLL** moves a page: an arrow tap at once, Q-Link 2 per three events; the cursor is kept in view.
- **PREVIEW** (on to start with): the file the cursor lands on plays straight from disk (mono, -3 dB, under the
  plugin's VOLUME, no instrument or effects), until it ends or the cursor moves.
- **AUTO LOAD** (off to start with, Q-Link 6): the file the cursor lands on loads into LOAD TO's instrument and plays
  through it, so the wheel alone browses and loads.
- **Taps:** a file row puts the cursor there and plays it; a second tap, or **LOAD**, loads it into LOAD TO's
  instrument (which then plays it once). A heading opens that folder on its own; a folder row, a drive or `.. UP`
  opens. **UP** goes to the folder above, the cursor on the first file of the folder you left. **DRIVES** lists the
  places: SAMPLE SET BY NUMBER (the plugin's built-in set), the plugin's own `samples/` folder if there is one, then
  the drives under `/media` (MPC's internal drive as INTERNAL; `az01-internal-sd` and `acvs*` are left out).
- **LOAD TO:** eight buttons, one per sample instrument. Picking one puts the cursor on the file it plays, and if that
  folder isn't in the list, the list goes there first.
- **AUDITION** (off to start with): turning an instrument's SAMPLE on its own page plays each sample once as you dial.
- **Readouts:** FOLDER shows the folder of the file under the cursor (its end, if long; MPC shows 24 characters). The
  box beside it says how much is listed (`64 WAVS IN 5 FOLDERS`, `40 FOLDERS + 12 WAVS`, `PICK A DRIVE`).
- **Names:** `.wav` dropped; a name longer than 24 characters shows its start and end with `..` between, so takes that
  differ at the end stay apart (`A long kick..ame take 04`).
- **After a load:** the instrument's SAMPLE steps through that folder's files in name order (`Kick 2` before
  `Kick 10`; the first 255) and shows the file's name; RANDOMISE and modulation stay within the folder. The project
  saves the folder and the file's **name** (`x1_dir=...;x1_file=...;`) and the folder being browsed (`br_path`), so
  files added to the folder later don't change what plays.

## The MPC facts it rests on

| Fact | Checked |
|---|---|
| A plugin only has parameters: no file dialog, no keyboard, no way to read the touch position. The browser is parameters whose **display text** the engine writes (`dynamic_display`), re-read by MPC when the wrapper marks them changed. | device, all plugins |
| MPC shows a control's **parameter name**, not its layout label, and about **24 characters** of text in a box. | device |
| The **data wheel** turns the control that has MPC's hardware focus (owner, 2026-10-19: "works as long as a box is selected"). A control takes it on a touch when its skin has `Mouse Down → Q-Link` and `acceptsHWFocus: Yes`; the wheel then moves its `Data` parameter. A stepper box (SAMPLE, FILE) is one. | device |
| A touch action only ever acts on the control's **own** `Data` parameter. Akai's own skins on the Live II (`/usr/share/Akai/Content/Synths/*/Plugin Skins/`, 248 skin files) use four actions, all on `Data` (`Mouse Down → Q-Link`, `Enter Pressed → Toggle Switch`, `Double Click` / `Enter Pressed → Show Overlay`); named handles are only an indirection (a parent maps names to parameters, a child's `Data` points at a name). A row whose `Mouse Down → Q-Link` named another parameter (`wheel=`) did nothing with the wheel (owner, 2026-10-20: "the big mpc job wheel doesn't do anything"). So a list row can't both take taps for itself and hand the wheel to a cursor. | device (2026-10-20) |
| A Q-Link's small turn arrives as **several events** on the Live II, one step each with `nudge_pct`, so a list cursor on a Q-Link "skip[s] a few rows" (owner, 2026-10-20). The wheel's click is one event of the same size, so the two can't be told apart: give the Q-Link a parameter of its own and count events there (`QL_TICKS` 3). | device (2026-10-20); the 3: offline, from the Key 37's (NOTES `qlink_ticks`) |
| A wheel click sends the value MPC last read back **+0.01** of the range, a Q-Link event **+1/128** (1-3 on a fast spin). On a 0-999 range that is 10 and 8 entries a tick: `nudge_pct` makes any move under N % of the range one step (wrapper; with `QLINK_TRAVEL` it needs fork commit `b014168`). | device (Force probe, NOTES "Input probe"; SAMPLE one per tick on the Live II, 2026-10-19) |
| A text readout is re-read every 100 ms by the wrapper (it hashes the text), so FOLDER and the status box update without being marked. | framework |
| A row is a **Toggle Switch** button: a tap sends `1 - its value`. A row whose value reads 1 (the cursor's, drawn with its "on" tile) sends **0** on a tap. | framework; the lit row on a device: **not yet checked** |
| Parameters are saved and Q-Linked **by index**: new ones go at the end, never reorder. | device |

## The engine

### Reading files: `src/library.c` (shared by every instance, its own thread)

The audio thread never opens a file or waits: it asks and checks back.

- **Listings** (`lib_browse(b, path)` then `lib_listing(b)`, per instance): the thread reads the folder and hands back a
  `lib_list_t` when it's done ("READING ..." until then). Entries are `LE_UP`, `LE_FILE`, `LE_HEAD` (a subfolder's
  heading in one list), `LE_DIR` (a subfolder row, one level at a time), `LE_PLACE` (a drive), `LE_ALPHABASE` (the
  built-in set). `g[]` lists the folders whose files are in it (`g[0]` the folder itself), each file knows its
  folder (`grp`). A newer request outdates an older one (`want_gen`).
- **One list, subfolders and all** (`list_folder`, `flat_sub`): the folder's files, then each subfolder's, depth first in
  natural order, skipping hidden names and folders with no WAV. It gives up and lists one level at a time past
  `LIB_FLAT` (999) entries or `FLAT_DIRS` (256) folders looked in; `FLAT_DEPTH` 8; `LIB_MAXDIRS` 600 subfolders; the
  first `LIB_FILES` (255) WAV files of a folder.
- **Folders** (`lib_folder_set(listing, g)`): a folder gets a number the first time a file of it is heard or loaded,
  with its file names as listed; listed again unchanged, the same number; changed, a new number (the old one keeps
  playing what it had). `LIB_FOLDERS` 1024 per process.
- **Files** (`lib_get(folder, file, &smp)` / `lib_put(slot)`): returns a held slot if the file is in memory, else asks
  for it and returns -1 (ask again next block). 128 slots, 64 MB of decoded audio; past that, the files longest unplayed
  that nothing holds are freed. A voice or the preview holds its slot (a count), so nothing is freed under it.
- **WAV:** 8/16/24/32-bit PCM and 32-bit float, `WAVE_FORMAT_EXTENSIBLE`, any rate (played at its own pitch), mixed to
  mono 16-bit, the first minute, 64 MB a file at most.

### The browser: `src/incipience.c` ("the SAMPLES tab")

- `browse(P, path)`: asks for a listing; `browse_adopt()` takes it once read (called from every display and set, so
  it lands at once) and places the cursor: on LOAD TO's wanted file (`bfind`), else on the first file of the folder
  just left (UP), else on the file LOAD TO's instrument plays (`mine_at`), else on the first file.
- `cursor_to(P, k, hear)`: the one place the cursor moves. Clamps, steps over headings and `..` in the direction it
  was going (`on_file`), scrolls the view to keep it in sight, marks the 8 rows changed (their text and which is lit),
  then if the move was the user's: AUTO LOAD loads, else PREVIEW plays.
- `browse_tap(P, row, on)`: a tap with value 1, or 0 on the lit row (MPC's Toggle Switch on a row reading 1). A lone 0
  within 100 ms of the tap that lit the row is MPC echoing that tap and is ignored (`ECHO_FRAMES`). A file not under
  the cursor: cursor there. Anything else: `browse_act`.
- `browse_act(P, k)`: opens a folder, a heading's folder or a place, goes up, or loads a file:
  `lib_folder_set` for its folder, the instrument's folder (`sfold[c]`) and SAMPLE set to the file's number, then the
  instrument plays it once (`audition`) if PREVIEW is on.
- `preview()` / `preview_block()`: the file under the cursor, from its folder, mixed after the effects; waits for the
  file at most a second.
- `e_get_param` for a row's value: 1 if the cursor is on that row (that's what lights its tile).
- `cursor_set()`: FILE and BROWSE (FILE's Q-Link) always read the same. `ql_tick()`: BROWSE's events (each already a
  step from the wrapper's `nudge_pct`) move the cursor a row per `QL_TICKS`; SCROLL's between-whole-number moves (a
  Q-Link's, the wheel's) a page per `QL_TICKS`, its whole-number ones (an arrow's, a restore) at once. A turn back
  starts the count over; every event marks the parameter so MPC reads back where it is.
- Saved state: `<inst>_dir`, `<inst>_file` per sample instrument, `br_path`; a saved file is found by name once its
  folder is read (`find_wanted`).

## The parameters (`mpc/gen.py`)

In this order at the end of the plugin's list (append-only):

| Key | Name (what MPC shows) | Kind | Notes |
|---|---|---|---|
| `br_row_1`..`_8` | ROW 1-8 | momentary, `dynamic_display` | text = the entry; value 1 = the cursor's row |
| `br_scroll` (+ `_prev`/`_next`) | SCROLL | stepper 0-999 | the top row; arrows a page, a Q-Link a page per three |
| `br_path`, `br_status` | FOLDER, IN IT | readout (string) | |
| `br_target` | LOAD TO | 8 options | the sample instruments |
| `br_preview` | PREVIEW | ON/OFF, on | |
| `br_up`, `br_places` | UP, DRIVES | momentary | |
| `br_cursor` (+ `_prev`/`_next`) | FILE | stepper 0-999, `clamped`, `display: int`, `nudge_pct: 10` | the box: a file a wheel click |
| `br_load` | LOAD | momentary | |
| `br_audition` | AUDITION | ON/OFF, off | |
| `br_autoload` | AUTO LOAD | ON/OFF, off | |
| `br_cursor_q` | BROWSE | stepper 0-999, as FILE, `qlink_only` | Q-Link 1: the cursor a row per three events |

`nudge_pct` needs the release tools at fork commit `b014168` or later; this repo's own `tools/build.sh` wrapper
doesn't have it yet (a Q-Link tick there moves FILE several entries).

## The screen

The page is drawn in `mpc/design.py` (`browser_page()`, the right column's `side_panels`), and three layout lines
are added by `mpc/merge_ab.py` from `BROWSER_LINES`:

```
readout cx=290 cy=154 w=488 h=32 vs=20 box=no style=dotmatrix key=br_path
readout cx=656 cy=154 w=196 h=32 vs=20 box=no style=dotmatrix key=br_status
list x=40 y=182 w=720 h=362 key=br_row cols=1 rows=8 th=40 gap=6 ink=ffffff lit=d32a2a focus=no
```

`ink=`, `lit=` and `focus=no` are this repo's additions to the skin builder (`tools/shadow_skin.py` in
`framework/mpc-vst-plugins.patch`, 2026-10-20): white text, the cursor's row in a red 3-pixel border (the default, the
active segment colour, was near-black on the black list), and no row taking the hardware focus (by default the first
row takes it). `check_skin.py` knows both: a `focus=no` list needs no bound row, and a `qlink_only` parameter may sit on
a Q-Link with nothing on the page.
FILE and SCROLL are `stepper`s (their text is touchable and takes the wheel); LOAD, UP and DRIVES are buttons (MPC
sizes a button by its label: only its centre comes from the markup); LOAD TO is an 8-option `enum_h` in two rows.

## Putting it in another plugin

1. **Library:** copy `src/library.c` and the `library.c` block of `src/incipience.h` (`lib_*`, `lib_list_t`,
   `lib_grp_t`, `LE_*`, `LIB_*`). It needs only the `smp_t` type and the plugin's data folder (`lib_open(dir)` at
   create, `lib_close()` at destroy, `lib_poke()` once a block). Rename `LE_ALPHABASE` / the places to your plugin's
   built-in set, or drop it.
2. **Parameters:** append the table above (adjust LOAD TO's options to your instruments or voices; one target is
   fine). Keep `nudge_pct` on FILE and on each voice's sample number.
3. **Engine:** copy the "SAMPLES tab" section of `src/incipience.c` (from `mark()` to `browse_status()`), the
   preview's two lines in the render (`prv_want` wait, `preview_block` after the effects), the `e_set_param` /
   `e_get_param` / display branches for `br_*`, and the state keys. Replace `set_folder` / `sample_ask` / `SMP_P` with
   how your voices take a sample (a folder number and a file number into `lib_get`).
4. **Screen:** the three layout lines above (with `ink=`, `lit=`, `focus=no`), FILE and SCROLL steppers (FILE's caption
   says to tap it for the wheel), LOAD / UP / DRIVES buttons, LOAD TO, the three switches; BROWSE on Q-Link 1, SCROLL
   on 2.
5. **Tests:** `mpc/selftest.c`'s "your own samples" block builds folders of WAV files and drives the browser through
   its parameters (taps, MPC's echo, FILE across headings, AUTO LOAD, UP, LOAD TO, the one-level fallback, 47 files
   past the memory limit while another voice plays). Run it under ASan/UBSan and TSan (Docker `gcc:12`; TSan needs
   `--security-opt seccomp=unconfined`).

## Still to check on a device (2026-10-20)

- The wheel on FILE's box after one tap, and staying there while rows are tapped (`focus=no`). Untried: whether MPC
  gives a page's first focusable control the focus when it opens (then FILE could have it with no tap).
- BROWSE and SCROLL at three Q-Link events a step: whether three is right for the Live II's Q-Links.
- The cursor's red border, and a second tap on the lit row loading (MPC's 0), the 100 ms echo guard.
- A drive's top level and a big sample pack: how long the one-list read takes on the SSD and SD card (the thread reads
  up to 256 folders before giving up), and the preview's delay.
- AUTO LOAD while a sequence plays the instrument.
