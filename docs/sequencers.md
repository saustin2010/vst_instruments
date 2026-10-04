# Playing other tracks from a sequencer

The nine **[SEQ]** plugins (Eucalypso, Grids, Groove Bank, Marbles, Maze Lite, MIDI Player, Pixel Walkers, Rampage,
Super Arp) make no sound of their own: they play other tracks. MPC ignores the MIDI a plugin sends, so each one opens
its own MIDI port instead, the way a USB MIDI keyboard would appear. While the sequencer is on a track, MPC lists that
port as one of its MIDI inputs, named after the plugin:

    [SEQ] Super Arp MIDI Out

(A second copy of the same sequencer gets its own port, `[SEQ] Super Arp 2 MIDI Out`.) Any track can take that port
as its MIDI input, like a keyboard plugged into the MPC.

## Setting it up

Example: Super Arp playing Hera.

1. **Two tracks.** Put the instrument that should sound on one track (**[SYN] Hera**) and the sequencer on another
   (**[SEQ] Super Arp**). Give it a few seconds: MPC connects to the new port by itself, with no restart.
2. **Menu → Preferences → MIDI.** In the list of MIDI inputs, find **[SEQ] Super Arp MIDI Out** and switch **Track**
   on (leave Remote off). It's only listed while the sequencer is on a track.
3. **Select the Hera track** and set, in its track settings:
   - **MIDI Input Port**: [SEQ] Super Arp MIDI Out
   - **MIDI Input Channel**: All
   - **Monitor**: **In** (or Merge). **Not Auto**: with Auto a track only listens while it's the selected track, and
     you'll be on Super Arp's track to play it. This is the step that's easy to miss.
4. **Select the Super Arp track, press play and hold some notes** on the pads or keys (or give its track a clip).
   Super Arp's notes play Hera.

Leave the sequencer's own track listening to your pads and keys as usual. Never set its input to its own port, or it
plays itself.

## What makes each one play

All of them follow MPC's tempo and transport, so the transport has to run.

| Plugin | Plays when | Sends |
|---|---|---|
| Eucalypso | you hold notes on its track | your notes as four Euclidean rhythms |
| Grids | the transport runs | drum notes (kick, snare, hi-hat parts): point a drum track at it |
| Groove Bank | you hold a chord on its track (LATCH keeps it going) | the chord in a groove |
| Marbles | the transport runs | random melodies, up to three voices |
| Maze Lite | the transport runs (a note on its track sets the key) | two generative note patterns |
| MIDI Player | the transport runs | the `.mid` file you picked, from `/sdcard/vst/midiplayer/MIDI` |
| Pixel Walkers | you play notes on its track | your notes, bouncing |
| Super Arp | you hold notes on its track | the arpeggio |
| Rampage | it cycles, or you play notes on its track | control changes (CC OUT A/B), not notes: MIDI-learn a parameter on the target track |

Each plugin's README has its own details (channels, voices, its pages).

## When nothing plays

- **The port isn't in the list.** The sequencer has to be on a track; wait a few seconds after inserting it. Removing
  and re-inserting the plugin makes a new port.
- **The port is listed but the other track is silent.** Check, in order: **Track** is on for the port (step 2); the
  target track's **MIDI Input Port** is the port and its channel is All; its **Monitor** is **In**, not Auto; the
  transport is running; for Eucalypso, Groove Bank, Pixel Walkers and Super Arp, you're holding notes on the
  sequencer's own track.
- **Doubled or runaway notes.** A track is listening to its own sequencer's port, or two tracks share an input
  you didn't mean to share.

## How far this is checked

On a Live II (2026-10-04, stock MPC OS 3.9.1 with SSH): MPC finds a sequencer's port and connects to it by itself, within
seconds, with no restart, and a second track plays from it (MIDI Input Port = the port, Monitor In, Remote off for the
port). It works on a Force too. The framework's `docs/NOTES.md` ("MIDI-output plugins") has the technical side.
