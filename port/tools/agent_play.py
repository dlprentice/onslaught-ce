#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offscreen agent-play harness and repeatable visual inspection gallery for Onslaught CE.

Runs Onslaught CE 100% offscreen (SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy,
DISPLAY/WAYLAND_DISPLAY/DBUS stripped, xdg-open shimmed, isolated private user folder)
so automated agents, reviewers, and visual critics can drive menus, start levels, steer
and fire in gameplay, capture crisp full-window PNGs, and inspect structured page/HUD/
world telemetry JSON without ever touching the user's desktop or live profile.

Two modes:
  1. Ad-hoc scripted play (test-kit twin OR plain public candidate):
       python3 port/tools/agent_play.py --out OUT \\
           --step "wait ticks 600" --step "key RETURN" --step "wait page fe:main" \\
           --step "dump main" --step "shot main"
       python3 port/tools/agent_play.py --out OUT --steps my_steps.txt \\
           --ce-ini commander=1 --level 100 --clock real
       python3 port/tools/agent_play.py --out OUT --exe BUILD/bea \\
           --keys my_keys.txt --shot-at 900 --quit-at 960

  2. Repeatable full visual inspection gallery (1280x720 widescreen PNGs + telemetry):
       python3 port/tools/agent_play.py --gallery --out OUT \\
           --exe TWIN/bea --fixtures FIXTURES_DIR
       python3 port/tools/agent_play.py --list-gallery

Clock note:
  Use --clock fixed (default, dt=0.016667) for deterministic menus, Extras, Catalogue,
  Mods, Museum, and Map Editor runs. Use --clock real (leaves BEA_PORT_CLOCK unset) for
  features fenced against test clocks: Photo Mode (F11), Commander Live View (M), and
  Play Any Unit (E).
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import re
import resource
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RES = "1280x720"
DEFAULT_DT = "0.016667"

EXPERIMENTAL_KEYS = frozenset({"map_editor", "museum", "commander", "play_unit"})

# Key mapping for exact-binary --keys scripts (SDL3 scancode, DirectInput PC scan code).
FAKE_KEY_TABLE: dict[str, tuple[int, int]] = {
    "RETURN": (40, 0x1C),
    "ESC": (41, 0x01),
    "BACKSPACE": (42, 0x0E),
    "TAB": (43, 0x0F),
    "SPACE": (44, 0x39),
    "RIGHT": (79, 0x14D),
    "LEFT": (80, 0x14B),
    "DOWN": (81, 0x150),
    "UP": (82, 0x148),
    "PGUP": (75, 0x149),
    "PGDN": (78, 0x151),
    "HOME": (74, 0x147),
    "END": (77, 0x14F),
    "DELETE": (76, 0x153),
    "GRAVE": (53, 0x29),
    "F1": (58, 0x3B),
    "F2": (59, 0x3C),
    "F3": (60, 0x3D),
    "F4": (61, 0x3E),
    "F5": (62, 0x3F),
    "F6": (63, 0x40),
    "F7": (64, 0x41),
    "F8": (65, 0x42),
    "F9": (66, 0x43),
    "F10": (67, 0x44),
    "F11": (68, 0x57),
    "F12": (69, 0x58),
    "LCTRL": (224, 0x1D),
    "LSHIFT": (225, 0x2A),
    "LALT": (226, 0x38),
    "1": (30, 0x02),
    "2": (31, 0x03),
    "3": (32, 0x04),
    "4": (33, 0x05),
    "5": (34, 0x06),
    "6": (35, 0x07),
    "7": (36, 0x08),
    "8": (37, 0x09),
    "9": (38, 0x0A),
    "0": (39, 0x0B),
    "A": (4, 0x1E),
    "B": (5, 0x30),
    "C": (6, 0x2E),
    "D": (7, 0x20),
    "E": (8, 0x12),
    "F": (9, 0x21),
    "G": (10, 0x22),
    "H": (11, 0x23),
    "I": (12, 0x17),
    "J": (13, 0x24),
    "K": (14, 0x25),
    "L": (15, 0x26),
    "M": (16, 0x32),
    "N": (17, 0x31),
    "O": (18, 0x18),
    "P": (19, 0x19),
    "Q": (20, 0x10),
    "R": (21, 0x13),
    "S": (22, 0x1F),
    "T": (23, 0x14),
    "U": (24, 0x16),
    "V": (25, 0x2F),
    "W": (26, 0x11),
    "X": (27, 0x2D),
    "Y": (28, 0x15),
    "Z": (29, 0x2C),
}

KNOWN_VERBS = frozenset({
    "go", "select", "focus", "back", "key", "wait", "assert", "click", "rclick", "hover",
    "drag", "test-dirty", "press", "release", "mouse-move", "mouse-stop", "repeat",
    "until", "mouse-down", "mouse-up", "require", "dump", "shot", "editor-pose",
    "probe-line", "probe-ground",
    "nav-to", "nav-offset", "nav-explore", "nav-nearest-enemy",
    "aim-at", "aim-nearest-enemy", "engage-enemy", "morph-to", "dodge-step",
    "take-nearest-friendly", "nav-drive-offset", "f9-clip",
})

STRIPPED_ENV = frozenset({
    "DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "XAUTHORITY",
    "DBUS_SESSION_BUS_ADDRESS", "DBUS_SYSTEM_BUS_ADDRESS",
    "SDL_VIDEODRIVER", "SDL_AUDIODRIVER", "BEA_PORT_UI_INPUT_FD",
    "BEA_PORT_TAPE", "BEA_PORT_RECORD", "BEA_PORT_PROBES",
    "BEA_PORT_CLOCK", "BEA_PORT_CLOCK_DT",
    "BEA_PORT_VERBS", "BEA_PORT_VERB_OUT", "BEA_PORT_VERB_RUN_DIR",
    "BEA_PORT_FAKE_KEYS", "BEA_PORT_FAKE_CLICK", "BEA_PORT_SHOT", "BEA_PORT_SHOT_EVERY",
    "BEA_PORT_QUIT",
})


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_resolution(spec: str) -> tuple[int, int]:
    m = re.fullmatch(r"([0-9]+)x([0-9]+)", spec.strip())
    if not m:
        raise ValueError(f"invalid resolution {spec!r}; expected WxH such as 1280x720")
    w, h = int(m.group(1)), int(m.group(2))
    if not (640 <= w <= 16384 and 480 <= h <= 16384):
        raise ValueError(f"resolution out of supported bounds (640x480..16384x16384): {spec!r}")
    return w, h


def format_ce_ini(settings: dict[str, str], base_text: str = "") -> str:
    """Build a well-formed ce.ini with [Experimental] modes separated cleanly."""
    top: list[str] = []
    exp: list[str] = []
    if base_text:
        for raw in base_text.splitlines():
            line = raw.strip()
            if not line or line.startswith(";") or line.startswith("#") or line.startswith("["):
                continue
            key, sep, val = line.partition("=")
            if not sep:
                continue
            k = key.strip()
            if k in settings:
                continue
            if k in EXPERIMENTAL_KEYS:
                exp.append(f"{k}={val.strip()}")
            else:
                top.append(f"{k}={val.strip()}")
    for key, val in settings.items():
        k = key.strip()
        if not k or "=" in k or "\n" in k or "\r" in k:
            raise ValueError(f"invalid ce.ini key: {key!r}")
        v = str(val).strip()
        if "\n" in v or "\r" in v:
            raise ValueError(f"invalid ce.ini value for {k!r}")
        entry = f"{k}={v}"
        if k in EXPERIMENTAL_KEYS:
            exp.append(entry)
        else:
            top.append(entry)
    out = list(top)
    if exp:
        out.append("[Experimental]")
        out.extend(exp)
    return ("\n".join(out) + "\n") if out else ""


def compile_fake_keys(text: str, hold_frames: int = 4) -> str:
    """Compile 'PRESENT KEY [hold N]' lines into a BEA_PORT_FAKE_KEYS token list."""
    tokens: list[tuple[int, str]] = []
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) not in (2, 4) or (len(parts) == 4 and parts[2].lower() != "hold"):
            raise ValueError(f"keys line {lineno}: expected 'PRESENT KEY [hold N]', got {raw!r}")
        at = int(parts[0])
        key = parts[1].upper()
        hold = int(parts[3]) if len(parts) == 4 else hold_frames
        if key not in FAKE_KEY_TABLE:
            raise ValueError(f"keys line {lineno}: unknown key {parts[1]!r}")
        if at < 0 or hold < 1:
            raise ValueError(f"keys line {lineno}: non-positive timing in {raw!r}")
        sdl, scan = FAKE_KEY_TABLE[key]
        tokens.append((at, f"{at}:{sdl}:{scan:x}:d"))
        tokens.append((at + hold, f"{at + hold}:{sdl}:{scan:x}:u"))
    tokens.sort(key=lambda item: item[0])
    return ",".join(tok for _, tok in tokens)


