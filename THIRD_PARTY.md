# Third-party components

STACK's own code (`app/`, `tests/`, `tools/`, `web/`) is GPL-3.0-or-later (see `LICENSE`).
It is built on the following, each under its own licence:

| Component | Where | Licence | Notes |
|---|---|---|---|
| fm1-base (Charles Vestal, Leo Kuroshita, keremimo) | `base/` (git submodule, pinned) | MPL-2.0 | platform layer, host simulator, web emulator, build and install tools. Files are not modified here. |
| felucca-bsp (Leo Kuroshita / Hügelton Instruments and contributors) | `base/bsp/` (submodule of fm1-base) | MPL-2.0 | chip and board layer, **update loader**, package builder. Never modified by STACK. |
| Fonts shipped by fm1-base (Terminus, Barlow) | `base/assets/fonts/` | SIL OFL 1.1 | used only by the platform's own screens; STACK draws with its own pixel font. |
| JieLi AC79 SDK files (`uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin`) | fetched at build time, inside each `.fwsc` | Apache-2.0 | never committed. |

Not used: M-VAVE's closed `libfm1_engine.a` (fm1_open). Because STACK links no closed binary, the FM1 SDK linking
exception does not apply.

Planned (added when used, with their licence files): msfa / Dexed FM core (Apache-2.0), Felucca-Engines DSP
primitives (MIT).

The reference photo `reference/kawa-soundsystem.jpg` is a visual reference supplied by the project owner. It is not
part of the firmware or the web emulator, and none of its names or logos are reproduced.
