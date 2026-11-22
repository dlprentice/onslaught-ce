#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deterministic generator for Onslaught CE's original GPL-3.0-only PlayUnit 1P weapon textures.

Produces two 256x256 metallic/energy weapon texture atlases (Forseti pulse carbine and
Muspell assault rifle with honed bayonet steel), compresses each as a lossless 24-bit TGA
inside a zlib stream, and emits a plain-text C++ header (port/game/ce_playunit_textures_data.h).

Atlas quadrant layout (normalized UV):
  [0.0..0.5, 0.0..0.5]  Armored upper shroud (brushed faction alloy, curved panel seams, rivets, wear)
  [0.5..1.0, 0.0..0.5]  Dark receiver & ribbed grip (anodized gunmetal / oiled iron, cooling slots)
  [0.0..0.5, 0.5..1.0]  Turned barrel & metallic hardware (anisotropic brushed steel, collars, muzzle bluing)
  [0.5..1.0, 0.5..1.0]  Energy coil & optic core (glowing cyan / fiery amber cells inside dark heat-sink ribs)

Usage:
    python3 port/tools/generate_playunit_textures.py                # regenerate header
    python3 port/tools/generate_playunit_textures.py --check        # verify header is up to date
    python3 port/tools/generate_playunit_textures.py --preview DIR  # write PNG previews
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
DEFAULT_OUT = ROOT / "port/game/ce_playunit_textures_data.h"
SIZE = 256
HALF = SIZE // 2


def _clamp8(v: float) -> int:
    if v <= 0.0:
        return 0
    if v >= 255.0:
        return 255
    return int(v + 0.5)


