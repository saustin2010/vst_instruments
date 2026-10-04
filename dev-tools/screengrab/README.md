# screengrab: screenshots of the MPC's own screen (2026-10-02)

`steve/tools/screengrab/grab.sh [host] [out.png]` copies what the MPC is showing right now to a PNG (default
`steve/screens/<time>.png`), so a skin can be checked on the device without a photo.

How: MPC OS draws through the display controller's DRM plane, not fbdev (`/dev/fb0` reads all black). `drmgrab`
(ARM32, built from `drmgrab.c` in the `arm32v7/gcc:12` container, raw kernel ioctls, no libdrm) opens
the display's `/dev/dri/card*` (`card0` or `card1`: it changes between boots, so `grab.sh` picks the one whose plane shows a framebuffer), finds the framebuffer the active plane scans out
(GETPLANE/GETFB, root), maps it (MAP_DUMB) and writes it to stdout: 800 x 1280, 32 bpp XRGB, portrait.
`rawpng.py` turns it into the 1280 x 800 landscape view (Pillow, in the `mpc-vst-html-art` container).

Read-only: nothing on the device changes except `/tmp/drmgrab` (tmpfs, gone after a reboot; `grab.sh` copies it again).
Build: `docker run --rm --platform linux/arm/v7 -v "$PWD":/w -w /w arm32v7/gcc:12 gcc -O2 -Wall -o drmgrab drmgrab.c`
(in this folder). `drmgrab /dev/dri/card0 list` prints the planes without copying anything.

## Recording and CPU
- `watch.sh [seconds] [interval] [max]`: records the screen while someone uses the MPC, keeping only frames that
  changed (one SSH session; frames sent as length-prefixed gzip, as BusyBox has no base64); PNGs in
  `steve/screens/watch-<time>/`. `sheet.py out.png cols raw...`: a contact sheet of raw frames.
- `cpuwatch.sh [seconds] [every]`: MPC's CPU every few seconds, total and the busiest threads by name
  (`MPC_Main_Thread` is the screen, `Audio_Processing`/`AudioWorker*` audio), from `/proc` ticks. Read-only.
