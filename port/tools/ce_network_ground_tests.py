#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the Building Defence ground blend and light map of a network co-op game (patch 0232, game/ce_network_ground.cpp).

The production module is compiled with a driver at several optimisation levels and must print the same table each
time: which (sharing, level, heights) loads are eligible, what the rewrite does to a mixer map and its light map, every weight
of the 513 x 513 sample grid and every light level, which must also equal this file's independent integer reference and stay within the documented
range. Neighbouring tiles must agree on their shared edge samples (no seams), a map with a slot that has no data must be
left untouched, and patch 0232 must call the module only inside CE_NETWORK_OFF guards, right after MIXERMAP.Init, and
CDXWater::Render must draw the water plane one unit lower while the rewritten map is loaded."""
from __future__ import annotations
import hashlib
from pathlib import Path
import re
import resource
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
PATCH = PORT / "patches/0232-network-coop-ground-blend.patch"
LOW, HIGH = -40, 36
LIGHT_LOW, LIGHT_HIGH = 28, 33

DRIVER = r"""
#include "ce_network_ground.h"
#include <stdio.h>
#include <string.h>
struct Slot { int mCount; unsigned char *mData; unsigned short mTextures[6]; };
static short heights[64 * 64 * 81];
static unsigned char data[4096][162];
static Slot slots[4096];
static unsigned char lights[512 * 512];
static void reset() { memset(lights, 0x7f, sizeof lights); for (int s = 0; s < 4096; s++) { slots[s].mCount = 2; slots[s].mData = data[s]; memset(data[s], 0x55, 162);
	for (int t = 0; t < 6; t++) slots[s].mTextures[t] = (unsigned short)(t + 2); } }
int main() {
	const unsigned levels[] = { 0, 100, 850, 855, 856, 857, 858, 859 };
	for (int sharing = 0; sharing < 2; sharing++) for (unsigned level : levels) {
		heights[12345] = 0; printf("eligible %d %u flat=%d", sharing, level, CeGroundEligible(sharing, level, heights));
		heights[12345] = 7; printf(" hilly=%d null=%d\n", CeGroundEligible(sharing, level, heights), CeGroundEligible(sharing, level, 0));
	}
	heights[12345] = 0;
	reset(); slots[4095].mData = 0; slots[4095].mCount = 0;
	int r = CeGroundRewrite(slots, 4096, lights); int same = 1;
	for (int s = 0; s < 4095; s++) { same &= slots[s].mCount == 2 && slots[s].mTextures[0] == 2; for (int k = 0; k < 162; k++) same &= data[s][k] == 0x55; }
	same &= lights[0] == 0x7f && lights[512 * 512 - 1] == 0x7f;
	printf("missing-data rewrite=%d untouched=%d\n", r, same);
	reset(); r = CeGroundRewrite(slots, 4096, 0); printf("no-lights rewrite=%d untouched=%d\n", r, slots[5].mCount == 2 && data[5][3] == 0x55);
	reset(); slots[17].mCount = 0;
	r = CeGroundRewrite(slots, 4096, lights); printf("empty-slot rewrite=%d untouched=%d\n", r, slots[0].mCount == 2 && data[0][0] == 0x55);
	reset(); printf("short-map rewrite=%d\n", CeGroundRewrite(slots, 4095, lights));
	reset(); r = CeGroundRewrite(slots, 4096, lights);
	int layout = 1, seams = 0, tail = 1;
	for (int s = 0; s < 4096; s++) {
		layout &= slots[s].mCount == 1 && slots[s].mTextures[0] == CE_GROUND_ROCK && slots[s].mTextures[1] == 3;
		for (int k = 81; k < 162; k++) tail &= data[s][k] == 0x55;
		const int tx = s % 64, ty = s / 64;
		for (int i = 0; i < 9; i++) {
			if (tx < 63 && data[s][i * 9 + 8] != data[s + 1][i * 9]) seams++;
			if (ty < 63 && data[s][72 + i] != data[s + 64][i]) seams++;
			for (int j = 0; j < 9; j++) if ((signed char)data[s][i * 9 + j] != CeGroundWeight(tx * 8 + j, ty * 8 + i)) layout = 0;
		}
	}
	printf("rewrite=%d layout=%d seams=%d tail-untouched=%d\n", r, layout, seams, tail);
	int lit = 1;
	for (int i = 0; i < 512 * 512; i++) lit &= lights[i] == CeGroundLight(i % 512, i / 512);
	printf("lights=%d\n", lit);
	CeGroundSetApplied(1); float a = CeGroundWaterDrop(); CeGroundSetApplied(0);
	printf("water drop=%g then %g\n", a, CeGroundWaterDrop());
	for (int y = 0; y <= 512; y++) for (int x = 0; x <= 512; x++) printf("%d%c", CeGroundWeight(x, y), x == 512 ? '\n' : ' ');
	for (int y = 0; y < 512; y++) for (int x = 0; x < 512; x++) printf("%d%c", CeGroundLight(x, y), x == 511 ? '\n' : ' ');
	return 0;
}
"""


def reference_noise(x, y, cell, seed):
    """An independent integer transcription of ce_network_ground.cpp's noise (Python integers, masked to 32 bits)."""
    def hash2(ix, iy, seed):
        h = (ix * 0x27d4eb2d + iy * 0x165667b1 + seed * 0x9e3779b9) & 0xffffffff
        h ^= h >> 15
        h = (h * 0x85ebca6b) & 0xffffffff
        h ^= h >> 13
        h = (h * 0xc2b2ae35) & 0xffffffff
        h ^= h >> 16
        return h & 255

    def smooth(t, cell):
        u = t * 256 // cell
        return (u * u * (768 - 2 * u)) >> 16

    ix, iy = x // cell, y // cell
    fx, fy = smooth(x - ix * cell, cell), smooth(y - iy * cell, cell)
    a, b, c, d = hash2(ix, iy, seed), hash2(ix + 1, iy, seed), hash2(ix, iy + 1, seed), hash2(ix + 1, iy + 1, seed)
    top, bottom = a * 256 + (b - a) * fx, c * 256 + (d - c) * fx
    return (top * 256 + (bottom - top) * fy) >> 16


