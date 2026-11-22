#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deterministic generator for Onslaught CE's original Grand Museum architectural textures.

Produces seamless 256x256 architectural textures (limestone ashlar, honed slate piers,
matte gallery floor slabs, sunlit courtyard pavers, dark basalt runner, recessed coffered
ceiling, brushed museum brass, calm reflecting-pool water, Forseti steel-blue trim,
Muspell iron-amber trim, cinema acoustic panelling, and honed plinth stone), compresses
each as a lossless 24-bit TGA inside a zlib stream, and emits a plain-text C++ header
(port/game/ce_museum_textures_data.h).

Usage:
    python3 port/tools/generate_museum_textures.py                # regenerate header
    python3 port/tools/generate_museum_textures.py --check        # verify header is up to date
    python3 port/tools/generate_museum_textures.py --preview DIR  # write TGA + contact sheet PNG
"""
from __future__ import annotations

import argparse
import hashlib
import math
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT = ROOT / "port/game/ce_museum_textures_data.h"
SIZE = 256


def _clamp8(v: float) -> int:
    if v <= 0.0:
        return 0
    if v >= 255.0:
        return 255
    return int(v + 0.5)


def _hash2(ix: int, iy: int, seed: int) -> float:
    """Deterministic periodic hash in [0, 1)."""
    n = (ix * 374761393 + iy * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    n ^= n >> 16
    return (n & 0xFFFFFF) / 16777216.0


def _make_value_grid(period: int, seed: int) -> list[list[float]]:
    return [[_hash2(x, y, seed) for x in range(period)] for y in range(period)]


def _smooth_grid(period: int, seed: int) -> list[list[float]]:
    """Precompute a seamless 256x256 smooth periodic noise field for one octave."""
    grid = _make_value_grid(period, seed)
    scale = period / float(SIZE)
    xs: list[tuple[int, int, float]] = []
    for x in range(SIZE):
        fx = x * scale
        ix = int(fx)
        tx = fx - ix
        ux = tx * tx * (3.0 - 2.0 * tx)
        xs.append((ix % period, (ix + 1) % period, ux))
    out: list[list[float]] = []
    for y in range(SIZE):
        fy = y * scale
        iy = int(fy)
        ty = fy - iy
        uy = ty * ty * (3.0 - 2.0 * ty)
        y0 = iy % period
        y1 = (iy + 1) % period
        row0 = grid[y0]
        row1 = grid[y1]
        row = [0.0] * SIZE
        for x in range(SIZE):
            x0, x1, ux = xs[x]
            a = row0[x0] + (row0[x1] - row0[x0]) * ux
            b = row1[x0] + (row1[x1] - row1[x0]) * ux
            row[x] = a + (b - a) * uy
        out.append(row)
    return out


def _fbm(octaves: list[tuple[int, float]], seed: int) -> list[list[float]]:
    """Seamless 256x256 multi-octave noise in [-1, 1]."""
    layers = [(_smooth_grid(p, seed + i * 97), w) for i, (p, w) in enumerate(octaves)]
    total_w = sum(w for _, w in layers) or 1.0
    out = [[0.0] * SIZE for _ in range(SIZE)]
    for layer, w in layers:
        s = (2.0 * w) / total_w
        for y in range(SIZE):
            dst = out[y]
            src = layer[y]
            for x in range(SIZE):
                dst[x] += (src[x] - 0.5) * s
    return out


def _encode_tga24(rgb: bytearray) -> bytes:
    """Uncompressed top-left 24-bit BGR TGA (accepted by D3DXCreateTextureFromFileInMemoryEx)."""
    header = struct.pack(
        "<BBBHHBHHHHBB",
        0,     # id length
        0,     # colormap type
        2,     # uncompressed true-color
        0, 0, 0,
        0, 0,
        SIZE, SIZE,
        24,    # bits per pixel
        0x20,  # top-left origin
    )
    bgr = bytearray(len(rgb))
    for i in range(0, len(rgb), 3):
        bgr[i] = rgb[i + 2]
        bgr[i + 1] = rgb[i + 1]
        bgr[i + 2] = rgb[i]
    return header + bytes(bgr)


def _gen_limestone_wall() -> bytearray:
    """Dressed coastal limestone ashlar: 4 staggered courses with calm travertine grain."""
    n_broad = _fbm([(4, 0.55), (8, 0.30), (16, 0.15)], seed=101)
    n_grain = _fbm([(16, 0.45), (32, 0.35), (64, 0.20)], seed=107)
    buf = bytearray(SIZE * SIZE * 3)
    course_h = 64
    block_w = 128
    for y in range(SIZE):
        course = y // course_h
        cy = y % course_h
        shift = (block_w // 2) if (course & 1) else 0
        for x in range(SIZE):
            bx_coord = (x + shift) % SIZE
            block = bx_coord // block_w
            cx = bx_coord % block_w
            tone = (_hash2(block, course, 113) - 0.5) * 9.0
            bed = math.sin((y / float(SIZE)) * 2.0 * math.pi * 6.0 + n_broad[y][x] * 1.4) * 2.5
            grain = n_broad[y][x] * 7.0 + n_grain[y][x] * 4.0 + bed + tone
            # Soft chamfered mortar seams
            d_edge = min(cy, course_h - 1 - cy, cx, block_w - 1 - cx)
            shade = 0.0
            if cy < 2 or cx < 2:
                shade = -26.0
            elif cy == 2 or cx == 2:
                shade = -8.0
            elif cy == course_h - 1 or cx == block_w - 1:
                shade = 6.0
            elif d_edge == 3:
                shade = 2.0
            r = _clamp8(220.0 + grain + shade)
            g = _clamp8(214.0 + grain * 0.96 + shade)
            b = _clamp8(201.0 + grain * 0.90 + shade * 0.95)
            idx = (y * SIZE + x) * 3
            buf[idx] = r
            buf[idx + 1] = g
            buf[idx + 2] = b
    return buf


def _gen_slate_pier() -> bytearray:
    """Honed dark architectural blue-slate for columns, colonnades, and plinth bases."""
    n_broad = _fbm([(4, 0.50), (8, 0.30), (16, 0.20)], seed=211)
    n_fine = _fbm([(16, 0.50), (32, 0.35), (64, 0.15)], seed=223)
    buf = bytearray(SIZE * SIZE * 3)
    course_h = 128
    for y in range(SIZE):
        cy = y % course_h
        course = y // course_h
        c_tone = (_hash2(0, course, 229) - 0.5) * 5.0
        for x in range(SIZE):
            vein = math.sin(((x * 0.5 + y * 1.0) / float(SIZE)) * 2.0 * math.pi * 3.0 + n_broad[y][x] * 2.0) * 2.5
            grain = n_broad[y][x] * 8.0 + n_fine[y][x] * 4.5 + vein + c_tone
            shade = 0.0
            if cy < 2:
                shade = -22.0
            elif cy == 2:
                shade = -7.0
            elif cy == course_h - 1:
                shade = 7.0
            r = _clamp8(98.0 + grain * 0.92 + shade)
            g = _clamp8(109.0 + grain * 0.96 + shade)
            b = _clamp8(119.0 + grain + shade)
            idx = (y * SIZE + x) * 3
            buf[idx] = r
            buf[idx + 1] = g
            buf[idx + 2] = b
    return buf


def _gen_gallery_floor() -> bytearray:
    """Calm matte honed architectural floor slabs (2x2 large travertine-grey tiles)."""
    n_broad = _fbm([(4, 0.55), (8, 0.30), (16, 0.15)], seed=307)
    n_speck = _fbm([(16, 0.40), (32, 0.35), (64, 0.25)], seed=313)
    buf = bytearray(SIZE * SIZE * 3)
    tile = 128
    for y in range(SIZE):
        ty = y // tile
        py = y % tile
        for x in range(SIZE):
            tx = x // tile
            px = x % tile
            tile_shift = (_hash2(tx, ty, 317) - 0.5) * 6.0
            grain = n_broad[y][x] * 6.0 + n_speck[y][x] * 3.0 + tile_shift
            shade = 0.0
            if px < 2 or py < 2:
                shade = -22.0
            elif px == 2 or py == 2:
                shade = -5.0
            elif px == tile - 1 or py == tile - 1:
                shade = 4.0
            r = _clamp8(198.0 + grain + shade)
            g = _clamp8(196.0 + grain * 0.98 + shade)
            b = _clamp8(190.0 + grain * 0.94 + shade)
            idx = (y * SIZE + x) * 3
            buf[idx] = r
            buf[idx + 1] = g
            buf[idx + 2] = b
    return buf


def _gen_courtyard_paver() -> bytearray:
    """Sunlit outdoor coastal terrace sandstone pavers in staggered ashlar bond."""
    n_broad = _fbm([(4, 0.50), (8, 0.35), (16, 0.15)], seed=401)
    n_fine = _fbm([(16, 0.45), (32, 0.35), (64, 0.20)], seed=409)
    buf = bytearray(SIZE * SIZE * 3)
    course_h = 64
    block_w = 128
    for y in range(SIZE):
        course = y // course_h
        cy = y % course_h
        shift = (block_w // 2) if (course & 1) else 0
        for x in range(SIZE):
            bx = (x + shift) % SIZE
            block = bx // block_w
            cx = bx % block_w
            tone = (_hash2(block, course, 419) - 0.5) * 11.0
            grain = n_broad[y][x] * 8.5 + n_fine[y][x] * 4.5 + tone
            shade = 0.0
            if cy < 3 or cx < 3:
                shade = -28.0
            elif cy == 3 or cx == 3:
                shade = -6.0
            elif cy == course_h - 1 or cx == block_w - 1:
                shade = 6.0
            r = _clamp8(212.0 + grain + shade)
            g = _clamp8(204.0 + grain * 0.95 + shade)
            b = _clamp8(187.0 + grain * 0.88 + shade * 0.92)
            idx = (y * SIZE + x) * 3
            buf[idx] = r
            buf[idx + 1] = g
            buf[idx + 2] = b
    return buf


def _gen_runner_stone() -> bytearray:
    """Refined dark honed basalt floor runner for wayfinding spines."""
    n_broad = _fbm([(4, 0.55), (8, 0.30), (16, 0.15)], seed=503)
    n_fine = _fbm([(16, 0.45), (32, 0.35), (64, 0.20)], seed=509)
    buf = bytearray(SIZE * SIZE * 3)
    for y in range(SIZE):
        py = y % 128
        for x in range(SIZE):
            grain = n_broad[y][x] * 6.5 + n_fine[y][x] * 3.5
            shade = -14.0 if py < 2 else (4.0 if py == 127 else 0.0)
            r = _clamp8(80.0 + grain + shade)
            g = _clamp8(89.0 + grain + shade)
            b = _clamp8(97.0 + grain * 1.02 + shade)
            idx = (y * SIZE + x) * 3
            buf[idx] = r
            buf[idx + 1] = g
            buf[idx + 2] = b
    return buf


def _gen_coffered_ceiling() -> bytearray:
    """Recessed architectural coffered ceiling panels (2x2 stepped coffers)."""
    n_fine = _fbm([(8, 0.50), (16, 0.30), (32, 0.20)], seed=601)
    buf = bytearray(SIZE * SIZE * 3)
    cell = 128
    for y in range(SIZE):
        py = y % cell
        for x in range(SIZE):
            px = x % cell
            d = min(px, cell - 1 - px, py, cell - 1 - py)
            grain = n_fine[y][x] * 3.5
            if d < 12:
                # Structural coffer rib beam
                base = 76.0 if d >= 2 else 66.0
                if d == 11:
                    base += 8.0
            elif d < 18:
                # Stepped recessed chamfer
                base = 52.0 + (18 - d) * 2.5
            elif d < 22:
                # Inner reveal groove
                base = 42.0
            else:
                # Quiet acoustic recessed coffer centre
                base = 50.0 + (0.0 if d > 26 else -4.0)
            r = _clamp8(base * 0.94 + grain)
            g = _clamp8(base * 1.00 + grain)
            b = _clamp8(base * 1.08 + grain)
            idx = (y * SIZE + x) * 3
            buf[idx] = r
            buf[idx + 1] = g
            buf[idx + 2] = b
    return buf


def _gen_brushed_brass() -> bytearray:
    """Warm brushed satin museum brass/bronze for frames, trim, and compass inlay."""
    n_broad = _fbm([(4, 0.60), (8, 0.40)], seed=701)
    n_streak = _fbm([(4, 0.30), (16, 0.40), (64, 0.30)], seed=709)
    buf = bytearray(SIZE * SIZE * 3)
    for y in range(SIZE):
        band = math.sin((y / float(SIZE)) * 2.0 * math.pi * 4.0) * 5.0
        for x in range(SIZE):
            # Horizontal brushed micro-grain (compress X frequency, stretch Y)
            brush = n_streak[y][(x // 4) % SIZE] * 10.0 + n_broad[y][x] * 6.0 + band
            r = _clamp8(204.0 + brush)
            g = _clamp8(166.0 + brush * 0.88)
            b = _clamp8(92.0 + brush * 0.65)
            idx = (y * SIZE + x) * 3
            buf[idx] = r
            buf[idx + 1] = g
            buf[idx + 2] = b
    return buf


def _gen_water_basin() -> bytearray:
    """Calm dark reflecting-pool water inlay with soft organic caustics."""
    n1 = _fbm([(4, 0.50), (8, 0.35), (16, 0.15)], seed=809)
    n2 = _fbm([(6, 0.45), (12, 0.35), (24, 0.20)], seed=821)
    buf = bytearray(SIZE * SIZE * 3)
    for y in range(SIZE):
        fy = y / float(SIZE)
        for x in range(SIZE):
            fx = x / float(SIZE)
            w1 = math.sin((fx * 3.0 + fy * 1.0) * 2.0 * math.pi + n1[y][x] * 2.2)
            w2 = math.sin((fx * 1.0 - fy * 4.0) * 2.0 * math.pi + n2[y][x] * 2.2)
            ripple = 0.5 * (w1 + w2)
            caustic = max(0.0, 1.0 - abs(w1 + w2) * 1.25) ** 2
            r = _clamp8(24.0 + ripple * 6.0 + caustic * 22.0 + n1[y][x] * 5.0)
            g = _clamp8(58.0 + ripple * 10.0 + caustic * 36.0 + n1[y][x] * 7.0)
            b = _clamp8(79.0 + ripple * 12.0 + caustic * 42.0 + n2[y][x] * 8.0)
            idx = (y * SIZE + x) * 3
            buf[idx] = r
            buf[idx + 1] = g
            buf[idx + 2] = b
    return buf


def _gen_forseti_trim() -> bytearray:
    """Cool Forseti steel-blue architectural accent panelling."""
    n_broad = _fbm([(4, 0.55), (8, 0.30), (16, 0.15)], seed=907)
    n_fine = _fbm([(16, 0.50), (32, 0.50)], seed=919)
    buf = bytearray(SIZE * SIZE * 3)
    for y in range(SIZE):
        py = y % 64
        for x in range(SIZE):
            grain = n_broad[y][x] * 7.0 + n_fine[y][(x // 2) % SIZE] * 3.5
            shade = 0.0
            if py < 2:
                shade = -20.0
            elif py == 2:
                shade = -6.0
            elif py == 63:
                shade = 10.0
            r = _clamp8(74.0 + grain * 0.85 + shade * 0.85)
            g = _clamp8(110.0 + grain * 0.95 + shade * 0.95)
            b = _clamp8(142.0 + grain + shade)
            idx = (y * SIZE + x) * 3
            buf[idx] = r
            buf[idx + 1] = g
            buf[idx + 2] = b
    return buf


def _gen_muspell_trim() -> bytearray:
    """Warm Muspell iron-amber architectural accent panelling."""
    n_broad = _fbm([(4, 0.55), (8, 0.30), (16, 0.15)], seed=1009)
    n_fine = _fbm([(16, 0.50), (32, 0.50)], seed=1021)
    buf = bytearray(SIZE * SIZE * 3)
    for y in range(SIZE):
        py = y % 64
        for x in range(SIZE):
            grain = n_broad[y][x] * 7.5 + n_fine[y][(x // 2) % SIZE] * 4.0
            shade = 0.0
            if py < 2:
                shade = -22.0
            elif py == 2:
                shade = -6.0
            elif py == 63:
                shade = 11.0
            r = _clamp8(152.0 + grain + shade)
            g = _clamp8(92.0 + grain * 0.80 + shade * 0.80)
            b = _clamp8(52.0 + grain * 0.65 + shade * 0.65)
            idx = (y * SIZE + x) * 3
            buf[idx] = r
            buf[idx + 1] = g
            buf[idx + 2] = b
    return buf


def _gen_cinema_acoustic() -> bytearray:
    """Quiet dark ribbed acoustic slats for the Screening Room."""
    n_fine = _fbm([(8, 0.55), (16, 0.45)], seed=1103)
    buf = bytearray(SIZE * SIZE * 3)
    for y in range(SIZE):
        for x in range(SIZE):
            slat = x % 16
            # Rounded vertical acoustic slat profile
            if slat < 2:
                rib = -14.0
            elif slat == 2 or slat == 15:
                rib = -5.0
            else:
                t = (slat - 2) / 13.0
                rib = math.sin(t * math.pi) * 8.0
            grain = n_fine[y][x] * 3.0 + rib
            r = _clamp8(50.0 + grain)
            g = _clamp8(58.0 + grain * 1.02)
            b = _clamp8(68.0 + grain * 1.06)
            idx = (y * SIZE + x) * 3
            buf[idx] = r
            buf[idx + 1] = g
            buf[idx + 2] = b
    return buf


def _gen_plinth_marble() -> bytearray:
    """Honed architectural pale-grey cast stone for exhibit plinths and turntables."""
    n_broad = _fbm([(4, 0.55), (8, 0.30), (16, 0.15)], seed=1201)
    n_fine = _fbm([(16, 0.50), (32, 0.35), (64, 0.15)], seed=1213)
    buf = bytearray(SIZE * SIZE * 3)
    for y in range(SIZE):
        for x in range(SIZE):
            vein = math.sin(((x * 0.7 + y * 0.4) / float(SIZE)) * 2.0 * math.pi * 2.0 + n_broad[y][x] * 2.4)
            vein_term = (1.0 - abs(vein)) ** 4 * -7.0
            grain = n_broad[y][x] * 6.0 + n_fine[y][x] * 3.0 + vein_term
            # Subtle recessed architectural reveal band near top/bottom
            shade = -12.0 if y < 2 else (5.0 if y == 2 else 0.0)
            r = _clamp8(168.0 + grain + shade)
            g = _clamp8(172.0 + grain * 0.99 + shade)
            b = _clamp8(175.0 + grain * 0.98 + shade)
            idx = (y * SIZE + x) * 3
            buf[idx] = r
            buf[idx + 1] = g
            buf[idx + 2] = b
    return buf


TEXTURES = (
    ("LimestoneWall", "limestone_wall", _gen_limestone_wall),
    ("SlatePier", "slate_pier", _gen_slate_pier),
    ("GalleryFloor", "gallery_floor", _gen_gallery_floor),
    ("CourtyardPaver", "courtyard_paver", _gen_courtyard_paver),
    ("RunnerStone", "runner_stone", _gen_runner_stone),
    ("CofferedCeiling", "coffered_ceiling", _gen_coffered_ceiling),
    ("BrushedBrass", "brushed_brass", _gen_brushed_brass),
    ("WaterBasin", "water_basin", _gen_water_basin),
    ("ForsetiTrim", "forseti_trim", _gen_forseti_trim),
    ("MuspellTrim", "muspell_trim", _gen_muspell_trim),
    ("CinemaAcoustic", "cinema_acoustic", _gen_cinema_acoustic),
    ("PlinthMarble", "plinth_marble", _gen_plinth_marble),
)


def _format_bytes(data: bytes, indent: str = "    ") -> str:
    lines: list[str] = []
    for off in range(0, len(data), 16):
        chunk = data[off : off + 16]
        lines.append(indent + ",".join(f"0x{b:02x}" for b in chunk) + ",")
    return "\n".join(lines)


def build_header(preview_dir: Path | None = None) -> tuple[str, dict[str, tuple[int, int, str]]]:
    entries: list[str] = []
    table: list[str] = []
    stats: dict[str, tuple[int, int, str]] = {}
    previews: list[tuple[str, bytearray]] = []

    for cpp_name, slug, fn in TEXTURES:
        rgb = fn()
        tga = _encode_tga24(rgb)
        packed = zlib.compress(tga, level=9)
        digest = hashlib.sha256(tga).hexdigest()[:16]
        stats[slug] = (len(tga), len(packed), digest)
        entries.append(
            f"// {slug}: {SIZE}x{SIZE} 24-bit TGA ({len(tga)} raw -> {len(packed)} zlib, sha256:{digest})\n"
            f"static const unsigned char kTex_{cpp_name}[] = {{\n"
            f"{_format_bytes(packed)}\n"
            f"}};"
        )
        table.append(
            f"    {{\"{slug}\", kTex_{cpp_name}, {len(packed)}u, {len(tga)}u, {SIZE}u, {SIZE}u}},"
        )
        if preview_dir is not None:
            previews.append((slug, rgb))

    if preview_dir is not None:
        preview_dir.mkdir(parents=True, exist_ok=True)
        for slug, rgb in previews:
            (preview_dir / f"{slug}.tga").write_bytes(_encode_tga24(rgb))
        try:
            from PIL import Image, ImageDraw
            cols, rows = 4, 3
            pad = 12
            label_h = 22
            sheet = Image.new("RGB", (cols * (SIZE + pad) + pad, rows * (SIZE + label_h + pad) + pad), (24, 28, 34))
            draw = ImageDraw.Draw(sheet)
            for idx, (slug, rgb) in enumerate(previews):
                r, c = divmod(idx, cols)
                ox = pad + c * (SIZE + pad)
                oy = pad + r * (SIZE + label_h + pad)
                img = Image.frombytes("RGB", (SIZE, SIZE), bytes(rgb))
                sheet.paste(img, (ox, oy))
                draw.text((ox + 4, oy + SIZE + 4), slug, fill=(230, 226, 215))
            sheet.save(preview_dir / "museum-textures-sheet.png")
        except ImportError:
            pass

    header = (
        "// SPDX-License-Identifier: GPL-3.0-only\n"
        "// Generated by port/tools/generate_museum_textures.py — do not edit by hand.\n"
        "// Original GPL-3.0-only seamless architectural textures for the Grand Museum.\n"
        "#ifndef CE_MUSEUM_TEXTURES_DATA_H\n"
        "#define CE_MUSEUM_TEXTURES_DATA_H\n\n"
        "namespace CeMuseumTexturesData {\n\n"
        "struct Entry {\n"
        "    const char *name;\n"
        "    const unsigned char *packed;\n"
        "    unsigned packedSize;\n"
        "    unsigned rawSize;\n"
        "    unsigned width;\n"
        "    unsigned height;\n"
        "};\n\n"
        + "\n\n".join(entries)
        + "\n\n"
        f"enum {{ kCount = {len(TEXTURES)} }};\n\n"
        "static const Entry kEntries[kCount] = {\n"
        + "\n".join(table)
        + "\n};\n\n"
        "} // namespace CeMuseumTexturesData\n"
        "#endif\n"
    )
    return header, stats


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT, help="output header path")
    ap.add_argument("--check", action="store_true", help="verify committed header matches generator")
    ap.add_argument("--preview", type=Path, default=None, help="optional directory for TGA + PNG contact sheet")
    args = ap.parse_args()

    text, stats = build_header(args.preview)
    if args.check:
        if not args.out.is_file():
            print(f"FAIL: missing generated header {args.out}", file=sys.stderr)
            return 1
        actual = args.out.read_text(encoding="utf-8")
        if actual != text:
            print(f"FAIL: {args.out} is out of date; re-run generate_museum_textures.py", file=sys.stderr)
            return 1
        total_packed = sum(p for _, p, _ in stats.values())
        print(f"ok: {len(stats)} museum textures match generator ({total_packed} compressed bytes, {len(text)} header bytes)")
        return 0

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, encoding="utf-8", newline="\n")
    total_packed = sum(p for _, p, _ in stats.values())
    print(f"wrote {args.out} ({len(stats)} textures, {total_packed} compressed bytes, {len(text)} header bytes)")
    for slug, (raw, packed, digest) in stats.items():
        print(f"  {slug:18s} {raw:7d} -> {packed:6d} bytes  sha256:{digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