def validate_steps_text(text: str) -> list[str]:
    """Check step lines statically before launching the game; returns non-empty step lines."""
    lines: list[str] = []
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        try:
            words = shlex.split(line)
        except ValueError as err:
            raise ValueError(f"step line {lineno}: {err}: {line!r}") from err
        if not words:
            continue
        verb = words[0]
        if verb not in KNOWN_VERBS:
            raise ValueError(f"step line {lineno}: unknown verb {verb!r} in {line!r}")
        lines.append(line)
    return lines


def offscreen_confirmed(log: str) -> bool:
    reports = re.findall(r"(?:bea-port|onslaught-ce): SDL video driver([^\r\n]*)", log)
    return bool(reports) and all(re.fullmatch(r"\s+offscreen\s*", r) is not None for r in reports)


def audio_confirmed(log: str, expected: str = "dummy") -> bool:
    reports = re.findall(r"(?:bea-port|onslaught-ce): SDL audio driver([^\r\n]*)", log)
    drivers: list[str] = []
    for r in reports:
        m = re.fullmatch(r"\s+(\S+)\s*", r)
        if not m:
            return False
        drivers.append(m.group(1))
    return bool(drivers) and drivers[0] != "(none)" and all(d == drivers[0] for d in drivers) and drivers[0] == expected


def x87_confirmed(log: str) -> tuple[bool, str | None]:
    reports = re.findall(r"(?:bea-port|onslaught-ce): CreateDevice([^\r\n]*)", log)
    words: list[str] = []
    pending = 0
    valid = True
    req_rx = re.compile(r"\s+asks [0-9]+x[0-9]+ format -?[0-9]+ windowed -?[0-9]+ refresh [0-9]+\s*")
    res_rx = re.compile(r"\s+\S+ flags (\S+); .* ([0-9a-fA-F]{4}) now\s*")
    for r in reports:
        if req_rx.fullmatch(r):
            pending += 1
            continue
        m = res_rx.fullmatch(r)
        if not m:
            valid = False
            continue
        try:
            flags = int(m.group(1), 16)
        except ValueError:
            valid = False
            continue
        if not 0 <= flags <= 0xFFFFFFFF:
            valid = False
            continue
        word = m.group(2).lower()
        words.append(word)
        if pending:
            pending -= 1
        if not (flags & 0x800) and word != "007f":
            valid = False
    return bool(words) and valid and pending == 0, ",".join(words) if words else None


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    """Read a binary P6 PPM image into (width, height, rgb_bytes)."""
    with path.open("rb") as f:
        magic = f.readline(16)
        if magic != b"P6\n":
            raise ValueError(f"{path}: expected P6 PPM header, got {magic!r}")
        line = f.readline(64)
        while line.startswith(b"#"):
            line = f.readline(64)
        parts = line.split()
        if len(parts) != 2:
            raise ValueError(f"{path}: malformed PPM dimensions {line!r}")
        w, h = int(parts[0]), int(parts[1])
        depth = f.readline(16).strip()
        if depth != b"255":
            raise ValueError(f"{path}: unsupported PPM maxval {depth!r}")
        rgb = f.read()
        if len(rgb) != w * h * 3:
            raise ValueError(f"{path}: truncated PPM ({len(rgb)} != {w * h * 3})")
        return w, h, rgb


def write_png_from_rgb(path: Path, width: int, height: int, rgb: bytes) -> None:
    """Minimal lossless RGB PNG encoder (zero external dependencies)."""
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


def convert_ppm_to_png(ppm: Path, png: Path) -> dict[str, object]:
    """Convert a P6 PPM frame to PNG and compute basic frame health metrics."""
    w, h, rgb = read_ppm(ppm)
    metrics = inspect_rgb_frame(w, h, rgb)
    if shutil.which("ffmpeg"):
        res = subprocess.run(
            ["ffmpeg", "-loglevel", "error", "-y", "-i", str(ppm), str(png)],
            capture_output=True,
        )
        if res.returncode != 0 or not png.is_file():
            write_png_from_rgb(png, w, h, rgb)
    else:
        write_png_from_rgb(png, w, h, rgb)
    metrics["png"] = str(png)
    metrics["png_sha256"] = sha256_file(png)
    metrics["png_bytes"] = png.stat().st_size
    return metrics


def inspect_rgb_frame(width: int, height: int, rgb: bytes) -> dict[str, object]:
    """Compute non-black share and luminance stats to catch blank/black captures automatically."""
    total = width * height
    if total == 0:
        return {"width": width, "height": height, "mean_luma": 0.0, "max_channel": 0,
                "nonblack_share": 0.0, "blank_warning": True}
    # Sample every 4th pixel for speed while remaining accurate across 1280x720 frames.
    step = 4 if total >= 65536 else 1
    luma_sum = 0.0
    nonblack = 0
    max_ch = 0
    samples = 0
    mv = memoryview(rgb)
    for idx in range(0, total, step):
        o = idx * 3
        r, g, b = mv[o], mv[o + 1], mv[o + 2]
        m = r if r >= g and r >= b else (g if g >= b else b)
        if m > max_ch:
            max_ch = m
        if m > 4:
            nonblack += 1
        luma_sum += 0.299 * r + 0.587 * g + 0.114 * b
        samples += 1
    mean_luma = round(luma_sum / samples, 2)
    nonblack_share = round(nonblack / samples, 4)
    return {
        "width": width,
        "height": height,
        "mean_luma": mean_luma,
        "max_channel": max_ch,
        "nonblack_share": nonblack_share,
        "blank_warning": nonblack_share < 0.02 or mean_luma < 2.0,
    }


def parse_verbs_telemetry(verbs_path: Path) -> dict[str, object]:
    """Extract structured dumps, steps, and end status from a test-kit verbs.jsonl log."""
    header = None
    steps: list[dict[str, object]] = []
    dumps: list[dict[str, object]] = []
    end = None
    unreadable: list[str] = []
    if verbs_path.is_file():
        for raw in verbs_path.read_text(errors="replace").splitlines():
            if not raw.strip():
                continue
            try:
                obj = json.loads(raw)
            except ValueError:
                unreadable.append(raw)
                continue
            if "kit" in obj:
                header = obj
            elif "dump" in obj:
                dumps.append(obj)
            elif "step" in obj:
                steps.append(obj)
            elif "end" in obj or "refused" in obj:
                end = obj
    return {
        "header": header,
        "end": end,
        "step_count": len(steps),
        "dump_count": len(dumps),
        "dumps": dumps,
        "steps": steps,
        "unreadable_lines": unreadable,
    }


