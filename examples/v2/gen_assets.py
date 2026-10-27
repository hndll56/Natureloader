#!/usr/bin/env python3
"""Generate the tiny placeholder PNGs used by the example mods (original artwork, no game data)."""
import struct, zlib, os, sys

def png(w, h, rgba_fn):
    raw = b"".join(b"\x00" + b"".join(bytes(rgba_fn(x, y)) for x in range(w)) for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

def turnip(x, y, s=1):
    cx, cy = 8 * s, 9 * s
    r2 = (x - cx) ** 2 + (y - cy) ** 2
    if r2 < (6 * s) ** 2: return (70, 90, 220, 255)
    if abs(x - cx) < 2 * s and 1 * s <= y < 4 * s: return (40, 170, 60, 255)
    return (0, 0, 0, 0)

root = os.path.dirname(os.path.abspath(__file__))
def put(path, data):
    path = os.path.join(root, path); os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(data)
put("kebunku/assets/sprites/lobak-biru.png", png(16, 16, turnip))
put("kebunku/assets/sprites/pak-biru.png", png(32, 48, lambda x, y: (200, 150, 90, 255) if 8 < x < 24 and 4 < y < 44 else (0, 0, 0, 0)))
put("hd-pack/assets/tex/player-hd.png", png(128, 192, lambda x, y: (230, 190, 120, 255) if 20 < x < 108 and 10 < y < 180 else (0, 0, 0, 0)))
