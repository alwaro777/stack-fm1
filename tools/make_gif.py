#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The README's GIF: the RAVE demo building up, from the host simulator's screenshots.

    python3 tools/make_gif.py SHOTS_DIR OUT.gif [SCALE]

SHOTS_DIR holds f000.png, f001.png ... (app/tests/gif.txt makes them, 250 ms apart); each frame is scaled up by an
integer (nearest neighbour: the pixels stay sharp) and shown for 250 ms."""
import sys
from pathlib import Path
from PIL import Image

shots = sorted(Path(sys.argv[1]).glob("f[0-9][0-9][0-9].png"))
scale = int(sys.argv[3]) if len(sys.argv) > 3 else 1
frames = []
for p in shots:
    im = Image.open(p).convert("RGB")
    if scale > 1:
        im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    frames.append(im.quantize(colors=32, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE))
frames[0].save(sys.argv[2], save_all=True, append_images=frames[1:], duration=250, loop=0, optimize=True)
print(f"{sys.argv[2]}: {len(frames)} frames, {Path(sys.argv[2]).stat().st_size // 1024} KiB")
