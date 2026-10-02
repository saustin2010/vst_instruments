# Edits the plugin list in MPC.settings (the <VALUE name="pluginList-arm"><KNOWNPLUGINS> block). Plain POSIX awk, so
# it runs with the MPC's own BusyBox awk. Run it with MPC stopped, on a copy, and check the result before swapping it in
# (install.sh / tools/mpc-side.sh do all of that).
#
#   awk -v add=entries.xml -f plugin_list.awk MPC.settings > MPC.settings.new
#       entries.xml: one <PLUGIN .../> element per line. Any entry already in the list for the same file= is dropped
#       first, so running it again updates an entry instead of adding a second one.
#   awk -v remove="/sdcard/vst/a.so /sdcard/vst/b.so" -f plugin_list.awk MPC.settings > MPC.settings.new
#       drops the entries for those files.
#
# MPC writes the list with one element per line, but a long <PLUGIN> can wrap its attributes over several lines, so
# an element is collected up to its "/>" before deciding whether to keep it.

function file_of(s,    i, r) {
    i = index(s, "file=\"")
    if (i == 0) return ""
    r = substr(s, i + 6)
    return substr(r, 1, index(r, "\"") - 1)
}

function indent_of(s) {
    match(s, /^[ \t]*/)
    return substr(s, 1, RLENGTH)
}

function print_entries(ind,    k) {
    for (k = 1; k <= n_add; k++) print ind entry[k]
    added = 1
}

BEGIN {
    n_add = 0
    if (add != "") {
        while ((getline line < add) > 0) {
            sub(/^[ \t]+/, "", line); sub(/[ \t\r]+$/, "", line)
            if (line == "") continue
            entry[++n_add] = line
            drop[file_of(line)] = 1
        }
        close(add)
    }
    n = split(remove, rm, " ")
    for (k = 1; k <= n; k++) drop[rm[k]] = 1
    in_list = 0; added = 0; pending = ""
}

# the rest of a <PLUGIN> element that wraps over several lines
pending != "" {
    pending = pending "\n" $0
    if (index($0, "/>") > 0) {
        if (!(file_of(pending) in drop)) print pending
        pending = ""
    }
    next
}

/<PLUGIN[ \t]/ || /<PLUGIN$/ {
    if (index($0, "/>") > 0) {
        if (!(file_of($0) in drop)) print
    } else pending = $0
    next
}

/<VALUE name="pluginList-arm"\/>/ {
    if (n_add > 0 && !added) {
        ind = indent_of($0)
        print ind "<VALUE name=\"pluginList-arm\">"
        print ind "  <KNOWNPLUGINS>"
        print_entries(ind "    ")
        print ind "  </KNOWNPLUGINS>"
        print ind "</VALUE>"
    } else print
    next
}

/<VALUE name="pluginList-arm">/ { in_list = 1; print; next }

in_list && /<KNOWNPLUGINS\/>/ {
    if (n_add > 0 && !added) {
        ind = indent_of($0)
        print ind "<KNOWNPLUGINS>"
        print_entries(ind "  ")
        print ind "</KNOWNPLUGINS>"
    } else print
    next
}

in_list && /<\/KNOWNPLUGINS>/ {
    if (n_add > 0 && !added) print_entries(indent_of($0) "  ")
    print
    next
}

in_list && /<\/VALUE>/ {
    if (n_add > 0 && !added) {   # a pluginList-arm value without a KNOWNPLUGINS element
        ind = indent_of($0)
        print ind "  <KNOWNPLUGINS>"
        print_entries(ind "    ")
        print ind "  </KNOWNPLUGINS>"
    }
    in_list = 0
    print
    next
}

/<\/PROPERTIES>/ {
    if (n_add > 0 && !added) {   # no plugin list at all yet
        print "  <VALUE name=\"pluginList-arm\">"
        print "    <KNOWNPLUGINS>"
        print_entries("      ")
        print "    </KNOWNPLUGINS>"
        print "  </VALUE>"
    }
    print
    next
}

{ print }
