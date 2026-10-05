# Playing other tracks from a sequencer

The **[SEQ]** plugins make no sound of their own: they play other tracks. MPC ignores the MIDI a plugin sends, so each
sequencer opens its own MIDI port instead, the way a USB MIDI keyboard would appear. While the sequencer is on a track,
MPC lists that port as one of its MIDI inputs, named after the plugin:

    [SEQ] <sequencer> MIDI Out

(A second copy of the same sequencer gets its own port, `[SEQ] <sequencer> 2 MIDI Out`.) Any track can take that port
as its MIDI input, like a keyboard plugged into the MPC.

## Setting it up

You need two tracks: the instrument that should sound, and the sequencer that plays it.

1. **Two tracks.** Put the instrument on one track (any [SYN] or [DRUM] plugin, or one of MPC's own instruments) and
   the sequencer on another. Give it a few seconds: MPC connects to the new port by itself, with no restart.
2. **Menu → Preferences → MIDI.** In the list of MIDI inputs, find **[SEQ] &lt;sequencer&gt; MIDI Out** and switch
   **Track** on (leave Remote off). It's only listed while the sequencer is on a track.
3. **Select the instrument's track** and set, in its track settings:
   - **MIDI Input Port**: [SEQ] &lt;sequencer&gt; MIDI Out
   - **MIDI Input Channel**: All
   - **Monitor**: **In** (or Merge). **Not Auto**: with Auto a track only listens while it's the selected track, and
     you'll be on the sequencer's track to play it. This is the step that's easy to miss.
4. **Select the sequencer's track and press play.** If it plays from notes, hold some on the pads or keys (or give its
   track a clip). The sequencer's notes play the instrument.

Leave the sequencer's own track listening to your pads and keys as usual. Never set its input to its own port, or it
plays itself.

## What makes a sequencer play

Every sequencer follows MPC's tempo and transport, so the transport has to run. Beyond that, each works in one of three
ways; its README says which, and covers its channels, voices and pages.

- **It plays from the notes you hold on its own track** (arpeggiators, chord grooves, rhythms made from held notes):
  hold notes on the pads or keys, or give its track a clip. Some have a LATCH that keeps it going after you let go.
- **It plays by itself while the transport runs** (step sequencers, generative and random sequencers, drum pattern
  generators, MIDI file players). A note on its track may set its key or transpose it.
- **It sends control changes, not notes** (modulation sources): MIDI-learn a parameter on the target track to it.

A sequencer that sends drum notes should play a drum track.

## Moving the instrument's controls

A sequencer can also send control changes (CC) along with its notes: Stevequencer's MOD lanes send a value per step,
Rampage sends its envelopes and LFOs. This repo's instruments take **CC 20-35** as their first page's Q-Links, in Q-Link
order (CC 20-23 = column 1, top to bottom, 24-27 = column 2, 28-31 = column 3, 32-35 = column 4), so CC 20 moves the
first knob. It needs nothing more than the routing above: the CCs arrive on the same MIDI input as the notes (working
on the Live II, 2026-10-05). Akai's own instruments don't follow these CCs.

To reach **any parameter on any page**, not only the first page's Q-Links, every instrument here also takes **NRPN n**
as its parameter n+1: [Stevequencer 16](../originals/stevequencer16/)'s lanes set to DEST **PARAM** use it, with the
parameter's P number from [parameter-numbers.md](parameter-numbers.md). (Not yet checked on a device: that MPC passes
NRPN through as it does CCs.)

## When nothing plays

- **The port isn't in the list.** The sequencer has to be on a track; wait a few seconds after inserting it. Removing
  and re-inserting the plugin makes a new port.
- **The port is listed but the instrument's track is silent.** Check, in order: **Track** is on for the port (step 2);
  the instrument track's **MIDI Input Port** is the port and its channel is All; its **Monitor** is **In**, not Auto; the
  transport is running; for a sequencer that plays from notes, you're holding notes on the sequencer's own track.
- **Doubled or runaway notes.** A track is listening to its own sequencer's port, or two tracks share an input
  you didn't mean to share.

## How far this is checked

On a Live II (2026-10-04, stock MPC OS 3.9.1 with SSH): MPC finds a sequencer's port and connects to it by itself, within
seconds, with no restart, and a second track plays from it (MIDI Input Port = the port, Monitor In, Remote off for the
port). It works on a Force too. The framework's `docs/NOTES.md` ("MIDI-output plugins") has the technical side.
