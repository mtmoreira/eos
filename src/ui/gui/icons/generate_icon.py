#!/usr/bin/env python3
"""Generate the eos-gui app icon from the approved EOS logo.

Source: docs/logo/eos-logo-dark.webp (the committed dark logo,
1600x1600, neon eos wordmark on black). That file is never
modified; this script only derives new files from it.

Outputs, all under this directory:
  eos-icon.iconset/  Apple iconset PNGs: 16, 32, 64, 128, 256,
                     512, and 1024 pixels, with the macOS icon
                     names (icon_16x16.png, icon_16x16@2x.png,
                     and so on). The artwork is the full logo
                     with macOS-style rounded corners (radius
                     about 22 percent of the size) and
                     transparent corners.
  eos-icon.icns      macOS icon bundle built from the same
                     PNGs: icp4 (16), icp5 (32), icp6 (64),
                     ic07 (128), ic08 (256), ic09 (512), and
                     ic10 (1024). The format is the standard
                     icns container: 'icns' plus a big-endian
                     total length, then one element per size
                     (4-byte type, big-endian length including
                     the 8 header bytes, PNG data). macOS
                     accepts PNG data in these slots; this is
                     what iconutil writes for the large sizes.

Regenerate with: python3 src/ui/gui/icons/generate_icon.py
Run from anywhere; the script finds the repo root from its own
path. Requires Pillow. The outputs are deterministic for a
given Pillow version, so regenerating reproduces them.

On macOS, the same iconset can also be turned into an .icns
with the system tool instead of this script:
  iconutil -c icns src/ui/gui/icons/eos-icon.iconset
That produces an equivalent .icns; the committed .icns is the
one this script writes, so the bundle build never needs macOS
tools and works the same on Linux CI.
"""

import os
import struct
import sys

from PIL import Image, ImageDraw

# (icns element type, pixel size) for every size macOS wants.
ELEMENTS = [
    ("icp4", 16),
    ("icp5", 32),
    ("icp6", 64),
    ("ic07", 128),
    ("ic08", 256),
    ("ic09", 512),
    ("ic10", 1024),
]

# Apple iconset file name per pixel size. Sizes shared by two
# names (32, 256, 512) list the base name here; the matching
# @2x alias is written alongside it below, since both names
# hold the same pixels.
ICONSET_NAMES = {
    16: "icon_16x16.png",
    32: "icon_32x32.png",
    64: "icon_32x32@2x.png",
    128: "icon_128x128.png",
    256: "icon_256x256.png",
    512: "icon_512x512.png",
    1024: "icon_512x512@2x.png",
}

# Alias name per pixel size for the sizes Apple names twice.
ICONSET_ALIASES = {
    32: "icon_16x16@2x.png",
    256: "icon_128x128@2x.png",
    512: "icon_256x256@2x.png",
}


def rounded(image, radius):
    """Return a copy of image with rounded corners and alpha."""
    rgba = image.convert("RGBA")
    mask = Image.new("L", rgba.size, 0)
    draw = ImageDraw.Draw(mask)
    draw.rounded_rectangle(
        [(0, 0), (rgba.size[0] - 1, rgba.size[1] - 1)],
        radius=radius,
        fill=255,
    )
    rgba.putalpha(mask)
    return rgba


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.dirname(here)))
    )
    source = os.path.join(root, "docs", "logo", "eos-logo-dark.webp")
    iconset = os.path.join(here, "eos-icon.iconset")
    icns_path = os.path.join(here, "eos-icon.icns")
    os.makedirs(iconset, exist_ok=True)

    logo = Image.open(source)
    if logo.size[0] != logo.size[1]:
        raise SystemExit("logo is not square: %s" % (logo.size,))
    print("source: %s %dx%d" % (source, logo.size[0], logo.size[1]))

    pngs = {}
    for element, size in ELEMENTS:
        resized = logo.resize((size, size), Image.LANCZOS)
        radius = max(1, round(0.2237 * size))
        icon = rounded(resized, radius)
        name = ICONSET_NAMES[size]
        path = os.path.join(iconset, name)
        icon.save(path, format="PNG")
        pngs[element] = path
        print("wrote: %s %dx%d %d bytes" % (path, size, size,
                                            os.path.getsize(path)))
    # The sizes Apple names twice get their @2x alias as a copy
    # of the same pixels.
    for size, alias in ICONSET_ALIASES.items():
        alias_src = os.path.join(iconset, ICONSET_NAMES[size])
        alias_dst = os.path.join(iconset, alias)
        with open(alias_src, "rb") as src, open(alias_dst, "wb") as dst:
            dst.write(src.read())
        print("wrote: %s (alias of %s) %d bytes"
              % (alias_dst, ICONSET_NAMES[size],
                 os.path.getsize(alias_dst)))

    body = b""
    for element, _size in ELEMENTS:
        with open(pngs[element], "rb") as handle:
            data = handle.read()
        body += element.encode("ascii")
        body += struct.pack(">I", 8 + len(data))
        body += data
    total = 8 + len(body)
    with open(icns_path, "wb") as handle:
        handle.write(b"icns")
        handle.write(struct.pack(">I", total))
        handle.write(body)
    print("wrote: %s %d bytes" % (icns_path, os.path.getsize(icns_path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
