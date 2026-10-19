"""Regenerate src/app/nlhc.ico from src/app/icon.png.

Windows picks the closest size out of the ICO, so the file carries the whole
range from 16 px (taskbar) to 256 px (large tiles).
"""

import os
import sys

from PIL import Image

SIZES = (16, 24, 32, 48, 64, 128, 256)
HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "..", "src", "app", "icon.png")
TARGET = os.path.join(HERE, "..", "src", "app", "nlhc.ico")


def main():
    image = Image.open(SOURCE).convert("RGBA")
    if image.width != image.height:
        side = max(image.size)
        canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
        canvas.paste(image, ((side - image.width) // 2, (side - image.height) // 2))
        image = canvas
    frames = [image.resize((size, size), Image.LANCZOS) for size in SIZES]
    frames[-1].save(TARGET, format="ICO", sizes=[(s, s) for s in SIZES])
    print("%s: %s -> %d bytes" % (os.path.relpath(TARGET), " ".join(str(s) for s in SIZES),
                                  os.path.getsize(TARGET)))
    return 0


if __name__ == "__main__":
    sys.exit(main())