def validate_bug_clips(user_dir: Path) -> dict[str, object]:
    """Validate every F9 bug-clip .zip under user_dir/clips/ (zip integrity, .avi + .txt, ffmpeg decode)."""
    clips_dir = user_dir / "clips"
    zips = sorted(clips_dir.glob("*.zip")) if clips_dir.is_dir() else []
    results: list[dict[str, object]] = []
    all_valid = True
    has_ffmpeg = shutil.which("ffmpeg") is not None

    for zp in zips:
        entry: dict[str, object] = {
            "zip": str(zp),
            "zip_bytes": zp.stat().st_size,
            "avi": None,
            "avi_bytes": 0,
            "txt": None,
            "txt_bytes": 0,
            "ffmpeg_checked": False,
            "ffmpeg_ok": None,
            "valid": False,
            "errors": [],
        }
        errors: list[str] = entry["errors"]  # type: ignore[assignment]
        try:
            with zipfile.ZipFile(zp, "r") as zf:
                bad = zf.testzip()
                if bad is not None:
                    errors.append(f"corrupt zip member: {bad}")
                names = zf.namelist()
                avis = [n for n in names if n.lower().endswith(".avi")]
                txts = [n for n in names if n.lower().endswith(".txt")]
                if not avis:
                    errors.append("missing .avi video inside clip zip")
                if not txts:
                    errors.append("missing .txt diagnostic report inside clip zip")
                if txts:
                    tinfo = zf.getinfo(txts[0])
                    entry["txt"] = txts[0]
                    entry["txt_bytes"] = tinfo.file_size
                    if tinfo.file_size <= 0:
                        errors.append(f"empty diagnostic report {txts[0]}")
                if avis:
                    ainfo = zf.getinfo(avis[0])
                    entry["avi"] = avis[0]
                    entry["avi_bytes"] = ainfo.file_size
                    if ainfo.file_size <= 0:
                        errors.append(f"empty video {avis[0]}")
                    elif has_ffmpeg:
                        entry["ffmpeg_checked"] = True
                        with tempfile.TemporaryDirectory(prefix="ce-clip-check-") as td:
                            avi_path = Path(td) / Path(avis[0]).name
                            avi_path.write_bytes(zf.read(avis[0]))
                            dec = subprocess.run(
                                ["ffmpeg", "-v", "error", "-i", str(avi_path), "-f", "null", "-"],
                                capture_output=True, text=True, timeout=60,
                            )
                            entry["ffmpeg_ok"] = (dec.returncode == 0 and not dec.stderr.strip())
                            if not entry["ffmpeg_ok"]:
                                errors.append(f"ffmpeg decode error (rc={dec.returncode}): {dec.stderr.strip()[:240]}")
        except (OSError, zipfile.BadZipFile, subprocess.SubprocessError) as exc:
            errors.append(str(exc))

        entry["valid"] = len(errors) == 0
        if not entry["valid"]:
            all_valid = False
        results.append(entry)

    return {
        "clips_dir": str(clips_dir),
        "clip_count": len(results),
        "all_valid": all_valid,
        "clips": results,
    }


def dependency_roots() -> list[Path]:
    roots: list[Path] = []
    if os.environ.get("BEA_PORT_DATA"):
        roots.append(Path(os.environ["BEA_PORT_DATA"]))
    roots.append(ROOT / "local-data/port-deps-canonical")
    git = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--git-common-dir"], capture_output=True, text=True)
    if git.returncode == 0 and git.stdout.strip():
        roots.append((ROOT / git.stdout.strip()).resolve().parent / "local-data/port-deps-canonical")
    seen: set[Path] = set()
    out: list[Path] = []
    for r in roots:
        rp = r.resolve() if r.exists() else r
        if rp.is_dir() and rp not in seen:
            seen.add(rp)
            out.append(rp)
    return out


def resolve_libraries(exe: Path, extra_libs: list[str]) -> tuple[list[str], str | None]:
    receipt = exe.parent / ".bea-sdl3-link.json"
    if not receipt.is_file():
        return list(extra_libs), None
    link = json.loads(receipt.read_text())
    if link.get("bea") != sha256_file(exe):
        raise SystemExit(f"{receipt}: link receipt names another binary (keep .bea-sdl3-link.json beside its matching bea)")
    want = link.get("sdl_receipt_sha256")
    for root in dependency_roots():
        sdl = root / "prefix-sniper/.bea-sdl3-build.json"
        if sdl.is_file() and sha256_file(sdl) == want:
            dirs = [root / "prefix-sniper/lib", root / "dxvk-native/usr/lib32", root / "ffmpeg-shared-sniper/linux/usr/lib"]
            return [str(d) for d in dirs if d.is_dir()] + list(extra_libs), str(root)
    raise SystemExit(f"no dependency root holds SDL build {str(want)[:12]} for {exe} (set BEA_PORT_DATA)")


def discover_exe(prefer_testkit: bool) -> Path:
    candidates = [
        ROOT / "local-data/test-kit-twin/bea",
        ROOT / "local-data/port/build/linux/bea",
    ]
    if not prefer_testkit:
        candidates.reverse()
    for p in candidates:
        if p.is_file():
            return p.resolve()
    raise SystemExit("no built bea binary found; pass --exe PATH")


def build_offscreen_env(
    *,
    base_env: dict[str, str],
    run_dir: Path,
    res: str,
    gpu: str,
    libs: list[str],
    deps_root: str | None,
    game_dir: str | None,
    clock: str,
    dt: str,
    steps_file: Path | None = None,
    verbs_out: Path | None = None,
    verb_run_dir: Path | None = None,
    fake_keys: str | None = None,
    fake_click: str | None = None,
    shot_at: tuple[int, Path] | None = None,
    shot_every: tuple[int, Path] | None = None,
    quit_at: int | None = None,
    extra_env: dict[str, str] | None = None,
) -> dict[str, str]:
    """Construct the strictly offscreen, silent, desktop-isolated child environment."""
    env = {k: v for k, v in base_env.items() if k not in STRIPPED_ENV}
    env.update({
        "SDL_VIDEO_DRIVER": "offscreen",
        "BEA_PORT_REQUIRE_OFFSCREEN": "1",
        "SDL_AUDIO_DRIVER": "dummy",
        "BEA_PORT_REQUIRE_DUMMY_AUDIO": "1",
        "BEA_REPORT_NO_OPEN": "1",
        "BROWSER": "/bin/true",
        "PYTHONDONTWRITEBYTECODE": "1",
        "DXVK_WSI_DRIVER": "SDL3",
        "DXVK_FILTER_DEVICE_NAME": "Intel" if gpu == "intel" else "NVIDIA",
        "DXVK_LOG_PATH": str(run_dir),
        "XDG_CACHE_HOME": str(run_dir / "cache"),
        "XDG_RUNTIME_DIR": str(run_dir / "runtime"),
        "BEA_USER_DIR": str(run_dir / "user"),
        "BEA_PORT_DEBUG": "1",
        "BEA_PORT_CE_LAYER": "1",
        "BEA_NO_UPDATE_CHECK": "1",
        "BEA_PORT_WIDE_TEST": res,
    })
    if libs:
        env["LD_LIBRARY_PATH"] = ":".join(libs)
    if deps_root:
        env.setdefault("BEA_PORT_DATA", deps_root)
    if game_dir:
        env["BEA_GAME_DIR"] = game_dir
    if clock != "real":
        env["BEA_PORT_CLOCK"] = clock
        if clock == "fixed":
            env["BEA_PORT_CLOCK_DT"] = dt
    if steps_file is not None:
        env["BEA_PORT_VERBS"] = str(steps_file)
        if verbs_out is not None:
            env["BEA_PORT_VERB_OUT"] = str(verbs_out)
        if verb_run_dir is not None:
            env["BEA_PORT_VERB_RUN_DIR"] = str(verb_run_dir)
    if fake_keys:
        env["BEA_PORT_FAKE_KEYS"] = fake_keys
    if fake_click:
        env["BEA_PORT_FAKE_CLICK"] = fake_click
    if shot_at is not None:
        env["BEA_PORT_SHOT"] = f"{shot_at[0]}:{shot_at[1]}"
    if shot_every is not None:
        env["BEA_PORT_SHOT_EVERY"] = f"{shot_every[0]}:{shot_every[1]}"
    if quit_at is not None:
        env["BEA_PORT_QUIT"] = str(quit_at)
    if extra_env:
        for k, v in extra_env.items():
            if k in STRIPPED_ENV and k not in {"BEA_PORT_FAKE_CLICK", "BEA_PORT_FAKE_KEYS"}:
                raise ValueError(f"cannot override protected offscreen variable {k}")
            env[k] = v
    env["PATH"] = f"{run_dir / 'shim'}:{env.get('PATH') or '/usr/bin:/bin'}"
    return env


