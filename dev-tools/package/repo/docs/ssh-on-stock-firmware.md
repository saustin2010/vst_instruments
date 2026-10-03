# Root SSH on a stock MPC (Live II, MPC OS 3.9.1)

The installer in this repo needs a root shell on the MPC over SSH. Stock MPC OS has none. Many people get one from a
community firmware mod; this page records the other route, used for the MPC Live II these plugins were developed on:
**rebuilding Akai's own update image with SSH switched on**. It's a field report from one device (2026-09-29), not a
supported procedure.

> **Read this first.** Flashing a modified update is at your own risk and may void your warranty. Keep Akai's original
> update image: flashing it (or any official update) puts the stock firmware back and removes SSH. Use a key with a
> passphrase: SSH as root is full control of the device for anyone with the key.

## 1. Device facts (verified 2026-09-29 with the framework's `tools/probe_device.sh`)

- **MPC Live II**, USB device id `0x09e84047` (family `inmusic,acva2`). Gen1 / 32-bit ARM.
- OS base `5.0.17 (scarthgap)`, glibc **2.39**, libstdc++ `6.0.32`.
- CPU: 4 cores, `CPU part 0xc0d` (Cortex-A17), **max 1608 MHz**, `isolcpus=2-3`. Note this is
  ~1.6 GHz vs the Force's ~1.8 GHz, so a bit less DSP headroom per block — bench before relying on
  a heavy port live.
- Audio threads: `AudioWorker0-3` (SCHED_FIFO, one per core) + `Audio Processing` — same layout as
  the Force.
- **Plugin formats compiled into MPC: VST2 yes, VST3 no, LV2 no** (same as Force). Don't build VST3.
- `MPC.settings` at `/media/az01-internal/Settings/MPC/MPC.settings` — same path as the Force.
- MPC runs as **uid 0** (root).
- `/sdcard` is the **internal eMMC** (`/dev/mmcblk1p1`, ext4 rw), NOT the SD card slot. ~14 GB, and
  it was ~85% full before installs. The SD card and SSD mount under `/media/` (see §3).
- No desktop GUI libraries present (`libX11`, `libxcb`, `libGL` absent; `libEGL`, `libfreetype`,
  `libasound`, `libcurl`, `libssl` present). Consequence: only plugins that draw **no window of
  their own** can load — which is exactly the model here (MPC draws the skin). Rules out most
  desktop/commercial VSTs even when an ARM build exists.


## 2. Enabling SSH on stock 3.9.1 

Stock MPC OS ships no shell access. Instead of a community mod, this device was opened by **rebuilding
Akai's own official update image** with SSH switched on, then flashing it through MPC's normal USB
updater. This is reversible: flashing any stock Akai update removes it, and every official update
will remove it (redo afterwards).

**The Google/LLM "edit /etc/shadow + PasswordAuthentication yes" guide does NOT work on 3.9.1** —
Akai's own `/etc/ssh/sshd_config.d/10-az0x.conf` already sets `PermitRootLogin prohibit-password`
and `PasswordAuthentication no`, and an included config wins, so password auth stays off and sshd
never starts at boot. You'd flash a modified image and still have no way in. Key-based login is the
route that actually works.

### Tools
- `mpcimg2` from **TheKikGen/MPC-LiveXplore** (`imgmaker/mpcimg2.c`) — unpacks/repacks the Akai
  image format used from firmware 3.4 onward. Builds natively on macOS/arm64 against Homebrew
  `xz` + `openssl@3` (`cc -o mpcimg2 mpcimg2.c -I.../xz/include -I.../openssl@3/include
  -L.../xz/lib -L.../openssl@3/lib -llzma -lcrypto`; the `-Wpointer-sign`/`-Wformat` warnings are
  harmless). Its Makefile's `-m32 -static` is Linux-only; drop it on macOS.
- Homebrew **e2fsprogs** `debugfs` — reads and writes the ext4 root filesystem image on macOS
  (macOS has no native ext4 writer). Read-only inspection is `debugfs -R '<cmd>' rootfs.img`;
  edits use `debugfs -w -f edits.txt rootfs.img`.

### Image format (3.9.1 Gen1, from `mpcimg2 -i`)
`AZ01` header (264 B) → single xz-compressed `rootfs` partition (guarded by a **SHA-1 that
`mpcimg2` recomputes on repack**, not a signature) → 16-byte `EOF` marker. Header + data + marker
== file size exactly, i.e. no room for a signature the tool doesn't know about. The updater appears
to trust the SHA-1 only (a rebuilt image installed and booted). The compat-device table in the
header lists all Gen1 ids incl. `0x09e84047`.

