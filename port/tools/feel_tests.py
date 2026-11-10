#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Checks of the frame-feel measurement (port/platform/common/feel.h): its plain arithmetic on the host, its wiring by text, and
the report that reads its log, on logs made with known pacing, update cadence, camera motion and input lag.

The synthetic logs establish that the report measures what it says it measures. They say nothing about how the game itself behaves."""
import math
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import feel_report as R  # noqa: E402

MS = 1_000_000


def view(yaw, eye=(0.0, 0.0, 0.0)):
    c, s = math.cos(yaw), math.sin(yaw)
    r, u, f = (c, 0, -s), (0, 1, 0), (s, 0, c)
    tr = tuple(-sum(eye[k] * ax[k] for k in range(3)) for ax in (r, u, f))
    return [r[0], u[0], f[0], 0, r[1], u[1], f[1], 0, r[2], u[2], f[2], 0, tr[0], tr[1], tr[2], 1]


PROJ = [1.2, 0, 0, 0, 0, 1.6, 0, 0, 0, 0, 1.001, 1, 0, 0, -1.0, 0]
ORTHO = [0.003, 0, 0, 0, 0, -0.004, 0, 0, 0, 0, 1, 0, -1, 1, 0, 1]


class SyntheticLog:
    """Writes a log the way feel.cpp does. Frames are Presents every `frame_ms`; updates every `tick_ms`; the camera is a function."""

    def __init__(self, frame_ms=16.667, tick_ms=50.0, frames=600, camera=None, stutter_at=(), fraction=True, input_lag_ms=None,
                 flush_at=None, jitter=None, mouse_stream=None, arm_ms=None, keys=None, map_view=None):
        self.lines = ["# feel 1", "# pace_hz 60 gpu 1 heap 0 max_events 4000000"]
        self.frame_ms, self.tick_ms, self.frames = frame_ms, tick_ms, frames
        t = 1000 * MS
        tick_next = t
        tick_n = 0
        last_tick = t
        if mouse_stream:
            self.lines.append("Q %d script arm present 0" % 0)
            a, b, vx, vy = mouse_stream
            self.lines.append("Q %d script mouse %g %g %g %g" % (0, a, b, vx, vy))
        arm_t = t + (arm_ms or 0) * MS
        for ms, kind, sc in keys or ():
            self.lines.append("Q %d script key %g %s %d" % (0, ms, kind, sc))
        if mouse_stream or keys:
            self.lines.append("Q %d arm present frame 0" % arm_t)
        for i in range(frames):
            start = t
            period = frame_ms + (stutter_at.get(i, 0) if isinstance(stutter_at, dict) else 0)
            if jitter:
                period += jitter[i % len(jitter)]
            end = start + int(period * MS)
            self.lines.append("U %d" % (start + 100000))
            begin = end - 1 * MS
            while tick_next < begin:
                tick_n += 1
                at = max(tick_next, start + 200000)
                self.lines.append("T %d %d 1 2 0 %.6f" % (at, tick_n, tick_n * 0.05))
                last_tick = at
                tick_next += int(tick_ms * MS)
            self.lines.append("B %d" % (start + 2 * MS))
            self.lines.append("J %d 0 0 %s" % (start + 2 * MS, " ".join("%.9g" % x for x in PROJ)))
            cam_t = start + 3 * MS
            yaw = camera(i, begin, last_tick) if camera else 0.0
            self.lines.append("V %d 0 0 %s" % (cam_t, " ".join("%.9g" % x for x in view(yaw, (yaw * 10, 0, 0)))))
            self.lines.append("J %d 800 4000 %s" % (begin - MS, " ".join("%.9g" % x for x in ORTHO)))
            self.lines.append("V %d 800 4000 %s" % (begin - MS, " ".join("%.9g" % x for x in view(0))))
            self.lines.append("E %d 900 4500 0" % (begin - MS // 2))
            frac = min(1.0, max(0.0, (begin - last_tick) / (tick_ms * MS))) if fraction else 1.0
            self.lines.append("P %d %d %d %d 00000000 %d %d 100 5 0" % (i, begin, begin + 200000, end, i * 6 * MS, i * 6 * MS + 5 * MS))
            if map_view:
                x, y, span = map_view(i, begin, last_tick)
                self.lines.append("F %d %.6f 0.5 1 2 0 %.9g %.9g %.9g 0 0 0 0 0 0 0 0 0 m" % (end, frac, x, y, span))
            else:
                self.lines.append("F %d %.6f 0.5 1 2 0" % (end, frac))
            if flush_at is not None and i == flush_at:
                self.lines.append("X %d %d 1000" % (end + 5 * MS, 40 * MS))
            t = end
        self.path = None

    def write(self, directory):
        self.path = Path(directory) / "feel.log"
        self.path.write_text("\n".join(self.lines) + "\n")
        return self.path


class ScriptTests(unittest.TestCase):
    def test_host_driver(self):
        out = Path(tempfile.mkdtemp(prefix="feel-driver-"))
        exe = out / "driver"
        build = subprocess.run(["g++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "port/platform/common"),
                                str(ROOT / "port/tools/tests/feel_script_driver.cpp"), "-o", str(exe)], capture_output=True, text=True, timeout=120)
        self.assertEqual(build.returncode, 0, build.stderr)
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, run.stdout)
        self.assertIn("failures=0", run.stdout)


class ReportTests(unittest.TestCase):
    def report(self, **kw):
        d = tempfile.mkdtemp(prefix="feel-log-")
        log = SyntheticLog(**kw)
        path = log.write(d)
        return R.report(str(path), after_arm=kw.get("mouse_stream") is not None)

    def test_pacing_steady(self):
        r = self.report(frames=400)
        p = r["pacing"]
        self.assertAlmostEqual(p["interval_ms"]["p50"], 16.667, places=2)
        self.assertEqual(p["over_1_5x_median"], 0)
        self.assertAlmostEqual(p["fps_mean"], 60.0, delta=0.1)

    def test_pacing_hitches_are_counted(self):
        r = self.report(frames=400, stutter_at={100: 40.0, 200: 90.0, 300: 10.0})
        p = r["pacing"]
        self.assertEqual(p["over_2x_median"], 2)       # 56.7 ms and 106.7 ms; the 26.7 ms one is over 1.5x but not 2x
        self.assertEqual(p["over_1_5x_median"], 3)
        self.assertEqual(p["over_100ms"], 1)
        self.assertGreater(p["time_in_hitches_pct"], 1.0)

    def test_updates_per_frame(self):
        r = self.report(frames=600)
        hist = r["ticks"]["ticks_per_frame"]
        self.assertEqual(set(hist), {"0", "1"})         # 60 Hz frames against 20 Hz updates
        self.assertAlmostEqual(r["ticks"]["tick_interval_ms"]["p50"], 50.0, places=1)
        self.assertGreater(hist["0"], hist["1"])

    def test_fraction_tracks_elapsed_time(self):
        r = self.report(frames=600)
        self.assertLess(abs(r["ticks"]["fraction_minus_elapsed_over_tick"]["p50"]), 0.05)
        self.assertEqual(r["ticks"]["fraction_backwards_without_tick"], 0)

    def test_smooth_camera_has_no_stationary_frames(self):
        rate = 0.5       # radians per second
        r = self.report(frames=600, camera=lambda i, t, last: rate * t * 1e-9)
        c = r["camera"]
        self.assertEqual(c["rotation_frames_standing_still_in_motion"], 0)
        self.assertLess(c["rotation_cv_while_moving"], 0.05)
        self.assertLess(c["rotation_cv_over_span"], 0.05)
        speeds = [v["rot"] for v in c["speed_by_update_phase"].values()]
        self.assertLess(max(speeds) / min(speeds), 1.1)

    def test_camera_stepping_with_updates_is_visible(self):
        rate = 0.5
        r = self.report(frames=600, camera=lambda i, t, last: rate * last * 1e-9)
        c = r["camera"]
        self.assertGreater(c["rotation_frames_standing_still_in_motion"], 100)
        self.assertGreater(c["rotation_cv_over_span"], 0.5)
        self.assertLess(c["rotation_cv_while_moving"], 0.05)       # when it moves, it moves evenly
        speeds = [v["rot"] for v in c["speed_by_update_phase"].values()]
        self.assertGreater(max(speeds) / max(min(speeds), 1e-12), 3.0)

    def test_world_view_is_the_perspective_pass_with_the_draws(self):
        r = self.report(frames=100, camera=lambda i, t, last: 0.2)
        self.assertEqual(r["camera"]["frames_with_world_view"], 100)
        self.assertAlmostEqual(r["camera"]["views_set_per_frame"]["p50"], 2.0)

    def test_mouse_lag_is_measured(self):
        lag = 70.0
        a, b, vx = 500.0, 2500.0, 1000.0
        gain = 0.0004    # radians per count (the whole stream turns the camera under a radian)

        def camera(i, t, last):
            # the camera shows the input as it was `lag` ms ago
            ms = (t - 1000 * MS) * 1e-6 - lag
            inp = vx * (min(max(ms, a), b) - a) / 1000.0
            return gain * inp
        r = self.report(frames=300, camera=camera, mouse_stream=(a, b, vx, 0.0), arm_ms=0)
        resp = r["input"]
        self.assertAlmostEqual(resp["lag_ms_median"], lag, delta=17.0)
        self.assertAlmostEqual(resp["gain_rad_per_count_median"], gain, delta=gain * 0.05)

    def held_key(self, stepped, lag=0.0, stop_lag=0.0, frame_ms=16.667, tick_ms=50.0):
        """A key held from 1000 to 3000 ms after the arm: the camera turns at 0.5 rad a second (and the eye moves with it) from `lag`
        after the key goes down to `stop_lag` after it goes up; `stepped` shows it as of the last update, as a camera driven by updates does."""
        a, b, rate = 1000.0, 3000.0, 0.5

        def camera(i, t, last):
            ms = ((last if stepped else t) - 1000 * MS) * 1e-6
            return rate * (min(max(ms, a + lag), b + stop_lag) - (a + lag)) / 1000.0
        return self.report(frames=330, camera=camera, keys=[(a, "d", 79), (b, "u", 79)], arm_ms=0, frame_ms=frame_ms, tick_ms=tick_ms)

    def test_a_held_key_moves_a_smooth_camera_on_every_frame(self):
        r = self.held_key(stepped=False)
        entry = r["input"]["keys_held"][0]
        for name in ("pos", "rot"):
            self.assertLess(entry[name]["onset_ms"], 20.0)
            self.assertLess(entry[name]["stop_ms"], 20.0)

    def test_held_motion_tells_a_camera_that_moves_with_updates_from_one_that_moves_with_frames(self):
        for stepped in (False, True):
            d = tempfile.mkdtemp(prefix="feel-log-")
            a, b, rate = 1000.0, 3000.0, 0.5
            log = SyntheticLog(frames=330, keys=[(a, "d", 79), (b, "u", 79)], arm_ms=0, camera=lambda i, t, last: rate * (min(max(((last if stepped else t) - 1000 * MS) * 1e-6, a), b) - a) / 1000.0)
            path = log.write(d)
            loaded = R.load(str(path))
            arm = R.script_of(loaded)[0]
            frames, _ = R.clean(R.build_frames(loaded), loaded)
            m = R.held_motion(R.view_steps(frames), arm + (a + 250) * 1e6, arm + (b - 100) * 1e6)["rot"]
            if stepped:
                self.assertGreater(m["still_pct"], 50.0)
                self.assertGreater(m["cv"], 0.8)
                self.assertGreater(m["moved_pct_when_an_update_ran"], 90.0)
                self.assertLess(m["moved_pct_when_none_ran"], 10.0)
            else:
                self.assertLess(m["still_pct"], 2.0)
                self.assertLess(m["cv"], 0.1)

    def test_key_onset_and_stop_are_measured(self):
        r = self.held_key(stepped=False, lag=70.0, stop_lag=120.0)
        entry = r["input"]["keys_held"][0]["rot"]
        self.assertAlmostEqual(entry["onset_ms"], 70.0, delta=18.0)
        self.assertAlmostEqual(entry["stop_ms"], 120.0, delta=18.0)
        self.assertGreater(entry["half_ms"], entry["onset_ms"] - 1.0)

    def test_a_camera_moved_by_updates_answers_a_key_at_the_next_update(self):
        r = self.held_key(stepped=True)
        entry = r["input"]["keys_held"][0]["rot"]
        self.assertLess(entry["onset_ms"], 70.0)       # within an update period and a frame of the key
        self.assertGreater(entry["half_ms"], 0.0)
        # the same input through a camera that follows the frames answers within a frame
        smooth = self.held_key(stepped=False)["input"]["keys_held"][0]["rot"]
        self.assertLess(smooth["onset_ms"], 20.0)

    def test_the_paused_maps_pan_is_read_from_its_tagged_samples(self):
        a, b, rate = 1000.0, 3000.0, 80.0      # map units a second

        def view(stepped):
            def f(i, t, last):
                ms = ((last if stepped else t) - 1000 * MS) * 1e-6
                return 100.0 + rate * (min(max(ms, a), b) - a) / 1000.0, 200.0, 128.0
            return f
        for stepped in (False, True):
            r = self.report(frames=330, keys=[(a, "d", 79), (b, "u", 79)], arm_ms=0, map_view=view(stepped))
            self.assertNotIn("driven_unit", {k for k, v in r.items() if v})      # the map's numbers are not a unit's pose
            entry = r["input"]["keys_held_map_view"][0]["pos"]
            self.assertAlmostEqual(entry["speed_held"], rate, delta=rate * 0.1)
            d = tempfile.mkdtemp(prefix="feel-log-")
            log = SyntheticLog(frames=330, keys=[(a, "d", 79), (b, "u", 79)], arm_ms=0, map_view=view(stepped)).write(d)
            loaded = R.load(str(log))
            arm = R.script_of(loaded)[0]
            frames, _ = R.clean(R.build_frames(loaded), loaded)
            m = R.held_motion(R.map_view_steps(frames), arm + (a + 250) * 1e6, arm + (b - 100) * 1e6)["pos"]
            if stepped:
                self.assertGreater(m["still_pct"], 50.0)
                self.assertLess(m["moved_pct_when_none_ran"], 10.0)
            else:
                self.assertLess(m["still_pct"], 2.0)

    def test_a_camera_already_moving_is_not_taken_for_an_answer(self):
        def camera(i, t, last):
            ms = (t - 1000 * MS) * 1e-6
            return 0.2 * ms / 1000.0 + (0.5 * (min(max(ms, 1000.0), 3000.0) - 1000.0) / 1000.0)
        r = self.report(frames=330, camera=camera, keys=[(1000.0, "d", 79), (3000.0, "u", 79)], arm_ms=0)
        entry = r["input"]["keys_held"][0]["rot"]
        self.assertAlmostEqual(entry["speed_before"], 0.2, delta=0.01)
        self.assertLess(entry["onset_ms"], 20.0)

    def test_frames_that_a_log_write_landed_in_are_dropped(self):
        r = self.report(frames=200, flush_at=100)
        self.assertGreaterEqual(r["dropped_for_log_writes"], 1)
        self.assertEqual(r["pacing"]["over_2x_median"], 0)

    def test_comparison_prints_changes(self):
        d = tempfile.mkdtemp(prefix="feel-compare-")
        import json
        a = self.report(frames=300, camera=lambda i, t, last: 0.5 * last * 1e-9)
        b = self.report(frames=300, camera=lambda i, t, last: 0.5 * t * 1e-9)
        pa, pb = Path(d) / "a.json", Path(d) / "b.json"
        pa.write_text(json.dumps(a, default=lambda o: None))
        pb.write_text(json.dumps(b, default=lambda o: None))
        from io import StringIO
        out = StringIO()
        R.compare(str(pa), str(pb), out)
        self.assertIn("camera.rotation_cv_while_moving", out.getvalue())


class DrivenUnitTests(unittest.TestCase):
    """The camera against a driven unit's displayed pose: rigid when it should be, and a lagging camera is seen."""

    def log_for(self, lag_frames=0, frames=200):
        lines = ["# feel 1"]
        t = 1000 * MS
        yaws = []
        for i in range(frames):
            start, end = t, t + int(16.667 * MS)
            yaw = 0.01 * i
            yaws.append(yaw)
            cam_yaw = yaws[max(0, i - lag_frames)]
            ux, uy = 0.1 * i, 0.02 * i                    # the unit's displayed position
            # the camera sits 6 behind and 3 above the unit, along the (possibly lagging) heading
            ex = ux - 6 * math.cos(cam_yaw)
            ey = uy - 6 * math.sin(cam_yaw)
            lines.append("U %d" % (start + 100000))
            lines.append("B %d" % (start + 2 * MS))
            lines.append("J %d 0 0 %s" % (start + 2 * MS, " ".join("%.9g" % x for x in PROJ)))
            lines.append("V %d 0 0 %s" % (start + 3 * MS, " ".join("%.9g" % x for x in view(cam_yaw, (ex, ey, 3.0)))))
            lines.append("E %d 900 4500 0" % (end - 2 * MS))
            lines.append("P %d %d %d %d 00000000 %d %d 0 0 0" % (i, end - MS, end - MS + 100000, end, i * 4 * MS, i * 4 * MS + 3 * MS))
            pose = [ux, uy, 0.0, ux - 0.05, uy - 0.01, 0.0, math.cos(yaw), math.sin(yaw), 0.0, math.cos(yaw), math.sin(yaw), 0.0]
            lines.append("F %d 0.5 0.5 1 3 0 %s" % (end, " ".join("%.9g" % x for x in pose)))
            t = end
        d = tempfile.mkdtemp(prefix="feel-unit-")
        path = Path(d) / "feel.log"
        path.write_text("\n".join(lines) + "\n")
        return path

    def test_a_rigid_camera_keeps_its_offset(self):
        r = R.report(str(self.log_for(0)))
        du = r["driven_unit"]
        self.assertEqual(du["frames_driving"], 200)
        self.assertAlmostEqual(du["camera_offset_up"]["p50"], 3.0, places=3)
        # the eye is 6 behind along the heading; view() has its own axis layout, so only the stability is asserted
        self.assertLess(du["camera_offset_forward_change_per_frame"]["p99"], 0.02)
        self.assertLess(du["camera_offset_side_change_per_frame"]["p99"], 0.02)
        self.assertLess(du["unit_displayed_speed_cv_while_moving"], 0.05)

    def test_a_camera_that_lags_the_heading_moves_against_the_unit(self):
        steady = R.report(str(self.log_for(0)))["driven_unit"]
        lagging = R.report(str(self.log_for(4)))["driven_unit"]
        self.assertGreater(lagging["camera_offset_side"]["max"], steady["camera_offset_side"]["max"] + 0.1)