def seed_sample_mod(user_dir: Path, game_dir: Path | None) -> bool:
    """Create a valid, lightweight sample mod inside an isolated run's user/mods/ folder."""
    if not game_dir:
        return False
    src_rel = "data/resources/dxtntextures/meshtex%BE_texA.tga(0)A1R5G5B5.aya"
    src_path = game_dir / src_rel
    if not src_path.is_file():
        return False
    mod_root = user_dir / "mods/sample-hull-pack"
    dst_path = mod_root / src_rel
    dst_path.parent.mkdir(parents=True, exist_ok=True)
    data = src_path.read_bytes()
    dst_path.write_bytes(data)
    digest = hashlib.sha256(data).hexdigest()
    manifest = {
        "schema": 1,
        "id": "sample-hull-pack",
        "name": "Sample Hull Pack",
        "version": "1.0.0",
        "author": "Gallery Fixture",
        "description": "Sample texture pack shown in the Mods page preview.",
        "files": [{"path": src_rel, "size": len(data), "sha256": digest}],
    }
    (mod_root / "mod.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return True


def run_session(
    *,
    exe: Path,
    out_dir: Path,
    res: str = DEFAULT_RES,
    clock: str = "fixed",
    dt: str = DEFAULT_DT,
    ce_ini: dict[str, str] | None = None,
    ce_ini_base: str = "",
    seeds: dict[str, Path] | None = None,
    add_sample_mod: bool = False,
    steps_text: str | None = None,
    fake_keys: str | None = None,
    fake_click: str | None = None,
    shot_at: int | None = None,
    shot_every: int | None = None,
    quit_at: int | None = None,
    level: int | None = None,
    extra_args: list[str] | None = None,
    extra_env: dict[str, str] | None = None,
    gpu: str = "intel",
    gpu_lock: str | None = None,
    libs: list[str] | None = None,
    deps_root: str | None = None,
    game_dir: str | None = None,
    timeout: int = 240,
    memory: str = "4G",
    png_dest_dir: Path | None = None,
    require_bug_clip: bool = False,
) -> dict[str, object]:
    """Execute one isolated offscreen Onslaught CE play session and return a full result dict."""
    w, h = parse_resolution(res)
    out_dir.mkdir(parents=True, exist_ok=False)
    for sub in ("user", "runtime", "cache", "shim", "shots"):
        (out_dir / sub).mkdir(mode=0o700)
    opener = out_dir / "shim/xdg-open"
    opener.write_text(
        "#!/bin/sh\n"
        f"printf '%s\\n' \"$*\" >> {shlex.quote(str(out_dir / 'xdg-open.log'))}\n"
        "exit 0\n"
    )
    opener.chmod(0o700)

    user_dir = out_dir / "user"
    ini_text = format_ce_ini(ce_ini or {}, ce_ini_base)
    if ini_text:
        (user_dir / "ce.ini").write_text(ini_text)
    for rel, src in (seeds or {}).items():
        dst = (user_dir / rel).resolve()
        if not dst.is_relative_to(user_dir.resolve()):
            raise ValueError(f"seed destination escapes user folder: {rel!r}")
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    if add_sample_mod:
        seed_sample_mod(user_dir, Path(game_dir) if game_dir else None)

    steps_file: Path | None = None
    verbs_out: Path | None = None
    if steps_text is not None:
        validate_steps_text(steps_text)
        if b"ce-testkit-v1" not in exe.read_bytes():
            raise SystemExit(f"{exe}: steps require a --test-kit build (build.py --public --test-kit)")
        steps_file = out_dir / "steps.txt"
        steps_file.write_text(steps_text.rstrip("\n") + "\n")
        verbs_out = out_dir / "verbs.jsonl"

    resolved_libs = list(libs) if libs is not None else []
    resolved_deps = deps_root
    if libs is None:
        resolved_libs, resolved_deps = resolve_libraries(exe, [])

    shot_pair = (shot_at, out_dir / "shots" / f"frame-{shot_at}.ppm") if shot_at is not None else None
    every_pair = (shot_every, out_dir / "shots") if shot_every is not None else None

    env = build_offscreen_env(
        base_env=dict(os.environ),
        run_dir=out_dir,
        res=res,
        gpu=gpu,
        libs=resolved_libs,
        deps_root=resolved_deps,
        game_dir=game_dir,
        clock=clock,
        dt=dt,
        steps_file=steps_file,
        verbs_out=verbs_out,
        verb_run_dir=out_dir / "shots",
        fake_keys=fake_keys,
        fake_click=fake_click,
        shot_at=shot_pair,
        shot_every=every_pair,
        quit_at=quit_at,
        extra_env=extra_env,
    )

    game_args = ["-skipfmv", "-forcewindowed", "-res", str(w), str(h)]
    if level is not None:
        game_args = ["-level", str(level)] + game_args
    if extra_args:
        game_args += list(extra_args)

    cmd: list[str] = ["timeout", "-s", "INT", "-k", "10", str(timeout), str(exe)] + game_args
    if gpu_lock:
        cmd = ["flock", "-w", "21600", gpu_lock] + cmd
    # Note: systemd-run --user talks to the user bus via XDG_RUNTIME_DIR, so wrap externally if memory scoping is requested.
    scope_env = dict(os.environ)
    use_scope = memory != "none" and shutil.which("systemd-run") is not None
    if use_scope:
        # Pass the child environment via env(1) so systemd-run itself keeps the user's XDG_RUNTIME_DIR.
        unit = re.sub(r"[^A-Za-z0-9_.-]", "-", f"agent-play-{out_dir.name}")[:180]
        # Serialize child env safely via a small helper invocation of python3 inside the scope.
        env_path = out_dir / "child_env.json"
        env_path.write_text(json.dumps(env))
        launcher_code = (
            "import json, os, resource, subprocess, sys; "
            "resource.setrlimit(resource.RLIMIT_CORE, (0, 0)); "
            "env = json.loads(open(sys.argv[1]).read()); "
            "raise SystemExit(subprocess.run(sys.argv[2:], env=env).returncode)"
        )
        run_cmd = [
            "systemd-run", "--user", "--scope", "--quiet", f"--unit={unit}",
            "-p", f"MemoryMax={memory}", "-p", "MemorySwapMax=0",
            sys.executable, "-c", launcher_code, str(env_path), *cmd,
        ]
        run_env = scope_env
        preexec = None
    else:
        run_cmd = cmd
        run_env = env
        preexec = lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

    with (out_dir / "stdout.log").open("wb") as sout, (out_dir / "stderr.log").open("wb") as serr:
        rc = subprocess.run(run_cmd, env=run_env, stdout=sout, stderr=serr, preexec_fn=preexec).returncode

    stderr_text = (out_dir / "stderr.log").read_text(errors="replace")
    off_ok = offscreen_confirmed(stderr_text)
    aud_ok = audio_confirmed(stderr_text, "dummy")
    x87_ok, x87_words = x87_confirmed(stderr_text)

    telemetry = parse_verbs_telemetry(verbs_out) if verbs_out else None
    if telemetry is not None:
        (out_dir / "telemetry.json").write_text(json.dumps(telemetry, indent=2) + "\n")

    shots: list[dict[str, object]] = []
    dest_root = png_dest_dir if png_dest_dir is not None else out_dir
    dest_root.mkdir(parents=True, exist_ok=True)
    for ppm in sorted((out_dir / "shots").glob("*.ppm")):
        png_path = dest_root / f"{ppm.stem}.png"
        info = convert_ppm_to_png(ppm, png_path)
        info["label"] = ppm.stem
        shots.append(info)
        ppm.unlink()

    bug_clips = validate_bug_clips(user_dir)
    clips_ok = bool(bug_clips["all_valid"]) and (not require_bug_clip or int(bug_clips["clip_count"]) > 0)

    steps_ok = True
    if telemetry is not None:
        end = telemetry.get("end")
        steps_ok = isinstance(end, dict) and end.get("end") == "pass"

    passed = off_ok and aud_ok and x87_ok and steps_ok and clips_ok and (rc in (0, 124) if steps_text is None else rc == 0)
    result = {
        "passed": passed,
        "exit_code": rc,
        "offscreen_confirmed": off_ok,
        "audio_dummy_confirmed": aud_ok,
        "x87_confirmed": x87_ok,
        "x87_words": x87_words,
        "clock": clock,
        "dt": dt if clock == "fixed" else None,
        "resolution": res,
        "exe": str(exe),
        "exe_sha256": sha256_file(exe),
        "ce_ini": ce_ini or {},
        "level": level,
        "shots": shots,
        "bug_clips": bug_clips,
        "telemetry_file": str(out_dir / "telemetry.json") if telemetry is not None else None,
        "telemetry_end": telemetry.get("end") if telemetry is not None else None,
        "dump_count": telemetry.get("dump_count", 0) if telemetry is not None else 0,
        "stderr_log": str(out_dir / "stderr.log"),
        "out_dir": str(out_dir),
    }
    (out_dir / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


# ---- Repeatable visual inspection gallery scenes ----------------------------------------------------------------------
def gallery_scenes() -> list[dict[str, object]]:
    """Every major Onslaught CE screen and mode, isolated per scene at 1280x720."""
    return [
        {
            "name": "01_menus_and_ce_settings",
            "description": "Main Menu, Onslaught CE Hub, and CE Settings",
            "clock": "fixed",
            "ce_ini": {
                "extras": "1", "catalogue": "1", "mods_menu": "1",
                "map_editor": "1", "museum": "1", "commander": "1", "play_unit": "1",
            },
            "shots_meta": {
                "01_main_menu": "Main Menu with Onslaught CE entry",
                "02_onslaught_ce_hub": "Onslaught CE Hub with all feature rows",
                "03_ce_settings": "CE Settings list and hint text",
            },
            "steps": """
wait ticks 600
key RETURN
wait page fe:main within 900
wait ticks 45
dump main_menu
shot 01_main_menu
go "Onslaught CE"
wait page ce:hub within 600
wait ticks 45
dump ce_hub
shot 02_onslaught_ce_hub
go "CE Settings"
wait page ce:settings within 600
wait ticks 45
dump ce_settings
shot 03_ce_settings
""",
        },
        {
            "name": "01b_video_graphics_options",
            "description": "Options > Video / Graphics Settings (real clock so CE display & quality rows appear)",
            "clock": "real",
            "ce_ini": {},
            "shots_meta": {
                "04_video_graphics_options": "Options > Video / Graphics Settings (Window mode & quality rows)",
            },
            "steps": """
wait ticks 180
key RETURN
wait page fe:main within 1800
go Options within 1200
wait page fe:options within 1200
wait ticks 90
key DOWN
wait ticks 20
key DOWN
wait ticks 20
key RETURN
wait ticks 90
dump video_options
shot 04_video_graphics_options
""",
        },
        {
            "name": "02_extras",
            "description": "Extras Hub, Service Record, Goodies Catalogue, Lore Reader, and Music Player",
            "clock": "fixed",
            "ce_ini": {"extras": "1"},
            "needs_gold_career": True,
            "shots_meta": {
                "05_extras_hub": "Extras Hub menu",
                "06_extras_service_record": "Extras > Service Record (Gold career)",
                "07_extras_goodies_catalogue": "Extras > Goodies Catalogue entries",
                "08_extras_lore_reader": "Extras > Lore article reader",
                "09_extras_music_player": "Extras > Music Player",
            },
            "steps": """
wait ticks 600
key RETURN
wait page fe:main within 900
go "Onslaught CE" > Extras
wait page xp:hub within 600
wait ticks 45
dump extras_hub
shot 05_extras_hub
go "Service Record"
wait page xp:careers within 600
select row Gold
wait page xp:record within 600
wait ticks 45
dump extras_service_record
shot 06_extras_service_record
back
wait page xp:careers within 600
back
wait page xp:hub within 600
go "Goodies Catalogue"
wait page xp:catalogue within 600
select row Gold
wait page xp:goodies within 600
wait ticks 45
dump extras_goodies_catalogue
shot 07_extras_goodies_catalogue
back
wait page xp:catalogue within 600
back
wait page xp:hub within 600
go Lore
wait page xp:lore within 600
wait ticks 35
select row #1
wait page xp:article within 600
wait ticks 45
dump extras_lore_reader
shot 08_extras_lore_reader
back
wait page xp:lore within 600
back
wait page xp:hub within 600
go "Music Player"
wait page xp:music within 600
wait ticks 45
dump extras_music_player
shot 09_extras_music_player
""",
        },
        {
            "name": "03_asset_catalogue",
            "description": "Asset Catalogue: Models 3D preview, Textures DDS preview, and Speech list",
            "clock": "fixed",
            "ce_ini": {"catalogue": "1"},
            "shots_meta": {
                "10_catalogue_models": "Asset Catalogue > Models list and 3D preview",
                "11_catalogue_texture_dds_preview": "Asset Catalogue > Textures DDS preview pane",
                "12_catalogue_speech": "Asset Catalogue > Speech: English list",
            },
            "steps": """
wait ticks 600
key RETURN
wait page fe:main within 900
go "Onslaught CE" > Catalogue
wait page ce:catalogue within 600
select row Models
wait page xp:sounds within 600
wait ticks 45
dump catalogue_models
shot 10_catalogue_models
back
wait page ce:catalogue within 600
select row Textures
wait page xp:sounds within 600
wait ticks 45
dump catalogue_texture_preview
shot 11_catalogue_texture_dds_preview
back
wait page ce:catalogue within 600
select row "Speech: English"
wait page xp:sounds within 600
wait ticks 45
dump catalogue_speech
shot 12_catalogue_speech
""",
        },
        {
            "name": "04_mods_menu",
            "description": "Mods Menu (mods_menu=1, real clock) with a valid sample mod installed",
            "clock": "real",
            "ce_ini": {"mods_menu": "1"},
            "sample_mod": True,
            "shots_meta": {
                "13_mods_menu": "Onslaught CE > Mods menu with valid sample mod",
            },
            "steps": """
wait ticks 180
key RETURN
wait page fe:main within 1800
go "Onslaught CE" within 1200
wait page ce:hub within 1200
select row Mods within 1200
wait ticks 90
dump mods_menu
shot 13_mods_menu
""",
        },
        {
            "name": "05_level100_gameplay_photo",
            "description": "Level 100 active gameplay (Walker HUD) and Photo Mode (F11 HUD hidden)",
            "clock": "real",
            "level": 100,
            "ce_ini": {"hud_scale": "1", "hud_anchor": "1"},
            "shots_meta": {
                "14_level100_walker_hud": "Level 100 active gameplay (Walker + HUD)",
                "16_photo_mode_hud_hidden": "Level 100 Photo Mode (F11 HUD hidden)",
            },
            "steps": """
wait state game.state=3 within 2400
wait ticks 90
dump level100_walker
shot 14_level100_walker_hud
key F11 hold 4
wait ticks 25
dump level100_photo_mode
shot 16_photo_mode_hud_hidden
key F11 hold 4
wait ticks 15
""",
        },
        {
            "name": "05b_jet_flight_hud",
            "description": "Active Jet flight gameplay + HUD on Level 110 (closed-loop morph + aerial nav)",
            "clock": "fixed",
            "level": 110,
            "ce_ini": {"hud_scale": "1", "hud_anchor": "1"},
            "shots_meta": {
                "15_level110_jet_hud": "Level 110 active Jet flight + HUD",
            },
            "steps": """
wait state game.state=3 within 2400
wait ticks 90
morph-to jet within 600
nav-offset 0 120 tol 18 mode jet within 600
dump level110_jet
shot 15_level110_jet_hud
""",
        },
        {
            "name": "06_commander_live_view",
            "description": "Commander Live View (commander=1, M key) on Level 110 with unit selection & action panel",
            "clock": "real",
            "level": 110,
            "ce_ini": {"commander": "1", "play_unit": "1"},
            "shots_meta": {
                "17_commander_live_overview": "Commander Live View opened via M key (Level 110)",
                "18_commander_unit_selection_panel": "Commander Live View with unit selection/action panel",
            },
            "steps": """
wait state game.state=3 within 2400
wait ticks 350
key M hold 15
wait ticks 180
dump commander_overview
shot 17_commander_live_overview
key LCTRL+A hold 15
wait ticks 80
dump commander_selected
shot 18_commander_unit_selection_panel
""",
        },
        {
            "name": "07_play_any_unit",
            "description": "Play Any Unit (play_unit=1) autonomously possessing & driving a friendly tank on Level 211",
            "clock": "real",
            "level": 211,
            "ce_ini": {"play_unit": "1"},
            "shots_meta": {
                "19_play_unit_tank_hud": "Play Any Unit driving friendly tank with unit HUD",
            },
            "steps": """
wait state game.state=3 within 2400
wait ticks 120
take-nearest-friendly maxdist 160 within 6000
nav-drive-offset 35 15 tol 10 within 12000
dump play_unit_tank
shot 19_play_unit_tank_hud
""",
        },
        {
            "name": "08_museum_hall_cinema",
            "description": "Museum Hall (A* around plinths to Atrium) and Screening Room Cinema",
            "clock": "fixed",
            "ce_ini": {"museum": "1"},
            "shots_meta": {
                "20_museum_hall": "Museum Hall Atrium & exhibit bays",
                "21_museum_cinema": "Museum Screening Room Cinema & console",
            },
            "steps": """
wait ticks 600
key RETURN
wait page fe:main within 900
go "Onslaught CE"
wait page ce:hub within 600
go Museum
wait page ce:museum within 600
select row Units
wait state game.state=3 within 2400
wait ticks 90
nav-to 256 270 tol 3.5 mode walker within 1200
dump museum_hall
shot 20_museum_hall
nav-to 303 256 tol 3.5 mode walker within 1800
aim-at 304 268 -4.0 tol 8 within 300
dump museum_cinema
shot 21_museum_cinema
""",
        },
        {
            "name": "09_map_editor",
            "description": "Map Editor (map_editor=1) on Installed Map 221 with object selection",
            "clock": "fixed",
            "ce_ini": {"map_editor": "1"},
            "shots_meta": {
                "22_map_editor_level221": "Map Editor authoring view (Level 221)",
            },
            "steps": """
wait ticks 600
key RETURN
wait page fe:main within 900
go "Onslaught CE" > "Map Editor" > "2.21"
wait page ed:map within 2400
wait ticks 60
key TAB hold 4
wait ticks 30
dump map_editor_level221
shot 22_map_editor_level221
""",
        },
    ]


# ---- Closed-loop autonomous gameplay journeys (--autonomous / --playtest-suite) ---------------------------------------
def autonomous_scenes() -> list[dict[str, object]]:
    """Six closed-loop autonomous gameplay journeys using A* pathfinding, combat, takeover, and F9 clip validation."""
    return [
        {
            "name": "auto_01_l110_combat_f9",
            "description": "Level 110 @ 1920x1080: autonomous nav-explore, dodge-step, engage-enemy & live F9 clip zip+ffmpeg check",
            "clock": "real",
            "level": 110,
            "res": "1920x1080",
            "ce_ini": {"hud_scale": "1", "hud_anchor": "1"},
            "require_bug_clip": True,
            "shots_meta": {
                "auto_01_explore": "Level 110 after autonomous A* waypoint exploration",
                "auto_01_combat": "Level 110 after closed-loop enemy engagement & dodge",
            },
            "steps": """
wait state game.state=3 within 2400
wait ticks 90
nav-explore 2 radius 45 mode walker within 12000
assert nav.moved>15
dump auto_01_explore
shot auto_01_explore
dodge-step right within 2400
engage-enemy ticks 180 maxrange 120 within 10000
dump auto_01_combat
shot auto_01_combat
f9-clip 90 within 6000
dump auto_01_f9_done
""",
        },
        {
            "name": "auto_02_l110_morph_commander",
            "description": "Level 110: Walker patrol -> morph-to jet -> aerial nav-offset -> morph-to walker -> Commander Live View",
            "clock": "real",
            "level": 110,
            "res": "1280x720",
            "ce_ini": {"commander": "1", "play_unit": "1", "hud_scale": "1", "hud_anchor": "1"},
            "shots_meta": {
                "auto_02_jet_flight": "Level 110 autonomous Jet aerial leg",
                "auto_02_commander_selected": "Level 110 Commander Live View with LCTRL+A unit selection",
            },
            "steps": """
wait state game.state=3 within 2400
wait ticks 120
nav-offset 30 30 tol 10 mode walker within 10000
morph-to jet within 3000
assert pilot.mode=jet
nav-offset 80 80 tol 22 mode jet within 8000
dump auto_02_jet_flight
shot auto_02_jet_flight
morph-to walker within 3000
assert pilot.mode=walker
wait ticks 60
key M hold 15
wait ticks 120
key LCTRL+A hold 15
wait ticks 60
dump auto_02_commander_selected
shot auto_02_commander_selected
""",
        },
        {
            "name": "auto_03_l211_playunit_smart",
            "description": "Level 211: autonomous take-nearest-friendly -> nav-drive-offset tank driving -> live fire burst",
            "clock": "real",
            "level": 211,
            "res": "1280x720",
            "ce_ini": {"play_unit": "1", "hud_scale": "1", "hud_anchor": "1"},
            "shots_meta": {
                "auto_03_tank_possessed": "Level 211 immediately after autonomous tank possession",
                "auto_03_tank_driven": "Level 211 after A*-steered tank drive & weapon burst",
            },
            "steps": """
wait state game.state=3 within 2400
wait ticks 120
take-nearest-friendly maxdist 160 within 6000
assert pilot.mode=playunit
dump auto_03_tank_possessed
shot auto_03_tank_possessed
nav-drive-offset 35 15 tol 10 within 12000
mouse-down
wait ticks 90
mouse-up
wait ticks 30
dump auto_03_tank_driven
shot auto_03_tank_driven
""",
        },
        {
            "name": "auto_04_museum_autonomous_tour",
            "description": "Grand Museum: A* around plinths & columns into Grand Atrium and Screening Room Cinema",
            "clock": "fixed",
            "res": "1280x720",
            "ce_ini": {"museum": "1"},
            "shots_meta": {
                "auto_04_museum_atrium": "Grand Museum Atrium reached via A* navigation around plinths",
                "auto_04_museum_cinema": "Grand Museum Screening Room reached via A* portals",
            },
            "steps": """
wait ticks 600
key RETURN
wait page fe:main within 900
go "Onslaught CE"
wait page ce:hub within 600
go Museum
wait page ce:museum within 600
select row Units
wait state game.state=3 within 2400
wait ticks 90
nav-to 256 270 tol 3.5 mode walker within 1200
dump auto_04_museum_atrium
shot auto_04_museum_atrium
nav-to 303 256 tol 3.5 mode walker within 1800
aim-at 304 268 -4.0 tol 8 within 300
dump auto_04_museum_cinema
shot auto_04_museum_cinema
""",
        },
        {
            "name": "auto_05_sandbox_sanctuary_tour",
            "description": "Living Sandbox: explore Sanctuary Citadel, Archive Pedestal & Shield Dome via A* navigation",
            "clock": "real",
            "res": "1280x720",
            "ce_ini": {"play_unit": "1", "commander": "1", "hud_scale": "1", "hud_anchor": "1"},
            "shots_meta": {
                "auto_05_sanctuary_pedestal": "Living Sandbox Sanctuary Citadel Archive Pedestal & Shield Dome",
                "auto_05_sanctuary_ramp": "Living Sandbox descending Sanctuary SE Ramp toward valley theater",
            },
            "steps": """
wait ticks 180
key RETURN
wait page fe:main within 1800
go "Onslaught CE" within 1200
wait page ce:hub within 1200
go "Bonus Levels" within 1200
wait page ce:bonus within 1200
select row "Living Sandbox" within 1200
wait state game.state=3 within 3000
wait ticks 120
nav-to 112 385 tol 4.0 mode walker within 8000
aim-at 112 392 -35.5 tol 10 within 2000
dump auto_05_sanctuary_pedestal
shot auto_05_sanctuary_pedestal
nav-to 144 316 tol 8.0 mode walker within 14000
dump auto_05_sanctuary_ramp
shot auto_05_sanctuary_ramp
""",
        },
        {
            "name": "auto_06_multires_hud_audit",
            "description": "Level 100 multi-resolution HUD telemetry & non-blank audit (640x480, 1280x720, 1920x1080, 2560x1080)",
            "clock": "real",
            "level": 100,
            "resolutions": ["640x480", "1280x720", "1920x1080", "2560x1080"],
            "ce_ini": {"hud_scale": "1", "hud_anchor": "1"},
            "shots_meta": {
                "auto_06_hud_640x480": "Level 100 HUD at 640x480 (4:3 baseline)",
                "auto_06_hud_1280x720": "Level 100 HUD at 1280x720 (16:9 HD)",
                "auto_06_hud_1920x1080": "Level 100 HUD at 1920x1080 (16:9 FHD)",
                "auto_06_hud_2560x1080": "Level 100 HUD at 2560x1080 (21:9 ultrawide)",
            },
            "steps_template": """
wait state game.state=3 within 2400
wait ticks 90
assert hud.buffer={res}
assert hud.wide={wide}
assert hud.scale=*
assert hud.anchored=*
dump hud_{res}
shot auto_06_hud_{res}
""",
        },
    ]


def expand_autonomous_scene_steps(sc: dict[str, object]) -> list[tuple[str, str, str]]:
    """Expand a scene (including multi-resolution audit specs) into (sub_name, res, steps_text) tuples."""
    if "resolutions" in sc:
        out: list[tuple[str, str, str]] = []
        tmpl = str(sc["steps_template"])
        for res in sc["resolutions"]:  # type: ignore[union-attr]
            w, h = parse_resolution(str(res))
            wide = "on" if (w * 3 > h * 4) else "off"
            steps = tmpl.format(res=res, wide=wide)
            out.append((f"{sc['name']}_{res}", str(res), steps))
        return out
    return [(str(sc["name"]), str(sc.get("res", DEFAULT_RES)), str(sc["steps"]))]


def run_autonomous(args: argparse.Namespace) -> int:
    exe = Path(args.exe).resolve() if args.exe else discover_exe(prefer_testkit=True)
    if b"ce-testkit-v1" not in exe.read_bytes():
        sys.exit(f"{exe}: --autonomous requires a --test-kit build (build.py --public --test-kit)")
    libs, deps_root = resolve_libraries(exe, args.lib)
    out_root = Path(args.out).resolve()
    out_root.mkdir(parents=True, exist_ok=True)
    (out_root / "telemetry").mkdir(exist_ok=True)
    (out_root / "runs").mkdir(exist_ok=True)

    wanted = {s.strip() for s in args.scenes.split(",") if s.strip()} if args.scenes else None
    all_scenes = autonomous_scenes()
    scenes = [sc for sc in all_scenes if wanted is None or sc["name"] in wanted]
    if wanted is not None:
        known = {str(sc["name"]) for sc in all_scenes}
        unknown = sorted(wanted - known)
        if unknown:
            sys.exit(f"unknown autonomous scene(s): {', '.join(unknown)} (known: {', '.join(sorted(known))})")

    commit = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")

    scene_results: list[dict[str, object]] = []
    frames: list[dict[str, object]] = []
    any_failed = False

    for sc in scenes:
        meta = sc.get("shots_meta", {})
        for sub_name, res_str, steps_text in expand_autonomous_scene_steps(sc):
            run_dir = out_root / "runs" / f"{sub_name}-{stamp}"
            print(f"RUN  {sub_name} ({res_str}): {sc['description']}", flush=True)
            res = run_session(
                exe=exe,
                out_dir=run_dir,
                res=res_str,
                clock=str(sc["clock"]),
                dt=args.dt,
                ce_ini=dict(sc.get("ce_ini", {})),
                steps_text=steps_text,
                level=sc.get("level"),
                gpu=args.gpu,
                gpu_lock=args.gpu_lock,
                libs=libs,
                deps_root=deps_root,
                game_dir=args.game,
                timeout=max(args.timeout, 300),
                memory=args.memory,
                png_dest_dir=out_root,
                require_bug_clip=bool(sc.get("require_bug_clip")),
            )
            if res.get("telemetry_file") and Path(str(res["telemetry_file"])).is_file():
                shutil.copy2(Path(str(res["telemetry_file"])), out_root / "telemetry" / f"{sub_name}.json")

            has_blank = False
            for shot in res["shots"]:
                label = str(shot["label"])
                if shot.get("blank_warning"):
                    has_blank = True
                frames.append({
                    "label": label,
                    "scene": sub_name,
                    "resolution": res_str,
                    "caption": meta.get(label, label),
                    "png": str(out_root / f"{label}.png"),
                    "width": shot["width"],
                    "height": shot["height"],
                    "mean_luma": shot["mean_luma"],
                    "nonblack_share": shot["nonblack_share"],
                    "blank_warning": shot["blank_warning"],
                    "sha256": shot["png_sha256"],
                    "telemetry": str(out_root / "telemetry" / f"{sub_name}.json"),
                })

            passed = bool(res["passed"]) and not has_blank
            status = "PASS" if passed else "FAIL"
            clips_summary = res.get("bug_clips", {})
            print(f"{status} {sub_name}  shots={len(res['shots'])}  clips={clips_summary.get('clip_count', 0)}  "
                  f"exit={res['exit_code']}  end={res['telemetry_end']}", flush=True)
            if not passed:
                any_failed = True
            scene_results.append({
                "scene": sub_name,
                "parent_scene": sc["name"],
                "description": sc["description"],
                "resolution": res_str,
                "passed": passed,
                "has_blank_frame": has_blank,
                "clock": sc["clock"],
                "level": sc.get("level"),
                "ce_ini": sc.get("ce_ini", {}),
                "exit_code": res["exit_code"],
                "offscreen_confirmed": res["offscreen_confirmed"],
                "audio_dummy_confirmed": res["audio_dummy_confirmed"],
                "x87_confirmed": res["x87_confirmed"],
                "bug_clips": clips_summary,
                "telemetry_end": res["telemetry_end"],
                "telemetry": str(out_root / "telemetry" / f"{sub_name}.json"),
                "run_dir": str(run_dir),
                "shots": [s["label"] for s in res["shots"]],
            })

    manifest = {
        "schema": 1,
        "mode": "autonomous",
        "generated_utc": stamp,
        "commit": commit,
        "exe": str(exe),
        "exe_sha256": sha256_file(exe),
        "passed": not any_failed,
        "scene_count": len(scene_results),
        "frame_count": len(frames),
        "blank_warnings": [f["label"] for f in frames if f["blank_warning"]],
        "scenes": scene_results,
        "frames": frames,
    }
    (out_root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"AUTONOMOUS {'PASS' if not any_failed else 'FAIL'}  runs={len(scene_results)}  frames={len(frames)}  "
          f"manifest={out_root / 'manifest.json'}")
    return 1 if any_failed else 0


def run_gallery(args: argparse.Namespace) -> int:
    exe = Path(args.exe).resolve() if args.exe else discover_exe(prefer_testkit=True)
    if b"ce-testkit-v1" not in exe.read_bytes():
        sys.exit(f"{exe}: --gallery requires a --test-kit build (build.py --public --test-kit)")
    libs, deps_root = resolve_libraries(exe, args.lib)
    out_root = Path(args.out).resolve()
    out_root.mkdir(parents=True, exist_ok=True)
    (out_root / "telemetry").mkdir(exist_ok=True)
    (out_root / "runs").mkdir(exist_ok=True)

    wanted = {s.strip() for s in args.scenes.split(",") if s.strip()} if args.scenes else None
    scenes = [sc for sc in gallery_scenes() if wanted is None or sc["name"] in wanted]
    if wanted is not None:
        known = {sc["name"] for sc in gallery_scenes()}
        unknown = sorted(wanted - known)
        if unknown:
            sys.exit(f"unknown gallery scene(s): {', '.join(unknown)} (known: {', '.join(sorted(known))})")

    gold_career = Path(args.fixtures) / "gold_career_save.bin" if args.fixtures else None
    commit = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")

    scene_results: list[dict[str, object]] = []
    frames: list[dict[str, object]] = []
    any_failed = False

    for sc in scenes:
        name = str(sc["name"])
        seeds: dict[str, Path] = {}
        if sc.get("needs_gold_career"):
            if not gold_career or not gold_career.is_file():
                sys.exit(f"scene {name} needs gold_career_save.bin; pass --fixtures DIR or BEA_JOURNEY_FIXTURES")
            seeds["savegames/Gold.bes"] = gold_career

        run_dir = out_root / "runs" / f"{name}-{stamp}"
        print(f"RUN  {name}: {sc['description']}", flush=True)
        res = run_session(
            exe=exe,
            out_dir=run_dir,
            res=args.res,
            clock=str(sc["clock"]),
            dt=args.dt,
            ce_ini=dict(sc.get("ce_ini", {})),
            seeds=seeds,
            add_sample_mod=bool(sc.get("sample_mod")),
            steps_text=str(sc["steps"]),
            level=sc.get("level"),
            gpu=args.gpu,
            gpu_lock=args.gpu_lock,
            libs=libs,
            deps_root=deps_root,
            game_dir=args.game,
            timeout=args.timeout,
            memory=args.memory,
            png_dest_dir=out_root,
        )
        if res.get("telemetry_file") and Path(str(res["telemetry_file"])).is_file():
            shutil.copy2(Path(str(res["telemetry_file"])), out_root / "telemetry" / f"{name}.json")

        meta = sc.get("shots_meta", {})
        for shot in res["shots"]:
            label = str(shot["label"])
            frames.append({
                "label": label,
                "scene": name,
                "caption": meta.get(label, label),
                "png": str(out_root / f"{label}.png"),
                "width": shot["width"],
                "height": shot["height"],
                "mean_luma": shot["mean_luma"],
                "nonblack_share": shot["nonblack_share"],
                "blank_warning": shot["blank_warning"],
                "sha256": shot["png_sha256"],
                "telemetry": str(out_root / "telemetry" / f"{name}.json"),
            })

        status = "PASS" if res["passed"] else "FAIL"
        print(f"{status} {name}  shots={len(res['shots'])}  exit={res['exit_code']}  end={res['telemetry_end']}", flush=True)
        if not res["passed"]:
            any_failed = True
        scene_results.append({
            "scene": name,
            "description": sc["description"],
            "passed": res["passed"],
            "clock": sc["clock"],
            "level": sc.get("level"),
            "ce_ini": sc.get("ce_ini", {}),
            "exit_code": res["exit_code"],
            "offscreen_confirmed": res["offscreen_confirmed"],
            "audio_dummy_confirmed": res["audio_dummy_confirmed"],
            "x87_confirmed": res["x87_confirmed"],
            "telemetry_end": res["telemetry_end"],
            "telemetry": str(out_root / "telemetry" / f"{name}.json"),
            "run_dir": str(run_dir),
            "shots": [s["label"] for s in res["shots"]],
        })

    manifest = {
        "schema": 1,
        "generated_utc": stamp,
        "commit": commit,
        "exe": str(exe),
        "exe_sha256": sha256_file(exe),
        "resolution": args.res,
        "passed": not any_failed,
        "scene_count": len(scene_results),
        "frame_count": len(frames),
        "blank_warnings": [f["label"] for f in frames if f["blank_warning"]],
        "scenes": scene_results,
        "frames": frames,
    }
    (out_root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"GALLERY {'PASS' if not any_failed else 'FAIL'}  frames={len(frames)}  manifest={out_root / 'manifest.json'}")
    return 1 if any_failed else 0


def parse_kv_items(items: list[str], flag: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for item in items:
        k, sep, v = item.partition("=")
        if not sep or not k.strip():
            raise SystemExit(f"{flag} expects KEY=VALUE, got {item!r}")
        out[k.strip()] = v
    return out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, help="path to bea (test-kit twin for steps/--gallery, or plain candidate for --keys)")
    ap.add_argument("--out", type=Path, help="output folder for PNGs, telemetry JSON, and logs")
    ap.add_argument("--gallery", action="store_true", help="run the repeatable full visual inspection gallery")
    ap.add_argument("--autonomous", "--playtest-suite", dest="autonomous", action="store_true",
                    help="run the closed-loop autonomous gameplay test suite (pathfinding, combat, takeover, F9 clips, multi-res HUD)")
    ap.add_argument("--scenes", help="comma-separated subset of scene names to run with --gallery or --autonomous")
    ap.add_argument("--list-gallery", action="store_true", help="list all gallery scenes and exit")
    ap.add_argument("--list-autonomous", action="store_true", help="list all autonomous playtest scenes and exit")
    ap.add_argument("--steps", type=Path, help="test-kit step script file")
    ap.add_argument("--step", action="append", default=[], help="inline test-kit step line (repeatable)")
    ap.add_argument("--keys", type=Path, help="exact-binary 'PRESENT KEY [hold N]' file (BEA_PORT_FAKE_KEYS)")
    ap.add_argument("--fake-keys", help="raw BEA_PORT_FAKE_KEYS token string for exact-binary runs")
    ap.add_argument("--fake-click", help="raw BEA_PORT_FAKE_CLICK=SECONDS:X,Y string")
    ap.add_argument("--shot-at", type=int, help="capture a single frame at Present N (exact-binary BEA_PORT_SHOT)")
    ap.add_argument("--shot-every", type=int, help="capture a frame every N Presents (BEA_PORT_SHOT_EVERY)")
    ap.add_argument("--quit-at", type=int, help="request clean quit at Present N (BEA_PORT_QUIT)")
    ap.add_argument("--ce-ini", action="append", default=[], help="KEY=VALUE to seed into ce.ini (repeatable)")
    ap.add_argument("--ce-ini-file", type=Path, help="base ce.ini file to merge before --ce-ini overrides")
    ap.add_argument("--seed", action="append", default=[], help="DEST=SRC file to copy into the isolated user folder (repeatable)")
    ap.add_argument("--sample-mod", action="store_true", help="seed a valid sample mod inside user/mods/")
    ap.add_argument("--level", type=int, help="start directly in level N (-level N)")
    ap.add_argument("--res", default=DEFAULT_RES, help="window/backbuffer size WxH (default 1280x720)")
    ap.add_argument("--clock", choices=["fixed", "real", "present"], default="fixed",
                    help="game clock: fixed (default, deterministic) or real (required for Photo Mode / Commander / Play Unit)")
    ap.add_argument("--dt", default=DEFAULT_DT, help="fixed clock step in seconds (default 0.016667)")
    ap.add_argument("--fixtures", default=os.environ.get("BEA_JOURNEY_FIXTURES"),
                    help="folder containing gold_career_save.bin (for gallery Extras scene)")
    ap.add_argument("--game", default=os.environ.get("BEA_GAME_DIR"), help="read-only installed game folder (BEA_GAME_DIR)")
    ap.add_argument("--lib", action="append", default=[d for d in os.environ.get("BEA_JOURNEY_LIBS", "").split(":") if d],
                    help="extra LD_LIBRARY_PATH folder (repeatable)")
    ap.add_argument("--gpu", choices=["intel", "nvidia"], default="intel")
    ap.add_argument("--gpu-lock", default=os.environ.get("GODOT_GPU_LOCK"),
                    help="flock lock file for GPU serialization (defaults to GODOT_GPU_LOCK env var)")
    ap.add_argument("--memory", default="4G", help="systemd-run MemoryMax scope cap (default 4G, 'none' to disable)")
    ap.add_argument("--timeout", type=int, default=240, help="per-run timeout in seconds (default 240)")
    ap.add_argument("--env", action="append", default=[], help="extra KEY=VALUE for the game process (repeatable)")
    ap.add_argument("--arg", action="append", default=[], help="extra command-line argument passed to bea (repeatable)")
    args = ap.parse_args(argv)

    if args.list_gallery:
        for sc in gallery_scenes():
            shots = ", ".join(sc["shots_meta"].keys())
            print(f"{sc['name']:28s} clock={sc['clock']:5s}  {sc['description']}  [{shots}]")
        return 0

    if args.list_autonomous:
        for sc in autonomous_scenes():
            shots = ", ".join(sc["shots_meta"].keys())
            res_label = ",".join(sc["resolutions"]) if "resolutions" in sc else str(sc.get("res", DEFAULT_RES))
            print(f"{sc['name']:30s} clock={sc['clock']:5s} res={res_label:20s} {sc['description']}  [{shots}]")
        return 0

    if not args.out:
        ap.error("--out DIR is required")

    if args.gallery:
        return run_gallery(args)

    if args.autonomous:
        return run_autonomous(args)

    steps_text: str | None = None
    if args.steps or args.step:
        chunks = []
        if args.steps:
            chunks.append(args.steps.read_text())
        if args.step:
            chunks.append("\n".join(args.step))
        steps_text = "\n".join(chunks)

    fake_keys = args.fake_keys
    if args.keys:
        compiled = compile_fake_keys(args.keys.read_text())
        fake_keys = f"{fake_keys},{compiled}" if fake_keys else compiled

    if steps_text is None and fake_keys is None and args.shot_at is None and args.shot_every is None and args.quit_at is None:
        ap.error("provide --gallery, --steps/--step, or --keys/--fake-keys/--shot-at/--quit-at")

    exe = args.exe.resolve() if args.exe else discover_exe(prefer_testkit=steps_text is not None)
    libs, deps_root = resolve_libraries(exe, args.lib)
    ce_ini = parse_kv_items(args.ce_ini, "--ce-ini")
    ce_ini_base = args.ce_ini_file.read_text() if args.ce_ini_file else ""
    seeds_raw = parse_kv_items(args.seed, "--seed")
    seeds = {rel: Path(src).resolve() for rel, src in seeds_raw.items()}
    extra_env = parse_kv_items(args.env, "--env")

    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    out_dir = Path(args.out).resolve()
    if out_dir.exists() and any(out_dir.iterdir()):
        out_dir = out_dir / f"play-{stamp}"

    res = run_session(
        exe=exe,
        out_dir=out_dir,
        res=args.res,
        clock=args.clock,
        dt=args.dt,
        ce_ini=ce_ini,
        ce_ini_base=ce_ini_base,
        seeds=seeds,
        add_sample_mod=args.sample_mod,
        steps_text=steps_text,
        fake_keys=fake_keys,
        fake_click=args.fake_click,
        shot_at=args.shot_at,
        shot_every=args.shot_every,
        quit_at=args.quit_at,
        level=args.level,
        extra_args=args.arg,
        extra_env=extra_env,
        gpu=args.gpu,
        gpu_lock=args.gpu_lock,
        libs=libs,
        deps_root=deps_root,
        game_dir=args.game,
        timeout=args.timeout,
        memory=args.memory,
    )
    print(json.dumps(res, indent=2))
    return 0 if res["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
