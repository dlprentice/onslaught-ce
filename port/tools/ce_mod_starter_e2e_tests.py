#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""End-to-end verification of a schema-1 Onslaught CE example starter mod.

    python3 -B port/tools/ce_mod_starter_e2e_tests.py [--exe PATH] [--game-dir PATH] [--shot-out PATH] [--require-live] [-v]

Verifies against a real built Linux `bea` binary and read-only `BEA_GAME_DIR` (never mutating `BEA_GAME_DIR`
or committing retail bytes):
  1. Generates a clean schema-1 example mod (`ce-example-starter`) inside a temporary user profile folder
     from `BEA_GAME_DIR` using `port/tools/mods.py`.
  2. Runs `bea --check-mods ce-example-starter` and asserts exit 0 and `result: 1 passed, 0 refused`.
  3. Causal negative mutant: corrupts 1 byte of the mod payload and asserts `bea --check-mods ce-example-starter`
     exits 1 and prints `refused ce-example-starter` and `result: 0 passed, 1 refused`, then restores the byte
     and verifies exit 0 returns.
  4. Boots offscreen Linux `bea` with `mods_menu=1` and `mods=ce-example-starter` (with no faithful-mode fence
     variables set), navigates Main Menu -> Onslaught CE -> Mods, verifies the runtime log confirms
     `ce-example-starter` is active, and captures a 1280x720 PNG of the in-game Mods UI.
