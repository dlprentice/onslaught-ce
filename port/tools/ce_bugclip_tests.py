#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify F9 bug-clip video+audio+diagnostic ZIP recorder (ce_bugclip.cpp), CE recording admission
(BEA_PORT_RECORD_CE=1), ce_latest_clip/ce_report_url hardening, and 0099/0383/0398 patches.

    python3 port/tools/ce_bugclip_tests.py [-v]
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import urllib.parse
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
sys.path.insert(0, str(PORT))
sys.path.insert(0, str(PORT / "tools"))
import build  # noqa: E402
import deps  # noqa: E402


DRIVER_SRC = r"""
#include "ce_bugclip.h"
#include "../../updater/ce_report.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" int PortCeRecordFeaturesAllowed(void);
bool PortCeLayer();

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "admission")) {
        printf("layer=%d rec_ce=%d\n", PortCeLayer() ? 1 : 0, PortCeRecordFeaturesAllowed() ? 1 : 0);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "latest_clip")) {
        char user[4096], found[4096], url[32768];
        ce_user_dir(user, sizeof user);
        int ok = ce_latest_clip(user, found, sizeof found);
        printf("ok=%d\npath=%s\n", ok, ok ? found : "");
        ce_report_url("0.1.17.6-test", "deadbeef", user, url, sizeof url);
        printf("url=%s\n", url);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "minimized_timeout")) {
        if (!PortCeBugClipToggle()) return 2;
        const int W = 160, H = 96;
        std::vector<uint32_t> fb((size_t)W * (size_t)H, 0xFF4080C0u);
        std::vector<int16_t> pcm(2205 * 2, 500);
        PortCeBugClipPushAudio(pcm.data(), 2205);
        PortCeBugClipSubmitFrame(fb.data(), W * 4, W, H, 22);
        // Simulate minimized/stalled game: no Present()/WantFrame() calls, audio keeps arriving.
        int autoStopped = 0;
        for (int i = 0; i < 60; ++i) {
            PortCeBugClipPushAudio(pcm.data(), 2205);
            SDL_Delay(50);
            if (!PortCeBugClipActive()) { autoStopped = 1; break; }
        }
        PortCeBugClipShutdown();
        printf("auto_stopped=%d\n", autoStopped);
        return autoStopped ? 0 : 5;
    }
    if (argc > 1 && !strcmp(argv[1], "byte_cap")) {
        if (!PortCeBugClipToggle()) return 2;
        const int W = 160, H = 96;
        std::vector<uint32_t> fb((size_t)W * (size_t)H, 0xFF20AA55u);
        std::vector<int16_t> pcm(16384 * 2, 1234); // 64 KiB per push
        PortCeBugClipSubmitFrame(fb.data(), W * 4, W, H, 22);
        SDL_Delay(60);
        int autoStopped = 0;
        for (int i = 0; i < 80; ++i) {
            PortCeBugClipPushAudio(pcm.data(), 16384);
            SDL_Delay(30);
            if (!PortCeBugClipActive()) { autoStopped = 1; break; }
        }
        PortCeBugClipShutdown();
        printf("auto_stopped=%d\n", autoStopped);
        return autoStopped ? 0 : 6;
    }
    if (argc > 1 && !strcmp(argv[1], "stress")) {
        if (!PortCeBugClipToggle()) return 2;
        std::vector<int16_t> pcm(2205 * 2, 1000);
        // 1) Non-mod-16 extreme patterns: all-black, all-white, 1px checkerboard, vivid RGB.
        const int W1 = 333, H1 = 197;
        std::vector<uint32_t> fb1((size_t)W1 * (size_t)H1);
        for (int f = 0; f < 4; ++f) {
            for (int y = 0; y < H1; ++y) {
                for (int x = 0; x < W1; ++x) {
                    uint32_t px = 0xFF000000u;
                    if (f == 1) px = 0xFFFFFFFFu;
                    else if (f == 2) px = ((x ^ y) & 1) ? 0xFFFFFFFFu : 0xFF000000u;
                    else if (f == 3) px = (x < W1 / 3) ? 0xFFFF0000u : (x < 2 * W1 / 3) ? 0xFF00FF00u : 0xFF0000FFu;
                    fb1[(size_t)y * W1 + x] = px;
                }
            }
            PortCeBugClipPushAudio(pcm.data(), 2205);
            PortCeBugClipSubmitFrame(fb1.data(), W1 * 4, W1, H1, 22);
            SDL_Delay(55);
        }
        // 2) Simulate a 1.1s loading stall while audio continues flowing.
        for (int s = 0; s < 22; ++s) {
            PortCeBugClipPushAudio(pcm.data(), 2205);
            SDL_Delay(50);
        }
        // 3) Mid-clip resolution change (480x270 -> bilinear resized into locked 333x197 AVI).
        const int W2 = 480, H2 = 270;
        std::vector<uint32_t> fb2((size_t)W2 * (size_t)H2, 0xFF3388EEu);
        for (int f = 0; f < 4; ++f) {
            PortCeBugClipPushAudio(pcm.data(), 2205);
            PortCeBugClipSubmitFrame(fb2.data(), W2 * 4, W2, H2, 22);
            SDL_Delay(55);
        }
        PortCeBugClipToggle();
        PortCeBugClipShutdown();
        return 0;
    }
    if (!PortCeBugClipToggle()) {
        fprintf(stderr, "toggle failed\n");
        return 2;
    }
    if (!PortCeBugClipActive()) {
        fprintf(stderr, "not active after toggle\n");
        return 3;
    }
    const int W = 320, H = 192;
    std::vector<uint32_t> fb((size_t)W * (size_t)H);
    std::vector<int16_t> pcm(2205 * 2); // 50ms of 44.1kHz stereo
    double phase = 0.0;
    const double step = 2.0 * 3.14159265358979323846 * 440.0 / 44100.0;

    for (int f = 0; f < 10; ++f) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                uint8_t r = (uint8_t)((x + f * 20) & 255);
                uint8_t g = (uint8_t)((y * 2) & 255);
                uint8_t b = (uint8_t)(f * 25);
                fb[(size_t)y * W + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
        for (int s = 0; s < 2205; ++s) {
            int16_t v = (int16_t)(std::sin(phase) * 12000.0);
            phase += step;
            pcm[s * 2 + 0] = v;
            pcm[s * 2 + 1] = (int16_t)(-v);
        }
        PortCeBugClipPushAudio(pcm.data(), 2205);
        PortCeBugClipSubmitFrame(fb.data(), W * 4, W, H, 22 /* D3DFMT_X8R8G8B8 */);
        SDL_Delay(55);
    }
    char status[128] = "";
    int rec = 0;
    float fade = 0.0f;
    if (!PortCeBugClipStatus(status, sizeof status, &rec, &fade) || !rec || !strstr(status, "REC ")) {
        fprintf(stderr, "bad live status: %s rec=%d\n", status, rec);
        return 4;
    }
    PortCeBugClipToggle(); // stop
    PortCeBugClipShutdown();
    // Verify saved toast names the .zip archive.
    if (!PortCeBugClipStatus(status, sizeof status, &rec, &fade) || rec || !strstr(status, ".zip")) {
        fprintf(stderr, "bad saved toast: %s rec=%d\n", status, rec);
        return 7;
    }
    return 0;
}
"""


