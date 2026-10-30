#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The verdict of the real-game start control (run_startup_music_tests.py judge), on authored session lines.

The control runs the game; its verdict is a function of the session log's lines, so the function is tested here on lines made
for the test, in the log's own format, with no game. What it must hold:

- a start at the front end after the configuration line, with nothing outstanding when it began, passes `--expect fixed`;
- the front end's track ending and starting again (a run of 1820 Presents or more) is a sequential restart and still passes:
  the control used to demand exactly one start and failed there (the finding on patch 0187's review);
- a start made while another stream plays (outstanding_music_starts=2), a first start before the front end, a log without
  the configuration line and a seed that was not applied each fail `--expect fixed`; the 0.1.10 start passes `--expect orphan`;
- the audio-obs line, when there is one (patch 0187), is read although its kind has a hyphen, and its music_voices_max must equal
  the log's own largest outstanding count: a mixer that played one voice while the log counts two, or the reverse, fails.

Each rule has a mutant: the old exactly-one rule, the overlap rule dropped, the cross-check dropped, a first start before the
front end allowed, and the kind pattern without the hyphen. The case written for a mutant is judged by the control and by the
mutant, and the two verdicts must differ.
"""
import importlib.util
from pathlib import Path
import sys
import unittest

TOOLS = Path(__file__).resolve().parent
SOURCE = TOOLS / "run_startup_music_tests.py"
sys.path.insert(0, str(TOOLS))


def load(text=None, name="startup_music_control"):
    """The control as a module: from the file, or from its text with one rule changed."""
    source = SOURCE.read_text() if text is None else text
    spec = importlib.util.spec_from_loader(name, loader=None)
    module = importlib.util.module_from_spec(spec)
    module.__file__ = str(SOURCE)
    exec(compile(source, str(SOURCE), "exec"), module.__dict__)
    return module


def row(present, kind, text):
    return f"onslaught-ce: session pid=4242 t_ms={present * 33} P={present} {kind}: {text}"


def start(present, outstanding):
    """The lines the log writes for one music start: the request, the buffer's start, then the count of streams it began with."""
    return [row(present, "music", "play requested asset=track.ogg result=00000000"),
            row(present, "music", "worker open attempted asset=track.ogg result=00000000"),
            row(present, "music", "buffer start asset=track.ogg result=00000000"),
            row(present, "music", f"start_lifetime=begun outstanding_music_starts={outstanding} outstanding_film_starts=0")]


def stop(present, outstanding):
    return [row(present, "music", "stop complete asset=track.ogg result=00000000"),
            row(present, "music",
                f"start_lifetime=stopped_or_closed outstanding_music_starts={outstanding} outstanding_film_starts=0")]


def init(present):
    return row(present, "music", "stream buffer init result=00000000 available=1 bytes=524288")


def front_end(present):
    return row(present, "game", "frontend page=1")


def configuration(rate=0):
    return row(0, "game", f"configuration sound_rate={rate} hw_sound=1")


def audio_obs(voices):
    return row(3000, "audio-obs", f"music_risk_refills=0 music_pair_refills=0 music_voices_max={voices} music_stale_halves=0")


def fixed_log(restart=False, restart_outstanding=1, voices=None, front_end_before_start=True, rate=0, with_configuration=True):
    """The start after patch 0113: an early start, the stream shut down when it is initialised again, one start at the front end,
    and, with restart, the track starting again once it has ended."""
    lines = [init(0), *start(0, 1)]
    if with_configuration:
        lines.append(configuration(rate))
    lines += [row(1, "music", "stream re-initialised: the earlier stream is shut down first"), *stop(1, 0), init(1)]
    if front_end_before_start:
        lines.append(front_end(6))
    lines += start(6, 1)
    if not front_end_before_start:
        lines.append(front_end(7))
    if restart:
        lines += [*stop(1817, 0), *start(1818, restart_outstanding)]
    if voices is not None:
        lines.append(audio_obs(voices))
    lines.append(row(3000, "summary", "frames=3000"))
    return lines


def orphan_log(voices=None):
    """The 0.1.10 start: the early stream is never stopped, so the front end's start is the second one outstanding."""
    lines = [init(0), *start(0, 1), configuration(), init(1), front_end(6), *start(6, 2)]
    if voices is not None:
        lines.append(audio_obs(voices))
    lines.append(row(3000, "summary", "frames=3000"))
    return lines


class StartControlVerdict(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.control = load()

    def judge(self, lines, expect="fixed"):
        return self.control.judge(lines, expect)

    def test_the_start_after_0113_passes_fixed_with_or_without_the_mixer_line(self):
        for voices in (None, 1):
            passed, reasons, facts = self.judge(fixed_log(voices=voices))
            self.assertTrue(passed, reasons)
            self.assertEqual(facts["max_outstanding_music_starts"], 1)
            self.assertEqual(facts["buffer_starts_after_configuration"], 1)
            self.assertEqual(facts["audio_obs"] is None, voices is None)

    def test_the_front_end_track_starting_again_is_a_sequential_restart_and_passes(self):
        passed, reasons, facts = self.judge(fixed_log(restart=True, voices=1))
        self.assertTrue(passed, reasons)
        self.assertEqual(facts["buffer_starts_after_configuration"], 2)
        self.assertEqual(facts["max_outstanding_music_starts"], 1)

    def test_a_start_made_while_another_stream_plays_fails_fixed(self):
        passed, reasons, _ = self.judge(orphan_log(voices=2))
        self.assertFalse(passed)
        self.assertTrue(any("outstanding_music_starts reached 2" in r for r in reasons), reasons)
        # a restart that began with the earlier stream still outstanding is the same fault
        passed, reasons, _ = self.judge(fixed_log(restart=True, restart_outstanding=2, voices=2))
        self.assertFalse(passed)
        self.assertEqual([r for r in reasons if "outstanding_music_starts reached 2" not in r], [])

    def test_a_first_start_before_the_front_end_fails_fixed(self):
        passed, reasons, _ = self.judge(fixed_log(front_end_before_start=False))
        self.assertFalse(passed)
        self.assertTrue(any("not at the front end" in r for r in reasons), reasons)

    def test_the_0_1_10_start_passes_orphan_and_only_with_two_voices_in_the_mixer(self):
        self.assertTrue(self.judge(orphan_log(), "orphan")[0])
        self.assertTrue(self.judge(orphan_log(voices=2), "orphan")[0])
        passed, reasons, _ = self.judge(orphan_log(voices=1), "orphan")
        self.assertFalse(passed)
        self.assertTrue(any("at most 1 music buffers at once, the log counts 2" in r for r in reasons), reasons)

    def test_the_mixer_line_must_agree_with_the_logs_own_count(self):
        passed, reasons, _ = self.judge(fixed_log(restart=True, voices=2))
        self.assertFalse(passed)
        self.assertTrue(any("at most 2 music buffers at once, the log counts 1" in r for r in reasons), reasons)

    def test_the_line_with_a_hyphenated_kind_is_read(self):
        match = self.control.LINE.search(audio_obs(1))
        self.assertIsNotNone(match)
        self.assertEqual(match.group(3), "audio-obs")
        facts = self.judge(fixed_log(voices=1))[2]
        self.assertEqual(facts["audio_obs"], {"music_risk_refills": 0, "music_pair_refills": 0, "music_voices_max": 1,
                                              "music_stale_halves": 0})

    def test_a_log_without_the_configuration_line_or_the_seed_fails(self):
        passed, reasons, _ = self.judge(fixed_log(with_configuration=False))
        self.assertFalse(passed)
        self.assertTrue(any("no configuration line" in r for r in reasons), reasons)
        passed, reasons, _ = self.judge(fixed_log(rate=1))
        self.assertFalse(passed)
        self.assertTrue(any("sound_rate=0" in r for r in reasons), reasons)


# (name, the text of the control it changes, what it becomes, the case written for it, the expect it is judged under,
#  and the verdict the control gives that case: the mutant must give the other)
MUTANTS = (
    ("exactly-one-start-again",
     "want(bool(after) and front is not None and after[0] > front,",
     "want(len(after) == 1 and front is not None and after[0] > front,",
     lambda: fixed_log(restart=True, voices=1), "fixed", True),
    ("overlap-allowed",
     'want(facts["max_outstanding_music_starts"] == 1, f"outstanding_music_starts reached {facts[\'max_outstanding_music_starts\']}")',
     'want(True, "")',
     lambda: fixed_log(restart=True, restart_outstanding=2, voices=2), "fixed", False),
    ("mixer-line-ignored",
     'want(obs.get("music_voices_max") == facts["max_outstanding_music_starts"],',
     'want(True or obs.get("music_voices_max") == facts["max_outstanding_music_starts"],',
     lambda: fixed_log(restart=True, voices=2), "fixed", False),
    ("first-start-before-the-front-end-allowed",
     "after[0] > front,",
     "True,",
     lambda: fixed_log(front_end_before_start=False), "fixed", False),
    ("kind-without-the-hyphen",
     "([\\w-]+): (.*)$",
     "(\\w+): (.*)$",
     lambda: fixed_log(voices=2), "fixed", False),
)


class Mutants(unittest.TestCase):
    def test_each_rule_has_a_mutant_its_case_tells_from_the_control(self):
        source = SOURCE.read_text()
        control = load(source)
        for name, old, new, case, expect, verdict in MUTANTS:
            with self.subTest(mutant=name):
                self.assertEqual(source.count(old), 1, f"the text the {name} mutant changes is not unique in the control")
                self.assertEqual(control.judge(case(), expect)[0], verdict, f"the control's verdict on the {name} case")
                mutant = load(source.replace(old, new), name="mutant_" + name.replace("-", "_"))
                self.assertEqual(mutant.judge(case(), expect)[0], not verdict, f"mutant {name} was not caught")


if __name__ == "__main__":
    unittest.main(verbosity=2)