"""
from __future__ import annotations

import argparse
import binascii
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
TOOLS = PORT / "tools"
sys.path.insert(0, str(TOOLS))

import mods as mods_tool  # noqa: E402

DEFAULT_EXE = ROOT / "local-data/port/build/linux/bea"
SCRATCH = ROOT / "local-data/port/ce-mod-starter-e2e-tests"

SDL_RETURN = (40, 0x1C)
SDL_DOWN = (81, 0x150)
SDL_UP = (82, 0x148)

CLI_EXE: Path | None = None
CLI_GAME_DIR: Path | None = None
CLI_SHOT_OUT: Path | None = None
CLI_REQUIRE_LIVE = False


def resolve_runtime_libs() -> list[str]:
    roots: list[Path] = []
    if os.environ.get("BEA_PORT_DATA"):
        roots.append(Path(os.environ["BEA_PORT_DATA"]))
    roots.append(ROOT / "local-data/port-deps-canonical")
    git = subprocess.run(
        ["git", "-C", str(ROOT), "rev-parse", "--git-common-dir"],
        capture_output=True,
        text=True,
    )
    if git.returncode == 0 and git.stdout.strip():
        common = (ROOT / git.stdout.strip()).resolve()
        roots.append(common.parent / "local-data/port-deps-canonical")
    for root in roots:
        if (root / "dxvk-native/usr/lib32").is_dir():
            dirs = [
                root / "prefix-sniper/lib",
                root / "dxvk-native/usr/lib32",
                root / "ffmpeg-shared-sniper/linux/usr/lib",
            ]
            return [str(d) for d in dirs if d.is_dir()]
    return []


def build_fake_keys(presses: list[tuple[int, tuple[int, int]]], hold_frames: int = 4) -> str:
    """Build a sorted BEA_PORT_FAKE_KEYS string from [(present, (sdl_scan, pc_scan)), ...]."""
    tokens: list[tuple[int, str]] = []
    for present, (sdl, raw) in presses:
        tokens.append((present, f"{present}:{sdl}:{raw:x}:d"))
        tokens.append((present + hold_frames, f"{present + hold_frames}:{sdl}:{raw:x}:u"))
    tokens.sort(key=lambda item: item[0])
    return ",".join(tok for _, tok in tokens)


def mods_menu_key_script() -> tuple[str, int, int]:
    """Return (fake_keys, shot_present, quit_present) at 30 Hz feel pacing to open Title -> Main Menu -> Onslaught CE -> Mods.

    FEPMain clamps at its last row (row 7 = Quit, always available) and never wraps, with row 6 = Onslaught CE
    immediately above Quit. Pressing Down 8 times then Up once selects Onslaught CE deterministically regardless
    of career-in-progress, language-row, or controller-count state. Inside the Onslaught CE hub (with CE_NETWORK_OFF,
    extras=0, catalogue=0, mods_menu=1), row 0 is Bonus Levels and row 1 is Mods.
    """
    presses: list[tuple[int, tuple[int, int]]] = [
        (105, SDL_RETURN),  # Dismiss FEP_INTRO ("Click to start") -> FEP_MAIN
        (195, SDL_DOWN),    # Clamp to bottom of FEP_MAIN (Quit, row 7)
        (205, SDL_DOWN),
        (215, SDL_DOWN),
        (225, SDL_DOWN),
        (235, SDL_DOWN),
        (245, SDL_DOWN),
        (255, SDL_DOWN),
        (265, SDL_DOWN),
        (278, SDL_UP),      # Step up from Quit (row 7) -> Onslaught CE (row 6)
        (292, SDL_RETURN),  # Open Onslaught CE hub (50-tick transition; row 0 = Bonus Levels)
        (390, SDL_DOWN),    # Bonus Levels -> Mods
        (405, SDL_RETURN),  # Open Mods page
    ]
    return build_fake_keys(presses, hold_frames=3), 430, 440


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    """Read a binary P6 PPM captured by BEA_PORT_SHOT into (width, height, rgb_bytes)."""
    raw = path.read_bytes()
    idx = 0
    tokens: list[bytes] = []
    while len(tokens) < 4:
        while idx < len(raw) and raw[idx] in b" \t\r\n":
            idx += 1
        if idx < len(raw) and raw[idx] == ord("#"):
            while idx < len(raw) and raw[idx] != ord("\n"):
                idx += 1
            continue
        start = idx
        while idx < len(raw) and raw[idx] not in b" \t\r\n":
            idx += 1
        if start == idx:
            raise ValueError(f"{path}: truncated PPM header")
        tokens.append(raw[start:idx])
    if tokens[0] != b"P6":
        raise ValueError(f"{path}: unsupported PPM magic {tokens[0]!r}")
    width, height, maxval = int(tokens[1]), int(tokens[2]), int(tokens[3])
    if maxval != 255 or width <= 0 or height <= 0:
        raise ValueError(f"{path}: unexpected PPM geometry {width}x{height} maxval={maxval}")
    # Exactly one whitespace byte follows maxval in P6 PPM.
    pixel_start = idx + 1
    expected = width * height * 3
    pixels = raw[pixel_start : pixel_start + expected]
    if len(pixels) != expected:
        raise ValueError(f"{path}: expected {expected} RGB bytes, got {len(pixels)}")
    return width, height, pixels


def encode_png_rgb(width: int, height: int, rgb: bytes) -> bytes:
    """Encode packed 24-bit RGB bytes as a standard PNG without third-party dependencies."""
    if len(rgb) != width * height * 3:
        raise ValueError("RGB buffer length does not match width * height * 3")

    def chunk(tag: bytes, payload: bytes) -> bytes:
        crc = binascii.crc32(tag + payload) & 0xFFFFFFFF
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", crc)

    stride = width * 3
    scanlines = bytearray()
    for y in range(height):
        scanlines.append(0)  # filter type 0 (None)
        scanlines.extend(rgb[y * stride : (y + 1) * stride])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    idat = zlib.compress(bytes(scanlines), level=6)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


def image_luma_stats(rgb: bytes) -> tuple[int, int, float]:
    """Return (min_luma, max_luma, mean_luma) sampled across RGB pixels."""
    step = max(3, (len(rgb) // 3 // 20000) * 3)
    lo = 255
    hi = 0
    total = 0
    count = 0
    for i in range(0, len(rgb) - 2, step):
        luma = (rgb[i] * 77 + rgb[i + 1] * 150 + rgb[i + 2] * 29) >> 8
        if luma < lo:
            lo = luma
        if luma > hi:
            hi = luma
        total += luma
        count += 1
    return lo, hi, (total / count) if count else 0.0


def headless_env(user_dir: Path, game_dir: Path, run_dir: Path) -> dict[str, str]:
    """Construct a strict offscreen, silent, mod-enabled (unfenced) environment for `bea`."""
    for sub in (user_dir, run_dir / "cache", run_dir / "runtime"):
        sub.mkdir(parents=True, exist_ok=True)
    env = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "HOME": str(user_dir),
        "SDL_VIDEO_DRIVER": "offscreen",
        "BEA_PORT_REQUIRE_OFFSCREEN": "1",
        "SDL_AUDIO_DRIVER": "dummy",
        "BEA_PORT_REQUIRE_DUMMY_AUDIO": "1",
        "BEA_REPORT_NO_OPEN": "1",
        "BROWSER": "/bin/true",
        "DXVK_WSI_DRIVER": "SDL3",
        "DXVK_LOG_PATH": str(run_dir),
        "XDG_CACHE_HOME": str(run_dir / "cache"),
        "XDG_RUNTIME_DIR": str(run_dir / "runtime"),
        "BEA_USER_DIR": str(user_dir),
        "BEA_GAME_DIR": str(game_dir),
        "BEA_NO_UPDATE_CHECK": "1",
        "PYTHONDONTWRITEBYTECODE": "1",
    }
    libs = resolve_runtime_libs()
    if libs:
        env["LD_LIBRARY_PATH"] = ":".join(libs)
    return env


def resolve_live_inputs() -> tuple[Path | None, Path | None]:
    exe = CLI_EXE or Path(os.environ.get("BEA_EXE", str(DEFAULT_EXE)))
    game_env = os.environ.get("BEA_GAME_DIR")
    game = CLI_GAME_DIR or (Path(game_env) if game_env else None)
    return (exe if exe.is_file() else None), (game if game and game.is_dir() else None)


class ModStarterHelperTests(unittest.TestCase):
    def test_ppm_and_png_encoding_roundtrip(self):
        with tempfile.TemporaryDirectory(prefix="mod-e2e-ppm-") as tmp:
            ppm = Path(tmp) / "frame.ppm"
            rgb = bytes([10, 20, 30, 240, 200, 100, 0, 128, 255, 255, 255, 255])
            ppm.write_bytes(b"P6\n# authored test frame\n2 2\n255\n" + rgb)
            w, h, pixels = read_ppm(ppm)
            self.assertEqual((w, h, pixels), (2, 2, rgb))
            png = encode_png_rgb(w, h, pixels)
            self.assertTrue(png.startswith(b"\x89PNG\r\n\x1a\n"))
            self.assertIn(b"IHDR", png)
            self.assertIn(b"IDAT", png)
            self.assertTrue(png.endswith(b"IEND\xaeB`\x82"))
            lo, hi, mean = image_luma_stats(pixels)
            self.assertLess(lo, 30)
            self.assertGreater(hi, 240)
            self.assertGreater(mean, 50.0)

    def test_mods_menu_key_script_is_monotonic_and_paired(self):
        keys, shot_at, quit_at = mods_menu_key_script()
        tokens = keys.split(",")
        self.assertEqual(len(tokens) % 2, 0)
        presents = [int(tok.split(":", 1)[0]) for tok in tokens]
        self.assertEqual(presents, sorted(presents))
        self.assertLess(presents[-1], shot_at)
        self.assertLess(shot_at, quit_at)


class ModStarterLiveE2ETests(unittest.TestCase):
    def setUp(self):
        self.exe, self.game_dir = resolve_live_inputs()
        if self.exe is None or self.game_dir is None:
            missing = []
            if self.exe is None:
                missing.append("built Linux bea binary")
            if self.game_dir is None:
                missing.append("BEA_GAME_DIR")
            reason = f"missing live inputs: {', '.join(missing)}"
            if CLI_REQUIRE_LIVE:
                self.fail(reason)
            self.skipTest(reason)

    def _create_example_mod(self, user_dir: Path) -> Path:
        mod_id, mod_dir = mods_tool.new_mod(
            "ce-example-starter",
            user_dir,
            self.game_dir,
            "CE Example Starter",
            author="Onslaught CE",
            description="Verified example starter mod loaded by Onslaught CE.",
        )
        self.assertEqual(mod_id, "ce-example-starter")
        manifest = json.loads((mod_dir / "mod.json").read_text(encoding="utf-8"))
        self.assertEqual(1, manifest["schema"])
        self.assertEqual("ce-example-starter", manifest["id"])
        self.assertEqual(1, len(manifest["files"]))
        return mod_dir

    def test_check_mods_positive_and_one_byte_corruption_mutant(self):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        run_dir = Path(tempfile.mkdtemp(prefix="check-mods-", dir=SCRATCH))
        try:
            user_dir = run_dir / "user"
            user_dir.mkdir(parents=True)
            mod_dir = self._create_example_mod(user_dir)
            env = headless_env(user_dir, self.game_dir, run_dir)

            # Positive check: valid schema-1 mod passes cleanly.
            good = subprocess.run(
                [str(self.exe), "--check-mods", "ce-example-starter"],
                cwd=str(self.game_dir),
                env=env,
                capture_output=True,
                text=True,
                timeout=60,
            )
            good_out = (good.stdout or "") + (good.stderr or "")
            self.assertEqual(0, good.returncode, good_out)
            self.assertIn("result: 1 passed, 0 refused", good_out)

            # Causal negative mutant: corrupt 1 byte of the mod payload -> must refuse with exit 1.
            payload = mod_dir / mods_tool.BE_TEXTURE
            original = payload.read_bytes()
            mutated = bytearray(original)
            mutated[len(mutated) // 2] ^= 0x01
            payload.write_bytes(bytes(mutated))

            bad = subprocess.run(
                [str(self.exe), "--check-mods", "ce-example-starter"],
                cwd=str(self.game_dir),
                env=env,
                capture_output=True,
                text=True,
                timeout=60,
            )
            bad_out = (bad.stdout or "") + (bad.stderr or "")
            self.assertEqual(1, bad.returncode, bad_out)
            self.assertIn("refused ce-example-starter", bad_out)
            self.assertIn("result: 0 passed, 1 refused", bad_out)

            # Restore original byte and confirm recovery.
            payload.write_bytes(original)
            recovered = subprocess.run(
                [str(self.exe), "--check-mods", "ce-example-starter"],
                cwd=str(self.game_dir),
                env=env,
                capture_output=True,
                text=True,
                timeout=60,
            )
            rec_out = (recovered.stdout or "") + (recovered.stderr or "")
            self.assertEqual(0, recovered.returncode, rec_out)
            self.assertIn("result: 1 passed, 0 refused", rec_out)
        finally:
            shutil.rmtree(run_dir, ignore_errors=True)

    def test_live_offscreen_mods_menu_loads_example_starter_and_captures_png(self):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        run_dir = Path(tempfile.mkdtemp(prefix="live-mods-ui-", dir=SCRATCH))
        try:
            user_dir = run_dir / "user"
            user_dir.mkdir(parents=True)
            self._create_example_mod(user_dir)
            (user_dir / "ce.ini").write_text(
                "mods_menu=1\nmods=ce-example-starter\n",
                encoding="ascii",
            )

            env = headless_env(user_dir, self.game_dir, run_dir)
            fake_keys, shot_at, quit_at = mods_menu_key_script()
            ppm_path = run_dir / "mods-menu.ppm"
            # Pace offscreen Presents at 30 Hz without enabling BEA_PORT_CLOCK (which fences mods off),
            # so wall-clock menu fade-ins and page transitions align deterministically with Present counts.
            env["BEA_PORT_FEEL_LOG"] = str(run_dir / "feel.log")
            env["BEA_PORT_FEEL_PACE_HZ"] = "30"
            env["BEA_PORT_FAKE_KEYS"] = fake_keys
            env["BEA_PORT_SHOT"] = f"{shot_at}:{ppm_path}"
            env["BEA_PORT_QUIT"] = str(quit_at)

            proc = subprocess.run(
                [str(self.exe), "-skipfmv", "-forcewindowed", "-res", "1280", "720"],
                cwd=str(self.game_dir),
                env=env,
                capture_output=True,
                text=True,
                timeout=180,
            )
            log = (proc.stdout or "") + (proc.stderr or "")
            self.assertEqual(0, proc.returncode, log)
            self.assertIn("1 active, 1 game files replaced", log)
            self.assertIn('active 1: ce-example-starter "CE Example Starter"', log)
            self.assertTrue(ppm_path.is_file(), f"screenshot not written:\n{log}")

            width, height, rgb = read_ppm(ppm_path)
            self.assertEqual((1280, 720), (width, height))
            lo, hi, mean = image_luma_stats(rgb)
            self.assertGreater(hi - lo, 80, f"captured frame appears flat/blank (luma {lo}..{hi}, mean {mean:.1f})")

            png_bytes = encode_png_rgb(width, height, rgb)
            self.assertTrue(png_bytes.startswith(b"\x89PNG\r\n\x1a\n"))

            shot_out = CLI_SHOT_OUT or (
                Path(os.environ["BEA_MOD_STARTER_SHOT_OUT"])
                if os.environ.get("BEA_MOD_STARTER_SHOT_OUT")
                else None
            )
            if shot_out is not None:
                shot_out.parent.mkdir(parents=True, exist_ok=True)
                shot_out.write_bytes(png_bytes)
        finally:
            shutil.rmtree(run_dir, ignore_errors=True)


def parse_args(argv: list[str]) -> list[str]:
    global CLI_EXE, CLI_GAME_DIR, CLI_SHOT_OUT, CLI_REQUIRE_LIVE
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--exe", type=Path, default=None)
    parser.add_argument("--game-dir", type=Path, default=None)
    parser.add_argument("--shot-out", type=Path, default=None)
    parser.add_argument("--require-live", action="store_true")
    known, rest = parser.parse_known_args(argv)
    CLI_EXE = known.exe
    CLI_GAME_DIR = known.game_dir
    CLI_SHOT_OUT = known.shot_out
    CLI_REQUIRE_LIVE = known.require_live
    return [sys.argv[0], *rest]


if __name__ == "__main__":
    unittest.main(argv=parse_args(sys.argv[1:]))
