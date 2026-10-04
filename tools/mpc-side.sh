#!/bin/sh
# The half of install.sh / uninstall.sh that runs ON the MPC (as root, sent over SSH: `ssh root@mpc sh -s -- <cmd> ...
# < tools/mpc-side.sh`). Plain POSIX sh for the MPC's own shell. You can also copy it to the MPC and run it there.
#
#   check                              is this an MPC OS device we can install on? (prints what it finds)
#   listed <so path> ...               print "<so path>|<name>|<manufacturer>|<isInstrument>|<category>" per entry
#   place <stage dir> <plugin> ...     move staged plugins (<stage dir>/<plugin>/{vst,Synths}, checked against their
#                                      SHA256SUMS) into /sdcard/vst and /sdcard/Synths
#   register <entries.xml>             add/replace these <PLUGIN/> entries in MPC.settings      } stop MPC, back up
#   unregister <so path> ... [-- <path to delete> ...]   drop entries, then delete files    } MPC.settings, edit,
#                                                                                              } check, start MPC
# MPC_ROOT (a folder standing in for /) and MPC_SYSTEMCTL let it be tried on a computer.
set -eu
R=${MPC_ROOT:-}
SYSTEMCTL=${MPC_SYSTEMCTL:-systemctl}
VST=$R/sdcard/vst
SYNTHS=$R/sdcard/Synths
HERE=$(cd "$(dirname "$0")" 2>/dev/null && pwd || echo /tmp)

die() { echo "error: $*" >&2; exit 1; }
settings() { ls "$R"/media/az01-internal/Settings/*/MPC.settings 2>/dev/null | head -n 1; }

check() {
    [ "$(id -u)" = 0 ] || [ -n "$R" ] || die "not root (log in as root@...)"
    a=$(uname -m)
    case "$a" in armv7*|armhf) ;; *) [ -n "$R" ] || die "these plugins are 32-bit ARM builds for Gen1 MPC OS devices; this one is $a" ;; esac
    s=$(settings); [ -n "$s" ] || die "MPC.settings not found under /media/az01-internal/Settings (not an MPC OS device?)"
    [ -d "$R/sdcard" ] || die "/sdcard not found"
    for t in awk sha256sum tar; do command -v $t >/dev/null || die "$t not found on the device"; done
    free=$(df -kP "$R/sdcard" | awk 'NR == 2 { print int($4 / 1024) }')
    echo "ok: $a, $s, ${free} MB free on /sdcard"
}

listed() {
    s=$(settings)
    for so in "$@"; do
        awk -v so="$so" '
            /<PLUGIN[ \t]/ || /<PLUGIN$/ { buf = $0; open = 1 }
            open && !/<PLUGIN/ { buf = buf " " $0 }
            open && /\/>/ {
                open = 0
                if (index(buf, "file=\"" so "\"")) { found = buf }
            }
            function attr(n,   i, r) { i = index(found, " " n "=\""); if (!i) return ""; r = substr(found, i + length(n) + 3); return substr(r, 1, index(r, "\"") - 1) }
            END { if (found != "") printf "%s|%s|%s|%s|%s\n", so, attr("name"), attr("manufacturer"), attr("isInstrument"), attr("category"); else printf "%s||||\n", so }
        ' "$s"
    done
}

# swap a staged folder in for an installed one (skins: they're entirely ours)
swap_dir() {   # swap_dir <staged dir> <destination dir>
    rm -rf "$2.old"
    if [ -e "$2" ]; then mv "$2" "$2.old"; fi
    mv "$1" "$2"
    rm -rf "$2.old"
}

place() {
    stage=$1; shift
    mkdir -p "$VST" "$SYNTHS"
    for p in "$@"; do
        d=$stage/$p
        [ -d "$d" ] || die "$p was not staged"
        (cd "$d" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || die "$p: files damaged on the way (SHA256SUMS): run install again"
        for f in "$d"/vst/*.so; do
            [ -e "$f" ] || continue
            mv "$f" "$VST/$(basename "$f")"
            echo "  $p: /sdcard/vst/$(basename "$f")"
        done
        for f in "$d"/vst/*/; do   # data folders (presets, kits, wavetables...): merged, so files you added stay
            [ -d "$f" ] || continue
            n=$(basename "$f")
            mkdir -p "$VST/$n"
            cp -R "$f." "$VST/$n/"
            echo "  $p: /sdcard/vst/$n/"
        done
        for f in "$d"/Synths/*/; do
            [ -d "$f" ] || continue
            n=$(basename "$f")
            swap_dir "${f%/}" "$SYNTHS/$n"
            echo "  $p: /sdcard/Synths/$n/"
        done
        rm -rf "$d"
    done
    rmdir "$stage" 2>/dev/null || true
    sync
}

stop_mpc() {
    echo "stopping MPC"
    $SYSTEMCTL stop acvs
    trap 'echo "starting MPC"; $SYSTEMCTL start acvs' EXIT
    i=0
    while pidof MPC >/dev/null 2>&1 && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
    if pidof MPC >/dev/null 2>&1; then die "MPC did not stop; nothing changed"; fi
}

# edit_list <awk variable assignment>: edit a copy of MPC.settings, check it, keep a backup, swap it in
edit_list() {
    s=$(settings); [ -n "$s" ] || die "MPC.settings not found"
    awk "$@" -f "$AWK" "$s" > "$s.new" || { rm -f "$s.new"; die "editing MPC.settings failed; nothing changed"; }
    # sanity checks before anything is replaced: same document end, every added file listed exactly once
    tail -n 3 "$s.new" | grep -q '</PROPERTIES>' || { rm -f "$s.new"; die "edited MPC.settings looks truncated; nothing changed"; }
    for so in $CHECK_ONCE; do
        n=$(grep -c "file=\"$so\"" "$s.new" || true)
        [ "$n" = 1 ] || { rm -f "$s.new"; die "$so listed $n times after the edit; nothing changed"; }
    done
    for so in $CHECK_GONE; do
        if grep -q "file=\"$so\"" "$s.new"; then rm -f "$s.new"; die "$so still listed after the edit; nothing changed"; fi
    done
    bak=$s.bak-vst_instruments-$(date +%Y%m%d-%H%M%S)
    cp "$s" "$bak"
    mv "$s.new" "$s"
    sync
    echo "MPC.settings updated (backup: $bak)"
}

need_awk() {
    AWK=${MPC_PLUGIN_LIST_AWK:-$HERE/plugin_list.awk}
    [ -f "$AWK" ] || die "plugin_list.awk not found (looked for $AWK)"
}

register() {
    need_awk
    [ -f "$1" ] || die "no entries file $1"
    CHECK_ONCE=$(sed -n 's/.*file="\([^"]*\)".*/\1/p' "$1")
    CHECK_GONE=""
    stop_mpc
    edit_list -v add="$1"
}