def _hash2(ix: int, iy: int, seed: int) -> float:
    n = (ix * 374761393 + iy * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    n ^= n >> 16
    return (n & 0xFFFFFF) / 16777216.0


def _smooth_grid(period_x: int, period_y: int, seed: int) -> list[list[float]]:
    grid = [[_hash2(x, y, seed) for x in range(period_x)] for y in range(period_y)]
    xs: list[tuple[int, int, float]] = []
    for x in range(HALF):
        fx = x * (period_x / float(HALF))
        ix = int(fx)
        tx = fx - ix
        ux = tx * tx * (3.0 - 2.0 * tx)
        xs.append((ix % period_x, (ix + 1) % period_x, ux))
    out: list[list[float]] = []
    for y in range(HALF):
        fy = y * (period_y / float(HALF))
        iy = int(fy)
        ty = fy - iy
        uy = ty * ty * (3.0 - 2.0 * ty)
        y0 = iy % period_y
        y1 = (iy + 1) % period_y
        r0 = grid[y0]
        r1 = grid[y1]
        row = [0.0] * HALF
        for x in range(HALF):
            x0, x1, ux = xs[x]
            a = r0[x0] + (r0[x1] - r0[x0]) * ux
            b = r1[x0] + (r1[x1] - r1[x0]) * ux
            row[x] = a + (b - a) * uy
        out.append(row)
    return out


def _fbm_aniso(octaves: list[tuple[int, int, float]], seed: int) -> list[list[float]]:
    layers = [(_smooth_grid(px, py, seed + i * 131), w) for i, (px, py, w) in enumerate(octaves)]
    total = sum(w for _, w in layers) or 1.0
    out = [[0.0] * HALF for _ in range(HALF)]
    for layer, w in layers:
        s = (2.0 * w) / total
        for y in range(HALF):
            dst = out[y]
            src = layer[y]
            for x in range(HALF):
                dst[x] += (src[x] - 0.5) * s
    return out


def _encode_tga24(rgb: bytearray) -> bytes:
    header = struct.pack(
        "<BBBHHBHHHHBB",
        0, 0, 2,
        0, 0, 0,
        0, 0,
        SIZE, SIZE,
        24, 0x20,
    )
    bgr = bytearray(len(rgb))
    for i in range(0, len(rgb), 3):
        bgr[i] = rgb[i + 2]
        bgr[i + 1] = rgb[i + 1]
        bgr[i + 2] = rgb[i]
    return header + bytes(bgr)


def _write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    def chunk(tag: bytes, payload: bytes) -> bytes:
        crc = zlib.crc32(tag + payload) & 0xFFFFFFFF
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", crc)

    stride = width * 3
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(rgb[y * stride:(y + 1) * stride])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    path.write_bytes(data)


def _set_px(buf: bytearray, x: int, y: int, r: float, g: float, b: float) -> None:
    o = (y * SIZE + x) * 3
    buf[o] = _clamp8(r)
    buf[o + 1] = _clamp8(g)
    buf[o + 2] = _clamp8(b)


def _gen_rifle_atlas(muspell: bool) -> bytearray:
    rgb = bytearray(SIZE * SIZE * 3)
    seed_base = 7301 if muspell else 4109

    # Directional brushed metal grain + subtle surface variation
    brush_h = _fbm_aniso([(4, 32, 0.55), (8, 64, 0.30), (16, 64, 0.15)], seed_base + 1)
    wear = _fbm_aniso([(6, 6, 0.5), (14, 14, 0.3), (32, 32, 0.2)], seed_base + 17)
    fine = _fbm_aniso([(16, 16, 0.5), (32, 32, 0.35), (64, 64, 0.15)], seed_base + 37)

    # Quadrant A [0..127, 0..127]: Armored upper shroud
    for y in range(HALF):
        v = y / float(HALF - 1)
        for x in range(HALF):
            u = x / float(HALF - 1)
            g_b = brush_h[y][x]
            g_w = wear[y][x]
            g_f = fine[y][x]

            # Base metallic alloy gradient matching BEA's sunlit Forseti ceramic-blue & Muspell burnished crimson-iron
            sheen = 0.88 + 0.24 * math.sin(v * math.pi)
            if muspell:
                r = (168.0 + 30.0 * g_w + 16.0 * g_b) * sheen
                g = (66.0 + 16.0 * g_w + 10.0 * g_b) * sheen
                b = (48.0 + 12.0 * g_w + 8.0 * g_b) * sheen
            else:
                r = (142.0 + 24.0 * g_w + 14.0 * g_b) * sheen
                g = (172.0 + 26.0 * g_w + 16.0 * g_b) * sheen
                b = (212.0 + 26.0 * g_w + 16.0 * g_b) * sheen

            # BEA-style sleek curved armor seam & recessed panel lines
            curve_y = 0.26 + 0.08 * math.sin(u * math.pi * 2.0)
            curve_y2 = 0.74 - 0.08 * math.sin(u * math.pi * 2.0)
            seam_dist = min(abs(v - curve_y), abs(v - curve_y2))
            if seam_dist < 0.014:
                r *= 0.42
                g *= 0.44
                b *= 0.46
            elif seam_dist < 0.028:
                # Specular bevel along seam edge
                boost = 1.24 if (v > curve_y and v < 0.5) or v > curve_y2 else 0.82
                r *= boost
                g *= boost
                b *= boost

            # Cross panel seams at u = 0.32 and u = 0.72
            for sx in (0.32, 0.72):
                dx = abs(u - sx)
                if dx < 0.011:
                    r *= 0.45
                    g *= 0.46
                    b *= 0.48
                elif dx < 0.022 and u > sx:
                    r *= 1.18
                    g *= 1.18
                    b *= 1.18

            # Diagonal cooling vents in center band
            if 0.38 <= u <= 0.66 and 0.40 <= v <= 0.60:
                stripe = ((u * 26.0 + v * 12.0) % 1.0)
                if stripe < 0.34:
                    r *= 0.34
                    g *= 0.36
                    b *= 0.38
                elif stripe < 0.46:
                    r *= 1.16
                    g *= 1.16
                    b *= 1.16

            # Rivets along panel corners
            for rx in (0.10, 0.26, 0.38, 0.66, 0.78, 0.92):
                for ry in (0.12, 0.88):
                    d2 = (u - rx) ** 2 + (v - ry) ** 2
                    if d2 < 0.00045:
                        r = r * 0.50 + 28.0
                        g = g * 0.50 + 28.0
                        b = b * 0.50 + 28.0
                    elif d2 < 0.00095 and (u - rx + v - ry) < 0.0:
                        r *= 1.18
                        g *= 1.18
                        b *= 1.18

            # Subtle metallic edge wear near top/bottom borders
            edge = min(v, 1.0 - v)
            if edge < 0.08:
                t = (0.08 - edge) / 0.08
                chip = max(0.0, g_f + 0.35) * t * 38.0
                r += chip
                g += chip
                b += chip * (0.85 if muspell else 1.05)

            _set_px(rgb, x, y, r, g, b)

    # Quadrant B [128..255, 0..127]: Dark anodized receiver & tactical grip
    for y in range(HALF):
        v = y / float(HALF - 1)
        for x in range(HALF):
            u = x / float(HALF - 1)
            g_b = brush_h[y][x]
            g_f = fine[y][x]
            sheen = 0.90 + 0.20 * math.sin(v * math.pi)
            if muspell:
                r = (78.0 + 16.0 * g_b + 10.0 * g_f) * sheen
                g = (66.0 + 14.0 * g_b + 9.0 * g_f) * sheen
                b = (62.0 + 13.0 * g_b + 8.0 * g_f) * sheen
            else:
                r = (70.0 + 14.0 * g_b + 9.0 * g_f) * sheen
                g = (88.0 + 16.0 * g_b + 10.0 * g_f) * sheen
                b = (112.0 + 18.0 * g_b + 12.0 * g_f) * sheen

            # Tactical micro-knurling / horizontal machined ribs in lower half
            if v > 0.55:
                rib = math.sin(u * math.pi * 24.0) * math.sin(v * math.pi * 24.0)
                k = 1.0 + 0.14 * rib
                r *= k
                g *= k
                b *= k
            else:
                # Upper receiver machined rail grooves & inspection hatch
                if abs(v - 0.24) < 0.015 or abs(v - 0.44) < 0.015:
                    r *= 0.52
                    g *= 0.52
                    b *= 0.54
                if 0.18 <= u <= 0.82 and 0.28 <= v <= 0.40:
                    slot = (u * 18.0) % 1.0
                    if slot < 0.28:
                        r *= 0.45
                        g *= 0.45
                        b *= 0.48
            # Recessed bolts
            for rx in (0.14, 0.50, 0.86):
                d2 = (u - rx) ** 2 + (v - 0.12) ** 2
                if d2 < 0.0007:
                    r *= 0.42
                    g *= 0.42
                    b *= 0.42
                elif d2 < 0.0013:
                    r *= 1.25
                    g *= 1.25
                    b *= 1.25
            _set_px(rgb, HALF + x, y, r, g, b)

    # Quadrant C [0..127, 128..255]: Turned alloy barrel & honed steel
    for y in range(HALF):
        v = y / float(HALF - 1)
        for x in range(HALF):
            u = x / float(HALF - 1)
            g_b = brush_h[y][x]
            g_f = fine[y][x]
            # Radial cylindrical metallic sheen along v, muzzle heat gradient along u
            cyl = 0.78 + 0.36 * (0.5 + 0.5 * math.cos((v - 0.32) * math.pi * 2.0))
            muzzle_dark = 1.0 - 0.34 * max(0.0, (u - 0.72) / 0.28) ** 1.5
            if muspell:
                r = (132.0 + 20.0 * g_b + 10.0 * g_f) * cyl * muzzle_dark
                g = (124.0 + 18.0 * g_b + 9.0 * g_f) * cyl * muzzle_dark
                b = (116.0 + 16.0 * g_b + 8.0 * g_f) * cyl * muzzle_dark
                # Subtle heat-blued temper band near muzzle
                if 0.68 < u < 0.84:
                    hb = math.sin(((u - 0.68) / 0.16) * math.pi)
                    r += hb * 16.0
                    g -= hb * 8.0
                    b += hb * 18.0
            else:
                r = (122.0 + 18.0 * g_b + 10.0 * g_f) * cyl * muzzle_dark
                g = (144.0 + 20.0 * g_b + 11.0 * g_f) * cyl * muzzle_dark
                b = (168.0 + 22.0 * g_b + 12.0 * g_f) * cyl * muzzle_dark

            # Machined barrel collar rings along u
            for ring in (0.18, 0.22, 0.54, 0.58, 0.88):
                du = abs(u - ring)
                if du < 0.012:
                    r *= 0.44
                    g *= 0.44
                    b *= 0.46
                elif du < 0.024:
                    r *= 1.18
                    g *= 1.18
                    b *= 1.20

            # Longitudinal fluting lines along barrel
            flute = math.sin(v * math.pi * 12.0)
            if abs(flute) > 0.86 and 0.25 < u < 0.82:
                r *= 0.78
                g *= 0.80
                b *= 0.82
            _set_px(rgb, x, HALF + y, r, g, b)

    # Quadrant D [128..255, 128..255]: Energy coil & holographic sight core
    for y in range(HALF):
        v = y / float(HALF - 1)
        for x in range(HALF):
            u = x / float(HALF - 1)
            # Outer dark titanium frame + interior glowing plasma cell bars
            border = min(u, 1.0 - u, v, 1.0 - v)
            rib = ((u * 8.0) % 1.0)
            core_v = max(0.0, 1.0 - abs(v - 0.5) * 2.4)
            if border < 0.09 or (rib < 0.20 and 0.12 < u < 0.88):
                # Dark metallic heat-sink housing catching spill light
                spill = max(0.0, 0.18 - border) * 2.5 if border >= 0.05 else 0.0
                if muspell:
                    r = 46.0 + spill * 120.0
                    g = 32.0 + spill * 48.0
                    b = 28.0 + spill * 16.0
                else:
                    r = 28.0 + spill * 26.0
                    g = 42.0 + spill * 96.0
                    b = 54.0 + spill * 130.0
            else:
                pulse = 0.72 + 0.28 * math.sin((rib - 0.20) / 0.80 * math.pi)
                hot = core_v * pulse
                if muspell:
                    r = 195.0 + 60.0 * hot
                    g = 86.0 + 148.0 * (hot ** 1.1)
                    b = 18.0 + 120.0 * (hot ** 2.2)
                else:
                    r = 28.0 + 185.0 * (hot ** 2.2)
                    g = 155.0 + 100.0 * hot
                    b = 215.0 + 40.0 * hot
            _set_px(rgb, HALF + x, HALF + y, r, g, b)

    return rgb


TEXTURES = (
    ("forseti_rifle", lambda: _gen_rifle_atlas(False)),
    ("muspell_rifle", lambda: _gen_rifle_atlas(True)),
)


def _format_header() -> str:
    entries_src: list[str] = []
    meta_rows: list[str] = []
    for name, fn in TEXTURES:
        rgb = fn()
        tga = _encode_tga24(rgb)
        packed = zlib.compress(tga, 9)
        digest = hashlib.sha256(tga).hexdigest()[:16]
        var = f"kTex_{name}"
        lines: list[str] = []
        for i in range(0, len(packed), 16):
            chunk = packed[i:i + 16]
            lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
        entries_src.append(
            f"// {name}: {SIZE}x{SIZE} TGA raw={len(tga)} packed={len(packed)} sha256={digest}\n"
            f"static const unsigned char {var}[] = {{\n" + "\n".join(lines) + "\n};\n"
        )
        meta_rows.append(
            f'    {{"{name}", {SIZE}, {SIZE}, {len(tga)}u, {len(packed)}u, {var}}},'
        )

    return (
        "// SPDX-License-Identifier: GPL-3.0-only\n"
        "// Generated by port/tools/generate_playunit_textures.py — do not edit by hand.\n"
        "#ifndef CE_PLAYUNIT_TEXTURES_DATA_H\n"
        "#define CE_PLAYUNIT_TEXTURES_DATA_H\n\n"
        "namespace CePlayUnitTexturesData {\n\n"
        "struct Entry {\n"
        "    const char *name;\n"
        "    unsigned width;\n"
        "    unsigned height;\n"
        "    unsigned rawSize;\n"
        "    unsigned packedSize;\n"
        "    const unsigned char *packed;\n"
        "};\n\n"
        + "\n".join(entries_src)
        + "\nstatic const Entry kEntries[] = {\n"
        + "\n".join(meta_rows)
        + "\n};\n\n"
        + f"static const unsigned kCount = {len(TEXTURES)}u;\n\n"
        + "} // namespace CePlayUnitTexturesData\n\n"
        + "#endif\n"
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--preview", type=Path, default=None)
    args = ap.parse_args()

    if args.preview is not None:
        args.preview.mkdir(parents=True, exist_ok=True)
        for name, fn in TEXTURES:
            rgb = fn()
            _write_png(args.preview / f"{name}.png", SIZE, SIZE, bytes(rgb))
        print(f"wrote previews to {args.preview}")

    text = _format_header()
    if args.check:
        if not args.out.is_file() or args.out.read_text(encoding="utf-8") != text:
            print(f"FAIL: {args.out} is out of date; re-run generate_playunit_textures.py", file=sys.stderr)
            return 1
        print(f"PASS: {args.out} matches generator")
        return 0

    args.out.write_text(text, encoding="utf-8")
    print(f"wrote {args.out} ({len(text)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
