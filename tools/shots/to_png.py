#!/usr/bin/env python3
"""Turns the harness's byte-swapped RGB565 frames into 2x PNGs (stdlib only)."""
import struct
import sys
import zlib
from pathlib import Path

W, H, SCALE = 320, 240, 2


def png(path, rows):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    ihdr = struct.pack(">IIBBBBB", W * SCALE, H * SCALE, 8, 2, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def convert(src, dst):
    data = src.read_bytes()
    rows = []
    for y in range(H):
        line = bytearray()
        for x in range(W):
            lo, hi = data[(y * W + x) * 2], data[(y * W + x) * 2 + 1]
            v = (lo << 8) | hi            # the panel's byte order, swapped back
            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
            px = bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
            line += px * SCALE
        rows.extend([line] * SCALE)
    png(dst, rows)


if __name__ == "__main__":
    src_dir, dst_dir = Path(sys.argv[1]), Path(sys.argv[2])
    dst_dir.mkdir(parents=True, exist_ok=True)
    for f in sorted(src_dir.glob("*.rgb565")):
        convert(f, dst_dir / (f.stem + ".png"))
        print("wrote", dst_dir / (f.stem + ".png"))