unregister() {
    need_awk
    sos=""; paths=""; past=0
    for a in "$@"; do
        if [ "$a" = -- ]; then past=1; elif [ $past = 0 ]; then sos="$sos $a"; else paths="$paths
$a"; fi
    done
    CHECK_ONCE=""; CHECK_GONE=$sos
    stop_mpc
    edit_list -v remove="$sos"
    echo "$paths" | while IFS= read -r f; do
        [ -n "$f" ] || continue
        case "$f" in /sdcard/vst/?*|/sdcard/Synths/?*) ;; *) echo "  skipped $f (not under /sdcard/vst or /sdcard/Synths)"; continue ;; esac
        if [ -e "$R$f" ]; then rm -rf "$R$f"; echo "  removed $f"; fi
    done
    sync
}

# rmskins <file>: remove the screen folders a renamed plugin left (one /sdcard/Synths/<maker> - VST - <name> per line)
rmskins() {
    while IFS= read -r f; do
        case "$f" in */../*|*/./*) echo "  skipped $f"; continue ;; esac
        case "$f" in "/sdcard/Synths/"?*" - VST - "?*) ;; *) echo "  skipped $f (not a plugin screen folder)"; continue ;; esac
        if [ -d "$R$f" ]; then rm -rf "$R$f"; echo "  removed the old screen folder $f"; fi
    done < "$1"
    sync
}

cmd=${1:-}; [ $# -gt 0 ] && shift
case "$cmd" in
    check) check ;;
    listed) listed "$@" ;;
    place) place "$@" ;;
    register) register "$@" ;;
    unregister) unregister "$@" ;;
    rmskins) rmskins "$@" ;;
    *) sed -n '2,14p' "$0" 2>/dev/null || true; exit 1 ;;
esac
