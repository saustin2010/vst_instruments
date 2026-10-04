"""Write presets.json, Mutable Vibe's presets for MPC's PRESET menu (the wrapper's own presets: upstream has none).
   python3 mpc-ports/mutablevibe/mpc/make_presets.py
Every preset sets every parameter: the defaults below, then its own changes, so switching between them is
deterministic. MODEL goes first (a model change silences the voices). VOLUME (dB) evens the levels out: LEVEL holds
each preset's trim, measured with dev-tools/presets/levels.sh (loudest 400 ms at about -16 dBFS, as Rings' presets)."""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)

DEFAULTS = {p["key"]: p["default"] for p in json.load(open(os.path.join(PORT, "params.json")))["params"]}

# Common pieces
SOFT_VERB = {"reverbDecay": 0.75, "reverbDamping": 0.4}
PLUCK_AMP = {"attack": 1.0, "decay": 2000.0, "sustain": 1.0, "release": 1200.0}   # the resonator decays by itself
VEL_BRIGHT = {"env5Target": "BRIGHTNESS", "env5Amount": 0.25}                       # VEL1: harder = brighter
MW_BRIGHT = {"modwheelTarget": "BRIGHTNESS", "modwheelAmount": 0.3}

PRESETS = [
    ("Init", {}),
    # Rings: STRUCTURE, BRIGHTNESS, DAMPING (longer as it goes up), POSITION (where the resonator is struck)
    ("Glass Marimba", dict(model="R: MODAL", structure=0.25, brightness=0.62, damping=0.42, position=0.32,
                           reverbWet=0.18, **PLUCK_AMP, **SOFT_VERB, **VEL_BRIGHT)),
    ("Tubular Bells", dict(model="R: MODAL", structure=0.62, brightness=0.7, damping=0.78, position=0.18,
                           reverbWet=0.25, reverbDecay=0.82, reverbDamping=0.3, attack=1.0, decay=2000.0, sustain=1.0,
                           release=2000.0, **VEL_BRIGHT)),
    ("Thumb Piano", dict(model="R: MODAL", structure=0.38, brightness=0.5, damping=0.38, position=0.6,
                         reverbWet=0.12, **PLUCK_AMP, **SOFT_VERB, **VEL_BRIGHT)),
    ("Sitar Drone", dict(model="R: SYMPATHETIC", structure=0.58, brightness=0.68, damping=0.82, position=0.15,
                         chorusWet=0.2, reverbWet=0.2, attack=1.0, decay=2000.0, sustain=1.0, release=2000.0,
                         **SOFT_VERB)),
    ("Harp Chords", dict(model="R: QUANTIZED", structure=0.35, brightness=0.55, damping=0.7, position=0.28,
                         reverbWet=0.3, **PLUCK_AMP, **SOFT_VERB, **VEL_BRIGHT)),
    ("Bent Pluck", dict(model="R: INHARM STRING", structure=0.3, brightness=0.42, damping=0.6, position=0.25,
                        delayWet=0.25, delayFeedback=0.4, delayTone=0.35, delayDiv="1/8.", **PLUCK_AMP, **VEL_BRIGHT)),
    ("FM Tines", dict(model="R: FM VOICE", structure=0.28, brightness=0.45, damping=0.55, position=0.4,
                      chorusWet=0.35, chorusRate=0.4, chorusDepth=0.4, reverbWet=0.15, **PLUCK_AMP, **SOFT_VERB,
                      **VEL_BRIGHT)),
    ("Verb String Pad", dict(model="R: REVERB STRING", structure=0.5, brightness=0.58, damping=0.85, position=0.4,
                             attack=300.0, decay=2000.0, sustain=1.0, release=2000.0, lfo1Target="POSITION",
                             lfo1Amount=0.2, lfo1Rate=0.2, lfo1Bipolar="BIPOLAR", **MW_BRIGHT)),
    ("Steel Strum", dict(model="R: SYMPATHETIC", structure=0.2, brightness=0.6, damping=0.6, position=0.22,
                         polyphony="4", reverbWet=0.15, **PLUCK_AMP, **SOFT_VERB, **VEL_BRIGHT)),
    # Plaits: HARMONICS, TIMBRE, MORPH on the first three knobs (POSITION = LPG colour; the LPG stays open, the amp
    # envelope shapes each note)
    ("Analog Bass", dict(model="P: VIRTUAL ANALOG", octave=-1, structure=0.2, brightness=0.5, damping=0.7,
                         attack=1.0, decay=300.0, sustain=0.6, release=120.0, filterCutoff=700.0, filterResonance=0.35,
                         filterEnvAmount=0.5, env2Attack=1.0, env2Decay=250.0, env2Sustain=0.0, env2Release=150.0,
                         satDrive=0.25, **VEL_BRIGHT)),
    ("VCF Lead", dict(model="P: VA VCF", structure=0.55, brightness=0.55, damping=0.6, attack=5.0, decay=600.0,
                      sustain=0.8, release=250.0, delayWet=0.2, delayFeedback=0.35, delayDiv="1/8.", **MW_BRIGHT)),
    ("Phase Keys", dict(model="P: PHASE DIST", structure=0.4, brightness=0.45, damping=0.6, attack=2.0, decay=900.0,
                        sustain=0.35, release=500.0, filterCutoff=6000.0, filterEnvAmount=0.3, env2Attack=1.0,
                        env2Decay=700.0, env2Sustain=0.0, env2Release=400.0, chorusWet=0.25, **VEL_BRIGHT)),
    ("Terrain Pad", dict(model="P: WAVE TERRAIN", structure=0.45, brightness=0.5, damping=0.5, attack=600.0,
                         decay=1200.0, sustain=0.85, release=1800.0, lfo1Target="BRIGHTNESS", lfo1Amount=0.3,
                         lfo1Rate=0.12, lfo1Bipolar="BIPOLAR", lfo2Target="DAMPING", lfo2Amount=0.25, lfo2Rate=0.07,
                         lfo2Bipolar="BIPOLAR", reverbWet=0.35, reverbDecay=0.82, reverbDamping=0.45, chorusWet=0.3,
                         **MW_BRIGHT)),
    ("String Machine", dict(model="P: STRING MACHINE", structure=0.5, brightness=0.6, damping=0.4, attack=250.0,
                            decay=800.0, sustain=0.9, release=1400.0, chorusWet=0.45, chorusRate=0.35,
                            chorusDepth=0.55, reverbWet=0.25, **SOFT_VERB)),
    ("Chip Lead", dict(model="P: CHIPTUNE", structure=0.5, brightness=0.3, damping=0.5, attack=1.0, decay=200.0,
                       sustain=0.7, release=80.0, delayWet=0.25, delayFeedback=0.3, delayDiv="1/16")),
    ("Wavefold Pluck", dict(model="P: WAVESHAPING", structure=0.4, brightness=0.62, damping=0.45, attack=1.0,
                            decay=450.0, sustain=0.0, release=300.0, env3Target="BRIGHTNESS", env3Amount=0.3,
                            env3Attack=1.0, env3Decay=300.0, env3Sustain=0.0, env3Release=200.0, reverbWet=0.15,
                            **SOFT_VERB, **VEL_BRIGHT)),
    ("FM Bell", dict(model="P: FM", structure=0.62, brightness=0.5, damping=0.3, attack=1.0, decay=1800.0,
                     sustain=0.0, release=1500.0, reverbWet=0.3, reverbDecay=0.8, reverbDamping=0.4, **VEL_BRIGHT)),
    ("Additive Organ", dict(model="P: ADDITIVE", structure=0.3, brightness=0.7, damping=0.2, attack=5.0,
                            decay=100.0, sustain=1.0, release=150.0, chorusWet=0.4, chorusRate=5.5, chorusDepth=0.35,
                            satDrive=0.15)),
    ("Wavetable Sweep", dict(model="P: WAVETABLE", structure=0.2, brightness=0.6, damping=0.5, attack=20.0,
                             decay=900.0, sustain=0.7, release=700.0, lfo1Target="BRIGHTNESS", lfo1Wave="SAW-TRI",
                             lfo1Shape=0.5, lfo1Amount=0.35, lfo1Rate=0.25, lfo1Bipolar="BIPOLAR", **MW_BRIGHT)),
    ("Chord Stab", dict(model="P: CHORD", structure=0.35, brightness=0.5, damping=0.4, attack=1.0, decay=350.0,
                        sustain=0.0, release=250.0, delayWet=0.3, delayFeedback=0.4, delayDiv="1/8.", reverbWet=0.15,
                        **SOFT_VERB)),
    ("Swarm Pad", dict(model="P: SWARM", structure=0.5, brightness=0.6, damping=0.4, attack=900.0, decay=1500.0,
                       sustain=0.9, release=2000.0, filterCutoff=6000.0, reverbWet=0.4, reverbDecay=0.85,
                       reverbDamping=0.45, **MW_BRIGHT)),
    ("Plucked String", dict(model="P: STRING", structure=0.4, brightness=0.5, damping=0.5, reverbWet=0.15,
                            **PLUCK_AMP, **SOFT_VERB, **VEL_BRIGHT)),
    ("Modal Mallet", dict(model="P: MODAL SYNTH", structure=0.5, brightness=0.6, damping=0.6, reverbWet=0.2,
                          **PLUCK_AMP, **SOFT_VERB, **VEL_BRIGHT)),
]

