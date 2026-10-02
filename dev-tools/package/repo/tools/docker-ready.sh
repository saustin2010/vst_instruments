# Make sure Docker is up and can run 32-bit ARM containers; sourced by tools/build.sh and dev-tools/fake-mpc/start.sh
# (or run it: `bash tools/docker-ready.sh`). Installing plugins doesn't need Docker; building and testing do.
#  - Docker not reachable: start Colima if it's installed (macOS), else say what to do.
#  - No 32-bit ARM emulation (it's lost on every Colima/VM restart): register it with tonistiigi/binfmt.
docker_ready() {
  if ! docker info >/dev/null 2>&1; then
    if command -v colima >/dev/null 2>&1; then
      echo "docker: not running, starting Colima..."
      colima start >/dev/null 2>&1 || { echo "docker: 'colima start' failed; start Docker yourself and retry"; return 1; }
    else
      echo "docker: not reachable. Start Docker (macOS: brew install colima docker && colima start) and retry"
      return 1
    fi
  fi
  if ! docker run --rm --platform linux/arm/v7 busybox true >/dev/null 2>&1; then
    echo "docker: registering 32-bit ARM emulation (needed after every Colima/VM restart)..."
    docker run --privileged --rm tonistiigi/binfmt --install arm >/dev/null 2>&1 &&
      docker run --rm --platform linux/arm/v7 busybox true >/dev/null 2>&1 ||
      { echo "docker: 32-bit ARM emulation isn't working (see BUILDING.md)"; return 1; }
  fi
}

# run directly: just do the check
if [ "${BASH_SOURCE[0]}" = "$0" ]; then docker_ready && echo "docker: ready (32-bit ARM emulation on)"; fi