def reference_light(x, y):
    n = (reference_noise(x, y, 96, 4) * 3 + reference_noise(x, y, 24, 5)) // 4
    return LIGHT_LOW + n * (LIGHT_HIGH - LIGHT_LOW) // 255


def reference_weight(x, y):
    octaves = ((40, 4, 1), (14, 2, 2), (5, 1, 3))
    n = sum(reference_noise(x, y, cell, seed) * weight for cell, weight, seed in octaves) // sum(w for _, w, _ in octaves)
    return LOW + n * (HIGH - LOW) // 255


def compile_and_run(work, flags):
    exe = work / ("driver" + flags.replace(" ", "").replace("-", "_"))
    driver = work / "driver.cpp"
    driver.write_text(DRIVER)
    subprocess.run(["g++", "-std=c++17", *flags.split(), "-I", str(PORT / "game"), str(driver),
                    str(PORT / "game/ce_network_ground.cpp"), "-o", str(exe)], check=True, timeout=120)
    return subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=120,
                          preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0))).stdout


def check_output(out):
    lines = out.splitlines()
    eligible = [l for l in lines if l.startswith("eligible ")]
    for line in eligible:
        sharing, level = (int(v) for v in line.split()[1:3])
        want = 1 if sharing and level in (856, 858) else 0
        if line != f"eligible {sharing} {level} flat={want} hilly=0 null=0":
            raise AssertionError("eligibility: " + line)
    for want in ("missing-data rewrite=0 untouched=1", "no-lights rewrite=0 untouched=1", "empty-slot rewrite=0 untouched=1",
                 "short-map rewrite=0", "rewrite=1 layout=1 seams=0 tail-untouched=1", "lights=1",
                 "water drop=1 then 0"):
        if want not in lines:
            raise AssertionError("rewrite table lacks: " + want)
    rows = lines[len(eligible) + 7:]
    grid = [[int(v) for v in l.split()] for l in rows[:513]]
    light = [[int(v) for v in l.split()] for l in rows[513:]]
    if len(light) != 512 or any(len(row) != 512 for row in light):
        raise AssertionError("light grid is not 512 x 512")
    flat_light = [v for row in light for v in row]
    if min(flat_light) < LIGHT_LOW or max(flat_light) > LIGHT_HIGH or len(set(flat_light)) < 3:
        raise AssertionError(f"light levels {min(flat_light)}..{max(flat_light)} leave {LIGHT_LOW}..{LIGHT_HIGH} or do not vary")
    for y in range(0, 512, 11):
        for x in range(0, 512, 5):
            if light[y][x] != reference_light(x, y):
                raise AssertionError(f"light ({x}, {y}) = {light[y][x]}, the reference says {reference_light(x, y)}")
    if len(grid) != 513 or any(len(row) != 513 for row in grid):
        raise AssertionError("weight grid is not 513 x 513")
    flat = [v for row in grid for v in row]
    if min(flat) < LOW or max(flat) > HIGH:
        raise AssertionError(f"weights {min(flat)}..{max(flat)} leave {LOW}..{HIGH}")
    # The blend should vary: some ground under no rock, some well under it (not a constant or a two-value map).
    if min(flat) > LOW + 20 or max(flat) < HIGH - 20 or len(set(flat)) < 40:
        raise AssertionError(f"weights barely vary: {min(flat)}..{max(flat)}, {len(set(flat))} values")
    for y in range(0, 513, 7):
        for x in range(0, 513, 3):
            if grid[y][x] != reference_weight(x, y):
                raise AssertionError(f"weight ({x}, {y}) = {grid[y][x]}, the reference says {reference_weight(x, y)}")
    return hashlib.sha256(out.encode()).hexdigest()


