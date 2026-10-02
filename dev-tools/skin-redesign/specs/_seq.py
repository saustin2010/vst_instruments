"""Shared bits for the sequencer ports: Schwung MIDI FX modules run through steve/tools/midifx/schwung_midi_fx.c,
which plays their notes out of the plugin's own ALSA MIDI port (MPC OS ignores VST MIDI output)."""


def info(name, slots=4, last="PLAY NOTES INTO THIS TRACK"):
    """A panel that says where the notes go: the plugin's port, picked as another track's MIDI input."""
    return ("@info:MIDI OUT|PLAYS OTHER TRACKS|PORT: %s MIDI OUT|PREFS > MIDI: TURN ON 'TRACK' FOR IT"
            "|OTHER TRACK: MIDI IN = THAT PORT|%s" % (name.upper(), last), slots)


def tidy(p, text_options=False):
    """Whole-number display (and whole-unit Q-Link steps) for the module's int params; option labels upper-cased.
    text_options: the module only parses its own option words (strcmp), so keep them as the "send" strings."""
    for k, v in list(p.byk.items()):
        opts = v.get("options")
        if opts:
            pretty = [str(o).upper().replace("_", " ") for o in opts]
            p.set(k, type=None, options=pretty, send=[str(o) for o in opts] if text_options else None)
        elif v.get("type") == "int":
            p.set(k, type=None, display="int")
    return p


GREEN_LED = dict(on="#7cf29a", off="#163a20", cap=("#4a5450", "#353c39", "#232826"), rim="#6f7a75", well="#060807")
