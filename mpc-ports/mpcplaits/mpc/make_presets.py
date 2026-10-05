"""Write presets.json, MPC Plaits' presets for MPC's PRESET menu (the wrapper's own presets: upstream has none).
   python3 mpc-ports/mpcplaits/mpc/make_presets.py
Every preset sets every parameter: src/module.json's defaults, then its own changes, so switching between them is
deterministic. MODEL goes first. VOLUME (0..2, 1 = as the model plays) evens the levels out: LEVEL holds each preset's
gain, measured with dev-tools/presets/levels.sh (loudest 400 ms at about -16 dBFS, as the other plugins' presets).
The 6-op FM presets pick a patch of the model's bank with HARMONICS: patch n of 32 is (n + 0.5) / 32."""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)

PARAMS = [p for p in json.load(open(os.path.join(PORT, "src", "module.json")))["capabilities"]["chain_params"]
          if p["key"] not in ("model_prev", "model_next")]   # the model buttons: triggers, not settings
DEFAULTS = {p["key"]: (p["options"][p["default"]] if p["type"] == "enum" else p["default"]) for p in PARAMS}


def patch(n):
    return (n + 0.5) / 32


def env(n, a, d, s, r):
    return {"env%d_attack" % n: a, "env%d_decay" % n: d, "env%d_sustain" % n: s, "env%d_release" % n: r}


def ping(decay, color=0.55):
    return {"amp_mode": "Ping", "lpg_decay": decay, "lpg_color": color}


ENV2_CUTOFF = {"asg_src1": "Env 2", "asg_1_3": 0.45}   # ASSIGN row 1: ENV 2 into CUTOFF (column 3)
MW_FM = {"asg_4_1": 0.25}                               # ASSIGN row 4: the mod wheel into FM (column 1)

PRESETS = [
    ("Init", {}),
    ("Analog Bass", dict(model="Virtual Analog", pitch=-12.0, harmonics=0.3, timbre=0.4, morph=0.65, amp_mode="Env",
                         voice_mode="Mono", filter_cutoff=0.35, filter_resonance=0.3, **env(1, 2, 350, 0.6, 150),
                         **env(2, 2, 250, 0.0, 150), **ENV2_CUTOFF)),
    ("Acid Line", dict(model="VA VCF", pitch=-12.0, harmonics=0.7, timbre=0.3, morph=0.3, amp_mode="Env",
                       voice_mode="Legato", glide_ms=60, mod_env2_timbre=0.45, **env(1, 1, 200, 0.5, 80),
                       **env(2, 1, 220, 0.0, 100))),
    ("Poly Brass", dict(model="Virtual Analog", harmonics=0.5, timbre=0.35, morph=0.8, amp_mode="Env", unison=2,
                        detune=0.15, spread=0.4, filter_cutoff=0.7, mod_env2_timbre=0.3, **env(1, 60, 500, 0.75, 350),
                        **env(2, 30, 600, 0.2, 300), **MW_FM)),
    ("Super Saw Pad", dict(model="Virtual Analog", harmonics=0.75, timbre=0.2, morph=0.9, amp_mode="Env", unison=4,
                           detune=0.35, spread=0.8, filter_cutoff=0.75, lfo1_rate=0.2, mod_lfo1_timbre=0.15,
                           **env(1, 700, 1200, 0.85, 2000))),
    ("Phase Keys", dict(model="Phase Distortion", harmonics=0.4, timbre=0.45, morph=0.6, amp_mode="Env",
                        mod_env2_timbre=0.35, **env(1, 2, 900, 0.35, 500), **env(2, 1, 600, 0.0, 400))),
    ("E.Piano", dict(model="6-Op FM II", harmonics=patch(0))),
    ("Marimba", dict(model="6-Op FM II", harmonics=patch(17))),
    ("Tubular Bells", dict(model="6-Op FM II", harmonics=patch(22))),
    ("Hammond", dict(model="6-Op FM III", harmonics=patch(1))),
    ("Full Strings", dict(model="6-Op FM III", harmonics=patch(27))),
    ("FM Brass", dict(model="6-Op FM III", harmonics=patch(29))),
    ("Solid Bass", dict(model="6-Op FM I", harmonics=patch(0), voice_mode="Mono")),
    ("Terrain Pad", dict(model="Wave Terrain", harmonics=0.4, timbre=0.6, morph=0.3, amp_mode="Env", unison=2,
                         detune=0.08, spread=0.5, lfo1_shape="Smooth", lfo1_rate=0.12, mod_lfo1_timbre=0.3,
                         lfo2_rate=0.07, asg_dst1="Morph", asg_1_1=0.25, **env(1, 400, 800, 0.8, 1500))),
    ("String Machine", dict(model="String Machine", harmonics=0.5, timbre=0.6, morph=0.5, amp_mode="Env", spread=0.6,
                            **env(1, 250, 600, 0.9, 1400))),
    ("Chip Arp", dict(model="Chiptune", harmonics=0.5, timbre=0.3, morph=0.5, trig_rate="1/16")),
    ("Fold Bass", dict(model="Waveshaping", pitch=-12.0, harmonics=0.4, timbre=0.6, morph=0.5, voice_mode="Mono",
                       amp_mode="Env", **env(1, 1, 400, 0.35, 120))),
    ("FM Bell", dict(model="FM 2-Op", harmonics=0.65, timbre=0.55, morph=0.3, **ping(0.8, 0.75))),
    ("Formant Choir", dict(model="Grain Formant", harmonics=0.5, timbre=0.5, morph=0.6, amp_mode="Env", unison=3,
                           detune=0.12, spread=0.7, lfo1_rate=0.25, mod_lfo1_morph=0.2,
                           **env(1, 300, 600, 0.8, 1200))),
    ("Additive Organ", dict(model="Harmonic", harmonics=0.5, timbre=0.7, morph=0.2)),
    ("Wavetable Sweep", dict(model="Wavetable", harmonics=0.2, timbre=0.6, morph=0.5, amp_mode="Env",
                             cycle_attack_ms=800, cycle_decay_ms=800, mod_cyc_timbre=0.35,
                             **env(1, 30, 800, 0.7, 900))),
    ("Chord Stab", dict(model="Chords", harmonics=0.35, timbre=0.5, morph=0.4, amp_mode="Env",
                        **env(1, 1, 600, 0.0, 300))),
    ("Talking Synth", dict(model="Speech", harmonics=0.5, timbre=0.5, morph=0.5, lfo1_rate=0.6, mod_lfo1_morph=0.45)),
    ("Swarm Pad", dict(model="Swarm", harmonics=0.5, timbre=0.6, morph=0.4, amp_mode="Env",
                       **env(1, 900, 1500, 0.9, 2500))),
    ("Particle Rain", dict(model="Particle", harmonics=0.6, timbre=0.55, morph=0.5)),
    ("Noise Sweep", dict(model="Noise", harmonics=0.5, timbre=0.6, morph=0.5, amp_mode="Env", lfo1_rate=0.3,
                         mod_lfo1_timbre=0.3, **env(1, 200, 800, 0.8, 1000))),
    ("Plucked String", dict(model="Inharm. String", harmonics=0.4, timbre=0.6, morph=0.65)),   # Gate: rings while held
    ("Modal Bell", dict(model="Modal Resonator", harmonics=0.5, timbre=0.6, morph=0.6, **ping(0.9))),
    ("Kick", dict(model="Bass Drum", harmonics=0.3, timbre=0.4, morph=0.5, **ping(0.9))),
    ("Snare", dict(model="Snare Drum", harmonics=0.5, timbre=0.5, morph=0.5, **ping(0.9))),
    ("Hi-Hat", dict(model="Hi-Hat", harmonics=0.5, timbre=0.6, morph=0.3, **ping(0.9))),
]