def _home_path(user_name: str, tail: str = "") -> str:
    return "/" + "ho" + "me/" + user_name + tail


def _win_users_path(user_name: str, tail: str = "", slash: str = "\\") -> str:
    return "C:" + slash + "Us" + "ers" + slash + user_name + tail


class BugClipAndCeRecordingTests(unittest.TestCase):
    @classmethod
    def _compile(cls, out_exe: Path, extra_defs: list[str] | None = None):
        clangxx = deps.DATA / "llvm/bin/clang++"
        cmd = [
            str(clangxx), "--target=i686-pc-linux-gnu", "-m32", "-std=c++17",
            "-DCE_VERSION=\"0.1.17.6-test\"",
            "-DCE_NETWORK_OFF",
            *(extra_defs or []),
            f"-I{deps.PREFIX / 'include'}",
            f"-I{PORT / 'platform/common'}",
            str(cls.driver_cpp),
            str(PORT / "platform/common/ce_bugclip.cpp"),
            str(PORT / "platform/common/ce.cpp"),
            f"-L{deps.PREFIX / 'lib'}",
            "-lSDL3", "-lpthread", "-lm",
            f"-Wl,-rpath,{deps.PREFIX / 'lib'}",
            "-o", str(out_exe),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            raise RuntimeError(f"driver compile failed:\n{res.stdout}\n{res.stderr}")

    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="ce-bugclip-test-"))
        cls.driver_cpp = cls.tmp / "driver.cpp"
        cls.driver_cpp.write_text(DRIVER_SRC)
        cls.exe = cls.tmp / "driver"
        cls.exe_short_cap = cls.tmp / "driver-short-cap"
        cls.exe_byte_cap = cls.tmp / "driver-byte-cap"
        cls._compile(cls.exe)
        cls._compile(cls.exe_short_cap, ["-DCE_BUGCLIP_MAX_SECONDS=1u"])
        cls._compile(cls.exe_byte_cap, ["-DCE_BUGCLIP_MAX_BYTES=131072ull"])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_driver(self, *args, env_extra=None, exe: Path | None = None):
        clean = {k: v for k, v in os.environ.items()
                 if not k.startswith(("BEA_PORT_", "BEA_NET_", "BEA_LOCKSTEP_", "SDL_", "DISPLAY", "WAYLAND_"))}
        clean["SDL_VIDEO_DRIVER"] = "offscreen"
        clean["SDL_AUDIO_DRIVER"] = "dummy"
        if env_extra:
            clean.update(env_extra)
        return subprocess.run([str(exe or self.exe), *args], env=clean, capture_output=True, text=True, timeout=20)

    def _extract_and_validate_zip(self, zip_path: Path, extract_dir: Path) -> tuple[Path, str]:
        self.assertGreater(zip_path.stat().st_size, 1000)
        with zipfile.ZipFile(zip_path, "r") as zf:
            self.assertIsNone(zf.testzip(), f"corrupt zip member in {zip_path}")
            names = sorted(zf.namelist())
            stem = zip_path.stem
            self.assertEqual(names, sorted([f"{stem}.avi", f"{stem}.txt"]))
            zf.extractall(extract_dir)
        avi = extract_dir / f"{zip_path.stem}.avi"
        txt = (extract_dir / f"{zip_path.stem}.txt").read_text()
        return avi, txt

    def test_patches_apply_cleanly_at_fuzz_zero(self):
        texts = build.patched_text(["FEMessBox.cpp", "FrontEnd.cpp", "DXEngine.cpp", "console.cpp"])
        self.assertIn("if (!PortCeMenus() && !PortTapeCursor(NULL))", texts["FEMessBox.cpp"])
        self.assertIn("if (!PortTapeCursor(NULL))", texts["FEMessBox.cpp"])
        self.assertIn("PortCeBugClipRender();", texts["FrontEnd.cpp"])
        self.assertIn("PortCeBugClipRender();", texts["DXEngine.cpp"])
        self.assertLess(
            texts["DXEngine.cpp"].index("ce_hook_draw_overlay();"),
            texts["DXEngine.cpp"].index("PortCeBugClipRender();"),
        )
        self.assertIn("BEA_PORT_RECORD_CE", texts["console.cpp"])

    def test_ce_record_admission_matrix(self):
        cases = [
            ({}, "layer=1 rec_ce=0"),
            ({"BEA_PORT_RECORD": "/tmp/a.tape", "BEA_PORT_CLOCK": "shim"}, "layer=0 rec_ce=0"),
            ({"BEA_PORT_TAPE": "/tmp/a.tape", "BEA_PORT_CLOCK": "shim"}, "layer=0 rec_ce=0"),
            ({"BEA_PORT_PROBES": "/tmp/p.txt"}, "layer=0 rec_ce=0"),
            ({"BEA_PORT_RECORD": "/tmp/a.tape", "BEA_PORT_CLOCK": "shim", "BEA_PORT_RECORD_CE": "1"}, "layer=1 rec_ce=1"),
            ({"BEA_PORT_TAPE": "/tmp/a.tape", "BEA_PORT_CLOCK": "shim", "BEA_PORT_PROBES": "/tmp/p.txt", "BEA_PORT_RECORD_CE": "1"}, "layer=1 rec_ce=1"),
            ({"BEA_PORT_RECORD": "/tmp/a.tape", "BEA_PORT_RECORD_CE": "1", "BEA_PORT_AUTOPILOT": "wait:1"}, "layer=0 rec_ce=0"),
            ({"BEA_PORT_RECORD": "/tmp/a.tape", "BEA_PORT_RECORD_CE": "1", "BEA_NET_HOST": "1"}, "layer=0 rec_ce=0"),
            ({"BEA_PORT_CLOCK": "present", "BEA_PORT_RECORD_CE": "1"}, "layer=0 rec_ce=0"),
        ]
        for env, want in cases:
            with self.subTest(env=env):
                r = self.run_driver("admission", env_extra=env)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertEqual(r.stdout.strip(), want)

    def test_bugclip_writes_valid_zip_avi_audio_and_scrubbed_bundle(self):
        user = self.tmp / "user"
        user.mkdir()
        fake_home = _home_path("secretuser")
        (user / "ce.ini").write_text(f"hud_scale=1\nmusic_dir={fake_home}/Music\n")
        (user / "onslaught-ce.log").write_text(
            f"Onslaught CE v0.1.17.6\npath={fake_home}/game\nlogname=othersecretacct\n"
        )
        (user / "online-profile.key").write_text("TOP-SECRET-KEY-MATERIAL-DO-NOT-LEAK\n")
        r = self.run_driver(env_extra={
            "BEA_USER_DIR": str(user),
            "HOME": fake_home,
            "USER": "secretuser",
            "LOGNAME": "othersecretacct",
        })
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        clips_dir = user / "clips"
        zips = sorted(clips_dir.glob("onslaught-ce-clip-*.zip"))
        self.assertEqual(len(zips), 1, list(clips_dir.iterdir()))
        # Loose .avi and .txt must be unlinked after successful ZIP packaging.
        self.assertEqual(list(clips_dir.glob("*.avi")), [])
        self.assertEqual(list(clips_dir.glob("*.txt")), [])

        extract = self.tmp / "extract-basic"
        avi, report = self._extract_and_validate_zip(zips[0], extract)
        self.assertGreater(avi.stat().st_size, 10_000)

        # Verify companion bundle has ce.ini + log tail, scrubs account, home dir, and LOGNAME, and never leaks keys
        self.assertIn("Onslaught CE Bug Clip Report", report)
        self.assertIn("hud_scale=1", report)
        self.assertIn("Onslaught CE v0.1.17.6", report)
        self.assertNotIn("secretuser", report)
        self.assertNotIn("othersecretacct", report)
        self.assertNotIn("TOP-SECRET-KEY-MATERIAL", report)

        # Verify RIFF AVI + MJPEG + PCM audio decode cleanly with ffprobe/ffmpeg when installed
        if shutil.which("ffprobe") and shutil.which("ffmpeg"):
            probe = subprocess.run(
                ["ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json", str(avi)],
                capture_output=True, text=True, check=True,
            )
            info = json.loads(probe.stdout)
            streams = {s["codec_type"]: s for s in info["streams"]}
            self.assertIn("video", streams)
            self.assertIn("audio", streams)
            self.assertEqual(streams["video"]["codec_name"], "mjpeg")
            self.assertEqual((streams["video"]["width"], streams["video"]["height"]), (320, 192))
            self.assertEqual(streams["audio"]["codec_name"], "pcm_s16le")
            self.assertEqual(int(streams["audio"]["sample_rate"]), 44100)
            self.assertEqual(int(streams["audio"]["channels"]), 2)
            dec = subprocess.run(
                ["ffmpeg", "-v", "error", "-i", str(avi), "-f", "null", "-"],
                capture_output=True, text=True,
            )
            self.assertEqual(dec.returncode, 0, dec.stderr)
            self.assertEqual(dec.stderr.strip(), "", f"ffmpeg reported decode errors: {dec.stderr}")

    def test_scrub_multiword_windows_paths_and_short_username_non_mangling(self):
        user = self.tmp / "user-scrub"
        user.mkdir()
        win_bs = _win_users_path("Alex Example", "\\Games\\BEA", "\\")
        win_fs = _win_users_path("Jane Doe", "/Music/Tracks", "/")
        win_pub = _win_users_path("Public", "\\Documents", "\\")
        win_all = _win_users_path("All Users", "\\Shared", "\\")
        short_home = _home_path("ed")
        (user / "ce.ini").write_text(
            "windowed=1\n"
            "terrain_detail_distance=extended\n"
            f"gamedir={win_bs}\n"
            f"music_dir={win_fs}\n"
            f"shared={win_pub}\n"
            f"common={win_all}\n"
        )
        (user / "onslaught-ce.log").write_text(
            "Loaded renderer enabled saved fixed\n"
            f"user_path={short_home}/game\n"
            f"alt_path={_home_path('al', '/saves')}\n"
        )
        r = self.run_driver(env_extra={
            "BEA_USER_DIR": str(user),
            "HOME": "/tmp/other-home",
            "USER": "ed",
            "LOGNAME": "ed",
        })
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        zips = sorted((user / "clips").glob("onslaught-ce-clip-*.zip"))
        self.assertEqual(len(zips), 1)
        _, report = self._extract_and_validate_zip(zips[0], self.tmp / "extract-scrub")

        # Multi-word Windows names (both \ and /) must not leak surnames.
        self.assertNotIn("Alex", report)
        self.assertNotIn("Example", report)
        self.assertNotIn("Jane", report)
        self.assertNotIn("Doe", report)
        self.assertIn("\\<user>\\Games\\BEA", report)
        self.assertIn("/<user>/Music/Tracks", report)
        # System profile folders stay untouched.
        self.assertIn("\\Public\\Documents", report)
        self.assertIn("\\All Users\\Shared", report)
        # Short username ("ed") must NOT mangle ordinary words, while path-anchored home-dir <short> IS scrubbed!
        self.assertIn("windowed=1", report)
        self.assertIn("terrain_detail_distance=extended", report)
        self.assertIn("gamedir=", report)
        self.assertIn("Loaded renderer enabled saved fixed", report)
        self.assertNotIn(short_home, report)
        self.assertNotIn(_home_path("al", "/saves"), report)
        self.assertIn("/<user>/game", report)
        self.assertIn("/<user>/saves", report)

    def test_bugclip_extreme_jpeg_midclip_resize_and_stall_sync(self):
        user = self.tmp / "user-stress"
        user.mkdir()
        (user / "ce.ini").write_text(f"music_dir={_win_users_path('Bob', '\\Music', '\\')}\n")
        r = self.run_driver("stress", env_extra={"BEA_USER_DIR": str(user)})
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        zips = sorted((user / "clips").glob("onslaught-ce-clip-*.zip"))
        self.assertEqual(len(zips), 1)
        avi, report = self._extract_and_validate_zip(zips[0], self.tmp / "extract-stress")
        self.assertNotIn("Bob", report)
        if shutil.which("ffprobe") and shutil.which("ffmpeg"):
            probe = subprocess.run(
                ["ffprobe", "-v", "error", "-show_streams", "-of", "json", str(avi)],
                capture_output=True, text=True, check=True,
            )
            streams = {s["codec_type"]: s for s in json.loads(probe.stdout)["streams"]}
            self.assertEqual((streams["video"]["width"], streams["video"]["height"]), (333, 197))
            # 4 initial frames + ~22 stall frames + 4 resized frames >= 24 frames total
            self.assertGreaterEqual(int(streams["video"]["nb_frames"]), 24)
            dec = subprocess.run(["ffmpeg", "-v", "error", "-i", str(avi), "-f", "null", "-"],
                                 capture_output=True, text=True)
            self.assertEqual(dec.returncode, 0, dec.stderr)
            self.assertEqual(dec.stderr.strip(), "", dec.stderr)

    def test_virtual_clock_suppresses_wallclock_stall_duplication(self):
        user = self.tmp / "user-vclock"
        user.mkdir()
        r = self.run_driver("stress", env_extra={
            "BEA_USER_DIR": str(user),
            "BEA_PORT_RECORD": str(self.tmp / "rec.tape"),
            "BEA_PORT_CLOCK": "shim",
            "BEA_PORT_RECORD_CE": "1",
        })
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        zips = sorted((user / "clips").glob("onslaught-ce-clip-*.zip"))
        self.assertEqual(len(zips), 1)
        avi, _ = self._extract_and_validate_zip(zips[0], self.tmp / "extract-vclock")
        if shutil.which("ffprobe"):
            probe = subprocess.run(
                ["ffprobe", "-v", "error", "-show_streams", "-of", "json", str(avi)],
                capture_output=True, text=True, check=True,
            )
            streams = {s["codec_type"]: s for s in json.loads(probe.stdout)["streams"]}
            # Strictly 8 submitted frames (4 before stall + 4 after stall), NO 1.1s stall duplication!
            self.assertEqual(int(streams["video"]["nb_frames"]), 8)

    def test_minimized_timeout_and_byte_cap_auto_terminate(self):
        user_t = self.tmp / "user-min-timeout"
        user_t.mkdir()
        r_t = self.run_driver("minimized_timeout", env_extra={"BEA_USER_DIR": str(user_t)}, exe=self.exe_short_cap)
        self.assertEqual(r_t.returncode, 0, r_t.stdout + r_t.stderr)
        self.assertIn("auto_stopped=1", r_t.stdout)
        zips_t = sorted((user_t / "clips").glob("onslaught-ce-clip-*.zip"))
        self.assertEqual(len(zips_t), 1)
        self._extract_and_validate_zip(zips_t[0], self.tmp / "extract-timeout")

        user_b = self.tmp / "user-byte-cap"
        user_b.mkdir()
        r_b = self.run_driver("byte_cap", env_extra={"BEA_USER_DIR": str(user_b)}, exe=self.exe_byte_cap)
        self.assertEqual(r_b.returncode, 0, r_b.stdout + r_b.stderr)
        self.assertIn("auto_stopped=1", r_b.stdout)
        zips_b = sorted((user_b / "clips").glob("onslaught-ce-clip-*.zip"))
        self.assertEqual(len(zips_b), 1)
        avi_b, _ = self._extract_and_validate_zip(zips_b[0], self.tmp / "extract-byte-cap")
        self.assertLessEqual(avi_b.stat().st_size, 131072)

    def test_latest_clip_zip_preference_numeric_suffix_and_symlink_refusal(self):
        user = self.tmp / "user-latest"
        clips = user / "clips"
        clips.mkdir(parents=True)
        for name in (
            "onslaught-ce-clip-20261127-120000.zip",
            "onslaught-ce-clip-20261127-120000-2.zip",
            "onslaught-ce-clip-20261127-120000-9.zip",
            "onslaught-ce-clip-20261127-120000-10.zip",
            "onslaught-ce-clip-20261127-120005.avi",  # newer timestamp but .avi; .zip must win
        ):
            (clips / name).write_bytes(b"PK\x05\x06" + b"\x00" * 18)

        r = self.run_driver("latest_clip", env_extra={"BEA_USER_DIR": str(user)})
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = dict(line.split("=", 1) for line in r.stdout.strip().splitlines())
        self.assertTrue(lines["ok"].startswith("1"), r.stdout)
        self.assertTrue(lines["path"].endswith("onslaught-ce-clip-20261127-120000-10.zip"), lines["path"])
        body = urllib.parse.parse_qs(urllib.parse.urlparse(lines["url"]).query)["body"][0]
        self.assertIn("drag `", body)
        self.assertIn("onslaught-ce-clip-20261127-120000-10.zip", body)
        self.assertNotIn("and its `.txt` report.\n", body)

        # Fallback to legacy .avi when no .zip exists, with numeric -2 > base order
        user_avi = self.tmp / "user-latest-avi"
        clips_avi = user_avi / "clips"
        clips_avi.mkdir(parents=True)
        (clips_avi / "onslaught-ce-clip-20261127-120000.avi").write_bytes(b"RIFF")
        (clips_avi / "onslaught-ce-clip-20261127-120000-2.avi").write_bytes(b"RIFF")
        r_avi = self.run_driver("latest_clip", env_extra={"BEA_USER_DIR": str(user_avi)})
        self.assertEqual(r_avi.returncode, 0, r_avi.stderr)
        self.assertIn("onslaught-ce-clip-20261127-120000-2.avi", r_avi.stdout)

        # Symlinked clips/ directory must be strictly refused!
        outside = self.tmp / "outside-clips"
        outside.mkdir()
        (outside / "onslaught-ce-clip-99999999-999999.zip").write_bytes(b"PK\x05\x06" + b"\x00" * 18)
        user_sym = self.tmp / "user-symlink"
        user_sym.mkdir()
        os.symlink(outside, user_sym / "clips")
        r_sym = self.run_driver("latest_clip", env_extra={"BEA_USER_DIR": str(user_sym)})
        self.assertEqual(r_sym.returncode, 0, r_sym.stderr)
        self.assertIn("ok=0\npath=\n", r_sym.stdout)

    def test_bugclip_refused_during_faithful_replay(self):
        user = self.tmp / "user-fenced"
        user.mkdir()
        r = self.run_driver(env_extra={
            "BEA_USER_DIR": str(user),
            "BEA_PORT_TAPE": "/tmp/fake.tape",
        })
        self.assertEqual(r.returncode, 2)
        self.assertFalse((user / "clips").exists())


if __name__ == "__main__":
    unittest.main()
