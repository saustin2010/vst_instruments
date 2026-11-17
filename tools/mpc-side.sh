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
#   vst2 [--apply] [--force] [<map>]   MPC OS 3.10.1+: copy every plugin MPC.settings lists into /sdcard/VST2 Plugins/
#                                      <maker> - VST - <name>/ with a plugin.json (a plan without --apply). Nothing is
#                                      deleted, MPC.settings isn't touched, MPC keeps running (restart it afterwards)
#   vst2retire [--apply] [--undo]      once the copies work: set the old /sdcard/Synths skins aside (MPC 3.10.1 would draw
#                                      them instead of the copies'); --undo puts them back (before going back to 3.9.1)
#   vst2labels [--apply] [--undo]      give the copies the names MPC.settings lists for their uid (labels like [SYN]), in
#                                      plugin.json only; --undo puts back the names the binaries report
#   vst2mode                           on / off / none: MPC OS 3.10.1's External Plugin Support setting (none = older OS)
#   vst2state <so path> ...            print "<so path>|<VST2 Plugins folder holding that binary>|<1: an older copy is there>"
#   place_vst2 <stage dir> <plugin> ...  MPC OS 3.10.1+: install staged plugins as /sdcard/VST2 Plugins/<maker> - VST -
#                                      <name>/ (MPC keeps running; it lists new ones after a restart)
#   vst2remove <list file> [--purge]   remove these VST2 Plugins folders (one name per line), keeping data unless --purge
#   restart                            restart MPC
# MPC_ROOT (a folder standing in for /) and MPC_SYSTEMCTL let it be tried on a computer.
set -eu
R=${MPC_ROOT:-}
SYSTEMCTL=${MPC_SYSTEMCTL:-systemctl}
VST=$R/sdcard/vst
SYNTHS=$R/sdcard/Synths
VST2="$R/sdcard/VST2 Plugins"
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

# ---- MPC OS 3.10.1: the VST2 Plugins folder ----
# 3.10.1's plugin browser lists only <drive>/VST2 Plugins/<maker> - VST - <name>/ folders holding a plugin.json and their
# binary; plugins registered in MPC.settings are hidden from it, though saved projects still load them from their old path
# (and from the copy once the old file is gone). The folder name must be the maker and name the binary reports: MPC finds
# the skin by them. Our layout keeps the binary in /sdcard/vst and the skin in /sdcard/Synths; other authors' releases
# keep both in the Synths folder.

# entries: one line per <PLUGIN/> of pluginList-arm: file|name|manufacturer|uid|isInstrument|numInputs|numOutputs|category|version
entries() {
    awk '
        function a(n,   i, r) {
            i = index(buf, " " n "=\""); if (!i) return ""
            r = substr(buf, i + length(n) + 3); r = substr(r, 1, index(r, "\"") - 1)
            gsub(/&quot;/, "\"", r); gsub(/&lt;/, "<", r); gsub(/&gt;/, ">", r); gsub(/&amp;/, "\\&", r)
            return r
        }
        { sub(/\r$/, "") }
        index($0, "<VALUE name=\"pluginList-arm\"") { on = 1; next }
        on && index($0, "</VALUE>") { on = 0 }
        !on { next }
        index($0, "<PLUGIN") { buf = ""; open = 1 }
        open { buf = buf " " $0 }
        open && index($0, "/>") {
            open = 0
            printf "%s|%s|%s|%s|%s|%s|%s|%s|%s\n", a("file"), a("name"), a("manufacturer"), a("uid"), a("isInstrument"),
                a("numInputs"), a("numOutputs"), a("category"), a("version")
        }
    ' "$1"
}

jstr() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'; }

# data_dirs <so name>: its data folders in /sdcard/vst (MODULE_SUBDIR / MODULE_DIR, from the map; else <so name without .so>)
data_dirs() {
    dd=""
    [ -n "$MAP" ] && dd=$(awk -v so="$1" '$1 == so { print $2 }' "$MAP")
    [ -n "$dd" ] || dd=${1%.so}
    for x in $dd; do if [ -d "$VST/$x" ]; then echo "$x"; fi; done
}

