#!/usr/bin/env bash
# A pretend MPC for testing the installer without a device: a 32-bit ARM BusyBox container (the MPC's own shell and
# tools) with /sdcard, a minimal MPC.settings and a systemctl that only prints what it was asked.
#   dev-tools/fake-mpc/start.sh          (re)start it, empty;  dev-tools/fake-mpc/start.sh stop  removes it
# Its files live in dev-tools/fake-mpc/state/ (ignored by git), so you can look at what an install wrote.
# Needs Docker with 32-bit ARM emulation (BUILDING.md).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../../tools/docker-ready.sh"
docker_ready || exit 1
docker rm -f fake-mpc >/dev/null 2>&1 || true
[ "${1:-}" = stop ] && { rm -rf "$HERE/state"; exit 0; }
rm -rf "$HERE/state"
mkdir -p "$HERE/state/sdcard" "$HERE/state/media/az01-internal/Settings/MPC" "$HERE/state/bin"
cp "$HERE/MPC.settings" "$HERE/state/media/az01-internal/Settings/MPC/MPC.settings"
printf '#!/bin/sh\necho "[systemctl $*]"\n' > "$HERE/state/bin/systemctl"
chmod +x "$HERE/state/bin/systemctl"
docker run -d --platform linux/arm/v7 --name fake-mpc -v "$HERE/state/sdcard:/sdcard" -v "$HERE/state/media:/media" \
  -v "$HERE/state/bin/systemctl:/usr/local/bin/systemctl" busybox sleep 86400 >/dev/null
echo "fake-mpc running ($(docker exec fake-mpc uname -m)). Try:"
echo "  MPC_SSH_BIN=dev-tools/fake-mpc/fake-ssh ./install.sh fake --dry-run all"
