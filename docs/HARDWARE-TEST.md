# Hardware test checklist (Phase 6)

The Firmware Hub asks for every release to be tested on a real FM-1 (thefirmwarehub.com/kb). STACK has run only in
the host simulator and the browser so far; this is what to check on the unit, in order, before a release is
submitted. Note the FM-1's flash chip (the installer and `tools/fm1_install.py` report it: 0x856014 P25Q80 or
0xB36014 UC25HQ80) and the result of each line.

## Before anything

- [ ] Back up the whole flash (thefirmwarehub.com/tools/rescue, or `base/bsp/tools/fm1_rescue.py`): a way back.
- [ ] Know the way out: **hold REC while switching on** = the chip's update mode (fm1-base). Try it once now.

## Install and leave

- [ ] Install from M-VAVE's V15 (installer page, Chrome or Edge). The FM-1 reports `FM-1_58XXYYZZ` afterwards.
- [ ] Install over another hub firmware.
- [ ] Install another firmware over STACK, then STACK again.
- [ ] Unplug USB in the middle of an install; the installer's resume finishes it.
- [ ] Two failed starts bring safe mode (USB on, no sound); an install works from there.

## Performance (the numbers the docs only estimate)

- [ ] GLOBAL page 1 shows CPU % and XRUNS. Play each demo (HOME + PLAY, three times) for a minute: note CPU %,
      XRUNS must stay 0. Estimates: RAVE ~8-17 %, the heavy worst case ~30-60 %.
- [ ] The heaviest pattern you can make (two RAVE STAB tracks with 4-voice chords, the HOOVER, every send up, pump,
      COMP, DJ filter): does GUARD appear in GLOBAL? Does the sound stay clean?
- [ ] The screen at 140 BPM on the sound system: cones keep time with the sound, the keys and knobs answer at once.
- [ ] Turning MASTER: no clicks.

## Functions

- [ ] Every control: SHIFT+FX opens the I/O test; every button, key and knob shows.
- [ ] SAVE (stopped), switch off and on: the project is back. SAVE while playing says STOP TO SAVE.
- [ ] The backup page (site /backup/): back up, change something, restore: it is back. Load a demo .stk.
- [ ] MIDI over USB with a DAW: notes out on a track's channel; clock out (GLOBAL 2) drives the DAW; the DAW's clock
      drives STACK (CLOCK IN EXT: the header shows EXT and the tempo); start/stop both ways.
- [ ] MIDI in on the TRS jack (a keyboard): a track on its channel plays.
- [ ] Headphones and speaker both.

## When something fails

Record what the screen says and the GLOBAL numbers; `base/tools/fm1_remote.py` reads the crash records if
fm1.toml's `crash_history` is turned on for a debug build.
