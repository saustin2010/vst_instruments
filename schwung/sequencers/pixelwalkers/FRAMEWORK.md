# The framework changes this plugin uses

This plugin is built with [saustin2010/mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins/tree/7f0653cfbf6f3c2a4a1d0549f4fd9fb3e14d8c27) at `7f0653c` (the commit in `.github/workflows/release.yml`): sd88me's [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) with changes added on top, each meant to be offered to him. This page lists the ones this plugin relies on, what each does here and what happens without it. The full list, for every plugin: [framework/README.md](https://github.com/saustin2010/vst_instruments/blob/main/framework/README.md) in vst_instruments.

## What this plugin needs

| Change | What it does here | Without it |
|---|---|---|
| `focus_ring=1` in `layout.conf` [`81f473b`](https://github.com/saustin2010/mpc-vst-plugins/commit/81f473b) | The control you touch gets a light tint and an accent outline, as on the screen checked on the device. | No highlight on the touched control (sd88me's default since 26 September 2026). |
| `qlink_box=slot` in `layout.conf` [`01da4b3`](https://github.com/saustin2010/mpc-vst-plugins/commit/01da4b3) | The outline MPC draws round the active Q-Link column frames each control's whole slot (a knob's 130 px box down to its value), leaves trigger buttons out and honours `qbox=no`: the boxes checked on a Live II. | Tighter outlines round the knob rings, which can cut through names and values. |
| `mpc_engine_transport()` [`2da5066`](https://github.com/saustin2010/mpc-vst-plugins/commit/2da5066) | The engine gets MPC's tempo, song position and play state every buffer, so it runs in time with the transport. | It builds and passes the offline test, but on the MPC it never plays in time. |
| `QLINK_TRAVEL` and `SET_IF_CHANGED` in `vst.json` `defines` [`10d8f5f`](https://github.com/saustin2010/mpc-vst-plugins/commit/10d8f5f) | Q-Link and data-wheel ticks add up like a detented knob, and a switch moves after half an option's width of turn; a set to the value the engine already holds is skipped. This is how the plugin was checked on the device. | Every tick steps an option: on a Live II, turning one Q-Link flipped other switches (8 October 2026). |

## The layout lines at the top of `layout.conf`

They make the tools draw the screen as it was checked on the device:

- `qlink_bounds=column`: one Q-Link outline per column (sd88me's own option)
- `qlink_box=slot`: those outlines measured by whole slots (a fork change, above)
- `label_scale=1`: names and values at their designed size (sd88me's option; his default is 1.15)
- `focus_ring=1`: the touched control highlighted (a fork change, above)

## Every plugin here also relies on

| Change | Why |
|---|---|
| The release's `install.sh` matches the plugin's path as text [`9595071`](https://github.com/saustin2010/mpc-vst-plugins/commit/9595071) | The plugin's name has a kind tag in brackets (`[SYN]`, `[SEQ]` ...), which sd88me's installer read as a pattern: it refused to install, leaving `MPC.settings` unchanged. |
| `tools_repo` in the release workflow [`6c87b6d`](https://github.com/saustin2010/mpc-vst-plugins/commit/6c87b6d) | Lets `.github/workflows/release.yml` build with the fork's tools instead of sd88me's. |
| The host test runs in Docker on macOS [`50f459d`](https://github.com/saustin2010/mpc-vst-plugins/commit/50f459d) | Apple's AddressSanitizer hangs on macOS 26 before the test starts. |
| Host test: long whole-number ranges [`6303f4e`](https://github.com/saustin2010/mpc-vst-plugins/commit/6303f4e) | A data-wheel click moves 1/100 of a long range (0-5000 ms), as it should; the test expected one step. |
| Host test with `QLINK_TRAVEL` [`f210a56`](https://github.com/saustin2010/mpc-vst-plugins/commit/f210a56) | Skips the travel check on a placeholder parameter, and allows half a step when a saved state is restored. |
| Parameter 0 held until the next block [`7f0653c`](https://github.com/saustin2010/mpc-vst-plugins/commit/7f0653c) | On insert and on every STOP, MPC's host sets parameter 0 to the far end of its range and straight back. Where that is a preset it was loaded twice and every knob moved since went back to it (Live II, 10 October 2026); the wrapper now drops a pair that ends where it started, and the host test replays the toggle. |

## When sd88me's mpc-vst-plugins has them

1. In `.github/workflows/release.yml`, point `uses:` at `sd88me/mpc-vst-plugins/.github/workflows/vst-release.yml@<his commit>`, set `tools_ref` to the same commit and delete the `tools_repo` line.
2. Nothing else changes: the layout lines, `defines`, `params.json` and `vst.json` stay as they are.
3. If he leaves one out or names it differently, keep building from the fork until that's settled; the table above says what changes for this plugin without it.
