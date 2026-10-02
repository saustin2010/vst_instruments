# Shared by install.sh and uninstall.sh: finding plugins and talking to the MPC over SSH.

plugin_dirs() { { find schwung mutable-instruments vcv-rack -mindepth 2 -maxdepth 3 -name vst.json 2>/dev/null || true; } | xargs -n1 dirname | sort; }
lookup() {   # lookup <name|group> -> plugin folders
  case "$1" in
    all) plugin_dirs ;;
    instruments) plugin_dirs | grep -E '^schwung/instruments/|^mutable-instruments/(rings|elements)$' ;;
    sequencers) plugin_dirs | grep -E '^schwung/sequencers/|^mutable-instruments/(grids|marbles)$|^vcv-rack/' ;;
    effects) plugin_dirs | grep -E '^schwung/effects/|^mutable-instruments/(warps|ringsfx)$' ;;
    schwung|mutable-instruments|vcv-rack) plugin_dirs | grep "^$1/" ;;
    *) plugin_dirs | grep -E "/$1\$" || { echo "no plugin or group called '$1' (./install.sh --list)" >&2; exit 1; } ;;
  esac
}

# ssh_setup <host>: one SSH connection reused for every step (one login). MPC_SSH_KEY, MPC_SSH_USER as in install.sh.
ssh_setup() {
  HOST=$1
  CM=/tmp/vsti-ssh-$$
  SSH=("${MPC_SSH_BIN:-ssh}" -o ConnectTimeout=10 -o ControlMaster=auto -o ControlPath="$CM" -o ControlPersist=120)
  [ -n "${MPC_SSH_KEY:-}" ] && SSH+=(-i "$MPC_SSH_KEY")
  SSH+=("${MPC_SSH_USER:-root}@$HOST")
  STAGE=/sdcard/.vst_instruments-stage
}
ssh_close() { "${MPC_SSH_BIN:-ssh}" -o ControlPath="$CM" -O exit "$HOST" >/dev/null 2>&1 || true; }
# side <command> <args>: run tools/mpc-side.sh on the MPC
side() { "${SSH[@]}" "MPC_PLUGIN_LIST_AWK=$STAGE/plugin_list.awk sh -s -- $*" < tools/mpc-side.sh; }
attr() { sed -n "s/.* $1=\"\([^\"]*\)\".*/\1/p" "$2"; }