def check_patch():
    text = PATCH.read_text(encoding="latin-1")
    files = re.findall(r"^--- a/(\S+)$", text, flags=re.M)
    if files != ["Map.cpp", "DXWater.cpp"]:
        raise AssertionError(f"patch 0232 must change Map.cpp and DXWater.cpp only, not {files}")
    added = [l[1:] for l in text.splitlines() if l.startswith("+") and not l.startswith("+++")]
    depth, calls, hides = 0, 0, 0
    for line in added:
        if line.startswith("#ifndef CE_NETWORK_OFF"):
            depth += 1
        elif line.startswith("#endif"):
            depth -= 1
        elif depth != 1:
            raise AssertionError("an added line outside the CE_NETWORK_OFF guard: " + line)
        calls += "CeGroundRewrite(MIXERMAP.mSlots, MIXER_MAP_SLOTS, MIXERMAP.mMap)" in line
        hides += line.strip() == "wlevel += CeGroundWaterDrop();"
    if depth or calls != 1 or hides != 1:
        raise AssertionError("patch 0232 must add one guarded rewrite and one guarded water test")
    hunk = re.search(r"^ \tMIXERMAP\.Init\(c\);\n\+#ifndef CE_NETWORK_OFF\n(\+\t//.*\n)*\+\tCeGroundSetApplied\(CeGroundEligible\(CE_LockstepSharing\(\), "
                     r"CE_LockstepLevel\(\), mHeightData\) &&\n\+\t\tCeGroundRewrite\(", text, flags=re.M)
    if not hunk:
        raise AssertionError("the rewrite must follow MIXERMAP.Init, be gated by the session, the level and the heights, and set the water flag")
    water = re.search(r"^ void CDXWater::Render\(float wlevel\)\t// FUNCTION: 0x0055b6c0\n \{\n\+#ifndef CE_NETWORK_OFF\n(\+\t//.*\n)*"
                      r"\+\twlevel \+= CeGroundWaterDrop\(\);\n\+#endif\n", text, flags=re.M)
    if not water:
        raise AssertionError("the water test must open CDXWater::Render and lower the plane before it draws anything")
    if "0232-network-coop-ground-blend.patch" not in (PORT / "network/PATCHES.txt").read_text().split():
        raise AssertionError("patch 0232 is not listed in port/network/PATCHES.txt")


def main():
    check_patch()
    with tempfile.TemporaryDirectory(prefix="ce-ground-", dir=ROOT / "local-data" if (ROOT / "local-data").is_dir() else None) as tmp:
        digests = {flags: check_output(compile_and_run(Path(tmp), flags)) for flags in ("-O0", "-O2", "-O2 -m32 -mfpmath=387")}
    if len(set(digests.values())) != 1:
        raise AssertionError(f"the table depends on compiler flags: {digests}")
    print(f"ce_network_ground_tests: ok (eligibility, rewrite, seams, reference weights and light levels, {len(digests)} flag sets agree)")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, subprocess.CalledProcessError) as error:
        print(f"ce_network_ground_tests: FAIL: {error}", file=sys.stderr)
        sys.exit(1)