# VOLUME per preset from dev-tools/presets/levels.sh, so each preset's loudest 400 ms is about -16 dBFS
LEVEL = {
    'Init': 0.977,
    'Analog Bass': 0.822,
    'Acid Line': 2.0,
    'Poly Brass': 0.347,
    'Super Saw Pad': 0.209,
    'Phase Keys': 0.759,
    'E.Piano': 0.841,
    'Marimba': 1.0,
    'Tubular Bells': 1.445,
    'Hammond': 0.912,
    'Full Strings': 0.767,
    'FM Brass': 0.933,
    'Solid Bass': 2.0,
    'Terrain Pad': 0.427,
    'String Machine': 2.0,
    'Chip Arp': 0.596,
    'Fold Bass': 1.905,
    'FM Bell': 1.884,
    'Formant Choir': 0.767,
    'Additive Organ': 2.0,
    'Wavetable Sweep': 2.0,
    'Chord Stab': 1.841,
    'Talking Synth': 1.245,
    'Swarm Pad': 1.841,
    'Particle Rain': 1.122,
    'Noise Sweep': 1.175,
    'Plucked String': 2.0,
    'Modal Bell': 1.429,
    'Kick': 2.0,
    'Snare': 2.0,
    'Hi-Hat': 2.0,
}


def main():
    options = {p["key"]: p.get("options") for p in PARAMS}
    out = []
    names = set()
    for name, changes in PRESETS:
        assert name not in names and len(name) <= 24, name
        names.add(name)
        unknown = set(changes) - set(DEFAULTS)
        assert not unknown, (name, unknown)
        for k, v in changes.items():
            assert not options[k] or v in options[k], (name, k, v)
        values = dict(DEFAULTS, **changes)
        values["volume"] = LEVEL.get(name, 1.0)
        ordered = {"model": values.pop("model")}
        ordered.update(values)
        out.append({"name": name, "values": ordered})
    with open(os.path.join(PORT, "presets.json"), "w") as f:
        json.dump({"presets": out}, f, indent=1)
        f.write("\n")
    print("%d presets" % len(out))


if __name__ == "__main__":
    main()