# VOLUME trims (dB) from dev-tools/presets/levels.sh, so each preset's loudest 400 ms is about -16 dBFS
LEVEL = {
    'Init': 18,
    'Glass Marimba': 16,
    'Tubular Bells': 14,
    'Thumb Piano': 16,
    'Sitar Drone': 11,
    'Harp Chords': 8,
    'Bent Pluck': 18,
    'FM Tines': 13,
    'Verb String Pad': 22,
    'Steel Strum': 11,
    'Analog Bass': -4,
    'VCF Lead': -7,
    'Phase Keys': 2,
    'Terrain Pad': -1,
    'String Machine': 6,
    'Chip Lead': -7,
    'Wavefold Pluck': -2,
    'FM Bell': -5,
    'Additive Organ': 9,
    'Wavetable Sweep': 5,
    'Chord Stab': 3,
    'Swarm Pad': 1,
    'Plucked String': 7,
    'Modal Mallet': -2,
}


def main():
    out = []
    names = set()
    for name, changes in PRESETS:
        assert name not in names and len(name) <= 24, name
        names.add(name)
        unknown = set(changes) - set(DEFAULTS)
        assert not unknown, (name, unknown)
        values = dict(DEFAULTS, **changes)
        values["volume"] = LEVEL.get(name, 0)
        ordered = {"model": values.pop("model")}
        ordered.update(values)
        out.append({"name": name, "values": ordered})
    with open(os.path.join(PORT, "presets.json"), "w") as f:
        json.dump({"presets": out}, f, indent=1)
        f.write("\n")
    print("%d presets" % len(out))


if __name__ == "__main__":
    main()