# synths_folder <name> <manufacturer>: the /sdcard/Synths/<maker> - VST - <name> folder of a plugin (compared as text: names
# hold [ ] that a glob would read as patterns; MPC writes accented makers as &#NNN; so the name decides, the maker breaks ties)
synths_folder() {
    hit=""; n=0
    for d in "$SYNTHS"/*/; do
        d=${d%/}; f=${d##*/}
        case "$f" in *" - VST - "*) ;; *) continue ;; esac
        [ "${f#* - VST - }" = "$1" ] || continue
        if [ "$f" = "$2 - VST - $1" ]; then echo "$d"; return; fi
        hit=$d; n=$((n + 1))
    done
    [ $n = 1 ] && echo "$hit"
}

# so_folder <file> <so name>: the Synths folder that holds a portable plugin's binary
so_folder() {
    if [ -f "$R$1" ]; then dirname "$R$1"; return; fi   # the path as written (no &#NNN; in it)
    hit=""; n=0
    for d in "$SYNTHS"/*/; do d=${d%/}; if [ -f "$d/$2" ]; then hit=$d; n=$((n + 1)); fi; done
    [ $n = 1 ] && echo "$hit"
}

write_json() {   # write_json <file>   (uses the loop's variables)
    kind=instrument; [ "$isi" = 0 ] && kind=effect
    {
        printf '{\n  "name": "%s",\n  "manufacturer": "%s",\n' "$(jstr "$pname")" "$(jstr "$pmfr")"
        printf '  "uid": "0x%s",\n  "format": "vst2",\n  "kind": "%s",\n' "$uid" "$kind"
        [ -n "$ver" ] && printf '  "version": "%s",\n' "$(jstr "$ver")"
        [ -n "$cat" ] && printf '  "category": "%s",\n' "$(jstr "$cat")"
        printf '  "numInputs": %s,\n  "numOutputs": %s,\n  "file32": "%s"\n}\n' "${nin:-0}" "${nout:-2}" "$(jstr "$so")"
    } > "$1"
}

copy_tree() { (cd "$1" && tar -cf - .) | (cd "$2" && tar -xf -); }   # contents of $1 into $2

vst2() {
    APPLY=0; FORCE=0; MAP=""
    for a in "$@"; do case "$a" in --apply) APPLY=1 ;; --force) FORCE=1 ;; *) MAP=$a ;; esac; done
    [ -z "$MAP" ] || [ -f "$MAP" ] || die "no map file $MAP"
    s=$(settings); [ -n "$s" ] || die "MPC.settings not found"
    if grep -q 'Enhancements.VST2Support" val="1"' "$s"; then echo "External Plugin Support: on"
    elif grep -q 'Enhancements.VST2Support' "$s"; then echo "External Plugin Support: OFF (Preferences > Enhancements): MPC won't list the copies until it's on"
    else
        echo "This MPC OS has no External Plugin Support setting (older than 3.10.1): MPC would not use the copies"
        [ $APPLY = 0 ] || [ $FORCE = 1 ] || die "not copying on this MPC OS (--force to copy anyway)"
    fi
    list=$(mktemp); entries "$s" > "$list"
    free=$(df -kP "$R/sdcard" | awk 'NR == 2 { print $4 }')
    nc=0; nh=0; ns=0
    while IFS='|' read -r file name mfr uid isi nin nout cat ver; do
        so=${file##*/}
        case "$uid" in *[!0-9a-fA-F]*|'') echo "SKIP  $name: uid \"$uid\" isn't hex"; ns=$((ns + 1)); continue ;; esac
        case "$file" in
            /sdcard/vst/*.so)   # this repo's layout
                src=$(synths_folder "$name" "$mfr"); data=$(data_dirs "$so")
                [ -f "$R$file" ] || { echo "SKIP  $name: $file is missing"; ns=$((ns + 1)); continue; }
                [ -n "$src" ] || { echo "SKIP  $name: no single /sdcard/Synths/<maker> - VST - $name folder (its skin)"; ns=$((ns + 1)); continue; } ;;
            *)
                src=$(so_folder "$file" "$so"); data=""
                [ -n "$src" ] || { echo "SKIP  $name: can't find the folder holding $so"; ns=$((ns + 1)); continue; } ;;
        esac
        folder=${src##*/}
        case "$folder" in *" - VST - "*) ;; *) echo "SKIP  $name: ${src#"$R"} isn't a <maker> - VST - <name> folder"; ns=$((ns + 1)); continue ;; esac
        pmfr=${folder%% - VST - *}; pname=${folder#* - VST - }
        dest="$VST2/$folder"
        if [ -f "$dest/plugin.json" ]; then echo "HAVE  $folder"; nh=$((nh + 1)); continue; fi
        if [ -e "$dest" ]; then echo "SKIP  $folder: ${dest#"$R"} exists without a plugin.json"; ns=$((ns + 1)); continue; fi
        need=$(du -sk "$src" | cut -f1)
        case "$file" in /sdcard/vst/*) need=$((need + $(du -sk "$R$file" | cut -f1))); for x in $data; do need=$((need + $(du -sk "$VST/$x" | cut -f1))); done ;; esac
        [ "$need" -lt $((free * 9 / 10)) ] || { echo "SKIP  $folder: needs $need KB, $free KB free"; ns=$((ns + 1)); continue; }
        what=""; [ "$pname" = "$name" ] || what=" (listed as \"$name\": MPC will show \"$pname\", the name the binary reports)"
        case "$file" in /sdcard/vst/*) what="$what from /sdcard/vst/$so${data:+ + data: $(echo $data)} and its skin" ;; esac
        echo "COPY  $folder$what, $need KB"
        nc=$((nc + 1))
        [ $APPLY = 1 ] || continue
        mkdir -p "$VST2"; rm -rf "$dest.new"; mkdir "$dest.new"
        ok=1
        copy_tree "$src" "$dest.new" || ok=0
        rm -rf "$dest.new/Plugin Skins.vst2-moved"
        case "$file" in /sdcard/vst/*)
            cp "$R$file" "$dest.new/$so" || ok=0
            for x in $data; do rm -rf "$dest.new/$x"; mkdir "$dest.new/$x" && copy_tree "$VST/$x" "$dest.new/$x" || ok=0; done ;;
        esac
        [ -f "$dest.new/$so" ] || ok=0
        [ -f "$dest.new/plugin.json" ] || write_json "$dest.new/plugin.json" || ok=0
        if [ $ok = 1 ] && mv "$dest.new" "$dest"; then free=$((free - need))
        else echo "FAIL  $folder: copy failed, nothing left behind" >&2; rm -rf "$dest.new"; fi
    done < "$list"
    rm -f "$list"
    sync
    echo "$nc to copy, $nh already there, $ns skipped$([ $APPLY = 1 ] || echo " (a plan: nothing changed)")"
}

vst2retire() {
    APPLY=0; UNDO=0
    for a in "$@"; do case "$a" in --apply) APPLY=1 ;; --undo) UNDO=1 ;; esac; done
    n=0
    for v in "$VST2"/*/; do
        v=${v%/}; f=${v##*/}; o="$SYNTHS/$f"
        [ -f "$v/plugin.json" ] || continue
        if [ $UNDO = 0 ]; then
            [ -d "$v/Plugin Skins" ] && [ -d "$o/Plugin Skins" ] || continue
            echo "SKIN  $f: /sdcard/Synths copy's skin set aside"
            [ $APPLY = 1 ] && { rm -rf "$o/Plugin Skins.vst2-moved"; mv "$o/Plugin Skins" "$o/Plugin Skins.vst2-moved"; }
        else
            [ -d "$o/Plugin Skins.vst2-moved" ] && [ ! -e "$o/Plugin Skins" ] || continue
            echo "SKIN  $f: /sdcard/Synths copy's skin put back"
            [ $APPLY = 1 ] && mv "$o/Plugin Skins.vst2-moved" "$o/Plugin Skins"
        fi
        n=$((n + 1))
    done
    sync
    echo "$n skins$([ $APPLY = 1 ] || echo " (a plan: nothing changed)")"
}

# vst2labels: the browser lists a VST2 Plugins copy by its plugin.json "name", which vst2 copied from the binary, so names
# changed only in MPC.settings (the [SYN], [FX] ... labels on other authors' plugins) are lost. This writes the name
# MPC.settings lists for the same uid into plugin.json. The folder keeps the binary's name: MPC finds the skin by the strings
# the loaded binary reports. The first rewrite keeps the original as plugin.json.vst2-orig, which --undo puts back.
vst2labels() {
    APPLY=0; UNDO=0
    for a in "$@"; do case "$a" in --apply) APPLY=1 ;; --undo) UNDO=1 ;; esac; done
    s=$(settings); [ -n "$s" ] || die "MPC.settings not found"
    list=$(mktemp); entries "$s" > "$list"
    n=0
    for v in "$VST2"/*/; do
        v=${v%/}; f=${v##*/}; j="$v/plugin.json"
        [ -f "$j" ] || continue
        if [ $UNDO = 1 ]; then
            [ -f "$j.vst2-orig" ] || continue
            echo "NAME  $f: back to the name its binary reports"
            if [ $APPLY = 1 ]; then mv "$j.vst2-orig" "$j"; fi
            n=$((n + 1)); continue
        fi
        rm -f "$j.new"
        # prints "<old name> -> <new name>" when they differ (and with apply=1 writes plugin.json.new), "SKIP ..." when unsure.
        # Characters MPC.settings holds as &#NNN; are written as UTF-8 (as the copies' names already are), or as \uXXXX
        # escapes when this awk can't make single bytes (u8 = "é" in UTF-8, to compare with)
        out=$(awk -v list="$list" -v new="$j.new" -v apply=$APPLY -v u8="$(printf '\303\251')" '
            function hexnorm(h) { h = tolower(h); sub(/^0x/, "", h); sub(/^0+/, "", h); return h == "" ? "0" : h }
            function hex2(c) { return substr(HEX, int(c / 16) + 1, 1) substr(HEX, c % 16 + 1, 1) }
            function uidof(v,   d, h, i) {   # plugin.json uid: "0x..." hex, decimal, or a four-character code
                if (v ~ /^0[xX][0-9a-fA-F]+$/) return hexnorm(v)
                if (v ~ /^-?[0-9]+$/) {
                    d = v + 0; if (d < 0) d += 4294967296; h = ""
                    while (d > 0) { h = substr(HEX, d % 16 + 1, 1) h; d = int(d / 16) }
                    return h == "" ? "0" : h
                }
                if (length(v) == 4) { h = ""; for (i = 1; i <= 4; i++) h = h hex2(ORD[substr(v, i, 1)]); return hexnorm(h) }
                return ""
            }
            function jesc(s,   r, i, c, k, t) {   # MPC.settings text (XML numeric entities left in) -> JSON string body
                r = ""
                for (i = 1; i <= length(s); i++) {
                    c = substr(s, i, 1)
                    if (c == "&" && match(substr(s, i), /^&#[0-9]+;/)) {
                        k = substr(s, i + 2, RLENGTH - 3) + 0
                        if (RAW && k >= 128 && k < 1114112) {
                            if (k < 2048) t = B[192 + int(k / 64)] B[128 + k % 64]
                            else if (k < 65536) t = B[224 + int(k / 4096)] B[128 + int(k / 64) % 64] B[128 + k % 64]
                            else t = B[240 + int(k / 262144)] B[128 + int(k / 4096) % 64] B[128 + int(k / 64) % 64] B[128 + k % 64]
                        } else if (k > 65535) { k -= 65536; t = sprintf("\\u%04x\\u%04x", 55296 + int(k / 1024), 56320 + k % 1024) }
                        else t = sprintf("\\u%04x", k)
                        r = r t; i += RLENGTH - 1; continue
                    }
                    r = r ((c == "\\" || c == "\"") ? "\\" c : c)
                }
                return r
            }
            BEGIN {
                HEX = "0123456789abcdef"; for (i = 32; i < 127; i++) ORD[sprintf("%c", i)] = i
                for (i = 128; i < 256; i++) B[i] = sprintf("%c", i)
                RAW = (B[195] B[169] == u8)
                while ((getline l < list) > 0) {
                    split(l, x, "|"); u = hexnorm(x[4]); if (x[2] == "") continue
                    if (!(u in nm)) nm[u] = x[2]; else if (nm[u] != x[2]) two[u] = two[u] " / " x[2]
                }
            }
            { t = (NR == 1) ? $0 : t "\n" $0 }
            END {
                if (!match(t, /"uid"[ \t\r\n]*:[ \t\r\n]*("[^"]*"|-?[0-9]+)/)) exit
                v = substr(t, RSTART, RLENGTH); sub(/^"uid"[ \t\r\n]*:[ \t\r\n]*/, "", v); gsub(/"/, "", v)
                u = uidof(v); if (u == "" || !(u in nm)) exit
                if (u in two) { print "SKIP: MPC.settings lists it as " nm[u] two[u]; exit }
                if (!match(t, /"name"[ \t\r\n]*:[ \t\r\n]*"[^"]*"/)) { print "SKIP: no \"name\" in plugin.json"; exit }
                s = RSTART; e = RSTART + RLENGTH; old = substr(t, s, RLENGTH); sub(/^"name"[ \t\r\n]*:[ \t\r\n]*"/, "", old)
                old = substr(old, 1, length(old) - 1)
                if (old ~ /\\$/) { print "SKIP: its name holds a quote"; exit }
                lab = jesc(nm[u]); if (lab == old) exit
                print old " -> " lab
                if (apply == 1) { printf("%s\"name\": \"%s\"%s\n", substr(t, 1, s - 1), lab, substr(t, e)) > new; close(new) }
            }' "$j")
        case "$out" in
            "") ;;
            SKIP*) echo "SKIP  $f: ${out#SKIP: }" ;;
            *)
                echo "NAME  $f: $out"; n=$((n + 1))
                if [ $APPLY = 1 ]; then
                    if [ -s "$j.new" ]; then
                        [ -f "$j.vst2-orig" ] || cp "$j" "$j.vst2-orig"
                        mv "$j.new" "$j"
                    else echo "FAIL  $f: plugin.json not rewritten" >&2; rm -f "$j.new"; fi
                fi ;;
        esac
    done
    rm -f "$list"
    sync
    echo "$n names$([ $APPLY = 1 ] || echo " (a plan: nothing changed)")$([ $APPLY = 1 ] && [ $n -gt 0 ] && echo ": restart MPC to see them")"
}

# ---- MPC OS 3.10.1: installing into VST2 Plugins (install.sh, uninstall.sh) ----

vst2mode() {
    s=$(settings); [ -n "$s" ] || die "MPC.settings not found"
    if grep -q 'Enhancements.VST2Support" val="1"' "$s"; then echo on
    elif grep -q 'Enhancements.VST2Support' "$s"; then echo off
    else echo none; fi
}

vst2state() {
    for so in "$@"; do
        b=${so##*/}; hit=""
        for d in "$VST2"/*/; do
            if [ -f "$d$b" ] && [ -f "${d}plugin.json" ]; then d=${d%/}; hit=${d##*/}; break; fi
        done
        old=0; if [ -f "$R$so" ]; then old=1; fi
        for s in "$SYNTHS"/*/; do if [ -f "$s$b" ]; then old=1; fi; done   # a release zip's layout
        echo "$so|$hit|$old"
    done
}

uidn() {   # uidn <plugin.json>: its "0x..." uid in lower case without 0x and leading zeros ("" for other forms)
    sed -n 's/.*"uid"[ \t]*:[ \t]*"0[xX]\([0-9A-Fa-f]*\)".*/\1/p' "$1" | head -n 1 | tr 'A-F' 'a-f' | sed 's/^0*//'
}

# place_vst2: each staged plugin (<stage>/<plugin>/{vst,Synths}, plugin.json, VST_DATA, checked against SHA256SUMS) becomes
# /sdcard/VST2 Plugins/<its screen folder's name>/: the screen folder's contents (Plugin Skins swapped whole), the .so, its
# data folders (merged, so files you added stay) and plugin.json. An existing folder is updated in place; a new one is built
# beside it and moved in when complete. Also:
# - a binary still at its old path, /sdcard/vst/<so> (this repo's installer) or /sdcard/Synths/<folder>/<so> (a release
#   zip's), is updated there too, with its data folders: saved projects that name that path load it from there while it
#   exists. So are data folders the plugin reads from a fixed /sdcard/vst/<name> (VST_DATA).
# - an old /sdcard/Synths copy's skin is set aside: MPC 3.10.1 would draw it instead of the new one.
# - another copy of the same plugin (same uid, e.g. under an old name) is hidden from MPC by renaming its plugin.json.
# Ends with "new: <n>", the plugins MPC lists only after a restart.
place_vst2() {
    stage=$1; shift
    mkdir -p "$VST2"
    nnew=0
    for p in "$@"; do
        d=$stage/$p
        [ -d "$d" ] || die "$p was not staged"
        (cd "$d" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || die "$p: files damaged on the way (SHA256SUMS): run install again"
        src=""; n=0
        for f in "$d"/Synths/*/; do if [ -d "$f" ]; then src=${f%/}; n=$((n + 1)); fi; done
        [ $n = 1 ] || die "$p: expected one screen folder in its deploy/Synths, found $n"
        so=""; for f in "$d"/vst/*.so; do if [ -f "$f" ]; then so=${f##*/}; fi; done
        [ -n "$so" ] || die "$p: no .so staged"
        [ -f "$d/plugin.json" ] || die "$p: no plugin.json staged"
        F=${src##*/}; dest="$VST2/$F"
        if [ -f "$dest/plugin.json" ]; then work=$dest; new=0
        else
            rm -rf "$dest.new"; mkdir "$dest.new"; work=$dest.new; new=1
            if [ -d "$dest" ]; then copy_tree "$dest" "$work"; fi
        fi
        fixed=""; if [ -f "$d/VST_DATA" ]; then fixed=$(cat "$d/VST_DATA"); fi
        oldso=0; if [ -f "$VST/$so" ]; then oldso=1; fi
        oldsyn=0; if [ -f "$SYNTHS/$F/$so" ]; then oldsyn=1; fi
        for f in "$d"/vst/*/; do
            [ -d "$f" ] || continue
            x=$(basename "$f")
            mkdir -p "$work/$x"; cp -R "$f." "$work/$x/"
            also=$oldso; for y in $fixed; do if [ "$y" = "$x" ]; then also=1; fi; done
            if [ $also = 1 ]; then mkdir -p "$VST/$x"; cp -R "$f." "$VST/$x/"; echo "  $p: /sdcard/vst/$x/ (read from there too)"; fi
            if [ $oldsyn = 1 ]; then mkdir -p "$SYNTHS/$F/$x"; cp -R "$f." "$SYNTHS/$F/$x/"; fi
        done
        if [ $oldso = 1 ]; then
            cp "$d/vst/$so" "$VST/$so.new" && mv "$VST/$so.new" "$VST/$so"
            echo "  $p: /sdcard/vst/$so (saved projects that name it load it from there)"
        fi
        if [ $oldsyn = 1 ]; then
            cp "$d/vst/$so" "$SYNTHS/$F/$so.new" && mv "$SYNTHS/$F/$so.new" "$SYNTHS/$F/$so"
            echo "  $p: /sdcard/Synths/$F/$so and its data (saved projects that name it load it from there)"
        fi
        for f in "$src"/*; do
            [ -e "$f" ] || continue
            x=${f##*/}
            if [ -d "$f" ]; then swap_dir "$f" "$work/$x"; else mv "$f" "$work/$x"; fi
        done
        mv "$d/vst/$so" "$work/$so"
        mv "$d/plugin.json" "$work/plugin.json"
        rm -f "$work/plugin.json.vst2-orig"   # a vst2labels backup describes the binary just replaced
        if [ $new = 1 ]; then rm -rf "$dest"; mv "$work" "$dest"; nnew=$((nnew + 1)); fi
        echo "  $p: /sdcard/VST2 Plugins/$F/$([ $new = 1 ] && echo " (new)" || true)"
        if [ -d "$SYNTHS/$F/Plugin Skins" ]; then
            rm -rf "$SYNTHS/$F/Plugin Skins.vst2-moved"; mv "$SYNTHS/$F/Plugin Skins" "$SYNTHS/$F/Plugin Skins.vst2-moved"
            echo "  $p: the old screen in /sdcard/Synths/$F/ set aside (MPC 3.10.1 would draw it instead)"
        fi
        u=$(uidn "$dest/plugin.json")
        for o in "$VST2"/*/; do
            o=${o%/}
            if [ "$o" != "$dest" ] && [ -n "$u" ] && [ -f "$o/plugin.json" ] && [ "$(uidn "$o/plugin.json")" = "$u" ]; then
                mv "$o/plugin.json" "$o/plugin.json.replaced"
                echo "  $p: the older copy in ${o##*/}/ hidden from MPC (same plugin: plugin.json renamed plugin.json.replaced)"
            fi
        done
        rm -rf "$d"
    done
    rmdir "$stage" 2>/dev/null || true
    sync
    echo "new: $nnew"
}

vst2remove() {
    purge=0; if [ "${2:-}" = --purge ]; then purge=1; fi
    while IFS= read -r F; do
        case "$F" in ""|*/*|.*) continue ;; *?" - VST - "?*) ;; *) echo "  skipped $F (not a plugin folder name)"; continue ;; esac
        v="$VST2/$F"
        [ -d "$v" ] || continue
        if [ $purge = 1 ]; then rm -rf "$v"; echo "  removed /sdcard/VST2 Plugins/$F/"; continue; fi
        for x in "$v"/*; do
            [ -e "$x" ] || continue
            case "${x##*/}" in plugin.json*|*.so|"Plugin Skins"*|version.xml|mpc-plugin.json|plugin-meta.xml) rm -rf "$x" ;; esac
        done
        if rmdir "$v" 2>/dev/null; then echo "  removed /sdcard/VST2 Plugins/$F/"
        else echo "  removed /sdcard/VST2 Plugins/$F/ except its data folders (--purge removes them)"; fi
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
    vst2) vst2 "$@" ;;
    vst2retire) vst2retire "$@" ;;
    vst2labels) vst2labels "$@" ;;
    vst2mode) vst2mode ;;
    vst2state) vst2state "$@" ;;
    place_vst2) place_vst2 "$@" ;;
    vst2remove) vst2remove "$@" ;;
    restart) echo "restarting MPC"; $SYSTEMCTL restart acvs ;;
    *) sed -n '2,25p' "$0" 2>/dev/null || true; exit 1 ;;
esac
