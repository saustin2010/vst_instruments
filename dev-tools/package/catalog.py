"""What each plugin is, for the vst_instruments repo's READMEs (package.py). One entry per port folder in
steve/schwung-ports: where it goes in the repo, what kind of plugin it is, and a description written for musicians.
Credits and licences come from the port's own files (UPSTREAM, LICENSE, src/module.json); `origin` covers ports
whose source has no UPSTREAM file."""

# group: the repo folder the port goes in
#   schwung/instruments, schwung/sequencers, schwung/effects   modules written for Ableton Move's Schwung host
#   mutable-instruments                                         ported from Mutable Instruments' own firmware source
#   vcv-rack                                                    ported from a VCV Rack module
# kind: Synth, Drum synth, Drum sampler, MIDI sequencer, Arpeggiator, Modulator, Audio effect
PORTS = {
    "303": dict(
        group="schwung/instruments", kind="Synth", title="303",
        tagline="TB-303 bass line: Open303 with the Devilfish mods and a drive stage.",
        about="""A Roland TB-303 emulation built on Robin Schmidt's Open303 engine, with the "Devilfish" modifications
(separate normal and accent decay, filter tracking, slide time, overdriven sub) and a soft/RAT-style drive after the
filter. Play it from MPC's pads, keys or a clip: overlapping notes slide, high velocities accent. Monophonic, as the
original. Turn CUTOFF, RESONANCE, ENV MOD and DECAY while a pattern runs for the classic acid squelch; the DEVILFISH
page holds the extra envelope, filter and slide controls.""",
        origin="Schwung module \"303\" v0.3.3 by Robin Schmidt / midilab / davemollen / Charles Vestal (Open303 + Devilfish)"),
    "aphex": dict(
        group="schwung/instruments", kind="Synth", title="Aphex",
        tagline="Korg MS-10/MS-20 style mono synth with a patch bay, an ESP section and both MS filters.",
        about="""A semi-modular, MS-20-flavoured mono synth: two VCOs with footage switches, both classic MS filter
circuits (the KORG-35 of the REV.1 and the OTA of the REV.2), two envelopes, a mixer, a source-side patch bay for
routing modulation, and the External Signal Processor (envelope follower, pitch tracking) that lets the synth follow
another sound. A MODERN page adds trims and extras the original never had. 41 named presets, plus randomise / mutate /
reset buttons for happy accidents. Black-panel look with white legends and orange patch-cable accents.""",
        origin=None),
    "braids": dict(
        group="schwung/instruments", kind="Synth", title="Braids",
        tagline="Mutable Instruments Braids: a macro oscillator with 47 synthesis models.",
        about="""Emilie Gillet's Braids "macro oscillator" as a playable synth voice. One knob picks the model (classic
analog waveforms, vowel/formant synthesis, FM, wavetables, physical models of plucked strings and drums, granular
clouds, noise...) and TIMBRE and COLOR shape it. Around the oscillator this port adds a filter, an amp envelope, a
filter envelope and modulation, so it plays like a complete synth. Ten preset files ship with it.""",
        origin="Schwung module \"Braids\" v0.2.8 by Emilie Gillet (port: charlesvestal)"),
    "chordism": dict(
        group="schwung/instruments", kind="Synth", title="Chordism",
        tagline="One key in, a four-voice chord out: morphing oscillators, FM, filter, lo-fi, delay, reverb and an arpeggiator.",
        about="""A chord machine: every note you play becomes a four-voice chord (octaves, fifths, minor, major,
sevenths, ninths, elevenths and more). Each voice has its own waveform (sine, triangle, saw, square, pulse or
wavetable) and shape; MORPH and PAN MORPH move the level and stereo position across the voices, and FM and a shape LFO
animate them. Then a 12/24 dB multimode filter with its own envelope and LFO, vibrato, a pitch sweep, a lo-fi section
(grind, bit shift, decimator), delay, reverb, glide and an arpeggiator that plays the chord's notes for you. Ten
pages, 135 parameters; the MAIN page gathers what you reach for most.""",
        origin=None),
    "denis": dict(
        group="schwung/instruments", kind="Synth", title="Denis",
        tagline="West Coast / Serge-inspired mono synth: complex oscillators, a wavefolder and a modulation matrix.",
        about="""A West Coast voice in the Serge / Buchla tradition: two oscillators (frequency, timbre and
harmonics) mixed into a wavefolder with several fold types for bright, harmonically rich timbres, a multimode filter
(low-pass, band-pass, high-pass, notch), an ADSR, and a noise source (white, pink, brown). A modulation matrix sends
the envelope, an LFO, sample & hold and noise to eight destinations each (pitch, timbre, harmonics, fold, filter
type, cutoff, level). 30 named presets plus randomise and reset buttons. Panel look: Serge black with banana-jack
colours.""",
        origin=None),
    "elements": dict(
        group="mutable-instruments", kind="Synth", title="Elements",
        tagline="Mutable Instruments Elements: a modal synthesis voice you bow, blow and strike.",
        about="""Elements models an acoustic instrument in two halves. The exciter bows, blows (with a flow of
noise through a tube) or strikes (mallets, plectrums, particles); the resonator is a bank of modes that rings like a
string, a plate, a bar or a bell, with GEOMETRY, BRIGHTNESS, DAMPING and POSITION shaping it, followed by a stereo
reverb space. This port runs Mutable's own Elements DSP: MIDI notes become the module's gate, pitch and strike
strength (velocity), and LEGATO lets a held note glide without re-striking. MODEL's fourth entry is the module's
hidden "Ominous" voice. Page 1 is laid out like the module's panel.""",
        origin=None),
    "eucalypso": dict(
        group="schwung/sequencers", kind="MIDI sequencer", title="Eucalypso",
        tagline="Four-lane Euclidean sequencer: turns held notes into interlocking rhythms for other tracks.",
        about="""Hold a chord (or a single note) on Eucalypso's track and its four lanes play it as Euclidean
rhythms: each lane spreads PULSES hits evenly over its STEPS, with ROTATE, a DROP chance, its own note and octave
(with randomisation), velocity and gate, all in time with MPC's tempo. Notes come from what you hold, from a scale,
or as drum-pad notes (REGISTER). Swing, velocity and gate randomisation loosen it up; seeds make every random choice
repeatable. It makes no sound itself: its notes go out of its own MIDI port to whatever tracks you point at it (see
"Playing other tracks").""",
        origin=None),
    "fizzik": dict(
        group="schwung/instruments", kind="Synth", title="Fizzik",
        tagline="Physical modelling: an exciter and two coupled resonators (string, beam, plate, membrane).",
        about="""A physical-modelling instrument. An exciter (an impulse and noise mix with crackle, colour, its
own envelope and resonance, velocity-sensitive) sets two resonators ringing; each can be a string, a beam, a plate or
a membrane, with its own structure, decay, damping, position, tuning and tension, and COUPLE lets the two feed each
other. After them: a filter with six voicings (SVF, SEM, MS-20, Steiner, two ladders), drive, chorus, delay, reverb
and a limiter, two LFOs and aftertouch presets (bow, swell, vibrato...). 31 named presets and three randomise
buttons. Plucked and struck tones, bowed glass, bells, metal and wood that react to how hard you play.""",
        origin=None),
    "grids": dict(
        group="mutable-instruments", kind="MIDI sequencer", title="Grids",
        tagline="Mutable Instruments Grids: a topographic drum sequencer that plays a drum track from MPC's clock.",
        about="""Grids holds a map of drum patterns learned from real grooves. MAP X and MAP Y pick a spot on the map,
the three FILL knobs set how busy the kick, snare and hi-hat parts are, and CHAOS perturbs the pattern so it
breathes. Switch to EUCLIDEAN mode and the fills and three LENGTHs make Euclidean rhythms instead. It needs no notes:
press play and it sends kick/snare/hat notes (with accents) out of its own MIDI port to a drum program, locked to
MPC's tempo and bar. Ported from Mutable's original AVR code, rewritten so each instance has its own state.""",
        origin=None),
    "groovebank": dict(
        group="schwung/sequencers", kind="MIDI sequencer", title="Groove Bank",
        tagline="Retriggers the chord you hold in a rhythm from a library of genre grooves.",
        about="""Hold a chord on Groove Bank's track and it replays it in a rhythm template from its library
of genre-first grooves (each with variants), with swing, gate length, strum and accent; LATCH keeps it going after you
let go. It follows MPC's tempo. The groove library ships with it. The notes go out of its own MIDI port to the tracks
you point at it; it makes no sound itself.""",
        origin=None),
    "hank": dict(
        group="schwung/instruments", kind="Synth", title="Hank",
        tagline="Two-operator FM on one page: ratio, brightness, bite, an envelope pair, noise and a tone sweep.",
        about="""A small, immediate FM synth, as its author puts it: "two-operator FM on eight knobs, one page:
ratio, brightness, bite, one envelope pair, noise and a tone sweep". Add voices, glide and transpose, and that's it.
32 built-in presets cover basses, keys, bells and plucks.""",
        origin="Schwung module \"Hank\" v0.6.0 by charlesvestal"),
    "helm": dict(
        group="schwung/instruments", kind="Synth", title="Helm",
        tagline="Matt Tytel's Helm polysynth with its factory patches (275 with Move Organ).",
        about="""Helm is a full polyphonic subtractive synth: two oscillators with unison and cross-modulation, a sub
oscillator and noise, a multimode filter with a formant section, filter and modulation envelopes, mono and poly LFOs,
a 32-step sequencer, an arpeggiator, distortion, delay, reverb and a stutter effect. This port runs Helm's own DSP
engine headless and comes with its 274 factory patches (CC BY 4.0, by Matt Tytel and contributors) plus
the Schwung port's Move Organ. Twelve pages; steps
17-32 of the step sequencer are automatable but not on a page.""",
        origin=None),
    "hera": dict(
        group="schwung/instruments", kind="Synth", title="Hera",
        tagline="Roland Juno-60: jpcima's Hera engine, with its 56 presets.",
        about="""A Juno-60 emulation: one DCO per voice with saw, pulse (PWM from the LFO or envelope), sub and noise,
a high-pass filter, the 24 dB resonant low-pass VCF with envelope, LFO and keyboard tracking, an ADSR, and the
famous BBD chorus (modes I, II and both). Polyphonic, warm and simple, as the original. 56 preset files ship with it.
The MAIN page has the patch browser, chorus, filter and envelope faders; DCO / LFO has the oscillator section.""",
        origin="Schwung module \"Hera\" v0.1.7 by jpcima (port: charlesvestal)"),
    "hush1": dict(
        group="schwung/instruments", kind="Synth", title="Hush One",
        tagline="Roland SH-101: a monophonic bass and lead synth with 11 presets.",
        about="""An SH-101 emulation: one oscillator with saw, pulse (with PWM), sub-oscillator and noise mixed
together, the resonant 4-pole filter with envelope and keyboard tracking, an ADSR, an LFO with its own routing,
portamento and the 101's trigger modes. Built for basses, leads and acid lines. 11 built-in presets.""",
        origin="Schwung module \"HUSH ONE\" v0.2.8 by the Move Everything community"),
    "libpo32": dict(
        group="schwung/instruments", kind="Drum synth", title="Libpo32",
        tagline="PO-32-style drum synth: 16 synthesised drum sounds on pads 36-51.",
        about="""A drum synthesiser in the spirit of Teenage Engineering's PO-32 Tonic: every sound is synthesised
(oscillator, pitch and noise envelopes, modulation), with 16 sounds on MIDI notes 36-51, so MPC's pads play it
directly. Three kits ship with it (tonic, tape, acid). KIT holds the kit and level per pad; EDIT is a per-pad sound
editor (pick a pad, shape its oscillator, modulation, noise and output); TUNE sets each pad's pitch and decay.""",
        origin="Schwung module \"Libpo32\" v1.1.0 by mestela"),
    "marbles": dict(
        group="mutable-instruments", kind="MIDI sequencer", title="Marbles",
        tagline="Mutable Instruments Marbles: a random sampler that plays up to three tracks.",
        about="""Marbles generates random rhythms and melodies you can steer. The T section makes three related
gate streams (T1, T2, T3) with controllable rate, bias and jitter; the X section makes random voltages quantised to
a scale, with SPREAD, BIAS and STEPS shaping their distribution. DEJA VU makes both loop: turn it up and the
randomness repeats. This port runs Mutable's own T and X generators clocked from MPC's tempo, and plays T1/T2/T3 as
notes of pitch X1/X2/X3 on MIDI channels 1, 2 and 3 (or all on 1), out of its own MIDI port. SETUP holds what the
module keeps in its menus (scales, ranges, clock ratio).""",
        origin=None),
    "mazelite": dict(
        group="schwung/sequencers", kind="MIDI sequencer", title="Maze Lite",
        tagline="A dual generative sequencer in the style of the Moog Labyrinth.",
        about="""Two clock-synced 8-step generative sequencers in the style of the Moog Labyrinth: each loops a
short pattern of bits that turns into notes in the chosen scale around a root, and CORRUPT mutates it (past noon it
starts flipping bits), from locked repetition to constant change. Per sequencer: range (pitch spread around the root),
length (1-8 steps), BIT FLIP and ADVANCE buttons and a reset every 1-8 bars; TRIG MIX, note rate and note length
shape the output. It runs with MPC's transport, a note you play on its track sets the key, and it plays out of its own
MIDI port for other tracks. The engine is sd88me's own Schwung module (the sequencer half of his Maze Voice).""",
        origin=None),
    "midiplayer": dict(
        group="schwung/sequencers", kind="MIDI sequencer", title="MIDI Player",
        tagline="Plays Standard MIDI Files in time with MPC's transport.",
        about="""Drop .mid files into /sdcard/vst/midiplayer/MIDI on the MPC and MIDI Player plays them in sync
with MPC's tempo and transport, out of its own MIDI port to the tracks you point at it. Step through files with the
arrows (the file's name shows on screen), pick one track of the file or ALL (every track, sent on channel 1), and
switch LOOP on or off. A short demo file ships with it.""",
        origin=None),
    "monksynth": dict(
        group="schwung/instruments", kind="Synth", title="MonkSynth",
        tagline="A formant (FOF) singing voice with 12 characters and a choir.",
        about="""A vocal synthesiser built on FOF formant synthesis: it sings vowels, and its 12 singers
(the presets) are different characters. Shape VOWEL, HEAD SIZE, BREATH, glide and vibrato on the SINGER page, with an
ADSR; CHOIR stacks unison voices (detune, spread), adds an echo, and routes pressure (aftertouch) to the vowel, the
pitch or both. Choirs, chants, robotic voices and vocal pads.""",
        origin=None),
    "monovoice": dict(
        group="schwung/instruments", kind="Synth", title="Mono Voice",
        tagline="Elektron Monomachine-style digital voice: SuperWave, SID, DigiPRO, FM and more machines.",
        about="""A digital synth voice modelled on the Elektron Monomachine: the MACHINE page picks the synthesis
machine (SuperWave, SID-style chip sounds, user-wave DigiPRO, FM and others, with an arpeggiator), and the SYNTH page's
16 controls change meaning with the machine (they're numbered SYN 1-16 for that reason). Then an amp page, a filter
page and an effect page, each with a SHIFT layer, and three LFOs that can each reach any of 114 destinations (picked
with arrows).""",
        origin=None),
    "moog": dict(
        group="schwung/instruments", kind="Synth", title="Moog",
        tagline="RaffoSynth: a Minimoog-style mono synth with four oscillators and a ladder filter.",
        about="""RaffoSynth, a Minimoog-inspired monosynth: four oscillators (waveform and footage switches, 32' to
2', detune), noise, the 24 dB resonant ladder filter with its contour, a loudness contour, an LFO, mod wheel routing
and glide. Fat basses and leads. 14 presets, also listed in MPC's own PRESET menu in the plugin header. The oscilloscope
on the MAIN page shows oscillator 1's waveform, and the two envelope displays follow their knobs.""",
        origin="Schwung module \"RaffoSynth\" v0.2.5 by Nicolas Roulet, Julian Palladino (port: charlesvestal)"),
    "mrdrums": dict(
        group="schwung/instruments", kind="Drum sampler", title="Mr Drums",
        tagline="A 16-pad drum sampler: kits of samples on pads 36-51.",
        about="""A drum sample player: each kit is a folder of up to 16 samples on MIDI notes 36-51, so MPC's pads
play it. Per pad: volume, pan, tune, start, attack, decay, choke group, gate or one-shot, and random pan, volume and
decay plus a play chance for humanised hits. Master volume, polyphony, velocity curve and humanize apply to the kit.
Kits live in /sdcard/vst/mrdrums/kits on the MPC (add your own folders there); a starter kit of synthesised hits
ships with it.""",
        origin="Schwung module \"MrDrums\" v0.0.4 by move-anything contributors"),
    "mrhyde": dict(
        group="schwung/instruments", kind="Synth", title="Mr Hyde",
        tagline="A MicroFreak-inspired voice: Plaits models with a low-pass gate, filter and a 6x6 mod matrix.",
        about="""Built around Mutable Instruments' Plaits engine (17 models: virtual analog, wavetables, FM, grains,
chords, speech, strings, modal and percussion models), Mr Hyde adds what a MicroFreak-style instrument needs: a
low-pass gate, a filter, an LFO, envelopes, a cycling envelope, a random source, and a 6x6 modulation matrix whose
rows are spread over the ASSIGN, PITCH HARM and TIMB CUT pages.""",
        origin="Schwung module \"MrHyde\" v0.0.1 by move-anything contributors"),
    "noisemaker": dict(
        group="schwung/instruments", kind="Synth", title="Noisemaker",
        tagline="TAL-NoiseMaker: a classic virtual-analog polysynth with 256 factory presets.",
        about="""TAL-NoiseMaker by Patrick Kunz: two oscillators plus sub, 12 multimode filters with their own envelope
and velocity response, two LFOs, a third envelope you can draw, chorus, reverb and delay. Six voices. All 256 factory
presets are built in, from pads and leads to basses and effects.""",
        origin="Schwung module \"Noisemaker\" v0.2.2 by legsmechanical (engine: Patrick Kunz / TAL)"),
    "nusaw": dict(
        group="schwung/instruments", kind="Synth", title="NuSaw",
        tagline="A detuned multi-saw (supersaw) polysynth with 27 presets.",
        about="""The supersaw sound of trance and big-room synths: a stack of detuned saws (SAWS, DETUNE, SPREAD)
with a sub oscillator, a resonant low-pass filter with its own envelope, an amp envelope, chorus and delay. 27
built-in presets, shown on a cyan LED patch display. Huge pads, stabs and leads with little effort.""",
        origin=None),
    "obxd": dict(
        group="schwung/instruments", kind="Synth", title="OB-Xd",
        tagline="Oberheim OB-X: reales' OB-Xd with its 128 factory presets.",
        about="""The OB-Xd emulation of Oberheim's OB-X: two oscillators per voice with sync, cross-modulation and pulse
width, a mixer with noise, a 12/24 dB multimode filter, filter and amp envelopes, an LFO routed to pitch, filter and
pulse width, and "voice variation" controls that detune each voice's oscillator, filter and envelopes slightly, as
analog voices do. 128 factory presets ship with it.""",
        origin="Schwung module \"OB-Xd\" v0.4.9 by reales (port: charlesvestal)"),
    "pixelwalkers": dict(
        group="schwung/sequencers", kind="MIDI sequencer", title="Pixel Walkers",
        tagline="Generative: the notes you play become walkers that bounce and retrigger when they land.",
        about="""A pixel-art gravity sequencer: every note you play on its track becomes a walker that falls,
bounces and retriggers its note each time it lands, so a few notes turn into a bouncing, decaying pattern. BOUNCE and
HARDNESS set how lively they are, HIT LEVEL and HIT DECAY how loud the retriggers stay, BIRTH NOTE whether the note
also plays when the walker is born; RANDOMIZE, TOMBOLA and KILL ALL shake things up or clear the screen. It plays out
of its own MIDI port for other tracks.""",
        origin=None),
    "plaits": dict(
        group="schwung/instruments", kind="Synth", title="Plaits",
        tagline="Mutable Instruments Plaits: a macro oscillator with 24 models, played like a synth.",
        about="""Plaits, the successor to Braids: 24 synthesis models. The original sixteen (virtual analog,
waveshaping, FM, grains, additive, wavetables, chords, speech, swarms, noise, modal and string physical models, three
drums) plus the eight from the later firmware (analog with a filter, phase distortion, three 6-operator FM banks, wave
terrain, string machine, chiptune). HARMONICS, TIMBRE and MORPH shape every model, and the built-in low-pass gate gives
it plucky, organic decays. For the 6-op FM models, FM PATCH steps through the bank's patches and shows each patch's
name.""",
        origin=None),
    "rampage": dict(
        group="vcv-rack", kind="Modulator", title="Rampage",
        tagline="Befaco Rampage: a dual slope generator (envelopes, LFOs, slew) that modulates other tracks.",
        about="""Rampage is Befaco's dual function generator: two channels that each make a rise and a fall, with
adjustable times, shapes and range. Triggered they're envelopes, gated they hold (attack-sustain-release), cycling
they're LFOs, and BALANCE and the MIN/MAX outputs combine both channels. On the MPC, notes on its track trigger or
gate each channel, and OUT A/B, MIN and MAX go out of its own MIDI port as CCs you can MIDI-learn on another track;
end-of-cycle events go out as notes. Turn AUDIO on to hear the outputs. Ported unchanged from Befaco's VCV Rack code
through a small Rack API stand-in.""",
        origin=None),
    "rings": dict(
        group="mutable-instruments", kind="Synth", title="Rings",
        tagline="Mutable Instruments Rings: a resonator, strummed by the notes you play.",
        about="""Rings is a resonator with seven models (a modal resonator, sympathetic strings, a string, an FM voice,
sympathetic chords, a string with reverb, and the module's hidden "Disastrous Peace" string synth), shaped by
STRUCTURE, BRIGHTNESS, DAMPING and POSITION, with one, two or four voices ringing at once. Here every note strums it
with Rings' internal exciter (velocity sets how hard) and the voices rotate as on the module. The string synth's
effect (formant, chorus, reverb, ensemble) is SYNTH FX. Runs Mutable's own DSP.""",
        origin=None),
    "ringsfx": dict(
        group="mutable-instruments", kind="Audio effect", title="Rings FX",
        tagline="Rings as an audio effect: the track's sound excites the resonator.",
        about="""The same Rings resonator as an insert effect: the track's audio becomes the exciter and Rings' own
onset detector strums it, tuned to NOTE. Drums turn into tuned metallic hits, voices and loops grow sympathetic
string halos. INPUT gain and a dry/wet MIX sit alongside the module's controls.""",
        origin=None),
    "superarp": dict(
        group="schwung/sequencers", kind="Arpeggiator", title="Super Arp",
        tagline="A pattern and rhythm arpeggiator with 40 patterns, 40 rhythms and random modifiers.",
        about="""An arpeggiator that separates the note order from the rhythm: a MODE (up, down, as played,
leaps, chord) or one of 40 PATTERNs sets which note comes next, and one of 40 RHYTHMs sets when, so the same chord can
roll, skip and syncopate in many ways. Drops, octave and note randomisation, and velocity and gate randomisation all
have seeds, so a random result repeats until you change it. Hold a chord on its track: the arpeggio follows MPC's tempo
(or its own BPM) and goes out of its own MIDI port to the tracks you point at it.""",
        origin=None),
    "tablor": dict(
        group="schwung/instruments", kind="Synth", title="Tablor",
        tagline="A two-oscillator wavetable synth that ships with its wavetables.",
        about="""Two wavetable oscillators, each scanning its own table (the table's name shows next to it, and the
arrows step through the library), with unison and shape controls, a sub and noise, a filter, amp and filter
envelopes, two modulation envelopes and a voice section. Its wavetable library ships with it; add your own tables
to /sdcard/vst/tablor/wavetables on the MPC.""",
        origin=None),
    "verglas": dict(
        group="schwung/effects", kind="Audio effect", title="Verglas",
        tagline="Mutable Instruments Clouds: a granular texture processor as an audio effect.",
        about="""Clouds (here Verglas) records the track's audio into a buffer and plays it back as grains:
POSITION, SIZE, PITCH, DENSITY and TEXTURE shape the cloud, FREEZE holds the buffer, and MODE switches between
granular, stretch, looper and spectral processing. DRY/WET, SPREAD, FEEDBACK and REVERB blend it, high- and low-pass
filters and a lo-fi QUALITY switch colour it, and the TONE page adds a low boost and a limiter. Ambient washes, frozen
pads and glitchy textures from any sound.""",
        origin=None),
    "warps": dict(
        group="mutable-instruments", kind="Audio effect", title="Warps",
        tagline="Mutable Instruments Warps: a meta-modulator (ring mod, fold, vocoder...) as an audio effect.",
        about="""Warps crosses two signals: its internal carrier oscillator (sine, triangle or saw at NOTE) and the
track's audio, or, with EXTERNAL, the input's left and right channels. ALGORITHM morphs through crossfading, folding,
analog and digital ring modulation, XOR, comparison, spectral morphing and a vocoder; TIMBRE adds the character of
each. MODE's second entry is the module's hidden frequency shifter. MIX blends the dry sound back.""",
        origin=None),
    "wurl": dict(
        group="schwung/instruments", kind="Synth", title="Wurl",
        tagline="A physically modelled Wurlitzer 200A electric piano.",
        about="""A model of the Wurlitzer 200A electric piano (the OpenWurli engine) rather than samples, so it
responds to velocity the way the real thing does, from mellow to barking. TREMOLO, BRIGHT, DARKEN, BARK, SPEAKER and
REVERB shape it, and ten presets set up different instruments (classic 200A, dreamy keys, barky soul, surf spring,
dark ballad...).""",
        origin=None),
}
