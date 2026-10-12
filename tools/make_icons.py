#!/usr/bin/env python3
"""Builds the application icons from _icons/icon.jpg (16..64 px) and _icons/icon_hd.jpg (128..512 px).

Writes resources/icons/icon_<size>.png (all sizes, embedded in the executable for the window icon)
and resources/app.ico (every size up to 256 px, the maximum an .ico can hold).
"""
import io
import pathlib
import struct

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
SMALL_SIZES = [16, 24, 32, 48, 64]      # from icon.jpg
LARGE_SIZES = [128, 256, 512]           # from icon_hd.jpg


def render(source, size):
    return source.convert("RGBA").resize((size, size), Image.LANCZOS)


def png_bytes(image):
    buffer = io.BytesIO()
    image.save(buffer, "PNG", optimize=True)
    return buffer.getvalue()


def main():
    small = Image.open(ROOT / "_icons" / "icon.jpg")
    large = Image.open(ROOT / "_icons" / "icon_hd.jpg")
    out = ROOT / "resources" / "icons"
    out.mkdir(parents=True, exist_ok=True)

    images = {}
    for size in SMALL_SIZES:
        images[size] = render(small, size)
    for size in LARGE_SIZES:
        images[size] = render(large, size)

    for size, image in images.items():
        (out / f"icon_{size}.png").write_bytes(png_bytes(image))

    ico_sizes = [s for s in sorted(images) if s <= 256]
    payloads = [png_bytes(images[s]) for s in ico_sizes]
    header = struct.pack("<HHH", 0, 1, len(ico_sizes))
    offset = 6 + 16 * len(ico_sizes)
    entries = b""
    for size, data in zip(ico_sizes, payloads):
        dim = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    (ROOT / "resources" / "app.ico").write_bytes(header + entries + b"".join(payloads))
    print("icons:", sorted(images), "ico:", ico_sizes)


if __name__ == "__main__":
    main()