def game_view(yaw, pitch, roll):
    """A world view matrix as the game's logs show one: the z axis points down, a level camera's right axis is (sin yaw, -cos yaw, 0), positive pitch looks down,
    positive roll has the right side lower, and right, up and forward make a right-handed set down the matrix's columns."""
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    fwd = (cp * cy, cp * sy, sp)
    r0 = (sy, -cy, 0.0)
    w = (r0[1] * fwd[2] - r0[2] * fwd[1], r0[2] * fwd[0] - r0[0] * fwd[2], r0[0] * fwd[1] - r0[1] * fwd[0])
    cr, sr = math.cos(roll), math.sin(roll)
    right = tuple(r0[k] * cr + w[k] * sr for k in range(3))
    up = (fwd[1] * right[2] - fwd[2] * right[1], fwd[2] * right[0] - fwd[0] * right[2], fwd[0] * right[1] - fwd[1] * right[0])
    return [right[0], up[0], fwd[0], 0, right[1], up[1], fwd[1], 0, right[2], up[2], fwd[2], 0, 0, 0, 0, 1]


class DrivenViewAngleTests(unittest.TestCase):
    """The view's heading, pitch and roll of a driven unit, frame to frame: smooth when it should be, and a camera that steps with the updates is seen."""

    def log_for(self, angles, frames=240, frame_ms=1000.0 / 60, tick_ms=50.0, driven=True):
        lines = ["# feel 1"]
        t = 1000 * MS
        tick_next, n = t, 0
        for i in range(frames):
            start, end = t, t + int(frame_ms * MS)
            begin = end - MS
            lines.append("U %d" % (start + 100000))
            while tick_next < begin:
                n += 1
                lines.append("T %d %d 1 3 0 %.6f" % (max(tick_next, start + 200000), n, n * 0.05))
                tick_next += int(tick_ms * MS)
            lines.append("B %d" % (start + 2 * MS))
            lines.append("J %d 0 0 %s" % (start + 2 * MS, " ".join("%.9g" % x for x in PROJ)))
            yaw, pitch, roll = angles(i, n)
            lines.append("V %d 0 0 %s" % (start + 3 * MS, " ".join("%.9g" % x for x in game_view(yaw, pitch, roll))))
            lines.append("E %d 900 4500 0" % (begin - MS // 2))
            lines.append("P %d %d %d %d 00000000 %d %d 0 0 0" % (i, begin, begin + 200000, end, i * 4 * MS, i * 4 * MS + 3 * MS))
            if driven:
                pose = [0.1 * i, 0.0, 0.0, 0.1 * i, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0]
                lines.append("F %d 0.5 0.5 1 3 0 %s" % (end, " ".join("%.9g" % x for x in pose)))
            else:
                lines.append("F %d 0.5 0.5 1 3 0" % end)
            t = end
        d = tempfile.mkdtemp(prefix="feel-angles-")
        path = Path(d) / "feel.log"
        path.write_text("\n".join(lines) + "\n")
        return path

    def test_the_angles_are_read_back_from_a_view(self):
        for yaw, pitch, roll in ((0.0, 0.0, 0.0), (1.2, 0.45, -0.1), (-2.9, 0.8, 0.3), (3.0, -0.4, 0.15)):
            got = R.view_angles(game_view(yaw, pitch, roll))
            self.assertAlmostEqual(math.remainder(got[0] - yaw, 2 * math.pi), 0.0, places=6)
            self.assertAlmostEqual(got[1], pitch, places=6)
            self.assertAlmostEqual(got[2], roll, places=6)

    def test_a_steady_turn_and_tilt_have_no_second_difference_and_no_pull_to_the_updates(self):
        # heading 0.6 degrees a frame across the wrap at pi, pitch and roll ramping: nothing changes its rate
        r = R.report(str(self.log_for(lambda i, n: (3.1 + 0.01 * i, 0.4 + 0.0005 * i, 0.0004 * i))))["driven_view_angles"]
        self.assertEqual(r["frames"], 240)
        self.assertAlmostEqual(r["frames_holding_an_update_pct"], 33.3, delta=2.0)
        self.assertAlmostEqual(r["heading_change_per_frame_deg"]["max"], 0.573, delta=0.02)      # the wrap is not a turn of a whole circle
        for name in ("heading", "pitch", "roll"):
            self.assertLess(r["%s_second_difference_deg" % name]["p99"], 0.001, name)
            self.assertAlmostEqual(r["%s_motion_on_update_frames_pct" % name], r["frames_holding_an_update_pct"], delta=6.0, msg=name)

    def test_a_camera_that_steps_with_the_updates_is_seen_in_every_axis(self):
        # each axis jumps at every fourth update (roll by 9 degrees, pitch by 1.4, heading by 2), on the first frame after it
        def stepped(i, n):
            k = n // 4
            return (0.035 * (k % 2), 0.025 * (k % 2), 0.157 * (k % 2))
        r = R.report(str(self.log_for(stepped)))["driven_view_angles"]
        self.assertAlmostEqual(r["roll_change_per_frame_deg"]["max"], 9.0, delta=0.1)
        self.assertAlmostEqual(r["pitch_change_per_frame_deg"]["max"], 1.43, delta=0.05)
        self.assertAlmostEqual(r["heading_change_per_frame_deg"]["max"], 2.0, delta=0.05)
        for name in ("heading", "pitch", "roll"):
            self.assertGreater(r["%s_motion_on_update_frames_pct" % name], 95.0, name)
            self.assertGreater(r["%s_second_difference_deg" % name]["max"], 1.5 * r["%s_change_per_frame_deg" % name]["p50"] + 1.0, name)

    def test_a_log_with_no_driven_unit_has_no_angles(self):
        r = R.report(str(self.log_for(lambda i, n: (0.0, 0.4, 0.0), driven=False)))
        self.assertEqual(r["driven_view_angles"], {})


def function_text(text, signature):
    """The text of the function whose definition starts with `signature` (at the start of a line), through its closing brace at the start of a line."""
    start = text.index(signature)
    return text[start:text.index("\n}\n", start) + 3]


def build_driver(template, pasted, extra=(), cxx_flags=()):
    """Compile a driver template with the function texts pasted at its markers; returns (the executable or None, the compiler's complaints)."""
    out = Path(tempfile.mkdtemp(prefix="feel-driver-"))
    source = (ROOT / "port/tools/tests" / template).read_text()
    for marker, text in pasted.items():
        assert source.count(marker) == 1, marker
        source = source.replace(marker, text)
    (out / "driver.cpp").write_text(source)
    build = subprocess.run(["g++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", *cxx_flags, str(out / "driver.cpp"), "-o", str(out / "driver")],
                           capture_output=True, text=True, timeout=180)
    return (out / "driver" if build.returncode == 0 else None), build.stderr, out


class SwitchTests(unittest.TestCase):
    """PortFeelOn's own text, compiled with counted stand-ins: off unless a log is named, never in a run the CE layer does not admit (a replay, a
    recording, probes, a virtual clock), decided once for the run, and the options read as the header says."""

    def switch_text(self):
        return function_text((ROOT / "port/platform/common/feel.cpp").read_text(), 'extern "C" int PortFeelOn(void)')

    def run_switch(self, switch, mutant=False):
        # a mutant is allowed the warnings its dead code earns; the text as written is not
        exe, complaints, out = build_driver("feel_switch_driver.cpp.in", {"/* PORT_FEEL_ON */": switch}, cxx_flags=["-w"] if mutant else [])
        if exe is None:
            return None, complaints
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60,
                             env={"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "FEEL_DRIVER_LOG": str(out / "feel.log")})
        return run, complaints

    def test_the_switch_as_written(self):
        run, complaints = self.run_switch(self.switch_text())
        self.assertIsNotNone(run, complaints)
        self.assertEqual(run.returncode, 0, run.stdout)
        self.assertIn("failures=0", run.stdout)

    def test_mutants_of_the_switch_are_caught(self):
        original = self.switch_text()
        mutants = {
            "the layer's answer inverted": ("if (!PortCeLayer())", "if (PortCeLayer())"),
            "the layer not asked": ("if (!PortCeLayer())", "if (false)"),
            "an off decision not remembered": ("\tgOn = 0;\n", ""),
            "the decision not looked up": ("\tif (gOn >= 0) return gOn;\n", ""),
            "an empty log name taken for a name": ("if (!path || !*path) return 0;", "if (!path) return 0;"),
            "0 Hz taken for a rate": ("hz >= 1.0", "hz >= 0.0"),
            "any rate taken for a rate": ("hz <= 1000.0", "hz <= 1e12"),
            "any GPU setting but 0 taken as on": ("!(v[0] == '0' && !v[1])", "v[0] != '0'"),
            "any heap setting beginning with 1": ("v[0] == '1' && !v[1]", "v[0] == '1'"),
            "any event limit": ("n > 1000", "n > 0"),
            "a frames setting without a folder": ("used > 0 && v[used] && strlen(v + used) < 560", "used > 0 && strlen(v + used) < 560"),
            "an empty script name read": ("script && *script", "script"),
        }
        for name, (before, after) in mutants.items():
            self.assertEqual(original.count(before), 1, name)
            run, complaints = self.run_switch(original.replace(before, after, 1), mutant=True)
            self.assertIsNotNone(run, "%s must still compile: %s" % (name, complaints))
            self.assertNotEqual(run.returncode, 0, "the mutant '%s' survived" % name)


class KeyHeldNowTests(unittest.TestCase):
    """window.cpp's PcScan and PortKeyHeldNow, compiled against SDL's scancode header with a settable keyboard state: the game's key code to SDL scancode
    mapping for every key SDL has a PC scan code for, held, released and unknown keys, the scripted keys' record, and one refresh of the state for a Present.
    The physical keyboard is not involved."""

    def window_text(self):
        return (ROOT / "port/platform/common/window.cpp").read_text()

    def texts(self):
        window = self.window_text()
        return (function_text(window, "unsigned PcScan(SDL_Scancode s)\n{"), function_text(window, 'extern "C" int PortKeyHeldNow(unsigned dik)'))

    def sdl_include(self):
        sys.path.insert(0, str(ROOT / "port"))
        import build
        include = Path(str(build.deps.PREFIX)) / "include"
        self.assertTrue((include / "SDL3/SDL_scancode.h").is_file(), "SDL's headers are not built (deps.py): %s" % include)
        return include

    def run_driver(self, pcscan, held, mutant=False):
        exe, complaints, _ = build_driver("key_held_now_driver.cpp.in", {"/* PCSCAN */": pcscan, "/* PORT_KEY_HELD_NOW */": held},
                                          cxx_flags=["-I", str(self.sdl_include())] + (["-w"] if mutant else []))
        if exe is None:
            return None, complaints
        return subprocess.run([str(exe)], capture_output=True, text=True, timeout=60), complaints

    def test_the_function_as_written(self):
        pcscan, held = self.texts()
        run, complaints = self.run_driver(pcscan, held)
        self.assertIsNotNone(run, complaints)
        self.assertEqual(run.returncode, 0, run.stdout)
        self.assertIn("failures=0", run.stdout)

    def test_mutants_are_caught(self):
        pcscan, held = self.texts()
        mutants = {
            "the extended flag dropped": ("((dik - 0x80) | 0x100)", "(dik - 0x80)"),
            "the scripted keys not read": ("\tif (gFakeHeld[s])\n\t\treturn 1;\n", ""),
            "the state refreshed at every ask": ("if (refreshed != PortPresentCount())", "if (true)"),
            "the state never refreshed": ("if (refreshed != PortPresentCount())", "if (false)"),
            "the state's length ignored": ("state && (int)s < n ?", "state ?"),
            "a missing state read as up": (": -1;\n}", ": 0;\n}"),
            "the extended bit lost in the table": ("PcScan((SDL_Scancode)s) & 0x1ff", "PcScan((SDL_Scancode)s) & 0xff"),
        }
        for name, (before, after) in mutants.items():
            self.assertEqual(held.count(before), 1, name)
            run, complaints = self.run_driver(pcscan, held.replace(before, after, 1), mutant=True)
            self.assertIsNotNone(run, "%s must still compile: %s" % (name, complaints))
            self.assertNotEqual(run.returncode, 0, "the mutant '%s' survived" % name)


class WiringTests(unittest.TestCase):
    """The hooks are where they should be and are guarded; text checks only."""

    def read(self, rel):
        return (ROOT / rel).read_text()

    def test_call_sites_are_guarded_and_weak(self):
        header = self.read("port/platform/common/feel.h")
        self.assertIn("PORT_FEEL_WEAK", header)
        calls = 0
        for rel in ("port/platform/common/d3d9hooks.cpp", "port/platform/common/window.cpp", "port/platform/common/dinput.cpp",
                    "port/game/updatedump.cpp", "port/platform/linux/main.cpp", "port/platform/linux/kernel.cpp"):
            lines = self.read(rel).splitlines()
            for n, line in enumerate(lines):
                if "PortFeel" not in line or "#include" in line or "PORT_FEEL_ON" in line or line.strip().startswith("//"):
                    continue
                if "FeelSample" in line:
                    continue       # the sampler is only registered from inside the guarded block
                calls += 1
                guard = "\n".join(lines[max(0, n - 14):n])
                self.assertIn("PORT_FEEL_ON", guard, "%s:%d: %s" % (rel, n + 1, line.strip()))
        self.assertGreater(calls, 3)

    def test_off_by_default_and_fenced(self):
        text = self.read("port/platform/common/feel.cpp")
        self.assertIn('getenv("BEA_PORT_FEEL_LOG")', text)
        self.assertIn("PortCeLayer()", text)
        # the reading of the environment happens once and an unset log leaves the module off
        on = text[text.index('extern "C" int PortFeelOn(void)'):]
        on = on[:on.index("\n}\n")]
        self.assertLess(on.index('getenv("BEA_PORT_FEEL_LOG")'), on.index("PortCeLayer()"))
        self.assertIn("return 0;", on[on.index('getenv("BEA_PORT_FEEL_LOG")'):on.index("PortCeLayer()")])

    def test_the_module_changes_no_game_state(self):
        # the sampler reads the game through getters only: every statement copies one getter's value into the sample
        import re
        text = self.read("port/game/updatedump.cpp")
        sampler = text[text.index("static void FeelSample"):]
        body = sampler[sampler.index("{") + 1:sampler.index("\n}\n")]
        statements = [line.strip() for line in body.splitlines() if line.strip()]
        self.assertGreaterEqual(len(statements), 5)
        for line in statements:
            if line.startswith("s->focus = CePlayUnitFeelPose(s->pose);"):
                continue         # the one call into the unit module, checked below
            if line in ("#ifdef CE_VERSION", "#endif") or line.startswith("if (!s->focus && CeCmdFeelView(s->pose)) s->focus = 2;"):
                continue         # the one call into the map module, checked below
            self.assertRegex(line, r"^s->\w+ = GAME\.(Get\w+|IsPaused)\(\);$", line)
        # that call reads the driven unit and writes only into the caller's array
        unit = self.read("port/game/ce_playunit.cpp")
        pose = unit[unit.index('extern "C" int CePlayUnitFeelPose(float *out)'):]
        pose = pose[:pose.index("\n}\n")]
        for line in [l.strip() for l in pose.splitlines()[2:] if l.strip()]:
            self.assertRegex(line, r"^(if \(.*\) return 0;|CUnit \*unit = Held\(\);|const FVector [^;]*= unit->Get\w+\(\)(\.GetColumnY\(\))?(, \w+ = unit->Get\w+\(\)(\.GetColumnY\(\))?)*;"
                                  r"|out\[\d+\] = \w+\.[XYZ];( out\[\d+\] = \w+\.[XYZ];)*|return 1;)$", line)
        # and the map's reads its own fields and writes only into the caller's array
        cmd = self.read("port/game/ce_cmdmap.cpp")
        view = cmd[cmd.index('extern "C" int CeCmdFeelView(float *out)'):]
        view = view[:view.index("\n}\n")]
        for line in [l.strip() for l in view.splitlines()[2:] if l.strip()]:
            self.assertRegex(line, r"^(if \(!gOpen \|\| gStrategic\) return 0;|out\[\d\] = g\w+;( out\[\d\] = g\w+;)*|return 1;)$", line)
        # the tick hook passes values to the log; it assigns nothing in the game
        hook = text[text.index("void PortUpdateHook()"):]
        hook = hook[:hook.index("#ifndef CE_NETWORK_OFF")]
        self.assertNotRegex(hook, r"GAME\.\w+\s*=[^=]")


if __name__ == "__main__":
    unittest.main()
