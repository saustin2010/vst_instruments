# fake-mpc: test the installer without an MPC

`start.sh` runs a 32-bit ARM BusyBox container that looks enough like an MPC for `install.sh` and `uninstall.sh`:
`/sdcard`, `MPC.settings` under `/media/az01-internal/Settings/MPC/` (a minimal one, with an entry from another
plugin wrapped over two lines, as MPC writes long entries) and a `systemctl` that just prints. `fake-ssh` replaces
ssh, so the scripts run exactly as they would against a device:

```
dev-tools/fake-mpc/start.sh
MPC_SSH_BIN=dev-tools/fake-mpc/fake-ssh ./install.sh fake --dry-run all
MPC_SSH_BIN=dev-tools/fake-mpc/fake-ssh ./install.sh fake sequencers effects --yes
cat dev-tools/fake-mpc/state/media/az01-internal/Settings/MPC/MPC.settings
MPC_SSH_BIN=dev-tools/fake-mpc/fake-ssh ./uninstall.sh fake grids --yes
dev-tools/fake-mpc/start.sh stop
```

What it can't tell you: whether the real MPC accepts the edited settings, finds the skins, or lists the plugins.