### Procedure (all on the Mac; only the flash happens on-device)
1. Download the official image from akaipro's firmware page CDN:
   `MPC-3.9.1-Gen1-update.img` (~162 MB). Extract: `mpcimg2 -r <img> rootfs.ext4`
   (confirm the printed SHA-1 matches the header's, and that no decoder error appears — the tool
   prints "Done." even on failure).
2. Inspect first (read-only) to learn the layout — the key findings on 3.9.1:
   - sshd is present (`/usr/sbin/sshd`) but simply not started; `sshd.service` exists under
     `/usr/lib/systemd/system/` and is only missing its `multi-user.target.wants` symlink.
   - `/etc` and `/var` are writable **overlays** (upper dir on `/data`), and Akai **wipes its copy
     of `/etc/ssh` and the passwd/shadow files at every boot** (`/etc/az01/overlayfs.conf` `R`
     lines) — so edits baked into the read-only lower image win; you don't fight the overlay.
   - root's home is `/root` (on the read-only rootfs). Akai ships `/root/.ssh/authorized_keys`
     containing a `cert-authority` line for an "AZ01 Certificate Authority" — i.e. Akai can mint a
     cert to log in as root. Harmless while sshd is off; **remove it** when enabling SSH so only
     your key is trusted.
   - The device ships a **shared** ed25519 host key (identical on every 3.9.1 unit, in the public
     download) — replace it with a freshly generated one so the device isn't impersonable.
   - Firewall (`/etc/iptables/*.rules`) is empty — won't block port 22.
3. Edits (on a copy of `rootfs.ext4`, via `debugfs -w`):
   - Replace `/etc/ssh/ssh_host_ed25519_key`(+`.pub`) with a freshly `ssh-keygen`'d host key
     (`-N ''`, host keys have no passphrase). Set mode 600/644, uid/gid 0.
   - Replace `/root/.ssh/authorized_keys` with **only** your own public key (drop Akai's
     cert-authority line). Mode 600, uid/gid 0.
   - `symlink sshd.service ../sshd.service` inside
     `/usr/lib/systemd/system/multi-user.target.wants/` to start sshd at boot.
   - Leave password auth **off** and root's password field `*` (locked). No settings loosened.
   - `e2fsck -fn` the result; size must stay identical (repack needs the same size).
4. Repack: `mpcimg2 -m <orig.img> rootfs-mod.ext4 ssh.img`, then verify: `mpcimg2 -i ssh.img`
   (device list intact, new SHA-1 self-consistent) and round-trip `mpcimg2 -r ssh.img check.ext4
   && cmp check.ext4 rootfs-mod.ext4`.
5. Copy `ssh.img` to a FAT/exFAT USB stick as `MPC-3.9.1-Gen1-update.img` (use `cp -X` +
   `dot_clean` on macOS so no `._` sidecar files confuse the updater). Compare checksums.
6. On the MPC (mains power, project saved): **Menu → Preferences → Update → USB Drive Update**
   (or hold Shift + Update). Let it reboot itself.
7. Connect the MPC to Wi-Fi, find its IP. From the Mac: `ssh -i <your_key> root@<device-ip>`.
   On first connect, the host-key fingerprint must equal the one you generated in step 3 — accept
   only if it matches. The prompt shows the MPC's hostname: `root@<hostname>:~#`.

**Security notes for whoever runs this:** use a passphrased key or a Keychain-loaded one; a
no-passphrase key file is a plaintext root credential to anyone on the LAN. The point of the
custom host key is that the shared Akai key makes the device impersonable — don't skip it.
`~/.ssh` on the device is on the read-only rootfs, so `authorized_keys` can't be edited live over
SFTP; changing the trusted key means another image rebuild (or writing to the `/etc`-style overlay,
not attempted here).


## 3. Managing project/plugin files over the network

The same SSH server does SFTP. Point Cyberduck (or `sftp://root@<device-ip>` in a file browser) at
the device using `~/.ssh/<your_key>`. The drives live under **`/media/`**:
`/media/az01-internal` (internal system storage), `/sdcard` = internal eMMC user area,
`/media/mpc_sd` (SD card), `/media/MPC SSD` (the SSD). Verified 2026-10-01: two separate eMMC chips
(`mmcblk0` 7.3 GB = read-only OS on `/` + `/media/az01-internal` aka `/data`; `mmcblk1` 14.8 GB = `/sdcard`
aka `/media/az01-internal-sd`, shown as "Internal" in MPC); the SD slot is an internal USB card reader
(`sda`, exFAT here) and the SSD sits behind a JMicron USB-SATA bridge (`sdb`, NTFS here). The SD and SSD
are mounted **`noexec`**, so plugin `.so` files must stay on `/sdcard/vst/`. Over SFTP the SSD is writable (MPC writes it
itself), unlike over USB where macOS mounts NTFS read-only. **You're root** — stay in the user
areas, and don't move/delete a project while it's open in MPC. USB "computer mode" is still faster
for bulk copies.
