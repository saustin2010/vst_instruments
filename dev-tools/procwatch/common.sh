# Shared by the procwatch scripts: where things are, and plugin lookups by .so name (acid303, hushone, ...).
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
W=${PROCWATCH_WORK:-$HERE/work}        # binaries, the fake /sdcard, native builds, results (ignored by git)
MV=$REPO/framework/mpc-vst-plugins      # the framework checkout (framework/setup.sh): native.sh builds against it
UIDGID="$(id -u):$(id -g)"

plugin_vst_jsons() { (cd "$REPO" && ls schwung/*/*/vst.json mutable-instruments/*/vst.json vcv-rack/*/vst.json \
    originals/*/vst.json mpc-ports/*/vst.json 2>/dev/null); }
# so_name <plugin dir>: its .so name without .so
so_name() { sed -n 's/.*"so": *"\([^"]*\)\.so".*/\1/p' "$REPO/$1/vst.json" | head -n 1; }
all_so_names() { for j in $(plugin_vst_jsons); do so_name "$(dirname "$j")"; done | sort; }
# plugin_dir <so name>: the plugin folder, relative to the repo
plugin_dir() {
    local j
    for j in $(plugin_vst_jsons); do
        [ "$(so_name "$(dirname "$j")")" = "$1" ] && { dirname "$j"; return 0; }
    done
    echo "no plugin builds $1.so (names are the .so names: acid303, hushone, ...)" >&2
    return 1
}
is_sequencer() { grep -q '"category": *"Sequencer"' "$REPO/$(plugin_dir "$1")/vst.json"; }
