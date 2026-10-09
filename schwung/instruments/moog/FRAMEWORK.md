# The framework changes this plugin uses

This plugin is built with [saustin2010/mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins/tree/f210a56b269deb622ce4d62f437ee3ac8567ceaf) at `f210a56` (the commit in `.github/workflows/release.yml`): sd88me's [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) with changes added on top, each meant to be offered to him. This page lists the ones this plugin relies on, what each does here and what happens without it. The full list, for every plugin: [framework/README.md](https://github.com/saustin2010/vst_instruments/blob/main/framework/README.md) in vst_instruments.

## What this plugin needs

| Change | What it does here | Without it |
|---|---|---|
| A hand-made `params.json` beside the Schwung `module` [`b1c39bc`](https://github.com/saustin2010/mpc-vst-plugins/commit/b1c39bc) | `vst.json` names both the Schwung module and this folder's `params.json` (readable names, ranges and options the module doesn't declare). The params file sets the parameter list and the Schwung adapter is still linked. | The build fails: sd88me's tools take `params.json` alone and don't link the adapter (undefined `mpc_engine`). |
| `focus_ring=1` in `layout.conf` [`81f473b`](https://github.com/saustin2010/mpc-vst-plugins/commit/81f473b) | The control you touch gets a light tint and an accent outline, as on the screen checked on the device. | No highlight on the touched control (sd88me's default since 26 September 2026). |
| `qlink_box=slot` in `layout.conf` [`01da4b3`](https://github.com/saustin2010/mpc-vst-plugins/commit/01da4b3) | The outline MPC draws round the active Q-Link column frames each control's whole slot (a knob's 130 px box down to its value), leaves trigger buttons out and honours `qbox=no`: the boxes checked on a Live II. | Tighter outlines round the knob rings, which can cut through names and values. |
| `when=<param>:<i>/<N>` in `layout.conf` [`81a2edd`](https://github.com/saustin2010/mpc-vst-plugins/commit/81a2edd) | A picture shown only while a continuous parameter is in band i of N (an envelope drawing per sustain level). | The build stops ("is not an option parameter"). |
| Option `values` / `send` in `params.json` [`8a6ed80`](https://github.com/saustin2010/mpc-vst-plugins/commit/8a6ed80) | An option sends the engine its own value or word instead of its index, because this engine parses those. | The options send their index and select the wrong setting. |
| `"programs"` `count` / `name_at` / `name` in `vst.json` [`ea27eda`](https://github.com/saustin2010/mpc-vst-plugins/commit/ea27eda) | MPC's PRESET menu lists exactly the engine's presets (its own count), named without loading each one (`name_at`), or by reading them once at creation (`name`). | The PRESET menu lists empty slots up to the parameter's range, or numbered names. |
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

## When sd88me's mpc-vst-plugins has them

1. In `.github/workflows/release.yml`, point `uses:` at `sd88me/mpc-vst-plugins/.github/workflows/vst-release.yml@<his commit>`, set `tools_ref` to the same commit and delete the `tools_repo` line.
2. Nothing else changes: the layout lines, `defines`, `params.json` and `vst.json` stay as they are.
3. If he leaves one out or names it differently, keep building from the fork until that's settled; the table above says what changes for this plugin without it.
