#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The music and mixer observations of patch 0187, on the production mixer and the complete music worker.

The refill driver (tests/audio_refill_driver.cpp, compiled with AUDIO_COUNTERS) runs the actual DirectSound mixer and the complete music worker
(src/PCMusicStream.cpp with every port patch's section) with authored PCM, virtual time and scripted scheduling; the observation API is recorded.
Controls: a worker that keeps up (c-normal), one held back for 3.1 s (c-starved), the 0.1.10 start (c-double: the music initialised twice, the first
stream's worker still handling its buffer's events), a reader that takes 3 ms (c-slowread), and the long runs of each (c-*-limit) that cross the line
budgets. Variants: "candidate" (this tree), "predecessor" (this tree without patch 0113: the defect, the frozen negative control), "baseline" (the
sources of the commit before patch 0187: dsound.cpp from git, no 0187 section), and a mutant for every counter. Gates: the ordinary session and the
faithful, virtual-clock, replay, recording and probe routes, where nothing may be observed and the baseline's playback must be reproduced byte for
byte (the mixed output's hash, frame counts, dispatches, voices and clock reads).

No game, device, retail data, display or Windows runtime; a host scheduling and ownership check, not an audible-output claim.
"""
import argparse
import json
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PATCH = "0187-audio-observation-counters.patch"
DOUBLE_FIX = "0113-music-stream-second-init.patch"
NAMES = ["music_risk_refills", "music_pair_refills", "music_order_anomalies", "music_lines_dropped", "music_voice_starts", "music_stale_halves",
         "music_overwritten_halves", "audio_callbacks_late", "events_dropped", "errors_dropped", "music_refill_us_max", "music_notify_age_us_max",
         "music_voices_max", "audio_gap_us_max", "device_rate", "device_channels", "device_period_frames"]
GATES = {"ordinary": {}, "faithful": {"BEA_PORT_CE_LAYER": "0"}, "clock": {"BEA_PORT_CLOCK": "shim"},
         "replay": {"BEA_PORT_TAPE": "fixture", "BEA_PORT_CE_LAYER": "1"}, "record": {"BEA_PORT_RECORD": "fixture", "BEA_PORT_CE_LAYER": "1"},
         "probes": {"BEA_PORT_PROBES": "fixture", "BEA_PORT_CE_LAYER": "1"}}
PLAYBACK = ("frames", "mismatched_frames", "refill_dispatches", "old_dispatches", "voices", "output_fnv", "clock_reads")


def function(text, signature):
    begin = text.index(signature)
    return text[begin:text.index("\n}", begin) + 2] + "\n"


def baseline_ref():
    """the commit before patch 0187 entered history, or HEAD while it is not committed yet"""
    added = subprocess.run(["git", "log", "--diff-filter=A", "--format=%H", "-1", "--", "port/patches/" + PATCH], cwd=ROOT,
                           capture_output=True, text=True).stdout.strip()
    return added + "^" if added else "HEAD"


def music_source(folder, skip):
    """src/PCMusicStream.cpp with the PCMusicStream.cpp section of every port patch but those in `skip`, in number order, as music.inc"""
    folder.mkdir(parents=True)
    (folder / "PCMusicStream.cpp").write_bytes((ROOT / "src/PCMusicStream.cpp").read_bytes())
    for patch in sorted((ROOT / "port/patches").glob("*.patch")):
        if patch.name in skip:
            continue
        sections = [s for s in re.split(r"(?=^--- a/)", patch.read_text(), flags=re.M) if s.startswith("--- a/PCMusicStream.cpp\n")]
        if sections:
            r = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1"], cwd=folder, input="".join(sections), capture_output=True, text=True, timeout=10)
            if r.returncode:
                raise RuntimeError(f"{patch.name} does not apply: {r.stdout}{r.stderr}")
    (folder / "music.inc").write_text(re.sub(r"^#include[^\n]*\n", "", (folder / "PCMusicStream.cpp").read_text(), flags=re.M))


class Counters(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        parent = ROOT / "local-data/port/audio-counters-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
        common = Path(subprocess.check_output(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT, text=True).strip()).parent
        cls.data = common / "local-data/port"
        cls.env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_", "SDL_AUDIO", "FIXTURE_"))}
        cls.env.update(TMPDIR=str(cls.out), OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1")
        cls.ref = baseline_ref()
        cls.baseline_mixer = subprocess.run(["git", "show", f"{cls.ref}:port/dsound/dsound.cpp"], cwd=ROOT, capture_output=True, text=True, check=True).stdout
        cls.baseline_logger = subprocess.run(["git", "show", f"{cls.ref}:port/platform/common/portlog.cpp"], cwd=ROOT, capture_output=True, text=True, check=True).stdout
        cls.source = {"candidate": (ROOT / "port/dsound/dsound.cpp").read_text(), "baseline": cls.baseline_mixer}
        cls.logger = (ROOT / "port/platform/common/portlog.cpp").read_text()
        cls.admission = function(cls.logger, "bool PortSessionEnabled()")
        cls.ce = function((ROOT / "port/platform/common/ce.cpp").read_text(), "bool PortCeLayer()")
        cls.binaries = {}
        cls.results = {}
        cls.build("candidate", set())
        cls.build("predecessor", {DOUBLE_FIX}, mixer="candidate")
        cls.build("baseline", {PATCH}, mixer="baseline")

    @classmethod
    def build(cls, label, skip, mixer="candidate", mutate=None):
        """compile the driver for one variant; `mutate` is (file, old, new) applied once to music.inc or dsound.cpp"""
        folder = cls.out / label
        music_source(folder, skip)
        mixer_text = cls.source[mixer]
        if mutate:
            name, old, new = mutate
            target = folder / "music.inc" if name == "music.inc" else None
            if target is not None:
                text = target.read_text()
                assert text.count(old) == 1, (label, old)
                target.write_text(text.replace(old, new))
            else:
                assert mixer_text.count(old) == 1, (label, old)
                mixer_text = mixer_text.replace(old, new)
        mixer_path = folder / "dsound.cpp"
        mixer_path.write_text(mixer_text)
        (folder / "admission.inc").write_text(cls.admission)
        (folder / "ce-admission.inc").write_text(cls.ce)
        binary = folder / "driver"
        command = ["clang++", "-std=c++17", "-O1", "-g", "-pthread", "-DNOMINMAX", "-DINITGUID", "-fms-extensions", "-fno-autolink", "-w",
                   '-DCE_VERSION="fixture"', "-DAUDIO_COUNTERS", "-I" + str(folder), "-I" + str(ROOT / "port/d3dx/include"), "-I" + str(ROOT / "port/include"),
                   "-I" + str(ROOT / "port/include/linux"), "-I" + str(cls.data / "dxvk-native/usr/include/dxvk"), "-I" + str(cls.data / "prefix-sniper/include"),
                   '-DMIXER_SOURCE="' + str(mixer_path) + '"', "-include", str(ROOT / "port/platform/common/portlog.h"),
                   str(ROOT / "port/tools/tests/audio_refill_driver.cpp"), "-o", str(binary)]
        build = subprocess.run(command, capture_output=True, text=True, timeout=120, env=cls.env)
        (folder / "build.log").write_text(build.stdout + build.stderr)
        if build.returncode:
            raise RuntimeError(f"compile failed: {folder / 'build.log'}")
        cls.binaries[label] = binary
        return binary

    def run_driver(self, label, mode, gate="ordinary"):
        r = subprocess.run([str(self.binaries[label]), mode], capture_output=True, text=True, timeout=30, env=dict(self.env, **GATES[gate]))
        self.assertEqual(r.returncode, 0, f"{label} {gate} {mode}: {r.stdout}{r.stderr}")
        result = json.loads(r.stdout)
        result["obs"] = dict(zip(NAMES, result["obs"]))
        self.results[(label, gate, mode)] = result
        return result

    # The expected observation values of each control, with the reason. Everything not named is zero.
    def expect(self, label, mode):
        """the nonzero observations a control must produce on `label`"""
        base = {"music_voice_starts": 1, "music_voices_max": 1, "device_rate": 44100, "device_channels": 2, "device_period_frames": 441}
        limit = "limit" in mode
        if "double" in mode and label == "predecessor":
            # the 0.1.10 start: two voices, and every boundary refilled a second time on the second buffer, the first stream's worker
            # handling the first buffer's notifications (at once, 210 ms behind, or 610 ms behind: a pair is within half a second)
            base.update(music_voice_starts=2, music_voices_max=2, music_order_anomalies=1)
            if "-late" in mode:
                base.update(music_stale_halves=3, music_overwritten_halves=3, music_notify_age_us_max=620000)
            elif "-lag" in mode:
                base.update(music_pair_refills=3, music_stale_halves=3, music_overwritten_halves=3, music_notify_age_us_max=220000)
            elif limit:
                base.update(music_pair_refills=150, music_lines_dropped=172, music_stale_halves=149, music_overwritten_halves=150, music_notify_age_us_max=10000)
            else:
                base.update(music_pair_refills=4, music_stale_halves=3, music_overwritten_halves=4, music_notify_age_us_max=10000)
        elif "double" in mode:
            base.update(music_voice_starts=2, music_notify_age_us_max=10000)   # started, shut down by patch 0113, started again: one voice at a time
            if limit:
                base.update(music_lines_dropped=22)
        elif "starved" in mode:
            # the worker held back for 3.1 s: one half is entered again without a refill (4410 frames of old data, the driver's own count),
            # and the late refill writes into the half being played and is flagged as underrun risk
            base.update(music_risk_refills=1, music_stale_halves=1, music_notify_age_us_max=3072154)
            if limit:
                base.update(music_lines_dropped=21)                      # 150 refills, 129 lines: 128 ordinary and the one risk line
        elif "slowread" in mode:
            base.update(music_notify_age_us_max=10000, music_refill_us_max=3000)
        else:                                                            # c-normal, c-prime, c-restart: the worker keeps up
            base.update(music_notify_age_us_max=10000)                   # one mixing block (441 frames) between the notification and the refill
            if "restart" in mode:
                base.update(music_voice_starts=2)                        # started, stopped, started again: neither is a stale replay or an overwrite
            if limit:
                base.update(music_lines_dropped=22)                      # 150 refills, 128 ordinary lines
        return base

    def check_obs(self, label, mode, result):
        got = {k: v for k, v in result["obs"].items() if v}
        self.assertEqual(got, self.expect(label, mode), f"{label} {mode}")
        self.assertEqual(result["second_voice_lines"], 1 if label == "predecessor" and "double" in mode else 0, f"{label} {mode}: the one-off second-voice line")

    def test_the_controls_produce_exactly_their_observations(self):
        for mode in ("c-normal", "c-slowread", "c-starved", "c-double", "c-double-lag", "c-double-late", "c-prime", "c-restart", "c-normal-limit", "c-starved-limit", "c-double-limit"):
            for label in ("candidate", "predecessor"):
                with self.subTest(label=label, mode=mode):
                    if label == "predecessor" and "double" not in mode:
                        continue                                          # the same code outside the double start
                    self.check_obs(label, mode, self.run_driver(label, mode))

    def test_the_double_start_is_flagged_in_one_line_and_the_fix_is_clean(self):
        defect = self.run_driver("predecessor", "c-double")
        fixed = self.run_driver("candidate", "c-double")
        self.assertEqual((defect["second_voice_lines"], defect["voices"]), (1, 2))        # one bounded line, two voices playing
        self.assertEqual((fixed["second_voice_lines"], fixed["voices"]), (0, 1))
        self.assertGreater(defect["mismatched_frames"], 0)                                   # and the output really is not the track
        self.assertEqual(fixed["mismatched_frames"], 0)
        self.assertEqual(defect["counted_refills"], 8)                                       # two refills per boundary against one
        self.assertEqual(fixed["counted_refills"], 4)

    def test_counters_keep_counting_after_the_line_budgets_are_spent(self):
        # 150 reports: 128 ordinary lines at most; the long double start crosses every budget
        r = self.run_driver("predecessor", "c-double-limit")
        self.assertEqual(r["refill_lines"], 128)                                              # the ordinary budget
        self.assertEqual(r["counted_refills"], 300)                                           # every refill, logged or not
        self.assertEqual(r["obs"]["music_lines_dropped"], r["counted_refills"] - r["refill_lines"])        # what was reported and not logged
        self.assertGreater(r["obs"]["music_pair_refills"], r["refill_lines"])                  # more pairs than there are lines

    def test_every_gate_but_the_ordinary_session_observes_nothing_and_reproduces_the_baselines_playback(self):
        for gate in GATES:
            for mode in ("c-normal", "c-starved", "c-double", "c-slowread"):
                with self.subTest(gate=gate, mode=mode):
                    base = self.run_driver("baseline", mode, gate)
                    cand = self.run_driver("candidate", mode, gate)
                    for key in PLAYBACK:
                        self.assertEqual(cand[key], base[key], f"{gate} {mode} {key}")      # the mixed output's hash, frame counts, dispatches, voices, clock reads
                    if gate != "ordinary":
                        self.assertEqual({k: v for k, v in cand["obs"].items() if v}, {}, f"{gate} {mode}")
                        self.assertEqual(cand["refill_lines"], 0)
                        self.assertEqual(cand["clock_reads"], base["clock_reads"])

    def test_without_the_patch_the_same_controls_observe_nothing(self):
        # red before: the baseline (the sources of the commit before patch 0187) runs the same driver and every nonzero expectation fails
        for mode in ("c-starved", "c-double", "c-slowread"):
            with self.subTest(mode=mode):
                result = self.run_driver("baseline", mode)
                self.assertEqual({k: v for k, v in result["obs"].items() if v}, {})
                with self.assertRaises(AssertionError):
                    self.check_obs("candidate", mode, result)

    def test_mutants_of_the_observations_are_caught(self):
        # each mutant removes or breaks one counter in the worker (music.inc) or the mixer (dsound.cpp); the control that needs it must fail
        mutants = (  # name, file, old, new, control; a control that needs the double start runs on the predecessor, the others on the candidate
            ("risk-not-counted", "music.inc", "    if (risk) PortSessionObserve(PORT_OBS_MUSIC_RISK_REFILLS);\n", "", "c-starved"),
            ("pair-not-counted", "music.inc", "    if (pair) PortSessionObserve(PORT_OBS_MUSIC_PAIR_REFILLS);\n", "", "c-double"),
            ("pair-any-half", "music.inc", "requested==sPortRefillLastHalf;", "true;", "c-starved"),
            ("pair-any-gap", "music.inc", "since<500000000ull&&", "", "c-double-late"),
            ("order-not-counted", "music.inc", "    PortSessionObserve(PORT_OBS_MUSIC_ORDER_ANOMALIES); // patch 0187: counted before the line cap\n", "", "c-double"),
            ("dropped-not-counted", "music.inc", "if (lines>=(risk?16u:128u)) { PortSessionObserve(PORT_OBS_MUSIC_LINES_DROPPED); return; }",
             "if (lines>=(risk?16u:128u)) { return; }", "c-normal-limit"),
            ("refill-time-not-kept", "music.inc", "PortSessionObserveMax(PORT_OBS_MUSIC_REFILL_US_MAX,", "(void)(", "c-slowread"),
            ("notify-age-not-kept", "music.inc", "PortSessionObserveMax(PORT_OBS_MUSIC_NOTIFY_AGE_US_MAX,", "(void)(", "c-starved"),
            ("age-from-the-wrong-count", "music.inc", "before.mixed+1-before.notifyMixed", "before.notifyMixed", "c-starved"),
            ("voice-start-not-counted", "dsound.cpp", "				PortSessionObserve(PORT_OBS_MUSIC_VOICE_STARTS);\n", "", "c-normal"),
            ("voices-max-not-kept", "dsound.cpp", "				PortSessionObserveMax(PORT_OBS_MUSIC_VOICES_MAX, music);\n", "", "c-double"),
            ("second-voice-line-lost", "dsound.cpp", "				secondMusicVoice = music > 1;\n", "", "c-double"),
            ("stale-not-counted", "dsound.cpp", "				PortSessionObserve(PORT_OBS_MUSIC_STALE_HALVES);\n", "				(void)0;\n", "c-starved"),
            ("stale-for-every-entry", "dsound.cpp", "if (__atomic_exchange_n(&halfFresh[h], (unsigned char)0, __ATOMIC_RELAXED) == 0)", "if (__atomic_exchange_n(&halfFresh[h], (unsigned char)0, __ATOMIC_RELAXED) != 7)", "c-normal"),
            ("overwrite-not-counted", "dsound.cpp", "						PortSessionObserve(PORT_OBS_MUSIC_OVERWRITTEN_HALVES);\n", "						(void)0;\n", "c-double"),
            ("play-writes-count-as-fresh", "dsound.cpp", "(unsigned char)(h == playingHalf ? 0 : 1)", "(unsigned char)1", "c-starved"),
            ("halves-not-unknown-on-play", "dsound.cpp", "				halfFresh[1 - current] = 2;\n", "				halfFresh[1 - current] = 1;\n", "c-prime"),
            ("halves-not-unknown-on-stop", "dsound.cpp", "			halfFresh[0] = halfFresh[1] = 2;		// patch 0187: what is written while it is stopped is not an overwrite", "			(void)0;", "c-restart"),
            ("device-numbers-lost", "dsound.cpp", "						PortSessionObserveMax(PORT_OBS_DEVICE_RATE, granted.freq > 0 ? (unsigned)granted.freq : 0);\n", "", "c-normal"),
        )
        for name, where, old, new, mode in mutants:
            with self.subTest(mutant=name):
                variant = "predecessor" if "double" in mode else "candidate"
                label = "mutant-" + name
                self.build(label, {DOUBLE_FIX} if variant == "predecessor" else set(), mutate=(where, old, new))
                result = self.run_driver(label, mode)
                with self.assertRaises(AssertionError, msg=f"mutant {name} survived {mode}"):
                    self.check_obs(variant, mode, result)


if __name__ == "__main__":
    unittest.main()
