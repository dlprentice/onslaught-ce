#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused unit tests for port/tools/agent_play.py (no game data, display, or built binary needed)."""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import agent_play  # noqa: E402


class AgentPlayUnitTests(unittest.TestCase):
    def test_format_ce_ini_splits_experimental_section(self):
        text = agent_play.format_ce_ini({
            "extras": "1",
            "catalogue": "1",
            "commander": "1",
            "play_unit": "1",
            "hud_scale": "1",
        })
        lines = text.splitlines()
        exp_idx = lines.index("[Experimental]")
        self.assertIn("extras=1", lines[:exp_idx])
        self.assertIn("catalogue=1", lines[:exp_idx])
        self.assertIn("hud_scale=1", lines[:exp_idx])
        self.assertIn("commander=1", lines[exp_idx + 1:])
        self.assertIn("play_unit=1", lines[exp_idx + 1:])

    def test_format_ce_ini_merges_and_overrides_base(self):
        base = "extras=0\n[Experimental]\ncommander=0\nmuseum=1\n"
        text = agent_play.format_ce_ini({"extras": "1", "commander": "1"}, base_text=base)
        self.assertIn("extras=1", text)
        self.assertNotIn("extras=0", text)
        self.assertIn("commander=1", text)
        self.assertIn("museum=1", text)
        self.assertEqual(text.count("commander="), 1)

    def test_offscreen_env_strips_desktop_and_handles_clocks(self):
        base = {
            "DISPLAY": ":0",
            "WAYLAND_DISPLAY": "wayland-1",
            "WAYLAND_SOCKET": "5",
            "XAUTHORITY": "/tmp/xauth",
            "DBUS_SESSION_BUS_ADDRESS": "unix:path=/run/user/1000/bus",
            "SDL_VIDEODRIVER": "wayland",
            "SDL_AUDIODRIVER": "pipewire",
            "PATH": "/usr/bin:/bin",
        }
        with tempfile.TemporaryDirectory() as td:
            run_dir = Path(td)
            env_fixed = agent_play.build_offscreen_env(
                base_env=base, run_dir=run_dir, res="1280x720", gpu="intel",
                libs=["/lib/a"], deps_root="/deps", game_dir="/game",
                clock="fixed", dt="0.016667",
            )
            for forbidden in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "XAUTHORITY",
                              "DBUS_SESSION_BUS_ADDRESS", "SDL_VIDEODRIVER", "SDL_AUDIODRIVER"):
                self.assertNotIn(forbidden, env_fixed)
            self.assertEqual(env_fixed["SDL_VIDEO_DRIVER"], "offscreen")
            self.assertEqual(env_fixed["BEA_PORT_REQUIRE_OFFSCREEN"], "1")
            self.assertEqual(env_fixed["SDL_AUDIO_DRIVER"], "dummy")
            self.assertEqual(env_fixed["BEA_PORT_REQUIRE_DUMMY_AUDIO"], "1")
            self.assertEqual(env_fixed["BEA_REPORT_NO_OPEN"], "1")
            self.assertEqual(env_fixed["BROWSER"], "/bin/true")
            self.assertEqual(env_fixed["BEA_PORT_WIDE_TEST"], "1280x720")
            self.assertEqual(env_fixed["BEA_PORT_CLOCK"], "fixed")
            self.assertEqual(env_fixed["BEA_PORT_CLOCK_DT"], "0.016667")

            env_real = agent_play.build_offscreen_env(
                base_env=base, run_dir=run_dir, res="1280x720", gpu="intel",
                libs=[], deps_root=None, game_dir=None, clock="real", dt="0.016667",
            )
            self.assertNotIn("BEA_PORT_CLOCK", env_real)
            self.assertNotIn("BEA_PORT_CLOCK_DT", env_real)

    def test_compile_fake_keys_orders_and_rejects_unknown(self):
        tokens = agent_play.compile_fake_keys("# intro\n200 RETURN\n300 F11 hold 6\n")
        self.assertEqual(tokens, "200:40:1c:d,204:40:1c:u,300:68:57:d,306:68:57:u")
        with self.assertRaises(ValueError):
            agent_play.compile_fake_keys("100 NOT_A_KEY\n")

    def test_ppm_png_conversion_and_blank_detection(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            # Black frame -> blank_warning True
            black_ppm = root / "black.ppm"
            black_ppm.write_bytes(b"P6\n16 12\n255\n" + bytes(16 * 12 * 3))
            info_black = agent_play.convert_ppm_to_png(black_ppm, root / "black.png")
            self.assertEqual((info_black["width"], info_black["height"]), (16, 12))
            self.assertTrue(info_black["blank_warning"])
            self.assertTrue((root / "black.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))

            # Rendered non-black frame -> blank_warning False
            bright_ppm = root / "bright.ppm"
            bright_ppm.write_bytes(b"P6\n16 12\n255\n" + bytes([40, 120, 200] * (16 * 12)))
            info_bright = agent_play.convert_ppm_to_png(bright_ppm, root / "bright.png")
            self.assertFalse(info_bright["blank_warning"])
            self.assertGreater(info_bright["mean_luma"], 90.0)
            self.assertEqual(info_bright["nonblack_share"], 1.0)

    def test_parse_verbs_telemetry(self):
        with tempfile.TemporaryDirectory() as td:
            v = Path(td) / "verbs.jsonl"
            v.write_text("\n".join([
                json.dumps({"kit": "ce-testkit-v1", "steps": 2, "clock": "fixed", "dt": "0.016667"}),
                json.dumps({"step": 0, "tick": 10, "line": "dump main", "ok": True, "reply": "dumped"}),
                json.dumps({"dump": "main", "tick": 10, "page": "fe:main", "rows": ["New Game", "Onslaught CE"], "focused": 0, "state": {"game.state": "0"}}),
                json.dumps({"end": "pass", "ticks": 12, "steps": 2, "testwrite": False}),
            ]) + "\n")
            tel = agent_play.parse_verbs_telemetry(v)
            self.assertEqual(tel["step_count"], 1)
            self.assertEqual(tel["dump_count"], 1)
            self.assertEqual(tel["end"]["end"], "pass")
            self.assertEqual(tel["dumps"][0]["page"], "fe:main")

    def test_log_guard_judges(self):
        good = (
            "onslaught-ce: SDL video driver offscreen\n"
            "onslaught-ce: SDL audio driver dummy\n"
            "onslaught-ce: CreateDevice asks 1280x720 format 22 windowed 1 refresh 0\n"
            "onslaught-ce: CreateDevice ok flags 00000040; x87 007f now\n"
        )
        self.assertTrue(agent_play.offscreen_confirmed(good))
        self.assertTrue(agent_play.audio_confirmed(good, "dummy"))
        self.assertEqual(agent_play.x87_confirmed(good), (True, "007f"))
        self.assertFalse(agent_play.offscreen_confirmed("onslaught-ce: SDL video driver wayland\n"))
        self.assertFalse(agent_play.audio_confirmed("onslaught-ce: SDL audio driver pipewire\n", "dummy"))
        self.assertFalse(agent_play.x87_confirmed("onslaught-ce: CreateDevice ok flags 00000040; x87 037f now\n")[0])

    def test_gallery_presets_are_valid_and_unique(self):
        scenes = agent_play.gallery_scenes()
        self.assertGreaterEqual(len(scenes), 9)
        seen_names: set[str] = set()
        seen_shots: set[str] = set()
        for sc in scenes:
            self.assertNotIn(sc["name"], seen_names)
            seen_names.add(sc["name"])
            lines = agent_play.validate_steps_text(sc["steps"])
            self.assertTrue(lines, f"empty steps in {sc['name']}")
            shot_labels = [line.split()[1] for line in lines if line.startswith("shot ")]
            self.assertEqual(set(shot_labels), set(sc["shots_meta"].keys()), sc["name"])
            for label in shot_labels:
                self.assertNotIn(label, seen_shots, f"duplicate shot label {label}")
                seen_shots.add(label)
            # Fenced live-play features must use real clock so BEA_PORT_CLOCK is not set.
            if any(k in sc.get("ce_ini", {}) for k in ("commander", "play_unit")) and sc.get("level") is not None:
                self.assertEqual(sc["clock"], "real", sc["name"])

    def test_autonomous_scenes_are_valid_and_unique(self):
        scenes = agent_play.autonomous_scenes()
        self.assertEqual(len(scenes), 6)
        seen_names: set[str] = set()
        seen_shots: set[str] = set()
        for sc in scenes:
            self.assertNotIn(sc["name"], seen_names)
            seen_names.add(sc["name"])
            all_shot_labels: set[str] = set()
            for sub_name, res_str, steps_text in agent_play.expand_autonomous_scene_steps(sc):
                agent_play.parse_resolution(res_str)
                lines = agent_play.validate_steps_text(steps_text)
                self.assertTrue(lines, f"empty steps in {sub_name}")
                for line in lines:
                    if line.startswith("shot "):
                        label = line.split()[1]
                        all_shot_labels.add(label)
                        self.assertNotIn(label, seen_shots, f"duplicate autonomous shot label {label}")
                        seen_shots.add(label)
            self.assertEqual(all_shot_labels, set(sc["shots_meta"].keys()), sc["name"])
            if any(k in sc.get("ce_ini", {}) for k in ("commander", "play_unit")) and sc.get("level") is not None:
                self.assertEqual(sc["clock"], "real", sc["name"])

    def test_nav_verbs_registered_in_known_verbs(self):
        required = {
            "nav-to", "nav-offset", "nav-explore", "nav-nearest-enemy",
            "aim-at", "aim-nearest-enemy", "engage-enemy", "morph-to",
            "dodge-step", "take-nearest-friendly", "nav-drive-offset", "f9-clip",
        }
        self.assertTrue(required.issubset(agent_play.KNOWN_VERBS))
        lines = agent_play.validate_steps_text("\n".join([
            "nav-to 256 270 tol 3.5 mode walker",
            "nav-offset 20 -15 mode auto",
            "nav-explore 3 radius 60",
            "nav-nearest-enemy range 80",
            "aim-at 100 200 -5 tol 6",
            "aim-nearest-enemy tol 5",
            "engage-enemy ticks 120 maxrange 95",
            "morph-to jet",
            "dodge-step left",
            "take-nearest-friendly maxdist 120",
            "nav-drive-offset 30 0 tol 8",
            "f9-clip 90",
        ]))
        self.assertEqual(len(lines), 12)

    def test_validate_bug_clips_checks_zip_members_and_ffmpeg(self):
        with tempfile.TemporaryDirectory() as td:
            user = Path(td)
            # Empty / missing clips dir -> 0 clips, all_valid=True
            empty = agent_play.validate_bug_clips(user)
            self.assertEqual(empty["clip_count"], 0)
            self.assertTrue(empty["all_valid"])

            clips_dir = user / "clips"
            clips_dir.mkdir()

            # Missing .txt report -> invalid
            bad_zip = clips_dir / "missing_txt.zip"
            with zipfile.ZipFile(bad_zip, "w") as zf:
                zf.writestr("clip.avi", b"not-a-valid-avi")
            bad_res = agent_play.validate_bug_clips(user)
            self.assertEqual(bad_res["clip_count"], 1)
            self.assertFalse(bad_res["all_valid"])
            self.assertFalse(bad_res["clips"][0]["valid"])
            bad_zip.unlink()

            # Valid clip zip (with real tiny MJPEG AVI + PCM audio if ffmpeg is present)
            good_zip = clips_dir / "onslaught-ce-clip-20261127-120000.zip"
            if shutil.which("ffmpeg"):
                avi_tmp = Path(td) / "test.avi"
                subprocess.run(
                    [
                        "ffmpeg", "-v", "error", "-y",
                        "-f", "lavfi", "-i", "color=c=blue:s=64x48:r=20:d=0.25",
                        "-f", "lavfi", "-i", "anullsrc=r=44100:cl=stereo:d=0.25",
                        "-c:v", "mjpeg", "-c:a", "pcm_s16le", "-shortest", str(avi_tmp),
                    ],
                    check=True, capture_output=True,
                )
                with zipfile.ZipFile(good_zip, "w") as zf:
                    zf.write(avi_tmp, "onslaught-ce-clip-20261127-120000.avi")
                    zf.writestr("onslaught-ce-clip-20261127-120000.txt", "Onslaught CE diagnostic report\n")
                good_res = agent_play.validate_bug_clips(user)
                self.assertEqual(good_res["clip_count"], 1)
                self.assertTrue(good_res["all_valid"], good_res)
                self.assertTrue(good_res["clips"][0]["ffmpeg_ok"])


if __name__ == "__main__":
    unittest.main()
